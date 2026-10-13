/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

namespace deskflow::windowshare {

// The server owns this state on its event-queue thread. Recipients identify a
// connection incarnation, never a proxy address or a reusable computer name.
// An empty recipient set represents physical input passed through locally, or
// an orphan whose remote owner disconnected. Neither may migrate to new focus.
class InputRouteState final
{
public:
  using Recipient = std::uint64_t;
  using Recipients = std::vector<Recipient>;
  enum class Kind
  {
    Key,
    Button,
    ModifierCommand
  };
  struct Control
  {
    Kind kind;
    std::uint64_t code;
    bool operator<(const Control &other) const
    {
      return kind != other.kind ? kind < other.kind : code < other.code;
    }
  };
  struct Press
  {
    Control control;
    std::uint32_t key = 0, mask = 0, button = 0;
    Recipients recipients;
    // A local dispatcher's immutable owner, never a network-supplied value.
    // Original presses have no legacy recipients. Zero plus no recipients is
    // a local press or a revoked owner's tombstone, both held until physical up.
    std::uint64_t originalOwner = 0;
    // A control can be reused after its up. Retained capture mappings must
    // identify this exact press, including after it becomes a tombstone.
    std::uint64_t incarnation = 0;
  };
  struct ModifierRelease
  {
    Recipient recipient;
    std::uint32_t mask;
  };
  struct Detached
  {
    std::vector<Press> presses;
    std::uint32_t modifiers = 0;
  };
  static constexpr std::size_t maxPresses = 1024;

  static Control keyControl(std::uint32_t key, std::uint32_t button)
  {
    // A physical key's symbol can change under a held modifier, and macOS ups
    // carry kKeyNone. Media keys and configured actions can have button zero.
    return {Kind::Key, button ? button : (std::uint64_t{1} << 32) | key};
  }

  Recipient attach()
  {
    if (m_nextRecipient == std::numeric_limits<Recipient>::max())
      throw std::overflow_error("Input recipient sequence exhausted");
    const Recipient recipient = ++m_nextRecipient;
    m_live.insert(recipient);
    return recipient;
  }

  std::optional<Press>
  down(Control control, std::uint32_t key, std::uint32_t mask, std::uint32_t button, Recipients recipients)
  {
    const auto existing = m_pressed.find(control);
    if (existing != m_pressed.end())
      return existing->second;
    if (m_pressed.size() >= maxPresses || m_nextPress == std::numeric_limits<std::uint64_t>::max())
      return std::nullopt;
    recipients.erase(
        std::remove_if(
            recipients.begin(), recipients.end(),
            [this](Recipient recipient) { return m_live.find(recipient) == m_live.end(); }
        ),
        recipients.end()
    );
    std::sort(recipients.begin(), recipients.end());
    recipients.erase(std::unique(recipients.begin(), recipients.end()), recipients.end());
    Press press{control, key, mask, button, std::move(recipients), 0, ++m_nextPress};
    m_pressed.emplace(control, press);
    return press;
  }

  // Call only after the dispatcher has reserved delivery capacity. Like a
  // legacy down, this cannot replace the owner of an already-held control.
  std::optional<Press>
  downOriginal(Control control, std::uint32_t key, std::uint32_t mask, std::uint32_t button, std::uint64_t owner)
  {
    if (!owner || (control.kind != Kind::Key && control.kind != Kind::Button))
      return std::nullopt;
    const auto existing = m_pressed.find(control);
    if (existing != m_pressed.end())
      return existing->second;
    if (m_pressed.size() >= maxPresses || m_nextPress == std::numeric_limits<std::uint64_t>::max())
      return std::nullopt;
    Press press{control, key, mask, button, {}, owner, ++m_nextPress};
    m_pressed.emplace(control, press);
    return press;
  }

  std::set<std::uint64_t> originalOwners() const
  {
    std::set<std::uint64_t> owners;
    for (const auto &[control, press] : m_pressed)
      if (press.originalOwner)
        owners.insert(press.originalOwner);
    return owners;
  }

  // Return exact cleanup ownership before converting to tombstones. Repeated
  // revocation cannot generate duplicate releases or reacquire a legacy owner.
  std::vector<Press> revokeOriginal(std::uint64_t owner)
  {
    std::vector<Press> released;
    if (!owner)
      return released;
    released.reserve(m_pressed.size());
    for (auto &[control, press] : m_pressed) {
      if (press.originalOwner != owner)
        continue;
      released.push_back(press);
      press.originalOwner = 0;
      press.recipients.clear();
    }
    return released;
  }

  std::optional<Press> repeat(Control control) const
  {
    const auto found = m_pressed.find(control);
    return found == m_pressed.end() ? std::nullopt : std::optional<Press>(found->second);
  }

  std::optional<Press> up(Control control)
  {
    const auto found = m_pressed.find(control);
    if (found == m_pressed.end())
      return std::nullopt;
    auto press = std::move(found->second);
    m_pressed.erase(found);
    return press;
  }

  // A key's desired modifier mask can make a client synthesize a modifier even
  // if its physical down happened locally. Track only masks actually sent to a
  // recipient, so the eventual modifier up can clear that paired synthetic
  // state without sending unowned key-ups to whatever happens to have focus.
  void observeModifiers(Recipient recipient, std::uint32_t mask)
  {
    if (mask && m_live.find(recipient) != m_live.end())
      m_observedModifiers[recipient] |= mask;
  }

  void forgetModifiers(Recipient recipient, std::uint32_t mask)
  {
    const auto found = m_observedModifiers.find(recipient);
    if (found == m_observedModifiers.end())
      return;
    found->second &= ~mask;
    if (found->second == 0)
      m_observedModifiers.erase(found);
  }

  std::vector<ModifierRelease> releaseModifiers(std::uint32_t mask)
  {
    std::vector<ModifierRelease> releases;
    for (auto it = m_observedModifiers.begin(); it != m_observedModifiers.end();) {
      const std::uint32_t cleared = it->second & mask;
      if (cleared)
        releases.push_back({it->first, cleared});
      it->second &= ~mask;
      if (it->second == 0)
        it = m_observedModifiers.erase(it);
      else
        ++it;
    }
    return releases;
  }

  Detached detach(Recipient recipient)
  {
    m_live.erase(recipient);
    return releaseRecipient(recipient);
  }

  // Retire only delivered ownership, retaining the connection incarnation.
  // Physical holds remain tombstones until up and cannot acquire the new
  // original-input destination after a full-input handoff.
  Detached releaseRecipient(Recipient recipient)
  {
    Detached result;
    const auto modifiers = m_observedModifiers.find(recipient);
    if (modifiers != m_observedModifiers.end()) {
      result.modifiers = modifiers->second;
      m_observedModifiers.erase(modifiers);
    }
    for (auto &[control, press] : m_pressed) {
      auto owner = std::find(press.recipients.begin(), press.recipients.end(), recipient);
      if (owner == press.recipients.end())
        continue;
      auto release = press;
      release.recipients = {recipient};
      result.presses.push_back(std::move(release));
      press.recipients.erase(owner);
      // Keep a tombstone until the physical up. A repeated down after reconnect
      // must not acquire a new recipient for a press it never received.
    }
    return result;
  }

  std::size_t size() const
  {
    return m_pressed.size();
  }

private:
  Recipient m_nextRecipient = 0;
  std::uint64_t m_nextPress = 0;
  std::set<Recipient> m_live;
  std::map<Control, Press> m_pressed;
  std::map<Recipient, std::uint32_t> m_observedModifiers;
};

} // namespace deskflow::windowshare
