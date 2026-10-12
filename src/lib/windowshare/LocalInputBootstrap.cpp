/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputBootstrap.h"
#include "LocalInputPeerTrust.h"
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fcntl.h>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace deskflow::windowshare {
namespace {
void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
bool related(std::string_view argument, std::string_view name)
{
  return argument.starts_with(name) || argument.starts_with(name.substr(1));
}
QString pathValue(std::string_view value)
{
  require(!value.empty() && value.size() <= 4096, "Invalid WindowShare configuration path");
  const auto path = QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
  require(!path.contains(QChar::ReplacementCharacter), "Invalid WindowShare configuration path encoding");
  return path;
}
QString stringField(const QJsonObject &object, const QString &key, qsizetype maximum)
{
  const auto value = object.value(key);
  require(value.isString(), "WindowShare configuration has a missing or invalid string field");
  const auto text = value.toString();
  require(
      !text.isEmpty() && text.size() <= maximum && !text.contains(QChar(0)), "Invalid WindowShare configuration string"
  );
  return text;
}
int integerField(const QJsonObject &object, const QString &key, int minimum, int maximum)
{
  const auto value = object.value(key);
  const double number = value.toDouble(-1);
  require(
      value.isDouble() && std::isfinite(number) && std::floor(number) == number && number >= minimum &&
          number <= maximum,
      "Invalid WindowShare configuration integer"
  );
  return static_cast<int>(number);
}
void requireUniqueKeys(const QByteArray &json)
{
  // QJsonDocument has already validated syntax; walk its top-level strings to
  // reject duplicate keys, including keys with equivalent Unicode escapes.
  QSet<QString> keys;
  int depth = 0;
  for (qsizetype i = 0; i < json.size(); ++i) {
    const auto c = json[i];
    if (c == '{' || c == '[')
      ++depth;
    else if (c == '}' || c == ']')
      --depth;
    else if (c == '"') {
      const auto start = i++;
      for (; i < json.size() && json[i] != '"'; ++i)
        if (json[i] == '\\')
          ++i;
      auto next = i + 1;
      while (next < json.size() && (json[next] == ' ' || json[next] == '\t' || json[next] == '\r' || json[next] == '\n')
      )
        ++next;
      if (depth == 1 && next < json.size() && json[next] == ':') {
        const auto decoded = QJsonDocument::fromJson('[' + json.mid(start, i - start + 1) + ']');
        const auto key = decoded.array().first().toString();
        require(!keys.contains(key), "Duplicate WindowShare host configuration field");
        keys.insert(key);
      }
    }
  }
}
} // namespace

LocalInputCoreBootstrap LocalInputCoreBootstrap::inspect(int argc, char *const argv[])
{
  LocalInputCoreBootstrap result;
  constexpr std::string_view probe = "--windowshare-ipc-probe", config = "--windowshare-host-config";
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (related(argument, probe)) {
      require(argument == probe && argc == 2, "Use --windowshare-ipc-probe alone");
      result.probe = true;
    } else if (related(argument, config)) {
      require(result.hostConfig.isEmpty(), "Duplicate WindowShare host configuration");
      if (argument == config) {
        require(index + 1 < argc && argv[index + 1][0] != '-', "Missing WindowShare host configuration path");
        result.hostConfig = pathValue(argv[++index]);
      } else {
        require(
            argument.starts_with(config) && argument.size() > config.size() && argument[config.size()] == '=',
            "Use --windowshare-host-config FILE"
        );
        result.hostConfig = pathValue(argument.substr(config.size() + 1));
      }
    }
  }
  if (result.probe || !result.hostConfig.isEmpty())
    result.pair.emplace(LocalInputSocketPair::create());
  return result;
}

