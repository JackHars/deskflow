/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <cmath>
#include <cstdint>

namespace deskflow::windowshare {
// Private Core -> Host metadata from the sole original-input consumer. A
// window of zero explicitly means unavailable identity. This is never an
// input packet, a native grab receipt, or permission to inject a button.
struct LocalWindowPointer
{
  enum class Kind : std::uint8_t
  {
    Down = 1,
    Up = 2,
    Lost = 3
  };
  Kind kind = Kind::Lost;
  std::uint64_t captureEpoch = 0, sequence = 0, sourceTime = 0, flags = 0;
  std::uint32_t window = 0, clicks = 0;
  std::int64_t eventNumber = 0;
  double x = 0, y = 0;
  bool operator==(const LocalWindowPointer &) const = default;
};
inline bool validLocalWindowPointer(const LocalWindowPointer &value) noexcept
{
  if (value.kind == LocalWindowPointer::Kind::Lost)
    return value == LocalWindowPointer{};
  return (value.kind == LocalWindowPointer::Kind::Down || value.kind == LocalWindowPointer::Kind::Up) &&
         value.captureEpoch && value.sequence && value.sourceTime && value.eventNumber >= 0 && value.clicks <= 32767 &&
         (value.kind != LocalWindowPointer::Kind::Down || value.clicks) && std::isfinite(value.x) &&
         std::isfinite(value.y) && std::abs(value.x) <= 10000000 && std::abs(value.y) <= 10000000;
}
// Separate replay protection from original-input leases. Observation cannot
// advance, reset, prepare, revoke or otherwise own the input lease's counters.
class LocalWindowPointerGate final
{
public:
  bool accept(const LocalWindowPointer &value) noexcept
  {
    if (!validLocalWindowPointer(value))
      return false;
    if (value.kind == LocalWindowPointer::Kind::Lost) {
      m_live = false;
      return true;
    }
    if (!m_live || (m_epoch && m_epoch != value.captureEpoch) || value.sequence <= m_sequence ||
        value.sourceTime <= m_time)
      return false;
    m_epoch = value.captureEpoch;
    m_sequence = value.sequence;
    m_time = value.sourceTime;
    return true;
  }

private:
  std::uint64_t m_epoch = 0, m_sequence = 0, m_time = 0;
  bool m_live = true;
};
} // namespace deskflow::windowshare
