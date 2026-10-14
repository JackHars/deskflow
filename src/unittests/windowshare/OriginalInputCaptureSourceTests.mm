/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "base/Event.h"
#include "platform/OSXEventTapRunLoop.h"
#include "platform/OriginalInputCaptureSource_mac.h"
#include "windowshare/PlatformMarkers.h"

#include <IOKit/hidsystem/IOLLEvent.h>
#include <barrier>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace deskflow::windowshare;
using namespace std::chrono_literals;

namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

struct EventFixture
{
  CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStatePrivate);
  CGEventRef event = source ? CGEventCreate(source) : nullptr;
  EventFixture()
  {
    check(event, "private constructed fixture");
    CGEventSetFlags(event, 0);
    CGEventSetTimestamp(event, 100);
    CGEventSetType(event, kCGEventMouseMoved);
    CGEventSetIntegerValueField(event, kCGMouseEventSubtype, kCGEventMouseSubtypeDefault);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, -7);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, 12);
  }
  ~EventFixture()
  {
    if (event)
      CFRelease(event);
    if (source)
      CFRelease(source);
  }
  void key(std::uint16_t code, std::uint64_t flags = 0)
  {
    CGEventSetType(event, kCGEventKeyDown);
    CGEventSetIntegerValueField(event, kCGKeyboardEventKeycode, code);
    CGEventSetIntegerValueField(event, kCGKeyboardEventKeyboardType, 41);
    CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, 0);
    CGEventSetFlags(event, flags);
  }
  void button(std::uint8_t button, bool down)
  {
    CGEventSetFlags(event, 0);
    CGEventSetType(
        event, button == 0   ? (down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp)
               : button == 1 ? (down ? kCGEventRightMouseDown : kCGEventRightMouseUp)
                             : (down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp)
    );
    CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, button);
    CGEventSetIntegerValueField(event, kCGMouseEventClickState, 1);
    CGEventSetIntegerValueField(event, kCGMouseEventSubtype, kCGEventMouseSubtypeDefault);
  }
  void scroll(
      OriginalScrollPhase phase, OriginalMomentumPhase momentum = OriginalMomentumPhase::None, bool continuous = true
  )
  {
    CGEventSetType(event, kCGEventScrollWheel);
    CGEventSetFlags(event, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventIsContinuous, continuous);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventScrollPhase, static_cast<unsigned>(phase));
    CGEventSetIntegerValueField(event, kCGScrollWheelEventMomentumPhase, static_cast<unsigned>(momentum));
    CGEventSetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1, 1);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventDeltaAxis3, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1, 3);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis3, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis1, 0x10000);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis2, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis3, 0);
  }
};

OriginalInputCaptured captureCompleted(
    OriginalInputCaptureSource &source, EventFixture &event, std::uint16_t parts = 1, bool uncertain = false
)
{
  const auto stamp = source.beginCapture(CGEventGetType(event.event), event.event);
  check(bool(stamp) && !source.popCaptured(), "transition waits for existing translation completion");
  source.completeCapture({stamp, parts, uncertain});
  auto captured = source.popCaptured();
  check(bool(captured), "completed transition delivered");
  check(
      captured->translation.parts == parts && captured->translation.uncertain == uncertain,
      "transition classifier preserves translation disposition"
  );
  return *captured;
}

bool acquires(const OriginalInputCaptured &captured)
{
  return captured.transition.permitsAcquisition(captured.input);
}

