/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "OriginalInput.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>

// Exercise allocation failure before release bookkeeping changes any owner.
// The switch is thread-local and is armed only around one synchronous call.
namespace {
thread_local bool failNextAllocation = false;
}
void *operator new(std::size_t size)
{
  if (failNextAllocation) {
    failNextAllocation = false;
    throw std::bad_alloc();
  }
  if (void *memory = std::malloc(size ? size : 1))
    return memory;
  throw std::bad_alloc();
}
void operator delete(void *memory) noexcept
{
  std::free(memory);
}

namespace deskflow::windowshare {
struct OriginalInputTestAccess
{
  static std::uint64_t reserve(std::atomic<std::uint64_t> &next)
  {
    return OriginalInputStream::reserveEpoch(next);
  }
  static void exhaustSequence(OriginalInputStream &stream)
  {
    stream.m_lastSequence = std::numeric_limits<std::uint64_t>::max();
  }
};
} // namespace deskflow::windowshare

using namespace deskflow::windowshare;
namespace {
using BarrierState = OriginalInputNeutralBarrier::State;
constexpr std::uint64_t leftShift = originalKey::shiftFlag | originalKey::leftShiftFlag;
constexpr std::uint64_t rightShift = originalKey::shiftFlag | originalKey::rightShiftFlag;
constexpr std::uint64_t opaqueFlags = 0x8000000000000100ULL;

void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}

OriginalInputKey key(std::uint16_t code, OriginalKeyTransition transition)
{
  auto kind = transition == OriginalKeyTransition::Up ? OriginalKeyKind::Up : OriginalKeyKind::Down;
  if (originalKeyIsModifier(code) || code == originalKey::capsLock)
    kind = OriginalKeyKind::FlagsChanged;
  return {41, code, kind, transition, transition == OriginalKeyTransition::Repeat};
}

struct Fixture
{
  OriginalInputStream stream;
  OriginalInputNeutralBarrier barrier{stream.epoch()};
  std::uint64_t sequence = 0, timestamp = 100;
  template <class Payload> OriginalInputRecord record(Payload payload, std::uint64_t flags = 0)
  {
    return {stream.epoch().id(), ++sequence, ++timestamp, flags, payload};
  }
  template <class Payload> OriginalInputDelivery emit(Payload payload, std::uint64_t flags = 0)
  {
    check(stream.push(record(payload, flags)), "fixture push");
    const auto delivery = stream.pop();
    check(delivery && delivery->live(), "fixture live delivery");
    return *delivery;
  }
  template <class Payload> OriginalInputDelivery observe(Payload payload, std::uint64_t flags = 0)
  {
    const auto delivery = emit(payload, flags);
    check(barrier.observe(delivery), "fixture ordered barrier observation");
    return delivery;
  }
  OriginalInputPhysicalObservation checkpoint(std::uint64_t flags = 0)
  {
    const auto delivery = observe(OriginalInputBoundary::Checkpoint, flags);
    OriginalInputPhysicalObservation result;
    result.checkpoint = delivery.record.sequence;
    result.sourceTimestampNs = delivery.record.sourceTimestampNs;
    result.flags = flags;
    result.coverageGeneration = 1;
    return result;
  }
  static void complete(OriginalInputPhysicalObservation &observation)
  {
    observation.knownKeys.set();
    observation.knownButtons.set();
    observation.flagsKnown = true;
    observation.scroll = OriginalScrollState::Idle;
  }
  void neutral(std::uint64_t flags = 0)
  {
    auto observation = checkpoint(flags);
    complete(observation);
    check(barrier.observeTrustedPhysical(observation), "explicit fake trusted coverage");
    check(barrier.state() == BarrierState::Neutral, "trusted neutral fixture");
  }
};

