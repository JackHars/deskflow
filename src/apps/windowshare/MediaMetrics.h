/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace deskflow::windowshare {
// GUI-thread diagnostics, enabled explicitly before the first frame. These
// measure receiveFrame -> completed Qt paint on ONE local monotonic clock.
// They are not capture timestamps, compositor presentation or input receipts.
struct MediaMetrics
{
  using Clock = std::chrono::steady_clock;
  std::uint64_t received = 0, admitted = 0, retired = 0, ignored = 0;
  std::uint64_t decoded = 0, decodeErrors = 0, painted = 0, replacedBeforePaint = 0;
  std::uint64_t discardedCompletions = 0, emptyDecodes = 0;
  std::uint64_t hardwareDecoded = 0, reusedHardwareTransfers = 0;
  std::uint64_t overflows = 0, recoveryRequests = 0;
  std::uint64_t pendingFrames = 0, pendingBytes = 0, peakFrames = 0, peakBytes = 0;
  std::uint64_t frameCapacity = 0, byteCapacity = 0, maxReceiveToPaintNs = 0;
  std::uint64_t timedRetirements = 0, invalidTimings = 0;
  std::uint64_t maxDecodeQueueNs = 0, maxDecodeWorkNs = 0, maxCompletionQueueNs = 0;
  // Upper bounds 1, 2, ... 65536 milliseconds; the final bucket is overflow.
  // Fixed storage even during a long soak. Repaints of one image count once.
  std::array<std::uint64_t, 18> receiveToPaint{};
  void completion(
      Clock::time_point received, Clock::time_point started, Clock::time_point finished, Clock::time_point consumed
  )
  {
    // These are elapsed wall times, including descheduling and native waits;
    // a worker interval is not an attribution of CPU time to FFmpeg.
    if (started < received || finished < started || consumed < finished) {
      ++invalidTimings;
      return;
    }
    const auto ns = [](auto interval) {
      return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(interval).count());
    };
    ++timedRetirements;
    maxDecodeQueueNs = std::max(maxDecodeQueueNs, ns(started - received));
    maxDecodeWorkNs = std::max(maxDecodeWorkNs, ns(finished - started));
    maxCompletionQueueNs = std::max(maxCompletionQueueNs, ns(consumed - finished));
  }
  void paint(std::uint64_t ns)
  {
    ++painted;
    maxReceiveToPaintNs = std::max(maxReceiveToPaintNs, ns);
    std::size_t bucket = 0;
    while (bucket + 1 < receiveToPaint.size() && ns > (1000000ULL << bucket))
      ++bucket;
    ++receiveToPaint[bucket];
  }
  std::optional<std::uint64_t> percentileUpperNs(unsigned percent) const
  {
    if (!painted || !percent || percent > 100)
      return {};
    const auto rank = (painted / 100) * percent + ((painted % 100) * percent + 99) / 100;
    std::uint64_t count = 0;
    for (std::size_t i = 0; i < receiveToPaint.size(); ++i) {
      count += receiveToPaint[i];
      if (count >= rank)
        return i + 1 == receiveToPaint.size() ? std::nullopt : std::optional(1000000ULL << i);
    }
    return {};
  }
};
} // namespace deskflow::windowshare
