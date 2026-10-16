/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "KeyboardRouteProtocol.h"
#include "Transport.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QSslConfiguration>
#include <QSslError>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <chrono>
#include <memory>
using namespace deskflow::windowshare;
namespace deskflow::windowshare {
struct AuthenticatedPeerObservationTestAccess
{
  using Clock = std::chrono::steady_clock;
  static bool liveAt(const AuthenticatedPeerObservation &observation, Clock::time_point now)
  {
    return observation.liveAt(now);
  }
  static Clock::time_point readTime(const AuthenticatedPeerObservation &observation)
  {
    return observation.m_read;
  }
};
struct ChannelTestAccess
{
  using Clock = std::chrono::steady_clock;
  static void clock(Channel &channel, const std::shared_ptr<Clock::time_point> &now)
  {
    channel.m_now = [now] { return *now; };
  }
  static void stopWatchdog(Channel &channel)
  {
    channel.m_watchdog.stop();
  }
  static void beforeEncryptedDispatch(Channel &channel, const std::function<void()> &callback)
  {
    QObject::connect(&channel.m_socket, &QSslSocket::encrypted, &channel, callback);
  }
  static void pauseReads(Channel &channel)
  {
    QObject::disconnect(&channel.m_socket, &QSslSocket::readyRead, &channel, nullptr);
  }
  static qint64 available(const Channel &channel)
  {
    return channel.m_socket.bytesAvailable();
  }
  static void queueRead(Channel &channel)
  {
    QTimer::singleShot(0, &channel, [&channel] { channel.read(); });
  }
  static void resumeReads(Channel &channel)
  {
    channel.m_reading = false;
  }
  static void suspendDispatch(Channel &channel)
  {
    channel.m_reading = true;
  }
  static void notifyReads(Channel &channel)
  {
    for (int i = 0; i < 50; ++i)
      channel.m_socket.readyRead();
  }
  static void dispatchClock(Channel &channel, const std::shared_ptr<Clock::time_point> &now)
  {
    channel.m_dispatchNow = [now] { return *now; };
  }
  static qintptr descriptor(const Channel &channel)
  {
    return channel.m_socket.socketDescriptor();
  }
  static qint64 plaintext(const Channel &channel)
  {
    return channel.m_socket.bytesToWrite();
  }
  static qint64 encrypted(const Channel &channel)
  {
    return channel.m_socket.encryptedBytesToWrite();
  }
  static void smallSendBuffer(Channel &channel)
  {
    channel.m_socket.setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4096);
  }
  static void reportCertificateError(Channel &channel, QSslError::SslError error)
  {
    channel.m_socket.sslErrors({QSslError(error, channel.m_socket.peerCertificate())});
  }
  static void malformedPacket(Channel &channel)
  {
    channel.m_socket.write(QByteArray(headerSize, '\0'));
  }
};
} // namespace deskflow::windowshare
namespace {
class Listener : public QTcpServer
{
public:
  std::function<void(qintptr)> incoming;

private:
  void incomingConnection(qintptr descriptor) override
  {
    incoming(descriptor);
  }
};
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
template <typename Predicate> void waitFor(Predicate ready, const char *message)
{
  if (ready())
    return;
  QEventLoop loop;
  QTimer poll, timeout;
  poll.setInterval(2);
  timeout.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (ready())
      loop.quit();
  });
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start();
  timeout.start(5000);
  loop.exec();
  check(ready(), message);
}
struct EvidencePair
{
  using Clock = std::chrono::steady_clock;
  Listener listener;
  std::shared_ptr<Clock::time_point> serverNow = std::make_shared<Clock::time_point>(Clock::now());
  std::shared_ptr<Clock::time_point> peerNow = std::make_shared<Clock::time_point>(*serverNow);
  std::unique_ptr<Channel> server, peer;

  EvidencePair(const Identity &host, const Identity &client)
  {
    peer = std::make_unique<Channel>(client, parseFingerprint(host.fingerprint()));
    ChannelTestAccess::clock(*peer, peerNow);
    check(!peer->authenticatedPeerObservation(), "An unconnected channel exposed evidence");
    listener.incoming = [&](qintptr descriptor) {
      server = std::make_unique<Channel>(host, parseFingerprint(client.fingerprint()));
      ChannelTestAccess::clock(*server, serverNow);
      check(!server->authenticatedPeerObservation(), "An accepted channel exposed evidence before TLS");
      server->accept(descriptor);
    };
    check(listener.listen(QHostAddress::LocalHost, 0), "Cannot listen for evidence fixture");
    peer->connectTo("127.0.0.1", listener.serverPort());
    waitFor([&] { return server && server->authenticated() && peer->authenticated(); }, "Evidence TLS setup failed");
    // Deterministic clock tests exercise at-use and queued-read checks without
    // letting a watchdog callback win the race under test.
    ChannelTestAccess::stopWatchdog(*server);
    ChannelTestAccess::stopWatchdog(*peer);
  }
  ~EvidencePair()
  {
    for (auto *channel : {server.get(), peer.get()}) {
      if (!channel)
        continue;
      channel->onAuthenticated = {};
      channel->onPacket = {};
      channel->onClosed = {};
    }
  }
};

void retainedEvidence(const Identity &host, const Identity &client)
{
  using Access = AuthenticatedPeerObservationTestAccess;
  const AuthenticatedPeerObservation invalid;
  check(!invalid.live() && !invalid.sameConnection(invalid), "Default evidence acquired authority");
  EvidencePair pair(host, client);
  const auto first = *pair.server->authenticatedPeerObservation();
  const auto outgoing = *pair.peer->authenticatedPeerObservation();
  const auto pin = parseFingerprint(client.fingerprint());
  check(
      first.live() && outgoing.live() && first.ticket().incarnation() != 0 &&
          std::equal(
              first.ticket().fingerprint().begin(), first.ticket().fingerprint().end(),
              reinterpret_cast<const std::uint8_t *>(pin.constData())
          ) &&
          !first.sameConnection(outgoing),
      "Authenticated evidence did not preserve the verified leaf and unique connection"
  );
  const auto start = Access::readTime(first);
  *pair.serverNow = start + std::chrono::milliseconds(5000);
  for (int i = 0; i != 8; ++i) {
    const auto again = pair.server->authenticatedPeerObservation();
    check(again && Access::readTime(*again) == start, "Reading evidence renewed its deadline");
  }
  check(pair.server->send(MessageType::Accepted), "Fresh evidence refused an outgoing packet");
  check(
      Access::readTime(*pair.server->authenticatedPeerObservation()) == start,
      "Outgoing traffic renewed the authenticated read deadline"
  );
  int packets = 0;
  pair.server->onPacket = [&](Packet) { ++packets; };
  check(pair.peer->send(MessageType::Hello), "Cannot send freshness fixture packet");
  waitFor([&] { return packets == 1; }, "Authenticated traffic did not refresh observation");
  const auto renewed = *pair.server->authenticatedPeerObservation();
  check(
      renewed.sameConnection(first) && renewed.ticket().incarnation() == first.ticket().incarnation() &&
          Access::readTime(renewed) == *pair.serverNow,
      "A read refresh changed connection identity or used observation access time"
  );
  *pair.serverNow = start + AuthenticatedPeerObservation::freshness;
  check(
      !Access::liveAt(first, *pair.serverNow) && Access::liveAt(renewed, *pair.serverNow) &&
          pair.server->authenticated() && !Access::liveAt(first, start - std::chrono::nanoseconds(1)),
      "A fresh observation revived an expired copy or accepted clock regression"
  );
  *pair.serverNow = Access::readTime(renewed) + AuthenticatedPeerObservation::freshness;
  check(
      !pair.server->authenticated() && !pair.server->readyForFrame() && !pair.server->authenticatedPeerObservation(),
      "An overdue watchdog left authority usable at query time"
  );
  bool revokedBeforeCallback = false;
  pair.server->onClosed = [&](QString, CloseCause cause) {
    revokedBeforeCallback = !first.ticket().live() && !renewed.live() && cause == CloseCause::Network;
  };
  check(
      !pair.server->send(MessageType::Hello) && revokedBeforeCallback &&
          !pair.server->sendFrame(7, 2, 1, QByteArray("frame")),
      "Expired send renewed authority or notified before revocation"
  );
}

