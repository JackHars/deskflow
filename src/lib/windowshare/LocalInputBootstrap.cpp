/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputBootstrap.h"
#include "CaptureRate.h"
#include "LocalInputPeerTrust.h"
#include "UniqueJson.h"
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
#include <vector>

namespace deskflow::windowshare {
namespace {
constexpr qsizetype maximumEncodedPeerPairs = 4096;
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
LocalInputPin fingerprint(const QString &text)
{
  require(
      text.size() == 64 && std::all_of(
                               text.begin(), text.end(),
                               [](QChar c) {
                                 const auto code = c.unicode();
                                 return (code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') ||
                                        (code >= 'A' && code <= 'F');
                               }
                           ),
      "WindowShare fingerprints must contain exactly 64 hexadecimal digits"
  );
  const auto bytes = QByteArray::fromHex(text.toLatin1());
  LocalInputPin pin{};
  std::copy_n(reinterpret_cast<const std::uint8_t *>(bytes.constData()), pin.size(), pin.begin());
  return pin;
}
QString fingerprintText(const LocalInputPin &pin)
{
  return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(pin.data()), pin.size()).toHex());
}
struct ParsedPeerPairs
{
  LocalInputPeerPolicy policy;
  QByteArray encoded;
};
ParsedPeerPairs peerPairs(const QJsonArray &array)
{
  require(
      array.size() <= static_cast<qsizetype>(LocalInputPeerPolicy::maximumPairs),
      "Too many WindowShare input peer pairs"
  );
  std::vector<LocalInputPeerPair> pairs;
  QJsonArray canonical;
  for (const auto &value : array) {
    require(value.isObject(), "WindowShare input peer pair must be an object");
    const auto row = value.toObject();
    require(
        row.size() == 2 && row.contains("deskflowPeerPin") && row.contains("windowSharePeerPin"),
        "WindowShare input peer pair requires exactly its two fingerprints"
    );
    const LocalInputPeerPair pair{
        fingerprint(stringField(row, "deskflowPeerPin", 64)), fingerprint(stringField(row, "windowSharePeerPin", 64))
    };
    pairs.push_back(pair);
    canonical.append(
        QJsonObject{
            {"deskflowPeerPin", fingerprintText(pair.deskflow)},
            {"windowSharePeerPin", fingerprintText(pair.windowShare)}
        }
    );
  }
  const auto policy = LocalInputPeerPolicy::create(pairs);
  require(policy.has_value(), "WindowShare input peer pairs must be nonzero and one-to-one");
  const auto encoded = QJsonDocument(canonical).toJson(QJsonDocument::Compact);
  require(encoded.size() <= maximumEncodedPeerPairs, "WindowShare input peer pairs exceed the encoded limit");
  return {*policy, encoded};
}
ParsedPeerPairs peerPairs(const QByteArray &json)
{
  require(!json.isEmpty() && json.size() <= maximumEncodedPeerPairs, "Invalid encoded WindowShare input peer pairs");
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(json, &error);
  require(
      error.error == QJsonParseError::NoError && document.isArray(), "WindowShare input peer pairs must be a JSON array"
  );
  requireUniqueJsonKeys(json);
  return peerPairs(document.array());
}
} // namespace

