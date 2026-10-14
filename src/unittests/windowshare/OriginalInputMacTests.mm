/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "platform/OriginalInput_mac.h"
#include "windowshare/PlatformMarkers.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

// The adapter may call read-only Quartz getters, but must not allocate C++
// storage on the capture path. Construct all private fixtures before this guard.
namespace {
thread_local bool rejectNew = false;
}
void *operator new(std::size_t size)
{
  if (rejectNew)
    std::abort();
  if (auto *memory = std::malloc(size ? size : 1))
    return memory;
  throw std::bad_alloc();
}
void operator delete(void *memory) noexcept
{
  std::free(memory);
}
void operator delete(void *memory, std::size_t) noexcept
{
  std::free(memory);
}

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

constexpr std::uint64_t epoch = 79, sequence = 123, timestamp = 9876543210123;
constexpr std::uint64_t modifiers = originalKey::commandFlag | originalKey::rightCommandFlag | 0x100;
constexpr CGPoint sourcePoint{193, 207}, targetPoint{-48.25, 91.5};
constexpr auto minimum = std::numeric_limits<std::int32_t>::min(), maximum = std::numeric_limits<std::int32_t>::max();

OriginalInputCapture capture(CGEventType type, CGEventRef event, std::uint64_t id = epoch, std::uint64_t seq = sequence)
{
  rejectNew = true;
  const auto result = captureOriginalInputEvent(type, event, id, seq);
  rejectNew = false;
  return result;
}

OriginalInputRecord captured(CGEventRef event)
{
  const auto result = capture(CGEventGetType(event), event);
  check(result.status == OriginalInputCapture::Status::Captured && result.record.has_value(), "fixture captured");
  check(validOriginalInputRecord(*result.record), "captured record passes shared validation");
  check(
      result.record->epoch == epoch && result.record->sequence == sequence &&
          result.record->sourceTimestampNs == timestamp && result.record->flags == CGEventGetFlags(event),
      "capture metadata remains exact"
  );
  return *result.record;
}

struct Fixture
{
  Owned<CGEventSourceRef> source{CGEventSourceCreate(kCGEventSourceStatePrivate)};
  Fixture()
  {
    check(source.get(), "private event source exists");
  }

  void common(CGEventRef event, std::uint64_t flags = modifiers) const
  {
    check(event, "private fixture exists");
    CGEventSetTimestamp(event, timestamp);
    CGEventSetFlags(event, static_cast<CGEventFlags>(flags));
    CGEventSetIntegerValueField(event, kCGEventSourceUserData, 0);
  }

  CGEventRef key(std::uint16_t code, CGEventType type, std::uint64_t flags, bool repeat = false) const
  {
    auto event = CGEventCreateKeyboardEvent(source.get(), code, type != kCGEventKeyUp);
    common(event, flags);
    CGEventSetType(event, type);
    CGEventSetIntegerValueField(event, kCGKeyboardEventKeycode, code);
    CGEventSetIntegerValueField(event, kCGKeyboardEventKeyboardType, 42);
    CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, repeat ? 1 : 0);
    return event;
  }

  CGEventRef pointer(CGEventType type, std::uint8_t button = 0) const
  {
    auto event = CGEventCreateMouseEvent(source.get(), type, sourcePoint, static_cast<CGMouseButton>(button));
    common(event);
    CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, button);
    CGEventSetIntegerValueField(event, kCGMouseEventClickState, 2);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, -19);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, 37);
    return event;
  }

  CGEventRef scroll() const
  {
    auto event = CGEventCreateScrollWheelEvent2(source.get(), kCGScrollEventUnitPixel, 3, 0, 0, 0);
    common(event);
    CGEventSetLocation(event, sourcePoint);
    // Values differ in every axis/representation: conversion or axis swapping
    // cannot accidentally pass merely because all default values were zero.
    CGEventSetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1, -7);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2, 3);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1, -71);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2, 33);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis1, -0x28001);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis2, 0x18001);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventDeltaAxis3, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis3, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis3, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventIsContinuous, 1);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventScrollPhase, kCGScrollPhaseChanged);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventMomentumPhase, kCGMomentumScrollPhaseNone);
    return event;
  }
};

void checkOutput(CGEventRef event, CGEventSourceRef source, const OriginalInputRecord &record, bool pointer = true)
{
  check(event, "retained construction succeeds");
  check(
      CGEventGetTimestamp(event) == record.sourceTimestampNs && CGEventGetFlags(event) == record.flags,
      "timestamp and every event-local flag are preserved"
  );
  check(
      CGEventGetIntegerValueField(event, kCGEventSourceUserData) == syntheticEventTag &&
          CGEventGetIntegerValueField(event, kCGEventSourceStateID) == CGEventSourceGetSourceStateID(source),
      "constructed event retains private state and exact loop marker"
  );
  if (pointer) {
    const auto point = CGEventGetLocation(event);
    check(point.x == targetPoint.x && point.y == targetPoint.y, "explicit target replaces source location");
  }
  const auto ignored = capture(CGEventGetType(event), event, 0, 0);
  check(
      ignored.status == OriginalInputCapture::Status::Ignored && !ignored.record,
      "tagged construction cannot recirculate even with invalid capture sequence"
  );
  Owned<CGEventRef> copy{CGEventCreateCopy(event)};
  check(copy.get(), "Quartz copy exists");
  check(
      capture(CGEventGetType(copy.get()), copy.get()).status == OriginalInputCapture::Status::Ignored,
      "Quartz copy preserves loop suppression"
  );
}

