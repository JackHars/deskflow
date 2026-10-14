/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "CaptureRate.h"
#include "NativeCaptureTiming.h"
#include "NativeHost.h"
#include "NativeKeyboardAdmission.h"
#include "NativeSourceTransaction.h"
#include "NativeTitlebarObservation.h"
#include "NativeWindowIdentity_mac.h"
#include "platform/OriginalInput_mac.h"
#include <CoreGraphics/CoreGraphics.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <array>
#include <cmath>
#include <dlfcn.h>
#include <limits>
#include <string>
#include <unistd.h>

// Internal filesystem primitive; exercising it never enumerates native windows.
namespace deskflow::windowshare::native {
bool persistJournal(const QString &path, const QJsonArray &records, QString *diagnostic);
bool targetedPointerAvailable();
CGEventRef targetedPointerEvent(
    CGEventSourceRef source, CGWindowID window, Rect frame, Point point, CGEventType type, std::uint32_t button,
    CGEventFlags modifiers, std::int64_t eventNumber, int clicks
);
CGEventRef targetedOriginalInputEvent(
    CGEventRef original, CGEventSourceRef source, CGWindowID window, Rect frame, Point point, std::int64_t eventNumber,
    int clicks
);
CGWindowID annotatedInputWindow(CGEventRef event);
} // namespace deskflow::windowshare::native

