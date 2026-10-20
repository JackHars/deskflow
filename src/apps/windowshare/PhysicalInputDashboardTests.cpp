/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "PhysicalInputDashboard.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <cstdio>
#include <stdexcept>
#include <tuple>

using namespace deskflow::windowshare;
namespace {
void check(bool value, const char *reason)
{
  if (!value)
    throw std::runtime_error(reason);
}
void key(
    QWidget &target, QEvent::Type type, int code, const QString &text = {}, bool repeat = false,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier
)
{
  QKeyEvent event(type, code, modifiers, text, repeat);
  QApplication::sendEvent(&target, &event);
}
void typeExpected(PhysicalInputDashboard &board)
{
  for (auto [code, text, modifiers] :
       {std::tuple{Qt::Key_A, QString("a"), Qt::NoModifier}, std::tuple{Qt::Key_B, QString("B"), Qt::ShiftModifier},
        std::tuple{Qt::Key_C, QString("c"), Qt::NoModifier}}) {
    key(board.field(), QEvent::KeyPress, code, text, false, modifiers);
    key(board.field(), QEvent::KeyRelease, code, text, false, modifiers);
  }
}
void mouse(QWidget &target, QEvent::Type type, QPointF point, Qt::MouseButton button, Qt::MouseButtons held)
{
  QMouseEvent event(type, point, target.mapToGlobal(point.toPoint()), button, held, Qt::NoModifier);
  QApplication::sendEvent(&target, &event);
}
void drag(PhysicalInputDashboard &board, bool onTarget)
{
  auto &target = board.dragTarget();
  const auto end = onTarget ? target.destination() : target.origin() + QPointF(60, -20);
  mouse(target, QEvent::MouseButtonPress, target.origin(), Qt::LeftButton, Qt::LeftButton);
  mouse(target, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  mouse(target, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
}
void wheel(PhysicalInputDashboard &board, QPoint pixels, QPoint angle = {})
{
  auto *target = board.scrollTarget().viewport();
  const QPointF position = target->rect().center();
  QWheelEvent event(
      position, target->mapToGlobal(position.toPoint()), pixels, angle, Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate,
      false
  );
  QApplication::sendEvent(target, &event);
}
bool observed(const PhysicalInputDashboard &board, const QString &name)
{
  return board.snapshot()["checks"].toObject()[name].toBool();
}
void complete(PhysicalInputDashboard &board, qint64 &now)
{
  typeExpected(board);
  key(board.field(), QEvent::KeyPress, Qt::Key_X, "x");
  now += 600;
  key(board.field(), QEvent::KeyPress, Qt::Key_X, "x", true);
  // Some Qt backends emit repeat releases; they must not end the real hold.
  key(board.field(), QEvent::KeyRelease, Qt::Key_X, "x", true);
  now += 500;
  key(board.field(), QEvent::KeyPress, Qt::Key_X, "x", true);
  key(board.field(), QEvent::KeyRelease, Qt::Key_X, "x");
  const auto center = board.clickTarget().rect().center();
  mouse(board.clickTarget(), QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  mouse(board.clickTarget(), QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  drag(board, true);
  wheel(board, {0, 36});
  wheel(board, {0, -42});
  wheel(board, {48, 0});
  wheel(board, {-32, 0});
  board.refresh();
  check(board.checked() == 6, "real widget input did not complete the six visual checks");
}
void save(QWidget &window, const QString &directory, const QString &name)
{
  if (directory.isEmpty())
    return;
  QCoreApplication::processEvents();
  check(window.grab().save(QDir(directory).filePath(name + ".png")), "cannot save offscreen UI preview");
}
} // namespace

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  try {
    const auto output =
        app.arguments().size() == 3 && app.arguments()[1] == "--preview-dir" ? app.arguments()[2] : QString();
    if (!output.isEmpty())
      check(QDir().mkpath(output), "cannot create preview directory");
    QWidget window;
    window.resize(740, 640);
    auto *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 12);
    qint64 now = 100;
    auto *board = new PhysicalInputDashboard(&window, [&] { return now; });
    layout->addWidget(board);
    auto *record = new QPushButton("Record 30-second check");
    record->setMinimumHeight(40);
    record->setStyleSheet(
        "QPushButton { background:#245eea;color:white;border-radius:6px;font-weight:600; }"
        "QPushButton:disabled { background:#d6dfeb;color:#526581; }"
    );
    layout->addWidget(record);
    window.show(); // Offscreen only: no OS input, native hooks or sharing.
    QCoreApplication::processEvents();
    check(board->checked() == 0, "new board already reports completed checks");
    typeExpected(*board);
    check(board->checked() == 0, "input before connection counted as practice");
    using Phase = PhysicalInputDashboard::Phase;
    board->setPhase(Phase::Practice);
    save(window, output, "practice");
    key(board->field(), QEvent::KeyPress, Qt::Key_X, "x");
    now += 1200;
    key(board->field(), QEvent::KeyRelease, Qt::Key_X, "x");
    check(!observed(*board, "holdRepeatRelease"), "a long key press without repeat passed");
    key(board->field(), QEvent::KeyPress, Qt::Key_X, "x", true);
    check(!observed(*board, "holdRepeatRelease"), "an orphan repeat passed");
    key(board->field(), QEvent::KeyPress, Qt::Key_X, "x");
    now += 1100;
    key(board->field(), QEvent::KeyPress, Qt::Key_X, "x", true);
    QFocusEvent blur(QEvent::FocusOut);
    QApplication::sendEvent(&board->field(), &blur);
    key(board->field(), QEvent::KeyRelease, Qt::Key_X, "x");
    check(!observed(*board, "holdRepeatRelease"), "lost focus was mistaken for a complete hold/release");
    drag(*board, false);
    check(!observed(*board, "dragDrop"), "dropping outside the ring passed");
    const auto before = board->scrollTarget().horizontalScrollBar()->value();
    wheel(*board, {48, 0});
    check(board->scrollTarget().horizontalScrollBar()->value() != before, "horizontal scroll is not visible");
    check(
        !observed(*board, "verticalScroll") && !observed(*board, "horizontalScroll"),
        "one scroll direction passed both axes"
    );
    complete(*board, now);
    board->setPhase(Phase::Arming);
    wheel(*board, {0, 100});
    check(board->checked() == 6, "arming changed the practice checklist");
    board->setPhase(Phase::Recording, 30);
    record->setEnabled(false);
    record->setText("Recording — use the test board above");
    check(
        board->checked() == 0 && board->field().text().isEmpty() && board->snapshot()["repeatEvents"].toInt() == 0,
        "practice results leaked into the measured recording"
    );
    save(window, output, "recording");
    board->setPhase(Phase::Complete);
    check(board->checked() == 0, "an empty recording was marked complete");
    save(window, output, "incomplete");
    board->setPhase(Phase::Recording, 18);
    complete(*board, now);
    const auto snapshot = board->snapshot();
    check(
        snapshot["repeatEvents"].toInt() == 2 && snapshot["longestHoldMs"].toInt() == 1100,
        "held-key duration/repeat feedback is incorrect"
    );
    check(snapshot["physicalAcceptance"] == "not-assessed", "visual checks claimed native acceptance");
    // A recording update must not reset the current checklist on every tick.
    board->setPhase(Phase::Recording, 17);
    check(board->checked() == 6, "countdown update erased the current recording");
    save(window, output, "recorded-inputs");
    board->setPhase(Phase::Complete);
    const auto frozen = board->snapshot();
    record->setText("Recording finished");
    key(board->field(), QEvent::KeyPress, Qt::Key_X, "x");
    wheel(*board, {0, 100});
    check(board->snapshot() == frozen, "inputs after recording altered its result");
    save(window, output, "complete");
    board->setPhase(Phase::Recording, 1);
    board->setRecordingGate([] { return false; });
    const auto expired = board->snapshot();
    typeExpected(*board);
    wheel(*board, {100, 100});
    drag(*board, true);
    const auto click = board->clickTarget().rect().center();
    mouse(board->clickTarget(), QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
    mouse(board->clickTarget(), QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
    check(board->snapshot() == expired, "events after the actual recorder deadline counted before the next UI tick");
    board->setRecordingGate({});
    board->setPhase(Phase::Practice);
    wheel(*board, {}, {0, 120});
    wheel(*board, {}, {0, -120});
    check(observed(*board, "verticalScroll"), "ordinary mouse wheel deltas did not count");
    std::puts(
        "Typing, actual repeat/release, click, drag/drop, both scroll axes, recording reset and frozen results passed "
        "offscreen"
    );
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