void testKeysAndRawModifierTransitions()
{
  Fixture fixture;
  const std::pair<std::uint16_t, std::uint64_t> sides[][2] = {
      {{0x38, 0x00020002}, {0x3c, 0x00020004}},
      {{0x3b, 0x00040001}, {0x3e, 0x00042000}},
      {{0x3a, 0x00080020}, {0x3d, 0x00080040}},
      {{0x37, 0x00100008}, {0x36, 0x00100010}}
  };
  for (const auto &pair : sides) {
    const std::pair<std::uint16_t, std::uint64_t> changes[]{
        pair[0], {pair[1].first, pair[0].second | pair[1].second}, {pair[0].first, pair[1].second}, {pair[1].first, 0}
    };
    for (unsigned index = 0; index < std::size(changes); ++index) {
      const auto [code, flags] = changes[index];
      Owned<CGEventRef> event{fixture.key(code, kCGEventFlagsChanged, flags | 0x100)};
      const auto record = captured(event.get());
      const auto key = std::get<OriginalInputKey>(record.payload);
      check(
          key.code == code && key.keyboardType == 42 && key.kind == OriginalKeyKind::FlagsChanged && !key.repeat &&
              key.transition == (index < 2 ? OriginalKeyTransition::Down : OriginalKeyTransition::Up),
          "modifier direction follows its own side even while opposite side stays down"
      );
      Owned<CGEventRef> output{createOriginalInputEvent(record, targetPoint, fixture.source.get())};
      checkOutput(output.get(), fixture.source.get(), record, false);
      check(CGEventGetType(output.get()) == kCGEventFlagsChanged, "modifier output preserves native event kind");
      check(CGEventGetIntegerValueField(output.get(), kCGKeyboardEventKeycode) == code, "modifier side code preserved");
    }
  }
  for (const auto flags : {originalKey::functionFlag, std::uint64_t{0}}) {
    Owned<CGEventRef> event{fixture.key(originalKey::function, kCGEventFlagsChanged, flags)};
    const auto record = captured(event.get());
    check(
        std::get<OriginalInputKey>(record.payload).transition ==
            (flags ? OriginalKeyTransition::Down : OriginalKeyTransition::Up),
        "Fn direction follows its own flag"
    );
    Owned<CGEventRef> output{createOriginalInputEvent(record, targetPoint, fixture.source.get())};
    checkOutput(output.get(), fixture.source.get(), record, false);
  }
  for (const auto flags : {originalKey::capsLockFlag, std::uint64_t{0}, originalKey::capsPhysicalFlags}) {
    Owned<CGEventRef> event{fixture.key(originalKey::capsLock, kCGEventFlagsChanged, flags)};
    const auto record = captured(event.get());
    check(
        std::get<OriginalInputKey>(record.payload).transition == OriginalKeyTransition::Toggle, "Caps is only Toggle"
    );
    Owned<CGEventRef> output{createOriginalInputEvent(record, targetPoint, fixture.source.get())};
    checkOutput(output.get(), fixture.source.get(), record, false);
  }
  for (const auto type : {kCGEventKeyDown, kCGEventKeyUp}) {
    for (const bool repeat : {false, true}) {
      if (type == kCGEventKeyUp && repeat)
        continue;
      Owned<CGEventRef> event{fixture.key(0x32, type, modifiers, repeat)};
      const auto record = captured(event.get());
      const auto key = std::get<OriginalInputKey>(record.payload);
      check(
          key.code == 0x32 && key.keyboardType == 42 && key.repeat == repeat &&
              key.transition == (type == kCGEventKeyUp ? OriginalKeyTransition::Up
                                 : repeat              ? OriginalKeyTransition::Repeat
                                                       : OriginalKeyTransition::Down),
          "ordinary key preserves raw code/type/repeat without translating text"
      );
      Owned<CGEventRef> output{createOriginalInputEvent(record, targetPoint, fixture.source.get())};
      checkOutput(output.get(), fixture.source.get(), record, false);
      check(CGEventGetIntegerValueField(output.get(), kCGKeyboardEventAutorepeat) == repeat, "repeat preserved");
      check(CGEventGetIntegerValueField(output.get(), kCGKeyboardEventKeyboardType) == 42, "keyboard type preserved");
    }
  }
}

