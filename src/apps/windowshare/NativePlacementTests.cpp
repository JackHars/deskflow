/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "AsyncNativeInspection.h"
#include "DiagnosticWindowSnapshots.h"
#include "HoldingRelocation.h"
#include "NativeInputWatchdog.h"
#include "NativePlacementVerification.h"
#if defined(__APPLE__)
#include "NativeAttributeBatch_mac.h"
#include "NativePlacementNotifications_mac.h"
#include "NativeWindowState_mac.h"
#endif

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QScopeGuard>
#include <QSemaphore>
#include <array>
#include <cstdio>
#include <deque>
#include <memory>
#include <stdexcept>
#include <vector>

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
void dispatchUntil(const std::function<bool()> &done)
{
  bool expired = false;
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &deadline, [&] { expired = true; });
  deadline.start(2000);
  while (!done() && !expired)
    QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
  check(done(), "native worker did not complete within the fixture deadline");
}
void globalNativeInspectionCapacity()
{
  using Inspection = AsyncNativeInspection<int>;
  struct BlockedReads
  {
    QSemaphore entered, release, returned;
  };
  check(NativeInspectionCapacity::retained() == 0, "inspection capacity was not initially empty");
  Inspection::Clock::time_point now{};
  const auto blocked = std::make_shared<BlockedReads>();
  const auto unblock = qScopeGuard([&] { blocked->release.release(NativeInspectionCapacity::maximum); });
  unsigned completions = 0;
  std::vector<std::unique_ptr<Inspection>> owners;
  for (unsigned i = 0; i < NativeInspectionCapacity::maximum; ++i) {
    auto owner = std::make_unique<Inspection>([&] { return now; });
    check(
        owner->request(
            [blocked] {
              blocked->entered.release();
              blocked->release.acquire();
              blocked->returned.release();
              return 7;
            },
            [&](auto result) {
              ++completions;
              check(!result, "destroyed or expired owner accepted a blocked observation");
            }
        ),
        "inspection capacity refused an available slot"
    );
    owners.push_back(std::move(owner));
  }
  check(
      blocked->entered.tryAcquire(NativeInspectionCapacity::maximum, 2000),
      "admitted inspections did not enter their queries"
  );
  owners[0]->cancel();
  now += Inspection::maximumAge;
  owners[1]->expire();
  check(completions == 1, "expiry did not fail its owner exactly once");
  owners.clear();
  check(
      NativeInspectionCapacity::retained() == NativeInspectionCapacity::maximum,
      "cancel, expiry or owner destruction released a still-running query"
  );
  AsyncNativeInspection<QString, 2000> replacement;
  bool replacementCompleted = false;
  const auto request = [&] {
    return replacement.request(
        [] { return QStringLiteral("fresh"); },
        [&](auto result) {
          check(result == QStringLiteral("fresh"), "replacement inspection lost its result");
          replacementCompleted = true;
        }
    );
  };
  check(!request() && !replacement.busy(), "a different result type bypassed the process inspection capacity");
  blocked->release.release(NativeInspectionCapacity::maximum);
  check(
      blocked->returned.tryAcquire(NativeInspectionCapacity::maximum, 2000),
      "cancelled native queries did not return independently"
  );
  // No owner event loop has run yet. Finished QThreads still awaiting deferred
  // destruction must not allow unbounded thread objects/results to accumulate.
  check(!request(), "finished but retained thread objects lost their capacity permits");
  dispatchUntil([] {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return NativeInspectionCapacity::retained() == 0;
  });
  check(completions == 1, "destroyed owners received late inspection completions");
  check(request(), "retired native queries permanently exhausted inspection capacity");
  dispatchUntil([&] { return replacementCompleted; });
  dispatchUntil([] {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return NativeInspectionCapacity::retained() == 0;
  });
}
void asynchronousNativeInspection()
{
  using Inspection = AsyncNativeInspection<int>;
  struct BlockedRead
  {
    QSemaphore entered, release, returned;
    int read()
    {
      entered.release();
      release.acquire();
      returned.release();
      return 17;
    }
  };
  for (int scenario = 0; scenario < 5; ++scenario) {
    Inspection::Clock::time_point now{};
    auto inspection = std::make_unique<Inspection>([&] { return now; });
    const auto blocked = std::make_shared<BlockedRead>();
    const auto unblock = qScopeGuard([&] { blocked->release.release(); });
    int completions = 0;
    std::optional<int> result;
    auto *ownerThread = QThread::currentThread();
    check(
        inspection->request(
            [blocked, ownerThread] {
              check(QThread::currentThread() != ownerThread, "native read ran on the input owner thread");
              return blocked->read();
            },
            [&](std::optional<int> value) {
              check(QThread::currentThread() == ownerThread, "inspection callback left the owner thread");
              ++completions;
              result = value;
            }
        ),
        "initial native inspection was refused"
    );
    check(blocked->entered.tryAcquire(1, 2000), "worker did not enter its native query");
    bool dispatched = false;
    QMetaObject::invokeMethod(inspection.get(), [&] { dispatched = true; }, Qt::QueuedConnection);
    dispatch();
    check(dispatched && completions == 0, "blocked native query stalled the owner event loop");
    check(!inspection->request([] { return 99; }, [](auto) {}), "overlapping native worker was admitted");
    if (scenario == 1) {
      now += Inspection::maximumAge;
      inspection->expire();
      check(completions == 1 && !result, "exact inspection deadline accepted an old observation");
      inspection->expire();
    } else if (scenario == 2) {
      inspection->cancel();
    } else if (scenario == 3) {
      inspection.reset();
    } else if (scenario == 4) {
      now += Inspection::maximumAge; // Completion must check even if the timer has not dispatched.
    }
    if (inspection)
      check(!inspection->request([] { return 99; }, [](auto) {}), "cancelled read lost its worker bound");
    blocked->release.release();
    check(blocked->returned.tryAcquire(1, 2000), "cancelled worker did not finish independently");
    if (!inspection) {
      dispatch();
      check(completions == 0, "destroyed inspection called its former owner");
      continue;
    }
    dispatchUntil([&] { return !inspection->busy(); });
    check(completions == (scenario == 2 ? 0 : 1), "late native result repeated or revived completion");
    check((scenario == 0 && result == 17) || (scenario != 0 && !result), "native result was changed or reused");
    const auto previous = completions;
    check(
        inspection->request(
            [] { return 23; },
            [&](auto fresh) {
              ++completions;
              result = fresh;
            }
        ),
        "completed worker retained its slot"
    );
    dispatchUntil([&] { return !inspection->busy(); });
    check(completions == previous + 1 && result == 23, "a new inspection reused the preceding result");
  }
  auto inspection = std::make_unique<Inspection>();
  bool failed = false;
  check(
      inspection->request(
          []() -> int { throw std::runtime_error("native read failed"); },
          [&](auto value) {
            failed = !value;
            inspection.reset();
          }
      ),
      "failing native read was refused"
  );
  dispatchUntil([&] { return !inspection; });
  check(failed, "native exception did not fail closed or completion could not destroy its owner");
}
void queuedNativeInspectionCapacity()
{
  using Inspection = AsyncNativeInspection<int>;
  struct Workers
  {
    QSemaphore entered;
    std::array<QThread *, NativeInspectionCapacity::maximum> threads{};
  };
  check(NativeInspectionCapacity::retained() == 0, "queued-result fixture did not start with empty capacity");
  const auto workers = std::make_shared<Workers>();
  unsigned completions = 0;
  std::vector<std::unique_ptr<Inspection>> owners;
  for (unsigned i = 0; i < NativeInspectionCapacity::maximum; ++i) {
    auto owner = std::make_unique<Inspection>();
    check(
        owner->request(
            [workers, i] {
              workers->threads[i] = QThread::currentThread();
              workers->entered.release();
              return 17;
            },
            [&](auto) { ++completions; }
        ),
        "queued-result fixture could not claim capacity"
    );
    owners.push_back(std::move(owner));
  }
  check(workers->entered.tryAcquire(NativeInspectionCapacity::maximum, 2000), "queued-result workers did not start");
  for (auto *thread : workers->threads)
    check(thread->wait(2000), "queued-result worker did not finish");
  // Retire the thread objects without consuming custom result events.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  check(completions == 0, "queued native results were consumed as thread deletion");
  check(
      NativeInspectionCapacity::retained() == NativeInspectionCapacity::maximum,
      "retired threads released capacity still occupied by queued results"
  );
  Inspection replacement;
  check(!replacement.request([] { return 23; }, [](auto) {}), "unconsumed results bypassed the process bound");
  owners.clear();
  check(NativeInspectionCapacity::retained() == 0, "destroying result owners leaked their capacity");
  QCoreApplication::sendPostedEvents();
  check(completions == 0, "destroyed result owner received a completion");
}
void prioritizedNativeInspection()
{
  using Inspection = AsyncNativeInspection<int, 250, Qt::HighEventPriority>;
  struct Worker
  {
    QSemaphore entered;
    QThread *thread = nullptr; // Published by the semaphore; owner does not dispatch until wait() finishes.
  };
  for (int scenario = 0; scenario < 3; ++scenario) {
    Inspection::Clock::time_point now{};
    auto inspection = std::make_unique<Inspection>([&] { return now; });
    QObject normal;
    std::vector<int> order;
    std::optional<int> result;
    int completions = 0;
    QMetaObject::invokeMethod(
        &normal,
        [&] {
          order.push_back(2);
          now += Inspection::maximumAge;
        },
        Qt::QueuedConnection
    );
    const auto worker = std::make_shared<Worker>();
    check(
        inspection->request(
            [worker] {
              worker->thread = QThread::currentThread();
              worker->entered.release();
              return 17;
            },
            [&](auto value) {
              order.push_back(1);
              result = value;
              ++completions;
            }
        ),
        "priority fixture could not start its inspection"
    );
    check(worker->entered.tryAcquire(1, 2000), "priority fixture worker did not start");
    // Qt deletion is queued to this thread. Waiting without dispatching keeps
    // both the ordinary event and the finished inspection pending together.
    check(worker->thread->wait(2000), "priority fixture worker did not finish");
    if (scenario == 1)
      now += Inspection::maximumAge;
    else if (scenario == 2)
      inspection.reset();
    QCoreApplication::sendPostedEvents();
    if (scenario == 2) {
      check(completions == 0 && order == std::vector<int>{2}, "queued result survived owner destruction");
    } else {
      check(completions == 1 && order == std::vector<int>({1, 2}), "ready inspection waited behind ordinary work");
      check((scenario == 0 && result == 17) || (scenario == 1 && !result), "priority revived a stale observation");
      inspection->expire();
      check(completions == 1, "priority completion was delivered twice");
    }
  }
  AsyncNativeInspection<int, 2000> placement;
  QObject normal;
  bool completed = false, cancelled = false;
  QMetaObject::invokeMethod(
      &normal,
      [&] {
        placement.cancel();
        cancelled = true;
      },
      Qt::QueuedConnection
  );
  const auto worker = std::make_shared<Worker>();
  check(
      placement.request(
          [worker] {
            worker->thread = QThread::currentThread();
            worker->entered.release();
            return 17;
          },
          [&](auto) { completed = true; }
      ),
      "ordered placement fixture did not start"
  );
  check(worker->entered.tryAcquire(1, 2000) && worker->thread->wait(2000), "ordered placement worker did not finish");
  QCoreApplication::sendPostedEvents();
  check(cancelled && !completed && !placement.busy(), "placement result overtook an earlier cancellation");
}
void asynchronousPlacementObservation()
{
  for (int scenario = 0; scenario < 6; ++scenario) {
    Gate::Clock::time_point now{};
    std::vector<Gate::ObservationCompletion> reads;
    int completions = 0;
    bool accepted = false;
    std::unique_ptr<Gate> gate;
    gate = std::make_unique<Gate>(
        Gate::AsyncProbe([&](Gate::ObservationCompletion complete) { reads.push_back(std::move(complete)); }),
        [&](bool success, QString) {
          ++completions;
          accepted = success;
          if (scenario == 5)
            gate.reset();
        },
        [&] { return now; }
    );
    gate->issued();
    dispatch();
    check(reads.size() == 1 && !completions, "asynchronous placement did not retain one outstanding read");
    gate->changed();
    gate->changed();
    dispatch();
    check(reads.size() == 1, "native notifications started overlapping placement reads");
    if (scenario == 0) {
      const auto first = reads[0];
      first(Gate::Observation::Pending, "AX temporarily unavailable");
      dispatch();
      check(reads.size() == 2 && !completions, "notifications during the read lost their single follow-up");
      first(Gate::Observation::Verified, "duplicate old completion");
      check(!completions, "an old read completed a newer placement observation");
      reads[1](Gate::Observation::Verified, "fresh exact window");
      check(completions == 1 && accepted, "fresh asynchronous placement was rejected");
    } else {
      if (scenario == 1) {
        now += 2s;
        gate->expire();
      } else if (scenario == 2) {
        gate->cancel();
      } else if (scenario == 3) {
        gate.reset();
      } else if (scenario == 4) {
        now += 2s; // Queued completion must enforce expiry without the timer.
      }
      const auto complete = reads[0];
      complete(Gate::Observation::Verified, "native read returned");
      complete(Gate::Observation::Verified, "duplicate completion");
      check(
          completions == (scenario == 2 || scenario == 3 ? 0 : 1) && accepted == (scenario == 5),
          "late/cancelled asynchronous read revived placement or repeated completion"
      );
    }
  }

  // A blocked WindowServer/AX read must not delay an unrelated 250 ms
  // environment inspection. Placement retains its original two-second gate.
  using Read = AsyncNativeInspection<int, 2000>;
  Gate::Clock::time_point now{};
  Read reader([&] { return now; });
  AsyncNativeInspection<int> environment([&] { return now; });
  auto entered = std::make_shared<QSemaphore>(), release = std::make_shared<QSemaphore>();
  const auto unblock = qScopeGuard([&] { release->release(); });
  bool placementComplete = false, environmentComplete = false, inputTurn = false;
  Gate gate(
      Gate::AsyncProbe([&](Gate::ObservationCompletion complete) {
        check(
            reader.request(
                [entered, release] {
                  entered->release();
                  release->acquire();
                  return 7;
                },
                [complete](auto result) {
                  complete(result == 7 ? Gate::Observation::Verified : Gate::Observation::Invalid, "fresh read");
                }
            ),
            "placement worker failed to start"
        );
      }),
      [&](bool success, QString) { placementComplete = success; }, [&] { return now; }
  );
  gate.issued();
  dispatch();
  check(entered->tryAcquire(1, 2000), "placement did not enter its native read");
  check(
      environment.request([] { return 11; }, [&](auto result) { environmentComplete = result == 11; }),
      "environment inspection did not start"
  );
  QMetaObject::invokeMethod(&gate, [&] { inputTurn = true; }, Qt::QueuedConnection);
  dispatchUntil([&] { return environmentComplete && inputTurn; });
  check(!placementComplete && reader.busy(), "blocked placement monopolized the owner thread");
  now += 400ms;
  release->release();
  dispatchUntil([&] { return placementComplete; });
  check(!reader.busy(), "completed native placement retained its worker slot");
}
void asynchronousPlacementPreparation()
{
  {
    Gate::Clock::time_point now{};
    bool failed = false, started = false;
    Gate gate(
        [](QString *) { return Gate::Observation::Verified; }, [&](bool success, QString) { failed = !success; },
        [&] { return now; }
    );
    now += 2s;
    gate.prepareAfter([&](auto) { started = true; }, {});
    check(failed && !started, "expired placement started native registration");
  }
  for (int scenario = 0; scenario < 8; ++scenario) {
    Gate::Clock::time_point now{};
    Gate::PreparationCompletion registered;
    int starts = 0, writes = 0, completions = 0, checks = 0;
    bool accepted = false, authority = true;
    std::unique_ptr<Gate> gate = std::make_unique<Gate>(
        [](QString *) { return Gate::Observation::Verified; },
        [&](bool success, QString) {
          ++completions;
          accepted = success;
        },
        [&] { return now; }
    );
    gate->prepareAfter(
        [&](auto complete) {
          ++starts;
          registered = std::move(complete);
        },
        {[&] {
          ++checks;
          if (!authority)
            return false;
          ++writes;
          return true;
        }}
    );
    gate->prepareAfter([&](auto) { ++starts; }, {});
    gate->issued();
    gate->changed();
    dispatch();
    check(starts == 1 && writes == 0 && checks == 0 && completions == 0, "registration was skipped or overlapped");
    if (scenario == 2)
      gate->cancel();
    else if (scenario == 3)
      gate.reset();
    else if (scenario == 4)
      now += 2s; // Completion must enforce expiry even before the timer dispatches.
    else if (scenario == 5) {
      now += 2s;
      gate->expire();
    } else if (scenario == 6)
      authority = false; // Registration grants no authority to the later write.
    registered(scenario != 1);
    registered(true); // A duplicate or contradictory completion cannot start work twice.
    check(writes == 0, "registration completion performed a native write on its caller's stack");
    if (scenario == 7)
      gate->cancel(); // Also cancel after registration, before the queued native write.
    for (int turn = 0; turn < 4; ++turn)
      dispatch();
    const bool cancelled = scenario == 2 || scenario == 3 || scenario == 7;
    check(completions == (cancelled ? 0 : 1), "registration completion was repeated or revived a cancelled placement");
    check(accepted == (scenario == 0), "failed, late or revoked registration committed placement");
    check(writes == (scenario == 0 ? 1 : 0), "unverified registration allowed a native write");
    check(
        checks == (scenario == 0 || scenario == 6 ? 1 : 0), "write did not require fresh authority after registration"
    );
  }

  // Reproduce a busy AX notification registration without blocking Qt or
  // allowing an unrelated 250 ms environment inspection to expire behind it.
  using Reader = AsyncNativeInspection<int, 2000>;
  Gate::Clock::time_point now{};
  Reader registration([&] { return now; });
  AsyncNativeInspection<int> environment([&] { return now; });
  auto entered = std::make_shared<QSemaphore>(), release = std::make_shared<QSemaphore>();
  const auto unblock = qScopeGuard([&] { release->release(); });
  bool environmentComplete = false, inputTurn = false, placed = false;
  int writes = 0;
  Gate gate(
      [](QString *) { return Gate::Observation::Verified; }, [&](bool success, QString) { placed = success; },
      [&] { return now; }
  );
  gate.prepareAfter(
      [&](Gate::PreparationCompletion complete) {
        check(
            registration.request(
                [entered, release] {
                  entered->release();
                  release->acquire();
                  return 7;
                },
                [complete](auto result) { complete(result == 7); }
            ),
            "notification registration worker failed to start"
        );
      },
      {[&] {
        ++writes;
        return true;
      }}
  );
  check(entered->tryAcquire(1, 2000), "registration did not enter its native query");
  check(
      environment.request([] { return 11; }, [&](auto result) { environmentComplete = result == 11; }),
      "environment inspection did not start"
  );
  QMetaObject::invokeMethod(&gate, [&] { inputTurn = true; }, Qt::QueuedConnection);
  dispatchUntil([&] { return environmentComplete && inputTurn; });
  check(registration.busy() && writes == 0 && !placed, "registration stalled input or started placement too soon");
  now += 464ms; // The measured blocking registration remains inside the original two seconds.
  release->release();
  dispatchUntil([&] { return placed; });
  check(writes == 1 && !registration.busy(), "registration did not release its slot or repeated placement");
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

void nativeInputWatchdog()
{
  NativeInputWatchdog::Clock::time_point now{};
  int inspected = 0;
  NativeInputWatchdog watchdog([&] { ++inspected; }, [&] { return now; });
  KeyboardRouteBinding binding{{1, 2}, 3, 4, 5, 6, ForwardingScope::All};
  check(!watchdog.covered(binding, 7), "no prior native delivery covers watchdog work");
  watchdog.completed(binding, 7);
  check(watchdog.covered(binding, 7), "same exact live route has already been inspected");
  auto changed = binding;
  ++changed.geometry;
  check(
      !watchdog.covered(changed, 7) && !watchdog.covered(binding, 8), "geometry and capture epoch cannot inherit work"
  );
  now += 499ms;
  check(watchdog.covered(binding, 7), "idle deadline is not rounded down");
  watchdog.completed(binding, 7);
  now += 1ms;
  watchdog.expire();
  check(inspected == 0 && watchdog.covered(binding, 7), "old queued timeout cannot supersede a fresh delivery");
  now += 499ms;
  check(!watchdog.covered(binding, 7), "exact 500 ms cutoff requires a new inspection");
  watchdog.expire();
  watchdog.expire();
  check(inspected == 1 && !watchdog.covered(binding, 7), "idle inspection runs once and grants no input authority");
  watchdog.completed(binding, 7);
  watchdog.clear();
  now += 1s;
  watchdog.expire();
  check(inspected == 1 && !watchdog.covered(binding, 7), "revocation cancels deferred work");
  watchdog.completed(binding, 0);
  check(!watchdog.covered(binding, 0), "invalid epoch never suppresses inspection");
}

void diagnosticWindowSnapshots()
{
  DiagnosticWindowSnapshots snapshots;
  const QJsonObject frame{{"x", 30}, {"y", 50}, {"width", 740}, {"height", 709}};
  const QJsonObject ui{{"qtFrame", frame}, {"qtVisible", true}, {"nativeVisible", true}, {"miniaturized", false}};
  QJsonObject input{{"windowId", 10},          {"sourceWindowId", 10},     {"controlWindowId", 11},
                    {"sourceObservation", ui}, {"controlObservation", ui}, {"eventCount", 123}};
  auto moving = frame;
  moving["x"] = 1440;
  auto result = snapshots.complete(input, {moving, true}, {frame, true});
  check(
      !result["ready"].toBool() && result["originalFrame"].toObject().isEmpty(),
      "transformed native frame cannot establish an original position"
  );
  check(
      !result["sourceVisible"].toBool() && result["controlVisible"].toBool(),
      "source and controls require independent matching observations"
  );
  result = snapshots.complete(input, {frame, true}, {frame, true});
  check(
      result["ready"].toBool() && result["sourceVisible"].toBool() && result["originalFrame"] == frame,
      "fresh agreeing observations establish readiness and original position"
  );
  auto movedUi = ui;
  movedUi["qtFrame"] = moving;
  input["sourceObservation"] = movedUi;
  result = snapshots.complete(input, {frame, true}, {frame, true});
  check(
      !result["ready"].toBool() && !result["sourceVisible"].toBool(),
      "older native geometry is not accepted against a newer UI snapshot"
  );
  result = snapshots.complete(input, {moving, true}, {frame, true});
  check(
      result["ready"].toBool() && result["originalFrame"] == frame && result["frame"] == moving,
      "subsequent movement does not replace the original return position"
  );
  result = snapshots.complete(input, {moving, false}, {frame, true});
  check(!result["sourceVisible"].toBool(), "a matching frame cannot invent on-screen visibility");
  movedUi["miniaturized"] = true;
  input["sourceObservation"] = movedUi;
  result = snapshots.complete(input, {moving, true}, {frame, true});
  check(!result["sourceVisible"].toBool(), "miniaturized native window is not visible");
  result = snapshots.complete(input, {}, {});
  check(
      !result["ready"].toBool() && result["windowId"].toInteger() == 0 && result["sourceWindowId"].toInteger() == 0 &&
          result["originalFrame"] == frame,
      "missing owned WindowServer result remains unavailable without discarding recovery position"
  );
  check(
      result["eventCount"].toInt() == 123 && result["controlWindowId"].toInt() == 11,
      "worker leaves the immutable UI event snapshot and requested control identity intact"
  );
}

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

void nativeAuthorityFences()
{
  // Model the expensive permission/display snapshot independently of the
  // in-process token. Each operation gets fresh native checks before and after
  // its AX call, without recursively charging that snapshot to every fence.
  Fixture f;
  bool live = true;
  int observations = 0;
  const auto current = [&] { return live; };
  const auto native = [&] {
    ++observations;
    f.now += 45ms;
    return true;
  };
  for (int operation = 0; operation < 9; ++operation) {
    check(f.gate.observeAuthority(current, native), "fresh native authority before each operation");
    f.now += 30ms;
    check(f.gate.observeAuthority(current, native), "fresh native authority after each operation");
  }
  check(observations == 18 && f.gate.withinDeadline(), "bounded native checks leave time for placement readback");
  live = false;
  check(!f.gate.observeAuthority(current, native) && observations == 18, "revocation prevents another native read");
  live = true;
  check(!f.gate.observeAuthority(current, [] { return false; }), "native authority refusal is terminal");
  check(
      !f.gate.observeAuthority(
          current,
          [&] {
            live = false;
            return true;
          }
      ),
      "revocation during native read denies"
  );
  live = true;
  check(
      !f.gate.observeAuthority(
          current,
          [&] {
            f.now += 2s;
            return true;
          }
      ),
      "native read cannot extend the deadline"
  );
  check(
      !f.gate.observeAuthority(current, native) && observations == 18, "expired authority does not query native state"
  );
  {
    Fixture cancelled;
    check(
        !cancelled.gate.observeAuthority(
            [] { return true; },
            [&] {
              cancelled.gate.cancel();
              return true;
            }
        ),
        "cancellation during a native read denies"
    );
  }
  for (bool inNative : {false, true}) {
    auto gate = std::make_unique<Gate>([](QString *) { return Gate::Observation::Verified; }, [](bool, QString) {});
    check(
        !gate->observeAuthority(
            [&] {
              if (!inNative)
                gate.reset();
              return true;
            },
            [&] {
              gate.reset();
              return true;
            }
        ),
        "destruction during either authority callback is safe and refuses placement"
    );
  }
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
void batchedNativeAttributes()
{
  auto window = AXUIElementCreateSystemWide();
  const AXError unavailable = kAXErrorCannotComplete;
  auto failure = AXValueCreate(kAXValueTypeAXError, &unavailable);
  const auto cleanup = qScopeGuard([&] {
    CFRelease(failure);
    CFRelease(window);
  });
  unsigned calls = 0;
  const auto read = [&](CFTypeRef top, CFTypeRef role, AXError status = kAXErrorSuccess, int count = 2) {
    return readNativeAttributes(
        window, std::array{kAXTopLevelUIElementAttribute, kAXRoleAttribute},
        [&](AXUIElementRef target, CFArrayRef names, auto options, CFArrayRef *out) {
          ++calls;
          check(target == window && !options && CFArrayGetCount(names) == 2, "wrong native attribute batch");
          check(
              CFEqual(CFArrayGetValueAtIndex(names, 0), kAXTopLevelUIElementAttribute) &&
                  CFEqual(CFArrayGetValueAtIndex(names, 1), kAXRoleAttribute),
              "native batch changed the requested observations"
          );
          const void *values[] = {top, role};
          *out = CFArrayCreate(nullptr, values, count, &kCFTypeArrayCallBacks);
          return status;
        }
    );
  };
  auto first = read(window, kAXSheetRole);
  check(first.get(0) == window && CFEqual(first.get(1), kAXSheetRole) && calls == 1, "one fresh batch lost a field");
  auto second = read(window, kAXDrawerRole);
  check(CFEqual(second.get(1), kAXDrawerRole) && calls == 2, "a later observation reused a stale role");
  auto moved = std::move(first);
  check(!first.get(0) && moved.get(0) == window, "moving a batch lost or duplicated its ownership");
  check(!moved.get(-1) && !moved.get(2), "out-of-range observation was accepted");
  const auto partial = read(window, failure);
  check(partial.get(0) == window && !partial.get(1), "per-field AX failure became an observation");
  const auto missing = read(kCFNull, kAXSheetRole);
  check(!missing.get(0) && CFEqual(missing.get(1), kAXSheetRole), "absent field became a value");
  const auto failed = read(window, kAXSheetRole, kAXErrorCannotComplete);
  check(!failed.get(0) && !failed.get(1), "failed call admitted partial authority");
  const auto shortReply = read(window, kAXSheetRole, kAXErrorSuccess, 1);
  check(!shortReply.get(0) && !shortReply.get(1), "truncated batch was accepted");
  const auto retained = [&] {
    const auto temporary = read(window, kAXSheetRole);
    return temporary.copy(0);
  }();
  check(retained && CFEqual(retained, window), "copied element did not survive its reply");
  CFRelease(retained);
}

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
             std::pair{"global-native-inspection-capacity", &globalNativeInspectionCapacity},
             std::pair{"queued-native-inspection-capacity", &queuedNativeInspectionCapacity},
             std::pair{"native-input-watchdog", &nativeInputWatchdog},
             std::pair{"asynchronous-native-inspection", &asynchronousNativeInspection},
             std::pair{"prioritized-native-inspection", &prioritizedNativeInspection},
             std::pair{"asynchronous-placement-observation", &asynchronousPlacementObservation},
             std::pair{"asynchronous-placement-preparation", &asynchronousPlacementPreparation},
             std::pair{"diagnostic-window-snapshots", &diagnosticWindowSnapshots},
             std::pair{"transient-readback", &transientReadback}, std::pair{"absolute-deadline", &absoluteDeadline},
             std::pair{"native-authority-fences", &nativeAuthorityFences},
             std::pair{"cancellation-and-destruction", &cancellationAndDestruction},
             std::pair{"preparation-yields", &preparationYields}, std::pair{"preparation-stops", &preparationStops},
             std::pair{"ownership-across-yield", &ownershipAcrossYield},
             std::pair{"asynchronous-window-reads", &asynchronousWindowReads}
#if defined(__APPLE__)
             ,
             std::pair{"unavailable-notifications", &unavailableNotifications},
             std::pair{"batched-native-attributes", &batchedNativeAttributes},
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
