/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VideoDecoder.h"
#include "Viewer.h"

#include <QApplication>
#include <QEventLoop>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QTextStream>
#include <QWheelEvent>
#include <cmath>
#include <stdexcept>
#include <vector>

using namespace deskflow::windowshare;

namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
bool near(double a, double b)
{
  return std::abs(a - b) < 0.001;
}

QByteArray frame()
{
  // Generated 64x48 solid red baseline H.264 fixture. SPS/PPS and one IDR only;
  // no external file, encoder binary, timing dependency, or copyrighted image.
  return QByteArray::fromBase64(
      "AAAAAWdCwAraEewEQAAAAwBAAAAPI8SJqAAAAAFozg/IAAAAAWWIhDoRigACGPHAAED2OAAIeUnJyddddddddeA="
  );
}
QJsonObject metadata(std::uint64_t geometry = 1, int width = 640, int height = 480)
{
  return {
      {"title", "Viewer regression fixture"},
      {"geometry", static_cast<qint64>(geometry)},
      {"width", width},
      {"height", height},
      {"pixelWidth", 64},
      {"pixelHeight", 48},
      {"codec", "h264"},
      {"phase", "preparing"}
  };
}

bool waitUntil(const std::function<bool()> &condition, int timeout = 5000)
{
  if (condition())
    return true;
  QEventLoop loop;
  QTimer poll, stop;
  poll.setInterval(5);
  stop.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (condition())
      loop.quit();
  });
  QObject::connect(&stop, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start();
  stop.start(timeout);
  loop.exec();
  return condition();
}

struct Sent
{
  MessageType type;
  QJsonObject object;
  std::uint64_t session, epoch;
};
struct Recording
{
  struct RouteSent
  {
    KeyboardRouteControl control;
    std::size_t inputCount;
  };
  std::vector<Sent> events;
  std::vector<RouteSent> routes;
  QStringList errors;
  void attach(Viewer &viewer)
  {
    viewer.onSend = [this](MessageType type, const QJsonObject &object, std::uint64_t session, std::uint64_t epoch) {
      events.push_back({type, object, session, epoch});
      return true;
    };
    viewer.onKeyboardRouteSend = [this](const KeyboardRouteControl &control) {
      routes.push_back({control, events.size()});
      return true;
    };
    viewer.onError = [this](QString error) { errors.push_back(error); };
  }
  int count(MessageType type, const QString &kind = {}) const
  {
    int count = 0;
    for (const auto &event : events)
      if (event.type == type && (kind.isEmpty() || event.object.value("kind").toString() == kind))
        ++count;
    return count;
  }
  const Sent &last(MessageType type, const QString &kind = {}) const
  {
    for (auto event = events.rbegin(); event != events.rend(); ++event)
      if (event->type == type && (kind.isEmpty() || event->object.value("kind").toString() == kind))
        return *event;
    throw std::runtime_error("Expected message was not sent");
  }
  std::uint64_t focusRequest() const
  {
    return last(MessageType::Input, "focus").object.value("request").toInteger();
  }
  const KeyboardRouteControl &lastRoute() const
  {
    check(!routes.empty(), "Expected keyboard route response was not sent");
    return routes.back().control;
  }
};

void focus(Viewer &viewer, bool active)
{
  QFocusEvent event(active ? QEvent::FocusIn : QEvent::FocusOut, Qt::OtherFocusReason);
  QCoreApplication::sendEvent(&viewer, &event);
}
void key(Viewer &viewer, QEvent::Type type, int code, QString text = {}, bool repeat = false)
{
  QKeyEvent event(type, code, Qt::NoModifier, text, repeat);
  QCoreApplication::sendEvent(&viewer, &event);
}
void mouse(
    Viewer &viewer, QEvent::Type type, QPointF position, Qt::MouseButton button = Qt::LeftButton,
    Qt::MouseButtons buttons = Qt::LeftButton
)
{
  QMouseEvent event(type, position, position, position, button, buttons, Qt::NoModifier);
  QCoreApplication::sendEvent(&viewer, &event);
}

void makeReady(Viewer &viewer, Recording &recording)
{
  viewer.show();
  QCoreApplication::processEvents();
  check(!viewer.firstFramePainted(), "Viewer declared ready without decoded pixels");
  check(recording.count(MessageType::Ready) == 0, "Ready sent before decoding");
  viewer.receiveFrame(frame(), viewer.geometryRevision());
  check(waitUntil([&] { return viewer.firstFramePainted(); }), "First video frame was not decoded and painted");
  check(recording.count(MessageType::Ready) == 1, "First painted frame did not send one Ready");
}
void makeFocused(Viewer &viewer, Recording &recording, std::uint64_t lease = 7)
{
  viewer.committed();
  focus(viewer, true);
  check(recording.count(MessageType::Input, "focus") > 0, "Committed active window did not request focus");
  viewer.focusAcknowledged(lease, recording.focusRequest());
}

KeyboardRouteControl
originalRoute(const Viewer &viewer, const Recording &recording, std::uint64_t generation = 1, std::uint64_t lease = 7)
{
  const auto &request = recording.last(MessageType::Input, "focus");
  return {{{request.session, request.epoch}, generation, recording.focusRequest(), lease, viewer.geometryRevision()}};
}

KeyboardRouteControl resumeRoute(KeyboardRouteControl control)
{
  control.mode = KeyboardRouteMode::Viewer;
  return control;
}

void testInputMethodIsExplicitlyUnsupported()
{
  Viewer viewer(205, 17, metadata());
  Recording recording;
  recording.attach(viewer);
  check(!viewer.testAttribute(Qt::WA_InputMethodEnabled), "Viewer advertised unproved input-method support");
  makeReady(viewer, recording);
  makeFocused(viewer, recording);
  const auto before = recording.events.size();
  QInputMethodEvent preedit(QStringLiteral("pending"), {});
  QCoreApplication::sendEvent(&viewer, &preedit);
  check(!preedit.isAccepted(), "Viewer accepted a deferred input-method preedit");
  QInputMethodEvent commit;
  commit.setCommitString(QStringLiteral("dangerous composition"));
  QCoreApplication::sendEvent(&viewer, &commit);
  check(!commit.isAccepted(), "Viewer accepted a deferred input-method commit");
  check(recording.events.size() == before, "Input method bypassed the native/Qt forwarding fence");
  viewer.close();
}

void testDecoder()
{
  VideoDecoder decoder(false);
  check(!decoder.decode("not H264").error.isEmpty(), "Malformed video did not fail closed");
  check(!decoder.decode(QByteArray::fromHex("00000001ff")).error.isEmpty(), "Invalid NAL header accepted");
  auto decoded = decoder.decode(frame());
  check(decoded.error.isEmpty(), "Known H.264 fixture failed decoding");
  check(decoded.image.size() == QSize(64, 48), "Decoded fixture dimensions were wrong");
  const QColor pixel = decoded.image.pixelColor(32, 24);
  check(pixel.red() > 230 && pixel.green() < 20 && pixel.blue() < 20, "Decoded RGB conversion was wrong");
  decoder.reset();
  decoded = decoder.decode(frame());
  check(decoded.error.isEmpty() && !decoded.image.isNull(), "Decoder failed after reset");
}

