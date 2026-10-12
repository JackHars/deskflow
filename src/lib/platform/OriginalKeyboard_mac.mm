/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "OriginalKeyboard_mac.h"
#include "windowshare/PlatformMarkers.h"

#include <Carbon/Carbon.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <limits>

namespace deskflow::windowshare {
namespace {
static_assert(originalKey::escape == kVK_Escape);
static_assert(originalKey::leftCommand == kVK_Command && originalKey::rightCommand == kVK_RightCommand);
static_assert(originalKey::leftShift == kVK_Shift && originalKey::rightShift == kVK_RightShift);
static_assert(originalKey::leftOption == kVK_Option && originalKey::rightOption == kVK_RightOption);
static_assert(originalKey::leftControl == kVK_Control && originalKey::rightControl == kVK_RightControl);
static_assert(originalKey::function == kVK_Function && originalKey::capsLock == kVK_CapsLock);
static_assert(originalKey::capsLockFlag == kCGEventFlagMaskAlphaShift);
static_assert(originalKey::shiftFlag == kCGEventFlagMaskShift && originalKey::controlFlag == kCGEventFlagMaskControl);
static_assert(
    originalKey::optionFlag == kCGEventFlagMaskAlternate && originalKey::commandFlag == kCGEventFlagMaskCommand
);
static_assert(originalKey::functionFlag == kCGEventFlagMaskSecondaryFn);
static_assert(originalKey::leftShiftFlag == NX_DEVICELSHIFTKEYMASK);
static_assert(originalKey::rightShiftFlag == NX_DEVICERSHIFTKEYMASK);
static_assert(originalKey::leftControlFlag == NX_DEVICELCTLKEYMASK);
static_assert(originalKey::rightControlFlag == NX_DEVICERCTLKEYMASK);
static_assert(originalKey::leftOptionFlag == NX_DEVICELALTKEYMASK);
static_assert(originalKey::rightOptionFlag == NX_DEVICERALTKEYMASK);
static_assert(originalKey::leftCommandFlag == NX_DEVICELCMDKEYMASK);
static_assert(originalKey::rightCommandFlag == NX_DEVICERCMDKEYMASK);
static_assert(originalKey::capsPhysicalFlags == (NX_ALPHASHIFT_STATELESS_MASK | NX_DEVICE_ALPHASHIFT_STATELESS_MASK));

CGEventRef makeEvent(
    const OriginalKeySample &sample, OriginalKeyKind kind, bool down, std::uint64_t flags, bool repeat,
    std::uint64_t timestamp, CGEventSourceRef source
) noexcept
{
  if (!source)
    return nullptr;
  // Private sources report their allocated state-table ID, not the -1 sentinel
  // passed to CGEventSourceCreate. Public combined/HID tables must be rejected.
  const auto state = CGEventSourceGetSourceStateID(source);
  if (state == kCGEventSourceStateCombinedSessionState || state == kCGEventSourceStateHIDSystemState)
    return nullptr;
  CGEventSourceSetKeyboardType(source, sample.keyboardType);
  CGEventRef event = CGEventCreateKeyboardEvent(source, sample.keyCode, down);
  if (!event)
    return nullptr;
  const CGEventType type = kind == OriginalKeyKind::FlagsChanged ? kCGEventFlagsChanged
                           : kind == OriginalKeyKind::Down       ? kCGEventKeyDown
                                                                 : kCGEventKeyUp;
  CGEventSetType(event, type);
  CGEventSetIntegerValueField(event, kCGKeyboardEventKeycode, sample.keyCode);
  CGEventSetIntegerValueField(event, kCGKeyboardEventKeyboardType, sample.keyboardType);
  CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, repeat ? 1 : 0);
  CGEventSetFlags(event, static_cast<CGEventFlags>(flags));
  CGEventSetTimestamp(event, timestamp);
  CGEventSetIntegerValueField(event, kCGEventSourceUserData, syntheticEventTag);
  if (CGEventGetType(event) != type || CGEventGetFlags(event) != flags || CGEventGetTimestamp(event) != timestamp ||
      CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode) != sample.keyCode ||
      CGEventGetIntegerValueField(event, kCGKeyboardEventKeyboardType) != sample.keyboardType ||
      CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) != (repeat ? 1 : 0) ||
      CGEventGetIntegerValueField(event, kCGEventSourceUserData) != syntheticEventTag) {
    CFRelease(event);
    return nullptr;
  }
  return event;
}
} // namespace

