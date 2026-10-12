/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeHost.h"
#include "VideoDecoder.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

using namespace deskflow::windowshare;
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
  QTemporaryDir directory;
  if (!directory.isValid())
    return 1;
  const auto title = "Deskflow smoke " + QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto events = QDir(directory.path()).filePath("events.json");
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
  NativeHost host(QDir(directory.path()).filePath("journal.json"));
  VideoDecoder decoder;
  std::uint64_t session = 0, epoch = 0, geometry = 0;
  bool requested = false, decoded = false, committed = false, focused = false, restored = false;
  int exitCode = 1;
  host.onError = [&](QString error) { QTextStream(stderr) << "Native host: " << error << '\n'; };
  host.onCatalog = [&](const QVector<Candidate> &windows) {
    if (requested)
      return;
    for (const auto &window : windows)
      if (window.title.contains(title)) {
        requested = true;
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
    host.ready(session, epoch);
  };
  host.onCommitted = [&](std::uint64_t, std::uint64_t) {
    committed = true;
    host.input(session, epoch, {{"kind", "focus"}, {"geometry", static_cast<qint64>(geometry)}, {"request", 1}});
  };
  host.onFocused = [&](std::uint64_t, std::uint64_t, std::uint64_t lease, std::uint64_t request) {
    if (focused || request != 1)
      return;
    focused = true;
    auto send = [&](QJsonObject object) {
      object["geometry"] = static_cast<qint64>(geometry);
      object["focusRevision"] = static_cast<qint64>(lease);
      host.input(session, epoch, object);
    };
    send({{"kind", "key"}, {"code", Qt::Key_F8}, {"down", true}, {"repeat", false}, {"modifiers", 0}});
    send({{"kind", "key"}, {"code", Qt::Key_F8}, {"down", false}, {"repeat", false}, {"modifiers", 0}});
    send({{"kind", "key"}, {"code", Qt::Key_A}, {"text", "a"}, {"down", true}, {"repeat", false}, {"modifiers", 0}});
    send({{"kind", "move"}, {"x", 100}, {"y", 100}, {"modifiers", 0}});
    send({{"kind", "button"}, {"code", 0}, {"x", 100}, {"y", 100}, {"down", true}, {"modifiers", 0}});
    send({{"kind", "button"}, {"code", 0}, {"x", 100}, {"y", 100}, {"down", false}, {"modifiers", 0}});
    send({{"kind", "scroll"}, {"x", 100}, {"y", 100}, {"dx", 0}, {"dy", 60}, {"modifiers", 0}});
    // releaseAll must synthesize the missing up for a lost connection.
    host.releaseAll();
    QTimer::singleShot(700, &app, [&] {
      QFile file(events);
      file.open(QIODevice::ReadOnly);
      const auto data = QJsonDocument::fromJson(file.readAll()).object();
      if (data["keyDown"].toInt() >= 2 && data["keyUp"].toInt() >= 2 && data["heldKeys"].toInt(-1) == 0 &&
          data["mouseDown"].toInt() >= 1 && data["mouseUp"].toInt() >= 1 && data["scroll"].toInt() >= 1) {
        exitCode = 0;
        QTextStream(stdout) << "Delivered targeted keys, mouse, scroll and released held key on disconnect\n";
      } else
        QTextStream(stderr) << "Input verification failed: " << QJsonDocument(data).toJson(QJsonDocument::Compact)
                            << '\n';
      host.restore(session);
      QTimer::singleShot(300, &app, &QApplication::quit);
    });
  };
  host.onClosed = [&](std::uint64_t id) {
    if (id == session)
      restored = true;
  };
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