void testMappingAndReadiness()
{
  Recording recording;
  Viewer viewer(31, 4, metadata());
  recording.attach(viewer);
  viewer.resize(400, 400);
  const auto rectangle = viewer.videoRect();
  check(
      near(rectangle.x(), 0) && near(rectangle.y(), 50) && near(rectangle.width(), 400) &&
          near(rectangle.height(), 300),
      "Letterbox rectangle was wrong"
  );
  check(!viewer.mapToSource({200, 20}), "Black bar accepted input");
  const auto middle = viewer.mapToSource({200, 200});
  check(middle && near(middle->x(), 320) && near(middle->y(), 240), "Source logical coordinate mapping was wrong");
  const auto edge = viewer.mapToSource({800, 800}, true);
  check(
      edge && edge->x() < 640 && edge->y() < 480 && near(edge->x(), 640), "Captured drag was not clamped inside source"
  );
  makeReady(viewer, recording);
  check(recording.count(MessageType::Input, "focus") == 0, "Viewer requested focus before host commit");
  viewer.repaint();
  QCoreApplication::processEvents();
  check(recording.count(MessageType::Ready) == 1, "Repeated paint duplicated Ready");
  check(
      recording.last(MessageType::Ready).session == 31 && recording.last(MessageType::Ready).epoch == 4,
      "Viewer messages lost session identity"
  );
  viewer.disconnected();
}

void testViewportLifecycle()
{
  Recording recording;
  auto viewer = std::make_unique<Viewer>(131, 14, metadata());
  recording.attach(*viewer);
  makeReady(*viewer, recording);
  makeFocused(*viewer, recording);
  QCoreApplication::processEvents();
  if (!QGuiApplication::platformName().startsWith("wayland")) {
    check(!viewer->waylandViewportState(), "A non-Wayland window manufactured native viewport evidence");
    check(!viewer->mapWaylandSurface({}, {20, 30}), "An unbound native surface point was mapped");
  }
  int invalidations = 0;
  viewer->onViewportInvalidated = [&] { ++invalidations; };
  viewer->repaint();
  QCoreApplication::processEvents();
  check(invalidations == 0, "An unchanged video repaint revoked its viewport");
  for (int i = 0; i < 3; ++i) {
    QMoveEvent event(viewer->pos(), viewer->pos());
    QCoreApplication::sendEvent(viewer.get(), &event);
  }
  check(invalidations == 0, "Viewport invalidation reentered the triggering native event");
  if (!waitUntil([&] { return invalidations == 1; })) {
    QTextStream(stderr) << "Viewport coalescing callbacks: expected=1 actual=" << invalidations << '\n';
    throw std::runtime_error("Native viewport changes were not coalesced");
  }
  focus(*viewer, false);
  check(waitUntil([&] { return invalidations == 2; }), "Focus loss did not revoke viewport observation");
  viewer->onViewportInvalidated = [&] { viewer.reset(); };
  viewer->disconnected();
  check(waitUntil([&] { return !viewer; }), "Viewport invalidation observer could not safely destroy viewer");
}

void testFocusQueueAndOwnership()
{
  Recording recording;
  Viewer viewer(32, 5, metadata());
  recording.attach(viewer);
  makeReady(viewer, recording);
  viewer.committed();
  focus(viewer, true);
  const auto request = recording.focusRequest();
  mouse(viewer, QEvent::MouseButtonPress, viewer.videoRect().center());
  mouse(viewer, QEvent::MouseButtonRelease, viewer.videoRect().center(), Qt::LeftButton, Qt::NoButton);
  check(recording.count(MessageType::Input, "button") == 0, "Click leaked before focus acknowledgement");
  viewer.focusAcknowledged(7, request + 1);
  check(recording.count(MessageType::Input, "button") == 0, "Stale focus reply released pending input");
  viewer.focusAcknowledged(7, request);
  check(recording.count(MessageType::Input, "button") == 2, "Activating click was swallowed instead of queued");
  const auto click = recording.last(MessageType::Input, "button").object;
  check(
      click.value("code").toInt() == 0 && !click.value("down").toBool() &&
          click.value("focusRevision").toInteger() == 7 && click.value("geometry").toInteger() == 1,
      "Click normalization or ownership revision was wrong"
  );
  mouse(viewer, QEvent::MouseButtonDblClick, viewer.videoRect().center());
  check(
      recording.last(MessageType::Input, "button").object.value("clickCount").toInt() == 2,
      "Double-click lost its native click count"
  );
  mouse(viewer, QEvent::MouseButtonRelease, viewer.videoRect().center(), Qt::LeftButton, Qt::NoButton);
  check(
      recording.last(MessageType::Input, "button").object.value("clickCount").toInt() == 2,
      "Double-click release lost its native click count"
  );
  key(viewer, QEvent::KeyPress, Qt::Key_Left);
  key(viewer, QEvent::KeyPress, Qt::Key_Left);
  key(viewer, QEvent::KeyPress, Qt::Key_Left, {}, true);
  key(viewer, QEvent::KeyRelease, Qt::Key_Left, {}, true);
  check(recording.count(MessageType::Input, "key") == 2, "Duplicate down or autorepeat release was forwarded");
  mouse(viewer, QEvent::MouseButtonPress, viewer.videoRect().center(), Qt::RightButton, Qt::RightButton);
  const auto beforeLoss = recording.events.size();
  focus(viewer, false);
  check(recording.events.size() >= beforeLoss + 3, "Focus loss did not release owned key and button");
  check(
      recording.events[beforeLoss].object.value("kind") == "key" &&
          !recording.events[beforeLoss].object.value("down").toBool() &&
          recording.events[beforeLoss + 1].object.value("kind") == "button" &&
          !recording.events[beforeLoss + 1].object.value("down").toBool() &&
          recording.events[beforeLoss + 2].object.value("kind") == "release",
      "Ownership release ordering was wrong"
  );
  const auto afterLoss = recording.events.size();
  viewer.focusAcknowledged(7, request);
  key(viewer, QEvent::KeyRelease, Qt::Key_Left);
  mouse(viewer, QEvent::MouseButtonRelease, viewer.videoRect().center(), Qt::RightButton, Qt::NoButton);
  check(recording.events.size() == afterLoss, "Late physical ups or focus ack duplicated released input");
  focus(viewer, true);
  key(viewer, QEvent::KeyPress, Qt::Key_Tab, "\t");
  const auto secondRequest = recording.focusRequest();
  check(secondRequest > request, "Focus request serial did not advance");
  const auto beforeStale = recording.count(MessageType::Input, "key");
  viewer.focusAcknowledged(7, request);
  check(recording.count(MessageType::Input, "key") == beforeStale, "Old lease acknowledged a new request");
  viewer.focusAcknowledged(8, secondRequest);
  check(
      recording.last(MessageType::Input, "key").object.value("code").toInt() == Qt::Key_Tab,
      "Qt focus traversal swallowed Tab"
  );
  viewer.close();
  check(recording.count(MessageType::Return) == 1, "Closing viewer did not return original window");
  check(!recording.last(MessageType::Input, "key").object.value("down").toBool(), "Close stranded a pressed key");
}

