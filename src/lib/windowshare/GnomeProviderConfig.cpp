/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "GnomeProviderConfig.h"
#include "UniqueJson.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace deskflow::windowshare {
namespace {
void require(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
QString field(const QJsonObject &object, const QString &name, qsizetype limit)
{
  const auto value = object.value(name);
  const auto text = value.toString();
  require(
      value.isString() && !text.isEmpty() && text.size() <= limit && !text.contains(QChar(0)),
      "Invalid GNOME provider configuration string"
  );
  return text;
}
int integer(const QJsonObject &object, const QString &name, int fallback, int low, int high)
{
  if (!object.contains(name))
    return fallback;
  const auto value = object.value(name);
  const auto number = value.toDouble(-1);
  require(
      value.isDouble() && std::isfinite(number) && std::floor(number) == number && number >= low && number <= high,
      "Invalid GNOME provider configuration integer"
  );
  return static_cast<int>(number);
}
} // namespace

GnomeProviderConfig GnomeProviderConfig::load(const QString &path)
{
  require(
      !path.isEmpty() && path.size() <= 4096 && !path.contains(QChar(0)), "Invalid GNOME provider configuration path"
  );
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "Cannot read GNOME provider configuration");
  const auto bytes = file.read(16385);
  require(!bytes.isEmpty() && bytes.size() <= 16384 && file.atEnd(), "GNOME provider configuration exceeds 16 KiB");
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  require(
      error.error == QJsonParseError::NoError && document.isObject(),
      "GNOME provider configuration must be a JSON object"
  );
  requireUniqueJsonKeys(bytes);
  const auto object = document.object();
  static const QSet<QString> fields{"version",  "viewerExecutable", "connect", "port",
                                    "identity", "peerPin",          "screen",  "exportWindow"};
  for (auto it = object.begin(); it != object.end(); ++it)
    require(fields.contains(it.key()), "Unknown GNOME provider configuration field");
  require(integer(object, "version", 0, 1, 1) == 1, "GNOME provider configuration requires version 1");
  GnomeProviderConfig result;
  const auto executable = field(object, "viewerExecutable", 4096);
  const QFileInfo executableInfo(executable);
  require(
      executableInfo.isAbsolute() && executableInfo.isFile() && executableInfo.isExecutable(),
      "GNOME viewer executable must be an existing absolute executable file"
  );
  result.m_executable = executableInfo.canonicalFilePath();
  require(!result.m_executable.isEmpty(), "Cannot resolve GNOME viewer executable");
  const auto host = field(object, "connect", 253);
  require(
      std::none_of(host.begin(), host.end(), [](QChar c) { return c.isSpace() || c == '/' || c == '\\'; }),
      "Invalid GNOME viewer host"
  );
  const auto identity = field(object, "identity", 4096);
  require(QFileInfo(identity).isAbsolute(), "GNOME viewer identity path must be absolute");
  const auto pin = field(object, "peerPin", 64).toLower();
  require(
      pin.size() == 64 &&
          std::all_of(
              pin.begin(), pin.end(), [](QChar c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
          ) &&
          pin != QString(64, '0'),
      "GNOME viewer peer pin must be a nonzero SHA-256 fingerprint"
  );
  result.m_arguments = {
      "--enable-seamless",
      "--headless",
      "--connect",
      host,
      "--port",
      QString::number(integer(object, "port", 24810, 1, 65535)),
      "--identity",
      identity,
      "--peer-pin",
      pin,
      "--screen",
      QString::number(integer(object, "screen", 0, 0, 63))
  };
  if (object.contains("exportWindow")) {
    const auto window = field(object, "exportWindow", 10).toLatin1();
    std::uint32_t id = 0;
    const auto parsed = std::from_chars(window.constData(), window.constData() + window.size(), id);
    require(
        parsed.ec == std::errc{} && parsed.ptr == window.constData() + window.size() && id && window[0] != '0',
        "GNOME exportWindow must be a canonical nonzero uint32 string"
    );
    result.m_arguments.append({"--export-window", QString::number(id)});
  }
  return result;
}

} // namespace deskflow::windowshare