void testMotionAndEveryButton()
{
  Fixture fixture;
  for (unsigned button = 0; button <= originalInputButtonCount; ++button) {
    const auto type = button == originalInputButtonCount ? kCGEventMouseMoved
                      : button == 0                      ? kCGEventLeftMouseDragged
                      : button == 1                      ? kCGEventRightMouseDragged
                                                         : kCGEventOtherMouseDragged;
    Owned<CGEventRef> event{fixture.pointer(type, button == originalInputButtonCount ? 0 : button)};
    // Quartz stores whole mouse deltas in a narrower native field on current
    // macOS. Test representable endpoints here and reject lossy larger records
    // below; setting INT32_MAX in a fixture silently reads back -1.
    for (const auto delta : {std::int64_t{0}, std::int64_t{-19}, std::int64_t{-32768}, std::int64_t{32767}}) {
      CGEventSetIntegerValueField(event.get(), kCGMouseEventDeltaX, delta);
      CGEventSetIntegerValueField(event.get(), kCGMouseEventDeltaY, delta == -32768 ? 32767 : -delta);
      const auto record = captured(event.get());
      const auto motion = std::get<OriginalInputMotion>(record.payload);
      check(
          motion.dragButton == button && motion.dx == delta && motion.dy == (delta == -32768 ? 32767 : -delta),
          "motion and drag preserve signed deltas and button owner domain"
      );
      Owned<CGEventRef> output{createOriginalInputEvent(record, targetPoint, fixture.source.get())};
      checkOutput(output.get(), fixture.source.get(), record);
      check(CGEventGetType(output.get()) == type, "motion/drag native kind retained");
      check(CGEventGetIntegerValueField(output.get(), kCGMouseEventDeltaX) == motion.dx, "horizontal delta exact");
      check(CGEventGetIntegerValueField(output.get(), kCGMouseEventDeltaY) == motion.dy, "vertical delta exact");
    }
    if (button == originalInputButtonCount)
      continue;
    for (const bool down : {true, false}) {
      const auto buttonType = button == 0   ? (down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp)
                              : button == 1 ? (down ? kCGEventRightMouseDown : kCGEventRightMouseUp)
                                            : (down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp);
      Owned<CGEventRef> press{fixture.pointer(buttonType, button)};
      for (const auto clicks : {1, 2, 3, maximum}) {
        CGEventSetIntegerValueField(press.get(), kCGMouseEventClickState, clicks);
        const auto record = captured(press.get());
        const auto value = std::get<OriginalInputButton>(record.payload);
        check(
            value.button == button && value.down == down && value.clickCount == static_cast<unsigned>(clicks),
            "every button and click count preserved"
        );
        Owned<CGEventRef> output{createOriginalInputEvent(record, targetPoint, fixture.source.get())};
        checkOutput(output.get(), fixture.source.get(), record);
        check(CGEventGetType(output.get()) == buttonType, "button direction native kind retained");
        check(CGEventGetIntegerValueField(output.get(), kCGMouseEventClickState) == clicks, "click count exact");
      }
    }
  }
}

void testScrollRepresentationsAndPhases()
{
  Fixture fixture;
  Owned<CGEventRef> event{fixture.scroll()};
  const auto record = captured(event.get());
  const auto scroll = std::get<OriginalInputScroll>(record.payload);
  check(scroll.lines == std::array<std::int64_t, 2>{3, -7}, "line axes are horizontal/vertical");
  check(scroll.points == std::array<std::int64_t, 2>{33, -71}, "point axes preserved independently of lines");
  check(
      scroll.fixed16_16 == std::array<std::int64_t, 2>{0x18001, -0x28001},
      "signed fixed16.16 values preserve fractional low bits"
  );
  check(
      CGEventGetDoubleValueField(event.get(), kCGScrollWheelEventFixedPtDeltaAxis2) == 0x18001 / 65536.0,
      "independent Quartz double accessor confirms raw fixed-point interpretation"
  );
  const std::pair<OriginalScrollPhase, OriginalMomentumPhase> phases[]{
      {OriginalScrollPhase::None, OriginalMomentumPhase::None},
      {OriginalScrollPhase::Began, OriginalMomentumPhase::None},
      {OriginalScrollPhase::Changed, OriginalMomentumPhase::None},
      {OriginalScrollPhase::Ended, OriginalMomentumPhase::None},
      {OriginalScrollPhase::Cancelled, OriginalMomentumPhase::None},
      {OriginalScrollPhase::MayBegin, OriginalMomentumPhase::None},
      {OriginalScrollPhase::None, OriginalMomentumPhase::Began},
      {OriginalScrollPhase::None, OriginalMomentumPhase::Continued},
      {OriginalScrollPhase::None, OriginalMomentumPhase::Ended}
  };
  const CGEventField fields[]{kCGScrollWheelEventDeltaAxis1,        kCGScrollWheelEventDeltaAxis2,
                              kCGScrollWheelEventPointDeltaAxis1,   kCGScrollWheelEventPointDeltaAxis2,
                              kCGScrollWheelEventFixedPtDeltaAxis1, kCGScrollWheelEventFixedPtDeltaAxis2};
  for (const auto [phase, momentum] : phases) {
    for (const auto value : {std::int64_t{0}, std::int64_t{-1}, std::int64_t{minimum}, std::int64_t{maximum}}) {
      CGEventSetIntegerValueField(event.get(), kCGScrollWheelEventScrollPhase, static_cast<unsigned>(phase));
      CGEventSetIntegerValueField(event.get(), kCGScrollWheelEventMomentumPhase, static_cast<unsigned>(momentum));
      // Line fields, like mouse deltas, are narrower on this Quartz runtime.
      // Point and fixed-point fields retain the full signed32 domain.
      const auto line = value < -32768 ? -32768 : value > 32767 ? 32767 : value;
      for (const auto field : fields) {
        const bool lineField = field == kCGScrollWheelEventDeltaAxis1 || field == kCGScrollWheelEventDeltaAxis2;
        CGEventSetIntegerValueField(event.get(), field, lineField ? line : value);
      }
      const auto input = captured(event.get());
      const auto data = std::get<OriginalInputScroll>(input.payload);
      check(
          data.phase == phase && data.momentum == momentum && data.continuous, "all supported scroll phases retained"
      );
      for (std::size_t axis = 0; axis < 2; ++axis)
        check(
            data.lines[axis] == line && data.points[axis] == value && data.fixed16_16[axis] == value,
            "signed scroll limits exact"
        );
      Owned<CGEventRef> output{createOriginalInputEvent(input, targetPoint, fixture.source.get())};
      checkOutput(output.get(), fixture.source.get(), input);
      check(CGEventGetType(output.get()) == kCGEventScrollWheel, "scroll event native kind retained");
      for (const auto field : fields) {
        const bool lineField = field == kCGScrollWheelEventDeltaAxis1 || field == kCGScrollWheelEventDeltaAxis2;
        check(
            CGEventGetIntegerValueField(output.get(), field) == (lineField ? line : value),
            "all six scroll fields retain exact integer bits"
        );
      }
      check(
          CGEventGetIntegerValueField(output.get(), kCGScrollWheelEventScrollPhase) == static_cast<unsigned>(phase),
          "scroll phase exact"
      );
      check(
          CGEventGetIntegerValueField(output.get(), kCGScrollWheelEventMomentumPhase) ==
              static_cast<unsigned>(momentum),
          "momentum phase exact"
      );
    }
  }
  CGEventSetIntegerValueField(event.get(), kCGScrollWheelEventIsContinuous, 0);
  CGEventSetIntegerValueField(event.get(), kCGScrollWheelEventScrollPhase, 0);
  CGEventSetIntegerValueField(event.get(), kCGScrollWheelEventMomentumPhase, 0);
  const auto wheel = captured(event.get());
  check(!std::get<OriginalInputScroll>(wheel.payload).continuous, "discrete wheel remains noncontinuous");
  Owned<CGEventRef> output{createOriginalInputEvent(wheel, targetPoint, fixture.source.get())};
  checkOutput(output.get(), fixture.source.get(), wheel);
  check(
      CGEventGetIntegerValueField(output.get(), kCGScrollWheelEventIsContinuous) == 0,
      "discrete construction does not gain phase"
  );
}

