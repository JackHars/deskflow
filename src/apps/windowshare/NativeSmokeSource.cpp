/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "../../lib/windowshare/PlatformMarkers.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CGEvent.h>
#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGWindow.h>
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>
#include <objc/message.h>
#include <objc/runtime.h>
#include <unistd.h>

namespace {
constexpr qsizetype receiptHistoryLimit = 128;
constexpr qint64 receiptSequenceLimit = 9007199254740991;

void appendReceipt(QJsonArray &history, qint64 &count, QJsonObject observed)
{
  // Keep JSON sequence values exact for collectors and never wrap/reuse one.
  if (count == receiptSequenceLimit)
    return;
  observed["sequence"] = ++count;
  history.append(observed);
  if (history.size() > receiptHistoryLimit)
    history.removeAt(0);
}

template <typename Result, typename... Args> Result nativeMessage(id receiver, const char *selector, Args... args)
{
  return reinterpret_cast<Result (*)(id, SEL, Args...)>(objc_msgSend)(receiver, sel_registerName(selector), args...);
}
id nativeApplication()
{
  return nativeMessage<id>(reinterpret_cast<id>(objc_getClass("NSApplication")), "sharedApplication");
}
} // namespace

// This process is the only target of the opt-in native smoke test. It records
// event counts (never user text) to a temporary file owned by the test runner.
class Recorder final : public QObject, public QAbstractNativeEventFilter
{
public:
  explicit Recorder(QString path) : m_path(std::move(path))
  {
    m_clock.start();
    connect(&m_stateTimer, &QTimer::timeout, this, [this] { save(); });
    m_stateTimer.start(25);
    save();
  }
  void watchWindow(QWidget *window)
  {
    m_window = window;
    save();
  }
  bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override
  {
    if (eventType != "mac_generic_NSEvent" || !message)
      return false;
    const auto event = reinterpret_cast<id>(message);
    const auto type = nativeMessage<unsigned long>(event, "type");
    if (!((type >= 1 && type <= 7) || type == 10 || type == 11 || type == 12 || type == 22 || type == 25 ||
          type == 26 || type == 27))
      return false;
    const auto position = nativeMessage<CGPoint>(event, "locationInWindow");
    const auto window = nativeMessage<id>(event, "window");
    const auto quartz = nativeMessage<CGEventRef>(event, "CGEvent");
    const auto tag = CGEventGetIntegerValueField(quartz, kCGEventSourceUserData);
    QJsonObject observed{
        {"type", qint64(type)},
        {"elapsedMs", m_clock.elapsed()},
        {"windowNumber", qint64(nativeMessage<long>(event, "windowNumber"))},
        {"windowResolved", window != nullptr},
        {"x", position.x},
        {"y", position.y},
        {"modifiers", qint64(nativeMessage<unsigned long>(event, "modifierFlags"))},
        {"tag", QString::number(tag, 16)},
        {"synthetic", deskflow::windowshare::isWindowshareSyntheticEvent(tag)},
        {"sourcePid", qint64(CGEventGetIntegerValueField(quartz, kCGEventSourceUnixProcessID))}
    };
    if (type == 10 || type == 11 || type == 12)
      observed["keyCode"] = int(nativeMessage<unsigned short>(event, "keyCode"));
    if (type == 22) {
      observed["dx"] = nativeMessage<double>(event, "scrollingDeltaX");
      observed["dy"] = nativeMessage<double>(event, "scrollingDeltaY");
      observed["precise"] = bool(nativeMessage<BOOL>(event, "hasPreciseScrollingDeltas"));
    }
    appendReceipt(m_nativeObserved, m_nativeEventCount, std::move(observed));
    save();
    return false;
  }
  bool eventFilter(QObject *receiver, QEvent *event) override
  {
    QJsonObject observed{{"type", int(event->type())}, {"receiver", receiver->metaObject()->className()}};
    switch (event->type()) {
    case QEvent::KeyPress:
      ++m_keyDown;
      m_keys.insert(static_cast<QKeyEvent *>(event)->key());
      break;
    case QEvent::KeyRelease:
      ++m_keyUp;
      m_keys.remove(static_cast<QKeyEvent *>(event)->key());
      break;
    case QEvent::MouseButtonPress:
      ++m_mouseDown;
      m_buttons.insert(int(static_cast<QMouseEvent *>(event)->button()));
      break;
    case QEvent::MouseButtonRelease:
      ++m_mouseUp;
      m_buttons.remove(int(static_cast<QMouseEvent *>(event)->button()));
      break;
    case QEvent::MouseMove:
      break;
    case QEvent::Wheel:
      ++m_scroll;
      break;
    default:
      return false;
    }
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
      const auto *key = static_cast<QKeyEvent *>(event);
      observed["code"] = key->key();
      observed["scan"] = qint64(key->nativeScanCode());
      observed["virtual"] = qint64(key->nativeVirtualKey());
      observed["modifiers"] = int(key->modifiers());
      observed["repeat"] = key->isAutoRepeat();
    } else if (event->type() == QEvent::Wheel) {
      const auto *wheel = static_cast<QWheelEvent *>(event);
      observed["x"] = wheel->position().x();
      observed["y"] = wheel->position().y();
      observed["dx"] = wheel->pixelDelta().x();
      observed["dy"] = wheel->pixelDelta().y();
    } else {
      const auto *mouse = static_cast<QMouseEvent *>(event);
      observed["x"] = mouse->position().x();
      observed["y"] = mouse->position().y();
      observed["button"] = int(mouse->button());
    }
    appendReceipt(m_observed, m_eventCount, std::move(observed));
    save();
    return false;
  }

