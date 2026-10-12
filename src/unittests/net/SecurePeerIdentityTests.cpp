/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "SecurePeerIdentityTests.h"

#include "../deskflow/MockEventQueue.h"
#include "base/Event.h"
#include "common/Settings.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include "io/StreamFilter.h"
#include "net/FingerprintDatabase.h"
#include "net/ISocketMultiplexerJob.h"
#include "net/NetworkAddress.h"
#include "net/SecureListenSocket.h"
#include "net/SecureSocket.h"
#include "net/SecureUtils.h"
#include "net/SocketMultiplexer.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <atomic>
#include <functional>
#include <thread>

namespace {
class IdentityEvents final : public MockEventQueue
{
public:
  void addEvent(Event &&event) override
  {
    if (event.getType() == EventTypes::ListenSocketConnecting)
      connecting.store(true);
    if (event.getType() == EventTypes::SocketDisconnected ||
        event.getType() == EventTypes::ClientListenerDisconnectedOnAccept)
      disconnected.store(true);
    Event::deleteData(event);
  }
  std::atomic_bool connecting = false;
  std::atomic_bool disconnected = false;
};

bool waitUntil(const std::function<bool()> &ready)
{
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QTest::qSleep(1);
  }
  return ready();
}

QByteArray readFile(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error("Cannot read temporary certificate");
  return file.readAll();
}

class PlainStream final : public deskflow::IStream
{
public:
  void close() override
  {
  }
  uint32_t read(void *, uint32_t) override
  {
    return 0;
  }
  void write(const void *, uint32_t) override
  {
  }
  void flush() override
  {
  }
  void shutdownInput() override
  {
  }
  void shutdownOutput() override
  {
  }
  void *getEventTarget() const override
  {
    return const_cast<PlainStream *>(this);
  }
  bool isReady() const override
  {
    return false;
  }
  uint32_t getSize() const override
  {
    return 0;
  }
};

class InterleavedReadJob final : public ISocketMultiplexerJob
{
public:
  InterleavedReadJob(ArchSocket socket, std::function<void()> run, std::atomic_bool &destroyed)
      : m_socket(ARCH->copySocket(socket)),
        m_run(std::move(run)),
        m_destroyed(destroyed)
  {
  }
  ~InterleavedReadJob() override
  {
    ARCH->closeSocket(m_socket);
    m_destroyed = true;
  }
  ISocketMultiplexerJob *run(bool, bool, bool) override
  {
    m_run();
    return nullptr;
  }
  ArchSocket getSocket() const override
  {
    return m_socket;
  }
  bool isReadable() const override
  {
    return false;
  }
  bool isWritable() const override
  {
    return true;
  }

private:
  ArchSocket m_socket;
  std::function<void()> m_run;
  std::atomic_bool &m_destroyed;
};
} // namespace

void SecurePeerIdentityTests::initTestCase()
{
  m_arch.init();
  QVERIFY(m_directory.isValid());
  Settings::setSettingsFile(m_directory.filePath("identity.conf"));
  Settings::setStateFile(m_directory.filePath("identity.state"));
  QVERIFY(QDir().mkpath(Settings::tlsDir()));
  QVERIFY(Settings::tlsDir().startsWith(m_directory.path() + '/'));
  m_hostCertificate = m_directory.filePath("host.pem");
  m_peerCertificate = m_directory.filePath("peer.pem");
  deskflow::generatePemSelfSignedCert(m_hostCertificate, 2048);
  deskflow::generatePemSelfSignedCert(m_peerCertificate, 2048);
  Settings::setValue(Settings::Security::Certificate, m_hostCertificate);
  // Receive core notifications in memory only; never expose the ordinary IPC
  // endpoint or touch the user's existing certificate/trust configuration.
  new deskflow::core::ipc::CoreIpcServer(this);
}

void SecurePeerIdentityTests::authenticatedIdentity_data()
{
  QTest::addColumn<bool>("peerAuth");
  QTest::addColumn<bool>("trusted");
  QTest::addColumn<bool>("certificate");
  QTest::newRow("approved-client") << true << true << true;
  QTest::newRow("unapproved-client") << true << false << true;
  QTest::newRow("missing-client-certificate") << true << true << false;
  QTest::newRow("encryption-without-client-authentication") << false << true << true;
}

