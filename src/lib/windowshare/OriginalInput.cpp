/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "OriginalInput.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace deskflow::windowshare {
namespace {
constexpr std::array modifierCodes{originalKey::leftShift,    originalKey::rightShift,   originalKey::leftControl,
                                   originalKey::rightControl, originalKey::leftOption,   originalKey::rightOption,
                                   originalKey::leftCommand,  originalKey::rightCommand, originalKey::function};

bool consistentFlags(std::uint64_t flags) noexcept
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

bool physicalFlagsAgree(const OriginalKeySet &known, const OriginalKeySet &down, std::uint64_t flags) noexcept
{
  constexpr auto aggregate =
      originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag | originalKey::commandFlag;
  for (const auto code : modifierCodes) {
    if (known[code] && down[code] != bool(flags & (originalKeyModifierFlags(code) & ~aggregate)))
      return false;
  }
  return true;
}

bool signed32(std::int64_t value) noexcept
{
  return value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max();
}

bool validPhase(OriginalScrollPhase phase) noexcept
{
  switch (phase) {
  case OriginalScrollPhase::None:
  case OriginalScrollPhase::Began:
  case OriginalScrollPhase::Changed:
  case OriginalScrollPhase::Ended:
  case OriginalScrollPhase::Cancelled:
  case OriginalScrollPhase::MayBegin:
    return true;
  }
  return false;
}

bool validMomentum(OriginalMomentumPhase phase) noexcept
{
  switch (phase) {
  case OriginalMomentumPhase::None:
  case OriginalMomentumPhase::Began:
  case OriginalMomentumPhase::Continued:
  case OriginalMomentumPhase::Ended:
    return true;
  }
  return false;
}

bool validScrollState(OriginalScrollState state) noexcept
{
  switch (state) {
  case OriginalScrollState::Unknown:
  case OriginalScrollState::Idle:
  case OriginalScrollState::Gesture:
  case OriginalScrollState::PendingMomentum:
  case OriginalScrollState::Momentum:
    return true;
  }
  return false;
}

bool emergency(const OriginalInputRecord &record) noexcept
{
  if (const auto *boundary = std::get_if<OriginalInputBoundary>(&record.payload))
    return *boundary == OriginalInputBoundary::Emergency;
  if (const auto key = originalInputKeyEvent(record))
    return originalEmergencyChord(key->sample);
  return false;
}
} // namespace

std::optional<OriginalKeyEvent> originalInputKeyEvent(const OriginalInputRecord &record) noexcept
{
  if (const auto *key = std::get_if<OriginalInputKey>(&record.payload)) {
    return OriginalKeyEvent{
        {record.sequence, record.sourceTimestampNs, record.flags, key->keyboardType, key->code, key->kind, key->repeat},
        key->transition
    };
  }
  return std::nullopt;
}

bool validOriginalInputRecord(const OriginalInputRecord &record) noexcept
{
  if (!record.epoch || !record.sequence || !record.sourceTimestampNs || !consistentFlags(record.flags))
    return false;
  if (const auto key = originalInputKeyEvent(record))
    return validOriginalKeyEvent(*key);
  if (const auto *motion = std::get_if<OriginalInputMotion>(&record.payload))
    return signed32(motion->dx) && signed32(motion->dy) && motion->dragButton <= originalInputButtonCount;
  if (const auto *button = std::get_if<OriginalInputButton>(&record.payload))
    return button->button < originalInputButtonCount && button->clickCount &&
           button->clickCount <= std::numeric_limits<std::int32_t>::max();
  if (const auto *scroll = std::get_if<OriginalInputScroll>(&record.payload)) {
    return validPhase(scroll->phase) && validMomentum(scroll->momentum) &&
           (scroll->phase == OriginalScrollPhase::None || scroll->momentum == OriginalMomentumPhase::None) &&
           (scroll->continuous ||
            (scroll->phase == OriginalScrollPhase::None && scroll->momentum == OriginalMomentumPhase::None)) &&
           std::all_of(scroll->lines.begin(), scroll->lines.end(), signed32) &&
           std::all_of(scroll->points.begin(), scroll->points.end(), signed32) &&
           std::all_of(scroll->fixed16_16.begin(), scroll->fixed16_16.end(), signed32);
  }
  if (const auto *boundary = std::get_if<OriginalInputBoundary>(&record.payload)) {
    switch (*boundary) {
    case OriginalInputBoundary::Checkpoint:
    case OriginalInputBoundary::Discontinuity:
    case OriginalInputBoundary::Emergency:
      return true;
    }
  }
  return false;
}

