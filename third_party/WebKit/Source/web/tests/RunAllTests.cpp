/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "base/bind.h"
#include "base/run_loop.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "base/test/test_suite.h"
#include "base/threading/thread_task_runner_handle.h"
#include "bindings/core/v8/V8GCController.h"
#include "content/test/blink_test_environment.h"
#include "mojo/edk/embedder/embedder.h"
#include "platform/heap/ThreadState.h"
#include "platform/weborigin/SchemeRegistry.h"
#include <v8.h>

namespace {

int runHelper(base::TestSuite* testSuite) {
  content::SetUpBlinkTestEnvironment();
  blink::SchemeRegistry::initialize();

  int result = testSuite->Run();

  // Tickle EndOfTaskRunner which among other things will flush the queue
  // of error messages via V8Initializer::reportRejectedPromisesOnMainThread.
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                base::Bind(&base::DoNothing));
  base::RunLoop().RunUntilIdle();

  // Collect garbage (including threadspecific persistent handles) in order
  // to release mock objects referred from v8 or Oilpan heap. Otherwise false
  // mock leaks will be reported.
  blink::V8GCController::collectAllGarbageForTesting(v8::Isolate::GetCurrent());

  content::TearDownBlinkTestEnvironment();

  return result;
}

}  // namespace

int main(int argc, char** argv) {
  mojo::edk::Init();

  base::TestSuite testSuite(argc, argv);
  return base::LaunchUnitTests(
      argc, argv, base::Bind(&runHelper, base::Unretained(&testSuite)));
}
