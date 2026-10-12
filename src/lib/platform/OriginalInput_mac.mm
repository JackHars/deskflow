/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "OriginalInput_mac.h"
#include "OriginalKeyboard_mac.h"
#include "windowshare/PlatformMarkers.h"

#include <cmath>
#include <limits>
#include <type_traits>

namespace deskflow::windowshare {
namespace {
static_assert(static_cast<unsigned>(OriginalScrollPhase::Began) == kCGScrollPhaseBegan);
static_assert(static_cast<unsigned>(OriginalScrollPhase::Changed) == kCGScrollPhaseChanged);
static_assert(static_cast<unsigned>(OriginalScrollPhase::Ended) == kCGScrollPhaseEnded);
static_assert(static_cast<unsigned>(OriginalScrollPhase::Cancelled) == kCGScrollPhaseCancelled);
static_assert(static_cast<unsigned>(OriginalScrollPhase::MayBegin) == kCGScrollPhaseMayBegin);
static_assert(static_cast<unsigned>(OriginalMomentumPhase::None) == kCGMomentumScrollPhaseNone);
static_assert(static_cast<unsigned>(OriginalMomentumPhase::Began) == kCGMomentumScrollPhaseBegin);
static_assert(static_cast<unsigned>(OriginalMomentumPhase::Continued) == kCGMomentumScrollPhaseContinue);
static_assert(static_cast<unsigned>(OriginalMomentumPhase::Ended) == kCGMomentumScrollPhaseEnd);
static_assert(originalInputButtonCount == 32); // CGEventCreateMouseEvent's documented button domain.
static_assert(std::is_nothrow_copy_assignable_v<OriginalInputRecord>);

// Record axes are horizontal/vertical; Quartz axes 1/2 are vertical/horizontal.
constexpr CGEventField lineFields[]{kCGScrollWheelEventDeltaAxis2, kCGScrollWheelEventDeltaAxis1};
constexpr CGEventField pointFields[]{kCGScrollWheelEventPointDeltaAxis2, kCGScrollWheelEventPointDeltaAxis1};
constexpr CGEventField fixedFields[]{kCGScrollWheelEventFixedPtDeltaAxis2, kCGScrollWheelEventFixedPtDeltaAxis1};

bool supported(CGEventType type) noexcept
{
  switch (type) {
  case kCGEventKeyDown:
  case kCGEventKeyUp:
  case kCGEventFlagsChanged:
  case kCGEventMouseMoved:
  case kCGEventLeftMouseDragged:
  case kCGEventRightMouseDragged:
  case kCGEventOtherMouseDragged:
  case kCGEventLeftMouseDown:
  case kCGEventLeftMouseUp:
  case kCGEventRightMouseDown:
  case kCGEventRightMouseUp:
  case kCGEventOtherMouseDown:
  case kCGEventOtherMouseUp:
  case kCGEventScrollWheel:
    return true;
  default:
    return false;
  }
}

bool privateSource(CGEventSourceRef source) noexcept
{
  if (!source)
    return false;
  // Private sources return their allocated table ID, not the -1 creation
  // sentinel. Invalid sources report CombinedSessionState per the public SDK.
  const auto state = CGEventSourceGetSourceStateID(source);
  return state != kCGEventSourceStateCombinedSessionState && state != kCGEventSourceStateHIDSystemState;
}

bool targetValid(CGPoint point) noexcept
{
  constexpr auto low = std::numeric_limits<std::int32_t>::min(), high = std::numeric_limits<std::int32_t>::max();
  return std::isfinite(point.x) && std::isfinite(point.y) && point.x >= low && point.x <= high && point.y >= low &&
         point.y <= high;
}

bool buttonMatches(CGEventType type, std::int64_t button) noexcept
{
  switch (type) {
  case kCGEventLeftMouseDown:
  case kCGEventLeftMouseUp:
  case kCGEventLeftMouseDragged:
    return button == 0;
  case kCGEventRightMouseDown:
  case kCGEventRightMouseUp:
  case kCGEventRightMouseDragged:
    return button == 1;
  case kCGEventOtherMouseDown:
  case kCGEventOtherMouseUp:
  case kCGEventOtherMouseDragged:
    return button >= 2 && button < static_cast<std::int64_t>(originalInputButtonCount);
  default:
    return false;
  }
}

OriginalKeyTransition transition(const OriginalKeySample &key) noexcept
{
  if (key.kind == OriginalKeyKind::Down)
    return key.repeat ? OriginalKeyTransition::Repeat : OriginalKeyTransition::Down;
  if (key.kind == OriginalKeyKind::Up)
    return OriginalKeyTransition::Up;
  if (key.keyCode == originalKey::capsLock)
    return OriginalKeyTransition::Toggle;
  constexpr auto aggregates =
      originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag | originalKey::commandFlag;
  const auto side = originalKeyModifierFlags(key.keyCode) & ~aggregates;
  return key.flags & side ? OriginalKeyTransition::Down : OriginalKeyTransition::Up;
}

bool readPayload(CGEventType type, CGEventRef event, OriginalInputRecord &record) noexcept
{
  switch (type) {
  case kCGEventKeyDown:
  case kCGEventKeyUp:
  case kCGEventFlagsChanged: {
    const auto captured = captureOriginalKeyboardEvent(event, record.sequence);
    if (captured.status != OriginalKeyCapture::Status::Captured || !captured.sample)
      return false;
    const auto &sample = *captured.sample;
    const OriginalKeyEvent key{sample, transition(sample)};
    if (!validOriginalKeyEvent(key))
      return false;
    record.payload = OriginalInputKey{sample.keyboardType, sample.keyCode, sample.kind, key.transition, sample.repeat};
    return true;
  }
  case kCGEventScrollWheel: {
    const auto continuous = CGEventGetIntegerValueField(event, kCGScrollWheelEventIsContinuous);
    const auto phase = CGEventGetIntegerValueField(event, kCGScrollWheelEventScrollPhase);
    const auto momentum = CGEventGetIntegerValueField(event, kCGScrollWheelEventMomentumPhase);
    if ((continuous != 0 && continuous != 1) || phase < 0 || phase > 255 || momentum < 0 || momentum > 255 ||
        CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis3) != 0 ||
        CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis3) != 0 ||
        CGEventGetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis3) != 0)
      return false;
    OriginalInputScroll scroll;
    scroll.continuous = continuous != 0;
    scroll.phase = static_cast<OriginalScrollPhase>(phase);
    scroll.momentum = static_cast<OriginalMomentumPhase>(momentum);
    for (std::size_t axis = 0; axis < 2; ++axis) {
      scroll.lines[axis] = CGEventGetIntegerValueField(event, lineFields[axis]);
      scroll.points[axis] = CGEventGetIntegerValueField(event, pointFields[axis]);
      // CGEventTypes.h defines signed 16.16 fields. Only the double accessor
      // scales them; the integer accessor preserves the exact signed bits.
      scroll.fixed16_16[axis] = CGEventGetIntegerValueField(event, fixedFields[axis]);
    }
    record.payload = scroll;
    return true;
  }
  default:
    break;
  }
  // Tablet subtypes carry data this protocol cannot represent. Never silently
  // turn them into a mouse record with the tablet information discarded.
  if (CGEventGetIntegerValueField(event, kCGMouseEventSubtype) != kCGEventMouseSubtypeDefault)
    return false;
  const bool motion = type == kCGEventMouseMoved || type == kCGEventLeftMouseDragged ||
                      type == kCGEventRightMouseDragged || type == kCGEventOtherMouseDragged;
  const auto button = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
  if (type != kCGEventMouseMoved && !buttonMatches(type, button))
    return false;
  if (motion) {
    record.payload = OriginalInputMotion{
        CGEventGetIntegerValueField(event, kCGMouseEventDeltaX),
        CGEventGetIntegerValueField(event, kCGMouseEventDeltaY),
        type == kCGEventMouseMoved ? std::uint8_t(originalInputButtonCount) : static_cast<std::uint8_t>(button)
    };
    return true;
  }
  const auto clicks = CGEventGetIntegerValueField(event, kCGMouseEventClickState);
  if (clicks <= 0 || clicks > std::numeric_limits<std::int32_t>::max())
    return false;
  const bool down = type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown || type == kCGEventOtherMouseDown;
  record.payload = OriginalInputButton{static_cast<std::uint8_t>(button), down, static_cast<std::uint32_t>(clicks)};
  return true;
}