struct OriginalInputEpoch::State
{
  explicit State(std::uint64_t value) : id(value)
  {
    if (!id)
      reason.store(OriginalInputRevocation::Exhausted);
  }
  const std::uint64_t id;
  std::atomic<OriginalInputRevocation> reason{OriginalInputRevocation::None};
  std::atomic<bool> emergency{false};
  std::atomic<std::uint64_t> published{0};
};

OriginalInputEpoch::OriginalInputEpoch(std::shared_ptr<State> state) : m_state(std::move(state))
{
}

std::uint64_t OriginalInputEpoch::id() const noexcept
{
  return m_state ? m_state->id : 0;
}

bool OriginalInputEpoch::live() const noexcept
{
  return id() && reason() == OriginalInputRevocation::None;
}

bool OriginalInputEpoch::sameEpoch(const OriginalInputEpoch &other) const noexcept
{
  return id() && m_state == other.m_state;
}

OriginalInputRevocation OriginalInputEpoch::reason() const noexcept
{
  return m_state ? m_state->reason.load(std::memory_order_acquire) : OriginalInputRevocation::Stopped;
}

bool OriginalInputEpoch::emergencyRequested() const noexcept
{
  return m_state && m_state->emergency.load(std::memory_order_acquire);
}

std::uint64_t OriginalInputEpoch::publishedSequence() const noexcept
{
  return m_state ? m_state->published.load(std::memory_order_acquire) : 0;
}

void OriginalInputEpoch::revoke(OriginalInputRevocation reason) const noexcept
{
  if (!m_state || reason == OriginalInputRevocation::None)
    return;
  switch (reason) {
  case OriginalInputRevocation::None:
    return;
  case OriginalInputRevocation::Emergency:
    m_state->emergency.store(true, std::memory_order_release);
    break;
  case OriginalInputRevocation::Overflow:
  case OriginalInputRevocation::Discontinuity:
  case OriginalInputRevocation::Exhausted:
  case OriginalInputRevocation::Stopped:
    break;
  default:
    reason = OriginalInputRevocation::Discontinuity;
    break;
  }
  auto expected = OriginalInputRevocation::None;
  m_state->reason.compare_exchange_strong(expected, reason, std::memory_order_acq_rel);
}

bool OriginalInputDelivery::live() const noexcept
{
  return epoch.live() && record.epoch == epoch.id();
}

struct OriginalInputCutover::State
{
  enum class Status
  {
    Prepared,
    Committed,
    Cancelled
  };
  OriginalInputEpoch epoch;
  std::uint64_t sequence, timestamp, request;
  std::atomic<Status> status{Status::Prepared};
  State(OriginalInputEpoch value, std::uint64_t sequenceValue, std::uint64_t timestampValue, std::uint64_t requestValue)
      : epoch(std::move(value)),
        sequence(sequenceValue),
        timestamp(timestampValue),
        request(requestValue)
  {
  }
};

OriginalInputCutover::OriginalInputCutover(std::shared_ptr<State> state) : m_state(std::move(state))
{
}

bool OriginalInputCutover::pending() const noexcept
{
  return m_state && m_state->status.load(std::memory_order_acquire) == State::Status::Prepared &&
         m_state->epoch.live() && m_state->epoch.publishedSequence() == m_state->sequence;
}

bool OriginalInputCutover::committed() const noexcept
{
  return m_state && m_state->status.load(std::memory_order_acquire) == State::Status::Committed &&
         m_state->epoch.live();
}

std::uint64_t OriginalInputCutover::request() const noexcept
{
  return m_state ? m_state->request : 0;
}

std::uint64_t OriginalInputCutover::sequence() const noexcept
{
  return m_state ? m_state->sequence : 0;
}

