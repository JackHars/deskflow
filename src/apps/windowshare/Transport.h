/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "AuthenticatedPeerObservation.h"
#include "Core.h"
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QString>
#include <QTimer>
#include <functional>

namespace deskflow::windowshare {
struct Identity
{
  QSslCertificate certificate;
  QSslKey key;
  static Identity load(const QString &directory);
  QString fingerprint() const;
};
void initializeIdentity(const QString &directory);
QByteArray parseFingerprint(QString fingerprint);
std::vector<std::uint8_t> jsonBytes(const QJsonObject &object);
QJsonObject parseObject(const Packet &packet);
struct FramePayload
{
  std::uint64_t geometry = 0;
  QByteArray accessUnit;
};
QByteArray encodeFramePayload(std::uint64_t geometry, const QByteArray &accessUnit);
FramePayload parseFramePayload(const Packet &packet);
enum class CloseCause
{
  Network,
  Authentication,
  Protocol,
  Local
};
// Exact leaf-certificate pinning in BOTH directions. Application messages are
// unavailable until TLS completed and the expected fingerprint was checked.
class Channel final : public QObject
{
  friend struct ChannelTestAccess;
  using Clock = std::chrono::steady_clock;

public:
  Channel(Identity identity, QByteArray peerPin, QObject *parent = nullptr);
  // Destruction silently shuts down transport. Call close() first when the
  // owner needs the terminal notification while its callbacks are still alive.
  ~Channel() override;
  // Each instance owns exactly one connection attempt. Reuse is terminal;
  // reconnect with a new Channel and a new authenticated ticket incarnation.
  void connectTo(const QString &host, quint16 port);
  void accept(qintptr descriptor);
  bool send(MessageType type, const QJsonObject &object = {}, std::uint64_t session = 0, std::uint64_t epoch = 0);
  bool sendFrame(std::uint64_t session, std::uint64_t epoch, std::uint64_t geometry, const QByteArray &frame);
  bool readyForFrame() const;
  bool authenticated() const;
  // Copying/reading this value does not renew its fixed read deadline.
  std::optional<AuthenticatedPeerObservation> authenticatedPeerObservation() const;
  void close(const QString &reason = QStringLiteral("Disconnected"), CloseCause cause = CloseCause::Local);
  std::function<void()> onAuthenticated;
  std::function<void(Packet)> onPacket;
  std::function<void(QString, CloseCause)> onClosed;

private:
  bool beginConnection();
  bool fresh();
  void read();
  void scheduleRead();
  void finishClose(const QString &reason, CloseCause cause, bool deferNotification);
  bool sendPacket(Packet packet);
  QString peerCertificateError() const;
  qint64 pendingOutput() const;
  QSslSocket m_socket;
  QByteArray m_pin;
  Decoder m_decoder;
  SequenceGate m_sequences;
  AuthenticatedPeerIssuer m_issuer;
  std::optional<AuthenticatedPeerObservation> m_observation;
  std::uint64_t m_outSequence = 0;
  bool m_authenticated = false, m_closed = false, m_started = false, m_reading = false, m_readScheduled = false;
  // Private deterministic clock seam; production always uses steady_clock.
  std::function<Clock::time_point()> m_now = [] { return Clock::now(); };
  std::function<Clock::time_point()> m_dispatchNow = [] { return Clock::now(); };
  Clock::time_point m_startedAt{};
  QTimer m_watchdog;
};
} // namespace deskflow::windowshare
