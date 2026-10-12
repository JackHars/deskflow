/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VideoDecoder.h"
#include "Viewer.h"

#include <QApplication>
#include <QEventLoop>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
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
  std::vector<Sent> events;
  QStringList errors;
  void attach(Viewer &viewer)
  {
    viewer.onSend = [this](MessageType type, const QJsonObject &object, std::uint64_t session, std::uint64_t epoch) {
      events.push_back({type, object, session, epoch});
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
} // namespace

int main(int argc, char **argv)
{
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  try {
    testDecoder();
    testMappingAndReadiness();
    testFocusQueueAndOwnership();
    testModifierRelease();
    testFocusRevocation();
    testResizeAndScroll();
    testBoundedDecodeQueue();
    testQueuedFrameRevision();
    QTextStream(
        stdout
    ) << "PASS: H.264 decode/reset, malformed video, letterboxing/scaling, first painted readiness, "
         "queued activating click, stale focus, special keys/repeat, ordered release, resize fencing, "
         "double-click, focus revocation/reclaim, scroll, minimize, safe return, disconnect, bounded video queue, "
         "queued geometry race\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
