/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "platform/OriginalKeyboard_mac.h"
#include "windowshare/PlatformMarkers.h"

#include <iostream>
#include <stdexcept>

using namespace deskflow::windowshare;

namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

template <class T> class Owned final
{
public:
  explicit Owned(T value) : m_value(value)
  {
  }
  ~Owned()
  {
    if (m_value)
      CFRelease(m_value);
  }
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;
  T get() const
  {
    return m_value;
  }

private:
  T m_value;
};

struct Fixture
{
  Owned<CGEventSourceRef> source{CGEventSourceCreate(kCGEventSourceStatePrivate)};
  OriginalKeyboardTracker tracker;
  std::uint64_t sequence = 0, timestamp = 1000;
  Fixture()
  {
    check(source.get(), "private construction source");
    check(tracker.observeNeutral({0, timestamp, 0, {}}), "constructed neutral fixture");
  }

  CGEventRef
  input(std::uint16_t code, CGEventType type, std::uint64_t flags, bool repeat = false, std::uint32_t keyboardType = 41)
  {
    // Independent Quartz fixture construction. These objects are never posted.
    CGEventRef event = CGEventCreateKeyboardEvent(source.get(), code, type != kCGEventKeyUp);
    check(event, "constructed input event");
    CGEventSetType(event, type);
    CGEventSetFlags(event, static_cast<CGEventFlags>(flags));
    CGEventSetTimestamp(event, ++timestamp);
    CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, repeat ? 1 : 0);
    CGEventSetIntegerValueField(event, kCGKeyboardEventKeyboardType, keyboardType);
    return event;
  }

  OriginalKeyboardTracker::Observation observe(CGEventRef event)
  {
    const auto captured = captureOriginalKeyboardEvent(event, ++sequence);
    check(captured.status == OriginalKeyCapture::Status::Captured && captured.sample.has_value(), "fixture captured");
    return tracker.observe(*captured.sample);
  }

  OriginalKeyEvent normalized(
      std::uint16_t code, CGEventType type, std::uint64_t flags, bool repeat = false, std::uint32_t keyboardType = 41
  )
  {
    Owned<CGEventRef> inputEvent{input(code, type, flags, repeat, keyboardType)};
    const auto observed = observe(inputEvent.get());
    check(
        observed.status == OriginalKeyboardTracker::Status::Accepted && observed.event.has_value(),
        "native fixture normalized"
    );
    return *observed.event;
  }
};

void checkLoopSuppression(CGEventRef event)
{
  check(event, "constructed output exists");
  check(
      CGEventGetIntegerValueField(event, kCGEventSourceUserData) == syntheticEventTag, "output has exact loop marker"
  );
  Owned<CGEventRef> copy{CGEventCreateCopy(event)};
  const auto captured = captureOriginalKeyboardEvent(copy.get(), 100000);
  check(
      captured.status == OriginalKeyCapture::Status::Ignored && !captured.sample,
      "output and Quartz copy cannot recirculate"
  );
}

void testNativeSidesAndKeyboardTypes()
{
  Fixture fixture;
  const std::pair<std::uint16_t, std::uint64_t> sides[][2] = {
      {{0x38, 0x00020002}, {0x3c, 0x00020004}},
      {{0x3b, 0x00040001}, {0x3e, 0x00042000}},
      {{0x3a, 0x00080020}, {0x3d, 0x00080040}},
      {{0x37, 0x00100008}, {0x36, 0x00100010}}
  };
  for (const auto &pair : sides) {
    const std::pair<std::uint16_t, std::uint64_t> changes[] = {
        {pair[0].first, pair[0].second},
        {pair[1].first, pair[0].second | pair[1].second},
        {pair[0].first, pair[1].second},
        {pair[1].first, 0}
    };
    for (unsigned index = 0; index < std::size(changes); ++index) {
      const auto [code, flags] = changes[index];
      const auto event = fixture.normalized(code, kCGEventFlagsChanged, flags | 0x100);
      check(
          event.transition == (index < 2 ? OriginalKeyTransition::Down : OriginalKeyTransition::Up),
          "CG flagsChanged direction follows actual changed side"
      );
      Owned<CGEventRef> output{createOriginalKeyboardEvent(event, fixture.source.get())};
      checkLoopSuppression(output.get());
      check(CGEventGetType(output.get()) == kCGEventFlagsChanged, "modifier output remains flagsChanged");
      check(CGEventGetFlags(output.get()) == (flags | 0x100), "all raw side and noncoalesced bits preserved");
      check(
          CGEventGetIntegerValueField(output.get(), kCGKeyboardEventKeycode) == code,
          "left/right virtual code preserved"
      );
    }
  }
  for (const std::uint32_t keyboardType : {40U, 41U, 42U}) {
    const auto down = fixture.normalized(0x32, kCGEventKeyDown, 0, false, keyboardType);
    Owned<CGEventRef> output{createOriginalKeyboardEvent(down, fixture.source.get())};
    checkLoopSuppression(output.get());
    check(
        CGEventGetIntegerValueField(output.get(), kCGKeyboardEventKeyboardType) == keyboardType,
        "keyboard type remains attached to original code"
    );
    check(CGEventGetTimestamp(output.get()) == down.sample.timestampNs, "capture timestamp retained exactly");
    fixture.normalized(0x32, kCGEventKeyUp, 0, false, keyboardType);
  }
  const auto caps = fixture.normalized(0x39, kCGEventFlagsChanged, 0x00010000);
  check(
      caps.transition == OriginalKeyTransition::Toggle && fixture.tracker.neutral(),
      "native Caps Lock update has no held press"
  );
  Owned<CGEventRef> capsOutput{createOriginalKeyboardEvent(caps, fixture.source.get())};
  checkLoopSuppression(capsOutput.get());
  check(
      CGEventGetType(capsOutput.get()) == kCGEventFlagsChanged && CGEventGetFlags(capsOutput.get()) == 0x00010000,
      "Caps Lock preserves state rather than fabricating key down"
  );
}