void transitionKeyOwnership()
{
  OriginalInputCaptureSource source;
  EventFixture event;
  check(source.start(), "key transition source start");
  event.key(0);
  check(!acquires(captureCompleted(source, event)), "first observed down has unknown prior ownership");
  CGEventSetIntegerValueField(event.event, kCGKeyboardEventAutorepeat, 1);
  check(!acquires(captureCompleted(source, event)), "repeat cannot acquire an unknown key");

  event.key(0);
  CGEventSetType(event.event, kCGEventKeyUp);
  const auto release = captureCompleted(source, event, 0);
  check(!acquires(release), "zero-translation physical up establishes no acquisition");
  event.key(0);
  const auto down = captureCompleted(source, event);
  check(
      acquires(down) && down.transition.kind() == OriginalInputTransitionEvidence::Kind::KeyDown &&
          down.transition.precedingReleaseSequence() == release.input.record.sequence,
      "captured up establishes only the next fresh logical down"
  );
  check(!acquires(captureCompleted(source, event)), "duplicate nonrepeat down cannot acquire again");
  CGEventSetIntegerValueField(event.event, kCGKeyboardEventAutorepeat, 1);
  check(!acquires(captureCompleted(source, event)), "owned repeat still requires exact existing owner");
  event.key(0);
  CGEventSetType(event.event, kCGEventKeyUp);
  captureCompleted(source, event, 2, true);
  event.key(0);
  const auto afterExpandedUp = captureCompleted(source, event, 0);
  check(acquires(afterExpandedUp), "raw release survives uncertain expansion before the next untranslated down");

  const OriginalInputCaptured fixtureDefault{down.input, down.translation};
  check(!acquires(fixtureDefault), "aggregate fixture defaults cannot mint source transition evidence");
  auto changed = down.input;
  ++changed.record.sequence;
  check(!down.transition.permitsAcquisition(changed), "transition cannot move to another capture sequence");
  changed = down.input;
  ++changed.record.sourceTimestampNs;
  check(!down.transition.permitsAcquisition(changed), "transition cannot move to another source time");
  changed = down.input;
  changed.record.flags = originalKey::capsLockFlag;
  check(!down.transition.permitsAcquisition(changed), "transition cannot replace captured flags");
  changed = down.input;
  std::get<OriginalInputKey>(changed.record.payload).code = 1;
  check(!down.transition.permitsAcquisition(changed), "transition cannot acquire another key");
  changed = down.input;
  ++std::get<OriginalInputKey>(changed.record.payload).keyboardType;
  check(!down.transition.permitsAcquisition(changed), "transition retains captured keyboard type");
  OriginalInputCaptureSource other;
  check(other.start(), "other capture epoch start");
  changed = down.input;
  changed.epoch = other.epoch();
  changed.record.epoch = changed.epoch.id();
  check(!down.transition.permitsAcquisition(changed), "transition cannot move to another live epoch");

  const auto inbox = source.completedInput();
  check(inbox->bind(17) && inbox->publish(afterExpandedUp), "existing Session copy path accepts capture evidence");
  const auto copied = inbox->pop();
  check(copied && acquires(*copied), "existing Server inbox preserves exact source evidence");
  source.stop();
  check(!acquires(down) && !acquires(*copied), "stop revokes all retained and copied source evidence");
}

void transitionModifiersAndButtons()
{
  OriginalInputCaptureSource source;
  EventFixture event;
  check(source.start(), "modifier transition source start");
  const auto right = originalKey::shiftFlag | originalKey::rightShiftFlag;
  event.key(originalKey::leftShift, right | originalKey::leftShiftFlag);
  CGEventSetType(event.event, kCGEventFlagsChanged);
  check(!acquires(captureCompleted(source, event)), "first side-flag down has unknown prior ownership");
  CGEventSetFlags(event.event, right);
  captureCompleted(source, event);
  CGEventSetFlags(event.event, right | originalKey::leftShiftFlag);
  const auto left = captureCompleted(source, event);
  check(
      acquires(left) && left.input.record.flags == (right | originalKey::leftShiftFlag),
      "left release/down qualifies only left while preserving independently checked right flags"
  );
  event.key(originalKey::rightShift, right | originalKey::leftShiftFlag);
  CGEventSetType(event.event, kCGEventFlagsChanged);
  check(!acquires(captureCompleted(source, event)), "another side is not released by aggregate or opposite up");
  event.key(originalKey::capsLock, originalKey::capsLockFlag);
  CGEventSetType(event.event, kCGEventFlagsChanged);
  check(!acquires(captureCompleted(source, event)), "ambiguous Caps toggle cannot acquire a physical press");
  CGEventSetFlags(event.event, 0);
  check(!acquires(captureCompleted(source, event)), "opposite Caps toggle is still not a physical up");

  for (const std::uint8_t button : {0, 31}) {
    event.button(button, true);
    check(!acquires(captureCompleted(source, event)), "unknown button down cannot acquire");
    event.button(button, false);
    const auto release = captureCompleted(source, event, 0);
    event.button(button, true);
    const auto down = captureCompleted(source, event);
    check(
        acquires(down) && down.transition.kind() == OriginalInputTransitionEvidence::Kind::ButtonDown &&
            down.transition.precedingReleaseSequence() == release.input.record.sequence,
        "button zero and highest supported button need their own captured release"
    );
    check(!acquires(captureCompleted(source, event)), "duplicate button down cannot acquire");
    CGEventSetType(event.event, button == 0 ? kCGEventLeftMouseDragged : kCGEventOtherMouseDragged);
    check(!acquires(captureCompleted(source, event)), "drag must resolve an exact existing button owner");
  }
  CGEventSetType(event.event, kCGEventMouseMoved);
  check(!acquires(captureCompleted(source, event)), "ordinary cursor motion is not acquisition evidence");
  check(source.checkpoint(101, 0), "logical checkpoint captured");
  const auto checkpoint = source.popCaptured();
  check(checkpoint && !acquires(*checkpoint), "checkpoint cannot mint transition or physical-neutral evidence");
}

