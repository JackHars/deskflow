/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "HoldingRelocation.h"
#include "NativePlacementVerification.h"
#if defined(__APPLE__)
#include "NativePlacementNotifications_mac.h"
#include "NativeWindowState_mac.h"
#endif

#include <QCoreApplication>
#include <QEvent>
#include <QScopeGuard>
#include <cstdio>
#include <deque>
#include <memory>
#include <stdexcept>

using namespace deskflow::windowshare;
using namespace std::chrono_literals;
using Gate = NativePlacementVerification;

namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
void dispatch()
{
  QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
}
struct Fixture
{
  Gate::Clock::time_point now{};
  Gate::Observation observation = Gate::Observation::Pending;
  Gate::Clock::duration probeDuration{};
  int probes = 0, completions = 0;
  bool success = false;
  QString failure;
  Gate gate{
      [&](QString *reason) {
        ++probes;
        now += probeDuration;
        *reason = "native readback";
        return observation;
      },
      [&](bool accepted, QString reason) {
        ++completions;
        success = accepted;
        failure = std::move(reason);
      },
      [&] { return now; }
  };
};

void transientReadback()
{
  Fixture f;
  f.gate.changed();
  dispatch();
  check(f.probes == 0, "notification before a native write cannot verify placement");
  f.gate.issued();
  for (int i = 0; i < 1000; ++i)
    f.gate.changed();
  check(f.probes == 0, "verification must yield out of the native write callback");
  dispatch();
  check(f.probes == 1 && f.completions == 0, "queued notifications coalesce and a busy AX bridge remains pending");
  dispatch();
  check(f.probes == 1, "pending readback does not spin or manufacture more observations");
  f.now += 1500ms;
  f.observation = Gate::Observation::Verified;
  f.gate.changed();
  dispatch();
  check(f.success && f.completions == 1 && f.probes == 2, "fresh native readback can complete after a transient error");
  f.gate.changed();
  f.gate.issued();
  f.gate.fail("late failure");
  f.now += 1h;
  f.gate.expire();
  dispatch();
  check(f.completions == 1 && f.probes == 2, "late events cannot reopen or repeat a committed placement");
}

void absoluteDeadline()
{
  for (int stage = 0; stage < 3; ++stage) {
    Fixture f;
    f.observation = Gate::Observation::Verified;
    if (stage == 0)
      f.now += 2s; // Observer registration/native write took the whole budget.
    f.gate.issued();
    if (stage == 1)
      f.now += 2s; // A delayed check runs before the overdue timer callback.
    if (stage == 2) {
      f.now += 1999ms;
      f.probeDuration = 1ms;
    }
    dispatch();
    check(!f.success && f.completions == 1 && !f.gate.pending(), "late native verification cannot win the timer race");
    check(f.failure.contains("timed out"), "deadline failure has a useful diagnostic");
    check(f.probes == (stage == 2 ? 1 : 0), "expired placement never starts a native read");
  }
  Fixture f;
  f.gate.issued();
  dispatch();
  f.now += 1999ms;
  f.gate.expire();
  f.gate.issued();
  f.gate.changed();
  dispatch();
  check(f.completions == 0, "early timer notification does not fail before its cutoff");
  f.now += 1ms;
  f.gate.expire();
  check(!f.success && f.completions == 1, "duplicate events and early timers never renew the deadline");
}

void cancellationAndDestruction()
{
  {
    Fixture f;
    f.gate.issued();
    f.gate.cancel();
    f.now += 1h;
    dispatch();
    f.gate.expire();
    check(f.completions == 0 && f.probes == 0, "restoration cancels queued placement work");
  }
  {
    Fixture f;
    f.gate.issued();
    f.observation = Gate::Observation::Invalid;
    dispatch();
    check(!f.success && f.completions == 1, "authority loss fails immediately instead of waiting for timeout");
  }
  for (bool destroyInProbe : {false, true}) {
    int completions = 0;
    std::unique_ptr<Gate> gate;
    gate = std::make_unique<Gate>(
        [&](QString *) {
          if (destroyInProbe)
            gate.reset();
          return Gate::Observation::Verified;
        },
        [&](bool success, QString) {
          check(success, "destructive completion still receives verified status");
          ++completions;
          gate.reset();
        }
    );
    gate->issued();
    dispatch();
    check(!gate && completions == (destroyInProbe ? 0 : 1), "owner destruction during callbacks is safe");
  }
}