void testRecords()
{
  static_assert(sizeof(OriginalInputRecord) <= 128);
  Fixture fixture;
  auto record = fixture.record(key(0, OriginalKeyTransition::Down), opaqueFlags);
  check(validOriginalInputRecord(record), "opaque flags retained");
  auto native = originalInputKeyEvent(record);
  check(
      native && native->sample.flags == opaqueFlags && native->sample.keyCode == 0 &&
          native->sample.keyboardType == 41 && native->sample.sequence == 1,
      "existing key envelope retains native identity and common order"
  );
  record.flags = originalKey::shiftFlag;
  check(!validOriginalInputRecord(record), "aggregate-only flags are not normalized authority");
  record.flags = leftShift;
  record.payload = key(originalKey::leftShift, OriginalKeyTransition::Up);
  check(!validOriginalInputRecord(record), "modifier direction matches native side bit");
  record.payload = OriginalInputMotion{std::numeric_limits<std::int32_t>::min(), 25, 31};
  check(validOriginalInputRecord(record), "bounded signed pointer deltas and high button");
  std::get<OriginalInputMotion>(record.payload).dx = std::numeric_limits<std::int64_t>::max();
  check(!validOriginalInputRecord(record), "pointer delta cannot silently narrow");
  record.payload = OriginalInputButton{32, true, 1};
  check(!validOriginalInputRecord(record), "button count bound");
  record.payload = OriginalInputButton{31, true, 0};
  check(!validOriginalInputRecord(record), "missing click state rejected");
  record.payload = OriginalInputButton{31, true, 3};
  check(validOriginalInputRecord(record), "native click count retained beyond double click");
  record.payload =
      OriginalInputScroll{{-3, 9}, {7, -18}, {123, -456}, OriginalScrollPhase::Began, OriginalMomentumPhase::None,
                          true};
  check(validOriginalInputRecord(record), "scroll representations coexist exactly");
  std::get<OriginalInputScroll>(record.payload).momentum = OriginalMomentumPhase::Began;
  check(!validOriginalInputRecord(record), "gesture and momentum phases cannot coexist");
  std::get<OriginalInputScroll>(record.payload).phase = static_cast<OriginalScrollPhase>(3);
  check(!validOriginalInputRecord(record), "unknown phase not normalized away");
  record.payload = OriginalInputBoundary::Checkpoint;
  check(validOriginalInputRecord(record), "checkpoint has same envelope");
  record.epoch = 0;
  check(!validOriginalInputRecord(record), "zero epoch invalid");
}

void testQueueOrderAndDiscontinuity()
{
  Fixture fixture;
  std::array<OriginalInputRecord, 6> records{
      fixture.record(key(0, OriginalKeyTransition::Down), opaqueFlags),
      fixture.record(OriginalInputMotion{17, -7}),
      fixture.record(OriginalInputButton{2, true, 2}),
      fixture.record(OriginalInputScroll{{-1, 2}, {0, 8}, {-65536, 131072}}),
      fixture.record(key(0, OriginalKeyTransition::Up)),
      fixture.record(OriginalInputBoundary::Checkpoint)
  };
  for (const auto &record : records)
    check(fixture.stream.push(record), "all kinds share one queue");
  for (const auto &record : records) {
    const auto delivery = fixture.stream.pop();
    check(
        delivery && delivery->live() && delivery->record.sequence == record.sequence &&
            delivery->record.sourceTimestampNs == record.sourceTimestampNs && delivery->record.flags == record.flags &&
            delivery->record.payload.index() == record.payload.index(),
        "mixed queue preserves exact capture order"
    );
    if (const auto *scroll = std::get_if<OriginalInputScroll>(&delivery->record.payload))
      check(
          scroll->fixed16_16[0] == -65536 && scroll->lines[1] == 2 && scroll->points[1] == 8,
          "no scroll scaling/coalescing in queue"
      );
  }
  check(!fixture.stream.pop(), "queue exhausted exactly");
  for (unsigned failure = 0; failure < 5; ++failure) {
    Fixture broken;
    const auto retained = broken.emit(OriginalInputMotion{1, 2});
    auto next = broken.record(OriginalInputMotion{3, 4});
    switch (failure) {
    case 0:
      ++next.sequence;
      break;
    case 1:
      --next.sequence;
      break;
    case 2:
      next.sourceTimestampNs = 1;
      break;
    case 3:
      ++next.epoch;
      break;
    case 4:
      next.payload = OriginalInputBoundary::Discontinuity;
      break;
    }
    check(!broken.stream.push(next) && !retained.live(), "gap/replay/time/epoch/loss revokes retained delivery");
    check(broken.stream.epoch().reason() == OriginalInputRevocation::Discontinuity, "first discontinuity sticky");
    check(!broken.stream.push(broken.record(OriginalInputBoundary::Checkpoint)), "checkpoint cannot reset epoch");
  }
}

