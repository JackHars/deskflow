/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: MIT
 */
// Launched only as an owned Wayland client in the disposable GNOME harness.
// Uses the actual Viewer; no input is posted or routed by this process.
#include "Viewer.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>

using namespace deskflow::windowshare;
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  if (argc != 4 || app.platformName() != QLatin1String("wayland"))
    return 2;
  QFile frame(QString::fromLocal8Bit(argv[1])), output(QString::fromLocal8Bit(argv[3]));
  if (!frame.open(QIODevice::ReadOnly) || frame.size() > 1024 * 1024 ||
      !output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    return 2;
  const auto bytes = frame.readAll();
  const QString commandPath = QString::fromLocal8Bit(argv[2]);
  Viewer viewer(
      1, 1,
      {{"geometry", 1},
       {"width", 640},
       {"height", 480},
       {"windowDrag", 1},
       {"title", "Isolated ordinary drag surface"},
       {"codec", "h264"}}
  );
  const auto report = [&](QJsonObject event) {
    output.write(QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n');
    output.flush();
  };
  viewer.onSend = [&](MessageType type, const QJsonObject &message, std::uint64_t, std::uint64_t) {
    if (type == MessageType::Input && message.value("kind") == QLatin1String("focus")) {
      const auto request = static_cast<std::uint64_t>(message.value("request").toInteger());
      QTimer::singleShot(0, &viewer, [&, request] { viewer.focusAcknowledged(1, request); });
    }
    return true;
  };
  viewer.onError = [&](const QString &error) {
    report({{"error", error}});
    app.exit(1);
  };
  QString stage = "prepared", reported;
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &viewer, [&] {
    QFile commands(commandPath);
    if (commands.open(QIODevice::ReadOnly) && commands.size() <= 64) {
      const auto command = commands.readAll().trimmed();
      if (command == "commit" && stage == "prepared") {
        stage = "committed";
        viewer.committed();
        viewer.update();
      } else if (command == "stop") {
        app.quit();
        return;
      }
    }
    if (reported == stage)
      return;
    if (const auto target = viewer.waylandDragTarget()) {
      report(
          {{"stage", stage},
           {"association", target->windowAssociation},
           {"surface", QString::number(target->surfaceIncarnation)},
           {"resource", QString::number(target->nativeSurface)},
           {"paint", QString::number(viewer.framePaintSerial())}}
      );
      reported = stage;
    }
  });
  poll.start(10);
  QTimer::singleShot(15000, &app, [&] {
    report({{"error", "fixture deadline"}});
    app.exit(1);
  });
  viewer.resize(640, 480);
  viewer.show();
  viewer.receiveFrame(bytes, 1);
  return app.exec();
}