void lateReadEvidence(const Identity &host, const Identity &client, bool expireBetweenPackets)
{
  EvidencePair pair(host, client);
  const auto retained = *pair.server->authenticatedPeerObservation();
  const auto start = AuthenticatedPeerObservationTestAccess::readTime(retained);
  ChannelTestAccess::pauseReads(*pair.server);
  int packets = 0, closed = 0;
  bool revokedBeforeCallback = false;
  pair.server->onPacket = [&](Packet) {
    ++packets;
    if (expireBetweenPackets)
      *pair.serverNow = start + AuthenticatedPeerObservation::freshness;
  };
  pair.server->onClosed = [&](QString, CloseCause cause) {
    ++closed;
    revokedBeforeCallback =
        !retained.ticket().live() && !pair.server->authenticatedPeerObservation() && cause == CloseCause::Network;
  };
  check(pair.peer->send(MessageType::Hello), "Cannot queue late-read fixture packet");
  if (expireBetweenPackets)
    check(pair.peer->send(MessageType::Hello), "Cannot queue second fixture packet");
  const auto required = static_cast<qint64>((headerSize + 2) * (expireBetweenPackets ? 2 : 1));
  waitFor(
      [&] { return ChannelTestAccess::available(*pair.server) >= required; }, "TLS bytes did not reach receive buffer"
  );
  ChannelTestAccess::queueRead(*pair.server);
  if (!expireBetweenPackets)
    *pair.serverNow = start + AuthenticatedPeerObservation::freshness;
  waitFor([&] { return closed != 0; }, "Queued late data did not terminate the expired channel");
  check(
      closed == 1 && packets == (expireBetweenPackets ? 1 : 0) && revokedBeforeCallback &&
          !pair.server->authenticatedPeerObservation(),
      "Queued data or a second callback revived expired authenticated authority"
  );
}

void packetBurstYield(const Identity &host, const Identity &client, bool slowCallback, bool expireAfterYield)
{
  EvidencePair pair(host, client);
  ChannelTestAccess::pauseReads(*pair.server);
  const auto retained = *pair.server->authenticatedPeerObservation();
  const int total = slowCallback ? 8 : 128;
  int delivered = 0, deliveredAtYield = 0, closed = 0;
  bool yielded = false, ordered = true;
  const auto workNow = std::make_shared<ChannelTestAccess::Clock::time_point>();
  ChannelTestAccess::dispatchClock(*pair.server, workNow);
  pair.server->onClosed = [&](QString, CloseCause) { ++closed; };
  pair.server->onPacket = [&](Packet packet) {
    ordered = ordered && parseObject(packet)["index"].toInt(-1) == delivered;
    if (++delivered == 1) {
      QTimer::singleShot(0, pair.server.get(), [&] {
        yielded = true;
        deliveredAtYield = delivered;
        if (expireAfterYield)
          *pair.serverNow += AuthenticatedPeerObservation::freshness;
      });
      if (slowCallback)
        *workNow += std::chrono::milliseconds(20);
    }
  };
  qint64 expectedBytes = 0;
  for (int i = 0; i < total; ++i) {
    const QJsonObject object{{"index", i}};
    expectedBytes += headerSize + jsonBytes(object).size();
    check(pair.peer->send(MessageType::Input, object, 7, 2), "Cannot queue input burst");
  }
  waitFor(
      [&] { return ChannelTestAccess::available(*pair.server) >= expectedBytes; },
      "Input burst did not reach TLS receive buffer"
  );
  ChannelTestAccess::queueRead(*pair.server);
  waitFor([&] { return yielded && (delivered == total || closed); }, "Input burst did not finish or expire");
  check(ordered && deliveredAtYield > 0 && deliveredAtYield < total, "Input burst starved independent event-loop work");
  if (expireAfterYield)
    check(
        closed == 1 && delivered == deliveredAtYield && !retained.ticket().live(),
        "Queued burst revived authority after the event-loop yield"
    );
  else
    check(delivered == total && closed == 0, "Yielding lost or terminated valid ordered input");
}

void repeatedReadNotifications(const Identity &host, const Identity &client)
{
  EvidencePair pair(host, client);
  ChannelTestAccess::suspendDispatch(*pair.server);
  int delivered = 0, atYield = 0;
  bool yielded = false;
  pair.server->onPacket = [&](Packet packet) {
    check(parseObject(packet)["index"].toInt(-1) == delivered++, "Coalesced TLS burst lost ordering");
  };
  qint64 expected = 0;
  for (int i = 0; i < 128; ++i) {
    const QJsonObject object{{"index", i}};
    expected += headerSize + jsonBytes(object).size();
    check(pair.peer->send(MessageType::Input, object, 1, 1), "Cannot send repeated-notification TLS burst");
  }
  waitFor([&] { return ChannelTestAccess::available(*pair.server) == expected; }, "TLS burst did not buffer");
  ChannelTestAccess::resumeReads(*pair.server);
  ChannelTestAccess::notifyReads(*pair.server);
  QTimer::singleShot(0, pair.server.get(), [&] {
    yielded = true;
    atYield = delivered;
  });
  waitFor([&] { return yielded && delivered == 128; }, "Coalesced TLS burst did not drain");
  check(atYield > 0 && atYield <= 32, "Repeated TLS notifications bypassed the per-turn work budget");
}

