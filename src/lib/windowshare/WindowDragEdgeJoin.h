/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalPointerJoin.h"
#include "WindowDragEdge.h"

namespace deskflow::windowshare {
struct WindowDragEdgeCandidate
{
  WindowDragEdgeMailbox::Observation boundary;
  OriginalPointerJoin::Match press;
  std::shared_ptr<const std::atomic_bool> held;
  bool live() const noexcept
  {
    return boundary.live() && press.live() && held && held->load(std::memory_order_acquire);
  }
  bool current() const noexcept
  {
    return boundary.current() && press.live() && held && held->load(std::memory_order_acquire);
  }
};

// Runs alongside the sole raw consumer. An edge cannot borrow a different
// press, a translated action, a multi-click or an annotation arriving after up.
// This still grants no native titlebar/grab authority and performs no I/O.
class WindowDragEdgeJoin final
{
public:
  WindowDragEdgeJoin() = default;
  WindowDragEdgeJoin(const WindowDragEdgeJoin &) = delete;
  WindowDragEdgeJoin &operator=(const WindowDragEdgeJoin &) = delete;
  ~WindowDragEdgeJoin()
  {
    revoke();
  }
  void revoke() noexcept
  {
    if (m_held)
      m_held->store(false, std::memory_order_release);
    m_held.reset();
    m_press.reset();
    m_rawDown = {};
  }
  void capture(const OriginalInputCaptured &captured) noexcept
  {
    const auto &record = captured.input.record;
    if (!captured.input.live()) {
      revoke();
      return;
    }
    if (const auto *key = std::get_if<OriginalInputKey>(&record.payload);
        key && key->code == originalKey::escape && key->kind == OriginalKeyKind::Down) {
      // Cancellation cannot wait for a stalled Server worker to dispatch Esc.
      revoke();
      return;
    }
    if (const auto *button = std::get_if<OriginalInputButton>(&record.payload)) {
      // Any additional button invalidates this single-button interaction.
      revoke();
      if (button->button == 0 && button->down && button->clickCount == 1)
        m_rawDown = {record.epoch, record.sequence, 1};
    }
  }
  void pointer(const OriginalPointerJoin::Match &match)
  {
    const auto &captured = match.captured;
    const auto &record = captured.input.record;
    const auto *button = std::get_if<OriginalInputButton>(&record.payload);
    if (!match.live() || !match.window() || !button || button->button != 0 || !button->down ||
        button->clickCount != 1 || m_rawDown != OriginalInputStamp{record.epoch, record.sequence, 1} ||
        captured.translation.uncertain || captured.translation.parts != 1 ||
        captured.translation.stamp.epoch != record.epoch || captured.translation.stamp.sequence != record.sequence)
      return;
    if (m_press)
      return;
    m_press = match;
    m_held = std::make_shared<std::atomic_bool>(true);
  }
  std::optional<WindowDragEdgeCandidate> join(const std::optional<WindowDragEdgeMailbox::Observation> &edge) const
  {
    if (!edge || !edge->live() || !m_press || !m_press->live() || !m_press->window() || !m_held ||
        !m_held->load(std::memory_order_acquire) || edge->edge().press != m_rawDown)
      return {};
    return WindowDragEdgeCandidate{*edge, *m_press, m_held};
  }

private:
  OriginalInputStamp m_rawDown;
  std::optional<OriginalPointerJoin::Match> m_press;
  std::shared_ptr<std::atomic_bool> m_held;
};
} // namespace deskflow::windowshare
