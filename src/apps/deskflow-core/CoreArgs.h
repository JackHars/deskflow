/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QCommandLineOption>
/**
 * @brief The CoreArgs class
 * This class contains args for the CoreArgParser
 */
struct CoreArgs
{
  inline static const auto helpOption = QCommandLineOption({"h", "help"}, "Display Help on the command line");
  inline static const auto versionOption = QCommandLineOption({"v", "version"}, "Display version information");
  inline static const auto multiInstanceOption =
      QCommandLineOption("new-instance", "Skip the check for a running instance, always makes a new instance");
  inline static const auto configOption =
      QCommandLineOption({"s", "settings"}, "override configuration file to use", "configFile");

#ifdef WINDOWSHARE_CORE_COMPANION
  inline static const auto windowShareProbeOption =
      QCommandLineOption("windowshare-ipc-probe", "Verify the packaged private host handshake without input hooks");
  inline static const auto windowShareHostOption = QCommandLineOption(
      "windowshare-host-config", "Launch the experimental packaged window host in server mode", "file"
  );
#endif
#ifdef WINDOWSHARE_X11_OBSERVER
  inline static const auto windowShareX11Option = QCommandLineOption(
      "windowshare-observe-x11", "Observe the experimental client's native X11 coordinate space (no original input)"
  );
#endif
#ifdef WINDOWSHARE_GNOME_PROVIDER
  inline static const auto windowShareGnomeOption = QCommandLineOption(
      "windowshare-gnome-config", "Use the experimental GNOME 50 input session and its owned viewer in client mode",
      "file"
  );
#endif
  inline static const QList<QCommandLineOption> options = {
      helpOption,
      versionOption,
      multiInstanceOption,
      configOption,
#ifdef WINDOWSHARE_CORE_COMPANION
      windowShareProbeOption,
      windowShareHostOption,
#endif
#ifdef WINDOWSHARE_X11_OBSERVER
      windowShareX11Option,
#endif
#ifdef WINDOWSHARE_GNOME_PROVIDER
      windowShareGnomeOption,
#endif
  };
};
