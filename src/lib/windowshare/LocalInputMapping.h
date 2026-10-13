/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace deskflow::windowshare {

// The Viewer endpoint's own TLS connection counter and its approved Mac host
// certificate. Neither is the Host endpoint's independently allocated counter
// or its approved Linux certificate. Only the authenticated, quiesced host may
// attest this tuple to Core; these values do not themselves grant input access.
struct LocalInputTarget
{
  std::uint64_t viewerConnection = 0;
  std::array<std::uint8_t, 32> viewerPeerFingerprint{};
  bool operator==(const LocalInputTarget &) const = default;
};
inline bool validLocalInputTarget(const LocalInputTarget &target) noexcept
{
  return target.viewerConnection &&
         std::any_of(target.viewerPeerFingerprint.begin(), target.viewerPeerFingerprint.end(), [](auto byte) {
           return byte != 0;
         });
}

// Private same-machine IPC only. steady_clock uses the same monotonic epoch in
// both authenticated processes. Carry the absolute deadline unchanged so queue
// transit, authorization callbacks and dispatch cannot renew a native receipt.
// The exact lease and original capture sequence are carried by the same frame.
struct LocalInputMappedPoint
{
  double x = 0, y = 0, width = 0, height = 0;
  std::uint64_t deadlineNs = 0;
  bool operator==(const LocalInputMappedPoint &) const = default;
};
inline std::uint64_t localInputMonotonicNs() noexcept
{
  const auto now =
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  return now > 0 ? static_cast<std::uint64_t>(now) : 0;
}
inline bool validLocalInputMappedPoint(const LocalInputMappedPoint &point) noexcept
{
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.width) &&
         std::isfinite(point.height) && point.x >= 0 && point.y >= 0 && point.width > 0 && point.height > 0 &&
         point.width <= 10000000 && point.height <= 10000000 && point.x < point.width && point.y < point.height &&
         point.deadlineNs;
}
inline bool liveLocalInputMappedPoint(const LocalInputMappedPoint &point) noexcept
{
  const auto now = localInputMonotonicNs();
  return validLocalInputMappedPoint(point) && now && point.deadlineNs > now && point.deadlineNs - now <= 250000000;
}

} // namespace deskflow::windowshare
