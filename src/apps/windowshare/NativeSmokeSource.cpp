/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "../../lib/windowshare/InputEvidenceRuntime.h"
#include "../../lib/windowshare/PlatformMarkers.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CGEvent.h>
#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGWindow.h>
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <objc/message.h>
#include <objc/runtime.h>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>

using namespace deskflow::windowshare;

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

struct OwnedWindowObservation
{
  QJsonObject frame;
  bool onScreen = false;
};
OwnedWindowObservation observeOwnedWindow(long number)
{
  OwnedWindowObservation result;
  if (number <= 0)
    return result;
  const auto windows = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, CGWindowID(number));
  if (!windows)
    return result;
  for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
    const auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, i));
    const auto ownerValue = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowOwnerPID));
    const auto numberValue = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowNumber));
    int owner = 0, observed = 0;
    CGRect bounds{};
    if (!ownerValue || !numberValue || !CFNumberGetValue(ownerValue, kCFNumberIntType, &owner) || owner != getpid() ||
        !CFNumberGetValue(numberValue, kCFNumberIntType, &observed) || observed != number ||
        !CGRectMakeWithDictionaryRepresentation(
            static_cast<CFDictionaryRef>(CFDictionaryGetValue(info, kCGWindowBounds)), &bounds
        ))
      continue;
    result.frame = {
        {"x", bounds.origin.x}, {"y", bounds.origin.y}, {"width", bounds.size.width}, {"height", bounds.size.height}
    };
    const auto visible = static_cast<CFBooleanRef>(CFDictionaryGetValue(info, kCGWindowIsOnscreen));
    result.onScreen = visible && CFGetTypeID(visible) == CFBooleanGetTypeID() && CFBooleanGetValue(visible);
  }
  CFRelease(windows);
  return result;
}

bool sameStatusFile(const struct stat &a, const struct stat &b)
{
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
         a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec && a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec &&
         a.st_ctimespec.tv_sec == b.st_ctimespec.tv_sec && a.st_ctimespec.tv_nsec == b.st_ctimespec.tv_nsec;
}

struct ManualStatus
{
  QString state;
  int remainingSeconds = 0;
  struct stat file{};
};
constexpr int manualStartSeconds = 300;

std::optional<ManualStatus> readManualStatus(int directory, const QByteArray &name, int sessionSeconds)
{
  if (directory < 0)
    return std::nullopt;
  const int descriptor = openat(directory, name.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (descriptor < 0)
    return std::nullopt;
  QFile file;
  if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
    close(descriptor);
    return std::nullopt;
  }
  struct stat before{}, after{};
  if (fstat(descriptor, &before) != 0 || !S_ISREG(before.st_mode) || before.st_uid != getuid() ||
      before.st_nlink != 1 || (before.st_mode & 0077) || before.st_size <= 0 || before.st_size > 512)
    return std::nullopt;
  const auto bytes = file.read(513);
  if (bytes.size() != before.st_size || fstat(descriptor, &after) != 0 || !sameStatusFile(before, after))
    return std::nullopt;
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  // All three allowed keys/values are simple scalars without colons. Counting
  // member separators also rejects duplicate keys which QJsonObject collapses.
  if (error.error != QJsonParseError::NoError || !document.isObject() || bytes.count(':') != 3)
    return std::nullopt;
  const auto value = document.object();
  const auto state = value.value("state").toString();
  const auto seconds = value.value("remainingSeconds");
  if (value.size() != 3 || !value.value("version").isDouble() || value.value("version").toDouble() != 1 ||
      !seconds.isDouble() || seconds.toDouble() != seconds.toInt(-1) || seconds.toInt(-1) < 0 ||
      seconds.toInt() > (state == "ready" ? manualStartSeconds : sessionSeconds) ||
      (state != "preparing" && state != "ready" && state != "active" && state != "stopped") ||
      (state == "active" ? seconds.toInt() == 0 : state != "ready" && seconds.toInt() != 0))
    return std::nullopt;
  return ManualStatus{state, seconds.toInt(), after};
}