OriginalKeyCapture captureOriginalKeyboardEvent(CGEventRef event, std::uint64_t sequence) noexcept
{
  OriginalKeyCapture result;
  if (!event || isWindowshareSyntheticEvent(CGEventGetIntegerValueField(event, kCGEventSourceUserData)))
    return result;
  OriginalKeySample sample;
  switch (CGEventGetType(event)) {
  case kCGEventKeyDown:
    sample.kind = OriginalKeyKind::Down;
    break;
  case kCGEventKeyUp:
    sample.kind = OriginalKeyKind::Up;
    break;
  case kCGEventFlagsChanged:
    sample.kind = OriginalKeyKind::FlagsChanged;
    break;
  default:
    return result;
  }
  result.status = OriginalKeyCapture::Status::Malformed;
  const auto code = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
  const auto keyboardType = CGEventGetIntegerValueField(event, kCGKeyboardEventKeyboardType);
  const auto repeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat);
  if (code < 0 || code >= static_cast<std::int64_t>(originalKey::keyCount) || keyboardType < 0 ||
      keyboardType > std::numeric_limits<std::uint32_t>::max() || (repeat != 0 && repeat != 1))
    return result;
  sample.sequence = sequence;
  sample.timestampNs = CGEventGetTimestamp(event);
  sample.flags = CGEventGetFlags(event);
  sample.keyboardType = static_cast<std::uint32_t>(keyboardType);
  sample.keyCode = static_cast<std::uint16_t>(code);
  sample.repeat = repeat != 0;
  if (!validOriginalKeySample(sample))
    return result;
  result.status = OriginalKeyCapture::Status::Captured;
  result.sample = sample;
  return result;
}

CGEventRef createOriginalKeyboardEvent(const OriginalKeyEvent &event, CGEventSourceRef privateSource) noexcept
{
  if (!validOriginalKeyEvent(event))
    return nullptr;
  const auto &sample = event.sample;
  const bool down = event.transition == OriginalKeyTransition::Down ||
                    event.transition == OriginalKeyTransition::Repeat ||
                    (event.transition == OriginalKeyTransition::Toggle && (sample.flags & originalKey::capsLockFlag));
  return makeEvent(sample, sample.kind, down, sample.flags, sample.repeat, sample.timestampNs, privateSource);
}

CGEventRef createOriginalKeyboardRelease(
    const OriginalKeyRelease &release, std::uint64_t timestampNs, CGEventSourceRef privateSource
) noexcept
{
  const auto &press = release.press;
  if (!release.owner || !validOriginalKeyEvent({press, OriginalKeyTransition::Down}) ||
      timestampNs < press.timestampNs || (release.flags & originalKey::capsPhysicalFlags))
    return nullptr;
  constexpr auto aggregates =
      originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag | originalKey::commandFlag;
  const auto side = originalKeyModifierFlags(press.keyCode) & ~aggregates;
  if (release.flags & side)
    return nullptr;
  const auto kind = originalKeyIsModifier(press.keyCode) ? OriginalKeyKind::FlagsChanged : OriginalKeyKind::Up;
  auto up = press;
  up.kind = kind;
  up.flags = release.flags;
  up.timestampNs = timestampNs;
  if (!validOriginalKeyEvent({up, OriginalKeyTransition::Up}))
    return nullptr;
  return makeEvent(press, kind, false, release.flags, false, timestampNs, privateSource);
}

} // namespace deskflow::windowshare