void touchDragZeroClickRelease()
{
  OriginalInputCaptureSource source;
  EventFixture event;
  check(source.start(), "touch drag capture starts");
  const auto epoch = source.epoch();
  // The observed physical sequence is a positive-count down, mouse drag, then
  // a zero-count up. The up must survive both capture and completion unchanged.
  event.button(0, true);
  CGEventSetIntegerValueField(event.event, kCGMouseEventSubtype, NX_SUBTYPE_MOUSE_TOUCH);
  const auto down = captureCompleted(source, event);
  CGEventSetTimestamp(event.event, 101);
  CGEventSetType(event.event, kCGEventLeftMouseDragged);
  captureCompleted(source, event);
  event.button(0, false);
  CGEventSetTimestamp(event.event, 102);
  CGEventSetIntegerValueField(event.event, kCGMouseEventSubtype, NX_SUBTYPE_MOUSE_TOUCH);
  CGEventSetIntegerValueField(event.event, kCGMouseEventClickState, 0);
  const auto up = captureCompleted(source, event);
  check(
      epoch.live() && up.input.live() && up.pointer && up.pointer->matches(up.input.record) &&
          up.pointer->mouseSubtype == NX_SUBTYPE_MOUSE_TOUCH &&
          std::get<OriginalInputButton>(up.input.record.payload).clickCount == 0 &&
          up.input.record.sequence == down.input.record.sequence + 2,
      "zero-click trackpad release preserves capture continuity and exact local metadata"
  );
  event.button(0, true);
  CGEventSetTimestamp(event.event, 103);
  const auto next = captureCompleted(source, event);
  check(
      acquires(next) && next.transition.precedingReleaseSequence() == up.input.record.sequence,
      "zero-click release establishes only its own next fresh button down"
  );
}

