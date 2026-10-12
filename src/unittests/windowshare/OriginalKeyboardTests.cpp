/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "OriginalKeyboard.h"

#include <iostream>
#include <map>
#include <stdexcept>

using namespace deskflow::windowshare;

namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

struct Stream
{
  OriginalKeyboardTracker tracker;
  std::uint64_t sequence = 0, timestamp = 100;

  Stream()
  {
    check(tracker.observeNeutral({sequence, timestamp, 0, {}}), "initial observed neutral");
  }
  OriginalKeySample sample(std::uint16_t code, OriginalKeyKind kind, std::uint64_t flags = 0, bool repeat = false)
  {
    return {++sequence, ++timestamp, flags, 41, code, kind, repeat};
  }
  OriginalKeyEvent event(std::uint16_t code, OriginalKeyKind kind, std::uint64_t flags = 0, bool repeat = false)
  {
    const auto result = tracker.observe(sample(code, kind, flags, repeat));
    check(
        result.status == OriginalKeyboardTracker::Status::Accepted && result.event.has_value(), "accepted fixture event"
    );
    return *result.event;
  }
};

constexpr std::uint64_t leftShift = 0x00020002, rightShift = 0x00020004, leftControl = 0x00040001,
                        rightControl = 0x00042000, leftOption = 0x00080020, rightOption = 0x00080040,
                        leftCommand = 0x00100008, rightCommand = 0x00100010;

void testShapeValidation()
{
  Stream stream;
  auto sample = stream.sample(0, OriginalKeyKind::Down);
  check(validOriginalKeySample(sample), "ordinary native key valid");
  sample.flags = 0x8000000000000100ULL;
  check(validOriginalKeySample(sample), "uninterpreted native flags retained exactly");
  check(validOriginalKeyEvent({sample, OriginalKeyTransition::Down}), "normalized ordinary down");
  check(!validOriginalKeyEvent({sample, OriginalKeyTransition::Up}), "contradictory normalized transition rejected");
  sample.repeat = true;
  check(validOriginalKeyEvent({sample, OriginalKeyTransition::Repeat}), "explicit native repeat");
  sample.kind = OriginalKeyKind::Up;
  check(!validOriginalKeySample(sample), "repeated up rejected");
  sample.repeat = false;
  sample.keyCode = 256;
  check(!validOriginalKeySample(sample), "key table bounded");
  sample.keyCode = originalKey::leftCommand;
  check(!validOriginalKeySample(sample), "modifier must retain flagsChanged kind");
  sample.kind = OriginalKeyKind::FlagsChanged;
  sample.flags = leftCommand;
  check(validOriginalKeyEvent({sample, OriginalKeyTransition::Down}), "Command identity preserved");
  check(!validOriginalKeyEvent({sample, OriginalKeyTransition::Up}), "modifier direction not caller-selectable");
  sample.flags = originalKey::leftCommandFlag;
  check(!validOriginalKeyEvent({sample, OriginalKeyTransition::Down}), "side without aggregate rejected");
  sample.keyCode = originalKey::capsLock;
  sample.flags = originalKey::capsLockFlag;
  check(validOriginalKeyEvent({sample, OriginalKeyTransition::Toggle}), "Caps Lock is a state update");
  check(!validOriginalKeyEvent({sample, OriginalKeyTransition::Down}), "Caps Lock cannot become a held press");
  sample.keyCode = 0;
  check(!validOriginalKeySample(sample), "ordinary key cannot forge flagsChanged");
  sample.kind = static_cast<OriginalKeyKind>(255);
  check(!validOriginalKeySample(sample), "unknown event kind rejected");
  sample.kind = OriginalKeyKind::Down;
  sample.sequence = 0;
  check(!validOriginalKeySample(sample), "sequence zero rejected");
  sample.sequence = 1;
  sample.timestampNs = 0;
  check(!validOriginalKeySample(sample), "missing timestamp rejected");
}