void terminalEvidence(const Identity &host, const Identity &client)
{
  std::optional<AuthenticatedPeerObservation> old;
  for (int mode = 0; mode != 6; ++mode) {
    EvidencePair pair(host, client);
    const auto retained = *pair.server->authenticatedPeerObservation();
    if (old)
      check(
          !old->live() && !retained.sameConnection(*old) &&
              retained.ticket().incarnation() != old->ticket().incarnation(),
          "Reconnect reused or revived the previous connection incarnation"
      );
    old = retained;
    int closed = 0;
    bool revokedBeforeCallback = false;
    pair.server->onClosed = [&](QString, CloseCause) {
      ++closed;
      revokedBeforeCallback = !retained.live() && !retained.ticket().live() && !pair.server->authenticated();
    };
    if (mode == 0)
      pair.server->close("explicit terminal fixture");
    else if (mode == 1)
      pair.peer->close("remote terminal fixture");
    else if (mode == 2)
      ChannelTestAccess::malformedPacket(*pair.peer);
    else if (mode == 3)
      ChannelTestAccess::reportCertificateError(*pair.server, QSslError::CertificateRevoked);
    else if (mode == 4) {
      pair.server.reset();
      check(!retained.live() && closed == 0, "Destructor did not silently revoke retained evidence");
      continue;
    } else
      pair.server->accept(ChannelTestAccess::descriptor(*pair.server));
    waitFor([&] { return closed != 0; }, "Terminal transport event did not notify");
    check(closed == 1 && revokedBeforeCallback, "Terminal callback ran before retained evidence was revoked");
    pair.server->connectTo("127.0.0.1", pair.listener.serverPort());
    check(
        !pair.server->authenticatedPeerObservation() && !retained.live() && closed == 1,
        "A terminated Channel attempted reauthentication"
    );
  }
  EvidencePair pair(host, client);
  const auto retained = *pair.peer->authenticatedPeerObservation();
  pair.peer->connectTo("127.0.0.1", pair.listener.serverPort());
  check(!retained.live() && !pair.peer->authenticated(), "A second connectTo preserved the first authority");
}

void destructiveEvidenceCallbacks(const Identity &host, const Identity &client)
{
  // These are real socket callbacks, not a direct call to the public handler.
  // Destroying the channel must leave neither a Decoder nor Qt socket signal
  // frame accessing the destroyed by-value transport objects.
  for (bool terminal : {false, true}) {
    EvidencePair pair(host, client);
    const auto retained = *pair.server->authenticatedPeerObservation();
    bool called = false;
    const auto state = std::make_shared<int>(37);
    auto destroy = [&, state] {
      pair.server.reset();
      check(*state == 37 && !retained.live(), "Callback capture or evidence survived owner deletion incorrectly");
      called = true;
    };
    if (terminal) {
      pair.server->onClosed = [destroy](QString, CloseCause) { destroy(); };
      pair.peer->close("destroy remote callback fixture");
    } else {
      pair.server->onPacket = [destroy](Packet) { destroy(); };
      check(pair.peer->send(MessageType::Hello), "Cannot send destructive callback packet");
      check(pair.peer->send(MessageType::Hello), "Cannot send post-destruction packet");
    }
    waitFor([&] { return called; }, "Destructive socket callback was not reached");
    check(!pair.server && !retained.live(), "Destroyed callback owner retained authenticated authority");
  }
  EvidencePair pair(host, client);
  const auto retained = *pair.server->authenticatedPeerObservation();
  int cancelled = 0;
  pair.server->onClosed = [&](QString, CloseCause) { ++cancelled; };
  ChannelTestAccess::reportCertificateError(*pair.server, QSslError::CertificateRevoked);
  check(!retained.live() && cancelled == 0, "Socket error did not revoke before deferred notification");
  pair.server->onClosed = {};
  QCoreApplication::processEvents();
  check(cancelled == 0, "A queued terminal callback ignored owner cancellation");
}

