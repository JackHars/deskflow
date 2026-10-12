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
  inline static const auto options = {helpOption,   versionOption,          multiInstanceOption,
                                      configOption, windowShareProbeOption, windowShareHostOption};
#elif defined(WINDOWSHARE_X11_OBSERVER)
  inline static const auto windowShareX11Option = QCommandLineOption(
      "windowshare-observe-x11", "Observe the experimental client's native X11 coordinate space (no original input)"
  );
  inline static const auto options = {
      helpOption, versionOption, multiInstanceOption, configOption, windowShareX11Option
  };
#else
  inline static const auto options = {helpOption, versionOption, multiInstanceOption, configOption};
#endif
};