void malformed(CGEventRef event)
{
  const auto result = capture(CGEventGetType(event), event);
  if (result.status != OriginalInputCapture::Status::Malformed || result.record) {
    // Only independently constructed fixtures reach this diagnostic.
    std::cerr << "Unexpected fixture acceptance: type=" << CGEventGetType(event)
              << " code=" << CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode)
              << " repeat=" << CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat)
              << " button=" << CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber)
              << " clicks=" << CGEventGetIntegerValueField(event, kCGMouseEventClickState)
              << " phase=" << CGEventGetIntegerValueField(event, kCGScrollWheelEventScrollPhase)
              << " momentum=" << CGEventGetIntegerValueField(event, kCGScrollWheelEventMomentumPhase)
              << " continuous=" << CGEventGetIntegerValueField(event, kCGScrollWheelEventIsContinuous) << '\n';
  }
  check(result.status == OriginalInputCapture::Status::Malformed && !result.record, "malformed fixture fails closed");
}

void testMalformedInputAndDiscontinuities()
{
  Fixture fixture;
  for (const auto type : {kCGEventTapDisabledByTimeout, kCGEventTapDisabledByUserInput}) {
    const auto disabled = capture(type, nullptr, 0, 0);
    check(
        disabled.status == OriginalInputCapture::Status::Discontinuity && !disabled.record,
        "tap disable has no captured record"
    );
    Owned<CGEventRef> tagged{fixture.key(0, kCGEventKeyDown, 0)};
    CGEventSetIntegerValueField(tagged.get(), kCGEventSourceUserData, syntheticEventTag);
    check(
        capture(type, tagged.get()).status == OriginalInputCapture::Status::Discontinuity,
        "disable notification wins over irrelevant tagged event"
    );
  }
  check(
      capture(kCGEventNull, nullptr).status == OriginalInputCapture::Status::Ignored, "unsupported null type ignored"
  );
  check(
      capture(kCGEventTabletPointer, nullptr).status == OriginalInputCapture::Status::Ignored,
      "unsupported tablet event ignored"
  );
  for (const auto type : {kCGEventKeyDown, kCGEventMouseMoved, kCGEventScrollWheel}) {
    check(
        capture(type, nullptr).status == OriginalInputCapture::Status::Malformed,
        "supported null event is discontinuity data, not empty capture"
    );
  }
  Owned<CGEventRef> event{fixture.key(0, kCGEventKeyDown, 0)};
  check(
      capture(kCGEventKeyUp, event.get()).status == OriginalInputCapture::Status::Malformed,
      "callback/event type mismatch rejected"
  );
  check(
      capture(kCGEventKeyDown, event.get(), 0).status == OriginalInputCapture::Status::Malformed, "zero epoch rejected"
  );
  check(
      capture(kCGEventKeyDown, event.get(), epoch, 0).status == OriginalInputCapture::Status::Malformed,
      "zero sequence rejected"
  );
  CGEventSetTimestamp(event.get(), 0);
  malformed(event.get());
  CGEventSetTimestamp(event.get(), timestamp);
  CGEventSetFlags(event.get(), originalKey::commandFlag);
  malformed(event.get());
  CGEventSetFlags(event.get(), originalKey::rightCommandFlag);
  malformed(event.get());
  CGEventSetFlags(event.get(), 0);
  CGEventSetIntegerValueField(event.get(), kCGKeyboardEventAutorepeat, 2);
  malformed(event.get());
  CGEventSetIntegerValueField(event.get(), kCGKeyboardEventAutorepeat, 0);
  CGEventSetIntegerValueField(event.get(), kCGKeyboardEventKeycode, 256);
  malformed(event.get());
  CGEventSetIntegerValueField(event.get(), kCGKeyboardEventKeycode, 0);
  CGEventSetType(event.get(), kCGEventFlagsChanged);
  malformed(event.get());
  CGEventSetType(event.get(), kCGEventKeyUp);
  CGEventSetIntegerValueField(event.get(), kCGKeyboardEventAutorepeat, 1);
  malformed(event.get());

  for (const auto type : {kCGEventLeftMouseDown, kCGEventRightMouseUp, kCGEventOtherMouseDragged}) {
    Owned<CGEventRef> pointer{fixture.pointer(type, type == kCGEventOtherMouseDragged ? 2 : 0)};
    CGEventSetIntegerValueField(pointer.get(), kCGMouseEventButtonNumber, type == kCGEventLeftMouseDown ? 1 : 0);
    malformed(pointer.get());
  }
  Owned<CGEventRef> pointer{fixture.pointer(kCGEventOtherMouseDown, 2)};
  for (const auto button : {-1, 32}) {
    CGEventSetIntegerValueField(pointer.get(), kCGMouseEventButtonNumber, button);
    malformed(pointer.get());
  }
  CGEventSetIntegerValueField(pointer.get(), kCGMouseEventButtonNumber, 2);
  for (const auto clicks : {0, -1}) {
    CGEventSetIntegerValueField(pointer.get(), kCGMouseEventClickState, clicks);
    malformed(pointer.get());
  }
  CGEventSetIntegerValueField(pointer.get(), kCGMouseEventClickState, 1);
  for (const auto subtype : {kCGEventMouseSubtypeTabletPoint, kCGEventMouseSubtypeTabletProximity}) {
    CGEventSetIntegerValueField(pointer.get(), kCGMouseEventSubtype, subtype);
    malformed(pointer.get());
  }
  Owned<CGEventRef> scroll{fixture.scroll()};
  const CGEventField third[]{
      kCGScrollWheelEventDeltaAxis3, kCGScrollWheelEventPointDeltaAxis3, kCGScrollWheelEventFixedPtDeltaAxis3
  };
  for (const auto field : third) {
    for (const auto value : {std::int64_t{1}, std::int64_t{-1}, std::int64_t{minimum}, std::int64_t{maximum}}) {
      const auto actual = field == kCGScrollWheelEventDeltaAxis3 ? (value < -32768  ? -32768
                                                                    : value > 32767 ? 32767
                                                                                    : value)
                                                                 : value;
      CGEventSetIntegerValueField(scroll.get(), field, actual);
      check(CGEventGetIntegerValueField(scroll.get(), field) == actual, "nonzero third-axis fixture is representable");
      malformed(scroll.get());
    }
    CGEventSetIntegerValueField(scroll.get(), field, 0);
  }
  CGEventSetIntegerValueField(scroll.get(), kCGScrollWheelEventMomentumPhase, kCGMomentumScrollPhaseBegin);
  malformed(scroll.get()); // Gesture + momentum concurrently is outside this protocol.
  CGEventSetIntegerValueField(scroll.get(), kCGScrollWheelEventMomentumPhase, 0);
  for (const auto phase : {3, 15, 129}) {
    CGEventSetIntegerValueField(scroll.get(), kCGScrollWheelEventScrollPhase, phase);
    check(
        CGEventGetIntegerValueField(scroll.get(), kCGScrollWheelEventScrollPhase) == phase,
        "invalid combined phase fixture is representable"
    );
    malformed(scroll.get());
  }
  CGEventSetIntegerValueField(scroll.get(), kCGScrollWheelEventScrollPhase, kCGScrollPhaseChanged);
  CGEventSetIntegerValueField(scroll.get(), kCGScrollWheelEventIsContinuous, 0);
  malformed(scroll.get());
  CGEventSetIntegerValueField(scroll.get(), kCGScrollWheelEventScrollPhase, 0);
  CGEventSetIntegerValueField(scroll.get(), kCGScrollWheelEventMomentumPhase, kCGMomentumScrollPhaseBegin);
  malformed(scroll.get()); // Momentum on a noncontinuous wheel is unsupported.
}

