/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "HoldingRelocation.h"
#include "NativePlacementVerification.h"
#if defined(__APPLE__)
#include "NativePlacementNotifications_mac.h"
#endif

#include <QCoreApplication>
#include <QEvent>
#include <cstdio>
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

#if defined(__APPLE__)
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
  void start()
  {
    HoldingRelocationAccess access;
    access.window = [&]() -> std::optional<HoldingRelocationWindow> {
      return live ? std::optional(window) : std::nullopt;
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
};

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
             std::pair{"ownership-across-yield", &ownershipAcrossYield}
#if defined(__APPLE__)
             ,
             std::pair{"unavailable-notifications", &unavailableNotifications}
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