CGEventType motionType(std::uint8_t button) noexcept
{
  if (button == originalInputButtonCount)
    return kCGEventMouseMoved;
  if (button == 0)
    return kCGEventLeftMouseDragged;
  return button == 1 ? kCGEventRightMouseDragged : kCGEventOtherMouseDragged;
}

CGEventType buttonType(const OriginalInputButton &button) noexcept
{
  if (button.button == 0)
    return button.down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
  if (button.button == 1)
    return button.down ? kCGEventRightMouseDown : kCGEventRightMouseUp;
  return button.down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
}

// Verify the modeled fields without clearing the loop marker on the event.
bool samePayload(const OriginalInputRecord &a, const OriginalInputRecord &b) noexcept
{
  if (const auto *motion = std::get_if<OriginalInputMotion>(&a.payload)) {
    const auto *other = std::get_if<OriginalInputMotion>(&b.payload);
    return other && motion->dx == other->dx && motion->dy == other->dy && motion->dragButton == other->dragButton;
  }
  if (const auto *button = std::get_if<OriginalInputButton>(&a.payload)) {
    const auto *other = std::get_if<OriginalInputButton>(&b.payload);
    return other && button->button == other->button && button->down == other->down &&
           button->clickCount == other->clickCount;
  }
  if (const auto *scroll = std::get_if<OriginalInputScroll>(&a.payload)) {
    const auto *other = std::get_if<OriginalInputScroll>(&b.payload);
    return other && scroll->lines == other->lines && scroll->points == other->points &&
           scroll->fixed16_16 == other->fixed16_16 && scroll->continuous == other->continuous &&
           scroll->phase == other->phase && scroll->momentum == other->momentum;
  }
  return false;
}