void testConstructionLimitsAndSources()
{
  Fixture fixture;
  Owned<CGEventRef> input{fixture.pointer(kCGEventMouseMoved)};
  auto record = captured(input.get());
  check(!createOriginalInputEvent(record, targetPoint, nullptr), "null construction source rejected");
  Owned<CGEventSourceRef> combined{CGEventSourceCreate(kCGEventSourceStateCombinedSessionState)};
  Owned<CGEventSourceRef> hid{CGEventSourceCreate(kCGEventSourceStateHIDSystemState)};
  check(combined.get() && hid.get(), "shared-source test fixtures exist");
  for (const auto source : {combined.get(), hid.get()}) {
    check(!createOriginalInputEvent(record, targetPoint, source), "shared state source rejected");
    auto scroll = record;
    scroll.payload = OriginalInputScroll{};
    check(!createOriginalInputEvent(scroll, targetPoint, source), "shared scroll source rejected");
    auto key = record;
    key.payload = OriginalInputKey{};
    check(!createOriginalInputEvent(key, targetPoint, source), "shared keyboard source rejected");
  }
  for (const auto point :
       {CGPoint{NAN, 0}, CGPoint{0, INFINITY}, CGPoint{double(maximum) + 1, 0}, CGPoint{0, double(minimum) - 1}})
    check(!createOriginalInputEvent(record, point, fixture.source.get()), "invalid or unbounded target rejected");
  for (const auto delta : {std::int64_t{minimum} - 1, std::int64_t{maximum} + 1}) {
    record.payload = OriginalInputMotion{delta, 0, 32};
    check(!createOriginalInputEvent(record, targetPoint, fixture.source.get()), "oversized motion cannot narrow");
    OriginalInputScroll scroll;
    scroll.fixed16_16[0] = delta;
    record.payload = scroll;
    check(
        !createOriginalInputEvent(record, targetPoint, fixture.source.get()),
        "oversized fixed-point value cannot narrow"
    );
    scroll.fixed16_16[0] = 0;
    scroll.points[1] = delta;
    record.payload = scroll;
    check(!createOriginalInputEvent(record, targetPoint, fixture.source.get()), "oversized point delta cannot narrow");
    scroll.points[1] = 0;
    scroll.lines[1] = delta;
    record.payload = scroll;
    check(!createOriginalInputEvent(record, targetPoint, fixture.source.get()), "oversized line delta cannot narrow");
  }
  for (const auto delta : {std::int64_t{minimum}, std::int64_t{-32769}, std::int64_t{32768}, std::int64_t{maximum}}) {
    record.payload = OriginalInputMotion{delta, 0, 32};
    check(validOriginalInputRecord(record), "signed32 motion belongs to protocol domain");
    check(
        !createOriginalInputEvent(record, targetPoint, fixture.source.get()), "Quartz narrowing of motion is rejected"
    );
    OriginalInputScroll scroll;
    scroll.lines[0] = delta;
    record.payload = scroll;
    check(validOriginalInputRecord(record), "signed32 line scroll belongs to protocol domain");
    check(
        !createOriginalInputEvent(record, targetPoint, fixture.source.get()),
        "Quartz narrowing of line scroll is rejected"
    );
  }
  record.payload = OriginalInputButton{32, true, 1};
  check(
      !createOriginalInputEvent(record, targetPoint, fixture.source.get()), "button outside documented domain rejected"
  );
  record.payload = OriginalInputButton{0, true, 0};
  check(!createOriginalInputEvent(record, targetPoint, fixture.source.get()), "zero click count rejected");
  record.payload = OriginalInputButton{0, true, std::uint32_t(maximum) + 1};
  check(!createOriginalInputEvent(record, targetPoint, fixture.source.get()), "oversized click count cannot narrow");
  OriginalInputScroll unsupportedScroll;
  unsupportedScroll.continuous = true;
  unsupportedScroll.phase = static_cast<OriginalScrollPhase>(16);
  record.payload = unsupportedScroll;
  check(
      !createOriginalInputEvent(record, targetPoint, fixture.source.get()),
      "unsupported record phase rejected before Quartz masks it"
  );
  unsupportedScroll.phase = OriginalScrollPhase::None;
  unsupportedScroll.momentum = static_cast<OriginalMomentumPhase>(4);
  record.payload = unsupportedScroll;
  check(
      !createOriginalInputEvent(record, targetPoint, fixture.source.get()),
      "unsupported record momentum rejected before Quartz masks it"
  );
  for (const auto boundary :
       {OriginalInputBoundary::Checkpoint, OriginalInputBoundary::Discontinuity, OriginalInputBoundary::Emergency}) {
    record.payload = boundary;
    check(!createOriginalInputEvent(record, targetPoint, fixture.source.get()), "boundary never constructs OS input");
  }
}

