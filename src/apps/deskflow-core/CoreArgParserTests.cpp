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
          {"core", "server", "--new-instance"},
          {"core", "--windowshare-ipc-probe"},
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
    std::puts("Opt-in core argument tests passed");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