void testRepeatAfterModifierRelease()
{
  Fixture fixture;
  OriginalKeyLedger ledger;
  ledger.route(fixture.normalized(0x37, kCGEventFlagsChanged, 0x00100008), 29);
  ledger.route(fixture.normalized(0, kCGEventKeyDown, 0x00100008), 29);
  ledger.route(fixture.normalized(0x37, kCGEventFlagsChanged, 0), 29);
  const auto repeat = ledger.route(fixture.normalized(0, kCGEventKeyDown, 0, true), 30);
  check(repeat.delivery && repeat.delivery->owner == 29, "native repeat retains original owner after focus change");
  Owned<CGEventRef> output{createOriginalKeyboardEvent(repeat.delivery->event, fixture.source.get())};
  checkLoopSuppression(output.get());
  check(CGEventGetIntegerValueField(output.get(), kCGKeyboardEventAutorepeat) == 1, "native repeat bit retained");
  check(CGEventGetFlags(output.get()) == 0, "released Command not copied from original down into repeat");
  const auto up = ledger.route(fixture.normalized(0, kCGEventKeyUp, 0), 30);
  check(up.delivery && up.delivery->owner == 29 && ledger.empty(), "native up balances same original press");
}

void testConstructedEmergencyAndReleases()
{
  Fixture fixture;
  OriginalKeyLedger ledger;
  constexpr std::uint64_t shift = 0x00020002, control = 0x00042000, option = 0x00080020;
  ledger.route(fixture.normalized(0x38, kCGEventFlagsChanged, shift), 51);
  ledger.route(fixture.normalized(0x3e, kCGEventFlagsChanged, shift | control), 51);
  ledger.route(fixture.normalized(0x3a, kCGEventFlagsChanged, shift | control | option), 51);
  ledger.route(fixture.normalized(0, kCGEventKeyDown, shift | control | option), 51);
  Owned<CGEventRef> escape{fixture.input(0x35, kCGEventKeyDown, shift | control | option)};
  const auto observed = fixture.observe(escape.get());
  check(
      observed.emergency && observed.consumed && observed.event,
      "native reserved chord detected before any posting/routing"
  );
  const auto releases = ledger.revokeAll();
  check(releases.size() == 4, "emergency generates all original owner releases");
  for (const auto &release : releases) {
    Owned<CGEventRef> output{createOriginalKeyboardRelease(release, fixture.timestamp + 100, fixture.source.get())};
    checkLoopSuppression(output.get());
    check(
        CGEventGetType(output.get()) == (release.press.keyCode == 0 ? kCGEventKeyUp : kCGEventFlagsChanged),
        "forced key/modifier release has proper native kind"
    );
    check(CGEventGetFlags(output.get()) == release.flags, "release retains only recipient-owned modifiers");
    check(
        CGEventGetIntegerValueField(output.get(), kCGKeyboardEventAutorepeat) == 0, "forced releases cannot autorepeat"
    );
    check(
        CGEventGetTimestamp(output.get()) == fixture.timestamp + 100,
        "forced release uses release time without fabricated capture sequence"
    );
  }
  check(releases[0].press.keyCode == 0 && releases.back().flags == 0, "letters release before final modifier clear");
  check(!ledger.route(*observed.event, 0).delivery, "reserved Escape is tombstoned and never injected");
}