void transitionScrollOwnership()
{
  for (const auto initial : {OriginalScrollPhase::Changed, OriginalScrollPhase::None}) {
    OriginalInputCaptureSource source;
    EventFixture event;
    check(source.start(), "unknown scroll transition source start");
    event.scroll(
        initial, initial == OriginalScrollPhase::None ? OriginalMomentumPhase::Continued : OriginalMomentumPhase::None
    );
    check(!acquires(captureCompleted(source, event)), "unknown gesture or momentum cannot acquire");
    event.scroll(OriginalScrollPhase::Began);
    check(!acquires(captureCompleted(source, event)), "fresh marker cannot erase an unresolved gesture or momentum");
    event.scroll(OriginalScrollPhase::Cancelled);
    check(!acquires(captureCompleted(source, event)), "cancellation only ends logical sequence");
    event.scroll(OriginalScrollPhase::Began);
    check(acquires(captureCompleted(source, event)), "explicitly ended sequence permits the next Began");
  }
  OriginalInputCaptureSource source;
  EventFixture event;
  check(source.start(), "fresh scroll transition source start");
  event.scroll(OriginalScrollPhase::MayBegin);
  check(!acquires(captureCompleted(source, event)), "MayBegin cannot acquire");
  event.scroll(OriginalScrollPhase::Began);
  const auto begin = captureCompleted(source, event);
  check(
      acquires(begin) && begin.transition.kind() == OriginalInputTransitionEvidence::Kind::ScrollBegin,
      "explicit first Began names a fresh logical gesture"
  );
  check(!acquires(captureCompleted(source, event)), "duplicate Began cannot acquire");
  event.scroll(OriginalScrollPhase::Ended);
  check(!acquires(captureCompleted(source, event)), "gesture end reserves possibility of momentum");
  event.scroll(OriginalScrollPhase::Began);
  check(!acquires(captureCompleted(source, event)), "unresolved momentum possibility cannot be silently replaced");
  event.scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Began);
  check(!acquires(captureCompleted(source, event)), "momentum continuation needs original gesture owner");
  event.scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Ended);
  check(!acquires(captureCompleted(source, event)), "momentum end only closes sequence");
  event.scroll(OriginalScrollPhase::Began);
  check(acquires(captureCompleted(source, event)), "completed momentum allows a new logical gesture");
  event.scroll(OriginalScrollPhase::None, OriginalMomentumPhase::None, false);
  const auto discrete = captureCompleted(source, event);
  check(
      acquires(discrete) && discrete.transition.kind() == OriginalInputTransitionEvidence::Kind::DiscreteScroll,
      "discrete wheel record has its own acquisition boundary"
  );
  source.capture(kCGEventTapDisabledByTimeout, nullptr);
  check(!acquires(begin) && !acquires(discrete), "tap discontinuity revokes retained scroll evidence");
}

void lifecycleAndOrdering()
{
  EventFixture fixture;
  OriginalInputEpoch retained;
  {
    OriginalInputCaptureSource source;
    check(!source.epoch().live() && !source.epoch().id(), "no token before producer starts");
    source.capture(kCGEventMouseMoved, fixture.event);
    check(!source.pop(), "no pre-start delivery");
    check(source.start() && !source.start(), "single-use start");
    retained = source.epoch();
    check(retained.live(), "started continuity token");

    EventFixture injected;
    CGEventSetIntegerValueField(injected.event, kCGEventSourceUserData, syntheticEventTag);
    source.capture(kCGEventMouseMoved, injected.event);
    source.capture(kCGEventNull, nullptr);
    source.capture(kCGEventMouseMoved, fixture.event);
    fixture.key(0);
    source.capture(kCGEventKeyDown, fixture.event);
    check(source.checkpoint(101, 0), "ordered producer checkpoint");

    OriginalInputNeutralBarrier barrier(retained);
    for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
      const auto input = source.pop();
      check(input && input->record.sequence == sequence && input->epoch.sameEpoch(retained), "one mixed sequence");
      check(barrier.observe(*input), "all records observed outside route");
      if (sequence == 1) {
        check(std::holds_alternative<OriginalInputMotion>(input->record.payload), "first capture must be motion");
        check(std::get<OriginalInputMotion>(input->record.payload).dx == -7, "untranslated motion retained");
      } else if (sequence == 2)
        check(std::holds_alternative<OriginalInputKey>(input->record.payload), "key follows motion");
    }
    check(!source.pop(), "ignored events consume no queue slots or sequence");
    check(
        barrier.state() != OriginalInputNeutralBarrier::State::Neutral, "checkpoint does not invent physical neutrality"
    );
  }
  check(
      !retained.live() && retained.reason() == OriginalInputRevocation::Stopped, "destruction revokes retained token"
  );

  OriginalInputCaptureSource stopped;
  stopped.stop();
  check(!stopped.start() && !stopped.epoch().live(), "stop before start is terminal");
}

void translationCompletion()
{
  EventFixture event;
  OriginalInputCaptureSource source;
  check(source.start(), "translation source start");
  const auto stamp = source.beginCapture(kCGEventMouseMoved, event.event);
  check(bool(stamp) && !source.popCaptured(), "raw capture waits for translation completion");
  OriginalInputStampScope scope(stamp);
  Event translated(deskflow::EventTypes::PrimaryComputerMotionOnSecondary);
  source.completeCapture(scope.translation());
  const auto captured = source.popCaptured();
  check(
      captured && captured->translation.parts == 1 && captured->translation.stamp == stamp &&
          translated.originalInputStamp() == OriginalInputStamp{stamp.epoch, stamp.sequence, 1},
      "same source epoch/sequence joins the actual Event"
  );
  check(
      std::get<OriginalInputMotion>(captured->input.record.payload).dx == -7, "translation cannot replace source deltas"
  );
  const auto unfinished = source.beginCapture(kCGEventMouseMoved, event.event);
  check(bool(unfinished), "next capture start");
  source.beginCapture(kCGEventMouseMoved, event.event);
  check(!captured->input.live(), "missing completion loses continuity rather than mixing captures");
}

