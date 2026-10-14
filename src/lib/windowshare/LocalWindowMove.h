/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "WindowDragEdge.h"
#include <algorithm>
#include <array>

namespace deskflow::windowshare {
// Host -> Core metadata for one exact exported window's native Viewer move.
// window is resolved locally by Host, never accepted from a network event.
// It is not evidence identifying the physical device that initiated the grab.
struct LocalWindowMove
{
  enum class Phase : std::uint8_t
  {
    Begin = 1,
    Motion,
    End,
    Cancel
  };
  Phase phase = Phase::Cancel;
  std::uint32_t window = 0;
  std::uint64_t offer = 0, session = 0, epoch = 0, geometry = 0, grab = 0, sequence = 0;
  std::array<std::uint8_t, 32> peer{};
  std::int32_t x = 0, y = 0;
  WindowDragScreen frame;
  bool operator==(const LocalWindowMove &) const = default;
  bool sameMove(const LocalWindowMove &other) const noexcept
  {
    auto copy = *this;
    copy.phase = other.phase;
    copy.sequence = other.sequence;
    copy.x = other.x;
    copy.y = other.y;
    copy.frame.x = other.frame.x;
    copy.frame.y = other.frame.y;
    return copy == other;
  }
};
inline bool validLocalWindowMove(const LocalWindowMove &value)
{
  const auto id = [](auto n) { return n && n <= ((std::uint64_t{1} << 53) - 1); };
  return value.phase >= LocalWindowMove::Phase::Begin && value.phase <= LocalWindowMove::Phase::Cancel &&
         value.window && id(value.offer) && id(value.session) && id(value.epoch) && id(value.geometry) &&
         id(value.grab) && id(value.sequence) && value.frame.width > 0 && value.frame.height > 0 &&
         std::int64_t(value.frame.x) + value.frame.width - 1 <= INT32_MAX &&
         std::int64_t(value.frame.y) + value.frame.height - 1 <= INT32_MAX &&
         std::any_of(value.peer.begin(), value.peer.end(), [](auto b) { return b != 0; });
}
class LocalWindowMoveGate final
{
public:
  bool accept(const LocalWindowMove &value)
  {
    if (!validLocalWindowMove(value))
      return false;
    if (value.phase == LocalWindowMove::Phase::Begin) {
      if (m_active || value.offer <= m_last)
        return false;
      m_last = value.offer;
    } else if (
        !m_active || !value.sameMove(*m_active) || value.sequence < m_active->sequence ||
        (value.sequence == m_active->sequence && value.phase != LocalWindowMove::Phase::Cancel)
    )
      return false;
    m_active = value;
    if (value.phase == LocalWindowMove::Phase::End || value.phase == LocalWindowMove::Phase::Cancel)
      m_active.reset();
    return true;
  }

private:
  std::uint64_t m_last = 0;
  std::optional<LocalWindowMove> m_active;
};
} // namespace deskflow::windowshare
