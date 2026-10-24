/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputBootstrap.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
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
  check(!probe.codeAdmissionProbe && !ordinary.codeAdmissionProbe, "Ordinary launch requested code admission probe");
  const auto admission = Arguments{"core", "--windowshare-code-admission-probe"}.core();
  check(
      admission.probe && admission.codeAdmissionProbe && admission.pair && admission.hostConfig.isEmpty(),
      "Inert code-admission probe was not recognized"
  );
  for (const auto &arguments :
       {QStringList{"core", "server", "--windowshare-ipc-probe"},
        {"core", "--windowshare-ipc-probe", "--help"},
        {"core", "-windowshare-ipc-probe"},
        {"core", "--windowshare-ipc-probe=1"},
        {"core", "--windowshare-code-admission-probe", "server"},
        {"core", "--windowshare-code-admission-probe", "--windowshare-ipc-probe"},
        {"core", "--windowshare-code-admission-probe", "--windowshare-host-config=/tmp/a"},
        {"core", "-windowshare-code-admission-probe"},
        {"core", "--windowshare-code-admission-probe=1"},
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
  for (const auto &action : {"--help", "--probe", "--init-identity", "--core-input-probe"})
    denied([&] { Arguments{"host", "--input-evidence-config", "/tmp/evidence.json", action}.host(); });
  denied([&] { Arguments{"host", "--input-evidence-config=/tmp/evidence.json"}.host(); });
  const auto pair = LocalInputSocketPair::create();
  int fd = ::dup(pair.childDescriptor());
  check(fd >= 3 && !(fcntl(fd, F_GETFD) & FD_CLOEXEC), "Fixture did not inherit an uncloexec descriptor");
  {
    auto host = Arguments{"host", QByteArray("--core-input-fd=") + QByteArray::number(fd), "--core-input-probe"}.host();
    check(host.probe() && host.inherited() && (fcntl(fd, F_GETFD) & FD_CLOEXEC), "Bootstrap did not restore CLOEXEC");
    host.validateOptions({"core-input-fd", "core-input-probe"});
    denied([&] { host.validateOptions({"core-input-fd", "core-input-probe", "help"}); });
    denied([&] { host.validateOptions({"core-input-fd", "core-input-probe", "share-clipboard-text"}); });
  }
  check(fcntl(fd, F_GETFD) == -1, "Inherited descriptor leaked after bootstrap destruction");
  fd = ::dup(pair.childDescriptor());
  {
    auto host = Arguments{"host", QByteArray("--core-input-fd=") + QByteArray::number(fd)}.host();
    const QStringList valid{"core-input-fd", "enable-seamless", "listen", "identity", "peer-pin", "journal"};
    host.validateOptions(valid);
    host.validateOptions(valid + QStringList{"capture-fps"});
    denied([&] { host.validateOptions(valid + QStringList{"input-evidence-config"}); });
    denied([&] { host.validateOptions(valid + QStringList{"capture-fps", "capture-fps"}); });
    auto clipboard = valid;
    clipboard.append("share-clipboard-text");
    host.validateOptions(clipboard);
    clipboard.append("share-clipboard-text");
    denied([&] { host.validateOptions(clipboard); });
    for (const auto &name :
         {"recover", "init-identity", "request-permissions", "probe", "fingerprint", "connect", "help", "version",
          "quit-after", "export-window", "session-controls"}) {
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
    bool recorderOpened = false;
    denied([&] { host.adopt([&] { recorderOpened = true; }); });
    check(!recorderOpened, "untrusted parent opened the diagnostic recorder before adoption");
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
  const auto configuration = LocalInputHostConfiguration::load(path);
  const auto &arguments = configuration.hostArguments();
  LocalInputPin unknown{};
  unknown.fill(0xaa);
  check(!configuration.peerPolicy().windowSharePeer(unknown), "Absent input policy approved the host network pin");
  check(
      arguments ==
          QStringList{
              "--enable-seamless", "--listen", "127.0.0.1", "--port", "24810", "--identity", "/tmp/private identity",
              "--peer-pin", QString(64, QChar('a')), "--journal", "/tmp/restore.json", "--headless"
          },
      "Configuration forwarded unexpected host arguments"
  );
  check(!arguments.contains("--share-clipboard-text"), "default host configuration enabled clipboard access");
  check(!arguments.contains("--capture-fps"), "absent host capture rate must retain the app default");
  for (const auto rate : {30, 60}) {
    auto configured = valid;
    configured.insert("captureFps", rate);
    write(QJsonDocument(configured).toJson());
    check(
        LocalInputHostConfiguration::load(path).hostArguments() ==
            arguments + QStringList{"--capture-fps", QString::number(rate)},
        "host configuration must forward the exact selected capture rate"
    );
  }
  for (const bool enabled : {false, true}) {
    auto clipboard = valid;
    clipboard.insert("shareClipboardText", enabled);
    write(QJsonDocument(clipboard).toJson());
    const auto configured = LocalInputHostConfiguration::load(path);
    auto expected = arguments;
    if (enabled)
      expected.append("--share-clipboard-text");
    check(
        configured.hostArguments() == expected && !configured.peerPolicy().windowSharePeer(unknown),
        "explicit clipboard policy must only append its one flag and never grant an input peer"
    );
  }
  for (auto it = valid.begin(); it != valid.end(); ++it) {
    if (it.key() == "headless")
      continue;
    auto missing = valid;
    missing.remove(it.key());
    write(QJsonDocument(missing).toJson());
    denied([&] { LocalInputHostConfiguration::load(path); });
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
      {"shareClipboardText", 1},
      {"shareClipboardText", "true"},
      {"shareClipboardText", QJsonValue{}},
      {"captureFps", 0},
      {"captureFps", 29},
      {"captureFps", 45},
      {"captureFps", 61},
      {"captureFps", 30.5},
      {"captureFps", "60"},
      {"captureFps", true},
      {"captureFps", QJsonValue{}},
      {"executable", "/tmp/helper"},
      {"expectedPid", 123},
      {"arguments", "--recover"},
      {"core-input-fd", 9}
  };
  for (const auto &[key, value] : rejected) {
    auto invalid = valid;
    invalid.insert(key, value);
    write(QJsonDocument(invalid).toJson());
    denied([&] { LocalInputHostConfiguration::load(path); });
  }
  auto bytes = QJsonDocument(valid).toJson(QJsonDocument::Compact);
  bytes.insert(1, "\"\\u0076ersion\":1,");
  write(bytes);
  denied([&] { LocalInputHostConfiguration::load(path); });
  bytes = QJsonDocument(valid).toJson(QJsonDocument::Compact);
  bytes.insert(1, "\"shareClipboardText\":true,\"share\\u0043lipboardText\":false,");
  write(bytes);
  denied([&] { LocalInputHostConfiguration::load(path); });
  bytes = QJsonDocument(valid).toJson(QJsonDocument::Compact);
  bytes.insert(1, "\"captureFps\":30,\"capture\\u0046ps\":60,");
  write(bytes);
  denied([&] { LocalInputHostConfiguration::load(path); });
  for (const auto &data : {QByteArray("[]"), QByteArray("{broken"), QByteArray(16385, ' ')}) {
    write(data);
    denied([&] { LocalInputHostConfiguration::load(path); });
  }
  const auto fifo = QFile::encodeName(directory.filePath("fifo"));
  check(::mkfifo(fifo.constData(), 0600) == 0, "Cannot create nonblocking-read fixture");
  denied([&] { LocalInputHostConfiguration::load(QString::fromUtf8(fifo)); });
  denied([&] { LocalInputHostConfiguration::load(directory.path()); });
  denied([&] { LocalInputHostConfiguration::load(directory.filePath("absent")); });
}
LocalInputPin pin(std::uint8_t byte)
{
  LocalInputPin value{};
  value.fill(byte);
  return value;
}
QString pinText(std::uint8_t byte)
{
  return QString::fromLatin1(QByteArray(32, static_cast<char>(byte)).toHex());
}
QJsonObject peerPair(std::uint8_t core, std::uint8_t host)
{
  return {{"deskflowPeerPin", pinText(core)}, {"windowSharePeerPin", pinText(host)}};
}
void configuredPeerPolicy()
{
  QTemporaryDir directory;
  check(directory.isValid(), "Cannot create policy fixture");
  const auto path = directory.filePath("host.json");
  const QJsonObject base{
      {"version", 1},
      {"listen", "127.0.0.1"},
      {"port", 24810},
      {"identity", "/tmp/identity"},
      {"peerPin", pinText(0xaa)},
      {"journal", "/tmp/restore.json"}
  };
  const auto write = [&](const QByteArray &bytes) {
    QFile file(path);
    check(
        file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(),
        "Cannot write policy"
    );
  };
  const auto configured = [&](QJsonValue pairs) {
    auto json = base;
    json.insert("inputPeerPairs", std::move(pairs));
    write(QJsonDocument(json).toJson(QJsonDocument::Compact));
    return LocalInputHostConfiguration::load(path);
  };
  const auto bootstrap = [&](const LocalInputHostConfiguration &configuration) {
    const auto pair = LocalInputSocketPair::create();
    const int fd = ::dup(pair.childDescriptor());
    check(fd >= 3, "Cannot duplicate inherited policy descriptor");
    Arguments arguments{};
    arguments.text.push_back("host");
    for (const auto &argument : configuration.hostArguments())
      arguments.text.push_back(argument.toUtf8());
    // The generated policy precedes the private descriptor, as in a real launch.
    arguments.text.push_back(QByteArray("--core-input-fd=") + QByteArray::number(fd));
    for (auto &argument : arguments.text)
      arguments.pointers.push_back(argument.data());
    return arguments.host();
  };

  auto row = peerPair(0xab, 0xcd);
  row["deskflowPeerPin"] = pinText(0xab).toUpper();
  const auto configuration = configured(QJsonArray{row});
  check(configuration.peerPolicy().windowSharePeer(pin(0xab)) == pin(0xcd), "Configured core map lost its exact pins");
  check(!configuration.peerPolicy().windowSharePeer(pin(0xaa)), "Host network pin was learned as a Deskflow identity");
  const auto expected = QJsonDocument(QJsonArray{peerPair(0xab, 0xcd)}).toJson(QJsonDocument::Compact);
  check(
      configuration.hostArguments().last() == "--core-input-peer-pairs=" + QString::fromUtf8(expected),
      "Forwarded input policy was not canonical"
  );
  auto host = bootstrap(configuration);
  check(
      host.inherited() && !host.probe() && host.peerPolicy().windowSharePeer(pin(0xab)) == pin(0xcd),
      "Host map diverged"
  );
  const QStringList names{"core-input-fd", "enable-seamless", "listen",  "port",
                          "identity",      "peer-pin",        "journal", "core-input-peer-pairs"};
  host.validateOptions(names);
  auto missingOption = names;
  missingOption.removeAll("core-input-peer-pairs");
  denied([&] { host.validateOptions(missingOption); });
  auto duplicateOption = names;
  duplicateOption.append("core-input-peer-pairs");
  denied([&] { host.validateOptions(duplicateOption); });
  check(configuration.inputEvidenceConfig().isEmpty(), "ordinary host unexpectedly enabled evidence");
  auto evidenceConfig = base;
  evidenceConfig["inputPeerPairs"] = QJsonArray{peerPair(0xab, 0xcd)};
  evidenceConfig["inputEvidence"] =
      QJsonObject{{"core", "/private/core-evidence.json"}, {"host", "/private/host-evidence.json"}};
  write(QJsonDocument(evidenceConfig).toJson(QJsonDocument::Compact));
  const auto evidence = LocalInputHostConfiguration::load(path);
  check(
      evidence.inputEvidenceConfig() == "/private/core-evidence.json" &&
          evidence.hostArguments().mid(evidence.hostArguments().size() - 2) ==
              QStringList{"--input-evidence-config", "/private/host-evidence.json"},
      "explicit evidence configs did not retain separate roles"
  );
  auto evidenceHost = bootstrap(evidence);
  evidenceHost.validateOptions(names + QStringList{"input-evidence-config"});
  denied([&] { evidenceHost.validateOptions(names); });
  denied([&] { evidenceHost.validateOptions(names + QStringList{"input-evidence-config", "input-evidence-config"}); });
  for (const auto &action : {"help", "probe", "init-identity", "recover"})
    denied([&] { evidenceHost.validateOptions(names + QStringList{"input-evidence-config", action}); });
  for (const auto &invalid :
       {QJsonValue{}, QJsonValue("path"), QJsonValue(QJsonObject{{"core", "/private/a"}}),
        QJsonValue(QJsonObject{{"core", "relative"}, {"host", "/private/b"}}),
        QJsonValue(QJsonObject{{"core", "/private/a"}, {"host", "/private/a"}})}) {
    auto changed = evidenceConfig;
    changed["inputEvidence"] = invalid;
    write(QJsonDocument(changed).toJson(QJsonDocument::Compact));
    denied([&] { LocalInputHostConfiguration::load(path); });
  }
  auto noPolicyEvidence = evidenceConfig;
  noPolicyEvidence.remove("inputPeerPairs");
  write(QJsonDocument(noPolicyEvidence).toJson(QJsonDocument::Compact));
  denied([&] { LocalInputHostConfiguration::load(path); });
  auto duplicateEvidence = QJsonDocument(evidenceConfig).toJson(QJsonDocument::Compact);
  duplicateEvidence.insert(1, "\"inputEvidence\":{},");
  write(duplicateEvidence);
  denied([&] { LocalInputHostConfiguration::load(path); });
  auto movedHost = std::move(host);
  check(movedHost.peerPolicy().windowSharePeer(pin(0xab)) == pin(0xcd), "Moving bootstrap lost its policy");

  // Neither endpoint rereads a changed file. Explicitly empty replacement is
  // default-deny, while the retained launch snapshot remains the approved map.
  const auto empty = configured(QJsonArray{});
  auto emptyHost = bootstrap(empty);
  check(
      !empty.peerPolicy().windowSharePeer(pin(0xab)) && !emptyHost.peerPolicy().windowSharePeer(pin(0xab)),
      "Empty policy approved input"
  );
  check(
      configuration.peerPolicy().windowSharePeer(pin(0xab)) == pin(0xcd) &&
          movedHost.peerPolicy().windowSharePeer(pin(0xab)) == pin(0xcd),
      "Configuration file replacement mutated retained policy"
  );
  QJsonArray eight;
  for (std::uint8_t i = 1; i <= 8; ++i)
    eight.append(peerPair(i, static_cast<std::uint8_t>(i + 16)));
  const auto full = configured(eight);
  auto fullHost = bootstrap(full);
  for (std::uint8_t i = 1; i <= 8; ++i)
    check(
        full.peerPolicy().windowSharePeer(pin(i)) == pin(static_cast<std::uint8_t>(i + 16)) &&
            fullHost.peerPolicy().windowSharePeer(pin(i)) == pin(static_cast<std::uint8_t>(i + 16)),
        "Bounded full policy lost a pair"
    );
  eight.append(peerPair(9, 25));
  denied([&] { configured(eight); });
  for (const QJsonValue &value :
       {QJsonValue{}, QJsonValue(false), QJsonValue(1), QJsonValue("[]"), QJsonValue(QJsonObject{})})
    denied([&] { configured(value); });
  for (const QJsonValue &value :
       {QJsonValue{}, QJsonValue(false), QJsonValue(1), QJsonValue("row"), QJsonValue(QJsonArray{})})
    denied([&] { configured(QJsonArray{value}); });
  for (const auto &field : {QString("deskflowPeerPin"), QString("windowSharePeerPin")}) {
    auto missing = peerPair(1, 2);
    missing.remove(field);
    denied([&] { configured(QJsonArray{missing}); });
    for (const QJsonValue &invalid :
         {QJsonValue{}, QJsonValue(1), QJsonValue(""), QJsonValue(QString(63, 'a')), QJsonValue(QString(65, 'a')),
          QJsonValue(QString(64, 'g')), QJsonValue(QString(64, '0')), QJsonValue(" " + pinText(1)),
          QJsonValue("sha256:" + pinText(1)), QJsonValue(QString(64, QChar(0xff11))),
          QJsonValue(pinText(1).left(31) + ":" + pinText(1).mid(32))}) {
      auto bad = peerPair(1, 2);
      bad.insert(field, invalid);
      denied([&] { configured(QJsonArray{bad}); });
    }
  }
  auto unknown = peerPair(1, 2);
  unknown.insert("clientName", "not-authority");
  denied([&] { configured(QJsonArray{unknown}); });
  for (const auto &pairs :
       {QJsonArray{peerPair(1, 2), peerPair(1, 2)}, QJsonArray{peerPair(1, 2), peerPair(1, 3)},
        QJsonArray{peerPair(1, 2), peerPair(3, 2)}})
    denied([&] { configured(pairs); });

  // QJsonDocument accepts duplicate keys; the raw scanner must reject them
  // before either endpoint sees Qt's last-key-wins representation.
  auto duplicateRow = QJsonDocument(QJsonArray{peerPair(1, 2)}).toJson(QJsonDocument::Compact);
  duplicateRow.insert(2, "\"deskflowPeerPin\":\"" + pinText(3).toUtf8() + "\",");
  auto escapedDuplicate = duplicateRow;
  escapedDuplicate.replace(2, 17, "\"\\u0064eskflowPeerPin\"");
  for (const auto &duplicates : {duplicateRow, escapedDuplicate}) {
    auto json = QJsonDocument(base).toJson(QJsonDocument::Compact);
    json.insert(1, "\"inputPeerPairs\":" + duplicates + ',');
    write(json);
    denied([&] { LocalInputHostConfiguration::load(path); });
  }

  for (const auto &payload : {QByteArray("[]"), expected}) {
    denied([&] { Arguments{"host", "--core-input-peer-pairs=" + payload}.host(); });
    denied([&] { Arguments{"host", "--core-input-peer-pairs=" + payload, "--init-identity"}.host(); });
  }
  const auto pair = LocalInputSocketPair::create();
  for (const auto &options :
       {QStringList{"--core-input-peer-pairs=[]", "--core-input-probe"},
        {"--core-input-peer-pairs=[]", "--core-input-peer-pairs=[]"},
        {"--core-input-peer-pairs", "[]"},
        {"-core-input-peer-pairs=[]"},
        {"--core-input-peer-pairs="},
        {"--core-input-peer-pairs={}"},
        {"--core-input-peer-pairs=null"},
        {"--core-input-peer-pairs=["},
        {"--core-input-peer-pairs=" + QString(4097, ' ')},
        {"--core-input-peer-pairs=" + QString::fromUtf8(duplicateRow)},
        {"--core-input-peer-pairs=" + QString::fromUtf8(escapedDuplicate)}}) {
    const int fd = ::dup(pair.childDescriptor());
    check(fd >= 3, "Cannot duplicate rejected policy descriptor");
    Arguments arguments{"host", "--core-input-fd=" + QByteArray::number(fd)};
    arguments.pointers.clear();
    for (const auto &option : options)
      arguments.text.push_back(option.toUtf8());
    for (auto &argument : arguments.text)
      arguments.pointers.push_back(argument.data());
    denied([&] { arguments.host(); });
    check(fcntl(fd, F_GETFD) == -1, "Rejected policy leaked inherited descriptor");
  }
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  try {
    coreArguments();
    hostArguments();
    configurations();
    configuredPeerPolicy();
    std::puts("Local input early bootstrap, strict host configuration and retained dual-pin policy tests passed");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
