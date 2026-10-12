/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
// Keep the controller private to its executable while testing its actual
// callback wiring. Its native implementation below never touches a display.
#define WINDOWSHARE_BRIDGE_TEST
#include "main.cpp"
#include <QCryptographicHash>
#include <QEventLoop>
#include <QSslConfiguration>
#include <QSslError>
#include <QTemporaryDir>

namespace {
QVector<std::uint32_t> exportedWindows;
QVector<std::pair<std::uint64_t, std::uint64_t>> keyframeRequests;
std::function<void(NativeHost &, std::uint64_t, std::uint64_t)> keyframeReply;
} // namespace

namespace deskflow::windowshare {
struct NativeHost::Impl
{
};
NativeHost::NativeHost(QString, QObject *parent) : QObject(parent)
{
}
NativeHost::~NativeHost() = default;
QJsonObject NativeHost::permissions()
{
  return {};
}
void NativeHost::requestPermissions()
{
}
bool NativeHost::recover()
{
  return true;
}
void NativeHost::setHoldingDisplayEnabled(bool)
{
}
// Enumeration completes only when the test invokes onCatalog.
void NativeHost::refresh()
{
}
void NativeHost::exportWindow(std::uint32_t id)
{
  exportedWindows.push_back(id);
}
void NativeHost::ready(std::uint64_t, std::uint64_t)
{
}
void NativeHost::input(std::uint64_t, std::uint64_t, const QJsonObject &)
{
}
void NativeHost::resizeWindow(std::uint64_t, std::uint64_t, int, int, std::uint64_t)
{
}
void NativeHost::requestKeyframe(std::uint64_t session, std::uint64_t epoch)
{
  keyframeRequests.push_back({session, epoch});
  if (keyframeReply)
    keyframeReply(*this, session, epoch);
}
void NativeHost::restore(std::uint64_t)
{
}
void NativeHost::restoreAll()
{
}
void NativeHost::releaseAll()
{
}
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
  static QVariant sendBuffer(Channel &channel)
  {
    return channel.m_socket.socketOption(QAbstractSocket::SendBufferSizeSocketOption);
  }
  static void smallSendBuffer(Channel &channel)
  {
    channel.m_socket.setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4096);
  }
  static void resumeOutput(Channel &channel, const QVariant &sendBuffer)
  {
    channel.m_socket.setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, sendBuffer);
    channel.m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
  }
};
} // namespace deskflow::windowshare

namespace {
struct BridgeTestAccess
{
  static void receive(Bridge &bridge, const Packet &packet)
  {
    bridge.receiveViewer(packet);
  }
  static Viewer *viewer(Bridge &bridge, std::uint64_t session)
  {
    const auto found = bridge.m_viewers.find(session);
    return found == bridge.m_viewers.end() ? nullptr : found->second.window.data();
  }
  static std::size_t count(const Bridge &bridge)
  {
    return bridge.m_viewers.size();
  }
  static bool established(const Bridge &bridge)
  {
    return bridge.m_established;
  }
  static bool catalogReady(const Bridge &bridge)
  {
    return bridge.m_catalogReady;
  }
  static std::uint32_t initialWindow(const Bridge &bridge)
  {
    return bridge.m_initialWindow;
  }
  static quint16 port(const Bridge &bridge)
  {
    return bridge.m_listener.serverPort();
  }
  static void catalog(Bridge &bridge, const QVector<Candidate> &windows)
  {
    bridge.m_native->onCatalog(windows);
  }
  static Channel *channel(Bridge &bridge)
  {
    return bridge.m_channel.data();
  }
  static void opened(Bridge &bridge, std::uint64_t session, std::uint64_t epoch)
  {
    bridge.m_native->onOpened(
        session, epoch,
        {{"title", "Frame fixture"}, {"width", 640}, {"height", 480}, {"geometry", 1}, {"codec", "h264"}}
    );
  }
  static void frame(Bridge &bridge, std::uint64_t session, std::uint64_t epoch, const QByteArray &bytes)
  {
    bridge.m_native->onFrame(session, epoch, 1, bytes);
  }
  static void closed(Bridge &bridge, std::uint64_t session)
  {
    bridge.m_native->onClosed(session);
  }
  static std::size_t pendingKeyframes(const Bridge &bridge)
  {
    return bridge.m_pendingKeyframes.size();
  }
  static bool retryActive(const Bridge &bridge)
  {
    return bridge.m_keyframeRetry.isActive();
  }
  static void pauseRetry(Bridge &bridge)
  {
    bridge.m_keyframeRetry.stop();
  }
  static void resumeRetry(Bridge &bridge)
  {
    bridge.m_keyframeRetry.start();
  }
};

void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

bool waitUntil(const std::function<bool()> &condition)
{
  if (condition())
    return true;
  QEventLoop loop;
  QTimer poll, timeout;
  poll.setInterval(5);
  timeout.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (condition())
      loop.quit();
  });
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start();
  timeout.start(5000);
  loop.exec();
  return condition();
}

