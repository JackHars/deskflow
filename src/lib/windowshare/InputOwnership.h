// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#pragma once

#include <cstdint>
#include <map>
#include <optional>

namespace deskflow::windowshare {

enum class InputTarget : std::uint8_t { LocalMac, Ubuntu, RemoteMacWindow };

struct InputOwner {
  InputTarget target = InputTarget::LocalMac;
  std::uint64_t sessionId = 0;
  std::uint64_t focusRevision = 0;
};

class InputOwnership {
public:
  bool setFocus(InputOwner next)
  {
    if (next.focusRevision <= m_focus.focusRevision)
      return false;
    if (next.target == InputTarget::RemoteMacWindow && next.sessionId == 0)
      return false;
    m_focus = next;
    return true;
  }

  const InputOwner &focus() const { return m_focus; }

  // Keep releases pinned to their press destination across focus changes.
  std::optional<InputOwner> press(std::uint32_t code)
  {
    if (m_pressed.count(code))
      return std::nullopt;
    m_pressed.emplace(code, m_focus);
    return m_focus;
  }

  std::optional<InputOwner> release(std::uint32_t code)
  {
    auto it = m_pressed.find(code);
    if (it == m_pressed.end())
      return std::nullopt;
    InputOwner owner = it->second;
    m_pressed.erase(it);
    return owner;
  }

  // On disconnect the caller must emit releases to all returned owners.
  std::map<std::uint32_t, InputOwner> drain()
  {
    auto pending = std::move(m_pressed);
    m_pressed.clear();
    return pending;
  }

private:
  InputOwner m_focus;
  std::map<std::uint32_t, InputOwner> m_pressed;
};

} // namespace deskflow::windowshare
