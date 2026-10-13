/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "GnomeProviderConfig.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

using deskflow::windowshare::GnomeProviderConfig;
namespace {
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
struct Fixture
{
  QTemporaryDir directory;
  QString executable = directory.filePath("viewer with spaces");
  QString path = directory.filePath("provider.json");
  QJsonObject valid{
      {"version", 1},
      {"viewerExecutable", executable},
      {"connect", "192.0.2.1"},
      {"identity", directory.filePath("identity")},
      {"peerPin", QString(64, 'A')}
  };
  Fixture()
  {
    check(directory.isValid(), "Cannot create fixture directory");
    QFile file(executable);
    check(
        file.open(QIODevice::WriteOnly) && file.write("fixture, never executed\n") > 0,
        "Cannot create fixture executable"
    );
    file.close();
    check(
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner),
        "Cannot set fixture mode"
    );
  }
  GnomeProviderConfig load(const QByteArray &bytes)
  {
    QFile file(path);
    check(
        file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(),
        "Cannot write fixture config"
    );
    file.close();
    return GnomeProviderConfig::load(path);
  }
  GnomeProviderConfig load(const QJsonObject &object)
  {
    return load(QJsonDocument(object).toJson(QJsonDocument::Compact));
  }
  void deny(const QByteArray &bytes)
  {
    bool rejected = false;
    try {
      load(bytes);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected, "Invalid GNOME configuration accepted");
  }
  void deny(const QJsonObject &object)
  {
    deny(QJsonDocument(object).toJson(QJsonDocument::Compact));
  }
};
void arguments()
{
  Fixture fixture;
  const auto config = fixture.load(fixture.valid);
  check(config.viewerExecutable() == QFileInfo(fixture.executable).canonicalFilePath(), "Executable path changed");
  const QStringList expected{
      "--enable-seamless",
      "--headless",
      "--connect",
      "192.0.2.1",
      "--port",
      "24810",
      "--identity",
      fixture.directory.filePath("identity"),
      "--peer-pin",
      QString(64, 'a'),
      "--screen",
      "0"
  };
  check(config.viewerArguments() == expected, "Fixed viewer arguments differ");
  check(!config.viewerArguments().contains("--share-clipboard-text"), "default viewer configuration enabled clipboard");
  check(!config.viewerArguments().contains("--capture-fps"), "absent capture rate must retain the local app default");
  for (const auto rate : {30, 60}) {
    auto object = fixture.valid;
    object.insert("captureFps", rate);
    check(
        fixture.load(object).viewerArguments() == expected + QStringList{"--capture-fps", QString::number(rate)},
        "GNOME configuration lost the exact local capture rate option"
    );
  }
  for (const bool enabled : {false, true}) {
    auto clipboard = fixture.valid;
    clipboard.insert("shareClipboardText", enabled);
    auto wanted = expected;
    if (enabled)
      wanted.append("--share-clipboard-text");
    const auto configured = fixture.load(clipboard);
    check(
        configured.viewerArguments() == wanted && configured.peerFingerprint() == config.peerFingerprint(),
        "explicit clipboard boolean must only append its one policy flag without changing peer identity"
    );
  }
  auto object = fixture.valid;
  object.insert("port", 65535);
  object.insert("screen", 63);
  object.insert("exportWindow", "4294967295");
  const auto explicitConfig = fixture.load(object);
  check(
      explicitConfig.viewerArguments().at(5) == "65535" && explicitConfig.viewerArguments().at(11) == "63" &&
          explicitConfig.viewerArguments().mid(12) == QStringList{"--export-window", "4294967295"},
      "Explicit viewer arguments lost precision"
  );
}
void rejection()
{
  Fixture fixture;
  for (const auto &field : {"version", "viewerExecutable", "connect", "identity", "peerPin"}) {
    auto object = fixture.valid;
    object.remove(field);
    fixture.deny(object);
  }
  const QList<QPair<QString, QJsonValue>> invalid{
      {"version", 2},
      {"version", "1"},
      {"port", 0},
      {"port", 65536},
      {"port", 2.5},
      {"port", true},
      {"screen", -1},
      {"screen", 64},
      {"exportWindow", "0"},
      {"exportWindow", "01"},
      {"exportWindow", "+1"},
      {"exportWindow", "4294967296"},
      {"exportWindow", "18446744073709551615"},
      {"exportWindow", 1},
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
      {"viewerExecutable", "relative"},
      {"viewerExecutable", fixture.directory.path()},
      {"viewerExecutable", fixture.directory.filePath("missing")},
      {"identity", "relative"},
      {"connect", ""},
      {"connect", "host name"},
      {"connect", "host/path"},
      {"connect", QString("host") + QChar(0)},
      {"peerPin", QString(64, '0')},
      {"peerPin", QString(64, 'g')},
      {"peerPin", QString(63, 'a')},
      {"viewerArguments", QJsonArray{"--platform", "xcb"}},
      {"environment", QJsonObject{}}
  };
  for (const auto &[field, value] : invalid) {
    auto object = fixture.valid;
    object.insert(field, value);
    fixture.deny(object);
  }
  auto duplicate = QJsonDocument(fixture.valid).toJson(QJsonDocument::Compact);
  duplicate.insert(1, "\"vers\\u0069on\":1,");
  fixture.deny(duplicate);
  duplicate = QJsonDocument(fixture.valid).toJson(QJsonDocument::Compact);
  duplicate.insert(1, "\"shareClipboardText\":true,\"share\\u0043lipboardText\":false,");
  fixture.deny(duplicate);
  duplicate = QJsonDocument(fixture.valid).toJson(QJsonDocument::Compact);
  duplicate.insert(1, "\"captureFps\":30,\"capture\\u0046ps\":60,");
  fixture.deny(duplicate);
  fixture.deny(QByteArray("[]"));
  fixture.deny(QByteArray("{bad"));
  fixture.deny(QByteArray(16385, ' '));
  check(
      QFile::setPermissions(fixture.executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner),
      "Cannot remove fixture execute mode"
  );
  fixture.deny(fixture.valid);
}
} // namespace
int main()
{
  try {
    arguments();
    rejection();
    std::puts("PASS: closed GNOME viewer configuration and exact argument construction");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