void boundedPresentation()
{
  QVector<Candidate> windows;
  const QString longTitle = QString(3000, QChar(0x0001)) + QString(3000, QChar(0x4e16));
  for (std::uint32_t id = 1; id <= 5000; ++id)
    windows.push_back({id, longTitle});
  const auto catalog = catalogForWire(windows);
  const auto payload = jsonBytes({{"windows", catalog}});
  check(!catalog.isEmpty() && catalog.size() <= maxCatalogWindows, "Catalog count was not bounded");
  check(payload.size() < maxControlPayload, "Escaped catalog exceeded its serialized wire budget");
  for (const auto &value : catalog) {
    const auto object = value.toObject();
    check(object["title"].toString().size() <= 512, "Catalog title was not bounded");
    check(object["id"].toInteger() > 0, "Catalog lost window identity");
  }
  check(
      windows.front().title == longTitle && windows.size() == 5000,
      "Wire bounds changed native identity strings or candidates"
  );
  windows.clear();
  for (std::uint32_t id = 1; id <= 1000; ++id)
    windows.push_back({id, "short"});
  check(catalogForWire(windows).size() == maxCatalogWindows, "Short-title catalog ignored its count limit");

  const QJsonObject source{{"title", longTitle}, {"geometry", 3}, {"width", 640}, {"height", 480}};
  const auto wire = windowMetadataForWire(source);
  check(
      wire["title"].toString().size() == 512 && wire["geometry"] == 3,
      "Wire metadata did not retain bounded title and geometry"
  );
  check(
      jsonBytes(wire).size() < maxControlPayload && source["title"].toString() == longTitle,
      "Metadata wire bounds changed source identity or exceeded the budget"
  );
}

void delayedViewerCleanup()
{
  Bridge bridge(false, Identity{QSslCertificate{}, QSslKey{}}, QByteArray(32, '\0'), QString{}, 0, 0, false);
  const QJsonObject metadata{
      {"title", "Bridge lifecycle fixture"}, {"geometry", 1}, {"width", 640}, {"height", 480}, {"codec", "h264"}
  };
  const Packet opened{MessageType::Opened, 91, 2, 1, jsonBytes(metadata)};
  BridgeTestAccess::receive(bridge, opened);
  QPointer<Viewer> old = BridgeTestAccess::viewer(bridge, 91);
  check(old, "Opened did not register a viewer");
  const auto delayedReturn = old->onReturned;

  // A peer can reuse an incarnation before Qt processes DeleteOnClose. Both
  // cleanup callbacks must identify the original object, not just the numbers.
  BridgeTestAccess::receive(bridge, Packet{MessageType::Closed, 91, 2, 2, jsonBytes({})});
  BridgeTestAccess::receive(bridge, opened);
  QPointer<Viewer> replacement = BridgeTestAccess::viewer(bridge, 91);
  check(replacement && replacement != old, "Replacement viewer was not distinct");
  delayedReturn();
  check(BridgeTestAccess::viewer(bridge, 91) == replacement, "Delayed return erased the replacement viewer");
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  check(
      !old && replacement && BridgeTestAccess::viewer(bridge, 91) == replacement,
      "Delayed destruction erased the replacement viewer"
  );
  BridgeTestAccess::receive(bridge, Packet{MessageType::Closed, 91, 2, 3, jsonBytes({})});
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  check(!replacement && BridgeTestAccess::count(bridge) == 0, "Replacement viewer was not cleaned up");
}