void handshakeEvidence(const Identity &host, const Identity &client)
{
  // Expire after the backend emits encrypted(), before the queued application
  // handler runs. A delayed handshake must not mint its first ticket either.
  Listener listener;
  std::unique_ptr<Channel> server;
  Channel peer(client, parseFingerprint(host.fingerprint()));
  auto now = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());
  ChannelTestAccess::clock(peer, now);
  ChannelTestAccess::beforeEncryptedDispatch(peer, [now] { *now += AuthenticatedPeerObservation::freshness; });
  listener.incoming = [&](qintptr descriptor) {
    server = std::make_unique<Channel>(host, parseFingerprint(client.fingerprint()));
    server->accept(descriptor);
  };
  check(listener.listen(QHostAddress::LocalHost, 0), "Cannot listen for handshake evidence fixture");
  int authenticated = 0, closed = 0;
  bool noEvidence = false;
  peer.onAuthenticated = [&] { ++authenticated; };
  peer.onClosed = [&](QString, CloseCause) {
    ++closed;
    noEvidence = !peer.authenticatedPeerObservation();
  };
  peer.connectTo("127.0.0.1", listener.serverPort());
  ChannelTestAccess::stopWatchdog(peer);
  waitFor([&] { return closed != 0; }, "Queued expired handshake did not close");
  check(closed == 1 && authenticated == 0 && noEvidence, "Expired handshake issued authenticated evidence");

  Channel pending(client, parseFingerprint(host.fingerprint()));
  int pendingClosed = 0;
  pending.onClosed = [&](QString, CloseCause) { ++pendingClosed; };
  pending.connectTo("127.0.0.1", listener.serverPort());
  pending.connectTo("127.0.0.1", listener.serverPort());
  check(
      pendingClosed == 1 && !pending.authenticated() && !pending.authenticatedPeerObservation(),
      "Double connection attempt did not terminate pending authentication"
  );
}
void keyboardRouteProtocol()
{
  const auto reject = [](const auto &operation) {
    bool rejected = false;
    try {
      operation();
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected, "Invalid forwarding control or capability was accepted");
  };
  check(!peerSupportsKeyboardRoute({}), "Legacy peer gained keyboard route capability");
  check(peerSupportsKeyboardRoute({{"capabilities", keyboardRouteCapabilities()}}), "Capability not recognized");
  check(
      keyboardRouteCapabilities() == QJsonArray{"keyboard-route-v1"} &&
          keyboardRouteCapabilities(false) == keyboardRouteCapabilities(),
      "Default negotiation changed the legacy keyboard-only capability"
  );
  check(
      keyboardRouteCapabilities(true) == QJsonArray{"keyboard-route-v1", "input-route-v1"},
      "Full-input forwarding requires both independently versioned capabilities"
  );
  check(
      !peerSupportsInputRoute({}) && !peerSupportsInputRoute({{"capabilities", QJsonArray{}}}) &&
          !peerSupportsInputRoute({{"capabilities", keyboardRouteCapabilities()}}) &&
          !peerSupportsInputRoute({{"capabilities", QJsonArray{"input-route-v1"}}}),
      "Absent, legacy or input-only capabilities granted a full-input route"
  );
  const QJsonObject extendedCapabilities{
      {"capabilities", QJsonArray{"future-feature", "input-route-v1", "keyboard-route-v1"}}
  };
  check(
      peerSupportsInputRoute({{"capabilities", keyboardRouteCapabilities(true)}}) &&
          peerSupportsKeyboardRoute({{"capabilities", keyboardRouteCapabilities(true)}}) &&
          peerSupportsInputRoute(extendedCapabilities),
      "Full-input negotiation lost its base capability or depended on capability order"
  );
  check(
      !peerSupportsKeyboardRoute({{"capabilities", QJsonArray{"future-feature"}}}), "Unknown capability granted route"
  );
  for (const auto &value :
       {QJsonValue("keyboard-route-v1"), QJsonValue(QJsonArray{1}),
        QJsonValue(QJsonArray{"keyboard-route-v1", "keyboard-route-v1"}),
        QJsonValue(QJsonArray{"input-route-v1", "input-route-v1"}),
        QJsonValue(QJsonArray{"keyboard-route-v1", "input-route-v1", "input-route-v1"}),
        QJsonValue(QJsonArray{"keyboard-route-v1", "input-route-v1", "future-feature", "future-feature"}),
        QJsonValue(QJsonArray{"keyboard-route-v1", "input-route-v1", ""}),
        QJsonValue(QJsonArray{"keyboard-route-v1", "input-route-v1", QString(65, QChar(u'x'))})}) {
    reject([&] { peerSupportsKeyboardRoute({{"capabilities", value}}); });
    reject([&] { peerSupportsInputRoute({{"capabilities", value}}); });
  }
  auto oversizedCapabilities = keyboardRouteCapabilities(true);
  for (int i = 0; i != 15; ++i)
    oversizedCapabilities.append(QString("future-%1").arg(i));
  reject([&] { peerSupportsInputRoute({{"capabilities", oversizedCapabilities}}); });
  const KeyboardRouteBinding binding{{7, 2}, 9, 3, 4, 5};
  auto allBinding = binding;
  allBinding.scope = ForwardingScope::All;
  check(
      !peerSupportsMappedInput({{"capabilities", keyboardRouteCapabilities(true, true)}}) &&
          peerSupportsMappedInput({{"capabilities", keyboardRouteCapabilities(true, true, true)}}),
      "Mapped endpoint identity was not independently negotiated"
  );
  KeyboardRouteControl mapped{
      allBinding,
      KeyboardRouteAction::Acknowledge,
      KeyboardRouteMode::Original,
      KeyboardRouteResult::Ready,
      KeyboardRouteReason::None,
      std::numeric_limits<std::uint64_t>::max()
  };
  auto mappedObject = encodeKeyboardRouteControl(mapped);
  check(
      mappedObject["viewerConnection"] == "ffffffffffffffff" &&
          parseKeyboardRouteControl({MessageType::InputRoute, 7, 2, 1, jsonBytes(mappedObject)}) == mapped,
      "Viewer connection identity lost full-width precision"
  );
  for (const auto value :
       {QJsonValue(1), QJsonValue("1"), QJsonValue("FFFFFFFFFFFFFFFF"), QJsonValue("0000000000000000"),
        QJsonValue("000000000000000g")}) {
    auto malformed = mappedObject;
    malformed["viewerConnection"] = value;
    reject([&] { parseKeyboardRouteControl({MessageType::InputRoute, 7, 2, 1, jsonBytes(malformed)}); });
  }
  mapped.mode = KeyboardRouteMode::Viewer;
  reject([&] { encodeKeyboardRouteControl(mapped); });
  mapped.mode = KeyboardRouteMode::Original;
  mapped.binding.scope = ForwardingScope::Keyboard;
  reject([&] { encodeKeyboardRouteControl(mapped); });
  check(
      binding.scope == ForwardingScope::Keyboard && validKeyboardRouteBinding(binding) &&
          validKeyboardRouteBinding(allBinding) && binding != allBinding,
      "Scope must be a valid, exact part of the forwarding binding"
  );
  check(
      forwardingRouteMessageType(binding) == MessageType::KeyboardRoute &&
          forwardingRouteMessageType(allBinding) == MessageType::InputRoute,
      "Full-input and legacy fences share a wire message type"
  );
  std::vector<KeyboardRouteControl> controls;
  for (auto mode : {KeyboardRouteMode::Viewer, KeyboardRouteMode::Original}) {
    controls.push_back({binding, KeyboardRouteAction::Request, mode});
    for (auto result :
         {KeyboardRouteResult::Ready, KeyboardRouteResult::Busy, KeyboardRouteResult::Stale,
          KeyboardRouteResult::Denied})
      controls.push_back({binding, KeyboardRouteAction::Acknowledge, mode, result});
  }
  for (auto reason :
       {KeyboardRouteReason::FocusChanged, KeyboardRouteReason::GeometryChanged, KeyboardRouteReason::Hidden,
        KeyboardRouteReason::Returned, KeyboardRouteReason::Disconnected, KeyboardRouteReason::Timeout,
        KeyboardRouteReason::AuthorityLost, KeyboardRouteReason::Failed})
    controls.push_back(
        {binding, KeyboardRouteAction::Withdraw, KeyboardRouteMode::Original, KeyboardRouteResult::None, reason}
    );
  for (const auto scope : {ForwardingScope::Keyboard, ForwardingScope::All}) {
    for (auto control : controls) {
      const auto legacyObject = encodeKeyboardRouteControl(control);
      control.binding.scope = scope;
      const auto object = encodeKeyboardRouteControl(control);
      check(object == legacyObject, "Scope changed the exact legacy JSON schema");
      check(
          !object.contains("scope") && object.size() == (control.action == KeyboardRouteAction::Request ? 6 : 7),
          "Forwarding controls gained unnegotiated payload fields"
      );
      const Packet packet{forwardingRouteMessageType(control.binding), 7, 2, 1, jsonBytes(object)};
      check(parseKeyboardRouteControl(packet) == control, "Forwarding tuple or scope changed on wire");
      Decoder decoder;
      int decoded = 0;
      const auto bytes = encode(packet);
      for (const auto byte : bytes)
        decoder.feed({&byte, 1}, [&](const Packet &received) {
          check(received.type == packet.type, "Forwarding message type changed during fragmented framing");
          check(parseKeyboardRouteControl(received) == control, "Framing changed the exact forwarding tuple");
          ++decoded;
        });
      check(decoded == 1, "Forwarding control was not decoded exactly once");

      auto substituted = packet;
      substituted.type = scope == ForwardingScope::All ? MessageType::KeyboardRoute : MessageType::InputRoute;
      const auto parsed = parseKeyboardRouteControl(substituted);
      auto expected = control;
      expected.binding.scope = scope == ForwardingScope::All ? ForwardingScope::Keyboard : ForwardingScope::All;
      check(
          parsed == expected && parsed != control && parsed.binding != control.binding,
          "A substituted message type retained the original fence authority"
      );
    }
    auto request = controls.front();
    request.binding.scope = scope;
    const auto type = forwardingRouteMessageType(request.binding);
    const auto parse = [&](QJsonObject object) {
      return parseKeyboardRouteControl({type, 7, 2, 1, jsonBytes(object)});
    };
    const auto original = encodeKeyboardRouteControl(request);
    for (const auto &field : {"generation", "focusRequest", "focusRevision", "geometry"})
      for (const auto &invalid :
           {QJsonValue(0), QJsonValue(-1), QJsonValue(1.5), QJsonValue("1"), QJsonValue(9007199254740992.0),
            QJsonValue(QJsonValue::Null)}) {
        auto object = original;
        object[field] = invalid;
        reject([&] { parse(object); });
      }
    for (const auto &field : {"native", "flags", "code", "result", "reason", "scope"}) {
      auto object = original;
      object[field] = 1;
      reject([&] { parse(object); });
    }
    for (const auto &field : {"action", "mode"}) {
      auto object = original;
      object[field] = "unknown";
      reject([&] { parse(object); });
      object.remove(field);
      reject([&] { parse(object); });
    }
    for (const auto action : {KeyboardRouteAction::Acknowledge, KeyboardRouteAction::Withdraw}) {
      auto control = request;
      control.action = action;
      control.mode = KeyboardRouteMode::Original;
      control.result =
          action == KeyboardRouteAction::Acknowledge ? KeyboardRouteResult::Ready : KeyboardRouteResult::None;
      control.reason =
          action == KeyboardRouteAction::Withdraw ? KeyboardRouteReason::Failed : KeyboardRouteReason::None;
      auto object = encodeKeyboardRouteControl(control);
      const auto field = action == KeyboardRouteAction::Acknowledge ? "result" : "reason";
      for (const auto invalid : {"unknown", "none"}) {
        object[field] = invalid;
        reject([&] { parse(object); });
      }
      object.remove(field);
      reject([&] { parse(object); });
    }
    auto wrong = request;
    wrong.action = static_cast<KeyboardRouteAction>(123);
    reject([&] { encodeKeyboardRouteControl(wrong); });
    wrong = request;
    wrong.binding.owner.epoch = 0;
    reject([&] { encodeKeyboardRouteControl(wrong); });
    wrong = request;
    wrong.action = KeyboardRouteAction::Withdraw;
    wrong.reason = KeyboardRouteReason::FocusChanged;
    reject([&] { encodeKeyboardRouteControl(wrong); }); // Withdraw is only Original -> host.
    reject([&] { parseKeyboardRouteControl({type, 0, 2, 1, jsonBytes(original)}); });
    reject([&] { parseKeyboardRouteControl({type, 7, 0, 1, jsonBytes(original)}); });
    reject([&] { parseKeyboardRouteControl({type, 7, 2, 1, std::vector<std::uint8_t>(1025)}); });
    reject([&] { parseKeyboardRouteControl({MessageType::Hello, 7, 2, 1, jsonBytes(original)}); });
    wrong = request;
    wrong.binding.scope = static_cast<ForwardingScope>(123);
    check(!validKeyboardRouteBinding(wrong.binding), "Unknown scope became a valid forwarding binding");
    reject([&] { encodeKeyboardRouteControl(wrong); });
    reject([&] { forwardingRouteMessageType(wrong.binding); });
  }
}
void writeFixture(const QString &path, const QByteArray &bytes)
{
  QFile file(path);
  check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot write certificate fixture");
}
Identity certificateFixture(
    const QString &directory, const QString &identityDirectory, const Identity &identity, const QDateTime &start,
    const QDateTime &end, const QByteArray &usage
)
{
  check(QDir().mkpath(directory), "Cannot create certificate fixture directory");
  const QDir dir(directory);
  writeFixture(dir.filePath("index"), {});
  writeFixture(dir.filePath("serial"), "01\n");
  writeFixture(
      dir.filePath("ca.cnf"),
      "[ca]\ndefault_ca=fixture\n[fixture]\ndatabase=index\nnew_certs_dir=.\nserial=serial\n"
      "default_md=sha256\npolicy=policy\nx509_extensions=usage\n[policy]\ncommonName=supplied\n"
      "[usage]\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n"
      "extendedKeyUsage=" +
          usage + "\n"
  );
  const auto key = QDir(identityDirectory).absoluteFilePath("key.pem");
  auto openssl = [&](const QStringList &arguments) {
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start("openssl", arguments);
    check(
        process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
        "Cannot generate certificate fixture"
    );
  };
  openssl({"req", "-new", "-key", key, "-subj", "/CN=DeskflowWindowShare", "-out", "request.pem"});
  openssl(
      {"ca", "-selfsign", "-batch", "-notext", "-config", "ca.cnf", "-in", "request.pem", "-keyfile", key, "-cert",
       QDir(identityDirectory).absoluteFilePath("cert.pem"), "-startdate", start.toString("yyyyMMddHHmmss'Z'"),
       "-enddate", end.toString("yyyyMMddHHmmss'Z'"), "-out", "certificate.pem"}
  );
  QFile certificateFile(dir.filePath("certificate.pem"));
  check(certificateFile.open(QIODevice::ReadOnly), "Cannot read certificate fixture");
  Identity result{QSslCertificate(certificateFile.readAll()), identity.key};
  check(!result.certificate.isNull(), "Invalid self-signed test certificate");
  return result;
}
void exercise(const Identity &host, const Identity &client, QByteArray hostPin, QByteArray clientPin, bool allowed)
{
  Listener listener;
  QEventLoop loop;
  std::unique_ptr<Channel> server;
  Channel peer(client, hostPin);
  int packets = 0, closed = 0;
  bool accepted = false, timedOut = false;
  QStringList closeReasons;
  listener.incoming = [&](qintptr descriptor) {
    server = std::make_unique<Channel>(host, clientPin);
    server->onClosed = [&](QString reason, CloseCause) {
      closeReasons.append("server: " + reason);
      ++closed;
      loop.quit();
    };
    server->onPacket = [&](Packet packet) {
      ++packets;
      check(
          packet.type == MessageType::Hello && parseObject(packet)["proof"].toInt() == 37,
          "authenticated payload mismatch"
      );
      server->send(MessageType::Accepted, {{"proof", 73}});
    };
    server->accept(descriptor);
  };
  check(listener.listen(QHostAddress::LocalHost, 0), "test listener failed");
  peer.onAuthenticated = [&] { peer.send(MessageType::Hello, {{"proof", 37}}); };
  peer.onPacket = [&](Packet packet) {
    accepted = packet.type == MessageType::Accepted && parseObject(packet)["proof"].toInt() == 73;
    loop.quit();
  };
  peer.onClosed = [&](QString reason, CloseCause) {
    closeReasons.append("client: " + reason);
    ++closed;
    loop.quit();
  };
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
    timedOut = true;
    loop.quit();
  });
  timeout.start(10000);
  peer.connectTo("127.0.0.1", listener.serverPort());
  loop.exec();
  peer.onClosed = {};
  peer.onPacket = {};
  peer.onAuthenticated = {};
  if (server) {
    server->onClosed = {};
    server->onPacket = {};
  }
  check(!timedOut, "TLS test timed out");
  if (allowed) {
    if (!accepted || packets != 1)
      throw std::runtime_error(("approved pair did not communicate; " + closeReasons.join("; ")).toStdString());
  } else
    check(!accepted && packets == 0 && closed > 0, "unapproved peer reached application protocol");
}
void mediaAndControl(const Identity &host, const Identity &client)
{
  Listener listener;
  QEventLoop loop;
  std::unique_ptr<Channel> server;
  Channel peer(client, parseFingerprint(host.fingerprint()));
  const QByteArray accessUnit(1024 * 1024, 'x');
  bool hello = false, queued = false, video = false, focus = false, timedOut = false;
  QStringList closeReasons;
  listener.incoming = [&](qintptr descriptor) {
    server = std::make_unique<Channel>(host, parseFingerprint(client.fingerprint()));
    server->onPacket = [&](Packet packet) {
      check(packet.type == MessageType::Hello, "media setup hello");
      hello = true;
    };
    server->onClosed = [&](QString reason, CloseCause) {
      closeReasons.append("server: " + reason);
      loop.quit();
    };
    server->accept(descriptor);
  };
  check(listener.listen(QHostAddress::LocalHost, 0), "media listener failed");
  peer.onAuthenticated = [&] { peer.send(MessageType::Hello); };
  peer.onPacket = [&](Packet packet) {
    if (packet.type == MessageType::Frame) {
      const auto frame = parseFramePayload(packet);
      video = frame.geometry == 19 && frame.accessUnit == accessUnit && packet.session == 7 && packet.epoch == 2;
    } else if (packet.type == MessageType::Focus) {
      focus = video && parseObject(packet)["focusRevision"].toInt() == 3;
      loop.quit();
    }
  };
  peer.onClosed = [&](QString reason, CloseCause) {
    closeReasons.append("peer: " + reason);
    loop.quit();
  };
  QTimer poll, timeout;
  poll.setInterval(2);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    // The backend can still be draining its final handshake ciphertext after
    // the peer's Hello. Frame admission intentionally counts that queue too.
    if (!hello || !server || !server->readyForFrame())
      return;
    poll.stop();
    queued = server->sendFrame(7, 2, 19, accessUnit) && !server->sendFrame(7, 2, 19, accessUnit) &&
             server->send(MessageType::Focus, {{"focusRevision", 3}}, 7, 2);
    if (!queued) {
      closeReasons.append("media/backlog/control admission failed");
      loop.quit();
    }
  });
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
    timedOut = true;
    loop.quit();
  });
  timeout.start(10000);
  poll.start();
  peer.connectTo("127.0.0.1", listener.serverPort());
  loop.exec();
  peer.onClosed = {};
  peer.onPacket = {};
  peer.onAuthenticated = {};
  if (server) {
    server->onClosed = {};
    server->onPacket = {};
  }
  if (timedOut || !queued || !video || !focus)
    throw std::runtime_error(
        (QString("large frame then control must arrive without disconnect (video=%1, focus=%2, timeout=%3): ")
             .arg(video)
             .arg(focus)
             .arg(timedOut) +
         closeReasons.join("; "))
            .toStdString()
    );
}