void testOwnerReleaseIntents()
{
  Fixture fixture;
  OriginalInputStream stream;
  OriginalInputPressLedger ledger(stream.epoch());
  std::uint64_t seq = 0;
  auto deliver = [&](CGEventRef event) {
    auto record = captured(event);
    record.epoch = stream.epoch().id();
    record.sequence = ++seq;
    check(stream.push(record), "constructed event enters mixed stream");
    const auto input = stream.pop();
    check(input.has_value(), "mixed stream delivers constructed event");
    const auto result = ledger.observe(*input, 53);
    check(result.delivery.has_value() && result.releases.empty() && !result.conflict, "press acquires fixture owner");
  };
  constexpr auto shift = originalKey::shiftFlag | originalKey::leftShiftFlag;
  Owned<CGEventRef> modifier{fixture.key(originalKey::leftShift, kCGEventFlagsChanged, shift)};
  Owned<CGEventRef> key{fixture.key(0, kCGEventKeyDown, shift)};
  Owned<CGEventRef> button{fixture.pointer(kCGEventOtherMouseDown, 31)};
  CGEventSetFlags(button.get(), shift);
  deliver(modifier.get());
  deliver(key.get());
  deliver(button.get());
  const auto releases = ledger.revokeAll();
  check(releases.size() == 3 && ledger.deliveredCount() == 0, "revoke yields all delivered key/button owners");
  for (const auto &release : releases) {
    Owned<CGEventRef> output{createOriginalInputRelease(release, timestamp + 1, targetPoint, fixture.source.get())};
    check(output.get(), "mixed ledger release constructs retained private event");
    check(
        CGEventGetTimestamp(output.get()) == timestamp + 1 && CGEventGetFlags(output.get()) == release.flags,
        "release uses owner flags and release time"
    );
    check(
        CGEventGetIntegerValueField(output.get(), kCGEventSourceUserData) == syntheticEventTag,
        "forced release is loop tagged"
    );
    check(
        capture(CGEventGetType(output.get()), output.get()).status == OriginalInputCapture::Status::Ignored,
        "forced release never recaptures"
    );
    if (const auto *press = std::get_if<OriginalInputButton>(&release.press.payload)) {
      check(CGEventGetType(output.get()) == kCGEventOtherMouseUp, "extra button forced release has matching native up");
      check(
          CGEventGetIntegerValueField(output.get(), kCGMouseEventButtonNumber) == press->button,
          "release retains original button"
      );
      check(
          CGEventGetIntegerValueField(output.get(), kCGMouseEventClickState) == press->clickCount,
          "release retains original click count"
      );
      const auto point = CGEventGetLocation(output.get());
      check(point.x == targetPoint.x && point.y == targetPoint.y, "release target is caller endpoint");
    } else {
      const auto keyPress = std::get<OriginalInputKey>(release.press.payload);
      check(
          CGEventGetType(output.get()) ==
              (keyPress.code == originalKey::leftShift ? kCGEventFlagsChanged : kCGEventKeyUp),
          "key release kind preserved"
      );
      check(
          CGEventGetIntegerValueField(output.get(), kCGKeyboardEventKeyboardType) == keyPress.keyboardType,
          "release keeps captured keyboard type"
      );
      check(CGEventGetIntegerValueField(output.get(), kCGKeyboardEventAutorepeat) == 0, "release never repeats");
    }
    check(
        !createOriginalInputRelease(release, timestamp - 1, targetPoint, fixture.source.get()),
        "release before press rejected"
    );
    auto invalid = release;
    invalid.owner = 0;
    check(
        !createOriginalInputRelease(invalid, timestamp, targetPoint, fixture.source.get()), "unowned release rejected"
    );
    invalid = release;
    invalid.flags = originalKey::capsPhysicalFlags;
    check(
        !createOriginalInputRelease(invalid, timestamp, targetPoint, fixture.source.get()),
        "physical Caps state never synthesized in forced release"
    );
  }
  auto invalid = releases.front();
  invalid.press.payload = OriginalInputMotion{1, 2, 32};
  check(
      !createOriginalInputRelease(invalid, timestamp, targetPoint, fixture.source.get()),
      "motion has no forced press release"
  );
  invalid.press.payload = OriginalInputButton{2, false, 1};
  check(
      !createOriginalInputRelease(invalid, timestamp, targetPoint, fixture.source.get()),
      "up cannot generate another release"
  );
  invalid.press.payload = OriginalInputKey{42, 0, OriginalKeyKind::Down, OriginalKeyTransition::Repeat, true};
  check(
      !createOriginalInputRelease(invalid, timestamp, targetPoint, fixture.source.get()),
      "repeat cannot manufacture a down owner"
  );
}