void testModifierRelease()
{
  Recording recording;
  Viewer viewer(37, 10, metadata());
  recording.attach(viewer);
  makeReady(viewer, recording);
  makeFocused(viewer, recording);
  QKeyEvent shiftedKey(QEvent::KeyPress, Qt::Key_A, Qt::ShiftModifier, "A");
  QCoreApplication::sendEvent(&viewer, &shiftedKey);
  check(
      recording.last(MessageType::Input, "key").object.value("modifiers").toInt() == Qt::ShiftModifier,
      "Modifier press was not delivered"
  );
  focus(viewer, false);
  const auto release = recording.last(MessageType::Input, "key").object;
  check(
      !release.value("down").toBool() && release.value("modifiers").toInt(-1) == 0,
      "Focus-loss release retained the modifier-down flags"
  );
  viewer.disconnected();
}

void testFocusRevocation()
{
  Recording recording;
  Viewer viewer(38, 11, metadata());
  recording.attach(viewer);
  makeReady(viewer, recording);
  makeFocused(viewer, recording);
  const auto firstRequest = recording.focusRequest();
  key(viewer, QEvent::KeyPress, Qt::Key_A, "a");
  mouse(viewer, QEvent::MouseButtonPress, viewer.videoRect().center());
  const auto beforeRevoke = recording.events.size();
  viewer.focusAcknowledged(0, firstRequest + 1);
  check(recording.events.size() == beforeRevoke, "Unrelated focus revocation affected the active lease");
  viewer.focusAcknowledged(0, firstRequest);
  check(
      recording.events.size() == beforeRevoke + 3 && recording.events[beforeRevoke].object.value("kind") == "key" &&
          !recording.events[beforeRevoke].object.value("down").toBool() &&
          recording.events[beforeRevoke + 1].object.value("kind") == "button" &&
          !recording.events[beforeRevoke + 1].object.value("down").toBool() &&
          recording.events[beforeRevoke + 2].object.value("kind") == "release",
      "Revocation did not release owned input before cancelling the lease"
  );
  const auto afterRevoke = recording.events.size();
  viewer.focusAcknowledged(0, firstRequest);
  viewer.focusAcknowledged(7, firstRequest);
  key(viewer, QEvent::KeyPress, Qt::Key_A, "a", true);
  key(viewer, QEvent::KeyRelease, Qt::Key_A, "a");
  mouse(viewer, QEvent::MouseButtonRelease, viewer.videoRect().center(), Qt::LeftButton, Qt::NoButton);
  mouse(viewer, QEvent::MouseMove, viewer.videoRect().center(), Qt::NoButton, Qt::NoButton);
  viewer.repaint();
  focus(viewer, true);
  viewer.committed();
  QCoreApplication::processEvents();
  check(
      recording.events.size() == afterRevoke,
      "Passive paint, pointer movement, stale ack or held input reclaimed a revoked lease"
  );

  // An intentional click creates a fresh request and remains queued until its
  // acknowledgement; a late revocation of the previous request cannot erase it.
  const int buttons = recording.count(MessageType::Input, "button");
  mouse(viewer, QEvent::MouseButtonPress, viewer.videoRect().center());
  mouse(viewer, QEvent::MouseButtonRelease, viewer.videoRect().center(), Qt::LeftButton, Qt::NoButton);
  const auto secondRequest = recording.focusRequest();
  check(
      secondRequest > firstRequest && recording.count(MessageType::Input, "button") == buttons,
      "Reclaiming click skipped the fresh focus handshake"
  );
  viewer.focusAcknowledged(0, firstRequest);
  viewer.focusAcknowledged(8, secondRequest);
  check(
      recording.count(MessageType::Input, "button") == buttons + 2 &&
          recording.last(MessageType::Input, "button").object.value("focusRevision").toInteger() == 8,
      "Reclaiming click was lost or sent with the revoked lease"
  );

  viewer.focusAcknowledged(0, secondRequest);
  const int keys = recording.count(MessageType::Input, "key");
  key(viewer, QEvent::KeyPress, Qt::Key_B, "b");
  const auto thirdRequest = recording.focusRequest();
  check(
      thirdRequest > secondRequest && recording.count(MessageType::Input, "key") == keys,
      "Deliberate key did not queue behind a fresh focus request"
  );
  viewer.focusAcknowledged(9, thirdRequest);
  key(viewer, QEvent::KeyRelease, Qt::Key_B, "b");
  check(recording.count(MessageType::Input, "key") == keys + 2, "Keyboard focus reclaim lost its paired input");
  viewer.focusAcknowledged(0, secondRequest);
  mouse(viewer, QEvent::MouseMove, viewer.videoRect().center(), Qt::NoButton, Qt::NoButton);
  check(
      recording.last(MessageType::Input, "move").object.value("focusRevision").toInteger() == 9,
      "Stale revocation cancelled a newer acknowledged lease"
  );

  viewer.focusAcknowledged(0, thirdRequest);
  QWheelEvent wheel(
      viewer.videoRect().center(), viewer.videoRect().center(), {}, {0, 120}, Qt::NoButton, Qt::NoModifier,
      Qt::NoScrollPhase, false
  );
  QCoreApplication::sendEvent(&viewer, &wheel);
  const auto fourthRequest = recording.focusRequest();
  check(
      fourthRequest > thirdRequest && recording.count(MessageType::Input, "scroll") == 0,
      "Deliberate scroll did not queue behind a fresh focus request"
  );
  viewer.focusAcknowledged(10, fourthRequest);
  check(
      recording.count(MessageType::Input, "scroll") == 1 &&
          recording.last(MessageType::Input, "scroll").object.value("focusRevision").toInteger() == 10,
      "Scroll focus reclaim lost its input"
  );
  viewer.disconnected();
}

void testResizeAndScroll()
{
  Recording recording;
  Viewer viewer(33, 6, metadata());
  recording.attach(viewer);
  makeReady(viewer, recording);
  makeFocused(viewer, recording);
  key(viewer, QEvent::KeyPress, Qt::Key_A, "a");
  viewer.resize(420, 315);
  check(waitUntil([&] { return recording.count(MessageType::Resize) == 1; }), "Resize was not requested");
  check(!recording.last(MessageType::Input, "key").object.value("down").toBool(), "Resize stranded a pressed key");
  const auto buttonCount = recording.count(MessageType::Input, "button");
  mouse(viewer, QEvent::MouseButtonPress, viewer.videoRect().center());
  check(recording.count(MessageType::Input, "button") == buttonCount, "Input crossed an unacknowledged resize");
  auto updated = metadata(2, 800, 600);
  updated.insert("resizeRequest", recording.last(MessageType::Resize).object.value("request"));
  viewer.applyMetadata(updated);
  viewer.applyMetadata(metadata());
  check(viewer.geometryRevision() == 2, "Stale metadata rolled back geometry");
  const int focusCount = recording.count(MessageType::Input, "focus");
  viewer.receiveFrame(frame(), 1);
  QCoreApplication::processEvents();
  viewer.repaint();
  QCoreApplication::processEvents();
  check(
      recording.count(MessageType::Input, "focus") == focusCount,
      "Old encoded frame arriving after metadata enabled resized input"
  );
  viewer.receiveFrame(frame(), 2);
  check(
      waitUntil([&] { return recording.count(MessageType::Input, "focus") > focusCount; }),
      "Resized frame did not establish a new focus request"
  );
  viewer.focusAcknowledged(9, recording.focusRequest());
  QWheelEvent wheel(
      viewer.videoRect().center(), viewer.videoRect().center(), {}, {0, 120}, Qt::NoButton, Qt::NoModifier,
      Qt::NoScrollPhase, false
  );
  QCoreApplication::sendEvent(&viewer, &wheel);
  const auto scroll = recording.last(MessageType::Input, "scroll").object;
  check(
      near(scroll.value("dy").toDouble(), 40) && near(scroll.value("x").toDouble(), 400) &&
          near(scroll.value("y").toDouble(), 300) && scroll.value("geometry").toInteger() == 2,
      "Wheel normalization used stale geometry or units"
  );
  viewer.showMinimized();
  QCoreApplication::processEvents();
  check(
      !recording.last(MessageType::Input, "visibility").object.value("visible").toBool(),
      "Minimize did not notify capture priority"
  );
  viewer.disconnected();
  const auto messages = recording.events.size();
  viewer.close();
  check(recording.events.size() == messages, "Disconnected viewer wrote to a dead channel");
}