void explicitCertificateErrors(const Identity &host, const Identity &client, QSslError::SslError error)
{
  Listener listener;
  QEventLoop loop;
  std::unique_ptr<Channel> server;
  Channel peer(client, parseFingerprint(host.fingerprint()));
  int packets = 0, closed = 0;
  bool sent = false, timedOut = false;
  QString closeReason;
  CloseCause closeCause = CloseCause::Local;
  listener.incoming = [&](qintptr descriptor) {
    server = std::make_unique<Channel>(host, parseFingerprint(client.fingerprint()));
    server->onPacket = [&](Packet) { ++packets; };
    server->accept(descriptor);
  };
  check(listener.listen(QHostAddress::LocalHost, 0), "certificate error listener failed");
  peer.onAuthenticated = [&] {
    // Some backends collapse these conditions into one generic trust error.
    // Inject their distinct Qt representations on a real pinned TLS channel
    // to ensure the generic-error recovery never waives explicit failures.
    ChannelTestAccess::reportCertificateError(peer, error);
    sent = peer.send(MessageType::Hello);
  };
  peer.onClosed = [&](QString reason, CloseCause cause) {
    ++closed;
    closeReason = reason;
    closeCause = cause;
    loop.quit();
  };
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
    timedOut = true;
    loop.quit();
  });
  timeout.start(10000);
  peer.connectTo("127.0.0.1", listener.serverPort());
  loop.exec();
  peer.onAuthenticated = {};
  peer.onClosed = {};
  if (server)
    server->onPacket = {};
  check(
      !timedOut && !sent && packets == 0 && closed == 1 && !peer.authenticated() &&
          closeCause == CloseCause::Authentication &&
          closeReason.contains(QStringLiteral("error %1").arg(static_cast<int>(error))),
      "An explicit certificate validation error must close the pinned channel"
  );
}