class AcceptanceWindow final : public QWidget
{
public:
  std::function<bool()> onManualClose;

protected:
  void closeEvent(QCloseEvent *event) override
  {
    if (onManualClose && !onManualClose()) {
      event->ignore();
    } else {
      QWidget::closeEvent(event);
    }
  }
};
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
  void watchControlWindow(QWidget *window)
  {
    m_controlWindow = window;
    save();
  }
  void flush()
  {
    save();
  }
  void setManualMetadata(QJsonObject metadata)
  {
    m_manual = std::move(metadata);
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
    if (!quartz) {
      if (window && window == m_nativeWindow)
        inputEvidenceInvalidate(InputEvidenceLoss::Malformed);
      return false;
    }
    const auto tag = CGEventGetIntegerValueField(quartz, kCGEventSourceUserData);
    InputEvidenceRaw raw;
    raw.timestamp = CGEventGetTimestamp(quartz);
    raw.flags = CGEventGetFlags(quartz);
    auto &fields = raw.fields;
    fields[0] = CGEventGetType(quartz);
    const auto integer = [quartz](CGEventField field) { return CGEventGetIntegerValueField(quartz, field); };
    if (fields[0] == 10 || fields[0] == 11 || fields[0] == 12) {
      fields[1] = integer(kCGKeyboardEventKeycode);
      fields[2] = integer(kCGKeyboardEventKeyboardType);
      fields[3] = integer(kCGKeyboardEventAutorepeat);
    } else if (fields[0] == 5 || fields[0] == 6 || fields[0] == 7 || fields[0] == 27) {
      fields[4] = integer(kCGMouseEventDeltaX);
      fields[5] = integer(kCGMouseEventDeltaY);
      fields[6] = integer(kCGMouseEventButtonNumber);
    } else if (
        fields[0] == 1 || fields[0] == 2 || fields[0] == 3 || fields[0] == 4 || fields[0] == 25 || fields[0] == 26
    ) {
      fields[6] = integer(kCGMouseEventButtonNumber);
      fields[7] = integer(kCGMouseEventClickState);
    } else if (fields[0] == 22) {
      fields[8] = integer(kCGScrollWheelEventDeltaAxis2);
      fields[9] = integer(kCGScrollWheelEventDeltaAxis1);
      fields[10] = integer(kCGScrollWheelEventPointDeltaAxis2);
      fields[11] = integer(kCGScrollWheelEventPointDeltaAxis1);
      fields[12] = integer(kCGScrollWheelEventFixedPtDeltaAxis2);
      fields[13] = integer(kCGScrollWheelEventFixedPtDeltaAxis1);
      fields[14] = integer(kCGScrollWheelEventScrollPhase);
      fields[15] = integer(kCGScrollWheelEventMomentumPhase);
      fields[16] = integer(kCGScrollWheelEventIsContinuous);
    }
    const auto sourcePid = integer(kCGEventSourceUnixProcessID);
    const auto windowNumber = nativeMessage<long>(event, "windowNumber");
    // The timestamp/raw effect is observed as received. AppKit may transform
    // it; the offline join must reject mismatches, never repair these fields.
    if (auto *evidence = inputEvidence();
        evidence && m_nativeWindow && window == m_nativeWindow && windowNumber == m_nativeWindowId)
      evidence->native(raw, windowNumber, sourcePid, tag);
    if (m_nativePendingWritten - m_nativePendingRead >= m_nativePending.size()) {
      ++m_nativePendingDropped;
      inputEvidenceInvalidate(InputEvidenceLoss::BufferFull);
      return false;
    }
    auto &observed = m_nativePending[m_nativePendingWritten++ % m_nativePending.size()];
    observed = {type,         m_clock.elapsed(),
                windowNumber, window != nullptr,
                position,     nativeMessage<unsigned long>(event, "modifierFlags"),
                tag,          sourcePid,
                raw};
    if (type == 22) {
      observed.dx = nativeMessage<double>(event, "scrollingDeltaX");
      observed.dy = nativeMessage<double>(event, "scrollingDeltaY");
      observed.precise = bool(nativeMessage<BOOL>(event, "hasPreciseScrollingDeltas"));
    }
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
    drainNative();
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
    if (m_nativeWindow && (m_nativeWindow != ownWindow || m_nativeWindowId != ownId))
      if (auto *evidence = inputEvidence())
        evidence->revokeOwner();
    m_nativeWindow = ownWindow;
    m_nativeWindowId = ownId;
    if (!m_evidenceBound && ownWindow && ownId > 0) {
      if (auto *evidence = inputEvidence()) {
        InputEvidenceOwner owner;
        owner.values[0] = getpid();
        owner.values[1] = ownId;
        evidence->bindOwner(owner);
        m_evidenceBound = true;
      }
    }
    const auto controlWindow =
        m_controlWindow ? nativeMessage<id>(reinterpret_cast<id>(m_controlWindow->winId()), "window") : nullptr;
    const auto controlId = controlWindow ? nativeMessage<long>(controlWindow, "windowNumber") : 0;
    const auto control = observeOwnedWindow(controlId);
    const auto controlExpected = m_controlWindow ? m_controlWindow->frameGeometry() : QRect();
    const bool controlSettled = !control.frame.isEmpty() && control.frame["x"].toDouble() == controlExpected.x() &&
                                control.frame["y"].toDouble() == controlExpected.y() &&
                                control.frame["width"].toDouble() == controlExpected.width() &&
                                control.frame["height"].toDouble() == controlExpected.height();
    if (m_originalControlFrame.isEmpty() && controlSettled)
      m_originalControlFrame = control.frame;
    const bool controlVisible = controlSettled && control.onScreen && m_controlWindow->isVisible() &&
                                nativeMessage<BOOL>(controlWindow, "isVisible") &&
                                !nativeMessage<BOOL>(controlWindow, "isMiniaturized");
    const auto source = observeOwnedWindow(ownId);
    const auto &frame = source.frame;
    const auto expected = m_window ? m_window->frameGeometry() : QRect();
    const bool settled = !frame.isEmpty() && frame["x"].toDouble() == expected.x() &&
                         frame["y"].toDouble() == expected.y() && frame["width"].toDouble() == expected.width() &&
                         frame["height"].toDouble() == expected.height();
    const bool sourceVisible = settled && source.onScreen && m_window->isVisible() &&
                               nativeMessage<BOOL>(ownWindow, "isVisible") &&
                               !nativeMessage<BOOL>(ownWindow, "isMiniaturized");
    // WindowServer can briefly report a transformed frame during the initial
    // show animation. Latch only the requested Qt frame, not that transient.
    if (m_originalFrame.isEmpty() && settled)
      m_originalFrame = frame;
    QJsonObject metadata{
        {"keyDown", m_keyDown},
        {"keyUp", m_keyUp},
        {"mouseDown", m_mouseDown},
        {"mouseUp", m_mouseUp},
        {"scroll", m_scroll},
        {"heldKeys", m_keys.size()},
        {"heldButtons", m_buttons.size()},
        {"pid", qint64(getpid())},
        {"windowId", frame.isEmpty() ? 0 : qint64(ownId)},
        {"sourceWindowId", frame.isEmpty() ? 0 : qint64(ownId)},
        {"controlWindowId", qint64(controlId)},
        {"controlFrame", control.frame},
        {"controlOriginalFrame", m_originalControlFrame},
        {"controlVisible", controlVisible},
        {"sourceVisible", sourceVisible},
        {"nativePendingDropped", QString::number(m_nativePendingDropped)},
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
    };
    if (!m_manual.isEmpty())
      metadata["manual"] = m_manual;
    file.write(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    file.commit();
  }
  struct NativeObserved
  {
    unsigned long type = 0;
    qint64 elapsed = 0;
    long window = 0;
    bool resolved = false;
    CGPoint position{};
    unsigned long modifiers = 0;
    std::int64_t tag = 0, sourcePid = 0;
    InputEvidenceRaw raw;
    double dx = 0, dy = 0;
    bool precise = false;
  };
  void drainNative()
  {
    while (m_nativePendingRead != m_nativePendingWritten) {
      const auto &row = m_nativePending[m_nativePendingRead++ % m_nativePending.size()];
      QJsonArray fields;
      for (auto field : row.raw.fields)
        fields.append(QString::number(field));
      QJsonObject observed{
          {"type", qint64(row.type)},
          {"elapsedMs", row.elapsed},
          {"windowNumber", qint64(row.window)},
          {"windowResolved", row.resolved},
          {"x", row.position.x},
          {"y", row.position.y},
          {"modifiers", qint64(row.modifiers)},
          {"tag", QString::number(row.tag, 16)},
          {"synthetic", isWindowshareSyntheticEvent(row.tag)},
          {"sourcePid", qint64(row.sourcePid)},
          {"cgTimestampNs", QString::number(row.raw.timestamp)},
          {"cgFlags", QString::number(row.raw.flags)},
          {"cgRaw", fields}
      };
      if (row.type == 10 || row.type == 11 || row.type == 12)
        observed["keyCode"] = int(row.raw.fields[1]);
      if (row.type == 22) {
        observed["dx"] = row.dx;
        observed["dy"] = row.dy;
        observed["precise"] = row.precise;
      }
      appendReceipt(m_nativeObserved, m_nativeEventCount, std::move(observed));
    }
  }
  std::array<NativeObserved, 512> m_nativePending;
  std::uint64_t m_nativePendingRead = 0, m_nativePendingWritten = 0, m_nativePendingDropped = 0;
  id m_nativeWindow = nullptr;
  long m_nativeWindowId = 0;
  bool m_evidenceBound = false;
  QString m_path;
  QPointer<QWidget> m_window, m_controlWindow;
  QJsonObject m_originalFrame;
  QJsonObject m_originalControlFrame;
  QJsonObject m_manual;
  QSet<int> m_keys;
  QSet<int> m_buttons;
  QJsonArray m_observed;
  QJsonArray m_nativeObserved;
  qint64 m_eventCount = 0, m_nativeEventCount = 0;
  QElapsedTimer m_clock;
  QTimer m_stateTimer;
  int m_keyDown = 0, m_keyUp = 0, m_mouseDown = 0, m_mouseUp = 0, m_scroll = 0;
};

