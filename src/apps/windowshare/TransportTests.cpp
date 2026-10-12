/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "Transport.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QHostAddress>
#include <QSslConfiguration>
#include <QSslError>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTextStream>
#include <memory>
using namespace deskflow::windowshare;
namespace deskflow::windowshare {
struct ChannelTestAccess
{
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
void exercise(const Identity &host, const Identity &client, QByteArray hostPin, QByteArray clientPin, bool allowed)
{
  Listener listener;
  QEventLoop loop;
  std::unique_ptr<Channel> server;
  Channel peer(client, hostPin);
  int packets = 0, closed = 0;
  bool accepted = false, timedOut = false;
  listener.incoming = [&](qintptr descriptor) {
    server = std::make_unique<Channel>(host, clientPin);
    server->onClosed = [&](QString) {
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
  peer.onClosed = [&](QString) {
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
  if (allowed)
    check(accepted && packets == 1, "approved pair did not communicate");
  else
    check(!accepted && packets == 0 && closed > 0, "unapproved peer reached application protocol");
}
void mediaAndControl(const Identity &host, const Identity &client)
{
  Listener listener;
  QEventLoop loop;
  std::unique_ptr<Channel> server;
  Channel peer(client, parseFingerprint(host.fingerprint()));
  const QByteArray accessUnit(1024 * 1024, 'x');
  bool video = false, focus = false, timedOut = false;
  listener.incoming = [&](qintptr descriptor) {
    server = std::make_unique<Channel>(host, parseFingerprint(client.fingerprint()));
    server->onPacket = [&](Packet packet) {
      check(packet.type == MessageType::Hello, "media setup hello");
      check(server->sendFrame(7, 2, 19, accessUnit), "first bounded frame accepted");
      check(!server->sendFrame(7, 2, 19, accessUnit), "video backlog not queued");
      check(server->send(MessageType::Focus, {{"focusRevision", 3}}, 7, 2), "control survives large queued IDR");
    };
    server->onClosed = [&](QString) { loop.quit(); };
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
  peer.onClosed = [&](QString) { loop.quit(); };
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
  check(!timedOut && video && focus, "large frame then control must arrive without disconnect");
}

void serializationFailure(const Identity &host, const Identity &client, int invalid)
{
  Listener listener;
  QEventLoop loop;
  std::unique_ptr<Channel> server;
  Channel peer(client, parseFingerprint(host.fingerprint()));
  bool rejected = false, threw = false, timedOut = false;
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
  peer.onClosed = [&](QString) {
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
  peer.onAuthenticated = {};
  peer.onClosed = {};
  if (server)
    server->onPacket = {};
  check(
      !timedOut && !threw && rejected && closed == 1 && packets == 0 && !peer.authenticated(),
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
  sink.setReadBufferSize(1);
  QObject::connect(&sink, &QSslSocket::sslErrors, &sink, [&](const QList<QSslError> &errors) {
    if (sink.peerCertificate().digest(QCryptographicHash::Sha256) == parseFingerprint(host.fingerprint()))
      sink.ignoreSslErrors(errors);
  });
  bool started = false, blockedMedia = false, timedOut = false;
  QString closeReason;
  listener.incoming = [&](qintptr descriptor) {
    server = std::make_unique<Channel>(host, parseFingerprint(client.fingerprint()));
    server->onAuthenticated = [&] { started = server->sendFrame(7, 2, 1, QByteArray(maxPayload - 8, 'x')); };
    server->onClosed = [&](QString reason) {
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
    if (!started || !server || !server->authenticated())
      return;
    // Wait until encryption moved the write out of the plaintext queue. The
    // old admission check incorrectly treated this state as an empty socket.
    if (ChannelTestAccess::plaintext(*server) != 0 || ChannelTestAccess::encrypted(*server) == 0)
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
    server->onAuthenticated = {};
    server->onClosed = {};
  }
  check(
      !timedOut && started && blockedMedia && closeReason == "Control peer is not draining its queue",
      "Encrypted socket backlog must block media and bound the control queue"
  );
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  try {
    QTemporaryDir directory;
    check(directory.isValid(), "temporary directory unavailable");
    const auto a = QDir(directory.path()).filePath("host"), b = QDir(directory.path()).filePath("client");
    initializeIdentity(a);
    initializeIdentity(b);
    const auto host = Identity::load(a), client = Identity::load(b);
    const auto hostPin = parseFingerprint(host.fingerprint()), clientPin = parseFingerprint(client.fingerprint());
    exercise(host, client, hostPin, clientPin, true);
    mediaAndControl(host, client);
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
    QTextStream(stdout) << "PASS: mutual TLS, wrong pins, identity guards, media revisions, serialization rejection, "
                           "encrypted backpressure, bounded media and control reserve\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