LocalInputCoreBootstrap LocalInputCoreBootstrap::inspect(int argc, char *const argv[])
{
  LocalInputCoreBootstrap result;
  constexpr std::string_view probe = "--windowshare-ipc-probe", admission = "--windowshare-code-admission-probe",
                             config = "--windowshare-host-config";
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (related(argument, probe)) {
      require(argument == probe && argc == 2, "Use --windowshare-ipc-probe alone");
      result.probe = true;
    } else if (related(argument, admission)) {
      require(argument == admission && argc == 2, "Use --windowshare-code-admission-probe alone");
      result.probe = result.codeAdmissionProbe = true;
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
  constexpr std::string_view descriptor = "--core-input-fd=", probe = "--core-input-probe",
                             policy = "--core-input-peer-pairs=";
  std::optional<std::string_view> encodedPairs;
  bool evidenceSupplied = false;
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
    } else if (related(argument, "--input-evidence-config")) {
      require(
          !evidenceSupplied && argument == "--input-evidence-config" && index + 1 < argc && argv[index + 1][0] != '-',
          "Use one canonical --input-evidence-config FILE"
      );
      evidenceSupplied = true;
      const auto path = pathValue(argv[++index]);
      require(QFileInfo(path).isAbsolute(), "Evidence configuration path must be absolute");
    } else if (related(argument, probe)) {
      require(argument == probe && !result.m_probe, "Use one --core-input-probe option");
      result.m_probe = true;
    } else if (related(argument, policy.substr(0, policy.size() - 1))) {
      require(!encodedPairs && argument.starts_with(policy), "Use one canonical --core-input-peer-pairs=JSON");
      encodedPairs = argument.substr(policy.size());
      require(
          !encodedPairs->empty() && encodedPairs->size() <= maximumEncodedPeerPairs,
          "Invalid encoded WindowShare input peer pairs"
      );
    }
  }
  require(
      !result.m_probe || (result.inherited() && argc == 3), "Private input probe requires only its inherited descriptor"
  );
  if (encodedPairs) {
    require(result.inherited() && !result.probe(), "Input peer pairs require a non-probe inherited companion");
    // Parse only after restoring CLOEXEC on the inherited capability. The
    // argv bytes contain public configured pins, never executable/process trust.
    result.m_peerPolicy =
        peerPairs(QByteArray(encodedPairs->data(), static_cast<qsizetype>(encodedPairs->size()))).policy;
    result.m_peerPairsSupplied = true;
  }
  require(
      !evidenceSupplied || (result.inherited() && !result.probe() && !result.m_peerPolicy.empty()),
      "Evidence requires an inherited companion and explicit input peer pairs"
  );
  result.m_evidenceSupplied = evidenceSupplied;
  return result;
}
LocalInputHostBootstrap::~LocalInputHostBootstrap()
{
  if (m_descriptor >= 0)
    ::close(m_descriptor);
}
LocalInputHostBootstrap::LocalInputHostBootstrap(LocalInputHostBootstrap &&other) noexcept
    : m_descriptor(std::exchange(other.m_descriptor, -1)),
      m_probe(other.m_probe),
      m_peerPairsSupplied(other.m_peerPairsSupplied),
      m_evidenceSupplied(other.m_evidenceSupplied),
      m_peerPolicy(other.m_peerPolicy)
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
const LocalInputPeerPolicy &LocalInputHostBootstrap::peerPolicy() const
{
  return m_peerPolicy;
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
  const QSet<QString> allowed{
      "core-input-fd",
      "enable-seamless",
      "listen",
      "port",
      "identity",
      "peer-pin",
      "journal",
      "holding-display",
      "headless",
      "core-input-peer-pairs",
      "share-clipboard-text",
      "capture-fps",
      "input-evidence-config"
  };
  QSet<QString> seen;
  for (const auto &name : names) {
    require(allowed.contains(name) && !seen.contains(name), "Invalid companion host option");
    seen.insert(name);
  }
  for (const auto &name : {"core-input-fd", "enable-seamless", "listen", "identity", "peer-pin", "journal"})
    require(seen.contains(name), "Private input companion requires explicit host configuration");
  require(
      seen.contains("core-input-peer-pairs") == m_peerPairsSupplied &&
          seen.contains("input-evidence-config") == m_evidenceSupplied,
      "Input policy or evidence option changed after bootstrap"
  );
}
std::unique_ptr<LocalInputChannel> LocalInputHostBootstrap::adopt(const std::function<void()> &verifiedSetup)
{
  require(inherited(), "No inherited input descriptor");
  const auto parent = localInputProcess(getppid());
  const auto trust = LocalInputPeerTrust::forPackagedPeer(LocalInputRole::Core);
  require(parent && trust && trust->verify(*parent), "Private input parent failed packaged-code verification");
  require(protectLocalInputDescriptor(m_descriptor), "Inherited input descriptor is no longer a private socketpair");
  if (verifiedSetup)
    verifiedSetup();
  return LocalInputChannel::adoptHost(std::exchange(m_descriptor, -1), *parent);
}

LocalInputHostConfiguration LocalInputHostConfiguration::load(const QString &configPath)
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
  requireUniqueJsonKeys(bytes);
  const auto object = document.object();
  const QSet<QString> allowed{"version",    "listen",         "port",     "identity",       "peerPin",
                              "journal",    "holdingDisplay", "headless", "inputPeerPairs", "shareClipboardText",
                              "captureFps", "inputEvidence"};
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
  const auto pin = fingerprint(stringField(object, "peerPin", 64));
  QStringList arguments{
      "--enable-seamless",
      "--listen",
      address.toString(),
      "--port",
      QString::number(integerField(object, "port", 1, 65535)),
      "--identity",
      identity,
      "--peer-pin",
      fingerprintText(pin),
      "--journal",
      journal
  };
  for (const auto &[key, option] :
       {std::pair{"holdingDisplay", "--holding-display"},
        {"headless", "--headless"},
        {"shareClipboardText", "--share-clipboard-text"}}) {
    if (object.contains(key)) {
      require(object[key].isBool(), "Invalid WindowShare configuration boolean");
      if (object[key].toBool())
        arguments.append(option);
    }
  }
  LocalInputHostConfiguration result;
  if (object.contains("captureFps")) {
    const auto rate = integerField(object, "captureFps", 30, 60);
    require(validCaptureRate(static_cast<unsigned>(rate)), "WindowShare captureFps must be 30 or 60");
    arguments.append({"--capture-fps", QString::number(rate)});
  }
  if (object.contains("inputPeerPairs")) {
    require(object["inputPeerPairs"].isArray(), "WindowShare input peer pairs must be a JSON array");
    const auto parsed = peerPairs(object["inputPeerPairs"].toArray());
    result.m_peerPolicy = parsed.policy;
    arguments.append(QStringLiteral("--core-input-peer-pairs=") + QString::fromUtf8(parsed.encoded));
  }
  if (object.contains("inputEvidence")) {
    require(
        !result.m_peerPolicy.empty() && object["inputEvidence"].isObject(),
        "Input evidence requires explicit input peer pairs"
    );
    const auto evidence = object["inputEvidence"].toObject();
    require(
        evidence.size() == 2 && evidence.contains("core") && evidence.contains("host"),
        "Input evidence requires exactly core and host config paths"
    );
    const auto core = stringField(evidence, "core", 4096), host = stringField(evidence, "host", 4096);
    require(
        QFileInfo(core).isAbsolute() && QFileInfo(host).isAbsolute() && core != host,
        "Input evidence configuration paths must be distinct and absolute"
    );
    result.m_inputEvidenceConfig = core;
    arguments.append({"--input-evidence-config", host});
  }
  result.m_arguments = std::move(arguments);
  return result;
}
const QString &LocalInputHostConfiguration::inputEvidenceConfig() const
{
  return m_inputEvidenceConfig;
}
const QStringList &LocalInputHostConfiguration::hostArguments() const
{
  return m_arguments;
}
const LocalInputPeerPolicy &LocalInputHostConfiguration::peerPolicy() const
{
  return m_peerPolicy;
}
} // namespace deskflow::windowshare