void testBoundedDecodeQueue()
{
  Recording recording;
  Viewer viewer(34, 7, metadata());
  recording.attach(viewer);
  for (int i = 0; i < 17; ++i)
    viewer.receiveFrame(frame(), 1);
  check(
      recording.count(MessageType::Return) == 1 && !recording.errors.isEmpty(),
      "Decoder backlog was not bounded with safe return"
  );
  check(!viewer.firstFramePainted(), "Backlogged stream became ready without painting");
  viewer.disconnected();
}

void testQueuedFrameRevision()
{
  Recording recording;
  Viewer viewer(35, 8, metadata());
  recording.attach(viewer);
  viewer.show();
  QCoreApplication::processEvents();
  viewer.receiveFrame(frame(), 1);
  // Change metadata while the first decode is still queued. The next malformed
  // access unit is a worker barrier: its error arrives after that old decode.
  viewer.applyMetadata(metadata(2, 800, 600));
  viewer.receiveFrame("not an Annex B access unit", 2);
  check(waitUntil([&] { return !recording.errors.isEmpty(); }), "Decode worker did not report malformed frame");
  QCoreApplication::processEvents();
  check(
      !viewer.firstFramePainted() && recording.count(MessageType::Ready) == 0,
      "Decode queued before resize was painted as current geometry"
  );
  viewer.receiveFrame(frame(), 2);
  check(waitUntil([&] { return viewer.firstFramePainted(); }), "Current geometry did not recover after stale decode");
  check(
      recording.last(MessageType::Ready).object.value("geometry").toInteger() == 2,
      "First frame readiness acknowledged the wrong geometry"
  );
  viewer.disconnected();
}