void initialExportWaitsForCatalog(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  // Both command-line initial exports must wait until asynchronous enumeration
  // completes. A real empty result still completes enumeration and is sent.
  exportedWindows.clear();
  Bridge host(true, hostIdentity, parseFingerprint(viewerIdentity.fingerprint()), QString{}, 0, 123, false);
  Bridge viewer(false, viewerIdentity, parseFingerprint(hostIdentity.fingerprint()), QString{}, 0, 456, false);
  check(host.listen(QHostAddress::LocalHost, 0), "Cannot listen for Bridge fixture");
  viewer.connectTo("127.0.0.1", BridgeTestAccess::port(host));
  check(
      waitUntil([&] { return BridgeTestAccess::established(host) && BridgeTestAccess::established(viewer); }),
      "Bridge peers did not negotiate"
  );
  check(
      exportedWindows.isEmpty() && BridgeTestAccess::initialWindow(host) == 123 &&
          BridgeTestAccess::initialWindow(viewer) == 456,
      "Initial export was consumed before native enumeration completed"
  );
  check(
      !BridgeTestAccess::catalogReady(host) && !BridgeTestAccess::catalogReady(viewer),
      "An uninitialized empty catalog was published as a completed catalog"
  );

  BridgeTestAccess::catalog(host, {});
  check(
      waitUntil([&] { return exportedWindows.size() == 2; }), "Completed empty catalog did not release initial exports"
  );
  check(
      exportedWindows.contains(123) && exportedWindows.contains(456) && BridgeTestAccess::catalogReady(host) &&
          BridgeTestAccess::catalogReady(viewer) && BridgeTestAccess::initialWindow(host) == 0 &&
          BridgeTestAccess::initialWindow(viewer) == 0,
      "Completed empty catalog did not export the requested IDs exactly once"
  );
  BridgeTestAccess::catalog(host, {});
  QCoreApplication::processEvents();
  check(exportedWindows.size() == 2, "Catalog refresh repeated an initial export");
}

