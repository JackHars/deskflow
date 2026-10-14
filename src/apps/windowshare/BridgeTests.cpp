/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
// Keep the controller private to its executable while testing its actual
// callback wiring. Its native implementation below never touches a display.
#define WINDOWSHARE_BRIDGE_TEST
#include "NativeKeyboardAdmission.h"
#include "main.cpp"
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QSslConfiguration>
#include <QSslError>
#include <QTemporaryDir>
#include <QWheelEvent>

namespace {
QVector<std::uint32_t> exportedWindows;
QVector<std::pair<std::uint64_t, std::uint64_t>> keyframeRequests;
QVector<std::array<std::uint64_t, 3>> nativePaintedReady;
std::function<void(NativeHost &, std::uint64_t, std::uint64_t)> keyframeReply;
bool automaticNative = false;
int nativeRefreshes = 0, nativeReleases = 0, nativeRestores = 0;
QVector<QJsonObject> nativeInputs;
QSet<int> nativeHeldKeys;
QSet<int> nativeHeldButtons;
std::function<void()> nativeResumeObserver;
std::function<void()> nativeQuiesceObserver;
std::function<void()> nativeCandidateObserver;
std::function<void()> nativeConstructObserver, nativeReleaseObserver, nativeRestoreObserver;
bool nativeResumeSucceeds = true;
struct ClipboardFixtureData
{
  QString text;
  unsigned reads = 0, writes = 0;
  std::function<void()> onWrite;
};
} // namespace