std::uint64_t OriginalInputStream::reserveEpoch(std::atomic<std::uint64_t> &next) noexcept
{
  auto value = next.load(std::memory_order_relaxed);
  while (value && value != std::numeric_limits<std::uint64_t>::max()) {
    if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
      return value;
  }
  return 0; // Exhaustion is terminal; no wrap or reuse.
}

OriginalInputStream::OriginalInputStream()
    : m_epoch([] {
        static std::atomic<std::uint64_t> next{1};
        return std::make_shared<OriginalInputEpoch::State>(reserveEpoch(next));
      }())
{
  static_assert(std::is_trivially_copyable_v<OriginalInputRecord>);
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
  static_assert(std::atomic<OriginalInputRevocation>::is_always_lock_free);
  static_assert(std::atomic<bool>::is_always_lock_free);
}

OriginalInputStream::~OriginalInputStream()
{
  revoke(OriginalInputRevocation::Stopped);
}

const OriginalInputEpoch &OriginalInputStream::epoch() const noexcept
{
  return m_epoch;
}

bool OriginalInputStream::push(const OriginalInputRecord &record) noexcept
{
  if (record.epoch != m_epoch.id()) {
    revoke(OriginalInputRevocation::Discontinuity);
    return false;
  }
  // The reserved chord must remain visible even with a full/already revoked
  // queue, and even when device-side modifier flags are ambiguous.
  if (emergency(record)) {
    revoke(OriginalInputRevocation::Emergency);
    return false;
  }
  if (!m_epoch.live())
    return false;
  if (const auto *boundary = std::get_if<OriginalInputBoundary>(&record.payload);
      boundary && *boundary == OriginalInputBoundary::Discontinuity) {
    revoke(OriginalInputRevocation::Discontinuity);
    return false;
  }
  if (m_lastSequence == std::numeric_limits<std::uint64_t>::max()) {
    revoke(OriginalInputRevocation::Exhausted);
    return false;
  }
  if (!validOriginalInputRecord(record) || record.sequence != m_lastSequence + 1 ||
      record.sourceTimestampNs < m_lastTimestamp) {
    revoke(OriginalInputRevocation::Discontinuity);
    return false;
  }
  const auto written = m_written.load(std::memory_order_relaxed);
  const auto read = m_read.load(std::memory_order_acquire);
  if (written - read >= capacity) {
    revoke(OriginalInputRevocation::Overflow);
    return false;
  }
  m_records[written % capacity] = record;
  m_lastSequence = record.sequence;
  m_lastTimestamp = record.sourceTimestampNs;
  m_epoch.m_state->published.store(record.sequence, std::memory_order_release);
  m_written.store(written + 1, std::memory_order_release);
  return m_epoch.live();
}

std::optional<OriginalInputDelivery> OriginalInputStream::pop() noexcept
{
  if (!m_epoch.live())
    return std::nullopt;
  const auto read = m_read.load(std::memory_order_relaxed);
  if (read == m_written.load(std::memory_order_acquire))
    return std::nullopt;
  OriginalInputDelivery result{m_records[read % capacity], m_epoch};
  m_read.store(read + 1, std::memory_order_release);
  return result.live() ? std::optional<OriginalInputDelivery>(std::move(result)) : std::nullopt;
}

void OriginalInputStream::revoke(OriginalInputRevocation reason) noexcept
{
  m_epoch.revoke(reason);
}

bool OriginalInputStream::commitCutover(const OriginalInputCutover &cutover) noexcept
{
  const auto &state = cutover.m_state;
  if (!state || !state->epoch.sameEpoch(m_epoch) || !cutover.pending() || state->sequence != m_lastSequence ||
      state->timestamp != m_lastTimestamp || state->sequence <= m_lastCutoverSequence ||
      state->request <= m_lastCutoverRequest ||
      m_written.load(std::memory_order_acquire) != m_read.load(std::memory_order_acquire))
    return false;
  auto expected = OriginalInputCutover::State::Status::Prepared;
  if (!state->status.compare_exchange_strong(
          expected, OriginalInputCutover::State::Status::Committed, std::memory_order_acq_rel
      ))
    return false;
  m_lastCutoverSequence = state->sequence;
  m_lastCutoverRequest = state->request;
  return cutover.committed();
}

