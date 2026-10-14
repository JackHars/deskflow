/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WindowDragPreviewController.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
#include <deque>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace deskflow::windowshare;
namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
bool wait(const std::function<bool()> &done, int budget = 1500)
{
  QElapsedTimer elapsed;
  elapsed.start();
  while (!done() && elapsed.elapsed() < budget) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return done();
}
struct Fixture
{
  WindowDragPreviewController::Paint paint{
      QStringLiteral("org.deskflow.window.") + QString(64, 'a'), 4, 1, 8, {640, 480}
  };
  bool visible = true, repaints = true, defer = false, wrongFrame = false;
  QRect frame;
  std::uint32_t revision = 0;
  std::deque<std::function<void()>> pending;
  std::vector<GnomeWindowDragAction> operations;
  std::vector<WindowDragPreviewReply> replies;
  std::unique_ptr<WindowDragPreviewController> controller;
  Fixture()
  {
    controller = std::make_unique<WindowDragPreviewController>(WindowDragPreviewController::Providers{
        [this]() -> std::optional<WindowDragPreviewController::Paint> {
          return visible ? std::optional(paint) : std::nullopt;
        },
        [this] {
          if (repaints)
            ++paint.serial;
        },
        [this](GnomeWindowDragRequest request, auto complete) {
          operations.push_back(request.action);
          if (request.action == GnomeWindowDragAction::Cancel)
            return true;
          auto operation = [this, request, complete] {
            if (request.action == GnomeWindowDragAction::Place) {
              frame = QRect(request.x, request.y, 650, 510);
              ++revision;
            }
            auto actual = frame;
            if (wrongFrame && request.action != GnomeWindowDragAction::Place)
              actual.translate(1, 0);
            complete(
                GnomeWindowDragReply{
                    request, true, actual, request.action == GnomeWindowDragAction::Finish ? 0 : revision
                }
            );
          };
          if (defer)
            pending.push_back(std::move(operation));
          else
            operation();
          return true;
        },
        [this](const auto &reply) { replies.push_back(reply); }
    });
  }
  WindowDragPreviewCommand command(std::uint64_t sequence = 10) const
  {
    return {WindowDragPreviewCommand::Kind::Move, 7, sequence, 4, 220, 75, .5, .025};
  }
  void complete()
  {
    check(!pending.empty(), "Missing native fixture operation");
    const auto operation = std::move(pending.front());
    pending.pop_front();
    operation();
  }
};
void placementAndDrop()
{
  Fixture fixture;
  auto move = fixture.command();
  check(fixture.controller->receive(move), "Exact first placement refused");
  check(fixture.replies.empty(), "Native position alone acknowledged a preview before paint");
  check(
      wait([&] { return fixture.replies.size() == 1; }) && fixture.replies.back().ok &&
          fixture.replies.back().paint == 2 && fixture.replies.back().frame == QRect(-100, 63, 650, 510),
      "Fresh painted position was not verified"
  );
  auto hold = move;
  hold.kind = WindowDragPreviewCommand::Kind::Hold;
  check(
      fixture.controller->receive(hold) && fixture.operations.back() == GnomeWindowDragAction::Hold,
      "Explicit stationary heartbeat did not verify native placement"
  );
  auto drop = move;
  drop.kind = WindowDragPreviewCommand::Kind::Drop;
  check(
      fixture.controller->receive(drop) && wait([&] { return fixture.replies.size() == 2; }), "Exact drop not completed"
  );
  const auto &reply = fixture.replies.back();
  check(
      reply.command == drop && reply.ok && reply.placement == 1 && reply.paint == 2,
      "Drop lost its current native placement and fresh paint"
  );
  const auto encoded = windowDragPreviewJson(reply);
  check(parseWindowDragPreviewReply(encoded).command == drop, "Network reply binding did not roundtrip");
}
void coalescedMovement()
{
  Fixture fixture;
  fixture.defer = true;
  auto first = fixture.command(), last = fixture.command(11);
  last.x += 25;
  check(fixture.controller->receive(first) && fixture.controller->receive(last), "Bounded motion coalescing failed");
  auto drop = last;
  drop.kind = WindowDragPreviewCommand::Kind::Drop;
  check(fixture.controller->receive(drop), "Release during outstanding paint was rejected");
  fixture.complete(); // First native placement.
  check(wait([&] { return !fixture.pending.empty(); }), "First paint was not rechecked");
  fixture.complete(); // First placement verified, but not final drop.
  check(
      fixture.replies.size() == 1 && fixture.replies[0].command.sequence == first.sequence,
      "Older coalesced movement was mistaken for final drop"
  );
  check(wait([&] { return !fixture.pending.empty(); }), "Final native placement was not requested");
  fixture.complete();
  check(wait([&] { return !fixture.pending.empty(); }), "Final paint was not verified");
  fixture.complete();
  check(wait([&] { return !fixture.pending.empty(); }), "Drop did not verify exact final native frame");
  fixture.complete();
  check(
      fixture.replies.size() == 3 && fixture.replies.back().ok && fixture.replies.back().command == drop &&
          fixture.replies.back().placement == 2,
      "Drop did not retain the final coalesced placement"
  );
}
void faults()
{
  for (unsigned fault = 0; fault < 5; ++fault) {
    Fixture fixture;
    if (fault == 0)
      fixture.repaints = false;
    if (fault == 1)
      fixture.wrongFrame = true;
    if (fault == 2)
      fixture.visible = false;
    auto command = fixture.command();
    fixture.controller->receive(command);
    if (fault == 3)
      ++fixture.paint.surface;
    if (fault == 4) {
      ++command.x;
      command.kind = WindowDragPreviewCommand::Kind::Hold;
      fixture.controller->receive(command);
    }
    check(
        wait([&] { return !fixture.replies.empty(); }) && !fixture.replies.back().ok,
        "Missing/stale paint, changed native placement/identity or substituted heartbeat was accepted"
    );
    check(!fixture.controller->receive(fixture.command(12)), "Retired drag was revived");
  }
  Fixture canceled;
  canceled.defer = true;
  canceled.controller->receive(canceled.command());
  canceled.controller->stop();
  canceled.complete();
  check(canceled.replies.empty(), "Canceled late native placement acknowledged a preview");
  Fixture destroyed;
  destroyed.defer = true;
  destroyed.controller->receive(destroyed.command());
  destroyed.controller.reset();
  destroyed.complete();
  check(destroyed.replies.empty(), "Destroyed preview handled a late placement");
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  try {
    placementAndDrop();
    coalescedMovement();
    faults();
    std::puts(
        "PASS: exact native placement, fresh paint and recheck; coalesced final drop; timeout, identity, cancellation "
        "and late callbacks"
    );
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
