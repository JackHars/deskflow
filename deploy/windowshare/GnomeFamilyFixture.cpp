/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: MIT
 */
// Private GNOME container only: real Viewers, no source app or OS input posting.
#include "Viewer.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <QWindow>
#if defined(WINDOWSHARE_FAMILY_NATIVE_FIXTURE) && (QT_VERSION_MINOR == 8 || QT_VERSION_MINOR == 10)
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <QtWaylandClient/private/wayland-xdg-shell-client-protocol.h>
#include <wayland-client-core.h>
#endif
using namespace deskflow::windowshare;
static bool detachNativeParent(QWindow *window)
{
#if defined(WINDOWSHARE_FAMILY_NATIVE_FIXTURE) && (QT_VERSION_MINOR == 8 || QT_VERSION_MINOR == 10)
  auto *platform = dynamic_cast<QtWaylandClient::QWaylandWindow *>(window->handle());
  auto *role = platform ? platform->shellSurface() : nullptr;
  if (!role)
    return false;
  const auto native = role->surfaceRole();
  auto *const *topLevel = std::any_cast<xdg_toplevel *>(&native);
  if (!topLevel || !*topLevel)
    return false;
  // Fault injection on this fixture's exact owned role. Qt's setter changes
  // its QObject property only; independently exercise the compositor fence.
  xdg_toplevel_set_parent(*topLevel, nullptr);
  return wl_display_flush(platform->display()->wl_display()) >= 0;
#else
  (void)window;
  return false;
#endif
}
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
  const auto metadata = [](int width, int height) {
    return QJsonObject{
        {"geometry", 1}, {"width", width}, {"height", height}, {"codec", "h264"}, {"title", "Isolated attached window"}
    };
  };
  const auto report = [&](QJsonObject event) {
    output.write(QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n');
    output.flush();
  };
  const auto wire = [&](Viewer &viewer) {
    viewer.onSend = [&viewer](MessageType type, const QJsonObject &message, std::uint64_t, std::uint64_t) {
      if (type == MessageType::Ready)
        QTimer::singleShot(0, &viewer, [&viewer] { viewer.committed(); });
      if (type == MessageType::Input && message.value("kind") == QLatin1String("focus")) {
        const auto request = static_cast<std::uint64_t>(message.value("request").toInteger());
        QTimer::singleShot(0, &viewer, [&viewer, request] { viewer.focusAcknowledged(request, request); });
      }
      return true;
    };
    viewer.onError = [&](const QString &error) {
      report({{"error", error}});
      app.exit(1);
    };
  };
  Viewer parent(1, 1, metadata(640, 480));
  wire(parent);
  std::unique_ptr<Viewer> child;
  QString stage = "parent", reported;
  unsigned relation = 0;
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &parent, [&] {
    QFile commands(commandPath);
    if (commands.open(QIODevice::ReadOnly) && commands.size() <= 64) {
      const auto command = commands.readAll().trimmed();
      if ((command == "sheet" || command == "menu") && stage != QString::fromLatin1(command)) {
        if (child) {
          child->disconnected();
          child.reset();
        }
        parent.setFamilyBlocked(false);
        auto childMetadata = metadata(320, 240);
        childMetadata.insert(
            "family",
            windowFamilyJson(
                {{1, 1}, ++relation, command == "sheet" ? WindowFamilyRole::Sheet : WindowFamilyRole::Menu, {40, 30}}
            )
        );
        child = std::make_unique<Viewer>(relation + 1, 1, childMetadata);
        wire(*child);
        child->attachFamilyParent(&parent);
        parent.setFamilyBlocked(true);
        child->show();
        child->receiveFrame(bytes, 1);
        stage = QString::fromLatin1(command);
      } else if (command == "dismiss" && stage != "dismissed") {
        if (child) {
          child->disconnected();
          child.reset();
        }
        parent.setFamilyBlocked(false);
        parent.update();
        stage = "dismissed";
      } else if (command == "reparent" && child && stage != "qt-reparented") {
        child->windowHandle()->setTransientParent(nullptr);
        child->update();
        stage = "qt-reparented";
      } else if (command == "native-reparent" && child && stage != "reparented") {
        if (!detachNativeParent(child->windowHandle())) {
          report({{"error", "Cannot detach fixture's native parent"}});
          app.exit(1);
          return;
        }
        stage = "reparented";
      } else if (command == "stop") {
        app.quit();
        return;
      }
    }
    if (reported == stage)
      return;
    if (stage == "reparented" || stage == "qt-reparented") {
      report({{"stage", stage}, {"childAvailable", bool(child->waylandPreparationTarget())}});
      reported = stage;
      return;
    }
    auto *target = child ? child.get() : &parent;
    if (const auto state = target->waylandPreparationTarget()) {
      report(
          {{"stage", stage},
           {"association", state->windowAssociation},
           {"parentBlocked", parent.familyBlocked()},
           {"resource", QString::number(state->nativeSurface)}}
      );
      reported = stage;
    }
  });
  poll.start(10);
  QTimer::singleShot(25000, &app, [&] {
    report({{"error", "fixture deadline"}});
    app.exit(1);
  });
  parent.resize(640, 480);
  parent.show();
  parent.receiveFrame(bytes, 1);
  return app.exec();
}
