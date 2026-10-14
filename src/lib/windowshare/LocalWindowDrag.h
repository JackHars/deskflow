/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "WindowDragEdge.h"
#include <algorithm>
#include <array>

namespace deskflow::windowshare {
// Private Core -> Host preparation only. No command transfers a press, posts
// input, releases a native grab or commits a drop. Core TLS counters are scoped
// by the authenticated private process incarnation, never by a screen name.
struct LocalWindowDrag
{
  enum class Kind : std::uint8_t
  {
    Begin = 1,
    Motion,
    Hold,
    Cancel
  };
  Kind kind = Kind::Begin;
  WindowDragEdgeSide side = WindowDragEdgeSide::Left;
  std::uint64_t drag = 0, server = 0, recipient = 0, boundary = 0, captureEpoch = 0;
  std::uint64_t pressSequence = 0, pressTime = 0, sequence = 0, coreConnection = 0;
  std::uint32_t window = 0;
  std::int64_t eventNumber = 0;
  std::array<std::uint8_t, 32> corePin{}, windowSharePin{};
  WindowDragScreen source, destination;
  std::int32_t sourceX = 0, sourceY = 0, destinationX = 0, destinationY = 0;
  bool operator==(const LocalWindowDrag &) const = default;

  bool sameDrag(const LocalWindowDrag &other) const noexcept
  {
    auto value = *this;
    value.kind = other.kind;
    value.sequence = other.sequence;
    value.sourceX = other.sourceX;
    value.sourceY = other.sourceY;
    value.destinationX = other.destinationX;
    value.destinationY = other.destinationY;
    return value == other;
  }
};
inline bool validLocalWindowDrag(const LocalWindowDrag &value) noexcept
{
  const auto nonzero = [](const auto &pin) {
    return std::any_of(pin.begin(), pin.end(), [](auto byte) { return byte != 0; });
  };
  return value.kind >= LocalWindowDrag::Kind::Begin && value.kind <= LocalWindowDrag::Kind::Cancel &&
         value.side >= WindowDragEdgeSide::Left && value.side <= WindowDragEdgeSide::Bottom && value.drag &&
         value.drag <= ((std::uint64_t{1} << 53) - 1) && value.server && value.recipient && value.boundary &&
         value.captureEpoch && value.pressSequence && value.pressTime && value.sequence > value.pressSequence &&
         value.coreConnection && value.window && value.eventNumber >= 0 && nonzero(value.corePin) &&
         nonzero(value.windowSharePin) && value.source.contains(value.sourceX, value.sourceY) &&
         value.destination.contains(value.destinationX, value.destinationY);
}
// Bounded independent replay state. Hold refreshes liveness, never motion or
// the absolute preparation deadline. Cancel must name the exact last update.
class LocalWindowDragGate final
{
public:
  bool accept(const LocalWindowDrag &value) noexcept
  {
    if (!validLocalWindowDrag(value))
      return false;
    if (value.kind == LocalWindowDrag::Kind::Begin) {
      if (m_current || value.drag <= m_last)
        return false;
      m_last = value.drag;
    } else {
      if (!m_current || !m_current->sameDrag(value))
        return false;
      if (value.kind == LocalWindowDrag::Kind::Motion) {
        if (value.sequence <= m_current->sequence)
          return false;
      } else {
        auto expected = *m_current;
        expected.kind = value.kind;
        if (value != expected)
          return false;
      }
    }
    if (value.kind == LocalWindowDrag::Kind::Cancel)
      m_current.reset();
    else
      m_current = value;
    return true;
  }

private:
  std::optional<LocalWindowDrag> m_current;
  std::uint64_t m_last = 0;
};
} // namespace deskflow::windowshare