void preparationYields()
{
  Fixture f;
  int steps = 0, unrelated = 0;
  Gate::Preparation actions;
  for (int i = 0; i < 9; ++i) {
    actions.emplace_back([&, i] {
      check(steps++ == i, "preparation preserves native operation order");
      f.now += 100ms; // Busy native IPC, without sleeping or contacting another app.
      for (int n = 0; n < 20; ++n)
        f.gate.changed();
      dispatch(); // Native reentrancy cannot start the next step or readback.
      check(steps == i + 1 && f.probes == 0, "preparation does not recurse into native work");
      return true;
    });
  }
  f.gate.prepare(std::move(actions));
  f.gate.prepare({[&] {
    throw std::runtime_error("duplicate preparation ran");
    return false;
  }});
  f.gate.issued(); // A premature/duplicate issued notification cannot skip preparation.
  QObject context;
  QMetaObject::invokeMethod(&context, [&] { unrelated = steps; }, Qt::QueuedConnection);
  check(steps == 0, "preparation never performs native work on its caller's stack");
  dispatch();
  check(steps == 1 && unrelated == 1, "independent event-loop work runs before the second native operation");
  for (int i = 1; i < 9; ++i) {
    dispatch();
    check(steps == i + 1 && f.probes == 0, "each turn runs at most one native operation");
  }
  f.observation = Gate::Observation::Verified;
  dispatch();
  check(f.completions == 0 && f.probes == 0, "native writes alone never commit placement");
  dispatch();
  check(f.success && f.completions == 1 && f.probes == 1, "fresh readback follows completed preparation");
}

void preparationStops()
{
  for (int interruption = 0; interruption < 5; ++interruption) {
    Fixture f;
    int steps = 0;
    f.gate.prepare(
        {[&] {
           ++steps;
           if (interruption == 1)
             f.now += 2s;
           if (interruption == 2)
             f.gate.cancel();
           if (interruption == 3)
             return false;
           return true;
         },
         [&] {
           ++steps;
           return true;
         }}
    );
    if (interruption == 0)
      f.now += 2s;
    dispatch();
    if (interruption == 4)
      f.gate.fail("authority changed between native operations");
    for (int i = 0; i < 4; ++i)
      dispatch();
    check(steps == (interruption == 0 ? 0 : 1), "expiry, cancellation and authority loss stop remaining writes");
    check(!f.success && f.probes == 0 && !f.gate.withinDeadline(), "interrupted preparation never starts readback");
    check(f.completions == (interruption == 2 ? 0 : 1), "preparation failure completes at most once");
    if (interruption <= 1)
      check(f.failure.contains("timed out"), "queued operations do not renew the original deadline");
  }
  int steps = 0, completions = 0;
  std::unique_ptr<Gate> gate = std::make_unique<Gate>(
      [](QString *) {
        throw std::runtime_error("destroyed preparation probed");
        return Gate::Observation::Verified;
      },
      [&](bool, QString) { ++completions; }
  );
  gate->prepare(
      {[&] {
         ++steps;
         gate.reset();
         return true;
       },
       [&] {
         ++steps;
         return true;
       }}
  );
  dispatch();
  dispatch();
  check(!gate && steps == 1 && completions == 0, "destruction inside a native operation drops all queued work");
}