OriginalInputNeutralBarrier::OriginalInputNeutralBarrier(OriginalInputEpoch epoch) : m_epoch(std::move(epoch))
{
}

OriginalInputNeutralBarrier::~OriginalInputNeutralBarrier()
{
  cancelPrepared();
}

void OriginalInputNeutralBarrier::cancelPrepared() noexcept
{
  if (m_prepared) {
    auto expected = OriginalInputCutover::State::Status::Prepared;
    m_prepared->status.compare_exchange_strong(
        expected, OriginalInputCutover::State::Status::Cancelled, std::memory_order_acq_rel
    );
    m_prepared.reset();
  }
}

bool OriginalInputNeutralBarrier::invalidate() noexcept
{
  cancelPrepared();
  m_epoch.revoke(OriginalInputRevocation::Discontinuity);
  return false;
}

bool OriginalInputNeutralBarrier::observe(const OriginalInputDelivery &delivery) noexcept
{
  const auto &record = delivery.record;
  if (!delivery.live() || !delivery.epoch.sameEpoch(m_epoch) || !validOriginalInputRecord(record) ||
      m_sequence == std::numeric_limits<std::uint64_t>::max() || record.sequence != m_sequence + 1 ||
      record.sourceTimestampNs < m_timestamp || record.sequence > m_epoch.publishedSequence())
    return invalidate();
  cancelPrepared();
  m_sequence = record.sequence;
  m_timestamp = record.sourceTimestampNs;
  m_checkpoint = false;
  m_flags = record.flags;
  m_flagsKnown = true;
  if (const auto key = originalInputKeyEvent(record)) {
    const auto code = key->sample.keyCode;
    switch (key->transition) {
    case OriginalKeyTransition::Down:
    case OriginalKeyTransition::Repeat:
    case OriginalKeyTransition::Up: {
      const bool wasDown = key->transition != OriginalKeyTransition::Down;
      if (m_knownKeys[code] && m_downKeys[code] != wasDown)
        return invalidate();
      m_knownKeys.set(code);
      m_downKeys.set(code, key->transition != OriginalKeyTransition::Up);
      break;
    }
    case OriginalKeyTransition::Toggle:
      // A lock update does not identify the Caps physical down/up transition.
      // Do not preserve a stale "known up" or guess that a held Caps released.
      m_knownKeys.reset(originalKey::capsLock);
      m_downKeys.reset(originalKey::capsLock);
      m_completeCoverage = false;
      break;
    }
  } else if (const auto *button = std::get_if<OriginalInputButton>(&record.payload)) {
    if (m_knownButtons[button->button] && m_downButtons[button->button] == button->down)
      return invalidate();
    m_knownButtons.set(button->button);
    m_downButtons.set(button->button, button->down);
  } else if (const auto *motion = std::get_if<OriginalInputMotion>(&record.payload)) {
    if (motion->dragButton < originalInputButtonCount) {
      if (m_knownButtons[motion->dragButton] && !m_downButtons[motion->dragButton])
        return invalidate();
      m_knownButtons.set(motion->dragButton);
      m_downButtons.set(motion->dragButton);
    }
  } else if (const auto *scroll = std::get_if<OriginalInputScroll>(&record.payload)) {
    observeScroll(*scroll);
  } else if (const auto *boundary = std::get_if<OriginalInputBoundary>(&record.payload)) {
    if (*boundary != OriginalInputBoundary::Checkpoint)
      return invalidate();
    m_checkpoint = true;
  }
  if (!physicalFlagsAgree(m_knownKeys, m_downKeys, m_flags))
    return invalidate();
  return m_epoch.live();
}

