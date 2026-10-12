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
#include <QFocusEvent>
#include <QKeyEvent>
#include <QSet>
#include <QSslConfiguration>
#include <QSslError>
#include <QTemporaryDir>

namespace {
QVector<std::uint32_t> exportedWindows;
QVector<std::pair<std::uint64_t, std::uint64_t>> keyframeRequests;
std::function<void(NativeHost &, std::uint64_t, std::uint64_t)> keyframeReply;
bool automaticNative = false;
int nativeRefreshes = 0, nativeReleases = 0, nativeRestores = 0;
QVector<QJsonObject> nativeInputs;
QSet<int> nativeHeldKeys;
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
  ++nativeRefreshes;
  if (automaticNative && onCatalog)
    onCatalog({{123, "Reconnect fixture"}});
}
void NativeHost::exportWindow(std::uint32_t id)
{
  exportedWindows.push_back(id);
}
void NativeHost::ready(std::uint64_t session, std::uint64_t epoch)
{
  if (automaticNative && onCommitted)
    onCommitted(session, epoch);
}
void NativeHost::input(std::uint64_t session, std::uint64_t epoch, const QJsonObject &event)
{
  nativeInputs.push_back(event);
  if (event["kind"] == "key") {
    const int code = event["code"].toInt();
    if (event["down"].toBool())
      nativeHeldKeys.insert(code);
    else
      nativeHeldKeys.remove(code);
  }
  if (automaticNative && event["kind"] == "focus" && onFocused)
    onFocused(session, epoch, 1, event["request"].toInteger());
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
  ++nativeRestores;
}
void NativeHost::releaseAll()
{
  ++nativeReleases;
  nativeHeldKeys.clear();
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
  static void fastReconnect(Bridge &bridge)
  {
    bridge.m_retryDelays = {10, 20, 40, 80, 120, 160};
    bridge.m_stableInterval = 100;
  }
  static bool reconnectWaiting(const Bridge &bridge)
  {
    return bridge.m_reconnectTimer.isActive();
  }
  static bool cleaning(const Bridge &bridge)
  {
    return bridge.m_cleaning;
  }
  static std::size_t retryStep(const Bridge &bridge)
  {
    return bridge.m_retryStep;
  }
  static int retryDelay(const Bridge &bridge)
  {
    return bridge.m_reconnectTimer.interval();
  }
  static std::uint64_t generation(const Bridge &bridge)
  {
    return bridge.m_connectionGeneration;
  }
  static void retry(Bridge &bridge)
  {
    bridge.m_retry->click();
  }
  static void cancel(Bridge &bridge)
  {
    bridge.m_cancel->click();
  }
  static bool openEnabled(const Bridge &bridge)
  {
    return bridge.m_open->isEnabled();
  }
  static QString status(const Bridge &bridge)
  {
    return bridge.m_status->text();
  }
  static void observeConnections(Bridge &bridge, std::function<void()> observer)
  {
    const auto accept = bridge.m_listener.incoming;
    bridge.m_listener.incoming = [accept, observer = std::move(observer)](qintptr descriptor) {
      observer();
      accept(descriptor);
    };
  }
};

void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