#if defined(__APPLE__)
void batchedWindowState()
{
  auto window = AXUIElementCreateSystemWide();
  const CGPoint point{120, 240};
  const CGSize dimensions{640, 480};
  const CGSize empty{0, 480};
  const AXError unsupported = kAXErrorAttributeUnsupported, unavailable = kAXErrorCannotComplete;
  auto position = AXValueCreate(kAXValueTypeCGPoint, &point);
  auto size = AXValueCreate(kAXValueTypeCGSize, &dimensions);
  auto invalidSize = AXValueCreate(kAXValueTypeCGSize, &empty);
  auto absent = AXValueCreate(kAXValueTypeAXError, &unsupported);
  auto failed = AXValueCreate(kAXValueTypeAXError, &unavailable);
  const auto cleanup = qScopeGuard([&] {
    for (const auto value : {position, size, invalidSize, absent, failed})
      CFRelease(value);
    CFRelease(window);
  });
  unsigned calls = 0;
  const auto observe = [&](CFTypeRef p, CFTypeRef s, CFTypeRef m, AXError status = kAXErrorSuccess, int count = 3) {
    return readNativeWindowState(window, [&](AXUIElementRef target, CFArrayRef names, auto options, CFArrayRef *out) {
      ++calls;
      check(target == window && !options && CFArrayGetCount(names) == 3, "incorrect native batch request");
      check(
          CFEqual(CFArrayGetValueAtIndex(names, 0), kAXPositionAttribute) &&
              CFEqual(CFArrayGetValueAtIndex(names, 1), kAXSizeAttribute) &&
              CFEqual(CFArrayGetValueAtIndex(names, 2), kAXMinimizedAttribute),
          "native batch omitted a required field"
      );
      const void *values[] = {p, s, m};
      *out = CFArrayCreate(nullptr, values, count, &kCFTypeArrayCallBacks);
      return status;
    });
  };
  const auto current = observe(position, size, kCFBooleanFalse);
  check(
      current && current->frame.x == 120 && current->frame.y == 240 && current->frame.width == 640 &&
          current->frame.height == 480 && current->minimized == false && calls == 1,
      "one batch did not retain all native window fields"
  );
  check(observe(position, size, kCFBooleanTrue)->minimized == true, "minimized state was lost");
  for (const auto missing : {static_cast<CFTypeRef>(absent), static_cast<CFTypeRef>(kCFNull)}) {
    const auto associated = observe(position, size, missing);
    check(associated && !associated->minimized.has_value(), "absent optional attribute became an observed false");
  }
  check(!observe(position, size, failed), "failed minimized observation was accepted");
  check(!observe(position, size, CFSTR("false")), "malformed minimized value was accepted");
  check(!observe(kCFNull, size, kCFBooleanFalse), "missing position was accepted");
  check(!observe(position, failed, kCFBooleanFalse), "failed size observation was accepted");
  check(!observe(size, position, kCFBooleanFalse), "incorrect AX value types were accepted");
  check(!observe(position, invalidSize, kCFBooleanFalse), "empty window was accepted");
  check(!observe(position, size, kCFBooleanFalse, kAXErrorCannotComplete), "failed batch was accepted");
  check(!observe(position, size, kCFBooleanFalse, kAXErrorSuccess, 2), "truncated batch was accepted");
}

