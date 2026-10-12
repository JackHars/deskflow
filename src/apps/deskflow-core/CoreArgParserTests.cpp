/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "CoreArgParser.h"
#include <QCoreApplication>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  try {
    for (const auto &arguments :
         {QStringList{"core", "server"},
          {"core", "client"},
          {"core", "--help"},
          {"core", "--version"},
          {"core", "server", "--new-instance"}})
      check(CoreArgParser(arguments).errorText().isEmpty(), "Valid core invocation was rejected");
#ifdef WINDOWSHARE_CORE_COMPANION
    for (const auto &arguments :
         {QStringList{"core", "--windowshare-ipc-probe"},
          {"core", "server", "--windowshare-host-config", "/tmp/host.json"}})
      check(CoreArgParser(arguments).errorText().isEmpty(), "Valid core invocation was rejected");
    for (const auto &arguments :
         {QStringList{"core", "server", "--windowshare-ipc-probe"},
          {"core", "--windowshare-ipc-probe", "--settings", "/tmp/core.ini"},
          {"core", "--windowshare-host-config", "/tmp/host.json"},
          {"core", "client", "--windowshare-host-config", "/tmp/host.json"},
          {"core", "server", "client", "--windowshare-host-config", "/tmp/host.json"},
          {"core", "server", "--help", "--windowshare-host-config", "/tmp/host.json"},
          {"core", "server", "--version", "--windowshare-host-config", "/tmp/host.json"},
          {"core", "server", "--windowshare-host-config="},
          {"core", "server", "--windowshare-host-config", "/tmp/host.json", "--exec=/tmp/host"}})
      check(!CoreArgParser(arguments).errorText().isEmpty(), "Invalid core companion invocation was accepted");
    const CoreArgParser configured({"core", "server", "--windowshare-host-config=/tmp/a b.json"});
    check(configured.windowShareHostConfig() == "/tmp/a b.json", "Core configuration path changed during parsing");
#elif defined(WINDOWSHARE_X11_OBSERVER)
    for (const auto &arguments :
         {QStringList{"core", "client", "--windowshare-observe-x11"},
          {"core", "client", "--windowshare-observe-x11", "--settings", "/tmp/core.ini"}}) {
      const CoreArgParser configured(arguments);
      check(configured.errorText().isEmpty() && configured.windowShareObserveX11(), "X11 observation opt-in failed");
    }
    check(!CoreArgParser({"core", "client"}).windowShareObserveX11(), "X11 observation enabled by default");
    for (const auto &arguments :
         {QStringList{"core", "--windowshare-observe-x11"},
          {"core", "server", "--windowshare-observe-x11"},
          {"core", "client", "server", "--windowshare-observe-x11"},
          {"core", "client", "--windowshare-observe-x11", "--help"},
          {"core", "client", "--windowshare-observe-x11", "--version"},
          {"core", "client", "--windowshare-observe-x11=yes"}})
      check(!CoreArgParser(arguments).errorText().isEmpty(), "Invalid X11 observation invocation was accepted");
#endif
    std::puts("Opt-in core argument tests passed");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
