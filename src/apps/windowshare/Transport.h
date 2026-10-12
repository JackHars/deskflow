/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "Core.h"
#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QString>
#include <QTimer>
#include <functional>

namespace deskflow::windowshare {
struct Identity {
  QSslCertificate certificate;
  QSslKey key;
  static Identity load(const QString &directory);
  QString fingerprint() const;
};
void initializeIdentity(const QString &directory);
QByteArray parseFingerprint(QString fingerprint);
std::vector<std::uint8_t> jsonBytes(const QJsonObject &object);
QJsonObject parseObject(const Packet &packet);
// Exact leaf-certificate pinning in BOTH directions. Application messages are
// unavailable until TLS completed and the expected fingerprint was checked.
class Channel final : public QObject {
public:
  Channel(Identity identity, QByteArray peerPin, QObject *parent = nullptr);
  void connectTo(const QString &host, quint16 port);
  void accept(qintptr descriptor);
  bool send(MessageType type, const QJsonObject &object = {}, std::uint64_t session = 0, std::uint64_t epoch = 0);
  bool sendFrame(std::uint64_t session, std::uint64_t epoch, const QByteArray &frame);
  bool authenticated() const { return m_authenticated; }
  void close(const QString &reason = QStringLiteral("Disconnected"));
  std::function<void()> onAuthenticated;
  std::function<void(Packet)> onPacket;
  std::function<void(QString)> onClosed;
private:
  bool sendPacket(Packet packet);
  bool pinned() const;
  QSslSocket m_socket;
  QByteArray m_pin;
  Decoder m_decoder;
  SequenceGate m_sequences;
  std::uint64_t m_outSequence = 0;
  bool m_authenticated = false, m_closed = false;
  QElapsedTimer m_lastRead;
  QTimer m_watchdog;
};
} // namespace deskflow::windowshare