void SecurePeerIdentityTests::authenticatedIdentity()
{
  QFETCH(bool, peerAuth);
  QFETCH(bool, trusted);
  QFETCH(bool, certificate);
  const auto peerPem = readFile(m_peerCertificate);
  const QSslCertificate peerCertificate(peerPem);
  const auto digest = peerCertificate.digest(QCryptographicHash::Sha256);
  QCOMPARE(digest.size(), 32);
  auto approved = digest;
  if (!trusted)
    approved[0] = static_cast<char>(approved[0] ^ 1);
  FingerprintDatabase database;
  database.addTrusted({QCryptographicHash::Sha256, approved});
  QVERIFY(database.write(Settings::tlsTrustedClientsDb()));

  IdentityEvents events;
  SocketMultiplexer multiplexer;
  SecureListenSocket listener(
      &events, &multiplexer, IArchNetwork::AddressFamily::INet,
      peerAuth ? SecurityLevel::PeerAuth : SecurityLevel::Encrypted
  );
  QTcpServer reservation;
  QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
  const auto port = reservation.serverPort();
  reservation.close();
  NetworkAddress address("127.0.0.1", port);
  address.resolve();
  listener.bind(address);

  QSslSocket peer;
  auto configuration = peer.sslConfiguration();
  configuration.setProtocol(QSsl::TlsV1_2OrLater);
  configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
  if (certificate) {
    configuration.setLocalCertificate(peerCertificate);
    configuration.setPrivateKey(QSslKey(peerPem, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey));
  }
  peer.setSslConfiguration(configuration);
  QObject::connect(&peer, &QSslSocket::sslErrors, &peer, [&](const QList<QSslError> &errors) {
    // The fixture server is explicitly known. Authentication under test is the
    // real core server's trusted-client check, never this fixture's CA policy.
    peer.ignoreSslErrors(errors);
  });
  peer.connectToHost("127.0.0.1", port);
  QVERIFY(waitUntil([&] { return events.connecting.load(); }));
  auto accepted = listener.accept();
  auto *server = dynamic_cast<SecureSocket *>(accepted.get());
  QVERIFY(server);
  QVERIFY(!server->authenticatedPeerFingerprint());
  QVERIFY(waitUntil([&] { return peer.state() == QAbstractSocket::ConnectedState; }));
  peer.startClientEncryption();
  const bool expected = peerAuth && trusted && certificate;
  const bool handshakeAccepted = !peerAuth || (trusted && certificate);
  QVERIFY(waitUntil([&] { return events.disconnected.load() || (peer.isEncrypted() && server->isSecureReady()); }));
  if (handshakeAccepted)
    QVERIFY(!events.disconnected.load());
  else
    QVERIFY(events.disconnected.load());

  StreamFilter first(&events, server, false);
  StreamFilter second(&events, &first, false);
  const auto identity = second.authenticatedPeerFingerprint();
  QCOMPARE(identity.has_value(), expected);
  if (identity) {
    const QByteArray actual(reinterpret_cast<const char *>(identity->data()), identity->size());
    QCOMPARE(actual, digest);
  }

  // Identity disappears on transport failure, before a caller could reuse this
  // same proxy or a filter to authorize input for a replacement connection.
  peer.abort();
  QVERIFY(waitUntil([&] { return events.disconnected.load(); }));
  QVERIFY(!second.authenticatedPeerFingerprint());
  server->close();
  QVERIFY(!second.authenticatedPeerFingerprint());
  listener.close();
}