using deskflow::windowshare::FocusLease;
using deskflow::windowshare::ForwardingScope;
using deskflow::windowshare::InputLedger;
using deskflow::windowshare::KeyboardRouteBinding;
using deskflow::windowshare::NativeHost;
using deskflow::windowshare::NativeKeyboardAdmission;
namespace {
int failures = 0;
void check(bool result, const char *message)
{
  if (!result) {
    ++failures;
    QTextStream(stderr) << "FAIL: " << message << '\n';
  }
}
void write(const QString &path, const QByteArray &bytes)
{
  QFile file(path);
  check(file.open(QIODevice::WriteOnly), "create fixture journal");
  check(file.write(bytes) == bytes.size(), "write fixture journal");
}
QByteArray read(const QString &path)
{
  QFile file(path);
  file.open(QIODevice::ReadOnly);
  return file.readAll();
}
void exactWindowIdentity()
{
  using namespace deskflow::windowshare;
  const auto permissions = NativeHost::permissions();
  check(
      permissions["exactWindowIdentity"].toBool() == native::axWindowIdentityAvailable(),
      "probe reports exact native identity capability"
  );
  check(
      permissions["exactWindowIdentityApi"].toString().contains("private"),
      "probe discloses the native identity SPI dependency"
  );
  CGWindowID window = 123;
  check(
      native::axWindowId(nullptr, window) == kAXErrorIllegalArgument && window == 0,
      "missing AX objects cannot retain an earlier window ID"
  );
  window = 123;
  check(
      native::axWindowId(reinterpret_cast<AXUIElementRef>(CFSTR("not a window")), window) == kAXErrorIllegalArgument &&
          window == 0,
      "non-AX objects cannot grant native identity"
  );
  for (const auto [process, id] : {std::pair<pid_t, CGWindowID>{0, 123}, {-1, 123}, {getpid(), 0}}) {
    QString diagnostic;
    bool retryable = true;
    check(
        !native::copyExactAXWindow(process, id, &diagnostic, &retryable) && !retryable && !diagnostic.isEmpty(),
        "incomplete native identities reject before an Accessibility query"
    );
  }
}
void targetedPointerConstruction()
{
  using namespace deskflow::windowshare;
  using native::targetedPointerEvent;
  const deskflow::windowshare::Rect frame{-400, 180, 640, 448};
  using InputPoint = deskflow::windowshare::Point;
  const InputPoint point{100.25, 140.5};
  constexpr CGWindowID window = 987654; // Construction only: no real window or posting.
  const auto source = CGEventSourceCreate(kCGEventSourceStatePrivate);
  check(source != nullptr, "create an independent private event source");
  if (!source)
    return;
  const auto make = [&](CGEventType type, std::uint32_t button) {
    return targetedPointerEvent(source, window, frame, point, type, button, kCGEventFlagMaskShift, 42, 2);
  };
  const auto getLocal = reinterpret_cast<CGPoint (*)(CGEventRef)>(dlsym(RTLD_DEFAULT, "CGEventGetWindowLocation"));
  const bool supported = native::targetedPointerAvailable();
  const auto permissions = NativeHost::permissions();
  check(permissions["targetedPointer"].toBool() == supported, "probe reports the actual targeted-pointer capability");
  check(permissions["targetedPointerApi"].toString().contains("private"), "probe discloses the private API dependency");
  for (const auto type :
       {kCGEventMouseMoved, kCGEventLeftMouseDown, kCGEventLeftMouseUp, kCGEventLeftMouseDragged,
        kCGEventRightMouseDown, kCGEventRightMouseUp, kCGEventRightMouseDragged, kCGEventOtherMouseDown,
        kCGEventOtherMouseUp, kCGEventOtherMouseDragged, kCGEventScrollWheel}) {
    const auto button =
        type == kCGEventRightMouseDown || type == kCGEventRightMouseUp || type == kCGEventRightMouseDragged   ? 1u
        : type == kCGEventOtherMouseDown || type == kCGEventOtherMouseUp || type == kCGEventOtherMouseDragged ? 2u
                                                                                                              : 0u;
    const auto event = make(type, button);
    check(bool(event) == supported, "all pointer families fail closed without the private annotation API");
    if (!event)
      continue;
    check(getLocal != nullptr, "private annotation getter is available for independent readback");
    check(CGEventGetType(event) == type, "pointer construction preserves its event family");
    check(
        CGPointEqualToPoint(CGEventGetLocation(event), CGPointMake(frame.x + point.x, frame.y + point.y)),
        "targeted pointer keeps correct global coordinates with a negative screen origin"
    );
    check(
        getLocal && CGPointEqualToPoint(getLocal(event), CGPointMake(point.x, point.y)),
        "global-location assignment cannot erase or flip the independent window-local coordinates"
    );
    // CG's mouse-only fields are not defined for the scroll-wheel type. Its
    // AppKit window binding and local/global coordinates are checked above.
    if (type != kCGEventScrollWheel) {
      check(
          CGEventGetIntegerValueField(event, kCGMouseEventWindowUnderMousePointer) == window &&
              CGEventGetIntegerValueField(event, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent) == window,
          "both public window annotations identify the same selected window"
      );
      check(
          CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber) == button,
          "button identity survives construction"
      );
      check(
          CGEventGetIntegerValueField(event, kCGMouseEventNumber) == 42, "pointer event number survives construction"
      );
      check(CGEventGetIntegerValueField(event, kCGMouseEventClickState) == 2, "click count survives construction");
    }
    check(CGEventGetFlags(event) == kCGEventFlagMaskShift, "no artificial Command modifier is added");
    check(
        CGEventGetIntegerValueField(event, kCGEventSourceUserData) == syntheticEventTag &&
            CGEventGetIntegerValueField(event, kCGEventSourceStateID) == CGEventSourceGetSourceStateID(source),
        "constructed events retain the private source and synthetic loop marker"
    );
    CFRelease(event);
  }
  if (supported) {
    const auto fractional = targetedPointerEvent(source, window, frame, {100.1, 140.3}, kCGEventMouseMoved, 0, 0, 0, 0);
    check(fractional != nullptr, "fractional remote coordinates retain their independent annotation");
    if (fractional)
      CFRelease(fractional);
  }
  check(!make(kCGEventKeyDown, 0), "pointer constructor rejects keyboard events");
  check(!make(kCGEventMouseMoved, 32), "pointer constructor rejects an unsupported button");
  check(!targetedPointerEvent(nullptr, window, frame, point, kCGEventMouseMoved, 0, 0, 0, 0), "null sources denied");
  check(!targetedPointerEvent(source, 0, frame, point, kCGEventMouseMoved, 0, 0, 0, 0), "unbound window denied");
  for (const auto invalid :
       {InputPoint{-1, 1}, InputPoint{1, -1}, InputPoint{640, 1}, InputPoint{1, 448}, InputPoint{NAN, 1}})
    check(
        !targetedPointerEvent(source, window, frame, invalid, kCGEventMouseMoved, 0, 0, 0, 0),
        "outside or nonfinite local point denied"
    );
  for (const auto state : {kCGEventSourceStateCombinedSessionState, kCGEventSourceStateHIDSystemState}) {
    const auto shared = CGEventSourceCreate(state);
    check(shared != nullptr, "create shared-source rejection fixture");
    check(
        !targetedPointerEvent(shared, window, frame, point, kCGEventMouseMoved, 0, 0, 0, 0),
        "shared physical/session event sources denied"
    );
    if (shared)
      CFRelease(shared);
  }
  CFRelease(source);
}
void captureTiming()
{
  using namespace deskflow::windowshare;
  using native::CaptureTimeline;
  check(defaultCaptureRate == 30 && validCaptureRate(30) && validCaptureRate(60), "capture rates retain default30");
  for (const unsigned invalid : {0U, 1U, 29U, 31U, 45U, 59U, 61U, 120U})
    check(
        !validCaptureRate(invalid) && effectiveCaptureRate(invalid, true, true) == 0,
        "unsupported capture rates cannot become a scheduling interval"
    );
  for (const auto rate : {30U, 60U}) {
    check(
        effectiveCaptureRate(rate, true, true) == rate && effectiveCaptureRate(rate, false, true) == 10 &&
            effectiveCaptureRate(rate, false, false) == 2,
        "foreground selection preserves visible/background rate budgets"
    );
  }
  CaptureTimeline time;
  check(!time.replayPresentation(30), "replay requires a real source/encode baseline");
  for (const auto invalid :
       {kCMTimeInvalid, kCMTimeIndefinite, kCMTimePositiveInfinity, CMTimeMake(-1, 60), CMTimeMakeWithEpoch(1, 60, 1)})
    check(!time.observeSource(invalid), "invalid and unrelated-epoch source PTS is rejected");
  const auto first = CMTimeMake(1001, 60000);
  check(time.observeSource(first), "source timestamp is observed without nominal-rate fabrication");
  const auto encoded = time.sourcePresentation(first);
  check(
      encoded && encoded->value == first.value && encoded->timescale == first.timescale,
      "source rational PTS is preserved exactly"
  );
  check(
      !time.observeSource(first) && !time.observeSource(CMTimeMake(1, 60000)),
      "duplicate and backward source capture times are ignored"
  );
  const auto replay = time.replayPresentation(60);
  check(
      replay && CMTimeCompare(*replay, first) > 0 &&
          CMTimeCompare(*replay, CMTimeAdd(first, CMTimeMake(1, 1000000))) <= 0,
      "static recovery uses a bounded synthetic encoder tick, not a new capture interval"
  );
  for (unsigned i = 0; i != 2000; ++i)
    (void)time.replayPresentation(60);
  const auto delayed = CMTimeAdd(first, CMTimeMake(1, 100000000));
  check(
      time.observeSource(delayed) && !time.sourcePresentation(delayed),
      "a delayed real sample behind scheduled replay is not retimed as a new capture"
  );
  const auto fresh = CMTimeAdd(first, CMTimeMake(1, 20));
  check(
      time.observeSource(fresh) && time.sourcePresentation(fresh).has_value(),
      "fresh capture resumes after replay without permanently suppressing source samples"
  );
  // Capture owns this timeline across VT encoder replacement/geometry changes.
  // No encoder-local reset is permitted to rewind source or replay timestamps.
  const auto afterResize = CMTimeAdd(fresh, CMTimeMake(1, 30));
  check(
      time.observeSource(afterResize) && time.sourcePresentation(afterResize).has_value(),
      "encoder recreation and rate change retain the source timeline"
  );
  const auto replay30 = time.replayPresentation(30);
  check(
      replay30 && CMTimeCompare(*replay30, afterResize) > 0 &&
          CMTimeCompare(*replay30, CMTimeAdd(afterResize, CMTimeMake(1, 1000000))) <= 0,
      "recovery remains bounded after encoder replacement without resetting presentation origin"
  );
  for (const auto rate : {2U, 10U, 30U, 60U}) {
    CaptureTimeline flooded;
    const auto source = CMTimeMake(1, 1);
    check(
        flooded.observeSource(source) && flooded.sourcePresentation(source).has_value(),
        "replay flood fixture starts with a real encoded source"
    );
    unsigned repeats = 0;
    for (unsigned i = 0; i != 2000; ++i)
      repeats += flooded.replayPresentation(rate).has_value();
    check(
        repeats > 0 && repeats <= 1000 && !flooded.replayPresentation(rate),
        "many recovery requests cannot grow synthetic lead without another real source sample"
    );
    const auto next = CMTimeAdd(source, CMTimeMake(1, static_cast<int32_t>(rate)));
    check(flooded.observeSource(next), "first next nominal source sample remains observable after recovery flood");
    const auto presented = flooded.sourcePresentation(next);
    check(
        presented && presented->value == next.value && presented->timescale == next.timescale,
        "first next real sample is encoded at its exact PTS after arbitrarily repeated recovery requests"
    );
  }
  check(
      CMTimeCompare(CaptureTimeline::duration(first, 60), first) == 0 &&
          CMTimeCompare(CaptureTimeline::duration(kCMTimeInvalid, 60), CMTimeMake(1, 60)) == 0 &&
          CMTimeCompare(CaptureTimeline::duration(kCMTimeZero, 30), CMTimeMake(1, 30)) == 0 &&
          !CMTIME_IS_VALID(CaptureTimeline::duration(kCMTimeInvalid, 45)),
      "actual sample duration is retained and only unknown duration uses the selected interval"
  );
  CaptureTimeline exhausted;
  const auto maximum = CMTimeMake(INT64_MAX, 1);
  check(
      exhausted.observeSource(maximum) && exhausted.sourcePresentation(maximum).has_value() &&
          !exhausted.replayPresentation(60),
      "replay overflow cannot wrap or rewind time"
  );
}
KeyboardRouteBinding routeBinding()
{
  return {{11, 12}, 13, 14, 1, 16};
}
void originalNativeConstruction()
{
  using namespace deskflow::windowshare;
  const deskflow::windowshare::Rect frame{-800, -400, 640, 480};
  const deskflow::windowshare::Point point{123.25, 210.5};
  constexpr CGWindowID window = 987654; // Construction only, never a real target.
  const auto source = CGEventSourceCreate(kCGEventSourceStatePrivate);
  check(source != nullptr, "private original-input event source");
  if (!source)
    return;
  const auto getLocal = reinterpret_cast<CGPoint (*)(CGEventRef)>(dlsym(RTLD_DEFAULT, "CGEventGetWindowLocation"));
  const std::vector<OriginalInputRecord> records{
      {71, 1, 1000, 0, OriginalInputKey{42, 0, OriginalKeyKind::Down, OriginalKeyTransition::Down, false}},
      {71, 2, 1100, 0, OriginalInputKey{42, 0, OriginalKeyKind::Down, OriginalKeyTransition::Repeat, true}},
      {71, 3, 1200, originalKey::shiftFlag | originalKey::rightShiftFlag,
       OriginalInputKey{
           42, originalKey::rightShift, OriginalKeyKind::FlagsChanged, OriginalKeyTransition::Down, false
       }},
      {71, 4, 1300, 0, OriginalInputButton{31, true, 7}},
      {71, 5, 1400, 0, OriginalInputMotion{-1234, 5678, 31}},
      {71, 6, 1500, 0, OriginalInputButton{31, false, 7}},
      {71, 7, 1600, 0,
       OriginalInputScroll{
           {-3, 7}, {-21, 49}, {-196609, 458751}, OriginalScrollPhase::Began, OriginalMomentumPhase::None, true
       }},
      {71, 8, 1700, 0,
       OriginalInputScroll{
           {-5, 9}, {-35, 63}, {-327681, 589823}, OriginalScrollPhase::None, OriginalMomentumPhase::Continued, true
       }},
      {71, 9, 1800, 0, OriginalInputKey{42, 0, OriginalKeyKind::Up, OriginalKeyTransition::Up, false}},
      {71, 10, 1900, 0,
       OriginalInputKey{42, originalKey::rightShift, OriginalKeyKind::FlagsChanged, OriginalKeyTransition::Up, false}}
  };
  for (const auto &record : records) {
    const auto original = createOriginalInputEvent(record, CGPointMake(frame.x + point.x, frame.y + point.y), source);
    check(original != nullptr, "original factory preserves record before target annotation");
    if (!original)
      continue;
    const bool keyboard = std::holds_alternative<OriginalInputKey>(record.payload);
    // Include a surrogate pair and combining mark so reconstructing the AppKit
    // template cannot silently replace a factory event's UTF-16 payload.
    constexpr std::array<UniChar, 5> text{'a', 0xd83d, 0xde42, 'e', 0x0301};
    if (keyboard)
      CGEventKeyboardSetUnicodeString(original, text.size(), text.data());
    const auto event = native::targetedOriginalInputEvent(original, source, window, frame, point, 55, 7);
    check(
        bool(event) == (keyboard || native::targetedPointerAvailable()),
        "original pointer targeting fails closed without private annotation API"
    );
    if (event) {
      check(
          CGEventGetTimestamp(event) == record.sourceTimestampNs && CGEventGetFlags(event) == record.flags &&
              CGEventGetType(event) == CGEventGetType(original),
          "targeting preserves exact timestamp, raw modifiers and native event kind"
      );
      check(
          CGEventGetIntegerValueField(event, kCGEventSourceStateID) == CGEventSourceGetSourceStateID(source),
          "targeting retains the private source state table"
      );
      check(
          native::annotatedInputWindow(event) == window,
          "every native event family retains the exact AppKit window annotation"
      );
      if (keyboard) {
        std::array<UniChar, text.size()> actual{};
        UniCharCount length = 0;
        CGEventKeyboardGetUnicodeString(event, actual.size(), &length, actual.data());
        check(
            length == text.size() && actual == text, "keyboard annotation preserves the complete factory UTF-16 payload"
        );
        const auto alternate = native::targetedOriginalInputEvent(original, source, window + 1, frame, point, 0, 0);
        check(
            alternate && native::annotatedInputWindow(alternate) == window + 1 &&
                native::annotatedInputWindow(event) == window,
            "independent key annotations cannot mutate another retained destination"
        );
        if (alternate)
          CFRelease(alternate);
      }
      if (!keyboard) {
        check(
            getLocal && CGPointEqualToPoint(getLocal(event), CGPointMake(point.x, point.y)) &&
                CGPointEqualToPoint(CGEventGetLocation(event), CGPointMake(frame.x + point.x, frame.y + point.y)),
            "original input has independent exact window-local and negative-origin global coordinates"
        );
      }
      check(
          captureOriginalInputEvent(CGEventGetType(event), event, record.epoch, record.sequence).status ==
              OriginalInputCapture::Status::Ignored,
          "annotated production event retains synthetic loop suppression"
      );
      // UserData comes from a retained CGEventSource; writing zero to the
      // copied event does not clear that source marker on current macOS. Give
      // only the unposted readback copy a separate source with a non-marker
      // fixture value. The production event must remain independently tagged.
      const auto inspect = CGEventCreateCopy(event);
      const auto inspectSource = CGEventSourceCreate(kCGEventSourceStatePrivate);
      check(inspect != nullptr, "copy original native event for readback");
      check(inspectSource != nullptr, "independent unposted source for parser readback");
      if (inspect && inspectSource) {
        CGEventSourceSetUserData(inspectSource, 1);
        CGEventSourceSetKeyboardType(inspectSource, CGEventGetIntegerValueField(event, kCGKeyboardEventKeyboardType));
        CGEventSetSource(inspect, inspectSource);
        CGEventSetIntegerValueField(inspect, kCGEventSourceUserData, 1);
        check(
            CGEventGetIntegerValueField(inspect, kCGEventSourceUserData) == 1 &&
                CGEventGetIntegerValueField(event, kCGEventSourceUserData) == syntheticEventTag,
            "readback copy changes only its independent loop marker"
        );
        check(
            CGEventGetTimestamp(inspect) == record.sourceTimestampNs && CGEventGetFlags(inspect) == record.flags &&
                CGEventGetType(inspect) == CGEventGetType(event),
            "readback source cannot change modeled timestamp, modifiers or input family"
        );
        const auto observed =
            captureOriginalInputEvent(CGEventGetType(inspect), inspect, record.epoch, record.sequence);
        if (!observed.record) {
          QTextStream(stderr) << "Native construction readback: sequence=" << record.sequence
                              << " status=" << static_cast<int>(observed.status)
                              << " tag=" << CGEventGetIntegerValueField(inspect, kCGEventSourceUserData)
                              << " type=" << CGEventGetType(inspect) << " time=" << CGEventGetTimestamp(inspect)
                              << " flags=" << CGEventGetFlags(inspect)
                              << " code=" << CGEventGetIntegerValueField(inspect, kCGKeyboardEventKeycode)
                              << " keyboardType=" << CGEventGetIntegerValueField(inspect, kCGKeyboardEventKeyboardType)
                              << " repeat=" << CGEventGetIntegerValueField(inspect, kCGKeyboardEventAutorepeat)
                              << " button=" << CGEventGetIntegerValueField(inspect, kCGMouseEventButtonNumber)
                              << " subtype=" << CGEventGetIntegerValueField(inspect, kCGMouseEventSubtype) << '\n';
        }
        check(observed.record.has_value(), "targeted event round-trips through real native capture parser");
        if (observed.record) {
          const auto &payload = observed.record->payload;
          if (const auto *key = std::get_if<OriginalInputKey>(&record.payload)) {
            const auto *actual = std::get_if<OriginalInputKey>(&payload);
            check(
                actual && actual->keyboardType == key->keyboardType && actual->code == key->code &&
                    actual->kind == key->kind && actual->transition == key->transition && actual->repeat == key->repeat,
                "original physical key/type/side/repeat semantics survive native targeting"
            );
          } else if (const auto *motion = std::get_if<OriginalInputMotion>(&record.payload)) {
            const auto *actual = std::get_if<OriginalInputMotion>(&payload);
            check(
                actual && actual->dx == motion->dx && actual->dy == motion->dy &&
                    actual->dragButton == motion->dragButton,
                "signed raw deltas and high-button drag survive native targeting without scaling"
            );
          } else if (const auto *button = std::get_if<OriginalInputButton>(&record.payload)) {
            const auto *actual = std::get_if<OriginalInputButton>(&payload);
            check(
                actual && actual->button == button->button && actual->down == button->down &&
                    actual->clickCount == button->clickCount,
                "original button and click count survive native targeting without the Qt click clamp"
            );
          } else {
            const auto &scroll = std::get<OriginalInputScroll>(record.payload);
            const auto *actual = std::get_if<OriginalInputScroll>(&payload);
            check(
                actual && actual->lines == scroll.lines && actual->points == scroll.points &&
                    actual->fixed16_16 == scroll.fixed16_16 && actual->phase == scroll.phase &&
                    actual->momentum == scroll.momentum && actual->continuous == scroll.continuous,
                "all scroll representations and gesture/momentum phases survive native targeting"
            );
          }
        }
      }
      if (inspect)
        CFRelease(inspect);
      if (inspectSource)
        CFRelease(inspectSource);
      CFRelease(event);
    }
    CFRelease(original);
  }
  const OriginalInputRecord press{
      72, 1, 2000, originalKey::controlFlag | originalKey::rightControlFlag,
      OriginalInputKey{51, originalKey::rightControl, OriginalKeyKind::FlagsChanged, OriginalKeyTransition::Down, false}
  };
  const OriginalInputRelease release{7, press, 0};
  const auto rawRelease = createOriginalInputRelease(release, 2100, CGPointZero, source);
  const auto targetedRelease = native::targetedOriginalInputEvent(rawRelease, source, window, frame, point, 0, 0);
  check(
      targetedRelease && native::annotatedInputWindow(targetedRelease) == window &&
          CGEventGetType(targetedRelease) == kCGEventFlagsChanged && CGEventGetFlags(targetedRelease) == 0 &&
          CGEventGetIntegerValueField(targetedRelease, kCGKeyboardEventKeycode) == originalKey::rightControl &&
          CGEventGetIntegerValueField(targetedRelease, kCGKeyboardEventKeyboardType) == 51,
      "owned modifier cleanup keeps the original window, side, keyboard type and released flags"
  );
  if (targetedRelease)
    CFRelease(targetedRelease);
  if (rawRelease)
    CFRelease(rawRelease);
  const OriginalInputRecord lossyMotion{72, 2, 2200, 0, OriginalInputMotion{-2147483648LL, 2147483647, 31}};
  const auto narrowed = createOriginalInputEvent(lossyMotion, CGPointZero, source);
  check(!narrowed, "lossy extreme Quartz deltas cannot reach target annotation or native posting");
  if (narrowed)
    CFRelease(narrowed);
  CFRelease(source);
}
void keyboardAdmission()
{
  const auto binding = routeBinding();
  NativeKeyboardAdmission gate;
  InputLedger ledger;
  FocusLease focus;
  const auto revision = focus.request(binding.owner);
  check(focus.acknowledge(binding.owner, revision), "fixture focus lease acknowledges");
  check(revision == binding.focusRevision, "fixture route uses the acknowledged focus revision");
  check(!gate.blocksKeys() && !gate.blocksPointers(), "network keyboard and pointer begin admitted");
  check(
      !gate.quiesce(binding, std::nullopt, true), "viewer acknowledgment without a current native candidate is denied"
  );
  auto invalid = binding;
  invalid.generation = 0;
  check(!gate.quiesce(invalid, invalid, true), "zero route generation cannot fence input");
  invalid.generation = std::uint64_t{1} << 53;
  check(!gate.quiesce(invalid, invalid, true), "inexact JSON route generation cannot fence input");

  std::vector<KeyboardRouteBinding> changed;
  auto other = binding;
  ++other.owner.session;
  changed.push_back(other);
  other = binding;
  ++other.owner.epoch;
  changed.push_back(other);
  for (const auto member :
       {&KeyboardRouteBinding::generation, &KeyboardRouteBinding::focusRequest, &KeyboardRouteBinding::focusRevision,
        &KeyboardRouteBinding::geometry}) {
    other = binding;
    ++(other.*member);
    changed.push_back(other);
  }
  for (const auto &mismatch : changed)
    check(!gate.quiesce(binding, mismatch, true), "every candidate binding field must match exactly");
  check(!gate.blocksKeys(), "denied admission attempts do not alter network forwarding");

  check(ledger.down(true, 0x01000020, binding.owner), "fixture holds Qt Shift");
  check(!gate.quiesce(binding, binding, ledger.size() == 0), "held Qt key denies quiescence");
  check(ledger.ownerOf(true, 0x01000020) == binding.owner, "failed quiescence preserves the Qt key owner");
  check(ledger.up(true, 0x01000020, binding.owner) == binding.owner, "Qt up releases its existing owner");
  check(ledger.down(false, 0, binding.owner), "fixture holds left button");
  check(!gate.quiesce(binding, binding, ledger.size() == 0), "held mouse button denies quiescence");
  check(ledger.ownerOf(false, 0) == binding.owner, "failed handoff never synthesizes a pointer release");
  check(ledger.up(false, 0, binding.owner) == binding.owner, "physical pointer up retains its owner");

  check(gate.quiesce(binding, binding, ledger.size() == 0), "neutral exact candidate closes keyboard admission");
  check(gate.blocksKeys(), "network key down and repeat admission is blocked while fenced");
  check(!gate.blocksPointers(), "a legacy keyboard-only fence leaves pointer forwarding admitted");
  check(gate.quiesce(binding, binding, true), "exact repeated acknowledgment is idempotent while valid");
  check(gate.quiesced(binding, binding, true), "quiescence verifies the exact valid gate");
  check(focus.permits(binding.owner, binding.focusRevision), "quiescence preserves the existing focus lease");
  check(!ledger.up(true, 0x01000020, binding.owner), "late Qt up cannot invent a delivered key owner");
  check(gate.blocksKeys(), "late Qt up cannot reopen network admission");
  check(ledger.down(false, 0, binding.owner), "pointer input continues while keyboard is fenced");
  check(!gate.quiesced(binding, binding, ledger.size() == 0), "held pointer denies a later neutral admission check");
  check(
      gate.blocksKeys() && focus.permits(binding.owner, binding.focusRevision), "held pointer preserves gate and focus"
  );
  check(ledger.up(false, 0, binding.owner) == binding.owner, "pointer up is unaffected by the keyboard gate");
  check(gate.quiesced(binding, binding, ledger.size() == 0), "pointer release restores the neutral admission boundary");

  for (const auto &mismatch : changed) {
    check(!gate.quiesced(binding, mismatch, true), "changed current facts deny quiescence immediately");
    check(!gate.quiesce(mismatch, mismatch, true), "another binding cannot replace an outstanding gate");
    check(!gate.resume(mismatch), "mismatched resume cannot reopen admission");
    check(gate.blocksKeys(), "mismatched operations preserve blocking state");
  }
  check(!gate.quiesced(binding, std::nullopt, true), "hidden or returned source cannot remain admissible");
  check(gate.blocksKeys(), "loss of native candidate alone never reopens keyboard input");
  check(!gate.invalidate(deskflow::windowshare::Owner{99, 12}), "another owner's change leaves this gate intact");
  check(gate.quiesced(binding, binding, true), "unrelated owner invalidation is inert");
  const auto revoked = gate.invalidate(binding.owner);
  check(revoked == binding, "invalidation identifies the exact fenced owner");
  check(!gate.quiesced(binding, binding, true) && gate.blocksKeys(), "invalidation marks a blocking tombstone first");
  check(!gate.blocksPointers(), "invalidating a legacy keyboard fence cannot widen its pointer scope");
  check(!gate.quiesce(binding, binding, true), "fresh acknowledgment cannot revive an invalidated gate");
  check(!gate.invalidate(), "reentrant invalidation emits only one notification");
  auto newer = binding;
  ++newer.generation;
  check(!gate.quiesce(newer, newer, true), "reentrant replacement is denied before exact resume");
  check(gate.resume(*revoked), "exact owner resume clears its tombstone after authority revocation");
  check(!gate.blocksKeys(), "exact resume reopens network admission");
  check(!gate.quiesce(binding, binding, true), "retired generation cannot be fenced again");
  check(gate.quiesce(newer, newer, true), "a newer route can fence the same still-focused source");
  check(!gate.resume(binding), "late old resume cannot clear the newer route");
  check(gate.quiesced(newer, newer, true), "newer route survives old callback completion");
  check(gate.invalidate() == newer, "global lifecycle invalidation preserves the latest exact owner");
  check(!gate.quiesced(newer, std::nullopt, true) && gate.blocksKeys(), "returned entry leaves a blocking tombstone");
  check(gate.resume(newer), "returned entry is not required to release an exact tombstone");
  other = binding;
  ++other.owner.session;
  other.generation = 1;
  check(gate.quiesce(other, other, true), "a fresh window incarnation may start its own generation sequence");
  check(!gate.resume(newer) && gate.blocksKeys(), "prior incarnation cannot reopen a new owner's keyboard route");
  check(gate.resume(other), "new owner's exact resume succeeds");
}
void fullInputAdmission()
{
  auto binding = routeBinding();
  binding.scope = ForwardingScope::All;
  auto keyboard = binding;
  keyboard.scope = ForwardingScope::Keyboard;
  check(binding != keyboard, "a full-input binding differs from the otherwise identical keyboard binding");
  NativeKeyboardAdmission gate;
  InputLedger ledger;
  check(!gate.blocksKeys() && !gate.blocksPointers(), "full-input fixture starts with both paths admitted");
  auto invalid = binding;
  invalid.scope = static_cast<ForwardingScope>(123);
  check(!gate.quiesce(invalid, invalid, true), "unknown scope cannot fence native forwarding");
  check(!gate.quiesce(binding, std::nullopt, true), "full-input fence requires an observed native candidate");
  check(!gate.quiesce(binding, keyboard, true), "keyboard-only candidate cannot acknowledge full-input quiescence");
  check(!gate.quiesce(keyboard, binding, true), "full-input candidate cannot be silently downgraded to keyboard");
  check(!gate.blocksKeys() && !gate.blocksPointers(), "failed full-input admission does not change either route");

  for (const auto isKey : {true, false}) {
    const auto code = isKey ? 0x01000020u : 0u;
    check(ledger.down(isKey, code, binding.owner), "fixture retains physical key or pointer ownership");
    check(!gate.quiesce(binding, binding, ledger.size() == 0), "held input denies full-input cutover");
    check(
        ledger.ownerOf(isKey, code) == binding.owner && !gate.blocksKeys() && !gate.blocksPointers(),
        "denied full-input cutover preserves ownership and the still-needed release route"
    );
    check(ledger.up(isKey, code, binding.owner) == binding.owner, "the prior owner receives its matching release");
  }
  check(gate.quiesce(binding, binding, ledger.size() == 0), "neutral exact full-input candidate fences both paths");
  check(gate.blocksKeys() && gate.blocksPointers(), "full-input fence blocks keyboard and pointer forwarding");
  check(gate.quiesce(binding, binding, true), "repeated exact full-input acknowledgement is idempotent");
  check(gate.quiesced(binding, binding, true), "full-input quiescence retains all current facts");
  check(!gate.quiesced(binding, binding, false), "full-input quiescence must remain neutral at every query");
  check(!gate.quiesced(binding, std::nullopt, true), "missing native candidate cannot retain admissible quiescence");
  check(gate.blocksKeys() && gate.blocksPointers(), "loss of candidate or neutrality never reopens either path");

  std::vector<KeyboardRouteBinding> changed{keyboard, invalid};
  auto other = binding;
  ++other.owner.session;
  changed.push_back(other);
  other = binding;
  ++other.owner.epoch;
  changed.push_back(other);
  for (const auto member :
       {&KeyboardRouteBinding::generation, &KeyboardRouteBinding::focusRequest, &KeyboardRouteBinding::focusRevision,
        &KeyboardRouteBinding::geometry}) {
    other = binding;
    ++(other.*member);
    changed.push_back(other);
  }
  for (const auto &mismatch : changed) {
    check(!gate.quiesced(binding, mismatch, true), "every current binding field participates in full-input admission");
    check(!gate.quiesced(mismatch, binding, true), "every queried binding field participates in full-input admission");
    check(!gate.quiesce(mismatch, mismatch, true), "a live full-input gate cannot be replaced or downgraded");
    check(!gate.resume(mismatch), "wrong-scope or stale completion cannot reopen a live full-input gate");
    check(gate.blocksKeys() && gate.blocksPointers(), "mismatched operations preserve both forwarding fences");
  }
  check(!gate.invalidate(deskflow::windowshare::Owner{99, 12}), "an unrelated source cannot invalidate this gate");
  check(gate.quiesced(binding, binding, true), "unrelated invalidation preserves exact full-input readiness");
  check(gate.invalidate(binding.owner) == binding, "invalidation returns the complete original scope and binding");
  check(
      !gate.quiesced(binding, binding, true) && gate.blocksKeys() && gate.blocksPointers(),
      "invalidated full-input tombstone keeps both forwarding paths fenced"
  );
  check(!gate.invalidate(), "full-input lifecycle invalidation is delivered only once");
  check(!gate.quiesce(binding, binding, true), "a repeated full-input acknowledgement cannot revive invalidation");
  for (const auto &mismatch : changed) {
    check(!gate.quiesce(mismatch, mismatch, true), "invalidated full-input fence cannot be replaced or downgraded");
    check(!gate.resume(mismatch), "tombstone cannot be resumed by a different scope or binding");
    check(gate.blocksKeys() && gate.blocksPointers(), "invalidated route retains both fences until exact resume");
  }
  check(gate.resume(binding), "exact full-input resume clears the tombstone after authority revocation");
  check(!gate.blocksKeys() && !gate.blocksPointers(), "exact resume reopens keyboard and pointer together");
  check(!gate.resume(binding), "duplicate full-input resume cannot affect another route");
  check(!gate.quiesce(binding, binding, true), "retired full-input generation cannot be revived");
  check(!gate.quiesce(keyboard, keyboard, true), "changing scope cannot replay a retired generation");
  auto older = binding;
  --older.generation;
  check(!gate.quiesce(older, older, true), "older full-input generations remain retired");
  older.scope = ForwardingScope::Keyboard;
  check(!gate.quiesce(older, older, true), "scope downgrade cannot bypass generation retirement");

  auto newerKeyboard = keyboard;
  ++newerKeyboard.generation;
  check(gate.quiesce(newerKeyboard, newerKeyboard, true), "newer negotiated keyboard-only route remains supported");
  check(gate.blocksKeys() && !gate.blocksPointers(), "new keyboard-only generation does not inherit old pointer fence");
  auto newerAll = newerKeyboard;
  newerAll.scope = ForwardingScope::All;
  check(!gate.quiesce(newerAll, newerAll, true), "a live keyboard gate cannot be silently widened");
  check(!gate.resume(newerAll), "full-input completion cannot resume a keyboard-only gate");
  check(gate.invalidate() == newerKeyboard, "keyboard-only invalidation preserves its scope after full-input use");
  check(gate.blocksKeys() && !gate.blocksPointers(), "keyboard-only tombstone does not acquire pointer ownership");
  check(gate.resume(newerKeyboard), "exact newer keyboard resume succeeds");
  check(!gate.quiesce(newerAll, newerAll, true), "scope upgrade cannot reuse a retired keyboard generation");
  ++newerAll.generation;
  check(gate.quiesce(newerAll, newerAll, true), "next full-input generation can establish a fresh fence");
  check(!gate.resume(binding) && !gate.resume(newerKeyboard), "old completions cannot clear newer full-input fences");
  check(gate.blocksKeys() && gate.blocksPointers(), "new full-input route survives both kinds of stale completion");
  check(gate.resume(newerAll), "exact newest full-input resume succeeds");

  other = binding;
  ++other.owner.epoch;
  other.generation = 1;
  check(gate.quiesce(other, other, true), "a new source epoch may begin its own full-input generation sequence");
  check(!gate.resume(newerAll), "prior source epoch cannot reopen new full-input ownership");
  check(gate.invalidate() == other && gate.blocksPointers(), "global invalidation preserves the new epoch scope");
  check(gate.resume(other), "new source epoch resumes only its exact tombstone");
  auto newWindow = other;
  ++newWindow.owner.session;
  check(gate.quiesce(newWindow, newWindow, true), "another source window can begin its own full-input sequence");
  check(!gate.resume(other) && gate.blocksKeys() && gate.blocksPointers(), "prior window cannot reopen new ownership");
  check(gate.resume(newWindow), "new window resumes its own full-input fence");
}
void preparationCancellation()
{
  using namespace deskflow::windowshare;
  struct Fixture
  {
    Session session;
    NativeSourceTransaction transaction;
    deskflow::windowshare::Rect source{350, 400, 640, 480}; // User moved it during capture preparation.
    bool exact = true, closed = false, restoreSucceeds = true;
    unsigned nativeQueries = 0, moves = 0, raises = 0, retained = 0;
    std::vector<std::string> calls;
    NativeSourceReturnAccess access;
    Fixture()
    {
      session.id = 71;
      session.journaled = true;
      session.prepare(1, {100, 100, 640, 480}, {100, 100, 640, 480});
      access.current = [&] { return exact && !closed; };
      access.fence = [&] {
        calls.emplace_back("fence");
        session.beginReturn();
      };
      access.releaseInput = [&] { calls.emplace_back("release"); };
      access.stopCapture = [&] { calls.emplace_back("stop"); };
      access.restoreSource = [&] {
        calls.emplace_back("native");
        ++nativeQueries;
        ++moves;
        source = session.original;
        ++raises;
        return restoreSucceeds;
      };
      access.retainFailure = [&] {
        calls.emplace_back("retain");
        ++retained;
      };
      access.finish = [&] {
        calls.emplace_back("close");
        closed = true;
      };
    }
    NativeSourceReturnResult cancel()
    {
      return returnNativeSource(transaction, access);
    }
  };
  for (const bool painted : {false, true}) {
    Fixture fixture;
    if (painted)
      fixture.session.frameReady(1);
    check(
        fixture.cancel() == NativeSourceReturnResult::Closed,
        "actual return procedure closes an untouched Preparing session"
    );
    check(
        fixture.calls == std::vector<std::string>{"fence", "release", "stop", "close"} && !fixture.nativeQueries &&
            !fixture.moves && !fixture.raises && fixture.source.x == 350 && fixture.source.y == 400 &&
            fixture.session.phase == Phase::Returning,
        "cancel before native mutation preserves a continuing local drag and focus without even querying the source"
    );
    const auto calls = fixture.calls;
    check(
        fixture.cancel() == NativeSourceReturnResult::Aborted && fixture.calls == calls,
        "duplicate cancellation cannot stop or close twice"
    );
  }
  Fixture failedJournal;
  unsigned writes = 0;
  check(
      !failedJournal.transaction.arm([&] {
        ++writes;
        check(
            failedJournal.transaction.journalRestoreRequired(),
            "write-ahead intent is visible in the record before persistence"
        );
        return false;
      }),
      "failed persistence cannot arm native mutation"
  );
  check(
      !failedJournal.transaction.attemptMutation() && !failedJournal.transaction.commit(),
      "undurable intent cannot mutate or authorize Remote input"
  );
  check(
      failedJournal.cancel() == NativeSourceReturnResult::Closed && !failedJournal.nativeQueries && writes == 1,
      "failed arming still leaves an untouched source alone in this live process"
  );

  Fixture durableUntouched;
  check(durableUntouched.transaction.arm([] { return true; }), "durable preparation intent can arm a future operation");
  check(
      durableUntouched.cancel() == NativeSourceReturnResult::Closed && !durableUntouched.moves,
      "arming alone is not an attempted source mutation"
  );
  check(
      !durableUntouched.transaction.arm([&] {
        ++writes;
        return true;
      }) &&
          !durableUntouched.transaction.attemptMutation() && !durableUntouched.transaction.commit() && writes == 1,
      "cancelled preparation cannot revive through delayed persist/Ready completion"
  );

  for (const bool committed : {false, true}) {
    Fixture fixture;
    check(fixture.transaction.arm([] { return true; }), "mutation/commit recovery intent persisted");
    if (committed) {
      fixture.session.frameReady(1);
      fixture.session.commit(1);
      check(fixture.transaction.commit(), "source-visible Remote session retains ordinary return recovery");
    } else {
      check(fixture.transaction.attemptMutation(), "attempt is latched before a native call can partially fail");
    }
    check(
        fixture.cancel() == NativeSourceReturnResult::Closed && fixture.nativeQueries == 1 && fixture.moves == 1 &&
            fixture.raises == 1 && fixture.source.x == 100 && fixture.source.y == 100,
        "attempted source placement and committed visible sessions still use exact native recovery"
    );
    check(
        fixture.calls == std::vector<std::string>{"fence", "release", "stop", "native", "close"},
        "source restoration follows admission fencing, owner release and capture stop"
    );
  }
  Fixture failure;
  failure.transaction.arm([] { return true; });
  failure.transaction.attemptMutation();
  failure.restoreSucceeds = false;
  check(
      failure.cancel() == NativeSourceReturnResult::RecoveryRetained && failure.retained == 1 && failure.closed,
      "failed native restoration retains the journal before closing its export"
  );
  check(
      failure.calls == std::vector<std::string>{"fence", "release", "stop", "native", "retain", "close"},
      "recovery retention precedes export retirement"
  );

  for (int boundary = 0; boundary < 5; ++boundary) {
    Fixture fixture;
    fixture.transaction.arm([] { return true; });
    fixture.transaction.attemptMutation();
    if (boundary == 0)
      fixture.access.fence = [&] { fixture.exact = false; };
    if (boundary == 1)
      fixture.access.releaseInput = [&] { fixture.exact = false; };
    if (boundary == 2)
      fixture.access.stopCapture = [&] { fixture.exact = false; };
    if (boundary == 3)
      fixture.access.restoreSource = [&] {
        fixture.exact = false;
        return true;
      };
    if (boundary == 4) {
      fixture.restoreSucceeds = false;
      fixture.access.retainFailure = [&] { fixture.exact = false; };
    }
    check(
        fixture.cancel() == NativeSourceReturnResult::Aborted && !fixture.closed,
        "owner replacement/destruction after any callback cannot retire a replacement export"
    );
    if (boundary < 3)
      check(
          !fixture.nativeQueries && !fixture.moves && !fixture.raises,
          "lost owner cannot reach native restore callbacks"
      );
  }
  Fixture reentrant;
  reentrant.access.releaseInput = [&] {
    check(reentrant.session.phase == Phase::Returning, "admission is fenced before release callback reentry");
    check(
        reentrant.cancel() == NativeSourceReturnResult::Aborted, "nested return cannot acquire the same source cleanup"
    );
    reentrant.calls.emplace_back("release");
  };
  check(
      reentrant.cancel() == NativeSourceReturnResult::Closed && !reentrant.nativeQueries,
      "outer untouched cancellation completes after safe reentry"
  );
  Fixture cancelledArming;
  check(
      !cancelledArming.transaction.arm([&] {
        check(
            cancelledArming.cancel() == NativeSourceReturnResult::Closed,
            "cancellation can retire a source during persistence callback"
        );
        return true;
      }) &&
          !cancelledArming.transaction.attemptMutation(),
      "late durable acknowledgement cannot revive cancelled preparation"
  );

  check(nativeJournalRestoreRequired({}) == true, "legacy journal entries retain conservative restoration");
  check(
      nativeJournalRestoreRequired({{"restoreRequired", false}}) == false &&
          nativeJournalRestoreRequired({{"restoreRequired", true}}) == true,
      "only exact journal booleans express preparation recovery disposition"
  );
  for (const auto invalid :
       {QJsonValue(), QJsonValue(0), QJsonValue(1), QJsonValue(QStringLiteral("false")), QJsonValue(QJsonArray{})})
    check(
        !nativeJournalRestoreRequired({{"restoreRequired", invalid}}),
        "malformed recovery intent is not permission to discard a journal"
    );
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  exactWindowIdentity();
  targetedPointerConstruction();
  captureTiming();
  originalNativeConstruction();
  keyboardAdmission();
  fullInputAdmission();
  preparationCancellation();
  QTemporaryDir directory;
  check(directory.isValid(), "temporary journal directory");
  if (!directory.isValid())
    return 1;
  const auto path = directory.filePath("restore.json");
  {
    NativeHost host(path);
    check(
        host.captureFrameRate() == 30 && host.setCaptureFrameRate(60) && host.captureFrameRate() == 60 &&
            !host.setCaptureFrameRate(45) && host.captureFrameRate() == 60 && host.setCaptureFrameRate(30),
        "native startup configuration selects30/60 and an invalid option leaves it unchanged"
    );
    // This deliberately never starts the event loop: --recover has the same
    // lifecycle, and must not depend on the deferred constructor callback.
    check(host.recover(), "synchronous recovery accepts a missing journal");
    check(!QFile::exists(path), "probe recovery does not create an unnecessary journal");
    const auto binding = routeBinding();
    int invalidations = 0;
    host.onKeyboardRouteInvalidated = [&](KeyboardRouteBinding, deskflow::windowshare::KeyboardRouteReason) {
      ++invalidations;
    };
    check(
        !host.keyboardRouteCandidate(binding.owner.session, binding.owner.epoch, binding.generation),
        "nonexistent native entry cannot produce a route candidate"
    );
    check(
        !host.quiesceKeyboard(binding) && !host.keyboardRouteQuiesced(binding),
        "an acknowledgment cannot admit a route without an exported native entry"
    );
    check(!host.resumeKeyboard(binding), "a route without a gate cannot be resumed");
    auto all = binding;
    all.scope = ForwardingScope::All;
    check(
        !host.keyboardRouteCandidate(all.owner.session, all.owner.epoch, all.generation, all.scope),
        "nonexistent native entry cannot produce a full-input route candidate"
    );
    check(
        !host.quiesceKeyboard(all) && !host.keyboardRouteQuiesced(all) && !host.resumeKeyboard(all),
        "full-input acknowledgement cannot create native ownership without an exported entry"
    );
    check(
        !host.establishOriginalInput(all, 71) && !host.originalInputCurrent(all, 71),
        "original endpoint requires a real exact exported native owner"
    );
    const deskflow::windowshare::OriginalInputRecord original{
        71, 1, 1000, 0, deskflow::windowshare::OriginalInputMotion{1, -1}
    };
    const deskflow::windowshare::LocalInputMappedPoint mapped{
        1, 1, 640, 480, deskflow::windowshare::localInputMonotonicNs() + 200000000
    };
    check(
        !host.deliverOriginalInput(all, original, mapped),
        "record and mapped point alone do not authorize native delivery"
    );
    host.revokeOriginalInput(all);
    host.releaseAll();
    check(invalidations == 0, "release without a gate never fabricates an invalidation");
  }
  {
    // No native window has this fixture identity. The actual synchronous
    // NativeHost recovery path must discard its explicitly untouched record
    // before any AX permission check, WindowServer query or native mutation.
    QJsonObject record{
        {"windowId", 987654},
        {"pid", 987654},
        {"launch", 1234567890},
        {"bundle", "org.deskflow.test"},
        {"title", "untouched preparation"},
        {"original", QJsonObject{{"x", 100}, {"y", 100}, {"width", 640}, {"height", 480}}},
        {"session", 71},
        {"epoch", 1},
        {"restoreRequired", false}
    };
    QString diagnostic;
    check(
        deskflow::windowshare::native::persistJournal(path, {record}, &diagnostic),
        "persist an untouched preparation record"
    );
    {
      NativeHost host(path);
      int errors = 0;
      host.onError = [&](QString) { ++errors; };
      check(
          host.recover() && !errors, "actual NativeHost recovery retires untouched preparation without native recovery"
      );
    }
    check(
        QJsonDocument::fromJson(read(path))["windows"].toArray().isEmpty(),
        "untouched preparation record is durably removed"
    );
    record["restoreRequired"] = QStringLiteral("false");
    check(
        deskflow::windowshare::native::persistJournal(path, {record}, &diagnostic), "persist malformed intent fixture"
    );
    {
      NativeHost host(path);
      check(!host.recover(), "actual NativeHost rejects a textual recovery intent before any native lookup");
    }
    check(
        QJsonDocument::fromJson(read(path))["windows"].toArray() == QJsonArray{record},
        "malformed recovery intent survives failed recovery unchanged"
    );
  }
  const QByteArray malformed("{partial power-loss data");
  write(path, malformed);
  {
    NativeHost host(path);
    bool reported = false;
    host.onError = [&](QString) { reported = true; };
    check(!host.recover(), "corrupt recovery journal fails closed");
    check(reported, "synchronous recovery reports corruption before exit");
    host.restoreAll();
  }
  check(read(path) == malformed, "corrupt journal survives failed recovery and destruction");
  const QByteArray oversized(1024 * 1024 + 1, 'x');
  write(path, oversized);
  {
    NativeHost host(path);
    check(!host.recover(), "oversized recovery journal rejected before parse");
  }
  check(read(path) == oversized, "oversized journal remains available for manual recovery");
  write(path, R"({"version":42,"windows":[]})");
  {
    NativeHost host(path);
    check(!host.recover(), "unknown journal version is never silently discarded");
  }
  check(read(path) == QByteArray(R"({"version":42,"windows":[]})"), "unknown journal schema preserved");
  write(path, R"({"version":1,"windows":[]})");
  {
    NativeHost host(path);
    check(host.recover(), "empty journal recovers without OS permissions");
    check(host.recover(), "successful synchronous recovery is idempotent");
    using namespace deskflow::windowshare;
    int opened = 0, errors = 0;
    host.onOpened = [&](auto, auto, const auto &) { ++opened; };
    host.onError = [&](const auto &) { ++errors; };
    check(!host.prepareWindowDrag({}), "Incomplete drag cannot start native capture");
    const WindowDragBinding unseen{7, 9, 11, 3, 211, 1000, 10383, 516};
    check(!host.prepareWindowDrag(unseen), "An unobserved titlebar cannot be exported by a field tuple");
    check(!host.releaseWindowDrag(unseen, {3, 425, 4000, 10383, 516}), "An unowned release cannot commit capture");
    host.cancelWindowDrag(unseen);
    host.observeWindowPointer({});
    auto malformed = LocalWindowPointer{LocalWindowPointer::Kind::Down, 3, 211, 1000, 0, 10383, 0, 516, 0, 0};
    host.observeWindowPointer(malformed);
    malformed.clicks = 1;
    malformed.x = std::numeric_limits<double>::quiet_NaN();
    host.observeWindowPointer(malformed);
    check(opened == 0 && errors == 0, "Rejected drag observations reached capture or native mutation");
    QString diagnostic;
    check(
        !native::NativeTitlebarObservation::begin(0, {0, 0}, &diagnostic) && !diagnostic.isEmpty(),
        "Missing native window cannot create a titlebar observer"
    );
    check(
        !native::NativeTitlebarObservation::begin(10383, {malformed.x, 0}),
        "Non-finite press cannot query a native titlebar"
    );
  }
  const auto document = QJsonDocument::fromJson(read(path));
  check(
      document.isObject() && document["version"].toInt() == 1 && document["windows"].isArray(),
      "successful recovery leaves valid journal"
  );
  const auto mode = QFile::permissions(path);
  check(
      !(mode & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther)),
      "journal excludes group and other readers"
  );
  {
    const auto boundedPath = directory.filePath("bounded.json");
    const QString fullTitle(700, QChar(u'\u20ac'));
    QJsonObject record{
        {"windowId", 123},      {"pid", 456},
        {"launch", 1234567890}, {"bundle", "org.deskflow.test"},
        {"title", fullTitle},   {"original", QJsonObject{{"x", 50}, {"y", 50}, {"width", 640}, {"height", 480}}},
        {"session", 123456},    {"epoch", 1}
    };
    QString diagnostic;
    check(
        deskflow::windowshare::native::persistJournal(boundedPath, {record}, &diagnostic),
        "persist complete long native identity"
    );
    check(
        QJsonDocument::fromJson(read(boundedPath))["windows"].toArray().at(0).toObject() == record,
        "journal preserves the complete title and native identity"
    );
    record["title"] = QString();
    const auto overhead = QJsonDocument(QJsonObject{{"version", 1}, {"windows", QJsonArray{record}}})
                              .toJson(QJsonDocument::Compact)
                              .size();
    record["title"] = QString(1024 * 1024 - overhead, QChar(u'x'));
    check(
        deskflow::windowshare::native::persistJournal(boundedPath, {record}, &diagnostic),
        "journal accepts the exact recovery byte limit"
    );
    const auto atLimit = read(boundedPath);
    check(atLimit.size() == 1024 * 1024, "boundary journal occupies exactly the supported bytes");
    record["title"] = record["title"].toString() + QChar(u'x');
    check(
        !deskflow::windowshare::native::persistJournal(boundedPath, {record}, &diagnostic),
        "journal rejects one byte over the recovery limit"
    );
    check(read(boundedPath) == atLimit, "oversized replacement leaves the prior recovery journal intact");
    record["title"] = QString(512 * 1024, QChar(u'\u20ac'));
    check(
        !deskflow::windowshare::native::persistJournal(boundedPath, {record}, &diagnostic),
        "journal limit measures serialized UTF-8 bytes, not characters"
    );
    check(read(boundedPath) == atLimit, "UTF-8 overflow preserves the existing journal");
    record["title"] = QString(512 * 1024, QChar(u'"'));
    check(
        !deskflow::windowshare::native::persistJournal(boundedPath, {record}, &diagnostic),
        "journal limit accounts for JSON escaping"
    );
    check(read(boundedPath) == atLimit, "escaped overflow preserves the existing journal");
  }
  if (geteuid() != 0) {
    QTemporaryDir restricted;
    check(restricted.isValid(), "restricted journal directory");
    const auto restrictedPath = restricted.filePath("restore.json");
    const QByteArray empty(R"({"version":1,"windows":[]})");
    write(restrictedPath, empty);
    check(
        QFile::setPermissions(restricted.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner),
        "restrict journal directory writes"
    );
    {
      NativeHost host(restrictedPath);
      check(!host.recover(), "recovery fails when the completed journal cannot be persisted");
      check(read(restrictedPath) == empty, "failed durable rewrite preserves the prior journal");
      check(
          QFile::setPermissions(
              restricted.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
          ),
          "restore journal directory writes"
      );
      check(host.recover(), "failed durable recovery can be retried on the same host");
    }
  }
  const auto permissions = NativeHost::permissions();
  check(
      permissions["screenCapture"].isBool() && permissions["accessibility"].isBool(),
      "permission probe is explicit and does not request permission"
  );
  QTextStream(stdout) << (failures ? "FAIL" : "PASS")
                      << ": native recovery journal and forwarding admission regressions\n";
  return failures ? 1 : 0;
}
