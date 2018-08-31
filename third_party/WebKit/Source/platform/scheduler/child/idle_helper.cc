// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/scheduler/child/idle_helper.h"

#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_event_argument.h"
#include "platform/scheduler/base/real_time_domain.h"
#include "public/platform/scheduler/base/task_queue.h"
#include "platform/scheduler/base/task_queue_manager.h"
#include "platform/scheduler/child/scheduler_helper.h"
#include "platform/scheduler/child/scheduler_tqm_delegate.h"

namespace blink {
namespace scheduler {

IdleHelper::IdleHelper(
    SchedulerHelper* helper,
    Delegate* delegate,
    const char* tracing_category,
    const char* disabled_by_default_tracing_category,
    const char* idle_period_tracing_name,
    base::TimeDelta required_quiescence_duration_before_long_idle_period)
    : helper_(helper),
      delegate_(delegate),
      idle_queue_(helper_->NewTaskQueue(TaskQueue::Spec("idle_tq"))),
      state_(helper,
             delegate,
             tracing_category,
             disabled_by_default_tracing_category,
             idle_period_tracing_name),
      required_quiescence_duration_before_long_idle_period_(
          required_quiescence_duration_before_long_idle_period),
      disabled_by_default_tracing_category_(
          disabled_by_default_tracing_category),
      weak_factory_(this) {
  weak_idle_helper_ptr_ = weak_factory_.GetWeakPtr();
  enable_next_long_idle_period_closure_.Reset(
      base::Bind(&IdleHelper::EnableLongIdlePeriod, weak_idle_helper_ptr_));
  on_idle_task_posted_closure_.Reset(base::Bind(
      &IdleHelper::OnIdleTaskPostedOnMainThread, weak_idle_helper_ptr_));

  idle_task_runner_ = make_scoped_refptr(
      new SingleThreadIdleTaskRunner(idle_queue_, this, tracing_category));

  idle_queue_->SetQueueEnabled(false);
  idle_queue_->SetQueuePriority(TaskQueue::BEST_EFFORT_PRIORITY);

  helper_->AddTaskObserver(this);
}

IdleHelper::~IdleHelper() {
  helper_->RemoveTaskObserver(this);
}

IdleHelper::Delegate::Delegate() {}

IdleHelper::Delegate::~Delegate() {}

scoped_refptr<SingleThreadIdleTaskRunner> IdleHelper::IdleTaskRunner() {
  helper_->CheckOnValidThread();
  return idle_task_runner_;
}

IdleHelper::IdlePeriodState IdleHelper::ComputeNewLongIdlePeriodState(
    const base::TimeTicks now,
    base::TimeDelta* next_long_idle_period_delay_out) {
  helper_->CheckOnValidThread();

  if (!delegate_->CanEnterLongIdlePeriod(now,
                                         next_long_idle_period_delay_out)) {
    return IdlePeriodState::NOT_IN_IDLE_PERIOD;
  }

  base::TimeTicks next_pending_delayed_task;
  base::TimeDelta max_long_idle_period_duration =
      base::TimeDelta::FromMilliseconds(kMaximumIdlePeriodMillis);
  base::TimeDelta long_idle_period_duration;
  if (helper_->real_time_domain()->NextScheduledRunTime(
          &next_pending_delayed_task)) {
    // Limit the idle period duration to be before the next pending task.
    long_idle_period_duration = std::min(next_pending_delayed_task - now,
                                         max_long_idle_period_duration);
  } else {
    long_idle_period_duration = max_long_idle_period_duration;
  }

  if (long_idle_period_duration >=
      base::TimeDelta::FromMilliseconds(kMinimumIdlePeriodDurationMillis)) {
    *next_long_idle_period_delay_out = long_idle_period_duration;
    if (!idle_queue_->HasPendingImmediateWork()) {
      return IdlePeriodState::IN_LONG_IDLE_PERIOD_PAUSED;
    } else if (long_idle_period_duration == max_long_idle_period_duration) {
      return IdlePeriodState::IN_LONG_IDLE_PERIOD_WITH_MAX_DEADLINE;
    } else {
      return IdlePeriodState::IN_LONG_IDLE_PERIOD;
    }
  } else {
    // If we can't start the idle period yet then try again after wakeup.
    *next_long_idle_period_delay_out = base::TimeDelta::FromMilliseconds(
        kRetryEnableLongIdlePeriodDelayMillis);
    return IdlePeriodState::NOT_IN_IDLE_PERIOD;
  }
}

bool IdleHelper::ShouldWaitForQuiescence() {
  helper_->CheckOnValidThread();

  if (helper_->IsShutdown())
    return false;

  if (required_quiescence_duration_before_long_idle_period_ ==
      base::TimeDelta()) {
    return false;
  }

  bool system_is_quiescent = helper_->GetAndClearSystemIsQuiescentBit();
  TRACE_EVENT1(disabled_by_default_tracing_category_, "ShouldWaitForQuiescence",
               "system_is_quiescent", system_is_quiescent);
  return !system_is_quiescent;
}

void IdleHelper::EnableLongIdlePeriod() {
  TRACE_EVENT0(disabled_by_default_tracing_category_, "EnableLongIdlePeriod");
  helper_->CheckOnValidThread();
  if (helper_->IsShutdown())
    return;

  // End any previous idle period.
  EndIdlePeriod();

  if (ShouldWaitForQuiescence()) {
    helper_->ControlTaskRunner()->PostDelayedTask(
        FROM_HERE, enable_next_long_idle_period_closure_.callback(),
        required_quiescence_duration_before_long_idle_period_);
    delegate_->IsNotQuiescent();
    return;
  }

  base::TimeTicks now(helper_->scheduler_tqm_delegate()->NowTicks());
  base::TimeDelta next_long_idle_period_delay;
  IdlePeriodState new_idle_period_state =
      ComputeNewLongIdlePeriodState(now, &next_long_idle_period_delay);
  if (IsInIdlePeriod(new_idle_period_state)) {
    StartIdlePeriod(new_idle_period_state, now,
                    now + next_long_idle_period_delay);
  } else {
    // Otherwise wait for the next long idle period delay before trying again.
    helper_->ControlTaskRunner()->PostDelayedTask(
        FROM_HERE, enable_next_long_idle_period_closure_.callback(),
        next_long_idle_period_delay);
  }
}

void IdleHelper::StartIdlePeriod(IdlePeriodState new_state,
                                 base::TimeTicks now,
                                 base::TimeTicks idle_period_deadline) {
  DCHECK_GT(idle_period_deadline, now);
  helper_->CheckOnValidThread();
  DCHECK(IsInIdlePeriod(new_state));

  base::TimeDelta idle_period_duration(idle_period_deadline - now);
  if (idle_period_duration <
      base::TimeDelta::FromMilliseconds(kMinimumIdlePeriodDurationMillis)) {
    TRACE_EVENT1(disabled_by_default_tracing_category_,
                 "NotStartingIdlePeriodBecauseDeadlineIsTooClose",
                 "idle_period_duration_ms",
                 idle_period_duration.InMillisecondsF());
    return;
  }

  TRACE_EVENT0(disabled_by_default_tracing_category_, "StartIdlePeriod");
  idle_queue_->SetQueueEnabled(true);
  // Use a fence to make sure any idle tasks posted after this point do not run
  // until the next idle period.
  idle_queue_->InsertFence();

  state_.UpdateState(new_state, idle_period_deadline, now);
}

void IdleHelper::EndIdlePeriod() {
  helper_->CheckOnValidThread();
  TRACE_EVENT0(disabled_by_default_tracing_category_, "EndIdlePeriod");

  enable_next_long_idle_period_closure_.Cancel();
  on_idle_task_posted_closure_.Cancel();

  // If we weren't already within an idle period then early-out.
  if (!IsInIdlePeriod(state_.idle_period_state()))
    return;

  idle_queue_->SetQueueEnabled(false);
  state_.UpdateState(IdlePeriodState::NOT_IN_IDLE_PERIOD, base::TimeTicks(),
                     base::TimeTicks());
}

void IdleHelper::WillProcessTask(const base::PendingTask& pending_task) {}

void IdleHelper::DidProcessTask(const base::PendingTask& pending_task) {
  helper_->CheckOnValidThread();
  TRACE_EVENT0(disabled_by_default_tracing_category_, "DidProcessTask");
  if (IsInIdlePeriod(state_.idle_period_state()) &&
      state_.idle_period_state() !=
          IdlePeriodState::IN_LONG_IDLE_PERIOD_PAUSED &&
      helper_->scheduler_tqm_delegate()->NowTicks() >=
          state_.idle_period_deadline()) {
    // If the idle period deadline has now been reached, either end the idle
    // period or trigger a new long-idle period.
    if (IsInLongIdlePeriod(state_.idle_period_state())) {
      EnableLongIdlePeriod();
    } else {
      DCHECK(IdlePeriodState::IN_SHORT_IDLE_PERIOD ==
             state_.idle_period_state());
      EndIdlePeriod();
    }
  }
}

void IdleHelper::UpdateLongIdlePeriodStateAfterIdleTask() {
  helper_->CheckOnValidThread();
  DCHECK(IsInLongIdlePeriod(state_.idle_period_state()));
  TRACE_EVENT0(disabled_by_default_tracing_category_,
               "UpdateLongIdlePeriodStateAfterIdleTask");

  if (!idle_queue_->HasPendingImmediateWork()) {
    // If there are no more idle tasks then pause long idle period ticks until a
    // new idle task is posted.
    state_.UpdateState(IdlePeriodState::IN_LONG_IDLE_PERIOD_PAUSED,
                       state_.idle_period_deadline(), base::TimeTicks());
  } else if (idle_queue_->BlockedByFence()) {
    // If there is still idle work to do then just start the next idle period.
    base::TimeDelta next_long_idle_period_delay;
    if (state_.idle_period_state() ==
        IdlePeriodState::IN_LONG_IDLE_PERIOD_WITH_MAX_DEADLINE) {
      // If we are in a max deadline long idle period then start the next
      // idle period immediately.
      next_long_idle_period_delay = base::TimeDelta();
    } else {
      // Otherwise ensure that we kick the scheduler at the right time to
      // initiate the next idle period.
      next_long_idle_period_delay = std::max(
          base::TimeDelta(), state_.idle_period_deadline() -
                                 helper_->scheduler_tqm_delegate()->NowTicks());
    }
    if (next_long_idle_period_delay.is_zero()) {
      EnableLongIdlePeriod();
    } else {
      helper_->ControlTaskRunner()->PostDelayedTask(
          FROM_HERE, enable_next_long_idle_period_closure_.callback(),
          next_long_idle_period_delay);
    }
  }
}

base::TimeTicks IdleHelper::CurrentIdleTaskDeadline() const {
  helper_->CheckOnValidThread();
  return state_.idle_period_deadline();
}

void IdleHelper::OnIdleTaskPosted() {
  TRACE_EVENT0(disabled_by_default_tracing_category_, "OnIdleTaskPosted");
  if (idle_task_runner_->RunsTasksOnCurrentThread()) {
    OnIdleTaskPostedOnMainThread();
  } else {
    helper_->ControlTaskRunner()->PostTask(
        FROM_HERE, on_idle_task_posted_closure_.callback());
  }
}

void IdleHelper::OnIdleTaskPostedOnMainThread() {
  TRACE_EVENT0(disabled_by_default_tracing_category_,
               "OnIdleTaskPostedOnMainThread");
  if (state_.idle_period_state() ==
      IdlePeriodState::IN_LONG_IDLE_PERIOD_PAUSED) {
    // Restart long idle period ticks.
    helper_->ControlTaskRunner()->PostTask(
        FROM_HERE, enable_next_long_idle_period_closure_.callback());
  }
}

base::TimeTicks IdleHelper::WillProcessIdleTask() {
  helper_->CheckOnValidThread();
  state_.TraceIdleIdleTaskStart();
  return CurrentIdleTaskDeadline();
}

void IdleHelper::DidProcessIdleTask() {
  helper_->CheckOnValidThread();
  state_.TraceIdleIdleTaskEnd();
  if (IsInLongIdlePeriod(state_.idle_period_state())) {
    UpdateLongIdlePeriodStateAfterIdleTask();
  }
}

// static
bool IdleHelper::IsInIdlePeriod(IdlePeriodState state) {
  return state != IdlePeriodState::NOT_IN_IDLE_PERIOD;
}

// static
bool IdleHelper::IsInLongIdlePeriod(IdlePeriodState state) {
  return state == IdlePeriodState::IN_LONG_IDLE_PERIOD ||
         state == IdlePeriodState::IN_LONG_IDLE_PERIOD_WITH_MAX_DEADLINE ||
         state == IdlePeriodState::IN_LONG_IDLE_PERIOD_PAUSED;
}

bool IdleHelper::CanExceedIdleDeadlineIfRequired() const {
  TRACE_EVENT0(disabled_by_default_tracing_category_,
               "CanExceedIdleDeadlineIfRequired");
  helper_->CheckOnValidThread();
  return state_.idle_period_state() ==
         IdlePeriodState::IN_LONG_IDLE_PERIOD_WITH_MAX_DEADLINE;
}

IdleHelper::IdlePeriodState IdleHelper::SchedulerIdlePeriodState() const {
  return state_.idle_period_state();
}

IdleHelper::State::State(SchedulerHelper* helper,
                         Delegate* delegate,
                         const char* tracing_category,
                         const char* disabled_by_default_tracing_category,
                         const char* idle_period_tracing_name)
    : helper_(helper),
      delegate_(delegate),
      idle_period_state_(IdlePeriodState::NOT_IN_IDLE_PERIOD),
      idle_period_trace_event_started_(false),
      running_idle_task_for_tracing_(false),
      tracing_category_(tracing_category),
      disabled_by_default_tracing_category_(
          disabled_by_default_tracing_category),
      idle_period_tracing_name_(idle_period_tracing_name) {}

IdleHelper::State::~State() {}

IdleHelper::IdlePeriodState IdleHelper::State::idle_period_state() const {
  helper_->CheckOnValidThread();
  return idle_period_state_;
}

base::TimeTicks IdleHelper::State::idle_period_deadline() const {
  helper_->CheckOnValidThread();
  return idle_period_deadline_;
}

void IdleHelper::State::UpdateState(IdlePeriodState new_state,
                                    base::TimeTicks new_deadline,
                                    base::TimeTicks optional_now) {
  IdlePeriodState old_idle_period_state = idle_period_state_;

  helper_->CheckOnValidThread();
  if (new_state == idle_period_state_) {
    DCHECK_EQ(new_deadline, idle_period_deadline_);
    return;
  }

  bool is_tracing;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(tracing_category_, &is_tracing);
  if (is_tracing) {
    base::TimeTicks now(optional_now.is_null()
                            ? helper_->scheduler_tqm_delegate()->NowTicks()
                            : optional_now);
    TraceEventIdlePeriodStateChange(new_state, running_idle_task_for_tracing_,
                                    idle_period_deadline_, now);
  }

  idle_period_state_ = new_state;
  idle_period_deadline_ = new_deadline;

  // Inform the delegate if we are starting or ending an idle period.
  if (IsInIdlePeriod(new_state) && !IsInIdlePeriod(old_idle_period_state)) {
    delegate_->OnIdlePeriodStarted();
  } else if (!IsInIdlePeriod(new_state) &&
             IsInIdlePeriod(old_idle_period_state)) {
    delegate_->OnIdlePeriodEnded();
  }
}

void IdleHelper::State::TraceIdleIdleTaskStart() {
  helper_->CheckOnValidThread();

  bool is_tracing;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(tracing_category_, &is_tracing);
  if (is_tracing) {
    TraceEventIdlePeriodStateChange(idle_period_state_, true,
                                    idle_period_deadline_,
                                    base::TimeTicks::Now());
  }
}

void IdleHelper::State::TraceIdleIdleTaskEnd() {
  helper_->CheckOnValidThread();

  bool is_tracing;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(tracing_category_, &is_tracing);
  if (is_tracing) {
    TraceEventIdlePeriodStateChange(idle_period_state_, false,
                                    idle_period_deadline_,
                                    base::TimeTicks::Now());
  }
}

void IdleHelper::State::TraceEventIdlePeriodStateChange(
    IdlePeriodState new_state,
    bool new_running_idle_task,
    base::TimeTicks new_deadline,
    base::TimeTicks now) {
  TRACE_EVENT2(disabled_by_default_tracing_category_, "SetIdlePeriodState",
               "old_state",
               IdleHelper::IdlePeriodStateToString(idle_period_state_),
               "new_state", IdleHelper::IdlePeriodStateToString(new_state));

  if (idle_period_trace_event_started_ && running_idle_task_for_tracing_ &&
      !new_running_idle_task) {
    running_idle_task_for_tracing_ = false;
    if (!idle_period_deadline_.is_null() && now > idle_period_deadline_) {
      TRACE_EVENT_ASYNC_STEP_INTO_WITH_TIMESTAMP0(
          tracing_category_, idle_period_tracing_name_, this, "DeadlineOverrun",
          std::max(idle_period_deadline_, last_idle_task_trace_time_));
    }
  }

  if (IsInIdlePeriod(new_state)) {
    if (!idle_period_trace_event_started_) {
      idle_period_trace_event_started_ = true;
      TRACE_EVENT_ASYNC_BEGIN1(tracing_category_, idle_period_tracing_name_,
                               this, "idle_period_length_ms",
                               (new_deadline - now).ToInternalValue());
    }

    if (new_running_idle_task) {
      last_idle_task_trace_time_ = now;
      running_idle_task_for_tracing_ = true;
      TRACE_EVENT_ASYNC_STEP_INTO0(tracing_category_, idle_period_tracing_name_,
                                   this, "RunningIdleTask");
    } else if (new_state == IdlePeriodState::IN_SHORT_IDLE_PERIOD) {
      TRACE_EVENT_ASYNC_STEP_INTO0(tracing_category_, idle_period_tracing_name_,
                                   this, "ShortIdlePeriod");
    } else if (IsInLongIdlePeriod(new_state) &&
               new_state != IdlePeriodState::IN_LONG_IDLE_PERIOD_PAUSED) {
      TRACE_EVENT_ASYNC_STEP_INTO0(tracing_category_, idle_period_tracing_name_,
                                   this, "LongIdlePeriod");
    } else if (new_state == IdlePeriodState::IN_LONG_IDLE_PERIOD_PAUSED) {
      TRACE_EVENT_ASYNC_STEP_INTO0(tracing_category_, idle_period_tracing_name_,
                                   this, "LongIdlePeriodPaused");
    }
  } else if (idle_period_trace_event_started_) {
    idle_period_trace_event_started_ = false;
    TRACE_EVENT_ASYNC_END0(tracing_category_, idle_period_tracing_name_, this);
  }
}

// static
const char* IdleHelper::IdlePeriodStateToString(
    IdlePeriodState idle_period_state) {
  switch (idle_period_state) {
    case IdlePeriodState::NOT_IN_IDLE_PERIOD:
      return "not_in_idle_period";
    case IdlePeriodState::IN_SHORT_IDLE_PERIOD:
      return "in_short_idle_period";
    case IdlePeriodState::IN_LONG_IDLE_PERIOD:
      return "in_long_idle_period";
    case IdlePeriodState::IN_LONG_IDLE_PERIOD_WITH_MAX_DEADLINE:
      return "in_long_idle_period_with_max_deadline";
    case IdlePeriodState::IN_LONG_IDLE_PERIOD_PAUSED:
      return "in_long_idle_period_paused";
    default:
      NOTREACHED();
      return nullptr;
  }
}

}  // namespace scheduler
}  // namespace blink
