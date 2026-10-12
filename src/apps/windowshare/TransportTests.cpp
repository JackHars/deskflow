/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
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
  bool video = false, focus = false, timedOut = false;
  listener.incoming = [&](qintptr descriptor) {
    server = std::make_unique<Channel>(host, parseFingerprint(client.fingerprint()));
    server->onPacket = [&](Packet packet) {
      check(packet.type == MessageType::Hello, "media setup hello");
      check(server->sendFrame(7, 2, 19, accessUnit), "first bounded frame accepted");
      check(!server->sendFrame(7, 2, 19, accessUnit), "video backlog not queued");
      check(server->send(MessageType::Focus, {{"focusRevision", 3}}, 7, 2), "control survives large queued IDR");
    };
    server->onClosed = [&](QString, CloseCause) { loop.quit(); };
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
  peer.onClosed = [&](QString, CloseCause) { loop.quit(); };
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
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  try {
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
                           "identity guards, media revisions, serialization rejection, "
                           "encrypted backpressure, bounded media and control reserve\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