void testOverflowEmergencyAndLifetime()
{
  Fixture fixture;
  const auto retained = fixture.emit(OriginalInputMotion{1, 2});
  for (std::size_t i = 0; i < OriginalInputStream::capacity; ++i)
    check(
        fixture.stream.push(fixture.record(OriginalInputMotion{static_cast<std::int64_t>(i), 0})),
        "fill every queue slot without overwrite"
    );
  check(!fixture.stream.push(fixture.record(OriginalInputMotion{9, 9})), "full queue rejects overflow");
  check(
      !retained.live() && fixture.stream.epoch().reason() == OriginalInputRevocation::Overflow,
      "overflow after pop revokes in-flight evidence"
  );
  check(!fixture.stream.pop(), "revoked queue never delivers its tail");
  const auto emergencyFlags = originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag;
  check(
      !fixture.stream.push(fixture.record(key(originalKey::escape, OriginalKeyTransition::Down), emergencyFlags)),
      "emergency bypasses full/revoked queue"
  );
  check(
      fixture.stream.epoch().emergencyRequested() &&
          fixture.stream.epoch().reason() == OriginalInputRevocation::Overflow,
      "emergency survives first-reason stickiness and ambiguous side flags"
  );
  Fixture emergencyFixture;
  check(
      !emergencyFixture.stream.push(emergencyFixture.record(OriginalInputBoundary::Emergency)) &&
          emergencyFixture.stream.epoch().reason() == OriginalInputRevocation::Emergency,
      "explicit emergency requires no queue slot"
  );
  OriginalInputEpoch retainedEpoch;
  std::optional<OriginalInputDelivery> delivery;
  {
    Fixture scoped;
    delivery = scoped.emit(OriginalInputMotion{});
    retainedEpoch = scoped.stream.epoch();
  }
  check(!retainedEpoch.live() && !delivery->live(), "stream destruction revokes every retained copy");
  Fixture reconnect;
  check(
      !reconnect.stream.epoch().sameEpoch(retainedEpoch) && reconnect.stream.epoch().id() != retainedEpoch.id(),
      "reconnection has a fresh epoch"
  );
  std::atomic<std::uint64_t> allocator{std::numeric_limits<std::uint64_t>::max() - 1};
  check(
      OriginalInputTestAccess::reserve(allocator) == std::numeric_limits<std::uint64_t>::max() - 1 &&
          !OriginalInputTestAccess::reserve(allocator) && !OriginalInputTestAccess::reserve(allocator),
      "epoch allocation saturates without wrap"
  );
  OriginalInputTestAccess::exhaustSequence(reconnect.stream);
  check(
      !reconnect.stream.push(reconnect.record(OriginalInputMotion{})) &&
          reconnect.stream.epoch().reason() == OriginalInputRevocation::Exhausted,
      "sequence exhaustion terminal"
  );
}

void testUnknownCoverageAndHeldStartup()
{
  Fixture unknown;
  check(
      unknown.barrier.state() == BarrierState::Unknown && !unknown.barrier.prepare(1),
      "empty tracker is not physical neutrality"
  );
  for (std::uint16_t code = 0; code < originalKey::keyCount; ++code) {
    if (!originalKeyIsModifier(code) && code != originalKey::capsLock)
      unknown.observe(key(code, OriginalKeyTransition::Up));
  }
  auto partial = unknown.checkpoint();
  check(
      unknown.barrier.state() == BarrierState::Unknown && !unknown.barrier.prepare(1),
      "many zero-flag ups plus checkpoint cannot assert complete coverage"
  );
  Fixture::complete(partial);
  partial.knownKeys.reset(255);
  check(
      unknown.barrier.observeTrustedPhysical(partial) && unknown.barrier.state() == BarrierState::Unknown,
      "partial provider coverage remains unknown"
  );
  partial = unknown.checkpoint();
  Fixture::complete(partial);
  check(
      unknown.barrier.observeTrustedPhysical(partial) && unknown.barrier.state() == BarrierState::Neutral,
      "only full provider observation resolves missing coverage"
  );
  Fixture heldKey;
  auto held = heldKey.checkpoint();
  Fixture::complete(held);
  held.downKeys.set(0);
  check(
      heldKey.barrier.observeTrustedPhysical(held) && heldKey.barrier.state() == BarrierState::Busy,
      "startup-held nonmodifier blocks despite zero event flags"
  );
  heldKey.observe(key(0, OriginalKeyTransition::Up));
  check(heldKey.barrier.state() == BarrierState::Neutral, "observed up resolves known startup hold");
  Fixture button;
  auto physical = button.checkpoint();
  Fixture::complete(physical);
  physical.downButtons.set(31);
  check(
      button.barrier.observeTrustedPhysical(physical) && button.barrier.state() == BarrierState::Busy,
      "highest supported startup button blocks"
  );
  button.observe(OriginalInputButton{31, false, 1});
  check(button.barrier.state() == BarrierState::Neutral, "known physical button up resolves hold");
  Fixture caps;
  caps.neutral(originalKey::capsLockFlag);
  caps.observe(key(originalKey::capsLock, OriginalKeyTransition::Toggle), originalKey::capsLockFlag);
  check(caps.barrier.state() == BarrierState::Unknown, "toggle does not identify physical Caps transition");
  auto capsRefresh = caps.checkpoint(originalKey::capsLockFlag);
  check(!caps.barrier.prepare(1), "checkpoint after Caps Toggle cannot prepare despite no physical flag");
  Fixture::complete(capsRefresh);
  check(
      caps.barrier.observeTrustedPhysical(capsRefresh) && caps.barrier.state() == BarrierState::Neutral,
      "trusted physical refresh resolves Caps transition ambiguity"
  );
  Fixture heldCaps;
  auto capsPhysical = heldCaps.checkpoint(originalKey::capsLockFlag | originalKey::capsPhysicalFlags);
  Fixture::complete(capsPhysical);
  capsPhysical.downKeys.set(originalKey::capsLock);
  check(
      heldCaps.barrier.observeTrustedPhysical(capsPhysical) && heldCaps.barrier.state() == BarrierState::Busy,
      "physical Caps hold distinct from latch"
  );
  heldCaps.observe(key(originalKey::capsLock, OriginalKeyTransition::Toggle), originalKey::capsLockFlag);
  check(heldCaps.barrier.state() == BarrierState::Unknown, "toggle alone cannot clear provider-observed physical Caps");
  Fixture unknownScroll;
  auto noScrollProof = unknownScroll.checkpoint();
  Fixture::complete(noScrollProof);
  noScrollProof.scroll = OriginalScrollState::Unknown;
  check(
      unknownScroll.barrier.observeTrustedPhysical(noScrollProof) &&
          unknownScroll.barrier.state() == BarrierState::Unknown,
      "complete key/button coverage cannot establish unknown scroll state"
  );
  Fixture pendingScroll;
  auto pending = pendingScroll.checkpoint();
  Fixture::complete(pending);
  pending.scroll = OriginalScrollState::PendingMomentum;
  check(
      pendingScroll.barrier.observeTrustedPhysical(pending) && !pendingScroll.barrier.prepare(1) &&
          pendingScroll.barrier.state() == BarrierState::Busy,
      "provider-observed pending momentum blocks cutover"
  );
}