void discontinuitiesAndEmergency()
{
  for (const auto disabled : {kCGEventTapDisabledByTimeout, kCGEventTapDisabledByUserInput}) {
    OriginalInputCaptureSource source;
    check(source.start(), "disabled fixture started");
    const auto token = source.epoch();
    source.capture(disabled, nullptr);
    check(
        !token.live() && token.reason() == OriginalInputRevocation::Discontinuity,
        "tap disable revokes even with no event"
    );
    check(!source.start() && !source.epoch().live(), "disabled epoch cannot restart");
  }
  EventFixture fixture;
  OriginalInputCaptureSource malformed;
  check(malformed.start(), "malformed fixture started");
  const auto malformedToken = malformed.epoch();
  malformed.capture(kCGEventKeyDown, fixture.event);
  check(!malformedToken.live() && !malformed.pop(), "callback/event mismatch revokes");

  OriginalInputCaptureSource overflow;
  check(overflow.start(), "overflow fixture started");
  const auto token = overflow.epoch();
  for (std::size_t i = 0; i <= OriginalInputStream::capacity; ++i)
    overflow.capture(kCGEventMouseMoved, fixture.event);
  check(
      !token.live() && token.reason() == OriginalInputRevocation::Overflow && !overflow.pop(),
      "overflow drops entire epoch"
  );
  fixture.key(originalKey::escape, originalKey::controlFlag | originalKey::optionFlag | originalKey::shiftFlag);
  overflow.capture(kCGEventKeyDown, fixture.event);
  check(
      token.emergencyRequested() && overflow.emergencyRequested(), "emergency survives overflow and ambiguous side bits"
  );
  check(token.reason() == OriginalInputRevocation::Overflow, "original failure reason retained");

  OriginalInputCaptureSource synthetic;
  check(synthetic.start(), "synthetic fixture started");
  CGEventSetIntegerValueField(fixture.event, kCGEventSourceUserData, syntheticEventTag);
  synthetic.capture(kCGEventKeyDown, fixture.event);
  check(synthetic.epoch().live() && !synthetic.emergencyRequested(), "tagged emergency does not recirculate");
  EventFixture physical;
  physical.key(originalKey::escape, originalKey::controlFlag | originalKey::optionFlag | originalKey::shiftFlag);
  synthetic.capture(kCGEventKeyDown, physical.event);
  check(
      !synthetic.epoch().live() && synthetic.emergencyRequested() && !synthetic.pop(),
      "emergency is out-of-band and terminal"
  );
}

void emergencyRecognition()
{
  constexpr auto chord = originalKey::controlFlag | originalKey::optionFlag | originalKey::shiftFlag;
  // These events now decide whether a running companion is terminated. A
  // release, repeat or incomplete chord must never be mistaken for the escape
  // press, even if its record invalidates ordinary capture continuity.
  struct Lookalike
  {
    CGEventType callbackType;
    CGEventType eventType;
    std::uint16_t code;
    std::uint64_t flags;
    bool repeat;
  };
  for (const auto &lookalike : {
           Lookalike{kCGEventKeyUp, kCGEventKeyUp, originalKey::escape, chord, false},
           Lookalike{kCGEventKeyDown, kCGEventKeyDown, originalKey::escape, chord, true},
           Lookalike{kCGEventKeyDown, kCGEventKeyDown, originalKey::escape, chord & ~originalKey::controlFlag, false},
           Lookalike{kCGEventKeyDown, kCGEventKeyDown, originalKey::escape, chord & ~originalKey::optionFlag, false},
           Lookalike{kCGEventKeyDown, kCGEventKeyDown, originalKey::escape, chord & ~originalKey::shiftFlag, false},
           Lookalike{kCGEventKeyDown, kCGEventKeyDown, 0, chord, false},
           Lookalike{kCGEventKeyUp, kCGEventKeyDown, originalKey::escape, chord, false},
       }) {
    OriginalInputCaptureSource source;
    check(source.start(), "lookalike fixture started");
    EventFixture event;
    event.key(lookalike.code, lookalike.flags);
    CGEventSetType(event.event, lookalike.eventType);
    CGEventSetIntegerValueField(event.event, kCGKeyboardEventAutorepeat, lookalike.repeat);
    source.capture(lookalike.callbackType, event.event);
    check(!source.emergencyRequested(), "lookalike must not terminate companion");
    event.key(originalKey::escape, chord);
    source.capture(kCGEventKeyDown, event.event);
    check(source.emergencyRequested(), "later genuine escape survives any continuity failure");
  }

  OriginalInputCaptureSource source;
  EventFixture event;
  event.key(originalKey::escape, chord);
  source.capture(kCGEventKeyDown, event.event);
  check(!source.emergencyRequested() && source.start(), "pre-start escape cannot stop a new companion");
  source.stop();
  source.capture(kCGEventKeyDown, event.event);
  check(!source.emergencyRequested(), "stopped source observes no further physical presses");
}