void congestionRecovery(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  keyframeRequests.clear();
  keyframeReply = {};
  Bridge host(true, hostIdentity, parseFingerprint(viewerIdentity.fingerprint()), QString{}, 0, 0, false);
  QSslSocket sink;
  auto configuration = sink.sslConfiguration();
  configuration.setProtocol(QSsl::TlsV1_2OrLater);
  configuration.setLocalCertificate(viewerIdentity.certificate);
  configuration.setPrivateKey(viewerIdentity.key);
  configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
  sink.setSslConfiguration(configuration);
  QObject::connect(&sink, &QSslSocket::sslErrors, &sink, [&](const QList<QSslError> &errors) {
    if (sink.peerCertificate().digest(QCryptographicHash::Sha256) == parseFingerprint(hostIdentity.fingerprint()))
      sink.ignoreSslErrors(errors);
  });
  std::uint64_t sequence = 0;
  auto write = [&](MessageType type, const QJsonObject &object) {
    const auto bytes = encode(Packet{type, 0, 0, ++sequence, jsonBytes(object)});
    sink.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size()));
  };
  QObject::connect(&sink, &QSslSocket::encrypted, &sink, [&] {
    write(MessageType::Hello, {{"version", static_cast<int>(protocolVersion)}, {"role", "viewer"}, {"codec", "h264"}});
  });
  // Real Channels originate idle heartbeats independently. A raw TLS fixture
  // must do the same while incoming media prevents host Ping/Pong from draining.
  QTimer heartbeat;
  heartbeat.setInterval(1000);
  QObject::connect(&heartbeat, &QTimer::timeout, &sink, [&] {
    if (sink.isEncrypted() && sink.state() == QAbstractSocket::ConnectedState && sink.bytesToWrite() == 0 &&
        sink.encryptedBytesToWrite() == 0)
      write(MessageType::Ping, {});
  });
  heartbeat.start();
  Decoder decoder;
  bool consume = true;
  int opened = 0;
  qint64 receivedBytes = 0;
  QByteArray receivedFrames;
  auto read = [&] {
    if (!consume)
      return;
    const auto bytes = sink.readAll();
    receivedBytes += bytes.size();
    decoder.feed(
        {reinterpret_cast<const std::uint8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size())},
        [&](Packet packet) {
          if (packet.type == MessageType::Opened)
            ++opened;
          if (packet.type == MessageType::Frame)
            receivedFrames.append(static_cast<char>(packet.payload.back()));
          if (packet.type == MessageType::Ping)
            write(MessageType::Pong, {});
        }
    );
  };
  QObject::connect(&sink, &QSslSocket::readyRead, &sink, read);
  check(host.listen(QHostAddress::LocalHost, 0), "Cannot listen for congestion fixture");
  sink.connectToHostEncrypted("127.0.0.1", BridgeTestAccess::port(host));
  check(waitUntil([&] { return BridgeTestAccess::established(host); }), "Congestion fixture did not negotiate");
  QPointer<Channel> channel = BridgeTestAccess::channel(host);
  const auto normalSendBuffer = ChannelTestAccess::sendBuffer(*channel);
  check(normalSendBuffer.isValid() && normalSendBuffer.toInt() > 0, "Cannot inspect fixture send buffer");
  ChannelTestAccess::smallSendBuffer(*channel);
  BridgeTestAccess::opened(host, 41, 7);
  BridgeTestAccess::opened(host, 42, 8);
  check(
      waitUntil([&] { return opened == 2 && channel && channel->readyForFrame(); }),
      "Congestion fixture setup did not drain"
  );

  consume = false;
  sink.setReadBufferSize(1);
  const QByteArray largeFrame(maxPayload - 8, 'A');
  BridgeTestAccess::frame(host, 41, 7, largeFrame);
  check(
      waitUntil([&] {
        return channel && ChannelTestAccess::plaintext(*channel) == 0 && ChannelTestAccess::encrypted(*channel) > 0;
      }),
      "Congestion fixture did not isolate encrypted output"
  );
  BridgeTestAccess::frame(host, 41, 7, QByteArray("P"));
  BridgeTestAccess::frame(host, 42, 8, QByteArray("P"));
  check(
      BridgeTestAccess::pendingKeyframes(host) == 2 && keyframeRequests.isEmpty(),
      "Dropped frames triggered immediate native retries"
  );
  BridgeTestAccess::closed(host, 42);
  check(BridgeTestAccess::pendingKeyframes(host) == 1, "Closed session retained a pending keyframe");
  QEventLoop blocked;
  QTimer burst;
  QObject::connect(&burst, &QTimer::timeout, &blocked, [&] {
    for (int i = 0; i < 100; ++i)
      BridgeTestAccess::frame(host, 41, 7, QByteArray("P"));
  });
  burst.start(5);
  QTimer::singleShot(150, &blocked, &QEventLoop::quit);
  blocked.exec();
  burst.stop();
  check(
      channel && !channel->readyForFrame() && ChannelTestAccess::encrypted(*channel) > 0 &&
          keyframeRequests.isEmpty() && BridgeTestAccess::pendingKeyframes(host) == 1 &&
          BridgeTestAccess::retryActive(host),
      "Encrypted congestion caused native replay work or unbounded pending requests"
  );

  // Hold the timer while inspecting the drained gap. A dependent frame still
  // cannot pass until requestKeyframe has invalidated the native GOP.
  BridgeTestAccess::pauseRetry(host);
  // Remove BOTH artificial limits. Leaving the 4 KiB sender buffer in place
  // couples this 16 MiB drain to Linux's Nagle/delayed-ACK timing even after the
  // receiver resumes reading. Throughput timing is not what this fixture tests.
  ChannelTestAccess::resumeOutput(*channel, normalSendBuffer);
  sink.setSocketOption(QAbstractSocket::LowDelayOption, 1);
  consume = true;
  sink.setReadBufferSize(0);
  read();
  const bool drained = waitUntil([&] { return channel && channel->readyForFrame() && receivedFrames == "A"; });
  if (!drained) {
    QTextStream diagnostic(stderr);
    diagnostic << "Drain diagnostic: connected=" << BridgeTestAccess::established(host)
               << " plaintext=" << (channel ? ChannelTestAccess::plaintext(*channel) : -1)
               << " ciphertext=" << (channel ? ChannelTestAccess::encrypted(*channel) : -1)
               << " sendBuffer=" << (channel ? ChannelTestAccess::sendBuffer(*channel).toInt() : -1)
               << " receivedBytes=" << receivedBytes << " completeFrames=" << receivedFrames.size()
               << " sinkAvailable=" << sink.bytesAvailable() << " sinkState=" << sink.state() << '\n';
  }
  check(drained, "Congested media did not drain");
  BridgeTestAccess::frame(host, 41, 7, QByteArray("P"));
  check(
      channel->readyForFrame() && BridgeTestAccess::pendingKeyframes(host) == 1,
      "Dependent frame escaped the pending recovery fence"
  );
  bool clearedBeforeNative = false;
  keyframeReply = [&](NativeHost &native, std::uint64_t session, std::uint64_t epoch) {
    clearedBeforeNative = channel && channel->readyForFrame() && BridgeTestAccess::pendingKeyframes(host) == 0;
    native.onFrame(session, epoch, 1, QByteArray("R"));
  };
  BridgeTestAccess::resumeRetry(host);
  check(waitUntil([&] { return receivedFrames == "AR"; }), "Recovery keyframe was not sent after the queue drained");
  keyframeReply = {};
  check(
      clearedBeforeNative && keyframeRequests == QVector<std::pair<std::uint64_t, std::uint64_t>>{{41, 7}} &&
          BridgeTestAccess::pendingKeyframes(host) == 0 && !BridgeTestAccess::retryActive(host),
      "Recovery was not coalesced to one request for the live session"
  );

  check(waitUntil([&] { return channel && channel->readyForFrame(); }), "Recovery output did not drain");
  consume = false;
  sink.setReadBufferSize(1);
  ChannelTestAccess::smallSendBuffer(*channel);
  BridgeTestAccess::frame(host, 41, 7, largeFrame);
  BridgeTestAccess::frame(host, 41, 7, QByteArray("P"));
  check(BridgeTestAccess::pendingKeyframes(host) == 1, "Disconnect fixture has no pending recovery");
  sink.abort();
  check(waitUntil([&] { return !BridgeTestAccess::established(host); }), "Peer disconnect did not close Bridge");
  BridgeTestAccess::frame(host, 41, 7, QByteArray("P"));
  check(
      BridgeTestAccess::pendingKeyframes(host) == 0 && !BridgeTestAccess::retryActive(host) &&
          keyframeRequests.size() == 1,
      "Disconnect retained or recreated native recovery work"
  );
}
} // namespace