void OriginalInputNeutralBarrier::observeScroll(const OriginalInputScroll &scroll) noexcept
{
  const auto previous = m_scroll;
  const bool mayBegin = m_scrollMayBegin;
  m_scrollMayBegin = false;
  m_scroll = OriginalScrollState::Unknown;
  if (previous == OriginalScrollState::Unknown)
    return;
  if (!scroll.continuous) {
    if (previous == OriginalScrollState::Idle)
      m_scroll = OriginalScrollState::Idle;
    return;
  }
  switch (scroll.phase) {
  case OriginalScrollPhase::MayBegin:
    if (previous == OriginalScrollState::Idle) {
      m_scroll = OriginalScrollState::Gesture;
      m_scrollMayBegin = true;
    }
    return;
  case OriginalScrollPhase::Began:
    if (previous == OriginalScrollState::Idle || (previous == OriginalScrollState::Gesture && mayBegin))
      m_scroll = OriginalScrollState::Gesture;
    return;
  case OriginalScrollPhase::Changed:
    if (previous == OriginalScrollState::Gesture && !mayBegin)
      m_scroll = OriginalScrollState::Gesture;
    return;
  case OriginalScrollPhase::Ended:
    if (previous == OriginalScrollState::Gesture && !mayBegin)
      m_scroll = OriginalScrollState::PendingMomentum;
    return;
  case OriginalScrollPhase::Cancelled:
    return; // A cancellation does not assert there can be no queued momentum.
  case OriginalScrollPhase::None:
    break;
  }
  switch (scroll.momentum) {
  case OriginalMomentumPhase::Began:
    if (previous == OriginalScrollState::PendingMomentum)
      m_scroll = OriginalScrollState::Momentum;
    break;
  case OriginalMomentumPhase::Continued:
    if (previous == OriginalScrollState::Momentum)
      m_scroll = OriginalScrollState::Momentum;
    break;
  case OriginalMomentumPhase::Ended:
    if (previous == OriginalScrollState::Momentum)
      m_scroll = OriginalScrollState::Idle;
    break;
  case OriginalMomentumPhase::None:
    break; // Phase-free continuous input cannot establish gesture completion.
  }
}

bool OriginalInputNeutralBarrier::observeTrustedPhysical(const OriginalInputPhysicalObservation &observation) noexcept
{
  cancelPrepared();
  if (!m_epoch.live() || !m_checkpoint || !observation.checkpoint || observation.checkpoint != m_sequence ||
      observation.checkpoint == m_observedCheckpoint || observation.sourceTimestampNs != m_timestamp ||
      observation.flags != m_flags || !observation.coverageGeneration ||
      (m_coverage && observation.coverageGeneration != m_coverage) || m_epoch.publishedSequence() != m_sequence ||
      (observation.downKeys & ~observation.knownKeys).any() ||
      (observation.downButtons & ~observation.knownButtons).any() || !validScrollState(observation.scroll) ||
      ((observation.downKeys ^ m_downKeys) & observation.knownKeys & m_knownKeys).any() ||
      ((observation.downButtons ^ m_downButtons) & observation.knownButtons & m_knownButtons).any() ||
      !consistentFlags(observation.flags) ||
      (observation.flagsKnown && !physicalFlagsAgree(observation.knownKeys, observation.downKeys, observation.flags)))
    return invalidate();
  // Partial provider coverage cannot erase previously observed holds and then
  // "rediscover" them up at a later checkpoint without their captured release.
  m_downKeys = (m_downKeys & ~observation.knownKeys) | observation.downKeys;
  m_knownKeys |= observation.knownKeys;
  m_downButtons = (m_downButtons & ~observation.knownButtons) | observation.downButtons;
  m_knownButtons |= observation.knownButtons;
  m_flagsKnown = observation.flagsKnown;
  m_completeCoverage = observation.knownKeys.all() && observation.knownButtons.all() && m_flagsKnown;
  m_scroll = observation.scroll;
  m_scrollMayBegin = false;
  m_coverage = observation.coverageGeneration;
  m_observedCheckpoint = observation.checkpoint;
  return m_epoch.live();
}

OriginalInputNeutralBarrier::State OriginalInputNeutralBarrier::state() const noexcept
{
  if (!m_epoch.live())
    return State::Revoked;
  if (m_epoch.publishedSequence() != m_sequence || !m_completeCoverage || !m_flagsKnown ||
      m_scroll == OriginalScrollState::Unknown)
    return State::Unknown;
  if (m_downKeys.any() || m_downButtons.any() ||
      (m_flags & (originalKey::momentaryFlags | originalKey::capsPhysicalFlags)) ||
      m_scroll != OriginalScrollState::Idle)
    return State::Busy;
  return State::Neutral;
}

