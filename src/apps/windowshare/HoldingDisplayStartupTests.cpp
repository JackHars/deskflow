/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "HoldingDisplayStartup.h"

#include <QCoreApplication>
#include <iostream>
#include <memory>
#include <stdexcept>

using deskflow::windowshare::HoldingDisplayStartup;
using namespace std::chrono_literals;

namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

struct Fixture
{
  HoldingDisplayStartup::Clock::time_point now{};
  int probes = 0, completions = 0;
  bool verified = false, accepted = false;
  QString failure;
  std::chrono::milliseconds probeDuration{0};
  HoldingDisplayStartup startup{
      [&](QString *reason) {
        ++probes;
        now += probeDuration;
        *reason = verified ? QString{} : "parent still observes inactive 1x1 display";
        return verified;
      },
      [&](bool success, QString reason) {
        ++completions;
        accepted = success;
        failure = std::move(reason);
      },
      [&] { return now; }
  };
  void expired() const
  {
    check(!accepted && completions == 1 && !startup.pending(), "expired startup completed exactly once as failure");
    check(failure.contains("timed out"), "expiration keeps a timeout diagnostic");
  }
};

void delayedNotifications()
{
  {
    Fixture f;
    f.now += 5s;
    f.startup.started();
    f.startup.ready();
    f.expired();
    check(f.probes == 0, "late process start cannot renew the original deadline");
  }
  {
    Fixture f;
    f.startup.started();
    f.verified = true;
    f.now += 10s;
    // Deliver readiness before Qt's overdue timer. This was previously accepted.
    QMetaObject::invokeMethod(&f.startup, [&] { f.startup.ready(); }, Qt::QueuedConnection);
    QCoreApplication::processEvents();
    f.expired();
    check(f.probes == 0, "late helper reply cannot probe or admit a display");
  }
  {
    Fixture f;
    f.startup.started();
    f.startup.ready();
    check(f.startup.pending() && f.probes == 1, "stale parent observation waits for a notification");
    f.now += 10s;
    f.verified = true;
    f.startup.changed();
    f.expired();
    check(f.probes == 1 && f.failure.contains("inactive 1x1"), "late visibility retains the last rejection");
  }
  for (const bool verified : {false, true}) {
    Fixture f;
    f.startup.started();
    f.now += 9s;
    f.verified = verified;
    f.probeDuration = 1s;
    f.startup.ready();
    f.expired();
    check(f.probes == 1, "native verification cannot overrun the deadline and succeed");
  }
  std::cout << "PASS: late process start, queued reply, display notification and native probe expire\n";
}

void deadlinesDoNotRenew()
{
  Fixture f;
  f.startup.started();
  f.startup.ready();
  f.now += 9999ms;
  f.startup.started();
  f.startup.ready();
  f.startup.expire(); // An early timeout wake-up must retain the same absolute cutoff.
  check(f.startup.pending() && f.completions == 0 && f.probes == 1, "early timer and duplicate replies do not poll");
  f.now += 1ms;
  f.startup.changed();
  f.startup.expire();
  f.startup.fail("late helper failure");
  f.expired();
  check(f.probes == 1, "duplicates and early timeout do not renew the deadline");
  std::cout << "PASS: exact deadline, duplicate notifications and early timer preserve the cutoff\n";
}

void completionAndCancellation()
{
  {
    Fixture f;
    f.startup.changed();
    f.startup.ready();
    check(f.probes == 0, "display notifications cannot replace process readiness");
    f.startup.started();
    f.startup.ready();
    f.now += 9999ms;
    f.verified = true;
    f.startup.changed();
    f.now += 1ms;
    f.startup.expire();
    f.startup.changed();
    check(f.accepted && f.completions == 1 && f.probes == 2, "verified observation before the cutoff succeeds once");
  }
  {
    Fixture f;
    f.startup.started();
    f.startup.ready();
    f.startup.cancel();
    f.now += 1h;
    f.startup.started();
    f.startup.ready();
    f.startup.changed();
    f.startup.expire();
    f.startup.fail("late helper failure");
    check(f.completions == 0 && f.probes == 1, "shutdown fences every late event without notifying its old owner");
  }
  for (const bool expires : {false, true}) {
    auto now = HoldingDisplayStartup::Clock::time_point{};
    std::unique_ptr<HoldingDisplayStartup> startup;
    int completions = 0;
    startup = std::make_unique<HoldingDisplayStartup>(
        [](QString *) { return true; },
        [&](bool success, QString) {
          check(success != expires, "destructive completion retains its success or expiry outcome");
          ++completions;
          startup.reset();
        },
        [&] { return now; }
    );
    startup->started();
    if (expires)
      now += 10s;
    startup->ready();
    check(!startup && completions == 1, "completion may destroy its gate on success or deadline failure");
  }
  std::cout << "PASS: verified readiness, shutdown cancellation and destructive completion\n";
}
} // namespace

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  try {
    delayedNotifications();
    deadlinesDoNotRenew();
    completionAndCancellation();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
