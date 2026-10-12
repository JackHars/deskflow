/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "platform/OSXEventTapRunLoop.h"
#include "platform/OriginalInputCaptureSource_mac.h"
#include "windowshare/PlatformMarkers.h"

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
};

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
} // namespace

int main()
{
  try {
    lifecycleAndOrdering();
    discontinuitiesAndEmergency();
    emergencyRecognition();
    concurrentStartStop();
    concurrentCaptureDrain();
    stopBeforeRunStarts();
    std::cout << "Original input capture lifecycle tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
