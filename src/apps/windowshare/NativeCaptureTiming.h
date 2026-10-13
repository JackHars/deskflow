/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <CoreMedia/CoreMedia.h>
#include <optional>

namespace deskflow::windowshare::native {
// Capture serial queue only. Real sample PTS is retained exactly, including its
// rational timebase. Recovery re-encoding is explicitly synthetic presentation
// scheduling, never a new source-capture or cross-machine latency observation.
class CaptureTimeline final
{
public:
  static bool valid(CMTime time) noexcept
  {
    return CMTIME_IS_NUMERIC(time) && time.timescale > 0 && time.epoch == 0 && CMTimeCompare(time, kCMTimeZero) >= 0;
  }
  bool observeSource(CMTime time) noexcept
  {
    if (!valid(time) || (valid(m_source) && CMTimeCompare(time, m_source) <= 0))
      return false;
    m_source = time;
    return true;
  }
  std::optional<CMTime> sourcePresentation(CMTime time) noexcept
  {
    if (!valid(time) || !valid(m_source) || CMTimeCompare(time, m_source) != 0 ||
        (valid(m_presentation) && CMTimeCompare(time, m_presentation) <= 0))
      return std::nullopt;
    m_presentation = time;
    return time;
  }
  std::optional<CMTime> replayPresentation(unsigned rate) noexcept
  {
    if (!valid(m_source) || !valid(m_presentation) || (rate != 2 && rate != 10 && rate != 30 && rate != 60))
      return std::nullopt;
    // A recovery request repeats an image; it is not another nominal capture
    // interval. Tiny strictly increasing encoder ticks cannot accrue more
    // than one microsecond of lead over the latest real source PTS, even when
    // requests arrive faster than the 2/10/30/60 fps capture cadence. Once this
    // bound is reached, leave the keyframe request pending for a fresh sample.
    const auto base = CMTimeCompare(m_source, m_presentation) > 0 ? m_source : m_presentation;
    const auto time = CMTimeAdd(base, CMTimeMake(1, 1000000000));
    const auto limit = CMTimeAdd(m_source, CMTimeMake(1, 1000000));
    if (!valid(time) || !valid(limit) || CMTimeCompare(time, base) <= 0 || CMTimeCompare(time, limit) > 0)
      return std::nullopt;
    m_presentation = time;
    return time;
  }
  static CMTime duration(CMTime sourceDuration, unsigned rate) noexcept
  {
    if (valid(sourceDuration) && CMTimeCompare(sourceDuration, kCMTimeZero) > 0)
      return sourceDuration;
    if (rate != 2 && rate != 10 && rate != 30 && rate != 60)
      return kCMTimeInvalid;
    return CMTimeMake(1, static_cast<int32_t>(rate));
  }

private:
  CMTime m_source = kCMTimeInvalid, m_presentation = kCMTimeInvalid;
};
} // namespace deskflow::windowshare::native