void testContradictoryPhysicalObservation()
{
  for (const bool button : {false, true}) {
    Fixture fixture;
    if (button)
      fixture.observe(OriginalInputButton{0, true, 1});
    else
      fixture.observe(key(0, OriginalKeyTransition::Down));
    auto observation = fixture.checkpoint();
    Fixture::complete(observation);
    check(
        !fixture.barrier.observeTrustedPhysical(observation) && fixture.barrier.state() == BarrierState::Revoked,
        "provider cannot erase captured key/button down without up"
    );
  }
  for (const bool button : {false, true}) {
    Fixture fixture;
    if (button)
      fixture.observe(OriginalInputButton{0, true, 1});
    else
      fixture.observe(key(0, OriginalKeyTransition::Down));
    auto partial = fixture.checkpoint();
    Fixture::complete(partial);
    if (button)
      partial.knownButtons.reset(0);
    else
      partial.knownKeys.reset(0);
    check(
        fixture.barrier.observeTrustedPhysical(partial) && fixture.barrier.state() == BarrierState::Unknown,
        "partial coverage demotes authority but retains observed physical state"
    );
    auto cleared = fixture.checkpoint();
    Fixture::complete(cleared);
    check(!fixture.barrier.observeTrustedPhysical(cleared), "partial coverage cannot launder a known hold into up");
  }
}

void testTrustedBoundaryAndCutover()
{
  Fixture fixture;
  fixture.neutral();
  const auto first = fixture.barrier.prepare(1);
  check(
      first && first->pending() && fixture.stream.commitCutover(*first) && first->committed(),
      "complete consumed checkpoint commits once"
  );
  check(!fixture.stream.commitCutover(*first), "token not reusable");
  auto replacement = fixture.barrier.prepare(2);
  check(replacement && !fixture.stream.commitCutover(*replacement), "same checkpoint cannot commit twice");
  fixture.observe(OriginalInputBoundary::Checkpoint);
  replacement = fixture.barrier.prepare(3);
  check(replacement && replacement->pending(), "fresh consumed checkpoint prepares");
  check(
      fixture.stream.push(fixture.record(OriginalInputMotion{1, 1})), "new input published before consumer catches up"
  );
  check(
      !replacement->pending() && !fixture.stream.commitCutover(*replacement) &&
          fixture.barrier.state() == BarrierState::Unknown,
      "producer advance defeats stale neutral checkpoint"
  );
  auto next = fixture.stream.pop();
  check(next && fixture.barrier.observe(*next), "consumer catches up");
  fixture.observe(OriginalInputBoundary::Checkpoint);
  const auto cancelled = fixture.barrier.prepare(4);
  check(cancelled && cancelled->pending(), "prepare cancellation fixture");
  fixture.barrier.cancelPrepared();
  check(!cancelled->pending() && !fixture.stream.commitCutover(*cancelled), "consumer cancellation wins");
  Fixture other;
  check(!other.stream.commitCutover(*first), "cutover cannot cross epochs");
  fixture.stream.revoke(OriginalInputRevocation::Stopped);
  check(!first->committed() && !replacement->pending(), "retained cutover remains revocable after commit");

  for (unsigned failure = 0; failure < 6; ++failure) {
    Fixture broken;
    auto observation = broken.checkpoint();
    Fixture::complete(observation);
    switch (failure) {
    case 0:
      ++observation.checkpoint;
      break;
    case 1:
      ++observation.sourceTimestampNs;
      break;
    case 2:
      observation.coverageGeneration = 0;
      break;
    case 3:
      observation.knownKeys.reset(0);
      observation.downKeys.set(0);
      break;
    case 4:
      observation.downKeys.set(originalKey::leftShift);
      break;
    case 5:
      observation.scroll = static_cast<OriginalScrollState>(255);
      break;
    }
    check(
        !broken.barrier.observeTrustedPhysical(observation) && broken.barrier.state() == BarrierState::Revoked,
        "invalid provider anchor/coverage never manufactures neutrality"
    );
  }
  Fixture duplicate;
  auto physical = duplicate.checkpoint();
  Fixture::complete(physical);
  check(duplicate.barrier.observeTrustedPhysical(physical), "first provider observation");
  check(!duplicate.barrier.observeTrustedPhysical(physical), "same checkpoint cannot be restated during commit race");
  Fixture coverageChange;
  coverageChange.neutral();
  auto changed = coverageChange.checkpoint();
  Fixture::complete(changed);
  changed.coverageGeneration = 2;
  check(!coverageChange.barrier.observeTrustedPhysical(changed), "changed physical domain needs fresh epoch");
}

