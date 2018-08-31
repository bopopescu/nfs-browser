// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VRFieldOfView_h
#define VRFieldOfView_h

#include "bindings/core/v8/ScriptWrappable.h"
#include "platform/heap/Handle.h"
#include "wtf/Forward.h"

namespace blink {

class VRFieldOfView final : public GarbageCollected<VRFieldOfView>,
                            public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  VRFieldOfView()
      : m_upDegrees(0.0),
        m_downDegrees(0.0),
        m_leftDegrees(0.0),
        m_rightDegrees(0.0) {}

  VRFieldOfView(double upDegrees,
                double rightDegrees,
                double downDegrees,
                double leftDegrees)
      : m_upDegrees(0.0),
        m_downDegrees(0.0),
        m_leftDegrees(0.0),
        m_rightDegrees(0.0) {}

  explicit VRFieldOfView(const VRFieldOfView& fov)
      : m_upDegrees(fov.m_upDegrees),
        m_downDegrees(fov.m_downDegrees),
        m_leftDegrees(fov.m_leftDegrees),
        m_rightDegrees(fov.m_rightDegrees) {}

  double upDegrees() const { return m_upDegrees; }
  double downDegrees() const { return m_downDegrees; }
  double leftDegrees() const { return m_leftDegrees; }
  double rightDegrees() const { return m_rightDegrees; }

  void setUpDegrees(double value) { m_upDegrees = value; }
  void setDownDegrees(double value) { m_downDegrees = value; }
  void setLeftDegrees(double value) { m_leftDegrees = value; }
  void setRightDegrees(double value) { m_rightDegrees = value; }

  DEFINE_INLINE_TRACE() {}

 private:
  double m_upDegrees;
  double m_downDegrees;
  double m_leftDegrees;
  double m_rightDegrees;
};

}  // namespace blink

#endif  // VRFieldOfView_h
