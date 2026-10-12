/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <vector>

namespace deskflow::windowshare {

// These types have authority only inside the private inherited local channel.
// They must never be decoded from viewer/network JSON. Values below match the
// public macOS HID constants; the native adapter verifies them at compile time.
namespace originalKey {
inline constexpr std::uint16_t escape = 0x35, rightCommand = 0x36, leftCommand = 0x37, leftShift = 0x38,
                               capsLock = 0x39, leftOption = 0x3a, leftControl = 0x3b, rightShift = 0x3c,
                               rightOption = 0x3d, rightControl = 0x3e, function = 0x3f;
inline constexpr std::uint64_t capsLockFlag = 0x00010000, shiftFlag = 0x00020000, controlFlag = 0x00040000,
                               optionFlag = 0x00080000, commandFlag = 0x00100000, functionFlag = 0x00800000;
inline constexpr std::uint64_t leftControlFlag = 0x00000001, leftShiftFlag = 0x00000002, rightShiftFlag = 0x00000004,
                               leftCommandFlag = 0x00000008, rightCommandFlag = 0x00000010, leftOptionFlag = 0x00000020,
                               rightOptionFlag = 0x00000040, rightControlFlag = 0x00002000;
inline constexpr std::uint64_t capsPhysicalFlags = 0x01000080;
inline constexpr std::uint64_t momentaryFlags = shiftFlag | controlFlag | optionFlag | commandFlag | functionFlag |
                                                leftControlFlag | leftShiftFlag | rightShiftFlag | leftCommandFlag |
                                                rightCommandFlag | leftOptionFlag | rightOptionFlag | rightControlFlag;
inline constexpr std::size_t keyCount = 256;
} // namespace originalKey

enum class OriginalKeyKind : std::uint8_t
{
  Down = 1,
  Up = 2,
  FlagsChanged = 3
};
enum class OriginalKeyTransition : std::uint8_t
{
  Down = 1,
  Up = 2,
  Repeat = 3,
  Toggle = 4
};

struct OriginalKeySample
{
  std::uint64_t sequence = 0, timestampNs = 0, flags = 0;
  std::uint32_t keyboardType = 0;
  std::uint16_t keyCode = 0;
  OriginalKeyKind kind = OriginalKeyKind::Down;
  bool repeat = false;
};

struct OriginalKeyEvent
{
  OriginalKeySample sample;
  OriginalKeyTransition transition = OriginalKeyTransition::Down;
};

bool validOriginalKeySample(const OriginalKeySample &sample) noexcept;
bool validOriginalKeyEvent(const OriginalKeyEvent &event) noexcept;
bool originalKeyIsModifier(std::uint16_t keyCode) noexcept;
std::uint64_t originalKeyModifierFlags(std::uint16_t keyCode) noexcept;
bool originalEmergencyChord(const OriginalKeySample &sample) noexcept;

using OriginalKeySet = std::bitset<originalKey::keyCount>;

// A caller-observed, serialized physical snapshot, not a claim from the network.
// The eventual tap integration must collect this on its capture thread and
// fence events around it. Empty event flags alone do not prove that A is up.
struct OriginalKeyboardSnapshot
{
  std::uint64_t sequence = 0, timestampNs = 0, flags = 0;
  OriginalKeySet down;
};

class OriginalKeyboardTracker final
{
public:
  enum class Status
  {
    Accepted,
    AwaitingNeutral,
    Invalid
  };
  struct Observation
  {
    Status status = Status::AwaitingNeutral;
    std::optional<OriginalKeyEvent> event;
    bool emergency = false;
    bool consumed = false;
  };

  bool observeNeutral(const OriginalKeyboardSnapshot &snapshot) noexcept;
  Observation observe(const OriginalKeySample &sample) noexcept;
  // Lost capture events, tap disablement, or queue overflow require revocation
  // and another observed neutral snapshot. Never infer missing transitions.
  void invalidate() noexcept;
  bool synchronized() const noexcept
  {
    return m_synchronized;
  }
  bool neutral() const noexcept;
  bool emergencyBlocked() const noexcept
  {
    return m_emergencyBlocked;
  }
  std::uint64_t sequence() const noexcept
  {
    return m_sequence;
  }
  std::uint64_t timestampNs() const noexcept
  {
    return m_timestamp;
  }
  const OriginalKeySet &down() const noexcept
  {
    return m_down;
  }

private:
  OriginalKeySet m_down;
  std::uint64_t m_sequence = 0, m_timestamp = 0, m_flags = 0;
  bool m_synchronized = false, m_emergencyBlocked = false;
};

using OriginalKeyOwner = std::uint64_t;

struct OriginalKeyDelivery
{
  OriginalKeyOwner owner = 0;
  std::uint64_t pressSequence = 0;
  OriginalKeyEvent event;
};

// A forced release is separate from captured events: it neither invents nor
// consumes a capture sequence. The owner maps to an immutable recipient/lease
// incarnation, never a PID, window pointer, or reusable connection name.
struct OriginalKeyRelease
{
  OriginalKeyOwner owner = 0;
  OriginalKeySample press;
  std::uint64_t flags = 0;
};

class OriginalKeyLedger final
{
public:
  struct Result
  {
    std::optional<OriginalKeyDelivery> delivery;
    std::vector<OriginalKeyRelease> releases;
    bool conflict = false;
  };

  // Feed every normalized transition, including pass-through or consumed
  // presses with proposedOwner=0. Repeats and ups use the original down's owner.
  // New ownership is refused while another recipient/local press remains down.
  // Any conflicting modifier state drains delivered presses instead of copying
  // another owner's modifiers into the current target.
  Result route(const OriginalKeyEvent &event, OriginalKeyOwner proposedOwner);
  std::vector<OriginalKeyRelease> revoke(OriginalKeyOwner owner);
  std::vector<OriginalKeyRelease> revokeAll();
  // Emergency integration: revokeAll(), route the consumed Escape with owner
  // zero, and keep feeding consumed transitions. Tombstones survive until ups.
  // Only a newly observed neutral snapshot may clear unobserved orphan ups.
  bool clearAtNeutral(const OriginalKeyboardTracker &tracker) noexcept;
  bool empty() const noexcept;
  std::size_t deliveredCount() const noexcept;

private:
  struct Press
  {
    OriginalKeyOwner owner = 0;
    OriginalKeySample sample;
  };
  std::uint64_t flagsFor(OriginalKeyOwner owner) const noexcept;
  bool canAcquire(OriginalKeyOwner owner) const noexcept;
  std::vector<OriginalKeyRelease> releaseMatching(OriginalKeyOwner owner, bool all);
  std::array<std::optional<Press>, originalKey::keyCount> m_pressed;
  std::uint64_t m_sequence = 0, m_timestamp = 0, m_flags = 0;
};

} // namespace deskflow::windowshare