void concurrentStartStop()
{
  for (int iteration = 0; iteration < 200; ++iteration) {
    OriginalInputCaptureSource source;
    std::barrier gate(3);
    OriginalInputEpoch retained;
    std::thread producer([&] {
      gate.arrive_and_wait();
      source.start();
      retained = source.epoch();
    });
    std::thread owner([&] {
      gate.arrive_and_wait();
      source.stop();
    });
    gate.arrive_and_wait();
    producer.join();
    owner.join();
    check(!retained.live() && !source.epoch().live() && !source.start(), "start/stop race cannot revive epoch");
  }
}

void concurrentCaptureDrain()
{
  OriginalInputCaptureSource source;
  std::atomic<std::uint64_t> consumed{0};
  std::atomic<bool> failed{false};
  std::promise<OriginalInputEpoch> ready;
  auto readyToken = ready.get_future();
  std::thread producer([&] {
    EventFixture fixture;
    source.start();
    ready.set_value(source.epoch());
    for (std::uint64_t i = 1; i <= 2000; ++i) {
      // Test coordination is outside capture(); the real producer never waits.
      while (i - consumed.load(std::memory_order_acquire) > 32 && !failed.load())
        std::this_thread::yield();
      if (failed.load())
        break;
      CGEventSetTimestamp(fixture.event, i);
      source.capture(kCGEventMouseMoved, fixture.event);
    }
  });
  const auto token = readyToken.get();
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  std::uint64_t expected = 1;
  while (expected <= 2000 && token.live() && std::chrono::steady_clock::now() < deadline) {
    if (const auto input = source.pop()) {
      if (input->record.sequence != expected || input->record.sourceTimestampNs != expected || !input->live()) {
        failed.store(true);
        break;
      }
      consumed.store(expected++, std::memory_order_release);
    } else {
      std::this_thread::yield();
    }
  }
  if (expected != 2001)
    failed.store(true);
  producer.join();
  check(!failed.load(), "single producer/consumer preserves complete mixed stream order");
  source.stop();
  check(!token.live(), "owner stop invalidates consumer token");
}

void stopBeforeRunStarts()
{
  // Synthetic CoreFoundation loop only: no CG tap, event posting or OS input.
  std::promise<CFRunLoopRef> published;
  auto loopFuture = published.get_future();
  std::promise<void> allowed, finished;
  auto allowedFuture = allowed.get_future();
  auto finishedFuture = finished.get_future();
  std::thread worker([&] {
    const auto loop = CFRunLoopGetCurrent();
    CFRetain(loop);
    CFRunLoopSourceContext context{};
    context.perform = [](void *) {};
    const auto source = CFRunLoopSourceCreate(nullptr, 0, &context);
    CFRunLoopAddSource(loop, source, kCFRunLoopDefaultMode);
    published.set_value(loop);
    allowedFuture.wait();
    CFRunLoopRun();
    CFRunLoopRemoveSource(loop, source, kCFRunLoopDefaultMode);
    CFRelease(source);
    finished.set_value();
  });
  const auto loop = loopFuture.get();
  stopOSXEventTapRunLoop(loop);
  allowed.set_value();
  const bool timely = finishedFuture.wait_for(2s) == std::future_status::ready;
  if (!timely) {
    CFRunLoopStop(loop); // Release a failing fixture before reporting the bug.
    CFRunLoopWakeUp(loop);
  }
  worker.join();
  CFRelease(loop);
  check(timely, "queued stop survives publication-before-run race");
}