private:
  void save()
  {
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
      return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto application = nativeApplication();
    const auto keyWindow = nativeMessage<id>(application, "keyWindow");
    const auto mainWindow = nativeMessage<id>(application, "mainWindow");
    // QWidget::winId is an NSView on macOS. Resolve its own window, never the
    // global focused window, then corroborate ID/PID against WindowServer.
    const auto ownWindow = m_window ? nativeMessage<id>(reinterpret_cast<id>(m_window->winId()), "window") : nullptr;
    const auto ownId = ownWindow ? nativeMessage<long>(ownWindow, "windowNumber") : 0;
    QJsonObject frame;
    if (ownId > 0) {
      const auto windows = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, CGWindowID(ownId));
      if (windows) {
        for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
          const auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, i));
          const auto ownerValue = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowOwnerPID));
          const auto numberValue = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowNumber));
          int owner = 0, number = 0;
          CGRect bounds{};
          if (ownerValue && numberValue && CFNumberGetValue(ownerValue, kCFNumberIntType, &owner) &&
              owner == getpid() && CFNumberGetValue(numberValue, kCFNumberIntType, &number) && number == ownId &&
              CGRectMakeWithDictionaryRepresentation(
                  static_cast<CFDictionaryRef>(CFDictionaryGetValue(info, kCGWindowBounds)), &bounds
              ))
            frame = {
                {"x", bounds.origin.x},
                {"y", bounds.origin.y},
                {"width", bounds.size.width},
                {"height", bounds.size.height}
            };
        }
        CFRelease(windows);
      }
    }
    const auto expected = m_window ? m_window->frameGeometry() : QRect();
    const bool settled = !frame.isEmpty() && frame["x"].toDouble() == expected.x() &&
                         frame["y"].toDouble() == expected.y() && frame["width"].toDouble() == expected.width() &&
                         frame["height"].toDouble() == expected.height();
    // WindowServer can briefly report a transformed frame during the initial
    // show animation. Latch only the requested Qt frame, not that transient.
    if (m_originalFrame.isEmpty() && settled)
      m_originalFrame = frame;
    file.write(QJsonDocument(
                   QJsonObject{
                       {"keyDown", m_keyDown},
                       {"keyUp", m_keyUp},
                       {"mouseDown", m_mouseDown},
                       {"mouseUp", m_mouseUp},
                       {"scroll", m_scroll},
                       {"heldKeys", m_keys.size()},
                       {"heldButtons", m_buttons.size()},
                       {"pid", qint64(getpid())},
                       {"windowId", frame.isEmpty() ? 0 : qint64(ownId)},
                       {"ready", !m_originalFrame.isEmpty() && settled},
                       {"title", m_window ? m_window->windowTitle() : QString()},
                       {"originalFrame", m_originalFrame},
                       {"frame", frame},
                       {"eventCount", m_eventCount},
                       {"nativeEventCount", m_nativeEventCount},
                       {"eventsDropped", m_eventCount - m_observed.size()},
                       {"nativeEventsDropped", m_nativeEventCount - m_nativeObserved.size()},
                       {"eventSequenceExhausted", m_eventCount == receiptSequenceLimit},
                       {"nativeEventSequenceExhausted", m_nativeEventCount == receiptSequenceLimit},
                       {"events", m_observed},
                       {"nativeEvents", m_nativeObserved},
                       {"nativeActive", bool(nativeMessage<BOOL>(application, "isActive"))},
                       {"nativeKeyWindow", qint64(nativeMessage<long>(keyWindow, "windowNumber"))},
                       {"nativeMainWindow", qint64(nativeMessage<long>(mainWindow, "windowNumber"))}
                   }
    ).toJson(QJsonDocument::Compact));
    file.commit();
  }
  QString m_path;
  QPointer<QWidget> m_window;
  QJsonObject m_originalFrame;
  QSet<int> m_keys;
  QSet<int> m_buttons;
  QJsonArray m_observed;
  QJsonArray m_nativeObserved;
  qint64 m_eventCount = 0, m_nativeEventCount = 0;
  QElapsedTimer m_clock;
  QTimer m_stateTimer;
  int m_keyDown = 0, m_keyUp = 0, m_mouseDown = 0, m_mouseUp = 0, m_scroll = 0;
};
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  if (app.arguments().size() != 3)
    return 2;
  Recorder recorder(app.arguments().at(2));
  QWidget window;
  window.setWindowTitle(app.arguments().at(1));
  window.resize(640, 420);
  auto *layout = new QVBoxLayout(&window);
  layout->addWidget(
      new QLabel("Deskflow native capture/input test\nThis temporary window closes automatically.", &window)
  );
  auto *field = new QLineEdit(&window);
  layout->addWidget(field);
  app.installEventFilter(&recorder);
  app.installNativeEventFilter(&recorder);
  window.show();
  recorder.watchWindow(&window);
  field->setFocus();
  return app.exec();
}
