/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "OriginalKeyboard.h"

#include <algorithm>
#include <utility>

namespace deskflow::windowshare {
namespace {
struct Modifier
{
  std::uint16_t code;
  std::uint64_t aggregate, side;
};

constexpr std::array modifiers{
    Modifier{originalKey::leftShift, originalKey::shiftFlag, originalKey::leftShiftFlag},
    Modifier{originalKey::rightShift, originalKey::shiftFlag, originalKey::rightShiftFlag},
    Modifier{originalKey::leftControl, originalKey::controlFlag, originalKey::leftControlFlag},
    Modifier{originalKey::rightControl, originalKey::controlFlag, originalKey::rightControlFlag},
    Modifier{originalKey::leftOption, originalKey::optionFlag, originalKey::leftOptionFlag},
    Modifier{originalKey::rightOption, originalKey::optionFlag, originalKey::rightOptionFlag},
    Modifier{originalKey::leftCommand, originalKey::commandFlag, originalKey::leftCommandFlag},
    Modifier{originalKey::rightCommand, originalKey::commandFlag, originalKey::rightCommandFlag},
    Modifier{originalKey::function, originalKey::functionFlag, originalKey::functionFlag}
};

const Modifier *modifier(std::uint16_t keyCode) noexcept
{
  const auto found = std::find_if(modifiers.begin(), modifiers.end(), [keyCode](const Modifier &item) {
    return item.code == keyCode;
  });
  return found == modifiers.end() ? nullptr : &*found;
}

std::uint64_t heldFlags(const OriginalKeySet &keys) noexcept
{
  std::uint64_t flags = 0;
  for (const auto &item : modifiers) {
    if (keys[item.code])
      flags |= item.aggregate | item.side;
  }
  return flags;
}

bool consistentModifierFlags(std::uint64_t flags) noexcept
{
  constexpr std::pair<std::uint64_t, std::uint64_t> groups[]{
      {originalKey::shiftFlag, originalKey::leftShiftFlag | originalKey::rightShiftFlag},
      {originalKey::controlFlag, originalKey::leftControlFlag | originalKey::rightControlFlag},
      {originalKey::optionFlag, originalKey::leftOptionFlag | originalKey::rightOptionFlag},
      {originalKey::commandFlag, originalKey::leftCommandFlag | originalKey::rightCommandFlag}
  };
  for (const auto &[aggregate, sides] : groups) {
    if (bool(flags & aggregate) != bool(flags & sides))
      return false;
  }
  return true;
}
} // namespace

bool originalKeyIsModifier(std::uint16_t keyCode) noexcept
{
  return modifier(keyCode) != nullptr;
}

std::uint64_t originalKeyModifierFlags(std::uint16_t keyCode) noexcept
{
  const auto *item = modifier(keyCode);
  return item ? item->aggregate | item->side : 0;
}

bool validOriginalKeySample(const OriginalKeySample &sample) noexcept
{
  if (!sample.sequence || !sample.timestampNs || sample.keyCode >= originalKey::keyCount)
    return false;
  const bool flagsKey = originalKeyIsModifier(sample.keyCode) || sample.keyCode == originalKey::capsLock;
  switch (sample.kind) {
  case OriginalKeyKind::Down:
    return !flagsKey;
  case OriginalKeyKind::Up:
    return !flagsKey && !sample.repeat;
  case OriginalKeyKind::FlagsChanged:
    return flagsKey && !sample.repeat;
  }
  return false;
}

bool validOriginalKeyEvent(const OriginalKeyEvent &event) noexcept
{
  const auto &sample = event.sample;
  if (!validOriginalKeySample(sample) || !consistentModifierFlags(sample.flags))
    return false;
  switch (sample.kind) {
  case OriginalKeyKind::Down:
    return event.transition == (sample.repeat ? OriginalKeyTransition::Repeat : OriginalKeyTransition::Down);
  case OriginalKeyKind::Up:
    return event.transition == OriginalKeyTransition::Up;
  case OriginalKeyKind::FlagsChanged:
    if (sample.keyCode == originalKey::capsLock)
      return event.transition == OriginalKeyTransition::Toggle;
    if (const auto *item = modifier(sample.keyCode)) {
      const bool down = (sample.flags & item->side) != 0;
      if (down && !(sample.flags & item->aggregate))
        return false;
      return event.transition == (down ? OriginalKeyTransition::Down : OriginalKeyTransition::Up);
    }
    return false;
  }
  return false;
}

bool originalEmergencyChord(const OriginalKeySample &sample) noexcept
{
  constexpr auto required = originalKey::controlFlag | originalKey::optionFlag | originalKey::shiftFlag;
  return validOriginalKeySample(sample) && sample.kind == OriginalKeyKind::Down &&
         sample.keyCode == originalKey::escape && !sample.repeat && (sample.flags & required) == required;
}

bool OriginalKeyboardTracker::observeNeutral(const OriginalKeyboardSnapshot &snapshot) noexcept
{
  if (!snapshot.timestampNs || snapshot.sequence < m_sequence || snapshot.timestampNs < m_timestamp ||
      snapshot.down.any() || (snapshot.flags & (originalKey::momentaryFlags | originalKey::capsPhysicalFlags)))
    return false;
  m_down.reset();
  m_sequence = snapshot.sequence;
  m_timestamp = snapshot.timestampNs;
  m_flags = snapshot.flags;
  m_synchronized = true;
  m_emergencyBlocked = false;
  return true;
}

void OriginalKeyboardTracker::invalidate() noexcept
{
  m_synchronized = false;
}

bool OriginalKeyboardTracker::neutral() const noexcept
{
  return m_synchronized && m_down.none() && !(m_flags & (originalKey::momentaryFlags | originalKey::capsPhysicalFlags));
}

OriginalKeyboardTracker::Observation OriginalKeyboardTracker::observe(const OriginalKeySample &sample) noexcept
{
  Observation result;
  const bool fresh = sample.sequence > m_sequence && sample.timestampNs >= m_timestamp;
  result.emergency = fresh && originalEmergencyChord(sample);
  if (result.emergency)
    m_emergencyBlocked = true;
  result.consumed = m_emergencyBlocked;
  if (!validOriginalKeySample(sample) || !fresh) {
    // Keep the high-water marks, including a rejected future event. A stale
    // snapshot must not undo a detected discontinuity.
    m_sequence = std::max(m_sequence, sample.sequence);
    m_timestamp = std::max(m_timestamp, sample.timestampNs);
    invalidate();
    result.status = Status::Invalid;
    return result;
  }
  m_sequence = sample.sequence;
  m_timestamp = sample.timestampNs;
  if (!m_synchronized)
    return result;

  OriginalKeyEvent event{sample};
  auto next = m_down;
  bool consistent = true;
  switch (sample.kind) {
  case OriginalKeyKind::Down:
    event.transition = sample.repeat ? OriginalKeyTransition::Repeat : OriginalKeyTransition::Down;
    consistent = next[sample.keyCode] == sample.repeat;
    next.set(sample.keyCode);
    break;
  case OriginalKeyKind::Up:
    event.transition = OriginalKeyTransition::Up;
    consistent = next[sample.keyCode];
    next.reset(sample.keyCode);
    break;
  case OriginalKeyKind::FlagsChanged:
    if (sample.keyCode == originalKey::capsLock) {
      // Some keyboards report both changes to the lock state and stateless
      // Caps physical flags. Both are state updates, never owned key presses.
      event.transition = OriginalKeyTransition::Toggle;
    } else {
      const auto *item = modifier(sample.keyCode); // validated above
      const bool down = (sample.flags & item->side) != 0;
      consistent = next[sample.keyCode] != down;
      event.transition = down ? OriginalKeyTransition::Down : OriginalKeyTransition::Up;
      next.set(sample.keyCode, down);
    }
    break;
  }
  // Aggregate-only Shift/Control/etc is ambiguous when both sides may be held.
  // Reject it, as well as unreported changes to any other modifier. A complete
  // stream with device-side flags gives one unambiguous physical transition.
  consistent = consistent && heldFlags(next) == (sample.flags & originalKey::momentaryFlags);
  if (!consistent || !validOriginalKeyEvent(event)) {
    invalidate();
    result.status = Status::Invalid;
    return result;
  }
  m_down = next;
  m_flags = sample.flags;
  result.status = Status::Accepted;
  result.event = event;
  return result;
}

std::uint64_t OriginalKeyLedger::flagsFor(OriginalKeyOwner owner) const noexcept
{
  std::uint64_t flags = m_flags & ~(originalKey::momentaryFlags | originalKey::capsPhysicalFlags);
  for (std::size_t code = 0; code < m_pressed.size(); ++code) {
    const auto &press = m_pressed[code];
    if (press && owner && press->owner == owner)
      flags |= originalKeyModifierFlags(static_cast<std::uint16_t>(code));
  }
  return flags;
}

bool OriginalKeyLedger::canAcquire(OriginalKeyOwner owner) const noexcept
{
  return owner && std::all_of(m_pressed.begin(), m_pressed.end(), [owner](const auto &press) {
           return !press || press->owner == owner;
         });
}

OriginalKeyLedger::Result OriginalKeyLedger::route(const OriginalKeyEvent &event, OriginalKeyOwner proposedOwner)
{
  Result result;
  const auto &sample = event.sample;
  if (!validOriginalKeyEvent(event) || sample.sequence <= m_sequence || sample.timestampNs < m_timestamp) {
    m_sequence = std::max(m_sequence, sample.sequence);
    m_timestamp = std::max(m_timestamp, sample.timestampNs);
    result.conflict = true;
    result.releases = revokeAll();
    return result;
  }
  m_sequence = sample.sequence;
  m_timestamp = sample.timestampNs;
  m_flags = sample.flags;
  auto &stored = m_pressed[sample.keyCode];
  OriginalKeyOwner owner = proposedOwner;
  std::uint64_t pressSequence = sample.sequence;
  std::optional<Press> released;

  switch (event.transition) {
  case OriginalKeyTransition::Down:
    if (stored) {
      result.conflict = true;
      result.releases = revokeAll();
      return result;
    }
    if (owner && !canAcquire(owner)) {
      result.conflict = true;
      result.releases = revokeAll();
      owner = 0;
    }
    stored = Press{owner, sample};
    if (!owner && originalKeyIsModifier(sample.keyCode) && deliveredCount()) {
      result.conflict = true;
      result.releases = revokeAll();
    }
    break;
  case OriginalKeyTransition::Repeat:
  case OriginalKeyTransition::Up:
    if (!stored) {
      result.conflict = true;
      result.releases = revokeAll();
      return result;
    }
    owner = stored->owner;
    pressSequence = stored->sample.sequence;
    if (event.transition == OriginalKeyTransition::Up) {
      released = stored;
      stored.reset();
    }
    break;
  case OriginalKeyTransition::Toggle:
    if (owner && !canAcquire(owner)) {
      result.conflict = true;
      result.releases = revokeAll();
      owner = 0;
    }
    break;
  }

  if (!owner)
    return result;
  if ((flagsFor(owner) & originalKey::momentaryFlags) != (sample.flags & originalKey::momentaryFlags)) {
    result.conflict = true;
    // The up removed its ledger slot, but its original recipient still needs a
    // release. Use only modifiers that recipient actually received.
    if (released)
      result.releases.push_back({owner, released->sample, flagsFor(owner)});
    auto remaining = revokeAll();
    result.releases.insert(result.releases.end(), remaining.begin(), remaining.end());
    return result;
  }
  result.delivery = OriginalKeyDelivery{owner, pressSequence, event};
  return result;
}

std::vector<OriginalKeyRelease> OriginalKeyLedger::releaseMatching(OriginalKeyOwner owner, bool all)
{
  std::vector<OriginalKeyRelease> releases;
  // Release ordinary keys before modifier keys. Each release's flags describe
  // the owner's remaining delivered modifiers, including another held side.
  for (const bool modifiersOnly : {false, true}) {
    for (std::size_t code = 0; code < m_pressed.size(); ++code) {
      auto &press = m_pressed[code];
      if (!press || !press->owner || (!all && press->owner != owner) ||
          originalKeyIsModifier(static_cast<std::uint16_t>(code)) != modifiersOnly)
        continue;
      const auto old = *press;
      press->owner = 0; // tombstone until its physical up
      releases.push_back({old.owner, old.sample, flagsFor(old.owner)});
    }
  }
  return releases;
}

std::vector<OriginalKeyRelease> OriginalKeyLedger::revoke(OriginalKeyOwner owner)
{
  return owner ? releaseMatching(owner, false) : std::vector<OriginalKeyRelease>{};
}

std::vector<OriginalKeyRelease> OriginalKeyLedger::revokeAll()
{
  return releaseMatching(0, true);
}

bool OriginalKeyLedger::clearAtNeutral(const OriginalKeyboardTracker &tracker) noexcept
{
  if (!tracker.neutral() || tracker.emergencyBlocked() || deliveredCount() || tracker.sequence() < m_sequence ||
      tracker.timestampNs() < m_timestamp)
    return false;
  for (auto &press : m_pressed)
    press.reset();
  return true;
}

bool OriginalKeyLedger::empty() const noexcept
{
  return std::none_of(m_pressed.begin(), m_pressed.end(), [](const auto &press) { return press.has_value(); });
}

std::size_t OriginalKeyLedger::deliveredCount() const noexcept
{
  return static_cast<std::size_t>(std::count_if(m_pressed.begin(), m_pressed.end(), [](const auto &press) {
    return press && press->owner;
  }));
}

} // namespace deskflow::windowshare