void unavailableNotifications()
{
  // Reproduce attempt 26: observer creation succeeds, but registering AXMoved
  // returns CannotComplete after the source's AX messaging timeout. No native
  // window, observer, input tap or input submission is used in this regression.
  for (int unavailable = 0; unavailable <= 5; ++unavailable) {
    Fixture f;
    int registrations = 0, reports = 0;
    const bool observed = registerPlacementNotifications(
        [&](CFStringRef) {
          const bool busy = unavailable == 5 || registrations == unavailable;
          ++registrations;
          if (busy)
            f.now += 100ms;
          return busy ? kAXErrorCannotComplete : kAXErrorSuccess;
        },
        [&](CFStringRef, AXError) { ++reports; }
    );
    if (!observed)
      f.gate.fail("Cannot register source placement notifications");
    check(f.completions == 0, "a notification timeout must not abort the pending placement");
    check(registrations == 5 && reports == 5, "all five notification outcomes remain visible without retries");
    f.gate.issued();
    dispatch();
    check(f.completions == 0 && f.probes == 1, "missing notifications cannot establish successful placement");
    if (unavailable == 5) {
      f.now = Gate::Clock::time_point{} + 2s;
      f.observation = Gate::Observation::Verified;
      f.gate.changed(); // A watchdog observation cannot renew the original deadline.
      dispatch();
      check(!f.success && f.completions == 1 && f.probes == 1, "a late frame still requires rollback");
    } else {
      f.now = Gate::Clock::time_point{} + 1500ms;
      f.observation = Gate::Observation::Verified;
      f.gate.changed(); // The existing Host watchdog provides this wakeup.
      dispatch();
      check(f.success && f.completions == 1 && f.probes == 2, "fresh readback verifies without native notifications");
    }
  }
  for (const auto error :
       {kAXErrorInvalidUIElement, kAXErrorInvalidUIElementObserver, kAXErrorIllegalArgument, kAXErrorAPIDisabled,
        kAXErrorFailure}) {
    Fixture f;
    int registrations = 0;
    const bool observed = registerPlacementNotifications(
        [&](CFStringRef) { return ++registrations == 2 ? error : kAXErrorCannotComplete; }, [](CFStringRef, AXError) {}
    );
    if (!observed)
      f.gate.fail("Permanent native notification failure");
    f.gate.issued();
    f.gate.changed();
    dispatch();
    check(registrations == 2 && !observed, "a permanent error stops registration after a transient timeout");
    check(f.completions == 1 && !f.success && f.probes == 0, "invalid notification setup cannot start readback");
  }
}
#endif

struct RelocationFixture
{
  HoldingRelocationWindow window{{71, 2}, 9, 9, {400, 188, 640, 448}};
  HoldingResizeDisplay display{42, {1440, -1, 4096, 4096}};
  bool live = true, journal = true;
  int writes = 0, completions = 0;
  deskflow::windowshare::Rect destination;
  HoldingRelocationResult result;
  std::function<void(bool)> nativeComplete;
  std::deque<HoldingRelocationAccess::WindowCompletion> reads;
  void start(bool asynchronous = false)
  {
    HoldingRelocationAccess access;
    access.window = [&]() -> std::optional<HoldingRelocationWindow> {
      return live ? std::optional(window) : std::nullopt;
    };
    if (asynchronous)
      access.observeWindow = [&](HoldingRelocationAccess::WindowCompletion complete) {
        reads.push_back(std::move(complete));
      };
    access.display = [&] { return std::optional(display); };
    access.persist = [&] { return journal; };
    access.place = [&](deskflow::windowshare::Rect requested, std::function<void(bool)> complete) {
      check(journal, "durable recovery precedes native mutation");
      ++writes;
      destination = requested;
      nativeComplete = std::move(complete);
    };
    relocateHoldingWindow({71, 2}, 9, std::move(access), [&](HoldingRelocationResult observed) {
      ++completions;
      result = observed;
    });
  }
  HoldingRelocationAccess::WindowCompletion nextRead()
  {
    check(!reads.empty(), "a fresh observation was requested");
    auto complete = std::move(reads.front());
    reads.pop_front();
    return complete;
  }
};

