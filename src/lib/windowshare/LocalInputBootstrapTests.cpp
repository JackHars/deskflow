/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputBootstrap.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>
#include <fcntl.h>
#include <functional>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace deskflow::windowshare;
namespace {
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
void denied(const std::function<void()> &operation)
{
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "Invalid bootstrap/configuration was accepted");
}
struct Arguments
{
  std::vector<QByteArray> text;
  std::vector<char *> pointers;
  Arguments(std::initializer_list<QByteArray> arguments) : text(arguments)
  {
    for (auto &argument : text)
      pointers.push_back(argument.data());
  }
  auto core()
  {
    return LocalInputCoreBootstrap::inspect(static_cast<int>(pointers.size()), pointers.data());
  }
  auto host()
  {
    return LocalInputHostBootstrap::inspect(static_cast<int>(pointers.size()), pointers.data());
  }
};
void coreArguments()
{
  const auto ordinary = Arguments{"core", "server", "--settings", "/tmp/core.ini"}.core();
  check(!ordinary.pair && !ordinary.probe && ordinary.hostConfig.isEmpty(), "Ordinary core created private IPC");
  const auto probe = Arguments{"core", "--windowshare-ipc-probe"}.core();
  check(probe.probe && probe.pair && probe.hostConfig.isEmpty(), "Core probe was not recognized");
  for (const auto &arguments :
       {QStringList{"core", "server", "--windowshare-ipc-probe"},
        {"core", "--windowshare-ipc-probe", "--help"},
        {"core", "-windowshare-ipc-probe"},
        {"core", "--windowshare-ipc-probe=1"},
        {"core", "--windowshare-host-config"},
        {"core", "--windowshare-host-config="},
        {"core", "--windowshare-host-config", "--help"},
        {"core", "-windowshare-host-config", "/tmp/config"},
        {"core", "--windowshare-host-config=/tmp/a", "--windowshare-host-config=/tmp/b"}}) {
    Arguments raw{};
    for (const auto &argument : arguments)
      raw.text.push_back(argument.toUtf8());
    for (auto &argument : raw.text)
      raw.pointers.push_back(argument.data());
    denied([&] { raw.core(); });
  }
  const auto config = Arguments{"core", "server", "--windowshare-host-config=/tmp/a b.json"}.core();
  check(config.pair && !config.probe && config.hostConfig == "/tmp/a b.json", "Configuration path changed");
  const auto configSeparate = Arguments{"core", "--windowshare-host-config", "config.json", "server"}.core();
  check(configSeparate.pair && configSeparate.hostConfig == "config.json", "Separate config option failed");
}
void hostArguments()
{
  check(!Arguments{"host", "--probe"}.host().inherited(), "Standalone diagnostics claimed private IPC");
  for (const auto &argument :
       {"--core-input-probe", "--core-input-fd", "--core-input-fd=", "--core-input-fd=-1", "--core-input-fd=2",
        "--core-input-fd=03", "--core-input-fd=+3", "-core-input-fd=3", "--core-input-fd=2147483648",
        "--core-input-fd=3junk", "--core-input-probe=yes"})
    denied([&] { Arguments{"host", argument}.host(); });
  const auto pair = LocalInputSocketPair::create();
  int fd = ::dup(pair.childDescriptor());
  check(fd >= 3 && !(fcntl(fd, F_GETFD) & FD_CLOEXEC), "Fixture did not inherit an uncloexec descriptor");
  {
    auto host = Arguments{"host", QByteArray("--core-input-fd=") + QByteArray::number(fd), "--core-input-probe"}.host();
    check(host.probe() && host.inherited() && (fcntl(fd, F_GETFD) & FD_CLOEXEC), "Bootstrap did not restore CLOEXEC");
    host.validateOptions({"core-input-fd", "core-input-probe"});
    denied([&] { host.validateOptions({"core-input-fd", "core-input-probe", "help"}); });
  }
  check(fcntl(fd, F_GETFD) == -1, "Inherited descriptor leaked after bootstrap destruction");
  fd = ::dup(pair.childDescriptor());
  {
    auto host = Arguments{"host", QByteArray("--core-input-fd=") + QByteArray::number(fd)}.host();
    const QStringList valid{"core-input-fd", "enable-seamless", "listen", "identity", "peer-pin", "journal"};
    host.validateOptions(valid);
    for (const auto &name :
         {"recover", "init-identity", "request-permissions", "probe", "fingerprint", "connect", "help", "version",
          "quit-after", "export-window"}) {
      auto invalid = valid;
      invalid.append(name);
      denied([&] { host.validateOptions(invalid); });
    }
    auto invalid = valid;
    invalid.removeAll("listen");
    denied([&] { host.validateOptions(invalid); });
    invalid = valid;
    invalid.append("listen");
    denied([&] { host.validateOptions(invalid); });
  }
  fd = ::dup(pair.childDescriptor());
  denied([&] {
    Arguments{"host", QByteArray("--core-input-fd=") + QByteArray::number(fd), "--core-input-probe", "--probe"}.host();
  });
  check(fcntl(fd, F_GETFD) == -1, "Rejected probe leaked inherited descriptor");
  fd = ::open("/dev/null", O_RDONLY);
  denied([&] { Arguments{"host", QByteArray("--core-input-fd=") + QByteArray::number(fd)}.host(); });
  check(fcntl(fd, F_GETFD) == -1, "Rejected non-socket descriptor leaked");
}
void configurations()
{
  QTemporaryDir directory;
  check(directory.isValid(), "Cannot create configuration fixture");
  const auto path = directory.filePath("host.json");
  const auto write = [&](const QByteArray &data) {
    QFile file(path);
    check(
        file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size(), "Cannot write fixture"
    );
  };
  const QJsonObject valid{
      {"version", 1},
      {"listen", "127.0.0.1"},
      {"port", 24810},
      {"identity", "/tmp/private identity"},
      {"peerPin", QString(64, QChar('A'))},
      {"journal", "/tmp/restore.json"},
      {"headless", true}
  };
  write(QJsonDocument(valid).toJson());
  const auto arguments = localInputHostArguments(path);
  check(
      arguments ==
          QStringList{
              "--enable-seamless", "--listen", "127.0.0.1", "--port", "24810", "--identity", "/tmp/private identity",
              "--peer-pin", QString(64, QChar('a')), "--journal", "/tmp/restore.json", "--headless"
          },
      "Configuration forwarded unexpected host arguments"
  );
  for (auto it = valid.begin(); it != valid.end(); ++it) {
    if (it.key() == "headless")
      continue;
    auto missing = valid;
    missing.remove(it.key());
    write(QJsonDocument(missing).toJson());
    denied([&] { localInputHostArguments(path); });
  }
  const std::vector<std::pair<QString, QJsonValue>> rejected{
      {"version", 2},
      {"port", 0},
      {"port", 65536},
      {"port", 1.5},
      {"port", "24810"},
      {"listen", "localhost"},
      {"peerPin", QString(63, QChar('a'))},
      {"peerPin", QString(64, QChar('g'))},
      {"identity", "relative"},
      {"journal", QString("/tmp/a") + QChar(0) + "b"},
      {"headless", "true"},
      {"holdingDisplay", 1},
      {"executable", "/tmp/helper"},
      {"expectedPid", 123},
      {"arguments", "--recover"},
      {"core-input-fd", 9}
  };
  for (const auto &[key, value] : rejected) {
    auto invalid = valid;
    invalid.insert(key, value);
    write(QJsonDocument(invalid).toJson());
    denied([&] { localInputHostArguments(path); });
  }
  auto bytes = QJsonDocument(valid).toJson(QJsonDocument::Compact);
  bytes.insert(1, "\"\\u0076ersion\":1,");
  write(bytes);
  denied([&] { localInputHostArguments(path); });
  for (const auto &data : {QByteArray("[]"), QByteArray("{broken"), QByteArray(16385, ' ')}) {
    write(data);
    denied([&] { localInputHostArguments(path); });
  }
  const auto fifo = QFile::encodeName(directory.filePath("fifo"));
  check(::mkfifo(fifo.constData(), 0600) == 0, "Cannot create nonblocking-read fixture");
  denied([&] { localInputHostArguments(QString::fromUtf8(fifo)); });
  denied([&] { localInputHostArguments(directory.path()); });
  denied([&] { localInputHostArguments(directory.filePath("absent")); });
}
} // namespace
int main()
{
  try {
    coreArguments();
    hostArguments();
    configurations();
    std::puts("Local input early bootstrap and host configuration tests passed");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