namespace deskflow::windowshare {
struct NativeHost::Impl
{
  std::optional<KeyboardRouteBinding> focus;
  NativeKeyboardAdmission keyboard;
  unsigned captureRate = defaultCaptureRate;
};
NativeHost::NativeHost(QString, QObject *parent) : QObject(parent), m_impl(std::make_unique<Impl>())
{
  const auto observer = nativeConstructObserver;
  if (observer)
    observer();
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
bool NativeHost::setCaptureFrameRate(unsigned rate)
{
  if (!validCaptureRate(rate))
    return false;
  m_impl->captureRate = rate;
  return true;
}
unsigned NativeHost::captureFrameRate() const
{
  return m_impl->captureRate;
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
void NativeHost::observeWindowPointer(const LocalWindowPointer &)
{
}
void NativeHost::ready(std::uint64_t session, std::uint64_t epoch, std::uint64_t paintedGeometry)
{
  nativePaintedReady.push_back({session, epoch, paintedGeometry});
  if (automaticNative && onCommitted)
    onCommitted(session, epoch);
}
void NativeHost::input(std::uint64_t session, std::uint64_t epoch, const QJsonObject &event)
{
  if (event["kind"] == "key" && m_impl->keyboard.blocksKeys() && event["down"].toBool())
    return;
  if (m_impl->keyboard.blocksPointers() &&
      ((event["kind"] == "button" && event["down"].toBool()) || event["kind"] == "move" || event["kind"] == "scroll"))
    return;
  nativeInputs.push_back(event);
  if (event["kind"] == "key") {
    const int code = event["code"].toInt();
    if (event["down"].toBool())
      nativeHeldKeys.insert(code);
    else
      nativeHeldKeys.remove(code);
  }
  if (event["kind"] == "button") {
    if (event["down"].toBool())
      nativeHeldButtons.insert(event["code"].toInt());
    else
      nativeHeldButtons.remove(event["code"].toInt());
  }
  if (event["kind"] == "focus") {
    m_impl->focus = KeyboardRouteBinding{
        {session, epoch},
        0,
        static_cast<std::uint64_t>(event["request"].toInteger()),
        1,
        static_cast<std::uint64_t>(event["geometry"].toInteger())
    };
    if (automaticNative && onFocused)
      onFocused(session, epoch, 1, event["request"].toInteger());
  }
  if (event["kind"] == "blur")
    m_impl->focus.reset();
}
std::optional<KeyboardRouteBinding> NativeHost::keyboardRouteCandidate(
    std::uint64_t session, std::uint64_t epoch, std::uint64_t generation, ForwardingScope scope
) const
{
  const auto observer = nativeCandidateObserver;
  if (observer)
    observer();
  auto binding = m_impl->focus;
  if (!binding || binding->owner != Owner{session, epoch})
    return std::nullopt;
  binding->generation = generation;
  binding->scope = scope;
  return binding;
}
bool NativeHost::quiesceKeyboard(const KeyboardRouteBinding &binding)
{
  const bool result = m_impl->keyboard.quiesce(
      binding, keyboardRouteCandidate(binding.owner.session, binding.owner.epoch, binding.generation, binding.scope),
      nativeHeldKeys.isEmpty() && nativeHeldButtons.isEmpty()
  );
  const auto observer = nativeQuiesceObserver;
  if (observer)
    observer();
  return result;
}
bool NativeHost::keyboardRouteQuiesced(const KeyboardRouteBinding &binding) const
{
  return m_impl->keyboard.quiesced(
      binding, keyboardRouteCandidate(binding.owner.session, binding.owner.epoch, binding.generation, binding.scope),
      nativeHeldKeys.isEmpty() && nativeHeldButtons.isEmpty()
  );
}
bool NativeHost::resumeKeyboard(const KeyboardRouteBinding &binding)
{
  if (nativeResumeObserver)
    nativeResumeObserver();
  return nativeResumeSucceeds && m_impl->keyboard.resume(binding);
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
  const auto observer = nativeRestoreObserver;
  if (observer)
    observer();
}
void NativeHost::releaseAll()
{
  ++nativeReleases;
  nativeHeldKeys.clear();
  nativeHeldButtons.clear();
  m_impl->focus.reset();
  const auto observer = nativeReleaseObserver;
  if (observer)
    observer();
}
std::optional<ClipboardFocusObservation> NativeHost::clipboardFocus() const
{
  if (!m_impl->focus)
    return {};
  const auto focus = *m_impl->focus;
  const ClipboardFocusBinding binding{focus.owner, focus.focusRequest, focus.focusRevision, focus.geometry};
  const QPointer<const NativeHost> alive(this);
  return ClipboardFocusObservation{binding, [alive, focus] { return alive && alive->m_impl->focus == focus; }};
}
void NativeHost::setClipboardTextMonitoring(bool)
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
  static unsigned captureRate(const Bridge &bridge)
  {
    return bridge.m_native ? bridge.m_native->captureFrameRate() : 0;
  }
  static void fakeClipboard(Bridge &bridge, ClipboardFixtureData &data)
  {
    const bool enabled = bridge.m_clipboard->enabled();
    const QPointer<Bridge> alive(&bridge);
    FocusedClipboard::Callbacks callbacks;
    callbacks.focus = [alive] { return alive ? alive->clipboardFocus() : std::nullopt; };
    callbacks.readText = [&data] {
      ++data.reads;
      return std::optional(data.text);
    };
    callbacks.writeText = [&data](const QString &text) {
      ++data.writes;
      data.text = text;
      const auto callback = data.onWrite;
      if (callback)
        callback();
      return true;
    };
    callbacks.send = [alive](const ClipboardTextMessage &message) {
      return alive && alive->send(
                          MessageType::ClipboardText, encodeClipboardText(message), message.focus.owner.session,
                          message.focus.owner.epoch
                      );
    };
    bridge.m_clipboard = std::make_unique<FocusedClipboard>(bridge.m_clipboardPolicy, std::move(callbacks));
    bridge.m_clipboard->setNegotiated(enabled);
  }
  static bool clipboardEnabled(const Bridge &bridge)
  {
    return bridge.m_clipboard->enabled();
  }
  static bool clipboardChanged(Bridge &bridge)
  {
    return bridge.m_clipboard->localChanged();
  }
  static std::optional<ClipboardFocusObservation> clipboardFocus(const Bridge &bridge)
  {
    return bridge.clipboardFocus();
  }
  static QString firstCatalogTitle(const Bridge &bridge)
  {
    return bridge.m_catalog->count() ? bridge.m_catalog->item(0)->text() : QString{};
  }
  static bool stopped(const Bridge &bridge)
  {
    return bridge.m_shutdownComplete;
  }
  static bool stopping(const Bridge &bridge)
  {
    return bridge.m_shuttingDown;
  }
  static bool listening(const Bridge &bridge)
  {
    return bridge.m_listener.isListening();
  }
  static std::optional<KeyboardRouteBinding> keyboardBinding(const Bridge &bridge)
  {
    return bridge.m_keyboardRoute ? std::optional(bridge.m_keyboardRoute->binding) : std::nullopt;
  }
  static bool keyboardQuiesced(const Bridge &bridge)
  {
    return bridge.m_keyboardRoute && bridge.originalKeyboardQuiesced(bridge.m_keyboardRoute->binding);
  }
  static qint64 keyboardDeadline(const Bridge &bridge)
  {
    return bridge.m_keyboardRoute->deadline.deadlineNSecs();
  }
  static void observedNativeFocus(Bridge &bridge, KeyboardRouteBinding binding)
  {
    const auto callback = std::exchange(bridge.m_native->onFocused, {});
    bridge.m_native->input(
        binding.owner.session, binding.owner.epoch,
        {{"kind", "focus"}, {"request", number(binding.focusRequest)}, {"geometry", number(binding.geometry)}}
    );
    bridge.m_native->onFocused = callback;
  }
  static void keyboardReply(Bridge &bridge, const KeyboardRouteControl &control)
  {
    bridge.receiveKeyboardRoute(
        Packet{
            forwardingRouteMessageType(control.binding), control.binding.owner.session, control.binding.owner.epoch, 1,
            jsonBytes(encodeKeyboardRouteControl(control))
        }
    );
  }
  static void fastKeyboardTimeout(Bridge &bridge)
  {
    bridge.m_keyboardTimeoutMs = 40;
  }
  static void deliverKeyboardTimeout(Bridge &bridge)
  {
    QMetaObject::invokeMethod(&bridge.m_keyboardTimeout, "timeout", Qt::DirectConnection);
  }
  static void invalidateKeyboard(Bridge &bridge, KeyboardRouteReason reason)
  {
    bridge.m_native->onKeyboardRouteInvalidated(bridge.m_keyboardRoute->binding, reason);
  }
  static bool keyboardCapable(const Bridge &bridge)
  {
    return bridge.m_keyboardCapable;
  }
  static bool inputCapable(const Bridge &bridge)
  {
    return bridge.m_inputCapable;
  }
  static bool mediaCapable(const Bridge &bridge)
  {
    return bridge.m_mediaRecoveryCapable;
  }
  static void receive(Bridge &bridge, const Packet &packet)
  {
    bridge.receiveViewer(packet);
  }
  static void receiveHost(Bridge &bridge, const Packet &packet)
  {
    bridge.receiveHost(packet);
  }
  static void hostSession(Bridge &bridge, std::uint64_t session, std::uint64_t epoch, std::uint64_t geometry)
  {
    bridge.m_hostSessions[session] = epoch;
    bridge.m_hostGeometries[session] = geometry;
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
  // Fixture for a sendFrame() congestion refusal, exercised through the real
  // Bridge retry timer path without filling the TLS buffers on every iteration.
  static void droppedFrame(Bridge &bridge, std::uint64_t session, std::uint64_t epoch)
  {
    bridge.m_pendingKeyframes[session] = epoch;
    if (!bridge.m_keyframeRetry.isActive())
      bridge.m_keyframeRetry.start();
  }
  static void resetKeyframeRetries(Bridge &bridge)
  {
    bridge.clearKeyframeRetries();
  }
  static void recoveryTick(Bridge &bridge)
  {
    bridge.retryKeyframe();
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
    nativeHeldButtons.clear();
    nativeResumeObserver = {};
    nativeQuiesceObserver = {};
    nativeCandidateObserver = {};
    nativeConstructObserver = {};
    nativeReleaseObserver = {};
    nativeRestoreObserver = {};
    nativeResumeSucceeds = true;
  }
  ~AutomaticNative()
  {
    automaticNative = false;
    nativeResumeObserver = {};
    nativeQuiesceObserver = {};
    nativeCandidateObserver = {};
    nativeConstructObserver = {};
    nativeReleaseObserver = {};
    nativeRestoreObserver = {};
    nativeResumeSucceeds = true;
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

void paintedReadyBinding()
{
  nativePaintedReady.clear();
  Bridge host(true, Identity{QSslCertificate{}, QSslKey{}}, QByteArray(32, '\0'), QString{}, 0, 0, false);
  BridgeTestAccess::hostSession(host, 91, 2, 7);
  BridgeTestAccess::hostSession(host, 92, 3, 8);
  const auto ready = [&](std::uint64_t session, std::uint64_t epoch, QJsonObject payload) {
    BridgeTestAccess::receiveHost(host, {MessageType::Ready, session, epoch, 1, jsonBytes(payload)});
  };
  ready(91, 2, {{"geometry", 6}});
  ready(91, 1, {{"geometry", 7}});
  ready(93, 2, {{"geometry", 7}});
  ready(91, 2, {{"geometry", 8}});
  check(nativePaintedReady.isEmpty(), "Stale or cross-session painted acknowledgement reached native relocation");
  ready(91, 2, {{"geometry", 7}});
  ready(92, 3, {{"geometry", 8}});
  check(
      nativePaintedReady == QVector<std::array<std::uint64_t, 3>>{{91, 2, 7}, {92, 3, 8}},
      "Bridge did not forward the exact painted geometry independently for each session"
  );
  BridgeTestAccess::hostSession(host, 91, 2, 9);
  ready(91, 2, {{"geometry", 7}});
  check(nativePaintedReady.size() == 2, "Late first paint hid a source with newer native geometry");
  for (const auto &payload :
       {QJsonObject{}, QJsonObject{{"geometry", 0}}, QJsonObject{{"geometry", 9}, {"unexpected", true}}}) {
    bool rejected = false;
    try {
      ready(91, 2, payload);
    } catch (const std::exception &) {
      rejected = true;
    }
    check(
        rejected && nativePaintedReady.size() == 2,
        "Malformed painted acknowledgement was admitted or fabricated a native revision"
    );
  }
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
  check(!BridgeTestAccess::keyboardCapable(host), "Legacy peer gained unnegotiated keyboard capability");
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
  // A low-numbered stream can immediately fail again after a native GOP
  // restart. The old begin()-based loop would run session 41 twice before ever
  // attempting session 42. Exercise real Bridge retryKeyframe() with the live
  // TLS channel ready and a synchronous NativeHost callback that requeues 41.
  BridgeTestAccess::opened(host, 42, 9);
  check(
      waitUntil([&] { return opened == 3 && channel && channel->readyForFrame(); }),
      "Second stream for fair recovery was not established"
  );
  BridgeTestAccess::resetKeyframeRetries(host);
  QVector<std::uint64_t> recoveryOrder;
  int repeatedFailures = 0;
  keyframeReply = [&](NativeHost &, std::uint64_t session, std::uint64_t epoch) {
    recoveryOrder.push_back(session);
    if (session == 41 && ++repeatedFailures == 1)
      BridgeTestAccess::droppedFrame(host, session, epoch);
  };
  BridgeTestAccess::droppedFrame(host, 41, 7);
  BridgeTestAccess::droppedFrame(host, 42, 9);
  BridgeTestAccess::pauseRetry(host);
  for (int tick = 0; tick < 3; ++tick) {
    check(channel && channel->readyForFrame(), "Recovery fairness fixture lost send capacity");
    BridgeTestAccess::recoveryTick(host);
    BridgeTestAccess::pauseRetry(host);
  }
  keyframeReply = {};
  check(
      recoveryOrder == QVector<std::uint64_t>{41, 42, 41} && BridgeTestAccess::pendingKeyframes(host) == 0 &&
          !BridgeTestAccess::retryActive(host) &&
          keyframeRequests == QVector<std::pair<std::uint64_t, std::uint64_t>>{{41, 7}, {41, 7}, {42, 9}, {41, 7}},
      "Repeated keyframe failure starved a different session or lost recovery ownership"
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
          keyframeRequests.size() == 4,
      "Disconnect retained or recreated native recovery work"
  );
}

void negotiatedMediaRecovery(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  keyframeRequests.clear();
  keyframeReply = {};
  AutomaticNative native;
  Bridge host(true, hostIdentity, parseFingerprint(viewerIdentity.fingerprint()), QString{}, 0, 0, false);
  Channel peer(viewerIdentity, parseFingerprint(hostIdentity.fingerprint()));
  bool negotiated = false;
  int opened = 0;
  peer.onAuthenticated = [&] {
    check(
        peer.send(
            MessageType::Hello, {{"version", static_cast<int>(protocolVersion)},
                                 {"role", "viewer"},
                                 {"codec", "h264"},
                                 {"capabilities", keyboardRouteCapabilities(true, true)}}
        ),
        "Cannot send media recovery capability negotiation"
    );
  };
  peer.onPacket = [&](const Packet &packet) {
    if (packet.type == MessageType::Accepted) {
      check(peerSupportsMediaRecovery(parseObject(packet)), "Host omitted negotiated media recovery capability");
      negotiated = true;
    } else if (packet.type == MessageType::Opened) {
      ++opened;
    }
  };
  check(host.listen(QHostAddress::LocalHost, 0), "Cannot listen for media recovery fixture");
  peer.connectTo("127.0.0.1", BridgeTestAccess::port(host));
  check(
      waitUntil([&] { return negotiated && BridgeTestAccess::mediaCapable(host); }),
      "Media capability did not bind to the authenticated connection"
  );
  BridgeTestAccess::opened(host, 815, 8);
  check(waitUntil([&] { return opened == 1; }), "Host did not publish the bound media session");
  auto recovery = [&](std::uint64_t request, std::uint64_t geometry = 1, std::uint64_t session = 815,
                      std::uint64_t epoch = 8) {
    return peer.send(
        MessageType::Input, {{"kind", "video-recovery"}, {"geometry", number(geometry)}, {"request", number(request)}},
        session, epoch
    );
  };
  check(recovery(1), "Cannot send a session-bound video recovery request");
  check(
      waitUntil([&] { return keyframeRequests.size() == 1; }),
      "Bounded authenticated media recovery did not reach the real native callback"
  );
  check(
      keyframeRequests[0] == std::pair<std::uint64_t, std::uint64_t>{815, 8},
      "Video recovery selected another session or epoch"
  );
  check(
      recovery(1) && recovery(2, 999) && recovery(3, 1, 9999),
      "Cannot send duplicate/stale/wrong-session recovery variants"
  );
  QEventLoop settle;
  QTimer::singleShot(130, &settle, &QEventLoop::quit);
  settle.exec();
  check(
      keyframeRequests.size() == 1 && BridgeTestAccess::established(host),
      "Duplicate, stale geometry or foreign owner restarted the native encoder"
  );
  // An ordinary replay/refusal must not consume the next valid request ID.
  check(recovery(2), "Cannot send next valid recovery request");
  check(
      waitUntil([&] { return keyframeRequests.size() == 2; }),
      "A new monotonic media request did not restart its own stream"
  );
  check(BridgeTestAccess::established(host), "Media recovery request closed a live peer");
  check(
      peer.send(
          MessageType::Input, {{"kind", "video-recovery"}, {"geometry", 1}, {"request", 3}, {"nativeCode", 777}}, 815, 8
      ),
      "Cannot send malformed recovery envelope"
  );
  check(
      waitUntil([&] { return !BridgeTestAccess::established(host); }),
      "Malformed media control envelope did not fail the authenticated channel closed"
  );
  check(
      keyframeRequests.size() == 2 && BridgeTestAccess::pendingKeyframes(host) == 0,
      "Malformed control or terminal connection retained video recovery work"
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
      waitUntil([&] {
        return BridgeTestAccess::generation(viewer) >= 7 && BridgeTestAccess::reconnectWaiting(viewer);
      }),
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

struct KeyboardFixture
{
  AutomaticNative native;
  Bridge host, viewer;
  QPointer<Viewer> window;
  int grants = 0, revokes = 0;
  bool original = false, releaseSucceeds = true;
  QVector<KeyboardRouteReason> reasons;

  KeyboardFixture(const Identity &hostIdentity, const Identity &viewerIdentity, bool clipboard = false)
      : host(true, hostIdentity, parseFingerprint(viewerIdentity.fingerprint()), QString{}, 0, 0, false, clipboard),
        viewer(false, viewerIdentity, parseFingerprint(hostIdentity.fingerprint()), QString{}, 0, 0, false, clipboard)
  {
    host.revokeOriginalKeyboard = [this](const auto &, auto reason) {
      ++revokes;
      reasons.push_back(reason);
      original = false;
      return releaseSucceeds;
    };
    host.onKeyboardQuiesced = [this](const auto &) {
      check(nativeHeldKeys.isEmpty() && nativeHeldButtons.isEmpty(), "Original grant preceded Qt releases");
      check(BridgeTestAccess::keyboardQuiesced(host), "Original grant preceded native keyboard fence");
      original = true;
      ++grants;
    };
    nativeResumeObserver = [this] { check(!original, "Native keyboard resumed before original authority revoke"); };
    check(host.listen(QHostAddress::LocalHost, 0), "Cannot listen for keyboard fixture");
    viewer.connectTo("127.0.0.1", BridgeTestAccess::port(host));
    check(waitUntil([&] { return BridgeTestAccess::catalogReady(viewer); }), "Keyboard fixture did not negotiate");
    check(
        BridgeTestAccess::keyboardCapable(host) && BridgeTestAccess::keyboardCapable(viewer),
        "Keyboard capability was not negotiated"
    );
    BridgeTestAccess::opened(host, 91, 2);
    check(waitUntil([&] { return BridgeTestAccess::viewer(viewer, 91) != nullptr; }), "Keyboard viewer missing");
    window = BridgeTestAccess::viewer(viewer, 91);
    check(waitUntil([&] { return BridgeTestAccess::channel(host)->readyForFrame(); }), "Keyboard frame queue blocked");
    BridgeTestAccess::frame(
        host, 91, 2,
        QByteArray::fromBase64(
            "AAAAAWdCwAraEewEQAAAAwBAAAAPI8SJqAAAAAFozg/IAAAAAWWIhDoRigACGPHAAED2OAAIeUnJyddddddddeA="
        )
    );
    check(waitUntil([&] { return window && window->firstFramePainted(); }), "Keyboard frame not painted");
    QFocusEvent focus(QEvent::FocusIn, Qt::OtherFocusReason);
    QCoreApplication::sendEvent(window, &focus);
    check(
        waitUntil([&] {
          return std::any_of(nativeInputs.begin(), nativeInputs.end(), [](const auto &input) {
            return input["kind"] == "focus";
          });
        }),
        "Keyboard fixture focus request missing"
    );
    key(QEvent::KeyPress, Qt::Key_A);
    check(waitUntil([&] { return nativeHeldKeys.contains(Qt::Key_A); }), "Keyboard focus did not acknowledge");
    key(QEvent::KeyRelease, Qt::Key_A);
    check(waitUntil([&] { return nativeHeldKeys.isEmpty(); }), "Keyboard fixture key release missing");
  }
  ~KeyboardFixture()
  {
    host.withdrawOriginalKeyboard(KeyboardRouteReason::Disconnected, false);
    host.revokeOriginalKeyboard = {};
    host.onKeyboardQuiesced = {};
    nativeResumeObserver = {};
  }
  void key(QEvent::Type type, int code, bool repeat = false)
  {
    QKeyEvent event(type, code, Qt::NoModifier, {}, repeat);
    QCoreApplication::sendEvent(window, &event);
  }
  void button(bool down)
  {
    const auto position = window->videoRect().center();
    QMouseEvent event(
        down ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease, position, position, Qt::LeftButton,
        down ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier
    );
    QCoreApplication::sendEvent(window, &event);
  }
  KeyboardRouteBinding request()
  {
    check(host.requestOriginalKeyboard(91, 2), "Keyboard handoff request failed");
    const auto binding = BridgeTestAccess::keyboardBinding(host);
    check(binding.has_value(), "Keyboard attempt not retained");
    return *binding;
  }
  void quiesce()
  {
    request();
    check(waitUntil([&] { return original && BridgeTestAccess::keyboardQuiesced(host); }), "Keyboard did not quiesce");
  }
};

void focusedClipboardPolicies(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  for (const auto policies : {std::pair{false, false}, std::pair{true, false}, std::pair{false, true}}) {
    AutomaticNative native;
    Bridge host(true, hostIdentity, parseFingerprint(viewerIdentity.fingerprint()), {}, 0, 0, false, policies.first);
    Bridge viewer(
        false, viewerIdentity, parseFingerprint(hostIdentity.fingerprint()), {}, 0, 0, false, policies.second
    );
    ClipboardFixtureData source{QStringLiteral("never read source")}, destination{QStringLiteral("never read viewer")};
    BridgeTestAccess::fakeClipboard(host, source);
    BridgeTestAccess::fakeClipboard(viewer, destination);
    check(host.listen(QHostAddress::LocalHost, 0), "Cannot listen for clipboard policy fixture");
    viewer.connectTo("127.0.0.1", BridgeTestAccess::port(host));
    check(
        waitUntil([&] { return BridgeTestAccess::established(host) && BridgeTestAccess::established(viewer); }),
        "Clipboard policy fixture did not negotiate TLS"
    );
    check(
        !BridgeTestAccess::clipboardEnabled(host) && !BridgeTestAccess::clipboardEnabled(viewer) &&
            !BridgeTestAccess::clipboardChanged(host) && !BridgeTestAccess::clipboardChanged(viewer) && !source.reads &&
            !source.writes && !destination.reads && !destination.writes,
        "default/one-sided policy cannot touch either private clipboard through the real Bridge"
    );
    const ClipboardTextMessage unsolicited{{{91, 2}, 1, 1, 1}, 1, QStringLiteral("unsolicited")};
    check(
        BridgeTestAccess::channel(viewer)->send(MessageType::ClipboardText, encodeClipboardText(unsolicited), 91, 2),
        "Cannot submit unnegotiated clipboard rejection fixture"
    );
    check(
        waitUntil([&] { return !BridgeTestAccess::established(host); }) && !source.writes,
        "an unnegotiated clipboard packet closes the protocol without applying text"
    );
  }
}
void focusedClipboardRoundTrip(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  ClipboardFixtureData source{QStringLiteral("Mac source text")}, destination{QStringLiteral("Ubuntu viewer text")};
  KeyboardFixture fixture(hostIdentity, viewerIdentity, true);
  auto &host = fixture.host;
  auto &viewer = fixture.viewer;
  fixture.window->activateWindow();
  fixture.window->setFocus(Qt::OtherFocusReason);
  BridgeTestAccess::fakeClipboard(host, source);
  BridgeTestAccess::fakeClipboard(viewer, destination);
  check(
      waitUntil([&] {
        const auto local = BridgeTestAccess::clipboardFocus(host), remote = BridgeTestAccess::clipboardFocus(viewer);
        return local && remote && local->live() && remote->live() && local->binding == remote->binding;
      }),
      "real Bridge clipboard endpoints lack matching focused painted identity"
  );
  check(
      !source.reads && !destination.reads && !source.writes && !destination.writes,
      "focus and negotiation do not read preexisting clipboard text"
  );
  destination.onWrite = [&] {
    check(!BridgeTestAccess::clipboardChanged(viewer), "Viewer reentrant clipboard signal echoed to host");
  };
  source.onWrite = [&] {
    check(!BridgeTestAccess::clipboardChanged(host), "Host reentrant clipboard signal echoed to viewer");
  };
  check(BridgeTestAccess::clipboardChanged(host), "Host fresh text failed to publish over authenticated TLS");
  check(
      waitUntil([&] { return destination.writes == 1; }) && destination.text == QStringLiteral("Mac source text"),
      "focused Viewer did not apply exact authenticated text"
  );
  check(
      !BridgeTestAccess::clipboardChanged(viewer) && !source.writes,
      "delayed clipboard notification did not preserve echo suppression"
  );
  destination.text = QStringLiteral("New viewer text — café");
  check(BridgeTestAccess::clipboardChanged(viewer), "Viewer fresh text failed to publish over authenticated TLS");
  check(
      waitUntil([&] { return source.writes == 1; }) && source.text == destination.text,
      "focused native source did not apply the viewer's exact text"
  );

  const auto old = BridgeTestAccess::clipboardFocus(viewer);
  check(old.has_value(), "clipboard stale-focus fixture lost focus early");
  fixture.window->hide();
  check(!old->live() && !BridgeTestAccess::clipboardFocus(viewer), "hidden viewer retained clipboard authority");
  const ClipboardTextMessage stale{old->binding, 2, QStringLiteral("must not replace hidden clipboard")};
  check(
      BridgeTestAccess::channel(host)->send(MessageType::ClipboardText, encodeClipboardText(stale), 91, 2),
      "Cannot submit exact old clipboard focus through TLS"
  );
  // A following focus-independent catalog response proves the previous packet
  // was processed; no quiet interval is used to establish non-delivery.
  BridgeTestAccess::catalog(host, {{124, QStringLiteral("clipboard ordered marker")}});
  check(
      waitUntil([&] {
        return BridgeTestAccess::firstCatalogTitle(viewer) == QStringLiteral("clipboard ordered marker");
      }),
      "ordered clipboard marker was not delivered after the stale packet"
  );
  check(destination.writes == 1, "stale hidden-viewer clipboard update was applied");
  const auto reads = source.reads + destination.reads;
  host.stopSharing();
  check(
      !BridgeTestAccess::clipboardEnabled(host) && !BridgeTestAccess::clipboardChanged(host) &&
          source.reads + destination.reads == reads,
      "terminal sharing shutdown prevents further clipboard reads"
  );
}

void keyboardFenceRoundTrip(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  KeyboardFixture fixture(hostIdentity, viewerIdentity);
  auto &host = fixture.host;
  const auto revoke = std::exchange(host.revokeOriginalKeyboard, {});
  check(!host.requestOriginalKeyboard(91, 2), "Network-only bridge activated original route without local authority");
  host.revokeOriginalKeyboard = revoke;
  fixture.key(QEvent::KeyPress, Qt::Key_Shift);
  check(waitUntil([&] { return nativeHeldKeys.contains(Qt::Key_Shift); }), "Held Shift fixture missing");
  const auto binding = fixture.request();
  check(waitUntil([&] { return fixture.grants == 1; }), "Held keyboard did not quiesce");
  check(nativeHeldKeys.isEmpty(), "Quiescence did not release held Shift");
  const auto keysBefore =
      std::count_if(nativeInputs.begin(), nativeInputs.end(), [](const auto &input) { return input["kind"] == "key"; });
  fixture.key(QEvent::KeyPress, Qt::Key_Shift, true);
  fixture.key(QEvent::KeyPress, Qt::Key_B);
  fixture.key(QEvent::KeyRelease, Qt::Key_B);
  // Even bypassing the viewer's public input path cannot send new network keys
  // into the fake native ledger after the host's independent fence is installed.
  check(
      BridgeTestAccess::channel(fixture.viewer)
          ->send(
              MessageType::Input,
              {{"kind", "key"}, {"code", Qt::Key_C}, {"down", true}, {"geometry", 1}, {"focusRevision", 1}}, 91, 2
          ),
      "Could not send fenced network key fixture"
  );
  fixture.button(true);
  check(waitUntil([&] { return !nativeHeldButtons.isEmpty(); }), "Pointer stopped while keyboard was fenced");
  check(!BridgeTestAccess::keyboardQuiesced(host), "Held pointer was reported as neutral");
  fixture.button(false);
  check(waitUntil([&] { return nativeHeldButtons.isEmpty(); }), "Fenced pointer release missing");
  check(
      nativeHeldKeys.isEmpty() && keysBefore == std::count_if(
                                                    nativeInputs.begin(), nativeInputs.end(),
                                                    [](const auto &input) { return input["kind"] == "key"; }
                                                ),
      "Fenced keyboard forwarded keys or repeats"
  );
  host.withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
  check(waitUntil([&] { return !BridgeTestAccess::keyboardBinding(host); }), "Exact viewer resume did not finish");
  check(fixture.revokes == 1 && !fixture.original, "Cancellation did not revoke original authority exactly once");
  BridgeTestAccess::keyboardReply(
      host, {binding, KeyboardRouteAction::Acknowledge, KeyboardRouteMode::Original, KeyboardRouteResult::Ready}
  );
  check(!BridgeTestAccess::keyboardBinding(host) && fixture.grants == 1, "Late Ready reactivated retired route");
  fixture.key(QEvent::KeyPress, Qt::Key_Shift, true);
  fixture.key(QEvent::KeyPress, Qt::Key_D);
  check(waitUntil([&] { return nativeHeldKeys.contains(Qt::Key_D); }), "Fresh keys did not resume");
  check(!nativeHeldKeys.contains(Qt::Key_Shift), "Pre-handoff held key replayed after resume");
  fixture.key(QEvent::KeyRelease, Qt::Key_Shift);
  fixture.key(QEvent::KeyRelease, Qt::Key_D);
  check(waitUntil([&] { return nativeHeldKeys.isEmpty(); }), "Resumed key release missing");
  const auto next = fixture.request();
  check(next.generation > binding.generation, "Next attempt reused a generation");
  check(waitUntil([&] { return fixture.grants == 2; }), "Next keyboard route did not quiesce");
  BridgeTestAccess::keyboardReply(
      host, {binding, KeyboardRouteAction::Withdraw, KeyboardRouteMode::Original, KeyboardRouteResult::None,
             KeyboardRouteReason::Hidden}
  );
  check(fixture.original && fixture.revokes == 1, "Retired withdrawal revoked the current route");
}

void fullInputFenceRoundTrip(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  KeyboardFixture fixture(hostIdentity, viewerIdentity);
  auto &host = fixture.host;
  check(
      BridgeTestAccess::inputCapable(host) && BridgeTestAccess::inputCapable(fixture.viewer),
      "Full-input capability was not negotiated over TLS"
  );
  fixture.key(QEvent::KeyPress, Qt::Key_Shift);
  check(waitUntil([&] { return nativeHeldKeys.contains(Qt::Key_Shift); }), "Full-input fixture key was not delivered");
  const auto binding = host.reserveOriginalInput(91, 2);
  check(
      binding && binding->scope == ForwardingScope::All && host.startOriginalKeyboard(*binding),
      "Full-input handoff could not start"
  );
  check(waitUntil([&] { return fixture.grants == 1; }), "Full-input handoff did not complete");
  const auto target = host.originalInputTarget(*binding);
  const auto viewerPeer = fixture.viewer.authenticatedPeerObservation();
  check(
      target && viewerPeer && target->viewerConnection == viewerPeer->ticket().incarnation() &&
          target->viewerPeerFingerprint == viewerPeer->ticket().fingerprint(),
      "Full-input target did not bind the actual Viewer endpoint and its Mac peer"
  );
  check(nativeHeldKeys.isEmpty() && nativeHeldButtons.isEmpty(), "Full-input grant preceded owner releases");
  const auto before = nativeInputs.size();
  fixture.key(QEvent::KeyPress, Qt::Key_B);
  fixture.key(QEvent::KeyRelease, Qt::Key_B);
  fixture.button(true);
  const auto point = fixture.window->videoRect().center();
  QMouseEvent move(QEvent::MouseMove, point, point, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(fixture.window, &move);
  QWheelEvent wheel(point, point, {3, -4}, {120, -120}, Qt::LeftButton, Qt::NoModifier, Qt::ScrollUpdate, false);
  QCoreApplication::sendEvent(fixture.window, &wheel);
  auto *channel = BridgeTestAccess::channel(fixture.viewer);
  // Bypass QWidget to exercise the host's independent admission gate through
  // the real encrypted transport. A later catalog request is an ordered marker.
  for (const auto &kind : {"key", "button", "move", "scroll"})
    check(
        channel->send(
            MessageType::Input,
            {{"kind", kind},
             {"code", qstrcmp(kind, "button") == 0 ? 0 : int(Qt::Key_C)},
             {"down", true},
             {"x", 3},
             {"y", 4},
             {"dx", 120},
             {"dy", -120},
             {"geometry", 1},
             {"focusRevision", 1}},
            91, 2
        ),
        "Cannot send fenced peer input fixture"
    );
  const auto refreshes = nativeRefreshes;
  check(channel->send(MessageType::Catalog, {{"refresh", true}}), "Cannot send post-input stream marker");
  check(waitUntil([&] { return nativeRefreshes > refreshes; }), "Fenced input stream did not drain");
  check(
      nativeInputs.size() == before && nativeHeldKeys.isEmpty() && nativeHeldButtons.isEmpty(),
      "Full-input fence leaked viewer or peer input into the host"
  );
  host.withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
  check(waitUntil([&] { return !BridgeTestAccess::keyboardBinding(host); }), "Full-input resume did not complete");
  check(fixture.revokes == 1 && !fixture.original, "Full-input resume preceded original ownership release");
  fixture.button(false);
  fixture.key(QEvent::KeyRelease, Qt::Key_Shift);
  fixture.button(true);
  check(waitUntil([&] { return nativeHeldButtons.contains(0); }), "Fresh pointer down did not resume");
  fixture.button(false);
  check(waitUntil([&] { return nativeHeldButtons.isEmpty(); }), "Resumed pointer lost its exact release owner");
  check(nativeInputs.size() == before + 2, "Full-input resume replayed an old key or button");
}

void fullInputScopeSubstitution(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  KeyboardFixture fixture(hostIdentity, viewerIdentity);
  const auto binding = fixture.host.reserveOriginalInput(91, 2);
  check(binding && fixture.host.startOriginalKeyboard(*binding), "Cannot reserve full-input substitution fixture");
  check(waitUntil([&] { return fixture.grants == 1; }), "Substitution fixture did not quiesce");
  auto partial = *binding;
  partial.scope = ForwardingScope::Keyboard;
  check(
      BridgeTestAccess::channel(fixture.viewer)
          ->send(
              MessageType::KeyboardRoute,
              encodeKeyboardRouteControl(
                  {partial, KeyboardRouteAction::Acknowledge, KeyboardRouteMode::Viewer, KeyboardRouteResult::Ready}
              ),
              91, 2
          ),
      "Cannot send scope-substitution fixture"
  );
  check(
      waitUntil([&] { return !BridgeTestAccess::established(fixture.host); }),
      "Keyboard-only acknowledgment was accepted for a full-input route"
  );
  check(!fixture.original && fixture.revokes == 1, "Mismatched scope preserved original ownership");
}

void fullInputLegacyNegotiation(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  for (bool keyboard : {false, true}) {
    AutomaticNative native;
    Bridge host(true, hostIdentity, parseFingerprint(viewerIdentity.fingerprint()), QString{}, 0, 0, false);
    Channel peer(viewerIdentity, parseFingerprint(hostIdentity.fingerprint()));
    bool accepted = false;
    peer.onAuthenticated = [&] {
      QJsonObject hello{{"version", static_cast<int>(protocolVersion)}, {"role", "viewer"}, {"codec", "h264"}};
      if (keyboard)
        hello["capabilities"] = keyboardRouteCapabilities();
      check(peer.send(MessageType::Hello, hello), "Cannot negotiate legacy route peer");
    };
    peer.onPacket = [&](const Packet &packet) {
      if (packet.type == MessageType::Accepted) {
        const auto object = parseObject(packet);
        check(
            !peerSupportsInputRoute(object) && peerSupportsKeyboardRoute(object) == keyboard,
            "Host advertised full input to a peer that did not offer it"
        );
        accepted = true;
      }
    };
    check(host.listen(QHostAddress::LocalHost, 0), "Cannot listen for legacy route peer");
    peer.connectTo("127.0.0.1", BridgeTestAccess::port(host));
    check(waitUntil([&] { return accepted; }), "Legacy route negotiation did not finish");
    check(!BridgeTestAccess::inputCapable(host), "Legacy peer acquired full-input capability");
    BridgeTestAccess::opened(host, 91, 2);
    host.revokeOriginalKeyboard = [](const auto &, auto) { return true; };
    KeyboardRouteBinding candidate{{91, 2}, 1, 1, 1, 1};
    BridgeTestAccess::observedNativeFocus(host, candidate);
    check(!host.reserveOriginalInput(91, 2), "Full-input route was reserved without negotiation");
    if (keyboard) {
      const auto legacy = host.reserveOriginalKeyboard(91, 2);
      check(legacy && legacy->scope == ForwardingScope::Keyboard, "Legacy keyboard-only reservation regressed");
      host.withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
    }
    candidate.scope = ForwardingScope::All;
    check(
        peer.send(
            MessageType::InputRoute,
            encodeKeyboardRouteControl(
                {candidate, KeyboardRouteAction::Acknowledge, KeyboardRouteMode::Original, KeyboardRouteResult::Ready}
            ),
            91, 2
        ),
        "Cannot send unnegotiated full-input control fixture"
    );
    check(
        waitUntil([&] { return !BridgeTestAccess::established(host); }), "Unnegotiated full-input control was accepted"
    );
  }
}

void keyboardReservation(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  KeyboardFixture fixture(hostIdentity, viewerIdentity);
  const auto peer = fixture.host.authenticatedPeerObservation();
  check(peer && peer->live(), "Established bridge did not expose live authenticated evidence");
  const auto replies = std::make_shared<int>(0);
  const auto send = fixture.window->onKeyboardRouteSend;
  fixture.window->onKeyboardRouteSend = [send, replies](const auto &control) {
    ++*replies;
    return send(control);
  };
  fixture.key(QEvent::KeyPress, Qt::Key_Shift);
  check(waitUntil([&] { return nativeHeldKeys.contains(Qt::Key_Shift); }), "Reservation held-key fixture failed");
  const auto binding = fixture.host.reserveOriginalKeyboard();
  check(binding && binding->owner == Owner{91, 2}, "Current focused candidate was not reserved");
  const auto deadline = BridgeTestAccess::keyboardDeadline(fixture.host);
  check(!fixture.host.reserveOriginalKeyboard(91, 2), "Two keyboard reservations were admitted");
  runEvents(20);
  check(
      *replies == 0 && nativeHeldKeys.contains(Qt::Key_Shift) && !fixture.host.originalKeyboardQuiesced(*binding),
      "Reservation contacted the viewer, released a held key, or claimed quiescence"
  );
  auto wrong = *binding;
  ++wrong.generation;
  check(!fixture.host.startOriginalKeyboard(wrong), "Mismatched reservation started a viewer request");
  check(BridgeTestAccess::keyboardBinding(fixture.host) == binding, "Stale start canceled the actual reservation");
  check(fixture.host.startOriginalKeyboard(*binding), "Exact reservation did not start");
  check(BridgeTestAccess::keyboardDeadline(fixture.host) == deadline, "Start extended the reservation deadline");
  check(!fixture.host.startOriginalKeyboard(*binding), "Repeated start resent an outstanding request");
  check(waitUntil([&] { return fixture.grants == 1; }), "Started reservation never quiesced");
  check(
      fixture.host.originalKeyboardQuiesced(*binding) && !fixture.host.originalKeyboardQuiesced(wrong),
      "Quiescence query did not require the exact attempt"
  );
  nativeHeldKeys.insert(Qt::Key_Z);
  check(!fixture.host.originalKeyboardQuiesced(*binding), "Quiescence ignored globally held Qt keys");
  nativeHeldKeys.clear();
  nativeHeldButtons.insert(13);
  check(!fixture.host.originalKeyboardQuiesced(*binding), "Quiescence ignored globally held Qt buttons");
  nativeHeldButtons.clear();
  check(fixture.host.originalKeyboardQuiesced(*binding), "Neutral exact attempt no longer quiesced");
  fixture.host.withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
  check(!fixture.host.originalKeyboardQuiesced(*binding), "Revocation left a live quiescence query");
  check(waitUntil([&] { return !BridgeTestAccess::keyboardBinding(fixture.host); }), "Viewer resume failed");
  fixture.key(QEvent::KeyRelease, Qt::Key_Shift);
}

void keyboardReservationInvalidation(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  // Revalidate directly observed native state even before its next notification.
  for (const bool geometry : {false, true}) {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    const auto binding = fixture.host.reserveOriginalKeyboard(91, 2);
    check(binding.has_value(), "Native-change reservation failed");
    auto changed = *binding;
    if (geometry)
      ++changed.geometry;
    else
      ++changed.focusRequest;
    BridgeTestAccess::observedNativeFocus(fixture.host, changed);
    check(!fixture.host.startOriginalKeyboard(*binding), "Stale native focus/geometry started a fence");
    check(
        !BridgeTestAccess::keyboardBinding(fixture.host) && fixture.revokes == 1 && fixture.grants == 0,
        "Invalid reservation did not synchronously revoke and retire without a viewer round-trip"
    );
  }
  for (const bool geometry : {false, true}) {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.quiesce();
    const auto binding = *BridgeTestAccess::keyboardBinding(fixture.host);
    auto changed = binding;
    if (geometry)
      ++changed.geometry;
    else
      ++changed.focusRequest;
    BridgeTestAccess::observedNativeFocus(fixture.host, changed);
    check(!fixture.host.originalKeyboardQuiesced(binding), "Quiescence reused stale native focus/geometry");
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    const auto binding = fixture.host.reserveOriginalKeyboard(91, 2);
    const auto peer = fixture.host.authenticatedPeerObservation();
    check(binding && peer, "Disconnect reservation fixture failed");
    BridgeTestAccess::fastReconnect(fixture.viewer);
    const auto generation = BridgeTestAccess::generation(fixture.viewer);
    BridgeTestAccess::channel(fixture.host)->close("Reserved connection lost", CloseCause::Network);
    check(
        !peer->live() && !fixture.host.authenticatedPeerObservation() &&
            !fixture.host.startOriginalKeyboard(*binding) && !BridgeTestAccess::keyboardBinding(fixture.host) &&
            fixture.revokes == 1,
        "Connection loss retained reservation or authenticated observation"
    );
    check(
        waitUntil([&] {
          return BridgeTestAccess::generation(fixture.viewer) > generation &&
                 BridgeTestAccess::catalogReady(fixture.viewer);
        }),
        "Reserved disconnect fixture failed to reconnect"
    );
    const auto replacement = fixture.host.authenticatedPeerObservation();
    check(
        replacement && !replacement->sameConnection(*peer) && !fixture.host.startOriginalKeyboard(*binding) &&
            !fixture.host.reserveOriginalKeyboard(),
        "Reconnect revived an old reservation or replayed its exported window"
    );
  }
}

void keyboardReservationDeadlineAndReentrancy(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  for (const bool slowValidation : {false, true}) {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    BridgeTestAccess::fastKeyboardTimeout(fixture.host);
    const auto binding = fixture.host.reserveOriginalKeyboard(91, 2);
    check(binding.has_value(), "Deadline reservation failed");
    if (slowValidation)
      nativeCandidateObserver = [] { QThread::msleep(70); };
    else
      QThread::msleep(70); // Deliberately beat delivery of the queued timeout.
    check(!fixture.host.startOriginalKeyboard(*binding), "Expired reservation started or extended its deadline");
    nativeCandidateObserver = {};
    check(
        !BridgeTestAccess::keyboardBinding(fixture.host) && fixture.revokes == 1 &&
            fixture.reasons.contains(KeyboardRouteReason::Timeout),
        "Expired reservation did not synchronously revoke before returning"
    );
    BridgeTestAccess::keyboardReply(
        fixture.host,
        {*binding, KeyboardRouteAction::Acknowledge, KeyboardRouteMode::Original, KeyboardRouteResult::Ready}
    );
    check(fixture.grants == 0 && !fixture.host.originalKeyboardQuiesced(*binding), "Late Ready revived expiry");
    const auto next = fixture.host.reserveOriginalKeyboard();
    check(next && next->generation > binding->generation, "New reservation reused an expired generation");
    check(!fixture.host.startOriginalKeyboard(*binding), "Old start consumed a newer reservation");
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    BridgeTestAccess::fastKeyboardTimeout(fixture.host);
    fixture.quiesce();
    const auto binding = *BridgeTestAccess::keyboardBinding(fixture.host);
    QThread::msleep(70);
    check(!fixture.host.originalKeyboardQuiesced(binding), "Quiescence outlived its absolute reservation deadline");
    check(
        waitUntil([&] { return !BridgeTestAccess::keyboardBinding(fixture.host); }) && fixture.revokes == 1 &&
            fixture.reasons.contains(KeyboardRouteReason::Timeout),
        "Abandoned quiesced preflight did not revoke and resume the viewer"
    );
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    bool attempted = false, nested = true;
    nativeCandidateObserver = [&] {
      nativeCandidateObserver = {};
      attempted = true;
      nested = fixture.host.reserveOriginalKeyboard(91, 2).has_value();
    };
    const auto binding = fixture.host.reserveOriginalKeyboard(91, 2);
    check(binding && attempted && !nested, "Reentrant native query allocated two reservations");
    nativeCandidateObserver = [&] {
      nativeCandidateObserver = {};
      fixture.host.withdrawOriginalKeyboard(KeyboardRouteReason::FocusChanged);
    };
    check(!fixture.host.startOriginalKeyboard(*binding), "Start ignored synchronous reservation withdrawal");
    check(!BridgeTestAccess::keyboardBinding(fixture.host) && fixture.revokes == 1, "Reentrant withdrawal leaked");
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    nativeCandidateObserver = [&] {
      nativeCandidateObserver = {};
      BridgeTestAccess::closed(fixture.host, 91);
    };
    check(!fixture.host.reserveOriginalKeyboard(91, 2), "Native query returned a removed host session");
    check(!BridgeTestAccess::keyboardBinding(fixture.host), "Removed session retained a reserved attempt");
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.quiesce();
    const auto binding = *BridgeTestAccess::keyboardBinding(fixture.host);
    nativeCandidateObserver = [&] {
      nativeCandidateObserver = {};
      BridgeTestAccess::channel(fixture.host)->close("Close inside query", CloseCause::Network);
    };
    check(!fixture.host.originalKeyboardQuiesced(binding), "Quiescence query ignored reentrant disconnect");
    check(fixture.revokes == 1 && !fixture.original, "Query disconnect retained owner authority");
  }
}

void keyboardActivation(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    BridgeTestAccess::fastKeyboardTimeout(fixture.host);
    const auto reserved = fixture.host.reserveOriginalKeyboard();
    check(reserved && !fixture.host.activateOriginalKeyboard(*reserved), "Reservation bypassed viewer quiescence");
    check(!fixture.host.originalKeyboardActive(*reserved), "Reserved keyboard appeared active");
    check(fixture.host.startOriginalKeyboard(*reserved), "Activation fixture did not start");
    check(waitUntil([&] { return fixture.grants == 1; }), "Activation fixture did not quiesce");
    auto wrong = *reserved;
    ++wrong.generation;
    check(!fixture.host.activateOriginalKeyboard(wrong), "Wrong binding activated the keyboard");
    check(fixture.host.originalKeyboardQuiesced(*reserved), "Wrong activation consumed the current reservation");
    bool nested = true;
    nativeCandidateObserver = [&] {
      nativeCandidateObserver = {};
      nested = fixture.host.activateOriginalKeyboard(*reserved);
    };
    check(fixture.host.activateOriginalKeyboard(*reserved) && !nested, "Commit was lost or entered twice");
    check(!fixture.host.activateOriginalKeyboard(*reserved), "Active binding committed twice");
    check(!fixture.host.originalKeyboardQuiesced(*reserved), "Active binding retained setup admission");
    check(fixture.host.originalKeyboardActive(*reserved), "Committed binding was not active");
    runEvents(70);
    BridgeTestAccess::deliverKeyboardTimeout(fixture.host);
    check(
        fixture.host.originalKeyboardActive(*reserved) && fixture.revokes == 0,
        "Active ownership expired on its former setup deadline or queued timeout"
    );
    BridgeTestAccess::keyboardReply(
        fixture.host,
        {*reserved, KeyboardRouteAction::Acknowledge, KeyboardRouteMode::Original, KeyboardRouteResult::Ready}
    );
    check(fixture.grants == 1, "Late Ready notified a committed route again");
    fixture.key(QEvent::KeyPress, Qt::Key_B);
    runEvents(10);
    check(nativeHeldKeys.isEmpty(), "Active original owner reopened legacy key forwarding");
    fixture.key(QEvent::KeyRelease, Qt::Key_B);
    fixture.host.withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
    check(!fixture.host.originalKeyboardActive(*reserved) && fixture.revokes == 1, "Active owner did not revoke");
    check(waitUntil([&] { return !BridgeTestAccess::keyboardBinding(fixture.host); }), "Active revoke did not resume");
    fixture.key(QEvent::KeyPress, Qt::Key_C);
    check(waitUntil([&] { return nativeHeldKeys.contains(Qt::Key_C); }), "Legacy keys did not resume after revoke");
    fixture.key(QEvent::KeyRelease, Qt::Key_C);
    check(waitUntil([&] { return nativeHeldKeys.isEmpty(); }), "Resumed key remained held");
  }
  for (const bool slowValidation : {false, true}) {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    BridgeTestAccess::fastKeyboardTimeout(fixture.host);
    fixture.quiesce();
    const auto binding = *BridgeTestAccess::keyboardBinding(fixture.host);
    if (slowValidation)
      nativeCandidateObserver = [] { QThread::msleep(70); };
    else
      QThread::msleep(70);
    check(!fixture.host.activateOriginalKeyboard(binding), "Expired reservation committed");
    nativeCandidateObserver = {};
    check(
        !fixture.host.originalKeyboardActive(binding) && fixture.revokes == 1 &&
            fixture.reasons.contains(KeyboardRouteReason::Timeout),
        "Commit expiry did not release the exact original owner"
    );
  }
  for (const bool duringActivation : {false, true}) {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.quiesce();
    const auto binding = *BridgeTestAccess::keyboardBinding(fixture.host);
    if (!duringActivation)
      check(fixture.host.activateOriginalKeyboard(binding), "Active disconnect fixture failed to commit");
    nativeCandidateObserver = [&] {
      nativeCandidateObserver = {};
      BridgeTestAccess::channel(fixture.host)->close("Connection lost in activation query", CloseCause::Network);
    };
    check(
        !(duringActivation ? fixture.host.activateOriginalKeyboard(binding)
                           : fixture.host.originalKeyboardActive(binding)),
        "Native query reused a disconnected route"
    );
    check(fixture.revokes == 1 && !fixture.original, "Disconnected active owner was not released exactly once");
  }
  for (const bool geometry : {false, true}) {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.quiesce();
    const auto binding = *BridgeTestAccess::keyboardBinding(fixture.host);
    check(fixture.host.activateOriginalKeyboard(binding), "Active native-change fixture failed to commit");
    auto changed = binding;
    if (geometry)
      ++changed.geometry;
    else
      ++changed.focusRequest;
    BridgeTestAccess::observedNativeFocus(fixture.host, changed);
    check(!fixture.host.originalKeyboardActive(binding), "Active query ignored native focus/geometry change");
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.quiesce();
    const auto binding = *BridgeTestAccess::keyboardBinding(fixture.host);
    nativeCandidateObserver = [&] {
      nativeCandidateObserver = {};
      fixture.host.stopSharing();
    };
    check(!fixture.host.activateOriginalKeyboard(binding), "Terminal shutdown committed original ownership");
    check(BridgeTestAccess::stopped(fixture.host) && fixture.revokes == 1, "Shutdown during commit leaked ownership");
  }
}

void keyboardCancellation(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  // Cancellation and Original Ready can cross on the two independent streams.
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.request();
    fixture.host.withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
    check(waitUntil([&] { return !BridgeTestAccess::keyboardBinding(fixture.host); }), "Crossed cancel did not resume");
    check(fixture.grants == 0 && fixture.revokes == 1, "Crossed Original Ready granted authority");
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.button(true);
    check(waitUntil([&] { return !nativeHeldButtons.isEmpty(); }), "Busy pointer fixture missing");
    fixture.request();
    check(
        waitUntil([&] { return !BridgeTestAccess::keyboardBinding(fixture.host); }), "Busy viewer attempt not retired"
    );
    check(fixture.grants == 0 && !nativeHeldButtons.isEmpty(), "Busy handoff released a pointer or granted input");
    fixture.button(false);
    check(waitUntil([&] { return nativeHeldButtons.isEmpty(); }), "Busy pointer release missing");
    fixture.quiesce();
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    BridgeTestAccess::fastKeyboardTimeout(fixture.host);
    const auto send = fixture.window->onKeyboardRouteSend;
    fixture.window->onKeyboardRouteSend = [send](const auto &control) {
      return control.mode == KeyboardRouteMode::Original ? true : send(control);
    };
    fixture.request();
    check(
        waitUntil([&] { return !BridgeTestAccess::keyboardBinding(fixture.host); }),
        "Quiescence timeout did not resume viewer"
    );
    check(
        fixture.grants == 0 && fixture.reasons.contains(KeyboardRouteReason::Timeout) &&
            BridgeTestAccess::established(fixture.host),
        "Quiescence timeout granted input or closed recovered peer"
    );
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.quiesce();
    BridgeTestAccess::fastKeyboardTimeout(fixture.host);
    fixture.window->onKeyboardRouteSend = [](const auto &) { return true; };
    fixture.host.withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
    check(
        waitUntil([&] { return !BridgeTestAccess::established(fixture.host); }), "Resume timeout did not close channel"
    );
    check(!fixture.original && !BridgeTestAccess::keyboardBinding(fixture.host), "Resume timeout retained authority");
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.quiesce();
    fixture.releaseSucceeds = false;
    fixture.host.withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
    check(!BridgeTestAccess::established(fixture.host), "Failed local revocation reopened viewer forwarding");
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.quiesce();
    nativeResumeSucceeds = false;
    fixture.host.withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
    check(
        !BridgeTestAccess::established(fixture.host) && !fixture.original,
        "Failed native resume reopened viewer forwarding"
    );
  }
}

void keyboardInvalidation(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  for (const auto reason :
       {KeyboardRouteReason::FocusChanged, KeyboardRouteReason::GeometryChanged, KeyboardRouteReason::Hidden,
        KeyboardRouteReason::Returned}) {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.quiesce();
    BridgeTestAccess::invalidateKeyboard(fixture.host, reason);
    check(
        waitUntil([&] { return !BridgeTestAccess::keyboardBinding(fixture.host); }),
        "Native invalidation did not resume"
    );
    check(
        fixture.revokes == 1 && fixture.reasons.back() == reason && !fixture.original,
        "Native invalidation did not revoke exact authority"
    );
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.quiesce();
    const auto binding = *BridgeTestAccess::keyboardBinding(fixture.host);
    BridgeTestAccess::fastReconnect(fixture.viewer);
    const auto generation = BridgeTestAccess::generation(fixture.viewer);
    BridgeTestAccess::channel(fixture.host)->close("Keyboard reconnect fixture", CloseCause::Network);
    check(
        !fixture.original && fixture.revokes == 1 && !BridgeTestAccess::keyboardBinding(fixture.host),
        "Disconnect retained original keyboard authority"
    );
    check(
        waitUntil([&] {
          return BridgeTestAccess::generation(fixture.viewer) > generation &&
                 BridgeTestAccess::catalogReady(fixture.viewer);
        }),
        "Keyboard fixture did not reconnect"
    );
    check(!fixture.host.requestOriginalKeyboard(91, 2), "Reconnect replayed keyboard session");
    BridgeTestAccess::keyboardReply(
        fixture.host,
        {binding, KeyboardRouteAction::Acknowledge, KeyboardRouteMode::Original, KeyboardRouteResult::Ready}
    );
    check(!fixture.original && fixture.grants == 1, "Stale pre-reconnect acknowledgement reactivated authority");
  }
  for (bool future : {false, true}) {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    auto binding = fixture.request();
    if (future)
      ++binding.generation;
    else
      ++binding.focusRequest;
    check(
        BridgeTestAccess::channel(fixture.viewer)
            ->send(
                MessageType::KeyboardRoute,
                encodeKeyboardRouteControl(
                    {binding, KeyboardRouteAction::Acknowledge, KeyboardRouteMode::Original, KeyboardRouteResult::Ready}
                ),
                91, 2
            ),
        "Invalid reply fixture send failed"
    );
    check(
        waitUntil([&] { return !BridgeTestAccess::established(fixture.host); }),
        "Mismatched keyboard reply was accepted"
    );
    check(!fixture.original, "Mismatched reply retained authority");
  }
}

void keyboardReentrancyAndDeadlines(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  for (bool succeeds : {true, false}) {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.quiesce();
    int resumes = 0;
    nativeResumeObserver = [&] { ++resumes; };
    fixture.host.revokeOriginalKeyboard = [&](const auto &binding, auto) {
      fixture.original = false;
      // This acknowledgement has not been requested yet and cannot retire an
      // attempt while its synchronous revocation callback is still running.
      BridgeTestAccess::keyboardReply(
          fixture.host,
          {binding, KeyboardRouteAction::Acknowledge, KeyboardRouteMode::Viewer, KeyboardRouteResult::Ready}
      );
      check(BridgeTestAccess::keyboardBinding(fixture.host) == binding, "Reentrant reply retired revoking attempt");
      BridgeTestAccess::channel(fixture.host)->close("Close inside revocation", CloseCause::Network);
      check(BridgeTestAccess::keyboardBinding(fixture.host) == binding, "Nested disconnect retired revoking attempt");
      QCoreApplication::processEvents();
      return succeeds;
    };
    fixture.host.withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
    check(
        !BridgeTestAccess::keyboardBinding(fixture.host) && !BridgeTestAccess::established(fixture.host),
        "Nested revoke did not complete cleanup"
    );
    check(resumes == (succeeds ? 1 : 0), "Nested disconnect stranded or prematurely reopened the native gate");
  }
  // A readyRead callback can beat an overdue QTimer. Observe elapsed time at
  // admission, including after a synchronous native focus/geometry query.
  for (bool stallDuringNative : {false, true}) {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    BridgeTestAccess::fastKeyboardTimeout(fixture.host);
    const auto send = fixture.window->onKeyboardRouteSend;
    bool delayed = false;
    if (stallDuringNative)
      nativeQuiesceObserver = [] { QThread::msleep(70); };
    else
      fixture.window->onKeyboardRouteSend = [&](const auto &control) {
        if (control.mode == KeyboardRouteMode::Original && control.action == KeyboardRouteAction::Acknowledge) {
          QThread::msleep(70);
          delayed = true;
          BridgeTestAccess::keyboardReply(fixture.host, control);
          return true;
        }
        return send(control);
      };
    fixture.request();
    check(
        waitUntil([&] { return !BridgeTestAccess::keyboardBinding(fixture.host); }),
        "Expired Ready did not recover viewer"
    );
    check(
        (stallDuringNative || delayed) && fixture.grants == 0 && fixture.reasons.contains(KeyboardRouteReason::Timeout),
        "Expired acknowledgement granted original authority"
    );
    nativeQuiesceObserver = {};
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    const auto binding = fixture.request();
    // Feed an exact Ready synchronously, then invalidate before its queued
    // controller notification. No original authority may escape that boundary.
    BridgeTestAccess::keyboardReply(
        fixture.host,
        {binding, KeyboardRouteAction::Acknowledge, KeyboardRouteMode::Original, KeyboardRouteResult::Ready}
    );
    fixture.host.withdrawOriginalKeyboard(KeyboardRouteReason::GeometryChanged);
    check(
        waitUntil([&] { return !BridgeTestAccess::keyboardBinding(fixture.host); }),
        "Queued grant cancellation did not retire"
    );
    check(fixture.grants == 0, "Queued quiescence callback ignored intervening invalidation");
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    fixture.host.onKeyboardQuiesced = [](const auto &) { throw std::runtime_error("Local controller failure"); };
    fixture.request();
    check(
        waitUntil([&] { return !BridgeTestAccess::keyboardBinding(fixture.host); }),
        "Throwing controller did not revoke"
    );
    check(fixture.reasons.contains(KeyboardRouteReason::Failed), "Controller failure lost revocation reason");
  }
  {
    KeyboardFixture fixture(hostIdentity, viewerIdentity);
    int notifications = 0;
    std::optional<KeyboardRouteBinding> replacement;
    fixture.host.onKeyboardQuiesced = [&](const auto &) {
      if (++notifications == 1) {
        fixture.host.withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
        check(
            waitUntil([&] { return !BridgeTestAccess::keyboardBinding(fixture.host); }), "Nested resume did not finish"
        );
        replacement = fixture.request();
        throw std::runtime_error("Old local callback failed after replacement");
      }
      fixture.original = true;
      ++fixture.grants;
    };
    fixture.request();
    check(waitUntil([&] { return fixture.grants == 1; }), "Old callback failure revoked replacement route");
    check(
        replacement == BridgeTestAccess::keyboardBinding(fixture.host) && fixture.revokes == 1,
        "Old callback exception changed the replacement binding"
    );
  }
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

void shutdownBeforeExec()
{
  QProcess process;
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("QT_QPA_PLATFORM", "offscreen");
  process.setProcessEnvironment(environment);
  process.start(QCoreApplication::applicationFilePath(), {"--shutdown-before-exec"});
  check(process.waitForStarted(3000), "Cannot start pre-exec shutdown fixture");
  check(process.waitForFinished(5000), "Pre-exec shutdown was lost before QApplication::exec");
  check(
      process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
      "Pre-exec shutdown did not deliver exactly one queued quit"
  );
}

void startupShutdownLifetime(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  AutomaticNative native;
  int quits = 0;
  {
    HostShutdownGuard shutdown([&] { ++quits; });
    shutdown.request();
    shutdown.request();
    check(shutdown.requested() && quits == 0, "Startup closure was not latched before queued quit");
    // main consults this latch before constructing NativeHost or listening.
  }
  runEvents(10);
  check(quits == 0, "Destroyed shutdown guard left a queued callback");

  {
    HostShutdownGuard shutdown([&] { ++quits; });
    const auto releases = nativeReleases, restores = nativeRestores;
    nativeConstructObserver = [&] { shutdown.request(); };
    Bridge host(true, hostIdentity, parseFingerprint(viewerIdentity.fingerprint()), QString{}, 0, 0, false);
    nativeConstructObserver = {};
    check(shutdown.requested(), "Closure during native construction was lost");
    shutdown.attach(host);
    check(
        BridgeTestAccess::stopped(host) && nativeReleases == releases + 1 && nativeRestores == restores + 1,
        "Attaching an already canceled startup did not restore exactly once"
    );
    check(!host.listen(QHostAddress::LocalHost, 0), "Canceled startup opened a listener");
    check(quits == 0, "Startup quit ran before its queued delivery");
    runEvents(10);
    check(quits == 1, "Canceled startup did not quit");
    shutdown.request();
    host.stopSharing();
    check(nativeRestores == restores + 1, "Repeated startup closure restored twice");
  }

  {
    HostShutdownGuard shutdown([&] { ++quits; });
    const auto restores = nativeRestores;
    {
      Bridge host(true, hostIdentity, parseFingerprint(viewerIdentity.fingerprint()), QString{}, 0, 0, false);
      shutdown.attach(host);
    }
    check(nativeRestores == restores + 1, "Destructor fallback did not restore");
    // main's private channel outlives the stack Bridge. Its final close must
    // neither call the destroyed controller nor leave an unguarded callback.
    shutdown.request();
    runEvents(10);
    check(quits == 2 && nativeRestores == restores + 1, "Late private closure used a destroyed Bridge");
  }
}

void terminalShutdownOrdering(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  // Cover a direct shutdown and one nested inside an already running revoke.
  for (const bool nested : {false, true}) {
    const auto restoresBefore = nativeRestores;
    {
      KeyboardFixture fixture(hostIdentity, viewerIdentity);
      fixture.quiesce();
      auto &host = fixture.host;
      const auto proof = host.authenticatedPeerObservation();
      check(proof && proof->live(), "Shutdown fixture has no authenticated proof");
      const auto stalePacket = BridgeTestAccess::channel(host)->onPacket;
      const auto staleClose = BridgeTestAccess::channel(host)->onClosed;
      const auto exported = exportedWindows.size(), inputs = nativeInputs.size();
      const auto releases = nativeReleases, restores = nativeRestores;
      QVector<QString> order;
      int quits = 0;
      HostShutdownGuard shutdown([&] {
        check(BridgeTestAccess::stopped(host), "Quit preceded terminal cleanup");
        order.push_back("quit");
        ++quits;
      });
      shutdown.attach(host);
      nativeReleaseObserver = [&] {
        check(!fixture.original, "Native release preceded original authority revocation");
        check(!BridgeTestAccess::channel(host) && !proof->live(), "Native release preceded transport revocation");
        order.push_back("release");
        host.stopSharing();
      };
      nativeRestoreObserver = [&] {
        check(order.last() == "release", "Restore preceded native release");
        order.push_back("restore");
        shutdown.request();
      };
      host.revokeOriginalKeyboard = [&](const auto &, auto) {
        ++fixture.revokes;
        order.push_back("revoke");
        if (nested)
          shutdown.request();
        check(BridgeTestAccess::stopping(host), "Revocation preceded terminal admission fence");
        check(!BridgeTestAccess::listening(host), "Listener remained open during terminal revoke");
        check(!host.authenticatedPeerObservation(), "Terminal controller still exposed authority");
        check(!host.listen(QHostAddress::LocalHost, 0), "Reentrant listen reopened terminal controller");
        host.stopSharing();
        stalePacket(Packet{MessageType::Export, 0, 0, 1, jsonBytes({{"windowId", 555}})});
        stalePacket(
            Packet{
                MessageType::Input, 91, 2, 2,
                jsonBytes({{"kind", "key"}, {"code", Qt::Key_A}, {"down", true}, {"geometry", 1}, {"focusRevision", 1}})
            }
        );
        staleClose("Obsolete network callback", CloseCause::Network);
        check(exportedWindows.size() == exported && nativeInputs.size() == inputs, "Late packet passed stop fence");
        runEvents(10);
        check(
            quits == 0 && nativeReleases == releases && nativeRestores == restores,
            "Nested event loop allowed cleanup or quit before revoke returned"
        );
        fixture.original = false;
        return true;
      };
      if (nested)
        host.withdrawOriginalKeyboard(KeyboardRouteReason::FocusChanged);
      else
        shutdown.request();
      check(
          BridgeTestAccess::stopped(host) && fixture.revokes == 1 && !BridgeTestAccess::keyboardBinding(host) &&
              nativeReleases == releases + 1 && nativeRestores == restores + 1 &&
              order == QVector<QString>{"revoke", "release", "restore"},
          "Terminal cleanup did not execute once in authority/transport/release/restore order"
      );
      host.stopSharing();
      shutdown.request();
      stalePacket(Packet{MessageType::Export, 0, 0, 3, jsonBytes({{"windowId", 556}})});
      staleClose("Late close after restore", CloseCause::Network);
      runEvents(10);
      check(quits == 1 && order.last() == "quit", "Terminal shutdown quit was not one-shot");
      check(nativeRestores == restores + 1 && exportedWindows.size() == exported, "Late callback restarted sharing");
      nativeReleaseObserver = {};
      nativeRestoreObserver = {};
    }
    // Both fixture controllers are destroyed here. Only the host has native
    // cleanup, and its destructor must not repeat the explicit stop.
    check(nativeRestores == restoresBefore + 1, "Destructor repeated completed terminal restoration");
  }
}

void captureRateConfiguration()
{
  const auto option = [](const QStringList &arguments) {
    QCommandLineParser parser;
    parser.addOption({"capture-fps", "Capture rate", "fps", "30"});
    check(parser.parse(QStringList{"fixture"} + arguments), "Capture rate fixture arguments did not parse");
    return captureRateOption(parser);
  };
  check(
      option({}) == 30 && option({"--capture-fps", "30"}) == 30 && option({"--capture-fps=60"}) == 60,
      "CLI capture rate lost default or explicit setting"
  );
  for (const auto &arguments : QList<QStringList>{
           {"--capture-fps=0"},
           {"--capture-fps=45"},
           {"--capture-fps=060"},
           {"--capture-fps=+60"},
           {"--capture-fps=60.0"},
           {"--capture-fps=30", "--capture-fps=60"}
       }) {
    bool denied = false;
    try {
      (void)option(arguments);
    } catch (const std::exception &) {
      denied = true;
    }
    check(denied, "Malformed or duplicate capture rate was accepted");
  }
  for (const auto rate : {30U, 60U}) {
    Bridge host(true, Identity{QSslCertificate{}, QSslKey{}}, QByteArray(32, '\0'), {}, 0, 0, false, false, rate);
    check(BridgeTestAccess::captureRate(host) == rate, "Bridge did not apply the selected native capture rate");
  }
}
void shutdownSupersedesDeferredCleanup(const Identity &hostIdentity, const Identity &viewerIdentity)
{
  KeyboardFixture fixture(hostIdentity, viewerIdentity);
  const auto releases = nativeReleases, restores = nativeRestores;
  BridgeTestAccess::channel(fixture.host)->close("Pending ordinary disconnect", CloseCause::Network);
  check(BridgeTestAccess::cleaning(fixture.host), "Fixture did not queue ordinary disconnect cleanup");
  fixture.host.stopSharing();
  check(nativeReleases == releases + 1 && nativeRestores == restores + 1, "Terminal cleanup was deferred");
  runEvents(20);
  check(
      nativeReleases == releases + 1 && nativeRestores == restores + 1 && !BridgeTestAccess::listening(fixture.host) &&
          !BridgeTestAccess::reconnectWaiting(fixture.host),
      "Queued disconnect cleanup repeated restoration or reopened admission"
  );
}
} // namespace

int main(int argc, char **argv)
{
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  try {
    const auto arguments = application.arguments();
    if (arguments.size() == 2 && arguments[1] == "--shutdown-before-exec") {
      int quits = 0;
      HostShutdownGuard shutdown([&] {
        ++quits;
        application.quit();
      });
      shutdown.request();
      shutdown.request();
      check(shutdown.requested() && quits == 0, "Pre-exec stop was not deferred");
      QTimer::singleShot(1500, &application, [&] { application.exit(9); });
      const int result = application.exec();
      check(result == 0 && quits == 1, "Pre-exec quit was lost or repeated");
      return 0;
    }
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
    paintedReadyBinding();
    captureRateConfiguration();
    QTemporaryDir directory;
    check(directory.isValid(), "Cannot create TLS identity fixture directory");
    initializeIdentity(directory.filePath("host"));
    initializeIdentity(directory.filePath("viewer"));
    const auto hostIdentity = Identity::load(directory.filePath("host"));
    const auto viewerIdentity = Identity::load(directory.filePath("viewer"));
    shutdownBeforeExec();
    startupShutdownLifetime(hostIdentity, viewerIdentity);
    initialExportWaitsForCatalog(hostIdentity, viewerIdentity);
    focusedClipboardPolicies(hostIdentity, viewerIdentity);
    focusedClipboardRoundTrip(hostIdentity, viewerIdentity);
    congestionRecovery(hostIdentity, viewerIdentity);
    negotiatedMediaRecovery(hostIdentity, viewerIdentity);
    reconnectRefusedThenAvailable(hostIdentity, viewerIdentity);
    reconnectCleanupAndIsolation(hostIdentity, viewerIdentity);
    keyboardFenceRoundTrip(hostIdentity, viewerIdentity);
    fullInputFenceRoundTrip(hostIdentity, viewerIdentity);
    fullInputScopeSubstitution(hostIdentity, viewerIdentity);
    fullInputLegacyNegotiation(hostIdentity, viewerIdentity);
    keyboardReservation(hostIdentity, viewerIdentity);
    keyboardReservationInvalidation(hostIdentity, viewerIdentity);
    keyboardReservationDeadlineAndReentrancy(hostIdentity, viewerIdentity);
    keyboardActivation(hostIdentity, viewerIdentity);
    keyboardCancellation(hostIdentity, viewerIdentity);
    keyboardInvalidation(hostIdentity, viewerIdentity);
    keyboardReentrancyAndDeadlines(hostIdentity, viewerIdentity);
    terminalReconnectFailures(hostIdentity, viewerIdentity);
    reconnectWaitsForHostCleanup(hostIdentity, viewerIdentity);
    cancelRetryAndDestroy(hostIdentity, viewerIdentity);
    terminalShutdownOrdering(hostIdentity, viewerIdentity);
    shutdownSupersedesDeferredCleanup(hostIdentity, viewerIdentity);
    QTextStream(
        stdout
    ) << "PASS: bounded catalog/title wire data, delayed viewer cleanup with reused incarnation, "
         "catalog completion, congestion and negotiated decoder recovery, media replay/geometry bounds, "
         "reconnect backoff/cleanup/isolation, terminal "
         "failures, cancellation, keyboard/full-input fencing and capability isolation, panel lifetime and terminal "
         "private-channel shutdown ordering\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