OriginalScrollState OriginalInputNeutralBarrier::scrollState() const noexcept
{
  return m_epoch.live() ? m_scroll : OriginalScrollState::Unknown;
}

std::optional<OriginalInputCutover> OriginalInputNeutralBarrier::prepare(std::uint64_t request)
{
  if (state() != State::Neutral || !m_checkpoint || m_epoch.publishedSequence() != m_sequence || !request ||
      request <= m_lastRequest)
    return std::nullopt;
  cancelPrepared();
  m_lastRequest = request;
  m_prepared = std::make_shared<OriginalInputCutover::State>(m_epoch, m_sequence, m_timestamp, request);
  return OriginalInputCutover(m_prepared);
}

OriginalInputPressLedger::OriginalInputPressLedger(OriginalInputEpoch epoch) : m_epoch(std::move(epoch))
{
}

bool OriginalInputPressLedger::canAcquire(OriginalInputOwner owner) const noexcept
{
  return owner && std::all_of(m_pressed.begin(), m_pressed.end(), [owner](const auto &press) {
           return !press || press->owner == owner;
         });
}

std::uint64_t OriginalInputPressLedger::flagsFor(OriginalInputOwner owner) const noexcept
{
  auto flags = m_flags & ~(originalKey::momentaryFlags | originalKey::capsPhysicalFlags);
  for (std::size_t code = 0; code < originalKey::keyCount; ++code) {
    if (m_pressed[code] && owner && m_pressed[code]->owner == owner)
      flags |= originalKeyModifierFlags(static_cast<std::uint16_t>(code));
  }
  return flags;
}