bool waitUntil(const std::function<bool()> &condition, const std::function<void()> &onReady = {})
{
  const auto observe = [&] {
    if (!condition())
      return false;
    if (onReady)
      onReady();
    return true;
  };
  if (observe())
    return true;
  bool observed = false;
  QEventLoop loop;
  QTimer poll, timeout;
  poll.setInterval(5);
  timeout.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (!observed && observe()) {
      observed = true;
      loop.quit();
    }
  });
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start();
  timeout.start(5000);
  loop.exec();
  // quit() does not stop other callbacks in the current event batch. Preserve
  // the successful observation even if one of them queues another heartbeat.
  return observed;
}
void runEvents(int milliseconds)
{
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}
struct AutomaticNative
{
  AutomaticNative()
  {
    automaticNative = true;
    nativeInputs.clear();
    nativeHeldKeys.clear();
  }
  ~AutomaticNative()
  {
    automaticNative = false;
  }
};
quint16 unusedPort()
{
  QTcpServer reservation;
  check(reservation.listen(QHostAddress::LocalHost, 0), "Cannot reserve reconnect fixture port");
  return reservation.serverPort();
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
  const QByteArray largeFrame(maxPayload - 8, 'A');
  check(
      waitUntil(
          [&] { return opened == 2 && channel && channel->readyForFrame(); },
          [&] {
            consume = false;
            sink.setReadBufferSize(1);
            BridgeTestAccess::frame(host, 41, 7, largeFrame);
          }
      ),
      "Congestion fixture setup did not drain"
  );

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
  const bool drained = waitUntil(
      [&] { return channel && channel->readyForFrame() && receivedFrames == "A"; },
      [&] {
        // Check the GOP fence at the exact drained observation, before another
        // heartbeat can make readyForFrame false again.
        BridgeTestAccess::frame(host, 41, 7, QByteArray("P"));
        check(
            channel->readyForFrame() && BridgeTestAccess::pendingKeyframes(host) == 1,
            "Dependent frame escaped the pending recovery fence"
        );
        // Reproduce a timer queuing control output in the same event batch as
        // waitUntil's successful poll. This must not invalidate that result.
        check(channel->send(MessageType::Ping), "Cannot queue the post-drain heartbeat regression");
      }
  );
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

  check(
      waitUntil(
          [&] { return channel && channel->readyForFrame(); },
          [&] {
            consume = false;
            sink.setReadBufferSize(1);
            ChannelTestAccess::smallSendBuffer(*channel);
            BridgeTestAccess::frame(host, 41, 7, largeFrame);
            BridgeTestAccess::frame(host, 41, 7, QByteArray("P"));
          }
      ),
      "Recovery output did not drain"
  );
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

void reconnectRefusedThenAvailable(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  AutomaticNative native;
  Bridge host(true, hostIdentity, parseFingerprint(viewerIdentity.fingerprint()), QString{}, 0, 0, false);
  Bridge viewer(false, viewerIdentity, parseFingerprint(hostIdentity.fingerprint()), QString{}, 0, 0, false);
  BridgeTestAccess::fastReconnect(viewer);
  const auto port = unusedPort();
  viewer.connectTo("127.0.0.1", port);
  check(
      waitUntil([&] { return BridgeTestAccess::generation(viewer) >= 7 && BridgeTestAccess::reconnectWaiting(viewer); }
      ),
      "Refused connections did not keep retrying to the backoff cap"
  );
  check(
      BridgeTestAccess::retryStep(viewer) == 5 && BridgeTestAccess::retryDelay(viewer) == 160 &&
          !BridgeTestAccess::channel(viewer),
      "Backoff did not cap, or timer overlapped an active attempt"
  );
  check(host.listen(QHostAddress::LocalHost, port), "Cannot start the previously unavailable host");
  int connections = 0;
  BridgeTestAccess::observeConnections(host, [&] { ++connections; });
  check(
      waitUntil([&] { return BridgeTestAccess::established(viewer) && BridgeTestAccess::catalogReady(viewer); }),
      "Automatic reconnect did not reach the newly available pinned host"
  );
  check(
      connections == 1 && !BridgeTestAccess::reconnectWaiting(viewer) && BridgeTestAccess::retryStep(viewer) == 5,
      "Negotiation reset backoff prematurely or left a second attempt scheduled"
  );
  check(waitUntil([&] { return BridgeTestAccess::retryStep(viewer) == 0; }), "Stable connection did not reset backoff");
  check(BridgeTestAccess::openEnabled(viewer), "Fresh catalog did not enable window selection");
}

void reconnectCleanupAndIsolation(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  AutomaticNative native;
  exportedWindows.clear();
  int panelQuits = 0, connections = 0;
  bool cleanupBeforeReconnect = true;
  Bridge host(true, hostIdentity, parseFingerprint(viewerIdentity.fingerprint()), QString{}, 0, 0, false);
  Bridge viewer(false, viewerIdentity, parseFingerprint(hostIdentity.fingerprint()), QString{}, 0, 123, false);
  configureApplicationLifetime(*qApp, viewer, [&] { ++panelQuits; });
  BridgeTestAccess::fastReconnect(viewer);
  check(host.listen(QHostAddress::LocalHost, 0), "Cannot listen for reconnect cleanup fixture");
  const int restoresBefore = nativeRestores, releasesBefore = nativeReleases;
  BridgeTestAccess::observeConnections(host, [&] {
    if (++connections > 1)
      cleanupBeforeReconnect = cleanupBeforeReconnect && nativeRestores > restoresBefore &&
                               nativeReleases > releasesBefore && nativeHeldKeys.isEmpty() &&
                               BridgeTestAccess::count(viewer) == 0 && !BridgeTestAccess::cleaning(viewer);
  });
  viewer.connectTo("127.0.0.1", BridgeTestAccess::port(host));
  check(
      waitUntil([&] { return exportedWindows == QVector<std::uint32_t>{123}; }), "Initial requested export was not sent"
  );
  BridgeTestAccess::opened(host, 91, 2);
  check(
      waitUntil([&] { return BridgeTestAccess::viewer(viewer, 91) != nullptr; }), "No initial viewer for cleanup test"
  );
  QPointer<Viewer> old = BridgeTestAccess::viewer(viewer, 91);
  const auto oldSend = old->onSend;
  const auto oldError = old->onError;
  const auto oldReturn = old->onReturned;
  const auto oldPacket = BridgeTestAccess::channel(viewer)->onPacket;
  const auto oldClose = BridgeTestAccess::channel(viewer)->onClosed;
  check(
      waitUntil([&] { return BridgeTestAccess::channel(host)->readyForFrame(); }), "Fixture media setup did not drain"
  );
  // The same generated solid-color SPS/PPS+IDR used by ViewerTests. Only Qt
  // events on the offscreen widget reach the fake native input implementation.
  BridgeTestAccess::frame(
      host, 91, 2,
      QByteArray::fromBase64("AAAAAWdCwAraEewEQAAAAwBAAAAPI8SJqAAAAAFozg/IAAAAAWWIhDoRigACGPHAAED2OAAIeUnJyddddddddeA=")
  );
  check(waitUntil([&] { return old && old->firstFramePainted(); }), "Reconnect fixture frame was not painted");
  QFocusEvent focus(QEvent::FocusIn, Qt::OtherFocusReason);
  QCoreApplication::sendEvent(old, &focus);
  check(
      waitUntil([&] {
        return std::any_of(nativeInputs.begin(), nativeInputs.end(), [](const auto &event) {
          return event["kind"] == "focus";
        });
      }),
      "Reconnect fixture did not acquire focus"
  );
  QKeyEvent down(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
  QCoreApplication::sendEvent(old, &down);
  check(waitUntil([&] { return nativeHeldKeys.contains(Qt::Key_A); }), "Reconnect fixture did not deliver held input");
  const auto inputsBefore = nativeInputs.size();
  const int refreshesBefore = nativeRefreshes;
  BridgeTestAccess::channel(host)->close("Fixture network outage", CloseCause::Network);
  check(
      waitUntil([&] {
        return connections == 2 && BridgeTestAccess::established(viewer) && BridgeTestAccess::catalogReady(viewer);
      }),
      "Established connection did not reconnect"
  );
  check(
      cleanupBeforeReconnect && nativeHeldKeys.isEmpty() && BridgeTestAccess::count(viewer) == 0 &&
          nativeInputs.size() == inputsBefore && exportedWindows == QVector<std::uint32_t>{123} &&
          nativeRefreshes > refreshesBefore,
      "Reconnect ran before cleanup, reused a catalog, or replayed input/export/session state"
  );
  check(!old, "Old viewer survived connection cleanup");
  // Offscreen plugins may omit the native last-window notification. Exercise
  // the application notification as well as the actual viewer destruction.
  qApp->lastWindowClosed();
  check(
      panelQuits == 0 && !qApp->quitOnLastWindowClosed(), "Closing the last headless viewer requested application exit"
  );

  const QJsonObject metadata{
      {"title", "Stale callback"}, {"width", 640}, {"height", 480}, {"geometry", 1}, {"codec", "h264"}
  };
  oldPacket(Packet{MessageType::Opened, 91, 2, 1, jsonBytes(metadata)});
  check(BridgeTestAccess::count(viewer) == 0, "Old channel callback created a viewer in the new connection");
  BridgeTestAccess::opened(host, 91, 2);
  check(waitUntil([&] { return BridgeTestAccess::viewer(viewer, 91) != nullptr; }), "No replacement viewer");
  auto *replacement = BridgeTestAccess::viewer(viewer, 91);
  const auto status = BridgeTestAccess::status(viewer);
  check(
      !oldSend(MessageType::Input, {{"kind", "key"}, {"code", Qt::Key_A}, {"down", false}}, 91, 2),
      "Old viewer callback sent input into the new connection"
  );
  oldReturn();
  oldError("stale error");
  oldClose("stale disconnect", CloseCause::Network);
  check(
      BridgeTestAccess::viewer(viewer, 91) == replacement && BridgeTestAccess::established(viewer) &&
          BridgeTestAccess::status(viewer) == status,
      "Old callbacks changed replacement connection state"
  );
  viewer.close();
  check(
      panelQuits == 1 && !BridgeTestAccess::reconnectWaiting(viewer),
      "Explicit panel close did not stop reconnect and request exit"
  );
  check(waitUntil([&] { return !BridgeTestAccess::cleaning(viewer); }), "Panel-close cleanup did not finish");
}

void terminalReconnectFailures(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  for (int scenario = 0; scenario != 3; ++scenario) {
    Listener listener;
    std::vector<std::unique_ptr<Channel>> peers;
    int connections = 0;
    listener.incoming = [&](qintptr descriptor) {
      ++connections;
      if (scenario == 1) {
        QTcpSocket socket;
        socket.setSocketDescriptor(descriptor);
        socket.abort();
        return;
      }
      auto peer = std::make_unique<Channel>(hostIdentity, parseFingerprint(viewerIdentity.fingerprint()));
      auto *server = peer.get();
      server->onPacket = [server, scenario](Packet) {
        server->send(
            MessageType::Accepted,
            {{"version", scenario == 2 ? 99 : static_cast<int>(protocolVersion)}, {"role", "host"}, {"codec", "h264"}}
        );
      };
      peers.push_back(std::move(peer));
      server->accept(descriptor);
    };
    check(listener.listen(QHostAddress::LocalHost, 0), "Cannot listen for terminal reconnect fixture");
    auto pin = parseFingerprint(hostIdentity.fingerprint());
    if (scenario == 0)
      pin[0] ^= 1;
    Bridge viewer(false, viewerIdentity, pin, QString{}, 0, 0, false);
    BridgeTestAccess::fastReconnect(viewer);
    viewer.connectTo("127.0.0.1", listener.serverPort());
    check(
        waitUntil([&] {
          return connections == 1 && !BridgeTestAccess::channel(viewer) && !BridgeTestAccess::cleaning(viewer);
        }),
        "Terminal failure did not close and clean up the connection"
    );
    runEvents(60);
    check(
        connections == 1 && !BridgeTestAccess::reconnectWaiting(viewer),
        "Authentication, pre-auth EOF, or protocol failure automatically retried"
    );
    if (scenario == 0) {
      BridgeTestAccess::retry(viewer);
      check(
          waitUntil([&] {
            return connections == 2 && !BridgeTestAccess::channel(viewer) && !BridgeTestAccess::cleaning(viewer);
          }),
          "Manual retry did not retain the explicit, still-mismatched pin"
      );
      check(
          !BridgeTestAccess::established(viewer) && !BridgeTestAccess::reconnectWaiting(viewer),
          "Manual retry changed trust policy"
      );
    }
  }
}

void reconnectWaitsForHostCleanup(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  AutomaticNative native;
  Bridge host(true, hostIdentity, parseFingerprint(viewerIdentity.fingerprint()), QString{}, 0, 0, false);
  Bridge first(false, viewerIdentity, parseFingerprint(hostIdentity.fingerprint()), QString{}, 0, 0, false);
  Bridge replacement(false, viewerIdentity, parseFingerprint(hostIdentity.fingerprint()), QString{}, 0, 0, false);
  check(host.listen(QHostAddress::LocalHost, 0), "Cannot listen for host-cleanup handoff");
  const int restoresBefore = nativeRestores;
  int connections = 0;
  bool cleanedBeforeAccept = false;
  BridgeTestAccess::observeConnections(host, [&] {
    if (++connections == 2)
      cleanedBeforeAccept = nativeRestores > restoresBefore && !BridgeTestAccess::cleaning(host);
  });
  first.connectTo("127.0.0.1", BridgeTestAccess::port(host));
  check(waitUntil([&] { return BridgeTestAccess::established(first); }), "First peer did not establish");
  replacement.connectTo("127.0.0.1", BridgeTestAccess::port(host));
  runEvents(50);
  check(
      connections == 1 && BridgeTestAccess::channel(replacement) && !BridgeTestAccess::established(replacement),
      "Busy host rejected or authenticated replacement before cleanup"
  );
  BridgeTestAccess::cancel(first);
  check(
      waitUntil([&] {
        return BridgeTestAccess::established(replacement) && BridgeTestAccess::catalogReady(replacement);
      }),
      "Waiting replacement did not connect after host cleanup"
  );
  check(
      connections == 2 && cleanedBeforeAccept && BridgeTestAccess::generation(replacement) == 1,
      "Replacement required a retry or was accepted before source cleanup"
  );
}

void cancelRetryAndDestroy(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  const auto port = unusedPort();
  Bridge viewer(false, viewerIdentity, parseFingerprint(hostIdentity.fingerprint()), QString{}, 0, 0, false);
  BridgeTestAccess::fastReconnect(viewer);
  viewer.connectTo("127.0.0.1", port);
  check(waitUntil([&] { return BridgeTestAccess::reconnectWaiting(viewer); }), "No pending retry to cancel");
  auto generation = BridgeTestAccess::generation(viewer);
  BridgeTestAccess::cancel(viewer);
  runEvents(60);
  check(
      BridgeTestAccess::generation(viewer) == generation && !BridgeTestAccess::channel(viewer) &&
          !BridgeTestAccess::reconnectWaiting(viewer),
      "Cancel left a reconnect timer running"
  );
  BridgeTestAccess::retry(viewer);
  ++generation;
  check(
      BridgeTestAccess::generation(viewer) == generation && BridgeTestAccess::channel(viewer),
      "Manual Retry did not start exactly one attempt"
  );
  BridgeTestAccess::retry(viewer);
  check(BridgeTestAccess::generation(viewer) == generation, "Retry overlapped an active attempt");
  BridgeTestAccess::cancel(viewer);
  check(BridgeTestAccess::cleaning(viewer), "Canceled attempt skipped deferred cleanup");
  BridgeTestAccess::retry(viewer);
  check(!BridgeTestAccess::channel(viewer), "Retry started before cleanup completed");
  BridgeTestAccess::cancel(viewer);
  runEvents(60);
  check(
      BridgeTestAccess::generation(viewer) == generation && !BridgeTestAccess::cleaning(viewer) &&
          !BridgeTestAccess::reconnectWaiting(viewer),
      "Cancel during cleanup failed to discard queued manual retry"
  );

  auto doomed = std::make_unique<Bridge>(
      false, viewerIdentity, parseFingerprint(hostIdentity.fingerprint()), QString{}, 0, 0, false
  );
  BridgeTestAccess::fastReconnect(*doomed);
  doomed->connectTo("127.0.0.1", port);
  check(
      waitUntil([&] { return BridgeTestAccess::reconnectWaiting(*doomed); }), "No pending retry for destruction fixture"
  );
  doomed.reset();
  Listener listener;
  int connections = 0;
  listener.incoming = [&](qintptr descriptor) {
    ++connections;
    QTcpSocket socket;
    socket.setSocketDescriptor(descriptor);
    socket.abort();
  };
  check(listener.listen(QHostAddress::LocalHost, port), "Cannot listen after destroying retry owner");
  runEvents(80);
  check(connections == 0, "Destroyed Bridge left a retry callback running");
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
    reconnectRefusedThenAvailable(hostIdentity, viewerIdentity);
    reconnectCleanupAndIsolation(hostIdentity, viewerIdentity);
    terminalReconnectFailures(hostIdentity, viewerIdentity);
    reconnectWaitsForHostCleanup(hostIdentity, viewerIdentity);
    cancelRetryAndDestroy(hostIdentity, viewerIdentity);
    QTextStream(stdout) << "PASS: bounded catalog/title wire data, delayed viewer cleanup with reused incarnation, "
                           "catalog completion, congestion recovery, reconnect backoff/cleanup/isolation, terminal "
                           "failures, cancellation and panel lifetime\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