void testNeutralAndAmbiguity()
{
  OriginalKeyboardTracker tracker;
  const OriginalKeySample down{1, 101, 0, 41, 0, OriginalKeyKind::Down, false};
  check(
      tracker.observe(down).status == OriginalKeyboardTracker::Status::AwaitingNeutral,
      "startup cannot infer neutrality"
  );
  OriginalKeyboardSnapshot snapshot{1, 102, 0, {}};
  snapshot.down.set(0);
  check(!tracker.observeNeutral(snapshot), "zero modifier flags do not imply letter key released");
  snapshot.down.reset();
  snapshot.flags = leftControl;
  check(!tracker.observeNeutral(snapshot), "held modifier cannot arm");
  snapshot.flags = originalKey::capsPhysicalFlags;
  check(!tracker.observeNeutral(snapshot), "physically held Caps Lock cannot arm");
  snapshot.flags = originalKey::capsLockFlag;
  check(tracker.observeNeutral(snapshot), "Caps Lock enabled is compatible with neutral");
  check(tracker.neutral(), "observed lock-only state neutral");

  auto ambiguous =
      OriginalKeySample{2,    103, originalKey::shiftFlag, 41, originalKey::leftShift, OriginalKeyKind::FlagsChanged,
                        false};
  check(
      tracker.observe(ambiguous).status == OriginalKeyboardTracker::Status::Invalid, "aggregate-only side is ambiguous"
  );
  check(!tracker.synchronized(), "ambiguity invalidates stream");
  ambiguous.sequence = 3;
  ambiguous.timestampNs = 104;
  ambiguous.flags = leftShift;
  check(
      tracker.observe(ambiguous).status == OriginalKeyboardTracker::Status::AwaitingNeutral,
      "later modifier cannot guess recovery"
  );
  check(!tracker.observeNeutral(snapshot), "stale snapshot cannot restore trust");
  check(tracker.observeNeutral({3, 105, 0, {}}), "fresh physical snapshot restores trust");
  auto missing = OriginalKeySample{4, 106, leftCommand, 41, 0, OriginalKeyKind::Down, false};
  check(
      tracker.observe(missing).status == OriginalKeyboardTracker::Status::Invalid,
      "unreported modifier transition rejected"
  );
}

void testModifierSidesAndRepeats()
{
  Stream stream;
  const std::pair<std::uint16_t, std::uint64_t> pairs[][2] = {
      {{0x38, leftShift}, {0x3c, rightShift}},
      {{0x3b, leftControl}, {0x3e, rightControl}},
      {{0x3a, leftOption}, {0x3d, rightOption}},
      {{0x37, leftCommand}, {0x36, rightCommand}}
  };
  for (const auto &pair : pairs) {
    check(
        stream.event(pair[0].first, OriginalKeyKind::FlagsChanged, pair[0].second).transition ==
            OriginalKeyTransition::Down,
        "first side pressed"
    );
    stream.event(pair[1].first, OriginalKeyKind::FlagsChanged, pair[0].second | pair[1].second);
    check(
        stream.tracker.down()[pair[0].first] && stream.tracker.down()[pair[1].first], "both sides independently held"
    );
    check(
        stream.event(pair[0].first, OriginalKeyKind::FlagsChanged, pair[1].second).transition ==
            OriginalKeyTransition::Up,
        "first side up while aggregate remains set"
    );
    check(
        !stream.tracker.down()[pair[0].first] && stream.tracker.down()[pair[1].first], "right-side ownership retained"
    );
    stream.event(pair[1].first, OriginalKeyKind::FlagsChanged);
    check(stream.tracker.neutral(), "both physical sides released");
  }
  stream.event(originalKey::function, OriginalKeyKind::FlagsChanged, originalKey::functionFlag);
  stream.event(0x7b, OriginalKeyKind::Down, originalKey::functionFlag);
  stream.event(0x7b, OriginalKeyKind::Up, originalKey::functionFlag);
  stream.event(originalKey::function, OriginalKeyKind::FlagsChanged);

  stream.event(originalKey::leftShift, OriginalKeyKind::FlagsChanged, leftShift);
  stream.event(0, OriginalKeyKind::Down, leftShift);
  stream.event(originalKey::leftShift, OriginalKeyKind::FlagsChanged);
  const auto repeat = stream.event(0, OriginalKeyKind::Down, 0, true);
  check(
      repeat.transition == OriginalKeyTransition::Repeat && repeat.sample.flags == 0,
      "repeat uses current flags after Shift release"
  );
  stream.event(0, OriginalKeyKind::Up);
  auto cap = stream.event(
      originalKey::capsLock, OriginalKeyKind::FlagsChanged, originalKey::capsLockFlag | originalKey::capsPhysicalFlags
  );
  check(
      cap.transition == OriginalKeyTransition::Toggle && stream.tracker.down().none(), "Caps state has no held-key slot"
  );
  check(!stream.tracker.neutral(), "stateless Caps physical bits delay neutral");
  stream.event(originalKey::capsLock, OriginalKeyKind::FlagsChanged, originalKey::capsLockFlag);
  check(stream.tracker.neutral(), "Caps physical up preserves lock and returns neutral");
}

