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
#include <QUuid>
#include <algorithm>
#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

namespace deskflow::windowshare {
namespace {
QByteArray readFile(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) throw std::runtime_error("Cannot read identity file");
  return file.readAll();
}
bool equalBytes(const QByteArray &a, const QByteArray &b) {
  if (a.size() != b.size()) return false;
  unsigned diff = 0;
  for (qsizetype i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
  return diff == 0;
}
}
Identity Identity::load(const QString &directory) {
  const auto keyPath = QDir(directory).filePath("key.pem");
#ifndef Q_OS_WIN
  struct stat info{};
  if (::stat(QFile::encodeName(keyPath).constData(), &info) != 0 || (info.st_mode & 0077) != 0)
    throw std::runtime_error("Private key must be readable by its owner only (chmod 600)");
#endif
  Identity result{QSslCertificate(readFile(QDir(directory).filePath("cert.pem")), QSsl::Pem),
                  QSslKey(readFile(keyPath), QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey)};
  const auto now = QDateTime::currentDateTimeUtc();
  if (result.certificate.isNull() || result.key.isNull() || result.certificate.expiryDate() <= now ||
      result.certificate.effectiveDate() > now) throw std::runtime_error("Invalid or expired identity");
  return result;
}
QString Identity::fingerprint() const { return QString::fromLatin1(certificate.digest(QCryptographicHash::Sha256).toHex()); }
void initializeIdentity(const QString &directory) {
  QDir dir(directory);
  if (dir.exists("cert.pem") || dir.exists("key.pem")) throw std::runtime_error("Identity already exists; refusing overwrite");
  if (!QDir().mkpath(directory) || !QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
    throw std::runtime_error("Cannot create private identity directory");
  // Create in a private directory. Never pass a passphrase or private key in argv.
  QProcess process;
  process.start("openssl", {"req", "-x509", "-newkey", "rsa:3072", "-nodes", "-sha256", "-days", "365",
                           "-subj", "/CN=DeskflowWindowShare", "-keyout", dir.filePath("key.pem"), "-out", dir.filePath("cert.pem")});
  if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    process.kill(); process.waitForFinished();
    throw std::runtime_error("openssl identity generation failed");
  }
  QFile::setPermissions(dir.filePath("key.pem"), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  QFile::setPermissions(dir.filePath("cert.pem"), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
QByteArray parseFingerprint(QString fingerprint) {
  fingerprint.remove(':'); fingerprint = fingerprint.trimmed().toLower();
  if (fingerprint.size() != 64) throw std::runtime_error("Expected a 64-hex-character SHA-256 fingerprint");
  for (const QChar c : fingerprint)
    if (!QStringLiteral("0123456789abcdef").contains(c)) throw std::runtime_error("Malformed certificate fingerprint");
  return QByteArray::fromHex(fingerprint.toLatin1());
}
std::vector<std::uint8_t> jsonBytes(const QJsonObject &object) {
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
  return {reinterpret_cast<const std::uint8_t *>(bytes.constData()), reinterpret_cast<const std::uint8_t *>(bytes.constData()) + bytes.size()};
}
QJsonObject parseObject(const Packet &packet) {
  if (packet.payload.size() > maxControlPayload) throw std::runtime_error("Control message too large");
  QJsonParseError error{};
  const auto document = QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char *>(packet.payload.data()), static_cast<qsizetype>(packet.payload.size())), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) throw std::runtime_error("Expected JSON object");
  return document.object();
}
Channel::Channel(Identity identity, QByteArray peerPin, QObject *parent)
    : QObject(parent), m_pin(std::move(peerPin)) {
  if (m_pin.size() != 32 || !QSslSocket::supportsSsl()) throw std::runtime_error("TLS or peer pin unavailable");
  auto configuration = m_socket.sslConfiguration();
  configuration.setProtocol(QSsl::TlsV1_2OrLater);
  configuration.setLocalCertificate(identity.certificate);
  configuration.setPrivateKey(identity.key);
  configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
  m_socket.setSslConfiguration(configuration);
  m_socket.setReadBufferSize(maxPayload + headerSize);
  connect(&m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
    if (!pinned()) { close("Peer certificate does not match the approved fingerprint"); return; }
    // A pinned self-signed certificate is our trust root. Never ignore expiry,
    // revocation or other errors simply because a certificate was presented.
    for (const auto &error : errors) {
      if (error.error() != QSslError::SelfSignedCertificate && error.error() != QSslError::SelfSignedCertificateInChain &&
          error.error() != QSslError::HostNameMismatch) { close("TLS certificate validation failed"); return; }
    }
    m_socket.ignoreSslErrors(errors);
  });
  connect(&m_socket, &QSslSocket::encrypted, this, [this] {
    if (m_closed || !pinned()) { close("Unapproved peer"); return; }
    m_authenticated = true; m_lastRead.restart();
    if (onAuthenticated) onAuthenticated();
  });
  connect(&m_socket, &QSslSocket::readyRead, this, [this] {
    if (!m_authenticated || m_closed) { close("Data before authenticated TLS"); return; }
    m_lastRead.restart();
    try {
      const auto bytes = m_socket.readAll();
      m_decoder.feed({reinterpret_cast<const std::uint8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size())}, [this](Packet packet) {
        if (m_closed || !m_sequences.accept(packet.sequence)) throw std::runtime_error("Replayed or out-of-order packet");
        if (packet.type == MessageType::Ping) { send(MessageType::Pong); return; }
        if (packet.type == MessageType::Pong) return;
        if (onPacket) onPacket(std::move(packet));
      });
    } catch (const std::exception &error) { close(QString::fromUtf8(error.what())); }
  });
  connect(&m_socket, &QSslSocket::disconnected, this, [this] { close(); });
  connect(&m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { close(m_socket.errorString()); });
  m_lastRead.start();
  m_watchdog.setInterval(1000);
  connect(&m_watchdog, &QTimer::timeout, this, [this] {
    if (m_lastRead.elapsed() > 6000) close("Peer heartbeat timed out");
    else if (m_authenticated && m_socket.bytesToWrite() == 0) send(MessageType::Ping);
  });
  m_watchdog.start();
}
bool Channel::pinned() const {
  const auto certificate = m_socket.peerCertificate();
  const auto now = QDateTime::currentDateTimeUtc();
  return !certificate.isNull() && certificate.effectiveDate() <= now && certificate.expiryDate() > now &&
         equalBytes(certificate.digest(QCryptographicHash::Sha256), m_pin);
}
void Channel::connectTo(const QString &host, quint16 port) { m_socket.connectToHostEncrypted(host, port); }
void Channel::accept(qintptr descriptor) {
  if (!m_socket.setSocketDescriptor(descriptor)) { close("Cannot accept peer"); return; }
  m_socket.startServerEncryption();
}
bool Channel::sendPacket(Packet packet) {
  if (!m_authenticated || m_closed) return false;
  if (m_socket.bytesToWrite() > 256 * 1024) {
    if (packet.type == MessageType::Frame) return false;
    // Heartbeats may share the media socket but must not disconnect a slow frame.
    if (packet.type == MessageType::Ping || packet.type == MessageType::Pong) return false;
    close("Control peer is not draining its queue"); return false;
  }
  packet.sequence = ++m_outSequence;
  const auto bytes = encode(packet);
  return m_socket.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size());
}
bool Channel::send(MessageType type, const QJsonObject &object, std::uint64_t session, std::uint64_t epoch) {
  return sendPacket(Packet{type, session, epoch, 0, jsonBytes(object)});
}
bool Channel::sendFrame(std::uint64_t session, std::uint64_t epoch, const QByteArray &frame) {
  if (frame.size() > maxPayload || m_socket.bytesToWrite() != 0) return false;
  return sendPacket(Packet{MessageType::Frame, session, epoch, 0,
    {reinterpret_cast<const std::uint8_t *>(frame.constData()), reinterpret_cast<const std::uint8_t *>(frame.constData()) + frame.size()}});
}
void Channel::close(const QString &reason) {
  if (m_closed) return;
  m_closed = true; m_authenticated = false; m_watchdog.stop(); m_socket.abort();
  if (onClosed) onClosed(reason);
}
} // namespace deskflow::windowshare