int main(int argc, char **argv)
{
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  try {
    const auto arguments = application.arguments();
    if (arguments.size() > 1) {
      check(
          arguments.size() == 2 && arguments[1].startsWith("--tls-backend="),
          "Usage: windowshare-bridge-tests [--tls-backend=<name>]"
      );
      check(QSslSocket::setActiveBackend(arguments[1].mid(14)), "Requested TLS backend is unavailable");
    }
    QTextStream(stdout) << "TLS backend: " << QSslSocket::activeBackend() << "; Qt " << qVersion() << '\n';
    boundedPresentation();
    delayedViewerCleanup();
    QTemporaryDir directory;
    check(directory.isValid(), "Cannot create TLS identity fixture directory");
    initializeIdentity(directory.filePath("host"));
    initializeIdentity(directory.filePath("viewer"));
    const auto hostIdentity = Identity::load(directory.filePath("host"));
    const auto viewerIdentity = Identity::load(directory.filePath("viewer"));
    initialExportWaitsForCatalog(hostIdentity, viewerIdentity);
    congestionRecovery(hostIdentity, viewerIdentity);
    QTextStream(
        stdout
    ) << "PASS: bounded catalog/title wire data, delayed viewer cleanup with reused incarnation, "
         "initial export waits for catalog completion, coalesced congestion recovery and disconnect\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
