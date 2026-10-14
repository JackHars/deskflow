/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "WindowDragEdge.h"
#include <algorithm>
#include <array>

namespace deskflow::windowshare {
// Private Core -> Host drag coordination. No command transfers a held press or
// posts input. Drop names the real release joined by the sole Core consumer;
// preview readiness alone never grants source mutation. Core TLS counters are scoped
// by the authenticated private process incarnation, never by a screen name.
struct LocalWindowDrag
{
  enum class Kind : std::uint8_t
  {
    Begin = 1,
    Motion,
    Hold,
    Cancel,
    Drop,
    Finish
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
  std::uint64_t releaseSequence = 0, releaseTime = 0;
  // Nonzero only for a return; names a Host-issued native move observation.
  std::uint64_t returnOffer = 0;
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
    value.releaseSequence = other.releaseSequence;
    value.releaseTime = other.releaseTime;
    return value == other;
  }
};
inline bool validLocalWindowDrag(const LocalWindowDrag &value) noexcept
{
  const auto nonzero = [](const auto &pin) {
    return std::any_of(pin.begin(), pin.end(), [](auto byte) { return byte != 0; });
  };
  const bool release = value.releaseSequence > value.sequence && value.releaseTime > value.pressTime;
  const bool releaseValid =
      value.kind == LocalWindowDrag::Kind::Drop || value.kind == LocalWindowDrag::Kind::Finish ? release
      : value.kind == LocalWindowDrag::Kind::Cancel && release                                 ? true
                                                               : !value.releaseSequence && !value.releaseTime;
  return releaseValid && value.returnOffer <= ((std::uint64_t{1} << 53) - 1) &&
         value.kind >= LocalWindowDrag::Kind::Begin && value.kind <= LocalWindowDrag::Kind::Finish &&
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
      if (m_current->kind == LocalWindowDrag::Kind::Drop && value.kind != LocalWindowDrag::Kind::Cancel &&
          value.kind != LocalWindowDrag::Kind::Finish)
        return false;
      if (value.kind == LocalWindowDrag::Kind::Finish && m_current->kind != LocalWindowDrag::Kind::Drop)
        return false;
      if (value.kind == LocalWindowDrag::Kind::Motion) {
        if (value.sequence <= m_current->sequence)
          return false;
      } else {
        auto expected = *m_current;
        expected.kind = value.kind;
        if (value.kind == LocalWindowDrag::Kind::Drop) {
          expected.releaseSequence = value.releaseSequence;
          expected.releaseTime = value.releaseTime;
        }
        if (value != expected)
          return false;
      }
    }
    if (value.kind == LocalWindowDrag::Kind::Cancel || value.kind == LocalWindowDrag::Kind::Finish) {
      m_retired = value;
      m_current.reset();
    } else
      m_current = value;
    return true;
  }
  bool matchesReply(const LocalWindowDrag &value) const noexcept
  {
    const auto &expected = m_current && m_current->drag == value.drag ? m_current : m_retired;
    if (!expected || !validLocalWindowDrag(value) || !expected->sameDrag(value) || value.sequence > expected->sequence)
      return false;
    if (value.releaseSequence &&
        (value.releaseSequence != expected->releaseSequence || value.releaseTime != expected->releaseTime))
      return false;
    return true;
  }

private:
  std::optional<LocalWindowDrag> m_current;
  std::optional<LocalWindowDrag> m_retired;
  std::uint64_t m_last = 0;
};
struct LocalWindowDragStatus
{
  enum class Kind : std::uint8_t
  {
    Ready = 1,
    Committed,
    Cancelled,
    Finished
  };
  Kind kind = Kind::Cancelled;
  LocalWindowDrag command;
};
inline bool validLocalWindowDragStatus(const LocalWindowDragStatus &value)
{
  return validLocalWindowDrag(value.command) && value.kind >= LocalWindowDragStatus::Kind::Ready &&
         value.kind <= LocalWindowDragStatus::Kind::Finished &&
         (value.kind != LocalWindowDragStatus::Kind::Finished || value.command.kind == LocalWindowDrag::Kind::Finish) &&
         (value.kind != LocalWindowDragStatus::Kind::Committed || value.command.kind == LocalWindowDrag::Kind::Drop) &&
         (value.kind != LocalWindowDragStatus::Kind::Ready ||
          (value.command.kind == LocalWindowDrag::Kind::Begin || value.command.kind == LocalWindowDrag::Kind::Motion ||
           value.command.kind == LocalWindowDrag::Kind::Hold));
}
} // namespace deskflow::windowshare