void pointerObservationLifetime()
{
  OriginalInputCaptureSource source;
  EventFixture event;
  check(source.start(), "pointer observation capture starts");
  event.button(0, true);
  CGEventSetLocation(event.event, {-22.5, 300.25});
  CGEventSetIntegerValueField(event.event, kCGMouseEventSubtype, NX_SUBTYPE_MOUSE_TOUCH);
  CGEventSetIntegerValueField(event.event, kCGMouseEventWindowUnderMousePointer, 71);
  CGEventSetIntegerValueField(event.event, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, 81);
  const auto stamp = source.beginCapture(kCGEventLeftMouseDown, event.event);
  check(bool(stamp) && !source.popCaptured(), "metadata waits for the same legacy translation completion");
  CGEventSetLocation(event.event, {999, 1000});
  CGEventSetIntegerValueField(event.event, kCGMouseEventWindowUnderMousePointer, 99);
  CGEventSetIntegerValueField(event.event, kCGMouseEventSubtype, kCGEventMouseSubtypeDefault);
  source.completeCapture({stamp, 1, false});
  const auto captured = source.popCaptured();
  check(
      captured && captured->pointer && captured->pointer->matches(captured->input.record) &&
          captured->pointer->x == -22.5 && captured->pointer->y == 300.25 &&
          captured->pointer->windowUnderPointer == 71 && captured->pointer->windowHandlingPointer == 81 &&
          captured->pointer->mouseSubtype == NX_SUBTYPE_MOUSE_TOUCH,
      "completed capture retains event-time values rather than reading a mutated Quartz event"
  );
  const auto inbox = source.completedInput();
  check(inbox->bind(71) && inbox->publish(*captured), "the existing Session/Server conduit carries the observation");
  const auto copied = inbox->pop();
  check(
      copied && copied->pointer && copied->pointer->matches(copied->input.record) &&
          copied->pointer->windowUnderPointer == 71 && copied->pointer->x == -22.5 &&
          copied->pointer->mouseSubtype == NX_SUBTYPE_MOUSE_TOUCH,
      "one-consumer copy path preserves pointer metadata"
  );
  check(source.checkpoint(101, 0), "checkpoint follows pointer input");
  const auto checkpoint = source.popCaptured();
  check(checkpoint && !checkpoint->pointer, "a checkpoint cannot inherit the preceding pointer observation");
  check(inbox->publish(*checkpoint) && bool(inbox->pop()), "checkpoint preserves the copy conduit's sequence");
  event.key(0);
  CGEventSetTimestamp(event.event, 102);
  const auto key = captureCompleted(source, event);
  check(!key.pointer, "a keyboard event clears the pointer observation");
  check(inbox->publish(key) && bool(inbox->pop()), "key preserves the copy conduit's sequence");
  event.button(0, false);
  CGEventSetTimestamp(event.event, 103);
  auto mismatched = captureCompleted(source, event);
  check(bool(mismatched.pointer), "release has its own metadata");
  --mismatched.pointer->sequence;
  check(!inbox->publish(mismatched) && !inbox->live(), "a mismatched observation revokes the copy conduit");
  check(source.epoch().live(), "invalid copy metadata does not stop the sole raw consumer or emergency capture");
  source.stop();
  check(!copied->input.live(), "a retained pointer observation never outlives its input epoch");
}
} // namespace

int main()
{
  try {
    lifecycleAndOrdering();
    translationCompletion();
    transitionKeyOwnership();
    transitionModifiersAndButtons();
    touchDragZeroClickRelease();
    transitionScrollOwnership();
    discontinuitiesAndEmergency();
    emergencyRecognition();
    concurrentStartStop();
    concurrentCaptureDrain();
    stopBeforeRunStarts();
    pointerObservationLifetime();
    std::cout << "Original input capture lifecycle tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
