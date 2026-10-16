/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "KeyboardRoute.h"

namespace deskflow::windowshare {

// Pure state for NativeHost's network keyboard fence. A caller supplies its
// independently observed focus/geometry and ledger state; this grants no native
// input authority. The host has one focus lease, so an outstanding gate blocks
// all network key downs/repeats, including after focus moves to another window.
// A full-input gate also blocks pointer acquisition, movement and scrolling.
class NativeKeyboardAdmission
{
public:
  bool
  quiesce(const KeyboardRouteBinding &binding, const std::optional<KeyboardRouteBinding> &current, bool ledgerEmpty)
  {
    if (!validKeyboardRouteBinding(binding) || current != binding || !ledgerEmpty)
      return false;
    if (m_gate)
      return *m_gate == binding && !m_invalidated;
    if (m_last && m_last->owner == binding.owner && binding.generation <= m_last->generation)
      return false;
    m_gate = binding;
    m_last = binding;
    m_invalidated = false;
    return true;
  }

  bool quiesced(
      const KeyboardRouteBinding &binding, const std::optional<KeyboardRouteBinding> &current, bool ledgerEmpty
  ) const
  {
    return fenced(binding, ledgerEmpty) && current == binding;
  }

  // Local queue boundary only. This does not observe native focus/geometry and
  // must never replace the fresh candidate required for input authorization.
  bool fenced(const KeyboardRouteBinding &binding, bool ledgerEmpty) const
  {
    return m_gate == binding && !m_invalidated && ledgerEmpty;
  }

  bool blocksKeys() const
  {
    return m_gate.has_value();
  }

  bool blocksPointers() const
  {
    return m_gate && m_gate->scope == ForwardingScope::All;
  }

  // Mark first, notify afterwards. Invalidation retains an exact-binding
  // tombstone even if the associated window has already been returned.
  std::optional<KeyboardRouteBinding> invalidate(std::optional<Owner> owner = std::nullopt)
  {
    if (!m_gate || m_invalidated || (owner && *owner != m_gate->owner))
      return std::nullopt;
    m_invalidated = true;
    return m_gate;
  }

  // The caller must revoke any original-input authority before reopening the
  // network route. Neither an invalidation nor a stale acknowledgment does so.
  bool resume(const KeyboardRouteBinding &binding)
  {
    if (m_gate != binding)
      return false;
    m_gate.reset();
    m_invalidated = false;
    return true;
  }

private:
  std::optional<KeyboardRouteBinding> m_gate, m_last;
  bool m_invalidated = false;
};

} // namespace deskflow::windowshare
