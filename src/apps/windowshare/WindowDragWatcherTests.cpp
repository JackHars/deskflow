/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WindowDragWatcher.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace deskflow::windowshare;
namespace {
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
bool until(const std::function<bool()> &done, int budget = 750)
{
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < budget) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return done();
}
struct Fixture
{
  GnomePreparationTarget target{1, 2, 3, 4, 5, 6, QStringLiteral("org.deskflow.window.") + QString(64, 'a')};
  bool available = true, deferred = false, deny = false, throwUnwatch = false;
  unsigned watches = 0, unwatches = 0;
  std::function<void()> complete;
  std::vector<WindowDragMove> events;
  std::unique_ptr<WindowDragWatcher> watcher;
  Fixture()
  {
    watcher = std::make_unique<WindowDragWatcher>(WindowDragWatcher::Providers{
        [this]() -> std::optional<GnomePreparationTarget> { return available ? std::optional(target) : std::nullopt; },
        [this](GnomeWindowDragRequest request, auto reply) {
          if (request.action == GnomeWindowDragAction::Watch) {
            ++watches;
            auto action = [this, request, reply] { reply({request, !deny, {}, 0}); };
            if (deferred)
              complete = action;
            else
              action();
          } else {
            ++unwatches;
            if (throwUnwatch)
              throw std::runtime_error("Native unwatch unavailable");
            reply({request, true, {}, 0});
          }
          return true;
        },
        [this](const WindowDragMove &value) { events.push_back(value); }
    });
  }
  GnomeWindowDragEvent
  event(GnomeWindowDragEvent::Phase phase = GnomeWindowDragEvent::Phase::Begin, std::uint64_t sequence = 1) const
  {
    return {target.windowAssociation,
            7,
            sequence,
            phase,
            120,
            40,
            {20, 28, 640, 480},
            phase == GnomeWindowDragEvent::Phase::End};
  }
};
void normal()
{
  Fixture f;
  check(until([&] { return f.watches == 1; }), "No committed native watch");
  f.watcher->receive(f.event());
  f.watcher->receive(f.event(GnomeWindowDragEvent::Phase::Motion, 2));
  f.watcher->receive(f.event(GnomeWindowDragEvent::Phase::End, 3));
  check(f.events.size() == 3 && f.events.back().native.buttonReleased, "Native move lost its actual release");
  for (const auto &value : f.events) {
    const auto json = windowDragMoveJson(value);
    check(windowDragMoveJson(parseWindowDragMove(json)) == json, "Native move wire round trip failed");
    for (const auto key : {"geometry", "surface", "paint"}) {
      auto bad = json;
      bad[key] = 0;
      bool rejected = false;
      try {
        (void)parseWindowDragMove(bad);
      } catch (...) {
        rejected = true;
      }
      check(rejected, "Missing exact painted identity accepted");
    }
    auto bad = json;
    bad["windowId"] = 99;
    bool rejected = false;
    try {
      (void)parseWindowDragMove(bad);
    } catch (...) {
      rejected = true;
    }
    check(rejected, "Remote native target injection accepted");
  }
  f.watcher->receive(f.event());
  check(f.events.size() == 3 && f.unwatches == 1, "Replayed native grab survived retirement");
}
void invalidation()
{
  for (int mode = 0; mode != 5; ++mode) {
    Fixture f;
    check(until([&] { return f.watches == 1; }), "No watch before invalidation");
    f.watcher->receive(f.event());
    if (mode == 0)
      f.available = false;
    if (mode == 1)
      ++f.target.geometry;
    if (mode == 2)
      ++f.target.surfaceIncarnation;
    if (mode == 3)
      ++f.target.nativeSurface;
    if (mode == 4)
      f.target.windowAssociation = QStringLiteral("org.deskflow.window.") + QString(64, 'b');
    f.watcher->receive(f.event(GnomeWindowDragEvent::Phase::Motion, 2));
    check(until([&] { return f.events.size() == 2; }), "Changed native target did not cancel the exact grab");
    check(
        f.events.back().native.phase == GnomeWindowDragEvent::Phase::Cancel && !f.events.back().native.buttonReleased &&
            f.unwatches == 1,
        "Cancellation invented a release"
    );
  }
}
void pendingAndDenial()
{
  Fixture f;
  f.deferred = true;
  check(until([&] { return bool(f.complete); }), "No deferred watch request");
  f.watcher->receive(f.event());
  check(f.events.empty(), "Pending watch forwarded a native event");
  check(until([&] { return f.unwatches != 0; }), "Missing native reply did not expire");
  ++f.target.paintRevision;
  QCoreApplication::processEvents();
  check(f.watches == 1, "Paint retried a denied exact native target");
  f.watcher.reset();
  const auto before = f.unwatches;
  f.throwUnwatch = true;
  f.complete();
  check(f.unwatches == before + 1, "Late successful watch leaked after owner destruction");
}
void reentrant()
{
  Fixture f;
  check(until([&] { return f.watches == 1; }), "No watch before shutdown");
  f.watcher->receive(f.event());
  f.watcher->stop();
  f.watcher->stop();
  f.watcher->receive(f.event(GnomeWindowDragEvent::Phase::End, 2));
  check(f.events.size() == 2 && f.unwatches == 1, "Stop revived or double-retired native observation");
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  try {
    normal();
    invalidation();
    pendingAndDenial();
    reentrant();
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
  std::puts("PASS: owned native move wire, replay, paint/surface replacement, expiry and late cleanup");
}
