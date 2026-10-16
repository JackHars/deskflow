/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <array>
#include <cstdint>
#include <utility>

// Event-tap-thread state only. A registered shortcut's key must never leak
// into the foreground application while the Server asynchronously switches
// screens. Keep consuming that physical cycle if its modifiers change first.
class HotKeyCapture
{
public:
  struct Result
  {
    bool consumed = false;
    std::uint32_t hotkey = 0;
    bool down = false;
  };
  Result observe(std::uint32_t key, bool down, bool repeat, std::uint32_t registered)
  {
    if (key >= m_keys.size())
      return {};
    auto &held = m_keys[key];
    if (!down) {
      const auto released = std::exchange(held, Press{});
      return {released.consumed, released.notified ? released.hotkey : 0, false};
    }
    if (held.observed)
      return {held.consumed, 0, false};
    // Carbon can consume the initial down before an autorepeat reaches us.
    // Suppress that orphan repeat, but do not invent another shortcut press.
    held = {true, registered != 0, registered != 0 && !repeat, registered};
    return {held.consumed, held.notified ? registered : 0, true};
  }

private:
  struct Press
  {
    bool observed = false, consumed = false, notified = false;
    std::uint32_t hotkey = 0;
  };
  std::array<Press, 256> m_keys{};
};
