/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalKeyboard.h"
#include <array>
#include <atomic>
#include <memory>
#include <variant>

namespace deskflow::windowshare {

inline constexpr std::size_t originalInputButtonCount = 32;
using OriginalInputButtons = std::bitset<originalInputButtonCount>;

struct OriginalInputKey
{
  std::uint32_t keyboardType = 0;
  std::uint16_t code = 0;
  OriginalKeyKind kind = OriginalKeyKind::Down;
  OriginalKeyTransition transition = OriginalKeyTransition::Down;
  bool repeat = false;
};
struct OriginalInputMotion
{
  std::int64_t dx = 0, dy = 0;
  // 32 means ordinary motion. A drag names the button whose press owns it.
  std::uint8_t dragButton = originalInputButtonCount;
};
struct OriginalInputButton
{
  std::uint8_t button = 0;
  bool down = false;
  std::uint32_t clickCount = 1;
};
enum class OriginalScrollPhase : std::uint8_t
{
  None = 0,
  Began = 1,
  Changed = 2,
  Ended = 4,
  Cancelled = 8,
  MayBegin = 128
};
enum class OriginalMomentumPhase : std::uint8_t
{
  None = 0,
  Began = 1,
  Continued = 2,
  Ended = 3
};
struct OriginalInputScroll
{
  // Horizontal, vertical; preserve all three representations without scaling.
  std::array<std::int64_t, 2> lines{}, points{}, fixed16_16{};
  OriginalScrollPhase phase = OriginalScrollPhase::None;
  OriginalMomentumPhase momentum = OriginalMomentumPhase::None;
  bool continuous = false;
};
enum class OriginalInputBoundary : std::uint8_t
{
  Checkpoint,
  Discontinuity,
  Emergency
};
struct OriginalInputRecord
{
  std::uint64_t epoch = 0, sequence = 0, sourceTimestampNs = 0, flags = 0;
  std::variant<OriginalInputKey, OriginalInputMotion, OriginalInputButton, OriginalInputScroll, OriginalInputBoundary>
      payload;
};

// Local, event-time pointer metadata. These are Quartz observations, not a
// titlebar hit test, a native grab owner or permission to redirect a press.
// Keep them outside OriginalInputRecord: they must not be reconstructed on the
// receiver or serialized as authority in the original-input wire protocol.
struct OriginalPointerObservation
{
  std::uint64_t epoch = 0, sequence = 0, sourceTimestampNs = 0;
  double x = 0, y = 0;
  std::uint32_t windowUnderPointer = 0, windowHandlingPointer = 0;
  std::int64_t eventNumber = 0, sourceProcess = 0, sourceState = 0;