bool commonFields(CGEventRef event, CGEventSourceRef source, const OriginalInputRecord &record) noexcept
{
  return CGEventGetFlags(event) == record.flags && CGEventGetTimestamp(event) == record.sourceTimestampNs &&
         CGEventGetIntegerValueField(event, kCGEventSourceUserData) == syntheticEventTag &&
         CGEventGetIntegerValueField(event, kCGEventSourceStateID) == CGEventSourceGetSourceStateID(source);
}
} // namespace

OriginalInputCapture captureOriginalInputEvent(
    CGEventType callbackType, CGEventRef event, std::uint64_t epoch, std::uint64_t sequence
) noexcept
{
  if (callbackType == kCGEventTapDisabledByTimeout || callbackType == kCGEventTapDisabledByUserInput)
    return {OriginalInputCapture::Status::Discontinuity, std::nullopt};
  if ((event && isWindowshareSyntheticEvent(CGEventGetIntegerValueField(event, kCGEventSourceUserData))) ||
      !supported(callbackType))
    return {};
  OriginalInputCapture result{OriginalInputCapture::Status::Malformed, std::nullopt};
  if (!event || CGEventGetType(event) != callbackType)
    return result;
  OriginalInputRecord record;
  record.epoch = epoch;
  record.sequence = sequence;
  record.sourceTimestampNs = CGEventGetTimestamp(event);
  record.flags = CGEventGetFlags(event);
  if (!readPayload(callbackType, event, record) || !validOriginalInputRecord(record))
    return result;
  return {OriginalInputCapture::Status::Captured, record};
}