void testDiscontinuities()
{
  Stream stream;
  const auto down = stream.event(0, OriginalKeyKind::Down);
  check(
      stream.tracker.observe(down.sample).status == OriginalKeyboardTracker::Status::Invalid, "sample replay rejected"
  );
  check(!stream.tracker.observeNeutral({0, 100, 0, {}}), "replayed sample cannot roll capture back");
  check(stream.tracker.observeNeutral({stream.sequence, ++stream.timestamp, 0, {}}), "resync after replay");
  auto staleTime = stream.sample(0, OriginalKeyKind::Down);
  staleTime.timestampNs = 1;
  check(
      stream.tracker.observe(staleTime).status == OriginalKeyboardTracker::Status::Invalid,
      "backward timestamp rejected"
  );
  check(
      stream.tracker.observeNeutral({stream.sequence, ++stream.timestamp, 0, {}}), "resync after clock discontinuity"
  );
  check(
      stream.tracker.observe(stream.sample(0, OriginalKeyKind::Down, 0, true)).status ==
          OriginalKeyboardTracker::Status::Invalid,
      "orphan repeat rejected"
  );
  check(stream.tracker.observeNeutral({stream.sequence, ++stream.timestamp, 0, {}}), "resync after orphan");
  stream.event(0, OriginalKeyKind::Down);
  check(
      stream.tracker.observe(stream.sample(0, OriginalKeyKind::Down)).status ==
          OriginalKeyboardTracker::Status::Invalid,
      "duplicate down without repeat cannot invent a press"
  );
  check(stream.tracker.observeNeutral({stream.sequence, ++stream.timestamp, 0, {}}), "resync after duplicate");
  stream.tracker.invalidate();
  check(
      stream.tracker.observe(stream.sample(0, OriginalKeyKind::Down)).status ==
          OriginalKeyboardTracker::Status::AwaitingNeutral,
      "capture loss explicitly disarms"
  );
}

void testPressOwnership()
{
  Stream stream;
  OriginalKeyLedger ledger;
  check(
      !ledger.route(stream.event(0, OriginalKeyKind::Down), 0).delivery, "local down recorded without remote delivery"
  );
  check(!ledger.route(stream.event(0, OriginalKeyKind::Down, 0, true), 9).delivery, "local repeat cannot migrate");
  check(!ledger.route(stream.event(0, OriginalKeyKind::Up), 9).delivery, "local up cannot migrate");
  const auto down = stream.event(0, OriginalKeyKind::Down);
  auto result = ledger.route(down, 9);
  check(result.delivery && result.delivery->owner == 9, "fresh press acquires owner");
  result = ledger.route(stream.event(0, OriginalKeyKind::Down, 0, true), 10);
  check(
      result.delivery && result.delivery->owner == 9 && result.delivery->pressSequence == down.sample.sequence,
      "repeat retains original owner and press ID"
  );
  result = ledger.route(stream.event(0, OriginalKeyKind::Up), 10);
  check(result.delivery && result.delivery->owner == 9, "up ignores current focus owner");
  check(ledger.empty(), "physical up clears ledger");

  ledger.route(stream.event(0, OriginalKeyKind::Down), 9);
  result = ledger.route(stream.event(1, OriginalKeyKind::Down), 10);
  check(
      result.conflict && !result.delivery && result.releases.size() == 1 && result.releases[0].owner == 9,
      "new owner cannot arm across held press; original owner drained"
  );
  check(!ledger.revoke(10).size() && ledger.revokeAll().empty(), "drain is idempotent and recipient-specific");
  check(!ledger.route(stream.event(0, OriginalKeyKind::Down, 0, true), 10).delivery, "revoked repeat tombstoned");
  check(!ledger.route(stream.event(0, OriginalKeyKind::Up), 10).delivery, "revoked up never addresses replacement");
  check(!ledger.route(stream.event(1, OriginalKeyKind::Up), 10).delivery, "rejected new down has no remote up");
  check(
      ledger.route(stream.event(0, OriginalKeyKind::Down), 10).delivery.has_value(),
      "fresh press after boundary can reacquire"
  );
  check(!ledger.clearAtNeutral(Stream{}.tracker), "cannot erase delivered keys even with neutral observation");
  ledger.revokeAll();
  check(!ledger.clearAtNeutral(Stream{}.tracker), "stale neutral observation cannot clear newer tombstones");
  check(!ledger.clearAtNeutral(stream.tracker), "held physical key blocks tombstone clearing");
  ledger.route(stream.event(0, OriginalKeyKind::Up), 0);
  check(ledger.clearAtNeutral(stream.tracker), "drained physical boundary can clear tombstones");
}