void testMixedQueueCopies()
{
  Fixture fixture;
  OriginalInputStream stream;
  Owned<CGEventRef> motion{fixture.pointer(kCGEventMouseMoved)};
  Owned<CGEventRef> key{fixture.key(0, kCGEventKeyDown, modifiers)};
  Owned<CGEventRef> scroll{fixture.scroll()};
  Owned<CGEventRef> button{fixture.pointer(kCGEventRightMouseDown, 1)};
  const CGEventRef events[]{motion.get(), key.get(), scroll.get(), button.get()};
  for (std::size_t index = 0; index < std::size(events); ++index) {
    const auto result = capture(CGEventGetType(events[index]), events[index], stream.epoch().id(), index + 1);
    check(result.record.has_value() && stream.push(*result.record), "mixed native capture enters C++ queue");
    // Mutating the source objects cannot change already-copied native records.
    CGEventSetType(events[index], kCGEventNull);
  }
  for (std::size_t index = 0; index < std::size(events); ++index) {
    const auto delivery = stream.pop();
    check(
        delivery.has_value() && delivery->live() && delivery->record.sequence == index + 1,
        "mixed queued order retained"
    );
    const auto &payload = delivery->record.payload;
    switch (index) {
    case 0: {
      const auto *value = std::get_if<OriginalInputMotion>(&payload);
      check(
          value && value->dx == -19 && value->dy == 37 && value->dragButton == 32,
          "motion survives ObjC++ to C++ queue copy"
      );
      break;
    }
    case 1: {
      const auto *value = std::get_if<OriginalInputKey>(&payload);
      check(
          value && value->code == 0 && value->transition == OriginalKeyTransition::Down, "key survives mixed queue copy"
      );
      break;
    }
    case 2: {
      const auto *value = std::get_if<OriginalInputScroll>(&payload);
      check(value && value->fixed16_16[1] == -0x28001, "scroll fractional bits survive mixed queue copy");
      break;
    }
    case 3: {
      const auto *value = std::get_if<OriginalInputButton>(&payload);
      check(value && value->button == 1 && value->down && value->clickCount == 2, "button survives mixed queue copy");
      break;
    }
    }
  }
  check(!stream.pop(), "mixed queue drained exactly once");
}