void serializationFailure(const Identity &host, const Identity &client, int invalid)
{
  Listener listener;
  QEventLoop loop;
  std::unique_ptr<Channel> server;
  Channel peer(client, parseFingerprint(host.fingerprint()));
  bool rejected = false, threw = false, timedOut = false;
  CloseCause closeCause = CloseCause::Network;
  int closed = 0, packets = 0;
  listener.incoming = [&](qintptr descriptor) {
    server = std::make_unique<Channel>(host, parseFingerprint(client.fingerprint()));
    server->onPacket = [&](Packet) { ++packets; };
    server->accept(descriptor);
  };
  check(listener.listen(QHostAddress::LocalHost, 0), "serialization listener failed");
  peer.onAuthenticated = [&] {
    try {
      rejected = invalid == 0   ? !peer.send(MessageType::Catalog, {{"title", QString(maxControlPayload, 'x')}})
                 : invalid == 1 ? !peer.sendFrame(7, 2, 0, QByteArray("frame"))
                                : !peer.send(static_cast<MessageType>(65535));
    } catch (...) {
      threw = true;
      loop.quit();
    }
  };
  peer.onClosed = [&](QString, CloseCause cause) {
    ++closed;
    closeCause = cause;
    loop.quit();
  };
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
    timedOut = true;
    loop.quit();
  });
  timeout.start(10000);
  peer.connectTo("127.0.0.1", listener.serverPort());
  loop.exec();
  peer.onAuthenticated = {};
  peer.onClosed = {};
  if (server)
    server->onPacket = {};
  check(
      !timedOut && !threw && rejected && closed == 1 && packets == 0 && !peer.authenticated() &&
          closeCause == CloseCause::Local,
      "Serialization validation must close once without throwing or sending malformed bytes"
  );
}