void testReleaseFlagsAndDefenses()
{
  Stream stream;
  OriginalKeyLedger ledger;
  ledger.route(stream.event(0x38, OriginalKeyKind::FlagsChanged, leftShift), 77);
  ledger.route(stream.event(0x3c, OriginalKeyKind::FlagsChanged, leftShift | rightShift), 77);
  ledger.route(stream.event(0, OriginalKeyKind::Down, leftShift | rightShift), 77);
  const auto releases = ledger.revoke(77);
  check(releases.size() == 3, "all delivered controls released");
  check(
      releases[0].press.keyCode == 0 && releases[0].flags == (leftShift | rightShift),
      "ordinary key released before modifiers"
  );
  check(
      releases[1].press.keyCode == 0x38 && releases[1].flags == rightShift, "left release retains delivered right Shift"
  );
  check(releases[2].press.keyCode == 0x3c && releases[2].flags == 0, "final side clears aggregate");
  ledger.route(stream.event(0, OriginalKeyKind::Up, leftShift | rightShift), 88);
  ledger.route(stream.event(0x38, OriginalKeyKind::FlagsChanged, rightShift), 88);
  ledger.route(stream.event(0x3c, OriginalKeyKind::FlagsChanged), 88);
  check(ledger.empty(), "physical ups consume drained tombstones");

  auto down = stream.event(0, OriginalKeyKind::Down);
  ledger.route(down, 77);
  // The ledger is an independent defense even if a caller skips the tracker.
  auto forgedUp = OriginalKeyEvent{stream.sample(0, OriginalKeyKind::Up, leftControl), OriginalKeyTransition::Up};
  auto result = ledger.route(forgedUp, 88);
  check(
      result.conflict && result.releases.size() == 1 && result.releases[0].owner == 77 && result.releases[0].flags == 0,
      "conflicting up still releases original key without foreign Control"
  );
  check(!result.delivery && ledger.empty(), "conflicting up neither injected nor stranded");
  down.sample.sequence = ++stream.sequence;
  down.sample.timestampNs = ++stream.timestamp;
  down.sample.flags = leftCommand;
  result = ledger.route(down, 77);
  check(
      result.conflict && result.releases.size() == 1 && result.releases[0].flags == 0,
      "unowned Command cannot leak into new target"
  );
  check(!result.delivery, "unowned modifier event suppressed");

  OriginalKeyLedger duplicates;
  down.sample.flags = 0;
  ++down.sample.sequence;
  duplicates.route(down, 1);
  ++down.sample.sequence;
  result = duplicates.route(down, 2);
  check(
      result.conflict && result.releases.size() == 1 && result.releases[0].owner == 1,
      "duplicate down drains once instead of reassigning"
  );
  check(duplicates.route(down, 2).releases.empty(), "replay cannot duplicate a forced release");

  Stream caps;
  OriginalKeyLedger capsLedger;
  result = capsLedger.route(caps.event(0x39, OriginalKeyKind::FlagsChanged, originalKey::capsLockFlag), 11);
  check(result.delivery && capsLedger.empty() && capsLedger.revokeAll().empty(), "Caps update has no release debt");
}