void testPhysicalConsistency()
{
  Fixture fixture;
  fixture.neutral();
  fixture.observe(key(originalKey::leftShift, OriginalKeyTransition::Down), leftShift);
  fixture.observe(OriginalInputButton{0, true, 1}, leftShift);
  fixture.observe(OriginalInputMotion{20, -9, 0}, leftShift);
  check(fixture.barrier.state() == BarrierState::Busy, "mixed key/button/drag remains physically busy");
  fixture.observe(key(originalKey::leftShift, OriginalKeyTransition::Up));
  fixture.observe(OriginalInputButton{0, false, 1});
  check(fixture.barrier.state() == BarrierState::Neutral, "all known physical holds released");
  const auto orphanDrag = fixture.emit(OriginalInputMotion{1, 1, 0});
  check(!fixture.barrier.observe(orphanDrag), "drag without known down indicates lost transition");
  Fixture missingModifier;
  missingModifier.neutral();
  check(
      !missingModifier.barrier.observe(missingModifier.emit(OriginalInputMotion{1, 1}, leftShift)),
      "pointer-local flags cannot hide a missed modifier down"
  );
  Fixture skipped;
  skipped.observe(OriginalInputMotion{});
  skipped.emit(OriginalInputButton{0, true, 1});
  check(
      !skipped.barrier.observe(skipped.emit(key(0, OriginalKeyTransition::Down))),
      "consumer cannot skip pointer record in key stream"
  );
}

OriginalInputScroll scroll(OriginalScrollPhase phase, OriginalMomentumPhase momentum = OriginalMomentumPhase::None)
{
  return {{0, 1}, {0, 7}, {0, 65536}, phase, momentum, true};
}

void testScrollKnowledge()
{
  Fixture fixture;
  fixture.neutral();
  fixture.observe(scroll(OriginalScrollPhase::MayBegin));
  fixture.observe(scroll(OriginalScrollPhase::Began));
  fixture.observe(scroll(OriginalScrollPhase::Changed));
  fixture.observe(scroll(OriginalScrollPhase::Ended));
  check(
      fixture.barrier.scrollState() == OriginalScrollState::PendingMomentum &&
          fixture.barrier.state() == BarrierState::Busy,
      "gesture end is not momentum completion"
  );
  for (unsigned i = 0; i < 20; ++i)
    fixture.observe(OriginalInputBoundary::Checkpoint);
  check(
      !fixture.barrier.prepare(1) && fixture.barrier.scrollState() == OriginalScrollState::PendingMomentum,
      "checkpoints and silence cannot clear pending momentum"
  );
  fixture.observe(scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Began));
  fixture.observe(scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Continued));
  fixture.observe(scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Ended));
  check(fixture.barrier.state() == BarrierState::Neutral, "complete observed gesture/momentum chain resolves");
  fixture.observe(OriginalInputScroll{{0, 1}, {0, 0}, {0, 0}});
  check(fixture.barrier.state() == BarrierState::Neutral, "discrete wheel has no continuous gesture ownership");
  for (const auto &orphan :
       {scroll(OriginalScrollPhase::None), scroll(OriginalScrollPhase::Changed), scroll(OriginalScrollPhase::Ended),
        scroll(OriginalScrollPhase::Cancelled), scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Continued),
        scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Ended)}) {
    Fixture unknown;
    unknown.neutral();
    unknown.observe(orphan);
    unknown.observe(scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Ended));
    auto physical = unknown.checkpoint();
    check(
        unknown.barrier.state() == BarrierState::Unknown && !unknown.barrier.prepare(1),
        "phase-free/orphan scroll remains unknown even after an end"
    );
    Fixture::complete(physical);
    check(
        unknown.barrier.observeTrustedPhysical(physical) && unknown.barrier.state() == BarrierState::Neutral,
        "only explicit trusted observation may resolve unknown scrolling"
    );
  }
  Fixture startup;
  startup.observe(scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Ended));
  check(startup.barrier.state() == BarrierState::Unknown, "startup end alone proves neither coverage nor idle");
}