void testMalformedAndPrivateSourceOnly()
{
  Fixture fixture;
  check(captureOriginalKeyboardEvent(nullptr, 1).status == OriginalKeyCapture::Status::Ignored, "null events ignored");
  Owned<CGEventRef> mouse{
      CGEventCreateMouseEvent(fixture.source.get(), kCGEventMouseMoved, CGPointZero, kCGMouseButtonLeft)
  };
  check(
      captureOriginalKeyboardEvent(mouse.get(), 1).status == OriginalKeyCapture::Status::Ignored,
      "keyboard adapter ignores pointer events"
  );
  Owned<CGEventRef> event{fixture.input(0, kCGEventKeyDown, 0)};
  check(
      captureOriginalKeyboardEvent(event.get(), 0).status == OriginalKeyCapture::Status::Malformed,
      "zero capture sequence malformed"
  );
  CGEventSetTimestamp(event.get(), 0);
  check(
      captureOriginalKeyboardEvent(event.get(), 1).status == OriginalKeyCapture::Status::Malformed,
      "zero native timestamp malformed"
  );
  CGEventSetTimestamp(event.get(), 1001);
  CGEventSetIntegerValueField(event.get(), kCGKeyboardEventKeycode, 256);
  check(
      captureOriginalKeyboardEvent(event.get(), 1).status == OriginalKeyCapture::Status::Malformed,
      "out-of-range native keycode rejected"
  );
  CGEventSetIntegerValueField(event.get(), kCGKeyboardEventKeycode, 0);
  CGEventSetIntegerValueField(event.get(), kCGKeyboardEventAutorepeat, 2);
  check(
      captureOriginalKeyboardEvent(event.get(), 1).status == OriginalKeyCapture::Status::Malformed,
      "non-boolean native repeat rejected"
  );
  CGEventSetIntegerValueField(event.get(), kCGKeyboardEventAutorepeat, 0);
  CGEventSetType(event.get(), kCGEventFlagsChanged);
  check(
      captureOriginalKeyboardEvent(event.get(), 1).status == OriginalKeyCapture::Status::Malformed,
      "flagsChanged ordinary key cannot acquire authority"
  );
  Owned<CGEventRef> tagged{fixture.input(0, kCGEventKeyDown, 0)};
  CGEventSetIntegerValueField(tagged.get(), kCGEventSourceUserData, syntheticEventTag);
  check(
      captureOriginalKeyboardEvent(tagged.get(), 0).status == OriginalKeyCapture::Status::Ignored,
      "loop marker takes precedence even when capture sequence is malformed"
  );

  const auto ordinary = fixture.normalized(0, kCGEventKeyDown, 0);
  check(!createOriginalKeyboardEvent(ordinary, nullptr), "default/global source never used");
  Owned<CGEventSourceRef> shared{CGEventSourceCreate(kCGEventSourceStateCombinedSessionState)};
  check(shared.get(), "constructed shared source fixture");
  check(
      !createOriginalKeyboardEvent(ordinary, shared.get()), "combined-session source rejected before event construction"
  );
  Owned<CGEventSourceRef> hid{CGEventSourceCreate(kCGEventSourceStateHIDSystemState)};
  check(hid.get(), "constructed HID source fixture");
  check(!createOriginalKeyboardEvent(ordinary, hid.get()), "HID-system source rejected before event construction");
  auto invalid = ordinary;
  invalid.transition = OriginalKeyTransition::Up;
  check(!createOriginalKeyboardEvent(invalid, fixture.source.get()), "contradictory transition cannot be constructed");
  OriginalKeyRelease release{1, ordinary.sample, 0};
  check(
      !createOriginalKeyboardRelease(release, ordinary.sample.timestampNs - 1, fixture.source.get()),
      "forced release cannot predate original press"
  );
  release.owner = 0;
  check(
      !createOriginalKeyboardRelease(release, ordinary.sample.timestampNs, fixture.source.get()),
      "unowned release rejected"
  );
  release.owner = 1;
  release.press.keyCode = 0x38;
  release.press.kind = OriginalKeyKind::FlagsChanged;
  release.press.flags = 0;
  check(
      !createOriginalKeyboardRelease(release, ordinary.sample.timestampNs, fixture.source.get()),
      "modifier-up cannot stand in for delivered down"
  );
  release.press.flags = 0x00020002;
  release.flags = 0x00020002;
  check(
      !createOriginalKeyboardRelease(release, ordinary.sample.timestampNs, fixture.source.get()),
      "forced release cannot leave same side pressed"
  );
  release.flags = 0x00020004;
  Owned<CGEventRef> otherSide{
      createOriginalKeyboardRelease(release, ordinary.sample.timestampNs, fixture.source.get())
  };
  checkLoopSuppression(otherSide.get());
  check(CGEventGetFlags(otherSide.get()) == 0x00020004, "forced left release may retain owned right side");
  release.flags = 0x00020000;
  check(
      !createOriginalKeyboardRelease(release, ordinary.sample.timestampNs, fixture.source.get()),
      "ambiguous aggregate-only forced release rejected"
  );
}
} // namespace

int main()
{
  try {
    testNativeSidesAndKeyboardTypes();
    testRepeatAfterModifierRelease();
    testConstructedEmergencyAndReleases();
    testMalformedAndPrivateSourceOnly();
    std::cout
        << "PASS: constructed native keyboard events, side/repeat fidelity, and release/loop invariants (no posting)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