void testEmergency()
{
  Stream stream;
  OriginalKeyLedger ledger;
  std::uint64_t flags = 0;
  for (const auto &[code, mask] :
       {std::pair<std::uint16_t, std::uint64_t>{0x3b, leftControl}, {0x3d, rightOption}, {0x38, leftShift}}) {
    flags |= mask;
    ledger.route(stream.event(code, OriginalKeyKind::FlagsChanged, flags), 501);
  }
  ledger.route(stream.event(0, OriginalKeyKind::Down, flags), 501);
  const auto escape = stream.tracker.observe(stream.sample(0x35, OriginalKeyKind::Down, flags));
  check(escape.event && escape.emergency && escape.consumed, "reserved chord detected before routing");
  const auto releases = ledger.revokeAll();
  check(releases.size() == 4, "emergency drains complete original-owner state");
  for (const auto &release : releases)
    check(release.owner == 501 && release.press.keyCode != 0x35, "emergency releases address original owners only");
  check(!ledger.route(*escape.event, 0).delivery, "Escape down itself never forwarded");
  auto repeat = stream.tracker.observe(stream.sample(0x35, OriginalKeyKind::Down, flags, true));
  check(repeat.event && !repeat.emergency && repeat.consumed, "held Escape repeat consumed without second emergency");
  check(!ledger.route(*repeat.event, 0).delivery, "Escape repeat tombstoned");
  auto routeConsumed = [&](std::uint16_t code, OriginalKeyKind kind, std::uint64_t after) {
    const auto observed = stream.tracker.observe(stream.sample(code, kind, after));
    check(observed.event && observed.consumed, "emergency remains latched through physical release");
    const auto result = ledger.route(*observed.event, 0);
    check(!result.delivery && result.releases.empty(), "no duplicate release or new owner while emergency latched");
  };
  routeConsumed(1, OriginalKeyKind::Down, flags);
  routeConsumed(0x38, OriginalKeyKind::FlagsChanged, leftControl | rightOption);
  routeConsumed(0x3d, OriginalKeyKind::FlagsChanged, leftControl);
  routeConsumed(0x3b, OriginalKeyKind::FlagsChanged, 0);
  routeConsumed(0, OriginalKeyKind::Up, 0);
  routeConsumed(0x35, OriginalKeyKind::Up, 0);
  routeConsumed(1, OriginalKeyKind::Up, 0);
  check(stream.tracker.neutral() && stream.tracker.emergencyBlocked(), "all ups do not silently rearm emergency route");
  check(ledger.empty() && !ledger.clearAtNeutral(stream.tracker), "emergency needs explicit observed neutral rearm");
  check(
      stream.tracker.observeNeutral({stream.sequence, ++stream.timestamp, 0, {}}),
      "fresh neutral releases emergency latch"
  );
  check(ledger.clearAtNeutral(stream.tracker), "neutral boundary clears only drained state");

  OriginalKeyboardTracker startup;
  const OriginalKeySample chord{1,    1,    originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag,
                                41,   0x35, OriginalKeyKind::Down,
                                false};
  const auto beforeNeutral = startup.observe(chord);
  check(
      beforeNeutral.emergency && beforeNeutral.consumed && !beforeNeutral.event,
      "emergency works before neutral and with aggregate-only ambiguous flags"
  );
  check(
      startup.observe(chord).consumed && !startup.observe(chord).emergency, "replayed emergency cannot trigger again"
  );
}