CGEventRef
createOriginalInputEvent(const OriginalInputRecord &record, CGPoint targetPoint, CGEventSourceRef source) noexcept
{
  if (!validOriginalInputRecord(record) || !privateSource(source))
    return nullptr;
  if (const auto key = originalInputKeyEvent(record)) {
    auto event = createOriginalKeyboardEvent(*key, source);
    if (event && !commonFields(event, source, record)) {
      CFRelease(event);
      return nullptr;
    }
    return event;
  }
  if (!targetValid(targetPoint))
    return nullptr;
  CGEventRef event = nullptr;
  CGEventType type;
  if (const auto *motion = std::get_if<OriginalInputMotion>(&record.payload)) {
    type = motionType(motion->dragButton);
    const auto button = motion->dragButton == originalInputButtonCount ? 0 : motion->dragButton;
    event = CGEventCreateMouseEvent(source, type, targetPoint, static_cast<CGMouseButton>(button));
    if (event) {
      CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, motion->dx);
      CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, motion->dy);
      CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, button);
    }
  } else if (const auto *button = std::get_if<OriginalInputButton>(&record.payload)) {
    type = buttonType(*button);
    event = CGEventCreateMouseEvent(source, type, targetPoint, static_cast<CGMouseButton>(button->button));
    if (event) {
      CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, button->button);
      CGEventSetIntegerValueField(event, kCGMouseEventClickState, button->clickCount);
    }
  } else if (const auto *scroll = std::get_if<OriginalInputScroll>(&record.payload)) {
    type = kCGEventScrollWheel;
    event = CGEventCreateScrollWheelEvent2(
        source, scroll->continuous ? kCGScrollEventUnitPixel : kCGScrollEventUnitLine, 2, 0, 0, 0
    );
    if (event) {
      for (std::size_t axis = 0; axis < 2; ++axis) {
        CGEventSetIntegerValueField(event, lineFields[axis], scroll->lines[axis]);
        CGEventSetIntegerValueField(event, pointFields[axis], scroll->points[axis]);
        CGEventSetIntegerValueField(event, fixedFields[axis], scroll->fixed16_16[axis]);
      }
      CGEventSetIntegerValueField(event, kCGScrollWheelEventDeltaAxis3, 0);
      CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis3, 0);
      CGEventSetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis3, 0);
      CGEventSetIntegerValueField(event, kCGScrollWheelEventIsContinuous, scroll->continuous ? 1 : 0);
      CGEventSetIntegerValueField(event, kCGScrollWheelEventScrollPhase, static_cast<unsigned>(scroll->phase));
      CGEventSetIntegerValueField(event, kCGScrollWheelEventMomentumPhase, static_cast<unsigned>(scroll->momentum));
    }
  } else {
    return nullptr;
  }
  if (!event)
    return nullptr;
  CGEventSetFlags(event, static_cast<CGEventFlags>(record.flags));
  CGEventSetTimestamp(event, record.sourceTimestampNs);
  CGEventSetLocation(event, targetPoint);
  CGEventSetIntegerValueField(event, kCGEventSourceUserData, syntheticEventTag);
  OriginalInputRecord observed = record;
  const auto point = CGEventGetLocation(event);
  if (CGEventGetType(event) != type || !commonFields(event, source, record) || point.x != targetPoint.x ||
      point.y != targetPoint.y || !readPayload(type, event, observed) || !samePayload(record, observed)) {
    CFRelease(event);
    return nullptr;
  }
  return event;
}

CGEventRef createOriginalInputRelease(
    const OriginalInputRelease &release, std::uint64_t timestampNs, CGPoint targetPoint, CGEventSourceRef source
) noexcept
{
  if (!release.owner || !validOriginalInputRecord(release.press) || timestampNs < release.press.sourceTimestampNs ||
      (release.flags & originalKey::capsPhysicalFlags) || !privateSource(source))
    return nullptr;
  if (const auto key = originalInputKeyEvent(release.press)) {
    auto event = createOriginalKeyboardRelease({release.owner, key->sample, release.flags}, timestampNs, source);
    if (event && CGEventGetIntegerValueField(event, kCGEventSourceStateID) != CGEventSourceGetSourceStateID(source)) {
      CFRelease(event);
      return nullptr;
    }
    return event;
  }
  const auto *press = std::get_if<OriginalInputButton>(&release.press.payload);
  if (!press || !press->down)
    return nullptr;
  auto up = release.press;
  up.sourceTimestampNs = timestampNs;
  up.flags = release.flags;
  up.payload = OriginalInputButton{press->button, false, press->clickCount};
  return createOriginalInputEvent(up, targetPoint, source);
}

} // namespace deskflow::windowshare