OriginalInputPressLedger::Result
OriginalInputPressLedger::observe(const OriginalInputDelivery &input, OriginalInputOwner proposedOwner)
{
  Result result;
  // Allocate the entire bounded result before changing even an up's slot.
  // appendReleases never allocates, including when an up discovers a conflict.
  result.releases.reserve(m_pressed.size());
  const auto &record = input.record;
  if (!input.live() || !input.epoch.sameEpoch(m_epoch) || !validOriginalInputRecord(record) ||
      m_sequence == std::numeric_limits<std::uint64_t>::max() || record.sequence != m_sequence + 1 ||
      record.sourceTimestampNs < m_timestamp || emergency(record)) {
    m_epoch.revoke(emergency(record) ? OriginalInputRevocation::Emergency : OriginalInputRevocation::Discontinuity);
    result.conflict = true;
    appendReleases(result.releases, 0, true);
    return result;
  }
  m_sequence = record.sequence;
  m_timestamp = record.sourceTimestampNs;
  m_flags = record.flags;
  std::size_t slot = 0;
  OriginalKeyTransition transition;
  if (const auto key = originalInputKeyEvent(record)) {
    slot = key->sample.keyCode;
    transition = key->transition;
  } else if (const auto *button = std::get_if<OriginalInputButton>(&record.payload)) {
    slot = originalKey::keyCount + button->button;
    transition = button->down ? OriginalKeyTransition::Down : OriginalKeyTransition::Up;
  } else {
    // Consuming every record checks mixed-stream continuity. A caller needs a
    // separate, proven pointer/scroll mapping and owner before delivering them.
    const auto *boundary = std::get_if<OriginalInputBoundary>(&record.payload);
    const bool wrongFlags = std::any_of(m_pressed.begin(), m_pressed.end(), [this, &record](const auto &press) {
      return press && press->owner &&
             (flagsFor(press->owner) & originalKey::momentaryFlags) != (record.flags & originalKey::momentaryFlags);
    });
    if ((boundary && *boundary != OriginalInputBoundary::Checkpoint) || wrongFlags) {
      result.conflict = true;
      appendReleases(result.releases, 0, true);
    }
    return result;
  }
  auto &stored = m_pressed[slot];
  auto owner = proposedOwner;
  auto pressSequence = record.sequence;
  std::optional<Press> released;
  switch (transition) {
  case OriginalKeyTransition::Down:
    if (stored) {
      result.conflict = true;
      appendReleases(result.releases, 0, true);
      return result;
    }
    if (owner && !canAcquire(owner)) {
      result.conflict = true;
      appendReleases(result.releases, 0, true);
      owner = 0;
    }
    stored = Press{owner, record};
    if (!owner && slot < originalKey::keyCount && originalKeyIsModifier(static_cast<std::uint16_t>(slot)) &&
        deliveredCount()) {
      result.conflict = true;
      appendReleases(result.releases, 0, true);
    }
    break;
  case OriginalKeyTransition::Repeat:
  case OriginalKeyTransition::Up:
    if (!stored) {
      result.conflict = true;
      appendReleases(result.releases, 0, true);
      // Remember an unobserved down evidenced by a repeat without inventing an
      // owner. Its eventual up must not acquire the newly selected recipient.
      if (transition == OriginalKeyTransition::Repeat)
        stored = Press{0, record};
      return result;
    }
    owner = stored->owner;
    pressSequence = stored->record.sequence;
    if (transition == OriginalKeyTransition::Up) {
      released = stored;
      stored.reset();
    }
    break;
  case OriginalKeyTransition::Toggle:
    if (owner && !canAcquire(owner)) {
      result.conflict = true;
      appendReleases(result.releases, 0, true);
      owner = 0;
    }
    break;
  }
  if (!owner)
    return result;
  if ((flagsFor(owner) & originalKey::momentaryFlags) != (record.flags & originalKey::momentaryFlags)) {
    result.conflict = true;
    // This new down has not been delivered. Keep its physical tombstone, but
    // never synthesize an up to a recipient that did not receive its down.
    if (transition == OriginalKeyTransition::Down)
      stored->owner = 0;
    // Include an already removed up in the same ordered cleanup. Restoring its
    // delivered ownership briefly lets buttons/ordinary keys release with that
    // owner's modifiers before the modifier itself is released.
    if (released)
      stored = released;
    appendReleases(result.releases, 0, true);
    if (released)
      stored.reset(); // Its physical up already arrived; no tombstone remains.
    return result;
  }
  result.delivery = OriginalInputPressDelivery{owner, pressSequence, input};
  return result;
}

std::vector<OriginalInputRelease> OriginalInputPressLedger::releaseMatching(OriginalInputOwner owner, bool all)
{
  std::vector<OriginalInputRelease> releases;
  releases.reserve(m_pressed.size());
  appendReleases(releases, owner, all);
  return releases;
}

void OriginalInputPressLedger::appendReleases(
    std::vector<OriginalInputRelease> &releases, OriginalInputOwner owner, bool all
) noexcept
{
  for (const bool modifiersOnly : {false, true}) {
    for (std::size_t slot = 0; slot < m_pressed.size(); ++slot) {
      auto &press = m_pressed[slot];
      const bool modifier = slot < originalKey::keyCount && originalKeyIsModifier(static_cast<std::uint16_t>(slot));
      if (!press || !press->owner || (!all && press->owner != owner) || modifier != modifiersOnly)
        continue;
      const auto old = *press;
      press->owner = 0;
      releases.push_back({old.owner, old.record, flagsFor(old.owner)});
    }
  }
}

std::vector<OriginalInputRelease> OriginalInputPressLedger::revoke(OriginalInputOwner owner)
{
  return owner ? releaseMatching(owner, false) : std::vector<OriginalInputRelease>{};
}

std::vector<OriginalInputRelease> OriginalInputPressLedger::revokeAll()
{
  return releaseMatching(0, true);
}

bool OriginalInputPressLedger::empty() const noexcept
{
  return std::none_of(m_pressed.begin(), m_pressed.end(), [](const auto &press) { return press.has_value(); });
}

std::size_t OriginalInputPressLedger::deliveredCount() const noexcept
{
  return static_cast<std::size_t>(std::count_if(m_pressed.begin(), m_pressed.end(), [](const auto &press) {
    return press && press->owner;
  }));
}

} // namespace deskflow::windowshare