void encryptedBackpressure(const Identity &host, const Identity &client)
{
  Listener listener;
  QEventLoop loop;
  std::unique_ptr<Channel> server;
  // A real TLS peer finishes its handshake but stops consuming application data.
  QSslSocket sink;
  auto configuration = sink.sslConfiguration();
  configuration.setProtocol(QSsl::TlsV1_2OrLater);
  configuration.setLocalCertificate(client.certificate);
  configuration.setPrivateKey(client.key);
  configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
  sink.setSslConfiguration(configuration);
  QObject::connect(&sink, &QSslSocket::sslErrors, &sink, [&](const QList<QSslError> &errors) {
    if (sink.peerCertificate().digest(QCryptographicHash::Sha256) == parseFingerprint(host.fingerprint()))
      sink.ignoreSslErrors(errors);
  });
  bool started = false, blockedMedia = false, timedOut = false;
  qint64 plaintext = 0, encrypted = 0;
  QString closeReason;
  listener.incoming = [&](qintptr descriptor) {
    server = std::make_unique<Channel>(host, parseFingerprint(client.fingerprint()));
    server->onClosed = [&](QString reason, CloseCause) {
      closeReason = reason;
      loop.quit();
    };
    server->accept(descriptor);
    ChannelTestAccess::smallSendBuffer(*server);
  };
  check(listener.listen(QHostAddress::LocalHost, 0), "backpressure listener failed");
  QTimer poll, timeout;
  poll.setInterval(5);
  timeout.setSingleShot(true);
  const QJsonObject control{{"message", QString(maxControlPayload - 64, 'x')}};
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (!server || !server->authenticated())
      return;
    if (!started) {
      // SecureTransport can still have its final handshake ciphertext queued
      // when encrypted() fires. Begin congestion only after TLS output drains.
      if (!server->readyForFrame())
        return;
      sink.setReadBufferSize(1);
      started = server->sendFrame(7, 2, 1, QByteArray(maxPayload - 8, 'x'));
      return;
    }
    plaintext = ChannelTestAccess::plaintext(*server);
    encrypted = ChannelTestAccess::encrypted(*server);
    // Wait until encryption moved the write out of the plaintext queue. The
    // old admission check incorrectly treated this state as an empty socket.
    if (plaintext != 0 || encrypted == 0)
      return;
    if (!blockedMedia) {
      blockedMedia = !server->sendFrame(7, 2, 1, QByteArray("next access unit"));
      if (!blockedMedia) {
        loop.quit();
        return;
      }
    }
    // One control message per encryption cycle grows only the encrypted queue,
    // proving that the control allowance uses it too, rather than just testing
    // several synchronous writes still waiting in the plaintext buffer.
    server->send(MessageType::Error, control);
  });
  QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
    timedOut = true;
    loop.quit();
  });
  poll.start();
  timeout.start(5000);
  sink.connectToHostEncrypted("127.0.0.1", listener.serverPort());
  loop.exec();
  if (server) {
    server->onClosed = {};
  }
  if (timedOut || !started || !blockedMedia || closeReason != "Control peer is not draining its queue")
    throw std::runtime_error(QStringLiteral(
                                 "Encrypted socket backlog must block media and bound the control queue "
                                 "(started=%1, blocked=%2, timeout=%3, plaintext=%4, encrypted=%5, closed=%6)"
    )
                                 .arg(started)
                                 .arg(blockedMedia)
                                 .arg(timedOut)
                                 .arg(plaintext)
                                 .arg(encrypted)
                                 .arg(closeReason)
                                 .toStdString());
}
void closeCauseTests(const Identity &host, const Identity &client)
{
  Channel first(client, parseFingerprint(host.fingerprint()));
  int firstCloses = 0;
  CloseCause firstCause = CloseCause::Network;
  QString firstReason;
  first.onClosed = [&](QString reason, CloseCause cause) {
    ++firstCloses;
    firstCause = cause;
    firstReason = reason;
  };
  first.close("protocol failure", CloseCause::Protocol);
  first.close("later network error", CloseCause::Network);
  check(
      firstCloses == 1 && firstCause == CloseCause::Protocol && firstReason == "protocol failure",
      "A later close replaced the first terminal cause"
  );

  for (int scenario = 0; scenario != 3; ++scenario) {
    Listener listener;
    QEventLoop loop;
    Channel peer(client, parseFingerprint(host.fingerprint()));
    std::unique_ptr<Channel> server;
    bool closed = false;
    CloseCause cause = CloseCause::Local;
    auto record = [&](QString, CloseCause result) {
      cause = result;
      closed = true;
      loop.quit();
    };
    peer.onClosed = [&](QString reason, CloseCause result) {
      if (scenario != 2)
        record(reason, result);
    };
    peer.onAuthenticated = [&] {
      if (scenario == 2)
        ChannelTestAccess::malformedPacket(peer);
    };
    listener.incoming = [&](qintptr descriptor) {
      if (scenario == 1) {
        QTcpSocket socket;
        socket.setSocketDescriptor(descriptor);
        socket.abort();
      } else {
        server = std::make_unique<Channel>(host, parseFingerprint(client.fingerprint()));
        server->onClosed = record;
        server->accept(descriptor);
      }
    };
    check(listener.listen(QHostAddress::LocalHost, 0), "Cannot listen for typed close fixture");
    const auto port = listener.serverPort();
    if (scenario == 0)
      listener.close();
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(5000);
    peer.connectTo("127.0.0.1", port);
    loop.exec();
    peer.onAuthenticated = {};
    peer.onClosed = {};
    if (server)
      server->onClosed = {};
    const auto expected = scenario == 0   ? CloseCause::Network
                          : scenario == 1 ? CloseCause::Authentication
                                          : CloseCause::Protocol;
    check(
        closed && cause == expected,
        "Refused connection, pre-auth EOF, or malformed TLS payload has the wrong close policy"
    );
  }
}
void destructionLifetime(const Identity &host, const Identity &client)
{
  // Destruction is not an onClosed notification. In particular, socket signals
  // emitted during teardown must not enter callbacks or already-destroyed timers.
  for (bool destroyServer : {false, true}) {
    Listener listener;
    QEventLoop loop;
    int serverCloses = 0, peerCloses = 0;
    bool ready = false, timedOut = false;
    std::unique_ptr<Channel> server;
    auto peer = std::make_unique<Channel>(client, parseFingerprint(host.fingerprint()));
    listener.incoming = [&](qintptr descriptor) {
      server = std::make_unique<Channel>(host, parseFingerprint(client.fingerprint()));
      server->onPacket = [&](Packet) { server->send(MessageType::Accepted); };
      server->onClosed = [&](QString, CloseCause) {
        ++serverCloses;
        loop.quit();
      };
      server->accept(descriptor);
    };
    peer->onAuthenticated = [&] { peer->send(MessageType::Hello); };
    peer->onPacket = [&](Packet packet) {
      ready = packet.type == MessageType::Accepted;
      loop.quit();
    };
    peer->onClosed = [&](QString, CloseCause) {
      ++peerCloses;
      loop.quit();
    };
    check(listener.listen(QHostAddress::LocalHost, 0), "Cannot listen for destruction fixture");
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
      timedOut = true;
      loop.quit();
    });
    timeout.start(5000);
    peer->connectTo("127.0.0.1", listener.serverPort());
    loop.exec();
    check(ready && !timedOut && !serverCloses && !peerCloses, "Destruction fixture did not establish");
    const auto &destroyedCloses = destroyServer ? serverCloses : peerCloses;
    const auto &survivingCloses = destroyServer ? peerCloses : serverCloses;
    (destroyServer ? server : peer).reset();
    check(destroyedCloses == 0, "Destruction invoked its own terminal callback");
    if (!survivingCloses) {
      timeout.start(5000);
      loop.exec();
    }
    check(!timedOut && survivingCloses == 1, "Destroying a live channel did not notify its remote peer once");
    (destroyServer ? peer : server).reset();
    check(destroyedCloses == 0 && survivingCloses == 1, "Teardown repeated a terminal callback");
  }
  // Also tear down before any asynchronous connection/handshake callback runs.
  QTcpServer listener;
  check(listener.listen(QHostAddress::LocalHost, 0), "Cannot listen for pending destruction fixture");
  int callbacks = 0;
  auto peer = std::make_unique<Channel>(client, parseFingerprint(host.fingerprint()));
  peer->onAuthenticated = [&] { ++callbacks; };
  peer->onPacket = [&](Packet) { ++callbacks; };
  peer->onClosed = [&](QString, CloseCause) { ++callbacks; };
  peer->connectTo("127.0.0.1", listener.serverPort());
  peer.reset();
  QCoreApplication::processEvents();
  check(callbacks == 0, "A pending connection called its destroyed owner");
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  try {
    keyboardRouteProtocol();
    const auto arguments = application.arguments();
    if (arguments.size() > 1) {
      check(
          arguments.size() == 2 && arguments[1].startsWith("--tls-backend="),
          "Usage: windowshare-transport-tests [--tls-backend=<name>]"
      );
      check(QSslSocket::setActiveBackend(arguments[1].mid(14)), "Requested TLS backend is unavailable");
    }
    QTextStream(stdout) << "TLS backend: " << QSslSocket::activeBackend() << "; Qt " << qVersion() << '\n';
    QTemporaryDir directory;
    check(directory.isValid(), "temporary directory unavailable");
    const auto a = QDir(directory.path()).filePath("host"), b = QDir(directory.path()).filePath("client");
    initializeIdentity(a);
    initializeIdentity(b);
    const auto host = Identity::load(a), client = Identity::load(b);
    const auto hostPin = parseFingerprint(host.fingerprint()), clientPin = parseFingerprint(client.fingerprint());
    retainedEvidence(host, client);
    lateReadEvidence(host, client, false);
    lateReadEvidence(host, client, true);
    repeatedReadNotifications(host, client);
    packetBurstYield(host, client, false, false);
    packetBurstYield(host, client, true, false);
    packetBurstYield(host, client, false, true);
    terminalEvidence(host, client);
    destructiveEvidenceCallbacks(host, client);
    handshakeEvidence(host, client);
    destructionLifetime(host, client);
    exercise(host, client, hostPin, clientPin, true);
    const auto now = QDateTime::currentDateTimeUtc();
    const auto expired = certificateFixture(
        QDir(directory.path()).filePath("expired"), a, host, now.addDays(-2), now.addDays(-1), "serverAuth,clientAuth"
    );
    const auto future = certificateFixture(
        QDir(directory.path()).filePath("future"), a, host, now.addDays(1), now.addDays(2), "serverAuth,clientAuth"
    );
    const auto wrongPurpose = certificateFixture(
        QDir(directory.path()).filePath("wrong-purpose"), a, host, now.addDays(-1), now.addDays(1), "codeSigning"
    );
    const auto serverOnly = certificateFixture(
        QDir(directory.path()).filePath("server-only"), a, host, now.addDays(-1), now.addDays(1), "serverAuth"
    );
    const auto clientOnly = certificateFixture(
        QDir(directory.path()).filePath("client-only"), b, client, now.addDays(-1), now.addDays(1), "clientAuth"
    );
    const auto serverOnlyPin = parseFingerprint(serverOnly.fingerprint());
    const auto clientOnlyPin = parseFingerprint(clientOnly.fingerprint());
    exercise(serverOnly, clientOnly, serverOnlyPin, clientOnlyPin, true);
    exercise(clientOnly, client, clientOnlyPin, clientPin, false);
    exercise(host, serverOnly, hostPin, serverOnlyPin, false);
    for (const auto &invalid : {expired, future, wrongPurpose}) {
      const auto pin = parseFingerprint(invalid.fingerprint());
      exercise(invalid, client, pin, clientPin, false);
      exercise(host, invalid, hostPin, pin, false);
    }
    for (auto error :
         {QSslError::CertificateExpired, QSslError::CertificateNotYetValid, QSslError::InvalidPurpose,
          QSslError::CertificateRevoked, QSslError::CertificateSignatureFailed,
          QSslError::UnableToVerifyFirstCertificate})
      explicitCertificateErrors(host, client, error);
    mediaAndControl(host, client);
    closeCauseTests(host, client);
    for (int invalid = 0; invalid < 3; ++invalid)
      serializationFailure(host, client, invalid);
    encryptedBackpressure(host, client);
    auto bad = hostPin;
    bad[0] = static_cast<char>(bad[0] ^ 1);
    exercise(host, client, bad, clientPin, false);
    bad = clientPin;
    bad[0] = static_cast<char>(bad[0] ^ 1);
    exercise(host, client, hostPin, bad, false);
    bool rejected = false;
    try {
      parseFingerprint("not-a-fingerprint");
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected, "malformed pin accepted");
    rejected = false;
    try {
      initializeIdentity(a);
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected, "existing identity overwritten");
    const auto media = encodeFramePayload(0x0102030405ULL, QByteArray("\0\0\0\1", 4));
    Packet packet{MessageType::Frame, 2, 1, 1, {media.begin(), media.end()}};
    check(parseFramePayload(packet).geometry == 0x0102030405ULL, "frame revision survives endian conversion");
    rejected = false;
    try {
      encodeFramePayload(0, QByteArray("frame"));
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected, "zero media revision rejected");
    packet.payload.resize(8);
    rejected = false;
    try {
      parseFramePayload(packet);
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected, "empty media payload rejected");
    QTextStream(stdout) << "PASS: mutual TLS, wrong pins, invalid certificate dates/purpose, explicit TLS errors, "
                           "identity guards, silent transport teardown, media revisions, serialization rejection, "
                           "encrypted backpressure, bounded media and control reserve\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