void testEventTimePointerObservation()
{
  Fixture fixture;
  Owned<CGEventRef> event{fixture.pointer(kCGEventLeftMouseDown, 0)};
  CGEventSetLocation(event.get(), {-140.25, 800.5});
  CGEventSetIntegerValueField(event.get(), kCGMouseEventWindowUnderMousePointer, 71);
  CGEventSetIntegerValueField(event.get(), kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, 81);
  CGEventSetIntegerValueField(event.get(), kCGMouseEventNumber, 93);
  const auto observed = capture(kCGEventLeftMouseDown, event.get());
  check(
      observed.record && observed.pointer && observed.pointer->matches(*observed.record),
      "pointer metadata retains exact capture stamp"
  );
  check(
      observed.pointer->x == -140.25 && observed.pointer->y == 800.5 && observed.pointer->windowUnderPointer == 71 &&
          observed.pointer->windowHandlingPointer == 81 && observed.pointer->eventNumber == 93,
      "distinct native window fields and fractional negative source coordinates survive capture"
  );
  check(
      observed.pointer->sourceProcess == CGEventGetIntegerValueField(event.get(), kCGEventSourceUnixProcessID) &&
          observed.pointer->sourceState == CGEventGetIntegerValueField(event.get(), kCGEventSourceStateID),
      "reported source fields are preserved without treating them as device authentication"
  );

  auto changed = *observed.record;
  ++changed.epoch;
  check(!observed.pointer->matches(changed), "pointer metadata cannot cross epochs");
  changed = *observed.record;
  ++changed.sequence;
  check(!observed.pointer->matches(changed), "pointer metadata cannot move to a later press");
  changed = *observed.record;
  ++changed.sourceTimestampNs;
  check(!observed.pointer->matches(changed), "pointer metadata cannot replace source time");
  changed = *observed.record;
  changed.payload = OriginalInputBoundary::Checkpoint;
  check(!observed.pointer->matches(changed), "pointer metadata cannot be attached to a checkpoint");

  CGEventSetIntegerValueField(event.get(), kCGMouseEventWindowUnderMousePointer, 0);
  CGEventSetIntegerValueField(event.get(), kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, 0);
  const auto unavailable = capture(kCGEventLeftMouseDown, event.get());
  check(
      unavailable.record && unavailable.pointer && !unavailable.pointer->windowUnderPointer &&
          !unavailable.pointer->windowHandlingPointer,
      "unknown event-time windows remain zero without a foreground or geometry fallback"
  );
  for (const auto invalid : {-1LL, 0x100000000LL}) {
    CGEventSetIntegerValueField(event.get(), kCGMouseEventWindowUnderMousePointer, invalid);
    // Quartz stores this field as a native window ID and may normalize the
    // setter's wider signed value. Compare against the actual event getter,
    // not an impossible representation invented by the fixture.
    const auto raw = CGEventGetIntegerValueField(event.get(), kCGMouseEventWindowUnderMousePointer);
    const auto normalized = capture(kCGEventLeftMouseDown, event.get());
    const bool representable = raw >= 0 && raw <= std::numeric_limits<std::uint32_t>::max();
    check(
        normalized.record &&
            (representable ? normalized.pointer && normalized.pointer->windowUnderPointer == raw : !normalized.pointer),
        "capture preserves representable Quartz window IDs and omits invalid native metadata"
    );
  }
  CGEventSetIntegerValueField(event.get(), kCGMouseEventWindowUnderMousePointer, 71);
  CGEventSetLocation(event.get(), {std::numeric_limits<double>::quiet_NaN(), 0});
  const auto invalidPoint = capture(kCGEventLeftMouseDown, event.get());
  check(invalidPoint.record && !invalidPoint.pointer, "nonfinite source point cannot become pointer evidence");
  Owned<CGEventRef> key{fixture.key(0, kCGEventKeyDown, modifiers)};
  check(
      !capture(kCGEventKeyDown, key.get()).pointer, "keys have no pointer metadata even if Quartz has a cursor position"
  );
  // Use a separately marked private source, as the live host does. Rewriting
  // source userData on a previously materialized event is not this contract.
  Owned<CGEventSourceRef> loopSource{CGEventSourceCreate(kCGEventSourceStatePrivate)};
  CGEventSourceSetUserData(loopSource.get(), syntheticEventTag);
  Owned<CGEventRef> loopEvent{
      CGEventCreateMouseEvent(loopSource.get(), kCGEventLeftMouseDown, sourcePoint, kCGMouseButtonLeft)
  };
  CGEventSetTimestamp(loopEvent.get(), timestamp);
  CGEventSetFlags(loopEvent.get(), 0);
  check(
      CGEventGetIntegerValueField(loopEvent.get(), kCGEventSourceUserData) == syntheticEventTag,
      "valid fixture retains its loop marker"
  );
  const auto synthetic = capture(kCGEventLeftMouseDown, loopEvent.get());
  check(
      synthetic.status == OriginalInputCapture::Status::Ignored && !synthetic.pointer,
      "marked synthetic events create no native pointer observation"
  );
}
} // namespace

int main()
{
  try {
    testKeysAndRawModifierTransitions();
    testMotionAndEveryButton();
    testScrollRepresentationsAndPhases();
    testMalformedInputAndDiscontinuities();
    testConstructionLimitsAndSources();
    testOwnerReleaseIntents();
    testMixedQueueCopies();
    testEventTimePointerObservation();
    std::cout << "OriginalInput Mac private-event tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << "OriginalInput Mac test failure: " << error.what() << '\n';
    return 1;
  }
}
