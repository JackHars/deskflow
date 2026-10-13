/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "Core.h"

#include <cmath>
#include <cstdint>

namespace deskflow::windowshare {

// Observation accounting for the opt-in, owned-window AppKit probe. There is
// intentionally no Released/Authorized outcome: AppKit event receipt and
// unchanged frames do not acknowledge consumption by a WindowServer grab.
class NativeWindowDragProbeState final
{
public:
  static constexpr std::uint64_t durationNs = 30000000000ULL;
  static constexpr std::uint64_t beforePostNs = 500000000ULL;
  enum class Outcome
  {
    NoDrag,
    InvalidObservation,
    NoSubmission,
    AppUpOnly,
    NoPostMotion,
    MotionWithoutObservedFrameMovement,
    FrameMovementAfterSubmission
  };
  explicit NativeWindowDragProbeState(std::uint64_t started) : m_started(started)
  {
  }
  bool expired(std::uint64_t now) const
  {
    return now < m_started || now - m_started >= durationNs;
  }
  bool begin(std::uint32_t window, int pid, Rect frame, Point point, std::uint64_t sourceTime, std::uint64_t now)
  {
    if (m_begun || expired(now) || !window || pid <= 0 || !frame.valid() || !finite(point) || !sourceTime ||
        point.x - frame.x < 0 || point.y - frame.y < 0 || point.x - frame.x >= frame.width ||
        point.y - frame.y >= frame.height)
      return false;
    m_begun = true;
    m_window = window;
    m_pid = pid;
    m_initial = frame;
    m_lastPoint = point;
    m_lastSourceTime = sourceTime;
    m_beganAt = now;
    return true;
  }
  void pointer(std::uint64_t serial, std::uint64_t sourceTime, Point point, bool physicalUp, std::uint64_t now)
  {
    if (!m_begun || m_physicalUp || m_invalid || expired(now))
      return;
    // An old callback, an overflowing buffer or a timestamp reversal cannot
    // become fresh motion evidence after the synthetic release submission.
    if (!serial || serial <= m_lastSerial || sourceTime < m_lastSourceTime || !finite(point)) {
      m_invalid = true;
      return;
    }
    m_lastSerial = serial;
    m_lastSourceTime = sourceTime;
    if (physicalUp) {
      m_physicalUp = true;
      return;
    }
    const auto distance = std::hypot(point.x - m_lastPoint.x, point.y - m_lastPoint.y);
    m_lastPoint = point;
    if (!m_posted) {
      m_beforeTravel += distance;
      return;
    }
    if (serial > m_postSerial && sourceTime > m_postSourceTime && distance > 0.25) {
      m_postTravel += distance;
      m_latestMotionSerial = serial;
      ++m_postMotions;
    }
  }
  void frame(std::uint32_t window, int pid, Rect frame, std::uint64_t now)
  {
    if (!m_begun || m_invalid || expired(now))
      return;
    if (window != m_window || pid != m_pid || !frame.valid()) {
      m_invalid = true;
      return;
    }
    if (!m_posted) {
      m_windowMovedBefore = m_windowMovedBefore || changed(frame, m_initial);
      return;
    }
    // Pair each independent frame observation with newer physical pointer
    // motion. Repeated timer ticks with no motion add no evidence, and physical
    // up cannot serve as the evidence that the earlier synthetic up worked.
    if (!m_physicalUp && m_latestMotionSerial > m_lastFramedSerial) {
      m_lastFramedSerial = m_latestMotionSerial;
      ++m_postFrames;
      m_frameMovedAfter = m_frameMovedAfter || changed(frame, m_postFrame);
    }
  }
  bool mayPost(std::uint64_t now) const
  {
    return m_begun && !m_posted && !m_invalid && !m_physicalUp && !expired(now) && now >= m_beganAt &&
           now - m_beganAt >= beforePostNs && m_windowMovedBefore && m_beforeTravel >= 12;
  }
  bool submitted(std::uint64_t serial, std::uint64_t sourceTime, Rect frame, std::uint64_t now)
  {
    if (!mayPost(now) || serial < m_lastSerial || sourceTime < m_lastSourceTime || !frame.valid())
      return false;
    m_posted = true;
    m_postSerial = serial;
    m_postSourceTime = sourceTime;
    m_postFrame = frame;
    return true;
  }
  void appUp()
  {
    if (m_posted)
      m_appUp = true;
  }
  void invalidate()
  {
    m_invalid = true;
  }
  bool begun() const
  {
    return m_begun;
  }
  bool posted() const
  {
    return m_posted;
  }
  bool physicalUp() const
  {
    return m_physicalUp;
  }
  bool appReceivedUp() const
  {
    return m_appUp;
  }
  bool invalid() const
  {
    return m_invalid;
  }
  std::uint64_t postMotions() const
  {
    return m_postMotions;
  }
  std::uint64_t postFrameObservations() const
  {
    return m_postFrames;
  }
  Outcome outcome() const
  {
    if (m_invalid)
      return Outcome::InvalidObservation;
    if (!m_begun)
      return Outcome::NoDrag;
    if (!m_posted)
      return Outcome::NoSubmission;
    if (m_frameMovedAfter)
      return Outcome::FrameMovementAfterSubmission;
    if (m_postFrames >= 3 && m_postMotions >= 3 && m_postTravel >= 24)
      return Outcome::MotionWithoutObservedFrameMovement;
    return m_appUp ? Outcome::AppUpOnly : Outcome::NoPostMotion;
  }
  static const char *name(Outcome outcome)
  {
    switch (outcome) {
    case Outcome::NoDrag:
      return "no-real-drag";
    case Outcome::InvalidObservation:
      return "invalid-observation";
    case Outcome::NoSubmission:
      return "no-targeted-up-submission";
    case Outcome::AppUpOnly:
      return "app-up-received-without-sufficient-motion-observation";
    case Outcome::NoPostMotion:
      return "no-post-submission-motion-evidence";
    case Outcome::MotionWithoutObservedFrameMovement:
      return "pointer-motion-without-observed-window-movement";
    case Outcome::FrameMovementAfterSubmission:
      return "window-movement-observed-after-targeted-up-submission";
    }
    return "invalid-observation";
  }

private:
  static bool finite(Point point)
  {
    return std::isfinite(point.x) && std::isfinite(point.y);
  }
  static bool changed(Rect a, Rect b)
  {
    return std::abs(a.x - b.x) > 0.5 || std::abs(a.y - b.y) > 0.5 || std::abs(a.width - b.width) > 0.5 ||
           std::abs(a.height - b.height) > 0.5;
  }
  std::uint64_t m_started = 0, m_beganAt = 0, m_lastSerial = 0, m_lastSourceTime = 0;
  std::uint64_t m_postSerial = 0, m_postSourceTime = 0, m_latestMotionSerial = 0, m_lastFramedSerial = 0;
  std::uint64_t m_postMotions = 0, m_postFrames = 0;
  std::uint32_t m_window = 0;
  int m_pid = 0;
  Rect m_initial, m_postFrame;
  Point m_lastPoint;
  double m_beforeTravel = 0, m_postTravel = 0;
  bool m_begun = false, m_posted = false, m_physicalUp = false, m_invalid = false;
  bool m_windowMovedBefore = false, m_frameMovedAfter = false, m_appUp = false;
};

} // namespace deskflow::windowshare