void testKeyboardRouteFenceAndResume()
{
  Recording recording;
  Viewer viewer(41, 14, metadata());
  recording.attach(viewer);
  makeReady(viewer, recording);
  makeFocused(viewer, recording);
  QKeyEvent shift(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
  QCoreApplication::sendEvent(&viewer, &shift);
  const auto request = originalRoute(viewer, recording);
  const auto before = recording.events.size();
  const auto focusCount = recording.count(MessageType::Input, "focus");
  viewer.keyboardRoute(request);
  check(recording.lastRoute().result == KeyboardRouteResult::Ready, "Keyboard fence was not acknowledged");
  check(
      recording.events.size() == before + 1 && recording.routes.back().inputCount == before + 1 &&
          recording.events.back().object.value("kind") == "key" &&
          recording.events.back().object.value("code").toInt() == Qt::Key_Shift &&
          !recording.events.back().object.value("down").toBool() &&
          recording.events.back().object.value("modifiers").toInt(-1) == 0 &&
          recording.events.back().object.value("focusRevision").toInteger() == 7,
      "Held Shift was not released with its owner before the route acknowledgment"
  );

  mouse(viewer, QEvent::MouseMove, viewer.videoRect().center(), Qt::NoButton, Qt::NoButton);
  mouse(viewer, QEvent::MouseButtonPress, viewer.videoRect().center());
  mouse(viewer, QEvent::MouseButtonRelease, viewer.videoRect().center(), Qt::LeftButton, Qt::NoButton);
  check(
      recording.count(MessageType::Input, "button") == 2 && recording.count(MessageType::Input, "release") == 0 &&
          recording.count(MessageType::Input, "focus") == focusCount &&
          recording.last(MessageType::Input, "button").object.value("focusRevision").toInteger() == 7,
      "Quiescence released pointer ownership or replaced the focus lease"
  );
  const int keys = recording.count(MessageType::Input, "key");
  key(viewer, QEvent::KeyPress, Qt::Key_B, "b");
  key(viewer, QEvent::KeyPress, Qt::Key_Tab, "\t");
  key(viewer, QEvent::KeyRelease, Qt::Key_Tab, "\t");
  key(viewer, QEvent::KeyPress, Qt::Key_Shift, {}, true);
  key(viewer, QEvent::KeyRelease, Qt::Key_Shift, {}, true);
  QKeyEvent shortcut(QEvent::ShortcutOverride, Qt::Key_B, Qt::ControlModifier);
  shortcut.ignore();
  QCoreApplication::sendEvent(&viewer, &shortcut);
  check(shortcut.isAccepted(), "Fenced shortcut escaped into local Qt shortcut handling");
  check(recording.count(MessageType::Input, "key") == keys, "Fenced input or repeats leaked to the host");

  // A key arriving synchronously during the resume send remains fenced until
  // the acknowledgement has actually been accepted by the transport.
  const auto routeSend = viewer.onKeyboardRouteSend;
  viewer.onKeyboardRouteSend = [&](const KeyboardRouteControl &control) {
    const bool result = routeSend(control);
    if (control.action == KeyboardRouteAction::Acknowledge && control.mode == KeyboardRouteMode::Viewer &&
        control.result == KeyboardRouteResult::Ready)
      key(viewer, QEvent::KeyPress, Qt::Key_Z, "z");
    return result;
  };
  viewer.keyboardRoute(resumeRoute(request));
  check(recording.lastRoute().result == KeyboardRouteResult::Ready, "Exact keyboard resume was rejected");
  key(viewer, QEvent::KeyPress, Qt::Key_B, "b", true);
  key(viewer, QEvent::KeyPress, Qt::Key_B, "b");
  key(viewer, QEvent::KeyPress, Qt::Key_Z, "z", true);
  key(viewer, QEvent::KeyRelease, Qt::Key_B, "b");
  key(viewer, QEvent::KeyRelease, Qt::Key_Z, "z");
  key(viewer, QEvent::KeyRelease, Qt::Key_Shift);
  check(recording.count(MessageType::Input, "key") == keys, "Resume replayed a held key or its orphan up");
  const auto acknowledgedAt = recording.routes.back().inputCount;
  key(viewer, QEvent::KeyPress, Qt::Key_B, "b");
  key(viewer, QEvent::KeyRelease, Qt::Key_B, "b");
  check(
      recording.count(MessageType::Input, "key") == keys + 2 && recording.events.size() == acknowledgedAt + 2,
      "Fresh paired input did not follow the successful resume acknowledgment"
  );
  viewer.disconnected();
}

void testBoundedMediaRecovery()
{
  Recording recording;
  Viewer viewer(811, 37, metadata());
  recording.attach(viewer);
  viewer.setMediaRecoveryEnabled(true);
  makeReady(viewer, recording);
  makeFocused(viewer, recording);
  key(viewer, QEvent::KeyPress, Qt::Key_A, "a");
  const auto delivered = recording.count(MessageType::Input, "key");
  check(delivered > 0, "Recovery test did not establish a held keyboard owner");
  // Queue the overload without processing GUI completion events. The original
  // implementation returned the entire window on its 17th incoming access
  // unit. Negotiated media recovery must keep the window, revoke input and
  // discard the old GOP before requesting a self-contained replacement.
  for (int i = 0; i < 17; ++i)
    viewer.receiveFrame(frame(), viewer.geometryRevision());
  check(viewer.videoRecovering(), "Decoder overflow did not enter the bounded recovery state");
  check(recording.count(MessageType::Return) == 0, "Decoder overload returned the window prematurely");
  check(recording.count(MessageType::Input, "key") > delivered,
        "Decoder overload did not release the exact held keyboard owner");
  key(viewer, QEvent::KeyPress, Qt::Key_B, "b");
  mouse(viewer, QEvent::MouseButtonPress, viewer.videoRect().center());
  check(recording.count(MessageType::Input, "key") == delivered + 1,
        "A new press escaped through a suspended video/input route");
  check(waitUntil([&] { return recording.count(MessageType::Input, "video-recovery") == 1; }),
        "Draining the decoder queue did not send exactly one recovery request");
  const auto &requested = recording.last(MessageType::Input, "video-recovery");
  check(requested.session == 811 && requested.epoch == 37 &&
            requested.object.value("geometry").toInteger() == 1 &&
            requested.object.value("request").toInteger() == 1,
        "Recovery request lost its session, geometry or monotonic identity");
  viewer.receiveFrame(QByteArray::fromHex("000000014188"), viewer.geometryRevision());
  check(viewer.videoRecovering() && recording.count(MessageType::Input, "video-recovery") == 1,
        "Dependent frame bypassed recovery or triggered a repeat request");
  viewer.receiveFrame(frame(), viewer.geometryRevision());
  check(waitUntil([&] { return !viewer.videoRecovering(); }),
        "A self-contained keyframe did not reset, decode and paint after the drain");
  check(recording.count(MessageType::Return) == 0 &&
            recording.count(MessageType::Input, "video-recovery") == 1,
        "Completed recovery returned the window or repeated its one-shot request");
  // Input must remain blocked until fresh user intent reestablishes focus.
  const auto beforeFocus = recording.count(MessageType::Input, "focus");
  QCoreApplication::processEvents();
  check(recording.count(MessageType::Input, "focus") == beforeFocus,
        "Recovered paint silently reused stale input focus");
  viewer.close();
}

void testMediaPauseOnHiddenViewer()
{
  Recording recording;
  Viewer viewer(833, 11, metadata());
  recording.attach(viewer);
  viewer.setMediaRecoveryEnabled(true);
  makeReady(viewer, recording);
  makeFocused(viewer, recording);
  key(viewer, QEvent::KeyPress, Qt::Key_A, "a");
  const int sentKeys = recording.count(MessageType::Input, "key");
  viewer.hide();
  check(viewer.videoPaused(), "Hidden negotiated Viewer did not suspend decoding");
  check(recording.count(MessageType::Input, "key") == sentKeys + 1,
        "Hiding a Viewer did not release exactly its held key");
  check(recording.count(MessageType::Input, "video-recovery") == 0,
        "Hidden Viewer began a timed IDR recovery before visibility returned");
  for (int i = 0; i < 64; ++i)
    viewer.receiveFrame(frame(), viewer.geometryRevision());
  QCoreApplication::processEvents();
  check(recording.count(MessageType::Return) == 0 && viewer.videoPaused(),
        "Hidden frame burst consumed decoding capacity or returned source");
  viewer.applyMetadata(metadata(2, 800, 600));
  check(viewer.videoPaused() && viewer.geometryRevision() == 2,
        "A hidden geometry update reopened unpainted input authority");
  viewer.show();
  check(waitUntil([&] { return recording.count(MessageType::Input, "video-recovery") == 1; }),
        "Resuming hidden Viewer did not request exactly one post-drain IDR");
  check(!viewer.videoPaused() && viewer.videoRecovering(),
        "Restored visibility bypassed decoder/input recovery fence");
  check(recording.last(MessageType::Input, "video-recovery").object.value("geometry").toInteger() == 2,
        "Visibility recovery reused a stale source geometry revision");
  viewer.receiveFrame(frame(), 1);
  check(viewer.videoRecovering(), "Stale pre-minimize source video reopened input authority");
  key(viewer, QEvent::KeyPress, Qt::Key_B, "b");
  check(recording.count(MessageType::Input, "key") == sentKeys + 1,
        "Input leaked before the resumed IDR was painted");
  viewer.receiveFrame(frame(), viewer.geometryRevision());
  check(waitUntil([&] { return !viewer.videoRecovering(); }),
        "Resumed hidden Viewer did not decode and paint independent keyframe");
  check(recording.count(MessageType::Return) == 0 &&
            recording.count(MessageType::Input, "video-recovery") == 1,
        "Resumed window returned source or requested repeated IDRs");
  viewer.close();
}

void testKeyboardRouteBusyAndGenerations()
{
  Recording recording;
  Viewer viewer(42, 15, metadata());
  recording.attach(viewer);
  makeReady(viewer, recording);
  makeFocused(viewer, recording);
  auto request = originalRoute(viewer, recording);
  mouse(viewer, QEvent::MouseButtonPress, viewer.videoRect().center());
  const auto before = recording.events.size();
  viewer.keyboardRoute(request);
  check(
      recording.lastRoute().result == KeyboardRouteResult::Busy && recording.events.size() == before,
      "Held mouse button was released to force a keyboard handoff"
  );
  mouse(viewer, QEvent::MouseButtonRelease, viewer.videoRect().center(), Qt::LeftButton, Qt::NoButton);
  check(recording.count(MessageType::Input, "button") == 2, "Busy response lost the held button owner");
  viewer.keyboardRoute(request);
  check(recording.lastRoute().result == KeyboardRouteResult::Stale, "Busy generation was reused after mouse up");
  request.binding.generation = 2;
  viewer.keyboardRoute(request);
  check(recording.lastRoute().result == KeyboardRouteResult::Ready, "Fresh generation did not quiesce");
  viewer.keyboardRoute(request);
  check(recording.lastRoute().result == KeyboardRouteResult::Ready, "Exact valid quiescence was not idempotent");
  auto stale = resumeRoute(request);
  stale.binding.generation = 1;
  viewer.keyboardRoute(stale);
  check(recording.lastRoute().result == KeyboardRouteResult::Stale, "Old generation resumed a newer fence");
  stale = resumeRoute(request);
  ++stale.binding.focusRevision;
  viewer.keyboardRoute(stale);
  check(recording.lastRoute().result == KeyboardRouteResult::Stale, "Wrong focus binding resumed keyboard input");
  const int keys = recording.count(MessageType::Input, "key");
  key(viewer, QEvent::KeyPress, Qt::Key_C, "c");
  key(viewer, QEvent::KeyRelease, Qt::Key_C, "c");
  check(recording.count(MessageType::Input, "key") == keys, "Rejected resume reopened the keyboard gate");
  auto later = request;
  later.binding.generation = 3;
  viewer.keyboardRoute(later);
  check(recording.lastRoute().result == KeyboardRouteResult::Busy, "New attempt replaced an existing fence");
  viewer.keyboardRoute(resumeRoute(request));
  check(recording.lastRoute().result == KeyboardRouteResult::Ready, "Later rejected attempt stranded the old fence");
  viewer.keyboardRoute(later);
  check(recording.lastRoute().result == KeyboardRouteResult::Stale, "Rejected later attempt was not remembered");
  later.binding.generation = 4;
  viewer.keyboardRoute(later);
  viewer.keyboardRoute(resumeRoute(request));
  check(recording.lastRoute().result == KeyboardRouteResult::Stale, "Retired resume reopened a fresh route");
  key(viewer, QEvent::KeyPress, Qt::Key_D, "d");
  key(viewer, QEvent::KeyRelease, Qt::Key_D, "d");
  check(recording.count(MessageType::Input, "key") == keys, "Retired resume allowed new key forwarding");
  viewer.disconnected();
}

void testKeyboardRoutePendingFocus(ForwardingScope scope = ForwardingScope::Keyboard, bool resumeImmediately = false)
{
  Recording recording;
  Viewer viewer(43, 16, metadata());
  recording.attach(viewer);
  makeReady(viewer, recording);
  viewer.committed();
  focus(viewer, true);
  auto request = originalRoute(viewer, recording);
  request.binding.scope = scope;
  viewer.keyboardRoute(request);
  check(recording.lastRoute().result == KeyboardRouteResult::Stale, "Pending focus accepted a keyboard lease");
  ++request.binding.generation;
  key(viewer, QEvent::KeyPress, Qt::Key_A, "a");
  key(viewer, QEvent::KeyPress, Qt::Key_B, "b");
  key(viewer, QEvent::KeyRelease, Qt::Key_B, "b");
  mouse(viewer, QEvent::MouseButtonPress, viewer.videoRect().center());
  mouse(viewer, QEvent::MouseButtonRelease, viewer.videoRect().center(), Qt::LeftButton, Qt::NoButton);
  QWheelEvent wheel(
      viewer.videoRect().center(), viewer.videoRect().center(), {}, {0, 120}, Qt::NoButton, Qt::NoModifier,
      Qt::NoScrollPhase, false
  );
  QCoreApplication::sendEvent(&viewer, &wheel);
  const auto send = viewer.onSend;
  bool quiesced = false;
  viewer.onSend = [&](MessageType type, const QJsonObject &object, std::uint64_t session, std::uint64_t epoch) {
    const bool sent = send(type, object, session, epoch);
    if (!quiesced && type == MessageType::Input && object.value("kind") == "key" && object.value("down").toBool()) {
      quiesced = true;
      viewer.keyboardRoute(request);
      if (resumeImmediately)
        viewer.keyboardRoute(resumeRoute(request));
    }
    return sent;
  };
  viewer.focusAcknowledged(7, recording.focusRequest());
  check(quiesced && recording.lastRoute().result == KeyboardRouteResult::Ready, "Pending flush did not quiesce");
  check(
      recording.count(MessageType::Input, "key") == 2 &&
          !recording.last(MessageType::Input, "key").object.value("down").toBool() &&
          recording.last(MessageType::Input, "key").object.value("code").toInt() == Qt::Key_A &&
          recording.routes.back().inputCount == 4,
      "Reentrant quiescence failed to release the in-flight key before acknowledgment"
  );
  const bool all = scope == ForwardingScope::All;
  check(
      recording.count(MessageType::Input, "button") == (all ? 0 : 2) &&
          recording.count(MessageType::Input, "scroll") == (all ? 0 : 1) &&
          recording.count(MessageType::Input, "release") == 0,
      "Forwarding scope did not govern the already-detached pending-focus queue"
  );
  viewer.keyboardRoute(resumeRoute(request));
  key(viewer, QEvent::KeyPress, Qt::Key_A, "a", true);
  key(viewer, QEvent::KeyRelease, Qt::Key_A, "a");
  check(recording.count(MessageType::Input, "key") == 2, "Queued held key replayed after resume");
  viewer.disconnected();
}

void testFullInputFenceAndResume()
{
  Recording recording;
  Viewer viewer(47, 20, metadata());
  recording.attach(viewer);
  makeReady(viewer, recording);
  makeFocused(viewer, recording);
  auto request = originalRoute(viewer, recording);
  request.binding.scope = ForwardingScope::All;
  const auto position = viewer.videoRect().center();
  mouse(viewer, QEvent::MouseButtonPress, position);
  const auto held = recording.events.size();
  viewer.keyboardRoute(request);
  check(
      recording.lastRoute().result == KeyboardRouteResult::Busy && recording.events.size() == held,
      "Full handoff interrupted an existing button owner"
  );
  mouse(viewer, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
  viewer.keyboardRoute(request);
  check(recording.lastRoute().result == KeyboardRouteResult::Stale, "Full busy generation was reusable");
  ++request.binding.generation;
  key(viewer, QEvent::KeyPress, Qt::Key_Shift);
  viewer.keyboardRoute(request);
  check(
      recording.lastRoute().result == KeyboardRouteResult::Ready && recording.lastRoute().binding == request.binding,
      "Full handoff did not acknowledge the exact scope"
  );
  check(
      recording.last(MessageType::Input, "key").object["down"] == false,
      "Full handoff did not release a delivered key before acknowledging"
  );
  const auto before = recording.events.size();
  const auto scroll = [&] {
    QWheelEvent event(position, position, {3, -4}, {120, -120}, Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(&viewer, &event);
  };
  key(viewer, QEvent::KeyPress, Qt::Key_A, "a");
  key(viewer, QEvent::KeyRelease, Qt::Key_A, "a");
  mouse(viewer, QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton);
  mouse(viewer, QEvent::MouseButtonPress, position);
  mouse(viewer, QEvent::MouseButtonDblClick, position);
  scroll();
  check(recording.events.size() == before, "Full fence leaked key, button, motion or either scroll axis");

  auto partial = resumeRoute(request);
  partial.binding.scope = ForwardingScope::Keyboard;
  viewer.keyboardRoute(partial);
  check(recording.lastRoute().result == KeyboardRouteResult::Stale, "Keyboard scope resumed a full-input fence");
  scroll();
  check(recording.events.size() == before, "Mismatched scope reopened pointer forwarding");
  const auto routeSend = viewer.onKeyboardRouteSend;
  viewer.onKeyboardRouteSend = [&](const KeyboardRouteControl &control) {
    const bool sent = routeSend(control);
    if (control.mode == KeyboardRouteMode::Viewer && control.result == KeyboardRouteResult::Ready) {
      mouse(viewer, QEvent::MouseButtonPress, position, Qt::RightButton, Qt::LeftButton | Qt::RightButton);
      scroll();
    }
    return sent;
  };
  viewer.keyboardRoute(resumeRoute(request));
  check(recording.lastRoute().result == KeyboardRouteResult::Ready, "Exact full-input resume failed");
  // Both presses began while the fence was closed, including one from inside
  // the resume callback. Their drags, repeat downs and physical ups stay orphaned.
  mouse(viewer, QEvent::MouseMove, position, Qt::NoButton, Qt::LeftButton | Qt::RightButton);
  mouse(viewer, QEvent::MouseButtonPress, position);
  scroll();
  mouse(viewer, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::RightButton);
  scroll();
  mouse(viewer, QEvent::MouseButtonRelease, position, Qt::RightButton, Qt::NoButton);
  key(viewer, QEvent::KeyRelease, Qt::Key_Shift);
  check(recording.events.size() == before, "Resume replayed fenced pointer input or lost a held-button tombstone");
  mouse(viewer, QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton);
  mouse(viewer, QEvent::MouseButtonPress, position);
  mouse(viewer, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
  scroll();
  check(
      recording.events.size() == before + 4 && recording.events.back().object["kind"] == "scroll",
      "Neutral pointer input did not resume after the exact acknowledgement"
  );
  viewer.disconnected();
}

void testFullInputFenceInvalidation()
{
  Recording recording;
  Viewer viewer(48, 21, metadata());
  recording.attach(viewer);
  makeReady(viewer, recording);
  makeFocused(viewer, recording);
  auto request = originalRoute(viewer, recording);
  request.binding.scope = ForwardingScope::All;
  viewer.keyboardRoute(request);
  const auto position = viewer.videoRect().center();
  mouse(viewer, QEvent::MouseButtonPress, position);
  viewer.focusAcknowledged(0, recording.focusRequest());
  check(
      recording.lastRoute().action == KeyboardRouteAction::Withdraw && recording.lastRoute().binding == request.binding,
      "Full invalidation lost its exact scope"
  );
  const auto before = recording.events.size();
  focus(viewer, true);
  mouse(viewer, QEvent::MouseMove, position, Qt::NoButton, Qt::LeftButton);
  mouse(viewer, QEvent::MouseButtonPress, position, Qt::RightButton, Qt::LeftButton | Qt::RightButton);
  check(recording.events.size() == before, "Focus changes reopened an invalidated full-input fence");
  viewer.keyboardRoute(request);
  check(recording.lastRoute().result == KeyboardRouteResult::Stale, "Invalidated full-input fence was revived");
  viewer.keyboardRoute(resumeRoute(request));
  mouse(viewer, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::RightButton);
  mouse(viewer, QEvent::MouseButtonRelease, position, Qt::RightButton, Qt::NoButton);
  check(recording.events.size() == before, "Invalidation lost pointer tombstones before physical ups");
  mouse(viewer, QEvent::MouseButtonPress, position);
  check(
      recording.events.size() == before + 1 && recording.events.back().object["kind"] == "focus",
      "Fresh click did not request focus after full-input resume"
  );
  viewer.focusAcknowledged(8, recording.focusRequest());
  mouse(viewer, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
  check(recording.count(MessageType::Input, "button") == 2, "Fresh post-resume click did not retain its owner");
  viewer.disconnected();
}

void testKeyboardRouteBindingValidation()
{
  Recording recording;
  Viewer viewer(46, 19, metadata());
  recording.attach(viewer);
  makeReady(viewer, recording);
  makeFocused(viewer, recording);
  auto request = originalRoute(viewer, recording);
  auto foreign = request;
  ++foreign.binding.owner.epoch;
  viewer.keyboardRoute(foreign);
  auto malformed = request;
  malformed.binding.generation = 0;
  viewer.keyboardRoute(malformed);
  malformed = request;
  malformed.action = KeyboardRouteAction::Acknowledge;
  malformed.result = KeyboardRouteResult::Ready;
  viewer.keyboardRoute(malformed);
  check(recording.routes.empty(), "Malformed or foreign route reached the send callback");

  for (int mismatch = 0; mismatch < 3; ++mismatch) {
    auto stale = request;
    if (mismatch == 0)
      ++stale.binding.focusRequest;
    else if (mismatch == 1)
      ++stale.binding.focusRevision;
    else
      ++stale.binding.geometry;
    viewer.keyboardRoute(stale);
    check(recording.lastRoute().result == KeyboardRouteResult::Stale, "Mismatched focus/geometry accepted a route");
    ++request.binding.generation;
  }
  key(viewer, QEvent::KeyPress, Qt::Key_A, "a");
  key(viewer, QEvent::KeyRelease, Qt::Key_A, "a");
  check(recording.count(MessageType::Input, "key") == 2, "Rejected route fenced normal keyboard forwarding");
  viewer.keyboardRoute(request);
  check(recording.lastRoute().result == KeyboardRouteResult::Ready, "Current binding was not accepted");

  // A resize acknowledgment does not by itself make the old painted frame safe
  // for a fresh route. The old fence can still be explicitly resumed.
  viewer.applyMetadata(metadata(2, 800, 600));
  check(
      recording.lastRoute().action == KeyboardRouteAction::Withdraw &&
          recording.lastRoute().reason == KeyboardRouteReason::GeometryChanged,
      "Source geometry change did not withdraw the route"
  );
  viewer.keyboardRoute(resumeRoute(request));
  ++request.binding.generation;
  request.binding.geometry = 2;
  viewer.keyboardRoute(request);
  check(recording.lastRoute().result == KeyboardRouteResult::Stale, "Unpainted geometry accepted a new route");
  viewer.disconnected();
}

void testKeyboardRouteInvalidation()
{
  for (const auto reason :
       {KeyboardRouteReason::FocusChanged, KeyboardRouteReason::GeometryChanged, KeyboardRouteReason::Hidden,
        KeyboardRouteReason::Returned, KeyboardRouteReason::Disconnected}) {
    Recording recording;
    Viewer viewer(44, 17, metadata());
    recording.attach(viewer);
    makeReady(viewer, recording);
    makeFocused(viewer, recording);
    const auto request = originalRoute(viewer, recording);
    viewer.keyboardRoute(request);
    key(viewer, QEvent::KeyPress, Qt::Key_C, "c");
    if (reason == KeyboardRouteReason::FocusChanged)
      viewer.focusAcknowledged(0, recording.focusRequest());
    else if (reason == KeyboardRouteReason::GeometryChanged)
      viewer.resize(420, 315);
    else if (reason == KeyboardRouteReason::Hidden) {
      QEvent hidden(QEvent::Hide);
      QCoreApplication::sendEvent(&viewer, &hidden);
    } else if (reason == KeyboardRouteReason::Returned)
      viewer.close();
    else
      viewer.disconnected();

    if (reason == KeyboardRouteReason::Disconnected) {
      check(recording.routes.size() == 1, "Disconnect tried to withdraw over a dead transport");
      const auto before = recording.events.size();
      viewer.keyboardRoute(resumeRoute(request));
      key(viewer, QEvent::KeyPress, Qt::Key_D, "d");
      viewer.close();
      check(recording.events.size() == before && recording.routes.size() == 1, "Disconnected route sent input");
      continue;
    }
    check(
        recording.routes.size() == 2 && recording.lastRoute().action == KeyboardRouteAction::Withdraw &&
            recording.lastRoute().binding == request.binding && recording.lastRoute().reason == reason,
        "Route invalidation did not withdraw its exact binding and reason"
    );
    const int keys = recording.count(MessageType::Input, "key");
    viewer.keyboardRoute(request);
    check(recording.lastRoute().result == KeyboardRouteResult::Stale, "Invalidated route was acknowledged again");

    if (reason == KeyboardRouteReason::GeometryChanged) {
      check(waitUntil([&] { return recording.count(MessageType::Resize) == 1; }), "Route resize was not requested");
      auto next = metadata(2, 800, 600);
      next.insert("resizeRequest", recording.last(MessageType::Resize).object.value("request"));
      viewer.applyMetadata(next);
      const int focuses = recording.count(MessageType::Input, "focus");
      viewer.receiveFrame(frame(), 2);
      check(
          waitUntil([&] { return recording.count(MessageType::Input, "focus") > focuses; }),
          "Resized route did not repaint before reclaiming focus"
      );
      viewer.focusAcknowledged(8, recording.focusRequest());
    } else if (reason != KeyboardRouteReason::Returned) {
      if (reason == KeyboardRouteReason::Hidden) {
        QEvent shown(QEvent::Show);
        QCoreApplication::sendEvent(&viewer, &shown);
      }
      focus(viewer, true);
      mouse(viewer, QEvent::MouseButtonPress, viewer.videoRect().center());
      mouse(viewer, QEvent::MouseButtonRelease, viewer.videoRect().center(), Qt::LeftButton, Qt::NoButton);
      viewer.focusAcknowledged(8, recording.focusRequest());
    }
    key(viewer, QEvent::KeyPress, Qt::Key_D, "d");
    key(viewer, QEvent::KeyRelease, Qt::Key_D, "d");
    check(recording.count(MessageType::Input, "key") == keys, "Focus, show or resize reopened the keyboard fence");
    viewer.keyboardRoute(resumeRoute(request));
    check(
        recording.lastRoute().result == KeyboardRouteResult::Ready,
        "Exact host resume could not clear an invalidated focus/geometry binding"
    );
    key(viewer, QEvent::KeyPress, Qt::Key_C, "c", true);
    key(viewer, QEvent::KeyRelease, Qt::Key_C, "c");
    check(recording.count(MessageType::Input, "key") == keys, "Invalidation erased held-key suppression");
    if (reason != KeyboardRouteReason::Returned) {
      key(viewer, QEvent::KeyPress, Qt::Key_D, "d");
      key(viewer, QEvent::KeyRelease, Qt::Key_D, "d");
      check(
          recording.count(MessageType::Input, "key") == keys + 2, "Exact resume did not restore fresh keyboard input"
      );
    }
    viewer.disconnected();
  }
}

void testKeyboardRouteSendFailure(ForwardingScope scope = ForwardingScope::Keyboard)
{
  for (int failure = 0; failure < 5; ++failure) {
    Recording recording;
    Viewer viewer(45, 18, metadata());
    recording.attach(viewer);
    makeReady(viewer, recording);
    makeFocused(viewer, recording);
    auto request = originalRoute(viewer, recording);
    request.binding.scope = scope;
    if (failure == 0) {
      key(viewer, QEvent::KeyPress, Qt::Key_Shift);
      const auto send = viewer.onSend;
      viewer.onSend = [=](MessageType type, const QJsonObject &object, std::uint64_t session, std::uint64_t epoch) {
        send(type, object, session, epoch);
        return false;
      };
      viewer.keyboardRoute(request);
      check(recording.routes.empty(), "Failed key-up send still acknowledged a ready original route");
    } else if (failure == 3) {
      viewer.onKeyboardRouteSend = {};
      viewer.keyboardRoute(request);
    } else {
      if (failure == 2)
        viewer.keyboardRoute(request);
      const auto send = viewer.onKeyboardRouteSend;
      viewer.onKeyboardRouteSend = [=](const KeyboardRouteControl &control) {
        send(control);
        if (failure == 4)
          throw std::runtime_error("Fixture route send failed");
        return false;
      };
      viewer.keyboardRoute(failure == 1 ? request : resumeRoute(request));
    }
    const auto before = recording.events.size();
    const auto routes = recording.routes.size();
    key(viewer, QEvent::KeyPress, Qt::Key_A, "a");
    key(viewer, QEvent::KeyRelease, Qt::Key_A, "a");
    mouse(viewer, QEvent::MouseButtonPress, viewer.videoRect().center());
    mouse(viewer, QEvent::MouseButtonRelease, viewer.videoRect().center(), Qt::LeftButton, Qt::NoButton);
    viewer.keyboardRoute(resumeRoute(request));
    viewer.close();
    check(
        recording.events.size() == before && recording.routes.size() == routes,
        "Failed keyboard fence send did not disconnect and stop further writes"
    );
  }
}
} // namespace

int main(int argc, char **argv)
{
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  try {
    testDecoder();
    testInputMethodIsExplicitlyUnsupported();
    testMappingAndReadiness();
    testViewportLifecycle();
    testFocusQueueAndOwnership();
    testModifierRelease();
    testFocusRevocation();
    testResizeAndScroll();
    testBoundedDecodeQueue();
    testBoundedMediaRecovery();
    testMediaPauseOnHiddenViewer();
    testQueuedFrameRevision();
    testKeyboardRouteFenceAndResume();
    testKeyboardRouteBusyAndGenerations();
    testKeyboardRoutePendingFocus();
    testKeyboardRoutePendingFocus(ForwardingScope::All);
    testKeyboardRoutePendingFocus(ForwardingScope::Keyboard, true);
    testKeyboardRoutePendingFocus(ForwardingScope::All, true);
    testFullInputFenceAndResume();
    testFullInputFenceInvalidation();
    testKeyboardRouteBindingValidation();
    testKeyboardRouteInvalidation();
    testKeyboardRouteSendFailure();
    testKeyboardRouteSendFailure(ForwardingScope::All);
    QTextStream(
        stdout
    ) << "PASS: H.264 decode/reset, malformed video, rejected input-method compositions, "
         "letterboxing/scaling, first painted readiness, "
         "queued activating click, stale focus, special keys/repeat, ordered release, resize fencing, "
         "double-click, focus revocation/reclaim, scroll, minimize, safe return, disconnect, bounded video queue, "
         "queued geometry race, negotiated bounded decoder recovery, hidden-viewer pause/IDR and input revocation, "
         "keyboard fence ordering, pointer/focus preservation, held-key tombstones, "
         "route generations, pending focus fence, full-input scope, pointer tombstones, "
         "route invalidation/resume and send failure\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