  // Zero window IDs mean unavailable. They never identify the desktop, the
  // foreground window, or a replacement object discovered by a later query.
  bool matches(const OriginalInputRecord &record) const noexcept;
};

bool validOriginalInputRecord(const OriginalInputRecord &record) noexcept;
std::optional<OriginalKeyEvent> originalInputKeyEvent(const OriginalInputRecord &record) noexcept;

enum class OriginalInputRevocation : std::uint8_t
{
  None,
  Overflow,
  Emergency,
  Discontinuity,
  Exhausted,
  Stopped
};
class OriginalInputStream;
class OriginalInputNeutralBarrier;
class OriginalInputCutover;

// Continuity evidence only: neither TLS authentication nor input permission.
// Copies remain revoked after stream destruction. Check again at each use.
class OriginalInputEpoch final
{
  friend class OriginalInputStream;
  friend class OriginalInputNeutralBarrier;
  friend class OriginalInputCutover;
  friend class OriginalInputPressLedger;
  struct State;

public:
  OriginalInputEpoch() = default;
  std::uint64_t id() const noexcept;
  bool live() const noexcept;
  bool sameEpoch(const OriginalInputEpoch &other) const noexcept;
  OriginalInputRevocation reason() const noexcept;
  bool emergencyRequested() const noexcept;

private:
  explicit OriginalInputEpoch(std::shared_ptr<State> state);
  std::uint64_t publishedSequence() const noexcept;
  void revoke(OriginalInputRevocation reason) const noexcept;
  std::shared_ptr<State> m_state;
};

struct OriginalInputDelivery
{
  OriginalInputRecord record;
  OriginalInputEpoch epoch;
  bool live() const noexcept;
};

// A single-use local checkpoint agreement. Even successful commit establishes
// no OS snapshot/tap fence, source ownership, or permission to post input.
// It does not inspect delivered press ledgers: external admission must also
// complete owner releases and verify those ledgers are empty before cutover.
class OriginalInputCutover final
{
  friend class OriginalInputNeutralBarrier;
  friend class OriginalInputStream;
  struct State;

public:
  bool pending() const noexcept;
  bool committed() const noexcept;
  std::uint64_t request() const noexcept;
  std::uint64_t sequence() const noexcept;

private:
  explicit OriginalInputCutover(std::shared_ptr<State> state);
  std::shared_ptr<State> m_state;
};

// Exactly one producer calls push()/commitCutover(), and exactly one consumer
// calls pop(). revoke() and retained evidence queries are thread-safe. Destroy
// only after both workers stop. The producer never waits or allocates here.
// One instance is one epoch; there is no reset, overwrite, or coalescing API.
class OriginalInputStream final
{
  friend struct OriginalInputTestAccess;

public:
  static constexpr std::size_t capacity = 256;
  OriginalInputStream();
  ~OriginalInputStream();
  OriginalInputStream(const OriginalInputStream &) = delete;
  OriginalInputStream &operator=(const OriginalInputStream &) = delete;
  const OriginalInputEpoch &epoch() const noexcept;
  bool push(const OriginalInputRecord &record) noexcept;
  // Empty is not physical neutrality. After revocation pop returns no records;
  // the consumer must still revoke its delivered owners through their ledgers.
  std::optional<OriginalInputDelivery> pop() noexcept;
  void revoke(OriginalInputRevocation reason) noexcept;
  // Producer only. The consumer must have consumed the checkpoint, and no
  // further record may have been published. Cancellation can win the CAS.
  bool commitCutover(const OriginalInputCutover &cutover) noexcept;

private:
  static std::uint64_t reserveEpoch(std::atomic<std::uint64_t> &next) noexcept;
  OriginalInputEpoch m_epoch;
  std::array<OriginalInputRecord, capacity> m_records;
  std::atomic<std::uint64_t> m_written{0}, m_read{0};
  std::uint64_t m_lastSequence = 0, m_lastTimestamp = 0, m_lastCutoverSequence = 0, m_lastCutoverRequest = 0;
};

enum class OriginalScrollState : std::uint8_t
{
  Unknown,
  Idle,
  Gesture,
  PendingMomentum,
  Momentum
};

// TRUSTED PROVIDER BOUNDARY. This is an assertion about a supported physical
// domain, not a structure that may be filled from network input or tracker
// defaults. The provider must establish complete coverage AND the ordering of
// this observation against the named checkpoint, including queued OS events.
// Merely delivering a checkpoint, scanning zero flags, or waiting proves none
// of that. No native provider or such OS fence is implemented by this module.
// Assertions cannot erase known ordinary key/button holds without captured
// transitions. Caps Toggle is direction-ambiguous: it invalidates physical Caps
// knowledge, which only a fresh trusted observation can then re-establish.
struct OriginalInputPhysicalObservation
{
  std::uint64_t checkpoint = 0, sourceTimestampNs = 0, flags = 0, coverageGeneration = 0;
  OriginalKeySet knownKeys, downKeys;
  OriginalInputButtons knownButtons, downButtons;
  OriginalScrollState scroll = OriginalScrollState::Unknown;
  bool flagsKnown = false;
};

// Consumer-thread state only. Every delivered record must be observed, even
// outside a route. Unknown-owner bookkeeping never substitutes for neutrality.
class OriginalInputNeutralBarrier final
{
public:
  enum class State
  {
    Unknown,
    Busy,
    Neutral,
    Revoked
  };
  explicit OriginalInputNeutralBarrier(OriginalInputEpoch epoch);
  ~OriginalInputNeutralBarrier();
  OriginalInputNeutralBarrier(const OriginalInputNeutralBarrier &) = delete;
  OriginalInputNeutralBarrier &operator=(const OriginalInputNeutralBarrier &) = delete;
  bool observe(const OriginalInputDelivery &delivery) noexcept;
  bool observeTrustedPhysical(const OriginalInputPhysicalObservation &observation) noexcept;
  State state() const noexcept;
  OriginalScrollState scrollState() const noexcept;
  std::optional<OriginalInputCutover> prepare(std::uint64_t request);
  void cancelPrepared() noexcept;

private:
  bool invalidate() noexcept;
  void observeScroll(const OriginalInputScroll &scroll) noexcept;
  OriginalInputEpoch m_epoch;
  OriginalKeySet m_knownKeys, m_downKeys;
  OriginalInputButtons m_knownButtons, m_downButtons;
  OriginalScrollState m_scroll = OriginalScrollState::Unknown;
  std::uint64_t m_sequence = 0, m_timestamp = 0, m_flags = 0, m_coverage = 0, m_observedCheckpoint = 0,
                m_lastRequest = 0;
  bool m_checkpoint = false, m_completeCoverage = false, m_flagsKnown = false, m_scrollMayBegin = false;
  std::shared_ptr<OriginalInputCutover::State> m_prepared;
};

using OriginalInputOwner = OriginalKeyOwner;
struct OriginalInputPressDelivery
{
  OriginalInputOwner owner = 0;
  std::uint64_t pressSequence = 0;
  OriginalInputDelivery input;
};
struct OriginalInputRelease
{
  OriginalInputOwner owner = 0;
  OriginalInputRecord press;
  std::uint64_t flags = 0;
};

// Consumer-side key/button ownership bookkeeping, not an admission gate. Feed
// pass-through presses with owner zero as well. Revoke returns release intents
// for delivered owners and retains physical tombstones until their ups. This
// layer deliberately does not authorize motion/scroll delivery or implement a
// scroll-owner ledger. Release vectors allocate here, never on the producer.
class OriginalInputPressLedger final
{
public:
  struct Result
  {
    std::optional<OriginalInputPressDelivery> delivery;
    std::vector<OriginalInputRelease> releases;
    bool conflict = false;
  };
  explicit OriginalInputPressLedger(OriginalInputEpoch epoch);
  Result observe(const OriginalInputDelivery &input, OriginalInputOwner proposedOwner);
  std::vector<OriginalInputRelease> revoke(OriginalInputOwner owner);
  std::vector<OriginalInputRelease> revokeAll();
  bool empty() const noexcept;
  std::size_t deliveredCount() const noexcept;

private:
  struct Press
  {
    OriginalInputOwner owner = 0;
    OriginalInputRecord record;
  };
  bool canAcquire(OriginalInputOwner owner) const noexcept;
  std::uint64_t flagsFor(OriginalInputOwner owner) const noexcept;
  void appendReleases(std::vector<OriginalInputRelease> &releases, OriginalInputOwner owner, bool all) noexcept;
  std::vector<OriginalInputRelease> releaseMatching(OriginalInputOwner owner, bool all);
  OriginalInputEpoch m_epoch;
  std::array<std::optional<Press>, originalKey::keyCount + originalInputButtonCount> m_pressed;
  std::uint64_t m_sequence = 0, m_timestamp = 0, m_flags = 0;
};

} // namespace deskflow::windowshare