void asynchronousWindowReads()
{
  using Status = HoldingRelocationResult::Status;
  {
    RelocationFixture f;
    f.start(true);
    check(f.writes == 0 && f.completions == 0, "pending initial AX read cannot move a source");
    auto initial = f.nextRead();
    initial(f.window);
    check(f.writes == 0 && f.completions == 0, "a fresh observation is required after recovery persistence");
    initial(std::nullopt);
    check(f.reads.size() == 1 && f.completions == 0, "duplicate initial observations cannot cancel the next stage");
    f.nextRead()(f.window);
    check(f.writes == 1 && f.completions == 0, "verified preparation submits exactly one move");
    f.window.frame = f.destination;
    f.nativeComplete(true);
    f.nativeComplete(false);
    check(f.reads.size() == 1 && f.completions == 0, "placement awaits fresh readback and ignores duplicate callbacks");
    auto final = f.nextRead();
    final(f.window);
    final(std::nullopt);
    check(f.completions == 1 && f.result.status == Status::Applied, "fresh asynchronous observations commit once");
  }
  for (int change = 0; change < 5; ++change) {
    RelocationFixture f;
    f.start(true);
    f.nextRead()(f.window);
    if (change == 0)
      ++f.window.owner.epoch;
    else if (change == 1)
      ++f.window.geometry;
    else if (change == 2)
      ++f.display.id;
    else if (change == 3)
      ++f.window.frame.x;
    f.nextRead()(change == 4 ? std::nullopt : std::optional(f.window));
    check(
        f.writes == 0 && f.completions == 1 && f.result.status == Status::Restore,
        "changed authority, display, frame or an expired read prevents native mutation"
    );
  }
  RelocationFixture f;
  f.start(true);
  f.nextRead()(f.window);
  f.nextRead()(f.window);
  f.window.frame = f.destination;
  f.nativeComplete(true);
  ++f.display.bounds.x;
  f.nextRead()(f.window);
  check(
      f.writes == 1 && f.completions == 1 && f.result.status == Status::Restore,
      "display changes during final asynchronous observation require restoration"
  );
}

void ownershipAcrossYield()
{
  using Status = HoldingRelocationResult::Status;
  {
    RelocationFixture f;
    f.start();
    check(f.writes == 1 && f.completions == 0, "submitting a move does not commit the export");
    f.window.frame = f.destination;
    f.nativeComplete(true);
    check(f.completions == 1 && f.result.status == Status::Applied, "same observed owner and contained frame commit");
    f.nativeComplete(false);
    f.nativeComplete(true);
    check(f.completions == 1, "duplicate async completion cannot commit twice");
  }
  for (int change = 0; change < 9; ++change) {
    RelocationFixture f;
    f.start();
    f.window.frame = f.destination;
    switch (change) {
    case 0:
      f.live = false;
      break;
    case 1:
      ++f.window.owner.session;
      break;
    case 2:
      ++f.window.owner.epoch;
      break;
    case 3:
      ++f.window.geometry;
      break;
    case 4:
      ++f.window.producedGeometry;
      break;
    case 5:
      ++f.display.id;
      break;
    case 6:
      ++f.display.bounds.width;
      break;
    case 7:
      f.window.frame.x = 0;
      break;
    case 8:
      break;
    }
    f.nativeComplete(change != 8);
    check(
        f.result.status == Status::Restore && f.completions == 1 && f.writes == 1,
        "changed owner, paint, display, containment or native failure requires rollback without another write"
    );
  }
  RelocationFixture f;
  f.journal = false;
  f.start();
  check(
      f.writes == 0 && f.completions == 1 && f.result.status == Status::Restore,
      "failed recovery persistence prevents native mutation"
  );
}
} // namespace

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  try {
    for (const auto &[name, test] : {
             std::pair{"transient-readback", &transientReadback}, std::pair{"absolute-deadline", &absoluteDeadline},
             std::pair{"cancellation-and-destruction", &cancellationAndDestruction},
             std::pair{"preparation-yields", &preparationYields}, std::pair{"preparation-stops", &preparationStops},
             std::pair{"ownership-across-yield", &ownershipAcrossYield},
             std::pair{"asynchronous-window-reads", &asynchronousWindowReads}
#if defined(__APPLE__)
             ,
             std::pair{"unavailable-notifications", &unavailableNotifications},
             std::pair{"batched-native-window-state", &batchedWindowState}
#endif
         }) {
      std::fprintf(stderr, "START: %s\n", name);
      std::fflush(stderr);
      test();
      std::fprintf(stderr, "PASS: %s\n", name);
      std::fflush(stderr);
    }
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