void testPressOwnersAndReleases()
{
  Fixture fixture;
  OriginalInputPressLedger ledger(fixture.stream.epoch());
  const auto down = ledger.observe(fixture.emit(key(0, OriginalKeyTransition::Down)), 11);
  check(down.delivery && down.delivery->owner == 11, "key down establishes original recipient");
  const auto button = ledger.observe(fixture.emit(OriginalInputButton{2, true, 2}), 11);
  check(button.delivery && button.delivery->owner == 11, "button shares same recipient");
  check(
      !ledger.observe(fixture.emit(OriginalInputMotion{3, 4, 2}), 22).delivery,
      "ledger does not authorize a pointer mapping"
  );
  auto up = ledger.observe(fixture.emit(OriginalInputButton{2, false, 2}), 22);
  check(
      up.delivery && up.delivery->owner == 11 && up.delivery->pressSequence == button.delivery->pressSequence,
      "button up stays with original owner despite selection change"
  );
  auto repeated = ledger.observe(fixture.emit(key(0, OriginalKeyTransition::Repeat)), 22);
  check(
      repeated.delivery && repeated.delivery->owner == 11 &&
          repeated.delivery->pressSequence == down.delivery->pressSequence,
      "repeat uses original down owner"
  );
  const auto releases = ledger.revoke(11);
  check(
      releases.size() == 1 && releases[0].owner == 11 && !ledger.empty() && !ledger.deliveredCount(),
      "revoke returns exact owner and retains physical tombstone"
  );
  check(ledger.revokeAll().empty(), "release is idempotent");
  check(
      !ledger.observe(fixture.emit(key(0, OriginalKeyTransition::Repeat)), 22).delivery,
      "late repeat cannot acquire new owner"
  );
  const auto blocked = ledger.observe(fixture.emit(OriginalInputButton{0, true, 1}), 22);
  check(blocked.conflict && !blocked.delivery, "physical tombstone blocks new recipient");
  check(
      !ledger.observe(fixture.emit(key(0, OriginalKeyTransition::Up)), 22).delivery,
      "late up clears tombstone without touching future recipient"
  );
  check(
      !ledger.observe(fixture.emit(OriginalInputButton{0, false, 1}), 22).delivery && ledger.empty(),
      "all tombstones need their physical ups"
  );

  Fixture modifiers;
  OriginalInputPressLedger ordered(modifiers.stream.epoch());
  ordered.observe(modifiers.emit(key(originalKey::leftShift, OriginalKeyTransition::Down), leftShift), 33);
  ordered.observe(
      modifiers.emit(key(originalKey::rightShift, OriginalKeyTransition::Down), leftShift | rightShift), 33
  );
  ordered.observe(modifiers.emit(key(0, OriginalKeyTransition::Down), leftShift | rightShift), 33);
  ordered.observe(modifiers.emit(OriginalInputButton{0, true, 1}, leftShift | rightShift), 33);
  const auto all = ordered.revokeAll();
  check(
      all.size() == 4 && std::holds_alternative<OriginalInputKey>(all[0].press.payload) &&
          std::holds_alternative<OriginalInputButton>(all[1].press.payload),
      "ordinary key and button released before modifiers"
  );
  check(
      all[0].flags == (leftShift | rightShift) && all[1].flags == (leftShift | rightShift) &&
          all[2].flags == rightShift && all[3].flags == 0,
      "release flags contain only owner's remaining modifier sides"
  );
}