void SecurePeerIdentityTests::activeClose()
{
  // Keep the regression bounded even when the old lock order deadlocks. The
  // child establishes real TLS, then arranges the two conflicting operations.
  if (!qEnvironmentVariableIsSet("DESKFLOW_TEST_ACTIVE_CLOSE_CHILD")) {
    QProcess child;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("DESKFLOW_TEST_ACTIVE_CLOSE_CHILD", "1");
    child.setProcessEnvironment(environment);
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QCoreApplication::applicationFilePath(), {"activeClose"});
    QVERIFY(child.waitForStarted());
    const bool finished = child.waitForFinished(15000);
    if (!finished) {
      child.kill();
      child.waitForFinished();
    }
    const auto output = child.readAll();
    QVERIFY2(finished, qPrintable("Active TLS close did not finish:\n" + QString::fromUtf8(output)));
    QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, output.constData());
    return;
  }

  const auto peerPem = readFile(m_peerCertificate);
  const QSslCertificate peerCertificate(peerPem);
  FingerprintDatabase database;
  database.addTrusted({QCryptographicHash::Sha256, peerCertificate.digest(QCryptographicHash::Sha256)});
  QVERIFY(database.write(Settings::tlsTrustedClientsDb()));
  IdentityEvents events;
  SocketMultiplexer multiplexer;
  SecureListenSocket listener(&events, &multiplexer, IArchNetwork::AddressFamily::INet, SecurityLevel::PeerAuth);
  QTcpServer reservation;
  QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
  const auto port = reservation.serverPort();
  reservation.close();
  NetworkAddress address("127.0.0.1", port);
  address.resolve();
  listener.bind(address);

  QSslSocket peer;
  auto configuration = peer.sslConfiguration();
  configuration.setProtocol(QSsl::TlsV1_2OrLater);
  configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
  configuration.setLocalCertificate(peerCertificate);
  configuration.setPrivateKey(QSslKey(peerPem, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey));
  peer.setSslConfiguration(configuration);
  QObject::connect(&peer, &QSslSocket::sslErrors, &peer, [&](const QList<QSslError> &errors) {
    peer.ignoreSslErrors(errors);
  });
  peer.connectToHost("127.0.0.1", port);
  QVERIFY(waitUntil([&] { return events.connecting.load() && peer.state() == QAbstractSocket::ConnectedState; }));
  auto accepted = listener.accept();
  auto *server = dynamic_cast<SecureSocket *>(accepted.get());
  QVERIFY(server);
  peer.startClientEncryption();
  QVERIFY(waitUntil([&] { return peer.isEncrypted() && server->isSecureReady(); }));
  QVERIFY(server->authenticatedPeerFingerprint());

  // Ordinary traffic must still flow in both directions before the close.
  const QByteArray payload("active TLS close");
  QCOMPARE(peer.write(payload), payload.size());
  QVERIFY(waitUntil([&] { return server->getSize() == payload.size(); }));
  QByteArray received(payload.size(), Qt::Uninitialized);
  QCOMPARE(server->read(received.data(), received.size()), payload.size());
  QCOMPARE(received, payload);
  server->write(payload.constData(), payload.size());
  QVERIFY(waitUntil([&] { return peer.bytesAvailable() == payload.size(); }));
  QCOMPARE(peer.readAll(), payload);

  std::atomic_bool entered = false, readFinished = false, destroyed = false, closeFinished = false;
  std::atomic_bool delayedRan = false, delayedDestroyed = false;
  // Prepare a job before shutdown and submit it afterwards, as can happen when
  // write() has constructed newJob() but close wins the scheduling race.
  auto delayedJob =
      std::make_unique<InterleavedReadJob>(server->getSocket(), [&] { delayedRan = true; }, delayedDestroyed);
  server->setJob(new InterleavedReadJob(
      server->getSocket(),
      [&] {
        entered = true;
        // close sets fatal before waiting to remove this currently executing job.
        while (!server->isFatal())
          std::this_thread::yield();
        char byte;
        int bytesRead = 0;
        readFinished = server->secureRead(&byte, 1, bytesRead) == -1;
      },
      destroyed
  ));
  QVERIFY(waitUntil([&] { return entered.load(); }));
  std::thread closer([&] {
    server->close();
    closeFinished = true;
  });
  if (!waitUntil([&] { return closeFinished.load(); }))
    qFatal("Closing an active TLS callback deadlocked");
  closer.join();
  QVERIFY(readFinished);
  QVERIFY(destroyed);
  QVERIFY(!server->authenticatedPeerFingerprint());
  QVERIFY(!server->isSecureReady());
  QVERIFY(!server->newJob());
  server->setJob(delayedJob.release());
  QVERIFY(delayedDestroyed);
  QVERIFY(!delayedRan);
  // Stale connect handlers must not recapture a closed socket or restart TLS.
  server->secureConnect();
  server->secureAccept();
  server->close();
  listener.close();
}

void SecurePeerIdentityTests::plainStreamHasNoIdentity()
{
  IdentityEvents events;
  PlainStream plain;
  StreamFilter filter(&events, &plain, false);
  QVERIFY(!plain.authenticatedPeerFingerprint());
  QVERIFY(!filter.authenticatedPeerFingerprint());
}

QTEST_GUILESS_MAIN(SecurePeerIdentityTests)