// Independent delivered-state model: every up/revoke must match a prior down to
// exactly that owner, and event flags may name only that owner's delivered sides.
void testDeterministicInterleavings()
{
  Stream stream;
  OriginalKeyLedger ledger;
  OriginalKeySet physical;
  std::map<std::pair<OriginalKeyOwner, std::uint16_t>, bool> delivered;
  const std::uint16_t codes[] = {0, 1, 2, 0x38, 0x3c, 0x3b, 0x3e, 0x3a, 0x3d, 0x37, 0x36, 0x3f};
  auto maskFor = [](std::uint16_t code) -> std::uint64_t {
    switch (code) {
    case 0x38:
      return leftShift;
    case 0x3c:
      return rightShift;
    case 0x3b:
      return leftControl;
    case 0x3e:
      return rightControl;
    case 0x3a:
      return leftOption;
    case 0x3d:
      return rightOption;
    case 0x37:
      return leftCommand;
    case 0x36:
      return rightCommand;
    case 0x3f:
      return 0x00800000;
    default:
      return 0;
    }
  };
  auto expectedFlags = [&](OriginalKeyOwner owner) {
    std::uint64_t flags = 0;
    for (const auto &[key, held] : delivered) {
      if (held && key.first == owner)
        flags |= maskFor(key.second);
    }
    return flags;
  };
  auto applyReleases = [&](const std::vector<OriginalKeyRelease> &releases) {
    for (const auto &release : releases) {
      check(
          delivered.erase({release.owner, release.press.keyCode}) == 1, "forced release has one matching delivered down"
      );
      check(
          (release.flags & originalKey::momentaryFlags) == expectedFlags(release.owner),
          "forced flags exclude foreign owners"
      );
    }
  };
  auto apply = [&](const OriginalKeyLedger::Result &result) {
    applyReleases(result.releases);
    if (result.delivery) {
      const auto &delivery = *result.delivery;
      const auto key = std::pair{delivery.owner, delivery.event.sample.keyCode};
      switch (delivery.event.transition) {
      case OriginalKeyTransition::Down:
        check(delivered.emplace(key, true).second, "one delivered down per owner/control");
        break;
      case OriginalKeyTransition::Up:
        check(delivered.erase(key) == 1, "normal up returns to original owner");
        break;
      case OriginalKeyTransition::Repeat:
        check(delivered.contains(key), "repeat has original owner down");
        break;
      case OriginalKeyTransition::Toggle:
        break;
      }
      check(
          (delivery.event.sample.flags & originalKey::momentaryFlags) == expectedFlags(delivery.owner),
          "normal flags exactly match delivered owner state"
      );
    }
    check(ledger.deliveredCount() == delivered.size(), "ledger and independent recipient model agree");
  };
  std::uint32_t random = 0x12653ab1;
  for (unsigned iteration = 0; iteration < 12000; ++iteration) {
    random = random * 1664525U + 1013904223U;
    const auto code = codes[(random >> 8) % std::size(codes)];
    const bool isModifier = maskFor(code) != 0;
    const bool repeat = physical[code] && !isModifier && (random & 4);
    const bool down = !physical[code] || repeat;
    physical.set(code, down);
    std::uint64_t flags = 0;
    for (const auto current : codes) {
      if (physical[current])
        flags |= maskFor(current);
    }
    const auto kind = isModifier ? OriginalKeyKind::FlagsChanged : down ? OriginalKeyKind::Down : OriginalKeyKind::Up;
    apply(ledger.route(stream.event(code, kind, flags, repeat), (random >> 20) % 4));
    if (iteration % 23 == 0)
      applyReleases(ledger.revoke((random >> 24) % 4));
    check(ledger.deliveredCount() == delivered.size(), "owner-specific revoke agrees with recipient model");
  }
  for (const auto code : codes) {
    if (!physical[code])
      continue;
    physical.reset(code);
    std::uint64_t flags = 0;
    for (const auto current : codes) {
      if (physical[current])
        flags |= maskFor(current);
    }
    apply(
        ledger.route(stream.event(code, maskFor(code) ? OriginalKeyKind::FlagsChanged : OriginalKeyKind::Up, flags), 99)
    );
  }
  check(
      ledger.empty() && delivered.empty() && stream.tracker.neutral(),
      "complete ups leave no delivered debt or tombstones"
  );
}
} // namespace

int main()
{
  try {
    testShapeValidation();
    testNeutralAndAmbiguity();
    testModifierSidesAndRepeats();
    testDiscontinuities();
    testPressOwnership();
    testReleaseFlagsAndDefenses();
    testEmergency();
    testDeterministicInterleavings();
    std::cout << "PASS: original keyboard state, recipient ownership, and emergency invariants\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