void testLedgerConflictsAndAllocation()
{
  Fixture fixture;
  OriginalInputPressLedger ledger(fixture.stream.epoch());
  ledger.observe(fixture.emit(key(0, OriginalKeyTransition::Down)), 7);
  failNextAllocation = true;
  bool failed = false;
  try {
    ledger.revokeAll();
  } catch (const std::bad_alloc &) {
    failed = true;
  }
  check(failed && ledger.deliveredCount() == 1, "release allocation failure preserves owner");
  auto up = fixture.emit(key(0, OriginalKeyTransition::Up));
  failNextAllocation = true;
  failed = false;
  try {
    ledger.observe(up, 7);
  } catch (const std::bad_alloc &) {
    failed = true;
  }
  check(failed && ledger.deliveredCount() == 1, "up allocation failure preserves slot and input high-water");
  check(ledger.observe(up, 7).delivery && ledger.empty(), "same unapplied up safely retried");
  Fixture startup;
  OriginalInputPressLedger unknown(startup.stream.epoch());
  check(
      unknown.observe(startup.emit(key(0, OriginalKeyTransition::Repeat)), 9).conflict,
      "orphan repeat cannot invent owner"
  );
  check(
      !unknown.empty() && !unknown.observe(startup.emit(key(1, OriginalKeyTransition::Down)), 9).delivery,
      "orphan repeat quarantines held key"
  );
  Fixture omitted;
  OriginalInputPressLedger continuity(omitted.stream.epoch());
  continuity.observe(omitted.emit(key(0, OriginalKeyTransition::Down)), 12);
  const auto skipped = omitted.emit(OriginalInputMotion{});
  const auto rejected = continuity.observe(omitted.emit(key(0, OriginalKeyTransition::Up)), 12);
  check(
      rejected.conflict && rejected.releases.size() == 1 && !skipped.live(),
      "omitting a pointer record revokes mixed consumer epoch and releases owner"
  );
  Fixture flags;
  OriginalInputPressLedger flagLedger(flags.stream.epoch());
  flagLedger.observe(flags.emit(key(originalKey::leftShift, OriginalKeyTransition::Down), leftShift), 14);
  const auto mismatch = flagLedger.observe(flags.emit(OriginalInputMotion{}), 14);
  check(
      mismatch.conflict && mismatch.releases.size() == 1 && !flagLedger.deliveredCount(),
      "pointer-local flags expose missing modifier transition"
  );
  Fixture stale;
  const auto foreign = stale.emit(key(0, OriginalKeyTransition::Down));
  check(
      flagLedger.observe(foreign, 14).conflict && !flags.stream.epoch().live(), "old ledger rejects a reconnect epoch"
  );
}

void testRejectedDownDoesNotRelease()
{
  for (const bool button : {false, true}) {
    Fixture fixture;
    OriginalInputPressLedger ledger(fixture.stream.epoch());
    const auto input = button ? fixture.emit(OriginalInputButton{0, true, 1}, leftShift)
                              : fixture.emit(key(0, OriginalKeyTransition::Down), leftShift);
    const auto rejected = ledger.observe(input, 7);
    check(
        rejected.conflict && !rejected.delivery && rejected.releases.empty() && !ledger.deliveredCount() &&
            !ledger.empty(),
        "rejected key/button down has only a tombstone, never an unowned release"
    );
    const auto up = button ? fixture.emit(OriginalInputButton{0, false, 1}, leftShift)
                           : fixture.emit(key(0, OriginalKeyTransition::Up), leftShift);
    check(!ledger.observe(up, 8).delivery && ledger.empty(), "rejected down's physical up cannot retarget");
  }
  Fixture prior;
  OriginalInputPressLedger ledger(prior.stream.epoch());
  check(
      ledger.observe(prior.emit(key(0, OriginalKeyTransition::Down)), 7).delivery.has_value(),
      "prior genuinely delivered press"
  );
  const auto rejected =
      ledger.observe(prior.emit(key(originalKey::leftShift, OriginalKeyTransition::Down), leftShift | rightShift), 7);
  check(
      rejected.conflict && !rejected.delivery && rejected.releases.size() == 1 &&
          std::get<OriginalInputKey>(rejected.releases[0].press.payload).code == 0 && rejected.releases[0].flags == 0,
      "mismatched new modifier releases only prior owner without undelivered modifier flags"
  );
  Fixture removed;
  OriginalInputPressLedger upLedger(removed.stream.epoch());
  upLedger.observe(removed.emit(key(0, OriginalKeyTransition::Down)), 7);
  const auto conflictingUp = upLedger.observe(removed.emit(key(0, OriginalKeyTransition::Up), leftShift), 8);
  check(
      conflictingUp.conflict && !conflictingUp.delivery && conflictingUp.releases.size() == 1 &&
          conflictingUp.releases[0].owner == 7 && conflictingUp.releases[0].flags == 0,
      "removed-up conflict still releases the recipient that actually received down"
  );

  Fixture modifiers;
  OriginalInputPressLedger modifierLedger(modifiers.stream.epoch());
  modifierLedger.observe(modifiers.emit(key(originalKey::leftShift, OriginalKeyTransition::Down), leftShift), 7);
  modifierLedger.observe(modifiers.emit(OriginalInputButton{0, true, 1}, leftShift), 7);
  const auto modifierUp =
      modifierLedger.observe(modifiers.emit(key(originalKey::leftShift, OriginalKeyTransition::Up), rightShift), 7);
  check(
      modifierUp.conflict && !modifierUp.delivery && modifierUp.releases.size() == 2 &&
          std::holds_alternative<OriginalInputButton>(modifierUp.releases[0].press.payload) &&
          modifierUp.releases[0].flags == leftShift &&
          std::get<OriginalInputKey>(modifierUp.releases[1].press.payload).code == originalKey::leftShift &&
          modifierUp.releases[1].flags == 0,
      "conflicting modifier up releases owned button first with its prior delivered modifier"
  );
  check(
      !modifierLedger.observe(modifiers.emit(OriginalInputButton{0, false, 1}, rightShift), 8).delivery &&
          modifierLedger.empty(),
      "already observed modifier up leaves no extra physical tombstone"
  );
}