class ManualAcceptance final : public QObject
{
public:
  ManualAcceptance(
      const QString &path, Recorder &recorder, QVBoxLayout &layout, QLineEdit &field, bool h11Input, bool recordInput
  )
      : m_recorder(recorder),
        m_field(field),
        m_sessionSeconds(h11Input ? 150 : 120),
        m_recordInput(recordInput)
  {
    const QFileInfo location(path);
    if (location.isAbsolute() && !location.fileName().isEmpty()) {
      m_directory =
          open(QFile::encodeName(location.absolutePath()).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY);
      struct stat info{};
      if (m_directory >= 0 && (fstat(m_directory, &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != getuid() ||
                               (info.st_mode & 0077))) {
        close(m_directory);
        m_directory = -1;
      }
      m_name = QFile::encodeName(location.fileName());
    }
    m_status = new QLabel;
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout.insertWidget(0, m_status);
    m_instructions = new QLabel;
    m_instructions->setTextFormat(Qt::PlainText);
    m_instructions->setWordWrap(true);
    layout.insertWidget(1, m_instructions);
    auto *buttons = new QHBoxLayout;
    m_start = new QPushButton("Start");
    m_stop = new QPushButton("Stop");
    m_start->setFocusPolicy(Qt::NoFocus);
    m_stop->setFocusPolicy(Qt::NoFocus);
    buttons->addWidget(m_start);
    buttons->addWidget(m_stop);
    layout.addLayout(buttons);
    connect(m_start, &QPushButton::clicked, this, [this] {
      poll();
      if (m_valid && m_state == "ready" && m_request == "none" && !m_ended) {
        m_request = "start";
        ++m_requestSequence;
        m_state = "preparing";
        m_remainingSeconds = 0;
        update();
      }
    });
    connect(m_stop, &QPushButton::clicked, this, [this] { requestStop(); });
    m_clock.start();
    connect(&m_timer, &QTimer::timeout, this, [this] { poll(); });
    m_timer.start(100);
    poll();
  }
  ~ManualAcceptance() override
  {
    if (m_directory >= 0)
      close(m_directory);
  }
  void requestStop()
  {
    if (m_request != "stop") {
      m_request = "stop";
      ++m_requestSequence;
    }
    m_ended = true;
    m_state = "stopped";
    m_remainingSeconds = 0;
    update();
  }
  bool requestClose()
  {
    const bool stopWasRequested = m_request == "stop";
    poll();
    // Keep the source alive during ordinary restoration. If its coordinator
    // died, a later close can exit after the collector's grace period; that
    // premature source loss cannot count as a passing physical test.
    if (stopWasRequested && coordinatorLost())
      return true;
    requestStop();
    return false;
  }
  bool activeForEvidence() const
  {
    return m_valid && !m_ended && m_state == "active" && m_request == "start";
  }

private:
  bool coordinatorLost() const
  {
    return !m_valid && m_clock.elapsed() - m_changedAt >= 15000;
  }
  void poll()
  {
    const auto status = readManualStatus(m_directory, m_name, m_sessionSeconds);
    const auto now = m_clock.elapsed();
    if (status) {
      if (m_seenFile && !sameStatusFile(m_lastFile, status->file)) {
        m_heartbeat = true;
        m_changedAt = now;
      }
      m_lastFile = status->file;
      m_seenFile = true;
    }
    // Require an actual fresh publication after our first observation. Merely
    // rereading an unchanged old ready file never enables Start or renews it.
    m_valid = status && m_heartbeat && now - m_changedAt < 2000;
    if (m_ended) {
      update();
      return;
    }
    if (!m_valid) {
      if (m_request == "start") {
        requestStop();
        return;
      }
      m_state = "preparing";
    } else if (status->state == "stopped") {
      m_ended = true;
      m_state = "stopped";
    } else if (status->state == "active") {
      if (m_request != "start") {
        requestStop();
        return;
      }
      const auto proposed = now + status->remainingSeconds * 1000;
      m_deadline = m_deadline ? std::min(m_deadline, proposed) : proposed;
      m_state = "active";
    } else if (m_deadline) {
      // Once active, a coordinator rollback cannot silently rearm the test.
      requestStop();
      return;
    } else {
      m_state = m_request == "start" ? QStringLiteral("preparing") : status->state;
      if (m_state == "ready" && status->remainingSeconds) {
        const auto proposed = now + status->remainingSeconds * 1000;
        m_readyDeadline = m_readyDeadline ? std::min(m_readyDeadline, proposed) : proposed;
      }
    }
    const auto deadline = m_state == "active" ? m_deadline : m_state == "ready" ? m_readyDeadline : 0;
    m_remainingSeconds = deadline ? int(std::max<qint64>(0, deadline - now + 999) / 1000) : 0;
    if (deadline && !m_remainingSeconds) {
      requestStop();
      return;
    }
    update();
  }
  void update()
  {
    if (m_state == "active") {
      m_status->setText(QStringLiteral("Active — %1 seconds remaining").arg(m_remainingSeconds));
      if (m_recordInput && m_sessionSeconds == 120)
        m_instructions->setText(
            "1. Ctrl+Option+Shift+Right, release all modifiers, then press Escape once.\n"
            "2. Type a, Shift+b, c; hold x for 1 second. Click/drag inside this window and scroll both ways; release "
            "everything.\n"
            "3. Click Record in the Viewer and wait for Recording.\n"
            "4. Repeat those inputs for 30 seconds; stay here until Recording complete.\n"
            "5. Ctrl+Option+Shift+Left to return. Release all keys, then click Stop."
        );
      else
        m_instructions->setText(
            m_sessionSeconds == 150
                ? "1. Ctrl+Option+Shift+Right, then release all modifiers.\n"
                  "2. Prime the planned controls, release them, then click Record in the Viewer.\n"
                  "3. Wait for Recording, then perform the planned two-minute mixed-input sequence.\n"
                  "4. Stay in this session until Recording complete.\n"
                  "5. Ctrl+Option+Shift+Left to return. Release all keys, then click Stop."
                : "1. Ctrl+Option+Shift+Right, then release all modifiers.\n"
                  "2. Press Escape once. Type a, then Shift+b, then c.\n"
                  "3. Hold x for 1 second, then release it.\n"
                  "4. Move, click and drag inside the test window. Scroll vertically and horizontally.\n"
                  "5. Ctrl+Option+Shift+Left to return. Release all keys, then click Stop."
        );
    } else if (m_state == "stopped") {
      m_status->setText("Test stopped");
      m_instructions->setText(
          m_request == "stop" && coordinatorLost()
              ? "The coordinator is not responding. You can close this window; the test remains incomplete."
              : "Release all keys and buttons. The coordinator will finish cleanup and close this window."
      );
    } else if (m_state == "ready") {
      m_status->setText(
          m_remainingSeconds ? QStringLiteral("Ready — click Start within %1 seconds").arg(m_remainingSeconds)
                             : QStringLiteral("Ready — click Start when you are ready")
      );
      m_instructions->setText(
          QStringLiteral("The %1-second countdown starts only after the connection is ready.").arg(m_sessionSeconds)
      );
    } else {
      m_status->setText(
          m_request == "start" ? "Start requested — preparing the connection" : "Preparing the input test"
      );
      m_instructions->setText("Please wait for Active before using the shortcut. You can click Stop at any time.");
    }
    m_start->setEnabled(m_valid && m_state == "ready" && m_request == "none" && !m_ended);
    m_stop->setEnabled(!m_ended);
    m_field.setReadOnly(m_state != "active");
    m_recorder.setManualMetadata(
        {{"state", m_state},
         {"request", m_request},
         {"requestSequence", m_requestSequence},
         {"statusValid", m_valid},
         {"remainingSeconds", m_remainingSeconds}}
    );
  }
  Recorder &m_recorder;
  QLineEdit &m_field;
  const int m_sessionSeconds;
  const bool m_recordInput;
  QLabel *m_status = nullptr;
  QLabel *m_instructions = nullptr;
  QPushButton *m_start = nullptr;
  QPushButton *m_stop = nullptr;
  QElapsedTimer m_clock;
  QTimer m_timer;
  int m_directory = -1;
  QByteArray m_name;
  struct stat m_lastFile{};
  QString m_state = "preparing", m_request = "none";
  qint64 m_changedAt = 0, m_deadline = 0, m_readyDeadline = 0;
  int m_requestSequence = 0, m_remainingSeconds = 0;
  bool m_seenFile = false, m_heartbeat = false, m_valid = false, m_ended = false;
};

class EvidenceControls final : public QObject
{
public:
  EvidenceControls(InputEvidenceRuntime &runtime, ManualAcceptance &manual, QString request, QVBoxLayout &layout)
      : m_runtime(runtime),
        m_manual(manual),
        m_request(std::move(request))
  {
    m_label = new QLabel;
    m_label->setTextFormat(Qt::PlainText);
    m_label->setWordWrap(true);
    m_record = new QPushButton("Record");
    m_record->setFocusPolicy(Qt::NoFocus);
    layout.addWidget(m_label);
    layout.addWidget(m_record);
    connect(m_record, &QPushButton::clicked, this, [this] {
      if (!m_requested && m_manual.activeForEvidence() && m_runtime.recorder().state() == InputEvidenceState::Waiting) {
        m_requested = true;
        m_requestOkay = m_runtime.requestRecord(m_request);
        update();
      }
    });
    connect(&m_timer, &QTimer::timeout, this, [this] { update(); });
    m_timer.start(100);
    update();
  }

private:
  void update()
  {
    const auto state = m_runtime.recorder().state();
    const bool lost = m_runtime.recorder().loss() != InputEvidenceLoss::None || (m_requested && !m_requestOkay);
    m_record->setEnabled(!m_requested && !lost && m_manual.activeForEvidence() && state == InputEvidenceState::Waiting);
    if (lost || state == InputEvidenceState::Denied)
      m_label->setText("Recording declined. Return to Mac and Stop; retain diagnostics.");
    else if (state == InputEvidenceState::Stopped)
      m_label->setText("Recording complete. Return to Mac, then Stop.");
    else if (state == InputEvidenceState::Draining)
      m_label->setText("Completing the bounded recording. Release controls and wait.");
    else if (state == InputEvidenceState::Recording)
      m_label->setText("Recording. Perform the planned input sequence here; stay in this session.");
    else if (m_requested)
      m_label->setText("Record requested. Waiting for the exact live route and empty queues.");
    else
      m_label->setText(
          "Prime the planned key/button/scroll controls, release them, then click Record here in the Viewer."
      );
  }
  InputEvidenceRuntime &m_runtime;
  ManualAcceptance &m_manual;
  QString m_request;
  QLabel *m_label = nullptr;
  QPushButton *m_record = nullptr;
  QTimer m_timer;
  bool m_requested = false, m_requestOkay = false;
};

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  const auto arguments = app.arguments();
  QString manualPath, evidencePath, recordRequestPath;
  bool separateControls = false, h11Input = false;
  if (arguments.size() < 3)
    return 2;
  for (int index = 3; index < arguments.size(); ++index) {
    const auto option = arguments.at(index);
    if (option == "--separate-controls" && !separateControls)
      separateControls = true;
    else if (option == "--h11-input" && !h11Input)
      h11Input = true;
    else if (
        (option == "--manual-status" || option == "--input-evidence-config" || option == "--record-request") &&
        index + 1 < arguments.size()
    ) {
      auto &value = option == "--manual-status"    ? manualPath
                    : option == "--record-request" ? recordRequestPath
                                                   : evidencePath;
      if (!value.isEmpty())
        return 2;
      value = arguments.at(++index);
      if (value.isEmpty() || value.startsWith("--"))
        return 2;
    } else
      return 2;
  }
  const bool manualMode = !manualPath.isEmpty();
  if ((separateControls || !evidencePath.isEmpty()) && !manualMode)
    return 2;
  if (!recordRequestPath.isEmpty() && evidencePath.isEmpty())
    return 2;
  if (h11Input && (!manualMode || evidencePath.isEmpty() || recordRequestPath.isEmpty()))
    return 2;
  std::unique_ptr<InputEvidenceRuntime> evidence;
  try {
    if (!evidencePath.isEmpty())
      // The fixture exists during Ready (up to five minutes), preparation and
      // the active session. Core/Host retain their ordinary three-minute wait;
      // this absolute fixture-only limit matches the coordinator's outer bound.
      evidence = InputEvidenceRuntime::open(evidencePath, InputEvidenceRole::Native, 600000);
  } catch (...) {
    return 2;
  }
  Recorder recorder(arguments.at(2));
  AcceptanceWindow window;
  window.setWindowTitle(arguments.at(1));
  window.resize(640, 420);
  auto *layout = new QVBoxLayout(&window);
  if (!manualMode)
    layout->addWidget(
        new QLabel("Deskflow native capture/input test\nThis temporary window closes automatically.", &window)
    );
  auto *field = new QLineEdit(&window);
  layout->addWidget(field);
  std::unique_ptr<AcceptanceWindow> controls;
  QVBoxLayout *controlsLayout = layout;
  if (separateControls) {
    controls = std::make_unique<AcceptanceWindow>();
    controls->setWindowTitle("Deskflow input test controls");
    controls->resize(480, 240);
    controlsLayout = new QVBoxLayout(controls.get());
  }
  std::unique_ptr<ManualAcceptance> manual;
  if (manualMode) {
    manual = std::make_unique<ManualAcceptance>(
        manualPath, recorder, *controlsLayout, *field, h11Input, !recordRequestPath.isEmpty()
    );
    window.onManualClose = [guard = QPointer<ManualAcceptance>(manual.get())] {
      return !guard || guard->requestClose();
    };
  }
  // Record stays in the exported source, including separate-controls mode:
  // returning to the Mac to arm would replace the route being measured.
  std::unique_ptr<EvidenceControls> evidenceControls;
  if (!recordRequestPath.isEmpty())
    evidenceControls = std::make_unique<EvidenceControls>(*evidence, *manual, recordRequestPath, *layout);
  if (controls)
    controls->onManualClose = window.onManualClose;
  app.installEventFilter(&recorder);
  app.installNativeEventFilter(&recorder);
  window.show();
  recorder.watchWindow(&window);
  if (controls) {
    controls->show();
    recorder.watchControlWindow(controls.get());
  }
  field->setFocus();
  if (manualMode) {
    QTimer::singleShot(0, &app, [&] {
      // Present only this disposable application's own windows, once. Never
      // steal focus again during physical input or synthesize a Start click.
      nativeMessage<void>(nativeApplication(), "activateIgnoringOtherApps:", YES);
      window.raise();
      auto *target = controls ? controls.get() : &window;
      target->raise();
      target->activateWindow();
    });
  }
  const int result = app.exec();
  app.removeNativeEventFilter(&recorder);
  app.removeEventFilter(&recorder);
  recorder.flush();
  return result;
}
