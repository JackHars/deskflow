/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeHost.h"
#include "VideoDecoder.h"
#include <ApplicationServices/ApplicationServices.h>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <objc/message.h>
#include <objc/runtime.h>
#include <optional>
#include <unistd.h>

using namespace deskflow::windowshare;
namespace {
template <typename Result, typename... Args> Result nativeMessage(id receiver, const char *selector, Args... args)
{
  return reinterpret_cast<Result (*)(id, SEL, Args...)>(objc_msgSend)(receiver, sel_registerName(selector), args...);
}
std::optional<CGRect> ownWindow(CGWindowID window, pid_t pid, const QString &title)
{
  auto windows = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, window);
  if (!windows)
    return std::nullopt;
  std::optional<CGRect> result;
  for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
    const auto item = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, i));
    int owner = 0, number = 0;
    const auto ownerValue = static_cast<CFNumberRef>(CFDictionaryGetValue(item, kCGWindowOwnerPID));
    const auto numberValue = static_cast<CFNumberRef>(CFDictionaryGetValue(item, kCGWindowNumber));
    const auto name = static_cast<CFStringRef>(CFDictionaryGetValue(item, kCGWindowName));
    const auto expected = title.toCFString();
    const bool matches = ownerValue && numberValue && name && CFNumberGetValue(ownerValue, kCFNumberIntType, &owner) &&
                         owner == pid && CFNumberGetValue(numberValue, kCFNumberIntType, &number) &&
                         number == int(window) && CFEqual(name, expected);
    CFRelease(expected);
    CGRect frame{};
    if (matches && CGRectMakeWithDictionaryRepresentation(
                       static_cast<CFDictionaryRef>(CFDictionaryGetValue(item, kCGWindowBounds)), &frame
                   ))
      result = frame;
  }
  CFRelease(windows);
  return result;
}
bool sourceFocused(pid_t pid, const QString &title)
{
  const auto running = nativeMessage<id>(
      reinterpret_cast<id>(objc_getClass("NSRunningApplication")), "runningApplicationWithProcessIdentifier:", pid
  );
  if (!running || !nativeMessage<BOOL>(running, "isActive"))
    return false;
  const auto application = AXUIElementCreateApplication(pid);
  AXUIElementSetMessagingTimeout(application, 0.25f);
  CFTypeRef focused = nullptr, name = nullptr;
  const auto expected = title.toCFString();
  const bool matches =
      AXUIElementCopyAttributeValue(application, kAXFocusedWindowAttribute, &focused) == kAXErrorSuccess && focused &&
      CFGetTypeID(focused) == AXUIElementGetTypeID() &&
      AXUIElementCopyAttributeValue(static_cast<AXUIElementRef>(focused), kAXTitleAttribute, &name) ==
          kAXErrorSuccess &&
      name && CFEqual(name, expected);
  if (name)
    CFRelease(name);
  if (focused)
    CFRelease(focused);
  CFRelease(expected);
  CFRelease(application);
  return matches;
}
QJsonObject received(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
    return {};
  return QJsonDocument::fromJson(file.readAll()).object();
}
bool near(double a, double b)
{
  return std::isfinite(a) && std::abs(a - b) < 0.01;
}
bool nativePointer(const QJsonObject &data, int type, CGWindowID window, double x, double y, double dy = 0)
{
  for (const auto &value : data["nativeEvents"].toArray()) {
    const auto event = value.toObject();
    if (event["type"].toInt() == type && event["windowNumber"].toInteger() == window &&
        event["windowResolved"].toBool() && event["synthetic"].toBool() && event["sourcePid"].toInteger() == getpid() &&
        near(event["x"].toDouble(NAN), x) && near(event["y"].toDouble(NAN), y) &&
        (type != 22 || near(event["dy"].toDouble(NAN), dy)))
      return true;
  }
  return false;
}
bool nativeKeys(const QJsonObject &data, CGWindowID window)
{
  // The application-wide Qt filter sees propagation to several receivers.
  // Prove both exact physical key pairs on the native stream, in FIFO order.
  const std::pair<int, int> expected[] = {{10, 100}, {11, 100}, {10, 0}, {11, 0}};
  std::size_t next = 0;
  for (const auto &value : data["nativeEvents"].toArray()) {
    const auto event = value.toObject();
    if (!event["synthetic"].toBool() || event["sourcePid"].toInteger() != getpid() ||
        event["windowNumber"].toInteger() != window || !event["windowResolved"].toBool())
      continue;
    const auto type = event["type"].toInt();
    if (type != 10 && type != 11)
      continue;
    if (next >= std::size(expected) || type != expected[next].first ||
        event["keyCode"].toInt(-1) != expected[next].second)
      return false;
    ++next;
  }
  return next == std::size(expected);
}
} // namespace
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  const auto permission = NativeHost::permissions();
  // No OS prompt or arbitrary-window input in an automated test. A human can
  // authorize this test executable through normal macOS Privacy settings.
  if (!permission["screenCapture"].toBool() || !permission["accessibility"].toBool()) {
    QTextStream(stderr) << "SKIP: native smoke needs Screen Recording and Accessibility permission: "
                        << QJsonDocument(permission).toJson(QJsonDocument::Compact) << '\n';
    return 77;
  }
  if (!permission["targetedPointer"].toBool()) {
    QTextStream(stderr) << "SKIP: private macOS window-location API is unavailable for targeted pointer input\n";
    return 77;
  }
  QTemporaryDir directory;
  if (!directory.isValid())
    return 1;
  const auto title = "Deskflow smoke " + QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto events = QDir(directory.path()).filePath("events.json");
  const auto journal = QDir(directory.path()).filePath("journal.json");
  QProcess source;
  QByteArray sourceDiagnostics;
  QObject::connect(&source, &QProcess::readyReadStandardError, &app, [&] {
    sourceDiagnostics.append(source.readAllStandardError());
    if (sourceDiagnostics.size() > 4096)
      sourceDiagnostics.remove(0, sourceDiagnostics.size() - 4096);
  });
  source.start(QDir(QCoreApplication::applicationDirPath()).filePath("windowshare-smoke-source"), {title, events});
  if (!source.waitForStarted(5000))
    return 1;
  const auto sourcePid = static_cast<pid_t>(source.processId());
  NativeHost host(journal);
  VideoDecoder decoder;
  std::uint64_t session = 0, epoch = 0, geometry = 0, focusRevision = 0;
  CGWindowID windowId = 0;
  CGRect original{};
  bool requested = false, decoded = false, committed = false, focused = false, closed = false, restored = false;
  bool inputVerified = false, nativeError = false;
  enum class Phase
  {
    Capture,
    Focus,
    Input,
    Restore,
    Done
  };
  Phase phase = Phase::Capture;
  QElapsedTimer phaseClock;
  int exitCode = 1;
  const auto restore = [&] {
    phase = Phase::Restore;
    phaseClock.restart();
    host.releaseAll();
    host.restore(session);
  };
  host.onError = [&](QString error) {
    nativeError = true;
    QTextStream(stderr) << "Native host: " << error << '\n';
  };
  host.onCatalog = [&](const QVector<Candidate> &windows) {
    if (requested)
      return;
    for (const auto &window : windows) {
      if (!window.title.contains(title))
        continue;
      // A title match alone is never permission to test another application.
      const auto frame = ownWindow(window.id, sourcePid, title);
      if (!frame)
        continue;
      requested = true;
      windowId = window.id;
      original = *frame;
      host.exportWindow(window.id);
      break;
    }
  };
  host.onOpened = [&](std::uint64_t id, std::uint64_t generation, const QJsonObject &metadata) {
    session = id;
    epoch = generation;
    geometry = static_cast<std::uint64_t>(metadata["geometry"].toDouble());
  };
  host.onFrame = [&](std::uint64_t id, std::uint64_t generation, std::uint64_t revision, const QByteArray &bytes) {
    if (decoded || id != session || generation != epoch || revision != geometry)
      return;
    const auto frame = decoder.decode(bytes);
    if (!frame.error.isEmpty())
      QTextStream(stderr) << frame.error << '\n';
    if (frame.image.isNull())
      return;
    decoded = true;
    QTextStream(stdout) << "Captured and decoded " << frame.image.width() << 'x' << frame.image.height() << " using "
                        << decoder.backend() << '\n';
    host.ready(session, epoch, revision);
  };
  host.onCommitted = [&](std::uint64_t, std::uint64_t) {
    committed = true;
    host.input(session, epoch, {{"kind", "focus"}, {"geometry", static_cast<qint64>(geometry)}, {"request", 1}});
  };
  host.onFocused = [&](std::uint64_t id, std::uint64_t generation, std::uint64_t lease, std::uint64_t request) {
    if (focused || !lease || request != 1 || id != session || generation != epoch)
      return;
    focused = true;
    focusRevision = lease;
    phase = Phase::Focus;
    phaseClock.start();
  };
  host.onClosed = [&](std::uint64_t id) {
    if (id == session)
      closed = true;
  };
  QTimer progress;
  QObject::connect(&progress, &QTimer::timeout, &app, [&] {
    if (phase == Phase::Capture || phase == Phase::Done)
      return;
    const auto data = received(events);
    if (phase == Phase::Focus) {
      // Activation requests are asynchronous. Require observed AppKit and AX
      // focus on our own process/window, not a fixed delay or a sent request.
      if (source.state() == QProcess::Running && ownWindow(windowId, sourcePid, title) &&
          data["pid"].toInteger() == sourcePid && data["ready"].toBool() && data["nativeActive"].toBool() &&
          data["nativeKeyWindow"].toInteger() == windowId && sourceFocused(sourcePid, title)) {
        QTextStream(stdout) << "Verified active source PID=" << sourcePid << " window=" << windowId
                            << " with matching AppKit key window and AX focus\n";
        phase = Phase::Input;
        phaseClock.restart();
        auto send = [&](QJsonObject object) {
          object["geometry"] = static_cast<qint64>(geometry);
          object["focusRevision"] = static_cast<qint64>(focusRevision);
          host.input(session, epoch, object);
        };
        send({{"kind", "key"}, {"code", Qt::Key_F8}, {"down", true}, {"repeat", false}, {"modifiers", 0}});
        send({{"kind", "key"}, {"code", Qt::Key_F8}, {"down", false}, {"repeat", false}, {"modifiers", 0}});
        send({{"kind", "key"}, {"code", Qt::Key_A}, {"text", "a"}, {"down", true}, {"repeat", false}, {"modifiers", 0}}
        );
        send({{"kind", "move"}, {"x", 100}, {"y", 100}, {"modifiers", 0}});
        send({{"kind", "button"}, {"code", 0}, {"x", 100}, {"y", 100}, {"down", true}, {"modifiers", 0}});
        send({{"kind", "move"}, {"x", 120}, {"y", 140}, {"modifiers", 0}});
        send({{"kind", "button"}, {"code", 0}, {"x", 120}, {"y", 140}, {"down", false}, {"modifiers", 0}});
        send({{"kind", "scroll"}, {"x", 120}, {"y", 140}, {"dx", 0}, {"dy", 60}, {"modifiers", 0}});
        send({{"kind", "button"}, {"code", 1}, {"x", 160}, {"y", 180}, {"down", true}, {"modifiers", 0}});
        // releaseAll must synthesize both missing ups on a lost connection.
        host.releaseAll();
      } else if (phaseClock.elapsed() >= 3000) {
        QTextStream(stderr) << "Source focus verification failed: "
                            << QJsonDocument(data).toJson(QJsonDocument::Compact) << '\n';
        restore();
      }
      return;
    }
    if (phase == Phase::Input) {
      const auto height = original.size.height;
      inputVerified =
          nativeKeys(data, windowId) && data["keyDown"].toInt() >= 2 && data["keyUp"].toInt() >= 2 &&
          data["heldKeys"].toInt(-1) == 0 && data["heldButtons"].toInt(-1) == 0 && data["mouseDown"].toInt() >= 2 &&
          data["mouseUp"].toInt() >= 2 && data["scroll"].toInt() >= 1 &&
          nativePointer(data, 5, windowId, 100, height - 100) && nativePointer(data, 1, windowId, 100, height - 100) &&
          nativePointer(data, 6, windowId, 120, height - 140) && nativePointer(data, 2, windowId, 120, height - 140) &&
          nativePointer(data, 22, windowId, 120, height - 140, 60) &&
          nativePointer(data, 3, windowId, 160, height - 180) && nativePointer(data, 4, windowId, 160, height - 180);
      if (inputVerified || phaseClock.elapsed() >= 3000) {
        QTextStream(inputVerified ? stdout : stderr)
            << (inputVerified ? "Verified targeted input: " : "Input verification failed: ")
            << QJsonDocument(data).toJson(QJsonDocument::Compact) << '\n';
        restore();
      }
      return;
    }
    const auto frame = ownWindow(windowId, sourcePid, title);
    restored = closed && frame && CGRectEqualToRect(*frame, original) && host.recover();
    if (restored || phaseClock.elapsed() >= 3000) {
      phase = Phase::Done;
      exitCode = inputVerified && restored && !nativeError ? 0 : 1;
      QTextStream(restored ? stdout : stderr)
          << (restored ? "Verified original window frame and empty recovery journal\n"
                       : "Window frame/journal restoration verification failed\n");
      app.quit();
    }
  });
  progress.start(20);
  QTimer catalog;
  QObject::connect(&catalog, &QTimer::timeout, &app, [&] {
    if (!requested)
      host.refresh();
  });
  catalog.start(500);
  QTimer::singleShot(45000, &app, [&] {
    QTextStream(stderr) << "Smoke timed out: source PID=" << source.processId() << " state=" << source.state()
                        << " error=" << source.errorString() << " diagnostics=" << sourceDiagnostics << '\n';
    app.quit();
  });
  app.exec();
  host.releaseAll();
  host.restoreAll();
  source.terminate();
  if (!source.waitForFinished(2000)) {
    source.kill();
    source.waitForFinished();
  }
  if (!decoded || !committed || !focused || !restored)
    exitCode = 1;
  QTextStream(stdout) << (exitCode ? "FAIL" : "PASS")
                      << ": native capture, H.264, focus, targeted input, release, restore\n";
  return exitCode;
}