LocalInputHostBootstrap LocalInputHostBootstrap::inspect(int argc, char *const argv[])
{
  LocalInputHostBootstrap result;
  constexpr std::string_view descriptor = "--core-input-fd=", probe = "--core-input-probe";
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (related(argument, descriptor.substr(0, descriptor.size() - 1))) {
      require(result.m_descriptor < 0 && argument.starts_with(descriptor), "Use one canonical --core-input-fd=NUMBER");
      const auto value = argument.substr(descriptor.size());
      int fd = -1;
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), fd);
      require(
          !value.empty() && value.front() != '0' && parsed.ec == std::errc() &&
              parsed.ptr == value.data() + value.size() && fd >= 3,
          "Invalid inherited input descriptor"
      );
      result.m_descriptor = fd;
      require(protectLocalInputDescriptor(fd), "Inherited input descriptor is not a private socketpair");
    } else if (related(argument, probe)) {
      require(argument == probe && !result.m_probe, "Use one --core-input-probe option");
      result.m_probe = true;
    }
  }
  require(
      !result.m_probe || (result.inherited() && argc == 3), "Private input probe requires only its inherited descriptor"
  );
  return result;
}
LocalInputHostBootstrap::~LocalInputHostBootstrap()
{
  if (m_descriptor >= 0)
    ::close(m_descriptor);
}
LocalInputHostBootstrap::LocalInputHostBootstrap(LocalInputHostBootstrap &&other) noexcept
    : m_descriptor(std::exchange(other.m_descriptor, -1)),
      m_probe(other.m_probe)
{
}
bool LocalInputHostBootstrap::inherited() const
{
  return m_descriptor >= 0;
}
bool LocalInputHostBootstrap::probe() const
{
  return m_probe;
}
void LocalInputHostBootstrap::validateOptions(const QStringList &names) const
{
  if (!inherited())
    return;
  if (probe()) {
    require(
        names.size() == 2 && names.contains("core-input-fd") && names.contains("core-input-probe"),
        "Invalid private input probe options"
    );
    return;
  }
  const QSet<QString> allowed{"core-input-fd", "enable-seamless", "listen",          "port",    "identity",
                              "peer-pin",      "journal",         "holding-display", "headless"};
  QSet<QString> seen;
  for (const auto &name : names) {
    require(allowed.contains(name) && !seen.contains(name), "Invalid companion host option");
    seen.insert(name);
  }
  for (const auto &name : {"core-input-fd", "enable-seamless", "listen", "identity", "peer-pin", "journal"})
    require(seen.contains(name), "Private input companion requires explicit host configuration");
}
std::unique_ptr<LocalInputChannel> LocalInputHostBootstrap::adopt()
{
  require(inherited(), "No inherited input descriptor");
  const auto parent = localInputProcess(getppid());
  const auto trust = LocalInputPeerTrust::forPackagedPeer(LocalInputRole::Core);
  require(parent && trust && trust->verify(*parent), "Private input parent failed packaged-code verification");
  return LocalInputChannel::adoptHost(std::exchange(m_descriptor, -1), *parent);
}

QStringList localInputHostArguments(const QString &configPath)
{
  constexpr qsizetype maximum = 16 * 1024;
  require(
      !configPath.isEmpty() && configPath.size() <= 4096 && !configPath.contains(QChar(0)),
      "Invalid WindowShare configuration path"
  );
  const auto path = QFile::encodeName(configPath);
  const int descriptor = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  require(descriptor >= 0, "Cannot open WindowShare host configuration");
  struct stat status{};
  QFile file;
  if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size <= 0 ||
      status.st_size > maximum || !file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
    ::close(descriptor);
    throw std::runtime_error("WindowShare host configuration must be a bounded regular file");
  }
  const auto bytes = file.read(maximum + 1);
  require(
      !bytes.isEmpty() && bytes.size() <= maximum && file.atEnd(), "WindowShare host configuration exceeds its limit"
  );
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  require(
      error.error == QJsonParseError::NoError && document.isObject(), "Invalid WindowShare host configuration JSON"
  );
  requireUniqueKeys(bytes);
  const auto object = document.object();
  const QSet<QString> allowed{"version", "listen",  "port",           "identity",
                              "peerPin", "journal", "holdingDisplay", "headless"};
  for (auto it = object.begin(); it != object.end(); ++it)
    require(allowed.contains(it.key()), "Unknown WindowShare host configuration field");
  require(integerField(object, "version", 1, 1) == 1, "Unsupported WindowShare host configuration version");
  const auto listen = stringField(object, "listen", 128);
  QHostAddress address;
  require(address.setAddress(listen), "WindowShare listen address must be an explicit IP address");
  const auto identity = stringField(object, "identity", 4096), journal = stringField(object, "journal", 4096);
  require(
      QFileInfo(identity).isAbsolute() && QFileInfo(journal).isAbsolute(),
      "WindowShare identity and journal paths must be absolute"
  );
  const auto pin = stringField(object, "peerPin", 64);
  require(
      pin.size() == 64 && std::all_of(
                              pin.begin(), pin.end(),
                              [](QChar c) {
                                const auto code = c.unicode();
                                return (code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') ||
                                       (code >= 'A' && code <= 'F');
                              }
                          ),
      "WindowShare peerPin must contain exactly 64 hexadecimal digits"
  );
  QStringList arguments{
      "--enable-seamless",
      "--listen",
      address.toString(),
      "--port",
      QString::number(integerField(object, "port", 1, 65535)),
      "--identity",
      identity,
      "--peer-pin",
      pin.toLower(),
      "--journal",
      journal
  };
  for (const auto &[key, option] : {std::pair{"holdingDisplay", "--holding-display"}, {"headless", "--headless"}}) {
    if (object.contains(key)) {
      require(object[key].isBool(), "Invalid WindowShare configuration boolean");
      if (object[key].toBool())
        arguments.append(option);
    }
  }
  return arguments;
}
} // namespace deskflow::windowshare