void testThreadedSpscAndCommit()
{
  OriginalInputStream stream;
  constexpr std::uint64_t count = 40000;
  std::atomic<std::uint64_t> acknowledged{0};
  std::atomic<bool> failed{false};
  std::optional<OriginalInputDelivery> retained;
  std::thread producer([&] {
    for (std::uint64_t sequence = 1; sequence <= count; ++sequence) {
      while (sequence - acknowledged.load(std::memory_order_acquire) > OriginalInputStream::capacity / 2 &&
             !failed.load(std::memory_order_relaxed))
        std::this_thread::yield();
      OriginalInputRecord record{
          stream.epoch().id(), sequence, sequence + 100, opaqueFlags,
          OriginalInputMotion{static_cast<std::int64_t>(sequence), -17}
      };
      switch (sequence % 4) {
      case 0:
        record.payload = OriginalInputButton{31, true, 3};
        break;
      case 2:
        record.payload = key(0, OriginalKeyTransition::Down);
        break;
      case 3:
        record.payload = OriginalInputScroll{{-1, 2}, {7, 19}, {1234, -5678}};
        break;
      default:
        break;
      }
      if (!stream.push(record)) {
        failed.store(true);
        return;
      }
    }
  });
  std::thread consumer([&] {
    for (std::uint64_t sequence = 1; sequence <= count;) {
      if (auto delivery = stream.pop()) {
        bool payloadMatches = false;
        switch (sequence % 4) {
        case 0:
          if (const auto *button = std::get_if<OriginalInputButton>(&delivery->record.payload))
            payloadMatches = button->button == 31 && button->down && button->clickCount == 3;
          break;
        case 1:
          if (const auto *motion = std::get_if<OriginalInputMotion>(&delivery->record.payload))
            payloadMatches = motion->dx == static_cast<std::int64_t>(sequence) && motion->dy == -17;
          break;
        case 2:
          if (const auto *sample = std::get_if<OriginalInputKey>(&delivery->record.payload))
            payloadMatches = sample->code == 0 && sample->transition == OriginalKeyTransition::Down;
          break;
        case 3:
          if (const auto *wheel = std::get_if<OriginalInputScroll>(&delivery->record.payload))
            payloadMatches = wheel->points[0] == 7 && wheel->fixed16_16[1] == -5678;
          break;
        }
        if (!delivery->live() || delivery->record.sequence != sequence || delivery->record.flags != opaqueFlags ||
            !payloadMatches) {
          failed.store(true);
          return;
        }
        if (sequence == 1)
          retained = *delivery;
        acknowledged.store(sequence++, std::memory_order_release);
      } else if (failed.load(std::memory_order_relaxed) || !stream.epoch().live()) {
        failed.store(true);
        return;
      } else {
        std::this_thread::yield();
      }
    }
  });
  producer.join();
  consumer.join();
  check(
      !failed.load() && acknowledged.load() == count && retained && retained->live(),
      "real SPSC threads preserve all 40000 records across ring reuse"
  );
  std::thread revoker([&] { stream.revoke(OriginalInputRevocation::Emergency); });
  revoker.join();
  check(!retained->live() && stream.epoch().emergencyRequested(), "cross-thread revocation reaches popped evidence");

  for (unsigned iteration = 0; iteration < 64; ++iteration) {
    Fixture fixture;
    fixture.neutral();
    const auto token = fixture.barrier.prepare(1);
    check(token.has_value(), "concurrent commit fixture");
    std::atomic<bool> start{false};
    bool committed = false;
    std::thread committer([&] {
      while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();
      committed = fixture.stream.commitCutover(*token);
    });
    start.store(true, std::memory_order_release);
    fixture.barrier.cancelPrepared();
    committer.join();
    check(committed == token->committed() && !token->pending(), "commit/cancel has exactly one atomic winner");
  }
}
} // namespace

int main()
{
  try {
    testRecords();
    testQueueOrderAndDiscontinuity();
    testOverflowEmergencyAndLifetime();
    testUnknownCoverageAndHeldStartup();
    testContradictoryPhysicalObservation();
    testTrustedBoundaryAndCutover();
    testPhysicalConsistency();
    testScrollKnowledge();
    testPressOwnersAndReleases();
    testLedgerConflictsAndAllocation();
    testRejectedDownDoesNotRelease();
    testThreadedSpscAndCommit();
    std::cout << "OriginalInput: all checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "OriginalInput: " << error.what() << '\n';
    return 1;
  }
}
