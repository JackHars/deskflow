/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "Transport.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSslConfiguration>
#include <QSslError>
#include <QTemporaryFile>
#include <QUuid>
#include <algorithm>
#ifdef Q_OS_MACOS
#include <Security/Security.h>
#include <memory>
#include <type_traits>
#endif
#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

namespace deskflow::windowshare {
namespace {
QByteArray readFile(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
    throw std::runtime_error("Cannot read identity file");
  return file.readAll();
}
bool equalBytes(const QByteArray &a, const QByteArray &b)
{
  if (a.size() != b.size())
    return false;
  unsigned diff = 0;
  for (qsizetype i = 0; i < a.size(); ++i)
    diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
  return diff == 0;
}
#ifdef Q_OS_MACOS
template <typename T> using ScopedCF = std::unique_ptr<std::remove_pointer_t<T>, decltype(&CFRelease)>;

QString validatePinnedAnchor(const QSslCertificate &certificate, bool server)
{
  const auto der = certificate.toDer();
  const ScopedCF<CFDataRef> data(
      CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(der.constData()), der.size()), CFRelease
  );
  if (!data)
    return QStringLiteral("Cannot allocate certificate data");
  const ScopedCF<SecCertificateRef> leaf(SecCertificateCreateWithData(kCFAllocatorDefault, data.get()), CFRelease);
  const ScopedCF<SecPolicyRef> policy(SecPolicyCreateSSL(server, nullptr), CFRelease);
  if (!leaf || !policy)
    return QStringLiteral("Cannot create pinned certificate policy");
  const void *anchors[] = {leaf.get()};
  const ScopedCF<CFArrayRef> anchorArray(
      CFArrayCreate(kCFAllocatorDefault, anchors, 1, &kCFTypeArrayCallBacks), CFRelease
  );
  if (!anchorArray)
    return QStringLiteral("Cannot allocate pinned certificate anchor");
  SecTrustRef rawTrust = nullptr;
  const auto status = SecTrustCreateWithCertificates(leaf.get(), policy.get(), &rawTrust);
  const ScopedCF<SecTrustRef> trust(rawTrust, CFRelease);
  if (status != errSecSuccess || !trust ||
      SecTrustSetAnchorCertificates(trust.get(), anchorArray.get()) != errSecSuccess ||
      SecTrustSetAnchorCertificatesOnly(trust.get(), true) != errSecSuccess ||
      SecTrustSetNetworkFetchAllowed(trust.get(), false) != errSecSuccess)
    return QStringLiteral("Cannot configure pinned certificate trust");
  // This trust object is private to this evaluation. It changes no keychains
  // or process-wide anchors, and cannot fetch certificates over the network.
  CFErrorRef rawError = nullptr;
  const bool valid = SecTrustEvaluateWithError(trust.get(), &rawError);
  const ScopedCF<CFErrorRef> error(rawError, CFRelease);
  if (valid)
    return {};
  return QStringLiteral("Pinned certificate TLS policy failed (Security error %1)")
      .arg(error ? CFErrorGetCode(error.get()) : 0);
}
#endif
} // namespace
Identity Identity::load(const QString &directory)
{
  const auto keyPath = QDir(directory).filePath("key.pem");
#ifndef Q_OS_WIN
  struct stat info{};
  if (::stat(QFile::encodeName(keyPath).constData(), &info) != 0 || (info.st_mode & 0077) != 0)
    throw std::runtime_error("Private key must be readable by its owner only (chmod 600)");
#endif
  Identity result{
      QSslCertificate(readFile(QDir(directory).filePath("cert.pem")), QSsl::Pem),
      QSslKey(readFile(keyPath), QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey)
  };
  const auto now = QDateTime::currentDateTimeUtc();
  if (result.certificate.isNull() || result.key.isNull() || result.certificate.expiryDate() <= now ||
      result.certificate.effectiveDate() > now)
    throw std::runtime_error("Invalid or expired identity");
  return result;
}
QString Identity::fingerprint() const
{
  return QString::fromLatin1(certificate.digest(QCryptographicHash::Sha256).toHex());
}
void initializeIdentity(const QString &directory)
{
  QDir dir(directory);
  if (dir.exists("cert.pem") || dir.exists("key.pem"))
    throw std::runtime_error("Identity already exists; refusing overwrite");
  if (!QDir().mkpath(directory) ||
      !QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
    throw std::runtime_error("Cannot create private identity directory");
  // Use a private, temporary config for both OpenSSL and Apple's LibreSSL.
  // Explicit TLS purposes are required by the macOS server trust policy.
  QTemporaryFile configuration(dir.filePath("certificate-XXXXXX.cnf"));
  const QByteArray settings(
      "[req]\nprompt=no\ndistinguished_name=subject\nx509_extensions=identity\n"
      "[subject]\nCN=DeskflowWindowShare\n[identity]\nbasicConstraints=critical,CA:FALSE\n"
      "keyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth,clientAuth\n"
  );
  if (!configuration.open() || configuration.write(settings) != settings.size() || !configuration.flush())
    throw std::runtime_error("Cannot create identity certificate configuration");
  configuration.close();
  // Create in a private directory. Never pass a passphrase or private key in argv.
  QProcess process;
  process.start(
      "openssl",
      {"req", "-x509", "-newkey", "rsa:3072", "-nodes", "-sha256", "-days", "365", "-subj", "/CN=DeskflowWindowShare",
       "-config", configuration.fileName(), "-keyout", dir.filePath("key.pem"), "-out", dir.filePath("cert.pem")}
  );
  if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    process.kill();
    process.waitForFinished();
    throw std::runtime_error("openssl identity generation failed");
  }
  QFile::setPermissions(dir.filePath("key.pem"), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  QFile::setPermissions(dir.filePath("cert.pem"), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
QByteArray parseFingerprint(QString fingerprint)
{
  fingerprint.remove(':');
  fingerprint = fingerprint.trimmed().toLower();
  if (fingerprint.size() != 64)
    throw std::runtime_error("Expected a 64-hex-character SHA-256 fingerprint");
  for (const QChar c : fingerprint)
    if (!QStringLiteral("0123456789abcdef").contains(c))
      throw std::runtime_error("Malformed certificate fingerprint");
  return QByteArray::fromHex(fingerprint.toLatin1());
}
std::vector<std::uint8_t> jsonBytes(const QJsonObject &object)
{
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
  return {
      reinterpret_cast<const std::uint8_t *>(bytes.constData()),
      reinterpret_cast<const std::uint8_t *>(bytes.constData()) + bytes.size()
  };
}
QJsonObject parseObject(const Packet &packet)
{
  if (packet.payload.size() > maxControlPayload)
    throw std::runtime_error("Control message too large");
  QJsonParseError error{};
  const auto document = QJsonDocument::fromJson(
      QByteArray(reinterpret_cast<const char *>(packet.payload.data()), static_cast<qsizetype>(packet.payload.size())),
      &error
  );
  if (error.error != QJsonParseError::NoError || !document.isObject())
    throw std::runtime_error("Expected JSON object");
  return document.object();
}
QByteArray encodeFramePayload(std::uint64_t geometry, const QByteArray &accessUnit)
{
  if (geometry == 0 || geometry > 9007199254740991ULL || accessUnit.isEmpty() || accessUnit.size() > maxPayload - 8)
    throw std::runtime_error("Invalid media geometry or access unit size");
  QByteArray result(8, Qt::Uninitialized);
  for (int i = 0; i < 8; ++i)
    result[i] = static_cast<char>(geometry >> (56 - i * 8));
  result.append(accessUnit);
  return result;
}
FramePayload parseFramePayload(const Packet &packet)
{
  if (packet.type != MessageType::Frame || packet.payload.size() <= 8 || packet.payload.size() > maxPayload)
    throw std::runtime_error("Invalid media envelope");
  FramePayload result;
  for (int i = 0; i < 8; ++i)
    result.geometry = (result.geometry << 8) | packet.payload[static_cast<std::size_t>(i)];
  if (result.geometry == 0 || result.geometry > 9007199254740991ULL)
    throw std::runtime_error("Invalid media geometry");
  result.accessUnit = QByteArray(
      reinterpret_cast<const char *>(packet.payload.data() + 8), static_cast<qsizetype>(packet.payload.size() - 8)
  );
  return result;
}
Channel::Channel(Identity identity, QByteArray peerPin, QObject *parent) : QObject(parent), m_pin(std::move(peerPin))
{
  if (m_pin.size() != 32 || !QSslSocket::supportsSsl())
    throw std::runtime_error("TLS or peer pin unavailable");
  auto configuration = m_socket.sslConfiguration();
  configuration.setProtocol(QSsl::TlsV1_2OrLater);
  configuration.setLocalCertificate(identity.certificate);
  configuration.setPrivateKey(identity.key);
  configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
  m_socket.setSslConfiguration(configuration);
  m_socket.setReadBufferSize(maxPayload + headerSize);
  connect(&m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
    const auto certificateError = peerCertificateError();
    if (!certificateError.isEmpty()) {
      close(certificateError, CloseCause::Authentication);
      return;
    }
    // The exact leaf pin replaces public CA and DNS-name trust. Every other
    // error remains fatal unless an independent, bounded evaluation succeeds.
    for (const auto &error : errors) {
      bool allowed = error.error() == QSslError::SelfSignedCertificate ||
                     error.error() == QSslError::SelfSignedCertificateInChain ||
                     error.error() == QSslError::HostNameMismatch;
      QString detail;
#ifdef Q_OS_MACOS
      // Qt SecureTransport collapses SecTrust failures (including bad purpose)
      // into CertificateUntrusted. Only a single self-signed, already pinned
      // leaf can be retried as its own anchor under the peer's TLS role policy.
      // Do not waive generic trust failures on other backends or CA chains.
      const auto certificate = m_socket.peerCertificate();
      if (error.error() == QSslError::CertificateUntrusted && QSslSocket::activeBackend() == "securetransport" &&
          m_socket.peerCertificateChain().size() == 1 && certificate.isSelfSigned() &&
          error.certificate() == certificate) {
        detail = validatePinnedAnchor(certificate, m_socket.mode() == QSslSocket::SslClientMode);
        allowed = detail.isEmpty();
      }
#endif
      if (!allowed) {
        close(
            QStringLiteral("TLS certificate validation failed (%1, error %2): %3")
                .arg(QSslSocket::activeBackend())
                .arg(static_cast<int>(error.error()))
                .arg(detail.isEmpty() ? error.errorString() : detail),
            CloseCause::Authentication
        );
        return;
      }
    }
    m_socket.ignoreSslErrors(errors);
  });
  connect(&m_socket, &QSslSocket::encrypted, this, [this] {
    const auto certificateError = peerCertificateError();
    if (m_closed || !certificateError.isEmpty()) {
      close(certificateError, CloseCause::Authentication);
      return;
    }
    m_authenticated = true;
    m_lastRead.restart();
    if (onAuthenticated)
      onAuthenticated();
  });
  connect(&m_socket, &QSslSocket::readyRead, this, [this] {
    if (!m_authenticated || m_closed) {
      close("Data before authenticated TLS", CloseCause::Protocol);
      return;
    }
    m_lastRead.restart();
    try {
      const auto bytes = m_socket.readAll();
      m_decoder.feed(
          {reinterpret_cast<const std::uint8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size())},
          [this](Packet packet) {
            if (m_closed || !m_sequences.accept(packet.sequence))
              throw std::runtime_error("Replayed or out-of-order packet");
            if (packet.type == MessageType::Ping) {
              send(MessageType::Pong);
              return;
            }
            if (packet.type == MessageType::Pong)
              return;
            if (onPacket)
              onPacket(std::move(packet));
          }
      );
    } catch (const std::exception &error) {
      close(QString::fromUtf8(error.what()), CloseCause::Protocol);
    }
  });
  connect(&m_socket, &QSslSocket::disconnected, this, [this] {
    close("Peer disconnected", m_authenticated ? CloseCause::Network : CloseCause::Authentication);
  });
  connect(&m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
    auto cause = CloseCause::Local;
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
    case QAbstractSocket::HostNotFoundError:
    case QAbstractSocket::SocketTimeoutError:
    case QAbstractSocket::NetworkError:
    case QAbstractSocket::TemporaryError:
      cause = CloseCause::Network;
      break;
    case QAbstractSocket::RemoteHostClosedError:
      // An EOF before peer authentication may be a rejected TLS identity.
      cause = m_authenticated ? CloseCause::Network : CloseCause::Authentication;
      break;
    case QAbstractSocket::SslHandshakeFailedError:
      cause = CloseCause::Authentication;
      break;
    default:
      break;
    }
    close(m_socket.errorString(), cause);
  });
  m_lastRead.start();
  m_watchdog.setInterval(1000);
  connect(&m_watchdog, &QTimer::timeout, this, [this] {
    if (m_lastRead.elapsed() > 6000) {
      const bool connecting =
          m_socket.state() == QAbstractSocket::HostLookupState || m_socket.state() == QAbstractSocket::ConnectingState;
      close(
          m_authenticated ? "Peer heartbeat timed out" : "Connection or TLS handshake timed out",
          m_authenticated || connecting ? CloseCause::Network : CloseCause::Authentication
      );
    } else if (m_authenticated && pendingOutput() == 0)
      send(MessageType::Ping);
  });
  m_watchdog.start();
}
QString Channel::peerCertificateError() const
{
  const auto certificate = m_socket.peerCertificate();
  const auto now = QDateTime::currentDateTimeUtc();
  if (certificate.isNull() || !equalBytes(certificate.digest(QCryptographicHash::Sha256), m_pin))
    return QStringLiteral("Peer certificate does not match the approved fingerprint");
  if (!certificate.effectiveDate().isValid() || !certificate.expiryDate().isValid())
    return QStringLiteral("Peer certificate has invalid validity dates");
  if (certificate.effectiveDate() > now)
    return QStringLiteral("Peer certificate is not yet valid");
  if (certificate.expiryDate() <= now)
    return QStringLiteral("Peer certificate has expired");
  if (certificate.isBlacklisted())
    return QStringLiteral("Peer certificate is blacklisted");
  return {};
}
void Channel::connectTo(const QString &host, quint16 port)
{
  m_socket.connectToHostEncrypted(host, port);
}
void Channel::accept(qintptr descriptor)
{
  if (!m_socket.setSocketDescriptor(descriptor)) {
    close("Cannot accept peer");
    return;
  }
  m_socket.startServerEncryption();
}
bool Channel::sendPacket(Packet packet)
{
  if (!m_authenticated || m_closed)
    return false;
  // One bounded video access unit may already be in the socket. Reserve a
  // separate control allowance so its focus/release reply cannot accidentally
  // disconnect a healthy peer just because an IDR exceeded 256 KiB.
  if (pendingOutput() > maxPayload + 256 * 1024) {
    if (packet.type == MessageType::Frame)
      return false;
    // Heartbeats may share the media socket but must not disconnect a slow frame.
    if (packet.type == MessageType::Ping || packet.type == MessageType::Pong)
      return false;
    close("Control peer is not draining its queue");
    return false;
  }
  packet.sequence = ++m_outSequence;
  const auto bytes = encode(packet);
  if (m_socket.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size())) !=
      static_cast<qint64>(bytes.size())) {
    close("Cannot queue a complete protocol packet");
    return false;
  }
  return true;
}
qint64 Channel::pendingOutput() const
{
  // QSslSocket moves plaintext out of bytesToWrite() as it encrypts it. Its
  // separate encrypted queue can still be blocked by a slow TCP peer.
  return m_socket.bytesToWrite() + m_socket.encryptedBytesToWrite();
}
bool Channel::readyForFrame() const
{
  return m_authenticated && !m_closed && pendingOutput() == 0;
}
bool Channel::send(MessageType type, const QJsonObject &object, std::uint64_t session, std::uint64_t epoch)
{
  try {
    return sendPacket(Packet{type, session, epoch, 0, jsonBytes(object)});
  } catch (const std::exception &error) {
    // Native capture/catalog callbacks are Qt callbacks, outside the inbound
    // packet exception boundary. Never unwind serialization failures into Qt.
    close(QString::fromUtf8(error.what()));
    return false;
  }
}
bool Channel::sendFrame(std::uint64_t session, std::uint64_t epoch, std::uint64_t geometry, const QByteArray &frame)
{
  if (!m_authenticated || m_closed)
    return false;
  try {
    if (geometry == 0 || geometry > 9007199254740991ULL || frame.isEmpty() || frame.size() > maxPayload - 8)
      throw std::runtime_error("Invalid media geometry or access unit size");
    if (!readyForFrame())
      return false;
    const auto payload = encodeFramePayload(geometry, frame);
    return sendPacket(
        Packet{
            MessageType::Frame,
            session,
            epoch,
            0,
            {reinterpret_cast<const std::uint8_t *>(payload.constData()),
             reinterpret_cast<const std::uint8_t *>(payload.constData()) + payload.size()}
        }
    );
  } catch (const std::exception &error) {
    close(QString::fromUtf8(error.what()));
    return false;
  }
}
void Channel::close(const QString &reason, CloseCause cause)
{
  if (m_closed)
    return;
  m_closed = true;
  m_authenticated = false;
  m_watchdog.stop();
  m_socket.abort();
  if (onClosed)
    onClosed(reason, cause);
}
} // namespace deskflow::windowshare
