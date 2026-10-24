/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "CaptureRate.h"
#include "ClipboardText.h"
#include "KeyboardRouteProtocol.h"
#include "LifecycleDiagnostics.h"
#include "LocalInputMapping.h"
#include "LocalWindowDrag.h"
#include "NativeApplicationStartup.h"
#if defined(__APPLE__) && !defined(WINDOWSHARE_BRIDGE_TEST)
#include "RecoveryWatchdog.h"
#endif
#include "NativeHost.h"
#include "Transport.h"
#ifndef WINDOWSHARE_BRIDGE_TEST
#include "SessionControls.h"
#endif
#include "Viewer.h"
#include "WindowDragPreviewController.h"
#include "WindowDragPreviewProtocol.h"
#include "WindowDragWatcher.h"
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
#include "ViewerViewportEndpoint.h"
#endif
#if defined(WINDOWSHARE_LOCAL_INPUT) && !defined(WINDOWSHARE_BRIDGE_TEST)
#include "InputEvidenceRuntime.h"
#include "LocalInputBootstrap.h"
#include "LocalInputCodeAdmission.h"
#include "LocalInputSession.h"
#include "LocalWindowDragCoordinator.h"
#endif
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QLockFile>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <csignal>
#include <map>
#include <memory>

using namespace deskflow::windowshare;
namespace {
#ifndef WINDOWSHARE_BRIDGE_TEST
volatile std::sig_atomic_t interrupted = 0;
void interruptHandler(int)
{
  interrupted = 1;
}
#endif
void require(bool value, const char *error)
{
  if (!value)
    throw std::runtime_error(error);
}
unsigned captureRateOption(const QCommandLineParser &parser)
{
  const auto values = parser.values("capture-fps");
  require(values.size() == 1, "Use --capture-fps once with exactly 30 or 60");
  const auto rate = parseCaptureRate(values.front().toStdString());
  require(rate.has_value(), "Use --capture-fps once with exactly 30 or 60");
  return *rate;
}
std::uint64_t integer(const QJsonObject &object, const char *name, std::uint64_t maximum = 9007199254740991ULL)
{
  const auto value = object.value(QLatin1String(name));
  require(value.isDouble(), "Missing integer field");
  const double number = value.toDouble();
  require(
      std::isfinite(number) && number >= 0 && number <= static_cast<double>(maximum) && std::floor(number) == number,
      "Invalid integer field"
  );
  return static_cast<std::uint64_t>(number);
}
QJsonValue number(std::uint64_t value)
{
  return QJsonValue(static_cast<qint64>(value));
}
constexpr qsizetype maxCatalogWindows = 512;
QString displayTitle(QString title)
{
  title = title.left(512);
  title.remove(QChar('\0'));
  return title;
}
QJsonObject windowMetadataForWire(QJsonObject metadata)
{
  // Preserve NativeHost's exact identity/title for AX matching and recovery.
  metadata.insert("title", displayTitle(metadata.value("title").toString()));
  return metadata;
}
QJsonArray catalogForWire(const QVector<Candidate> &windows)
{
  QJsonArray catalog;
  qsizetype bytes = QJsonDocument(QJsonObject{{"windows", catalog}}).toJson(QJsonDocument::Compact).size();
  for (const auto &window : windows) {
    if (catalog.size() == maxCatalogWindows)
      break;
    const QJsonObject item{{"id", static_cast<qint64>(window.id)}, {"title", displayTitle(window.title)}};
    const auto itemBytes = QJsonDocument(item).toJson(QJsonDocument::Compact).size() + (catalog.isEmpty() ? 0 : 1);
    if (bytes + itemBytes >= maxControlPayload)
      break;
    bytes += itemBytes;
    catalog.append(item);
  }
  return catalog;
}

class Listener final : public QTcpServer
{
public:
  std::function<void(qintptr)> incoming;

private:
  void incomingConnection(qintptr descriptor) override
  {
    if (incoming)
      incoming(descriptor);
  }
};

class Bridge final : public QWidget
{
  friend struct BridgeTestAccess;
  friend class HostShutdownGuard;

public:
  Bridge(
      bool hosting, Identity identity, QByteArray pin, QString journal, int screen, std::uint32_t initialWindow,
      bool holding, bool clipboardText = false, unsigned captureRate = defaultCaptureRate
  )
      : m_hosting(hosting),
        m_clipboardPolicy(clipboardText),
        m_identity(std::move(identity)),
        m_pin(std::move(pin)),
        m_screen(screen),
        m_initialWindow(initialWindow)
  {
    require(validCaptureRate(captureRate), "Capture frame rate must be 30 or 60");
    setWindowTitle(hosting ? "Deskflow Window Share — Mac host" : "Deskflow Window Share — viewer");
    auto *layout = new QVBoxLayout(this);
    auto *explanation = new QLabel(
        hosting
            ? "Share selected app windows with your paired computer.\nEmergency restore: Control–Option–Shift–Escape."
            : "Select a Mac window to open here. Closing its viewer returns it to the Mac.",
        this
    );
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    m_status = new QLabel("Disconnected", this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    m_catalog = new QListWidget(this);
    layout->addWidget(m_catalog);
    m_open = new QPushButton(hosting ? "Share selected window" : "Open selected window", this);
    m_refresh = new QPushButton("Refresh windows", this);
    auto *restore = new QPushButton("Return all windows to Mac", this);
    layout->addWidget(m_open);
    layout->addWidget(m_refresh);
    layout->addWidget(restore);
    if (!hosting) {
      m_retry = new QPushButton("Retry connection", this);
      m_cancel = new QPushButton("Cancel connection", this);
      layout->addWidget(m_retry);
      layout->addWidget(m_cancel);
      connect(m_retry, &QPushButton::clicked, this, [this] { retryConnection(); });
      connect(m_cancel, &QPushButton::clicked, this, [this] { cancelConnection(); });
    }
    resize(480, 440);
    connect(m_open, &QPushButton::clicked, this, [this] { openSelected(); });
    connect(m_catalog, &QListWidget::itemDoubleClicked, this, [this] { openSelected(); });
    connect(m_refresh, &QPushButton::clicked, this, [this] {
      if (m_shuttingDown)
        return;
      if (m_native)
        m_native->refresh();
      else
        send(MessageType::Catalog, {{"refresh", true}});
    });
    connect(restore, &QPushButton::clicked, this, [this] { returnAll(); });
    m_keyframeRetry.setInterval(25);
    connect(&m_keyframeRetry, &QTimer::timeout, this, [this] { retryKeyframe(); });
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] { startConnection(); });
    m_stableTimer.setSingleShot(true);
    connect(&m_stableTimer, &QTimer::timeout, this, [this] {
      if (m_established && m_catalogReady && !m_shuttingDown)
        m_retryStep = 0;
    });
    m_keyboardTimeout.setSingleShot(true);
    connect(&m_keyboardTimeout, &QTimer::timeout, this, [this] {
      // A setup timeout already queued before commit must not revoke a route
      // whose lifetime is now owned by the private channel's active checks.
      if (!m_keyboardRoute || m_keyboardRoute->phase == KeyboardPhase::Active)
        return;
      if (m_keyboardRoute && m_keyboardRoute->phase == KeyboardPhase::AwaitViewer) {
        if (m_channel)
          m_channel->close("Keyboard handoff resume timed out");
      } else
        withdrawOriginalKeyboard(KeyboardRouteReason::Timeout);
    });
    if (m_hosting) {
      m_native = std::make_unique<NativeHost>(std::move(journal));
      m_native->setHoldingDisplayEnabled(holding);
      require(m_native->setCaptureFrameRate(captureRate), "Cannot configure native capture frame rate");
      m_native->onError = [this](QString error) {
        status(error);
        send(MessageType::Error, {{"message", error.left(2048)}});
      };
      m_native->onCatalog = [this](const QVector<Candidate> &windows) {
        if (m_shuttingDown)
          return;
        const auto catalog = catalogForWire(windows);
        m_lastCatalog = catalog;
        m_catalogReady = true;
        displayCatalog(catalog);
        updateControls();
        if (m_established)
          send(MessageType::Catalog, {{"windows", catalog}});
        maybeInitialExport();
      };
      m_native->onOpened = [this](std::uint64_t session, std::uint64_t epoch, const QJsonObject &metadata) {
        if (m_shuttingDown)
          return;
        if (m_keyboardRoute && m_keyboardRoute->binding.owner == Owner{session, epoch} &&
            m_keyboardRoute->binding.geometry != integer(metadata, "geometry"))
          withdrawOriginalKeyboard(KeyboardRouteReason::GeometryChanged);
        if (m_shuttingDown)
          return;
        m_hostSessions[session] = epoch;
        if (m_hostGeometries.contains(session) && m_hostGeometries.at(session) != integer(metadata, "geometry"))
          m_hostMoveGates[session].cancel();
        m_hostGeometries[session] = integer(metadata, "geometry");
        m_hostMetadata[session] = metadata;
        if (metadata.value("windowDrag").toDouble(0) > 0)
          m_dragAnchors[session] =
              QPointF(metadata.value("dragAnchorX").toDouble(-1), metadata.value("dragAnchorY").toDouble(-1));
        if (!send(MessageType::Opened, windowMetadataForWire(metadata), session, epoch))
          QTimer::singleShot(0, this, [this, session] {
            if (!m_shuttingDown)
              m_native->restore(session);
          });
      };
      m_native->onFrame =
          [this](std::uint64_t session, std::uint64_t epoch, std::uint64_t geometry, const QByteArray &frame) {
            const QPointer<Bridge> alive(this);
            const auto found = m_hostSessions.find(session);
            if (!m_channel || !m_established || found == m_hostSessions.end() || found->second != epoch)
              return;
            if (m_returnDrag && m_returnDrag->owner == Owner{session, epoch} && m_returnDrag->preview)
              m_returnDrag->preview->receiveFrame(frame, geometry);
            if (!alive || m_shuttingDown || !m_channel || !m_established)
              return;
            // Once an access unit is dropped, no dependent frame may pass until
            // a deferred keyframe request invalidates the old GOP. Polling the
            // drained socket avoids an encode/drop/re-encode loop on a slow peer.
            if (m_pendingKeyframes.count(session))
              return;
            if (!m_channel->sendFrame(session, epoch, geometry, frame) && m_channel && m_established) {
              m_pendingKeyframes[session] = epoch;
              if (!m_keyframeRetry.isActive())
                m_keyframeRetry.start();
            }
          };
      m_native->onCommitted = [this](std::uint64_t session, std::uint64_t epoch) {
        const QPointer<Bridge> alive(this);
        const auto drag = m_hostDrag && m_hostDrag->owner == Owner{session, epoch} ? std::optional(m_hostDrag->binding)
                                                                                   : std::nullopt;
        if (send(MessageType::Accepted, {{"kind", "committed"}}, session, epoch) && alive && drag) {
          const auto callback = onWindowDragCommitted;
          if (callback)
            callback(*drag);
        }
      };
      m_native->onFocused = [this](
                                std::uint64_t session, std::uint64_t epoch, std::uint64_t revision,
                                std::uint64_t request
                            ) {
        if (m_keyboardRoute && m_keyboardRoute->binding.owner == Owner{session, epoch} &&
            (m_keyboardRoute->binding.focusRevision != revision || m_keyboardRoute->binding.focusRequest != request))
          withdrawOriginalKeyboard(KeyboardRouteReason::FocusChanged);
        send(MessageType::Focus, {{"focusRevision", number(revision)}, {"request", number(request)}}, session, epoch);
      };
      m_native->onKeyboardRouteInvalidated = [this](KeyboardRouteBinding binding, KeyboardRouteReason reason) {
        if (m_keyboardRoute && m_keyboardRoute->binding == binding)
          withdrawOriginalKeyboard(reason);
      };
      m_native->onClosed = [this](std::uint64_t session) {
        const QPointer<Bridge> alive(this);
        if (m_keyboardRoute && m_keyboardRoute->binding.owner.session == session)
          withdrawOriginalKeyboard(KeyboardRouteReason::Returned, false);
        m_pendingKeyframes.erase(session);
        m_hostGeometries.erase(session);
        m_hostMetadata.erase(session);
        m_nativeMoves.erase(session);
        if (m_returnDrag && m_returnDrag->owner.session == session && !m_returnDrag->committing) {
          const auto binding = m_returnDrag->binding;
          cancelWindowReturn(binding);
          const auto callback = onWindowDragCancelled;
          if (callback)
            callback(binding);
          if (!alive)
            return;
        }
        m_hostMoveGates.erase(session);
        m_dragAnchors.erase(session);
        const auto drag =
            m_hostDrag && m_hostDrag->owner.session == session ? std::exchange(m_hostDrag, {}) : std::nullopt;
        m_mediaRecoveryRequests.erase(session);
        if (m_pendingKeyframes.empty())
          m_keyframeRetry.stop();
        const auto found = m_hostSessions.find(session);
        if (found != m_hostSessions.end()) {
          const auto epoch = found->second;
          m_hostSessions.erase(found);
          send(MessageType::Closed, {}, session, epoch);
        }
        if (!alive)
          return;
        const auto callback = onWindowDragCancelled;
        if (drag && callback)
          callback(drag->binding);
      };
      m_native->refresh();
    }
    initializeClipboard();
    updateControls();
  }
  std::function<void()> onQuitRequested;
  bool nativeWindowDragAvailable() const
  {
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
    return m_viewportEndpoint && m_viewportEndpoint->live() && QGuiApplication::platformName().startsWith("wayland");
#else
    return false;
#endif
  }
  void observeWindowPointer(const LocalWindowPointer &pointer)
  {
    if (m_native && !m_shuttingDown)
      m_native->observeWindowPointer(pointer);
  }
  bool prepareWindowDrag(const WindowDragBinding &binding)
  {
    const auto peer = authenticatedPeerObservation();
    if (!m_native || !m_windowDragCapable || !peer || peer->ticket().incarnation() != binding.connection)
      return false;
    return m_native->prepareWindowDrag(binding).has_value();
  }
  bool prepareWindowDrag(const WindowDragBinding &binding, const LocalWindowDrag &command)
  {
    if (binding.direction == WindowDragDirection::Return)
      return prepareWindowReturn(binding, command);
    const auto peer = authenticatedPeerObservation();
    if (!m_native || !m_windowDragMotionCapable || !peer || peer->ticket().incarnation() != binding.connection ||
        m_hostDrag)
      return false;
    const auto owner = m_native->prepareWindowDrag(binding);
    if (!owner)
      return false;
    if (!m_dragAnchors.contains(owner->session) || !m_hostGeometries.contains(owner->session)) {
      m_native->cancelWindowDrag(binding);
      return false;
    }
    m_hostDrag = HostDrag{binding, *owner, {}, {}, {}, false};
    moveWindowDrag(binding, command);
    return m_hostDrag.has_value();
  }
  std::function<void(const WindowDragBinding &, const WindowDragPreviewReply &)> onWindowDragPreview;
  std::function<void(std::uint32_t, Owner, const WindowDragMove &)> onWindowMove;
  std::function<void(const WindowDragBinding &)> onWindowDragCommitted, onWindowDragCancelled;
  bool
  dropWindowDrag(const WindowDragBinding &binding, const LocalWindowDrag &command, const WindowDragRelease &release)
  {
    if (binding.direction == WindowDragDirection::Return) {
      if (!m_returnDrag || m_returnDrag->binding != binding || m_returnDrag->drop ||
          command.kind != LocalWindowDrag::Kind::Drop || release.sequence != command.releaseSequence)
        return false;
      m_returnDrag->command = command;
      m_returnDrag->drop = true;
      return true;
    }
    if (!m_hostDrag || m_hostDrag->binding != binding || m_hostDrag->release ||
        command.kind != LocalWindowDrag::Kind::Drop || !m_windowDragMotionCapable)
      return false;
    m_hostDrag->release = release;
    const QPointer<Bridge> alive(this);
    moveWindowDrag(binding, command);
    return alive && m_hostDrag && m_hostDrag->binding == binding;
  }
  void finishWindowDrag(const WindowDragBinding &binding)
  {
    if (binding.direction == WindowDragDirection::Return)
      return cancelWindowReturn(binding);
    if (m_hostDrag && m_hostDrag->binding == binding)
      m_hostDrag.reset();
  }
  void moveWindowDrag(const WindowDragBinding &binding, const LocalWindowDrag &command)
  {
    if (binding.direction == WindowDragDirection::Return) {
      if (m_returnDrag && m_returnDrag->binding == binding && !m_returnDrag->drop) {
        m_returnDrag->command = command;
        placeWindowReturn();
      }
      return;
    }
    if (!m_hostDrag || m_hostDrag->binding != binding || !m_windowDragMotionCapable)
      return;
    const auto owner = m_hostDrag->owner;
    const auto anchor = m_dragAnchors.find(owner.session);
    const auto geometry = m_hostGeometries.find(owner.session);
    if (anchor == m_dragAnchors.end() || geometry == m_hostGeometries.end())
      return cancelWindowDrag(binding);
    WindowDragPreviewCommand move{
        command.kind == LocalWindowDrag::Kind::Drop   ? WindowDragPreviewCommand::Kind::Drop
        : command.kind == LocalWindowDrag::Kind::Hold ? WindowDragPreviewCommand::Kind::Hold
                                                      : WindowDragPreviewCommand::Kind::Move,
        binding.drag,
        command.sequence,
        geometry->second,
        command.destinationX,
        command.destinationY,
        anchor->second.x(),
        anchor->second.y()
    };
    m_hostDrag->command = move;
    if (!send(MessageType::WindowDrag, windowDragPreviewJson(move), owner.session, owner.epoch))
      cancelWindowDrag(binding);
  }
  void cancelWindowDrag(const WindowDragBinding &binding)
  {
    if (binding.direction == WindowDragDirection::Return)
      return cancelWindowReturn(binding);
    if (m_hostDrag && m_hostDrag->binding == binding) {
      const auto old = std::exchange(m_hostDrag, {});
      if (old->command.drag) {
        auto cancelled = old->command;
        cancelled.kind = WindowDragPreviewCommand::Kind::Cancel;
        send(MessageType::WindowDrag, windowDragPreviewJson(cancelled), old->owner.session, old->owner.epoch);
      }
    }
    if (m_native)
      m_native->cancelWindowDrag(binding);
  }
  bool prepareWindowReturn(const WindowDragBinding &binding, const LocalWindowDrag &command)
  {
    const QPointer<Bridge> alive(this);
    const auto peer = authenticatedPeerObservation();
    if (m_shuttingDown || !m_native || !m_windowDragMoveCapable || m_returnDrag || m_hostDrag || !peer ||
        peer->ticket().incarnation() != binding.connection || !command.returnOffer)
      return false;
    for (const auto &[session, observed] : m_nativeMoves) {
      const auto observation = observed;
      const auto metadata = m_hostMetadata.find(session);
      if (metadata == m_hostMetadata.end() || integer(metadata->second, "windowId", UINT32_MAX) != binding.window ||
          metadata->second.value("phase") != QJsonValue("remote") ||
          observation.geometry != integer(metadata->second, "geometry") ||
          (observation.native.phase != GnomeWindowDragEvent::Phase::Begin &&
           observation.native.phase != GnomeWindowDragEvent::Phase::Motion))
        continue;
      const auto &frame = observation.native.frame;
      const Point anchor{
          std::clamp(double(observation.native.x - frame.x()) / frame.width(), 0., 1.),
          std::clamp(double(observation.native.y - frame.y()) / frame.height(), 0., 1.)
      };
      const Owner owner{session, m_hostSessions.at(session)};
      auto description = metadata->second;
      const auto rectangle = m_native->windowReturnFrame(
          owner, observation.geometry, {double(command.destinationX), double(command.destinationY)}, anchor
      );
      if (!alive || m_shuttingDown || !rectangle || m_returnDrag || !m_hostSessions.contains(owner.session) ||
          m_hostSessions.at(owner.session) != owner.epoch)
        return false;
      description.insert("phase", "preparing");
      description.insert("windowDrag", number(binding.drag));
      const QPointer<Viewer> preview = new Viewer(owner.session, owner.epoch, description);
      preview->setWindowFlags(
          Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput
      );
      preview->setAttribute(Qt::WA_ShowWithoutActivating);
      preview->setAttribute(Qt::WA_DeleteOnClose);
      preview->onSend = [](MessageType type, const QJsonObject &object, std::uint64_t, std::uint64_t) {
        return type == MessageType::Ready ||
               (type == MessageType::Input && object.value("kind") == QJsonValue("visibility"));
      };
      preview->onError = [this, binding](const QString &) { failWindowReturn(binding); };
      m_returnDrag = ReturnDrag{binding, owner, observation, command, anchor, *rectangle, preview};
      connect(preview, &QObject::destroyed, this, [this, binding] {
        // QWidget emits destroyed after Viewer members are gone, before the
        // QObject base clears QPointer. Never call back into that partial object.
        if (m_returnDrag && m_returnDrag->binding == binding) {
          m_returnDrag->preview.clear();
          failWindowReturn(binding);
        }
      });
      auto *poll = new QTimer(preview);
      connect(poll, &QTimer::timeout, this, [this, binding] { pollWindowReturn(binding); });
      poll->start(25);
      preview->resize(qRound(rectangle->width), qRound(rectangle->height));
      if (!alive || !preview || !m_returnDrag || m_returnDrag->binding != binding)
        return false;
      preview->move(qRound(rectangle->x), qRound(rectangle->y));
      if (!alive || !preview || !m_returnDrag || m_returnDrag->binding != binding)
        return false;
      preview->show();
      if (!alive || !preview || !m_returnDrag || m_returnDrag->binding != binding)
        return false;
      m_native->requestKeyframe(owner.session, owner.epoch);
      return alive && m_returnDrag && m_returnDrag->binding == binding;
    }
    return false;
  }
  void cancelWindowReturn(const WindowDragBinding &binding)
  {
    if (!m_returnDrag || m_returnDrag->binding != binding)
      return;
    const auto old = std::exchange(m_returnDrag, {});
    if (old->preview) {
      old->preview->onSend = {};
      old->preview->onError = {};
      old->preview->disconnected();
      old->preview->close();
    }
  }
  void failWindowReturn(const WindowDragBinding &binding)
  {
    if (!m_returnDrag || m_returnDrag->binding != binding)
      return;
    const QPointer<Bridge> alive(this);
    cancelWindowReturn(binding);
    if (alive) {
      const auto callback = onWindowDragCancelled;
      if (callback)
        callback(binding);
    }
  }
  void placeWindowReturn()
  {
    if (!m_returnDrag || !m_returnDrag->preview || !m_native)
      return;
    const QPointer<Bridge> alive(this);
    const auto drag = *m_returnDrag;
    const auto frame = m_native->windowReturnFrame(
        drag.owner, drag.native.geometry, {double(drag.command.destinationX), double(drag.command.destinationY)},
        drag.anchor
    );
    if (!alive || !m_returnDrag || m_returnDrag->binding != drag.binding || m_returnDrag->command != drag.command)
      return;
    if (!frame || !drag.preview)
      return failWindowReturn(drag.binding);
    if (!sameReturnFrame(drag.frame, *frame)) {
      m_returnDrag->beforePaint = drag.preview->framePaintSerial();
      m_returnDrag->frame = *frame;
      drag.preview->move(qRound(frame->x), qRound(frame->y));
      if (alive && drag.preview && m_returnDrag && m_returnDrag->binding == drag.binding)
        drag.preview->update();
    }
  }
  static bool sameReturnFrame(Rect a, Rect b)
  {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
  }
  void pollWindowReturn(const WindowDragBinding &binding)
  {
    if (!m_returnDrag || m_returnDrag->binding != binding || m_returnDrag->committing)
      return;
    const QPointer<Bridge> alive(this);
    const auto peer = authenticatedPeerObservation();
    const auto drag = *m_returnDrag;
    const auto current = m_nativeMoves.find(drag.owner.session);
    if (!drag.preview || !peer || peer->ticket().incarnation() != binding.connection ||
        current == m_nativeMoves.end() || current->second.geometry != drag.native.geometry ||
        current->second.surface != drag.native.surface || current->second.native.grab != drag.native.native.grab ||
        current->second.native.phase == GnomeWindowDragEvent::Phase::Cancel)
      return failWindowReturn(binding);
    const auto nativeSequence = current->second.native.sequence;
    const bool nativeReleased =
        current->second.native.phase == GnomeWindowDragEvent::Phase::End && current->second.native.buttonReleased;
    const auto frame = m_native->windowReturnFrame(
        drag.owner, drag.native.geometry, {double(drag.command.destinationX), double(drag.command.destinationY)},
        drag.anchor
    );
    if (!alive || !m_returnDrag || m_returnDrag->binding != binding || m_returnDrag->command != drag.command)
      return;
    const auto latest = m_nativeMoves.find(drag.owner.session);
    if (latest == m_nativeMoves.end() || latest->second.native.sequence != nativeSequence)
      return;
    if (!frame || !sameReturnFrame(*frame, drag.frame))
      return failWindowReturn(binding);
    if (drag.preview->framePaintSerial() <= drag.beforePaint ||
        !NativeHost::windowReturnPreviewCurrent(drag.preview->internalWinId(), drag.frame))
      return;
    if (drag.drop) {
      if (!nativeReleased)
        return;
      m_returnDrag->committing = true;
      const auto owner = drag.owner;
      const auto geometry = drag.native.geometry;
      const auto destination = drag.frame;
      const bool restored = m_native->returnWindowDrag(owner, geometry, destination);
      if (!alive || !m_returnDrag || m_returnDrag->binding != binding)
        return;
      if (!restored)
        return failWindowReturn(binding);
      const auto callback = onWindowDragCommitted;
      if (callback)
        callback(binding);
      return;
    }
    if (drag.acknowledged == drag.command.sequence)
      return;
    m_returnDrag->acknowledged = drag.command.sequence;
    const WindowDragPreviewReply ready{
        {WindowDragPreviewCommand::Kind::Move, binding.drag, drag.command.sequence, drag.native.geometry,
         drag.command.destinationX, drag.command.destinationY, drag.anchor.x, drag.anchor.y},
        true,
        {qRound(frame->x), qRound(frame->y), qRound(frame->width), qRound(frame->height)},
        drag.preview->framePaintSerial(),
        1
    };
    const auto callback = onWindowDragPreview;
    if (callback)
      callback(binding, ready);
  }
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
  void attachViewportEndpoint(ViewerViewportEndpoint &endpoint)
  {
    require(!m_hosting && !m_viewportEndpoint, "Viewport endpoint requires one viewer controller");
    m_viewportEndpoint = &endpoint;
    const QPointer<Bridge> alive(this);
    endpoint.onWindowDrag = [alive](const GnomeWindowDragEvent &event) {
      if (!alive || alive->m_shuttingDown)
        return;
      std::vector<QPointer<WindowDragWatcher>> watchers;
      for (const auto &[id, view] : alive->m_viewers)
        if (view.watcher)
          watchers.push_back(view.watcher);
      for (const auto &watcher : watchers)
        if (alive && watcher)
          watcher->receive(event);
    };
    endpoint.onSnapshot = [alive] { return alive ? alive->viewportState() : std::nullopt; };
    endpoint.onCheck = [alive](const GnomeViewportState &expected) {
      return alive && alive->viewportState() == std::optional(expected);
    };
    endpoint.onMap = [alive](const GnomeViewportState &expected, QPointF point) -> std::optional<QPointF> {
      if (!alive || alive->viewportState() != std::optional(expected))
        return {};
      const auto found = alive->m_viewers.find(expected.session);
      if (found == alive->m_viewers.end() || !found->second.window)
        return {};
      const auto mapped = found->second.window->mapWaylandSurface(expected, point);
      return alive && alive->viewportState() == std::optional(expected) ? mapped : std::nullopt;
    };
    endpoint.onQtQueue = [alive, &endpoint](const GnomeViewportState &expected, std::function<void(bool)> finished) {
      if (!alive || alive->viewportState() != std::optional(expected))
        return false;
      const auto found = alive->m_viewers.find(expected.session);
      if (found == alive->m_viewers.end() || !found->second.window ||
          found->second.window->geometryRevision() != expected.geometry)
        return false;
      auto *window = found->second.window->windowHandle();
      const auto geometry = readWaylandViewport(window);
      if (!geometry || !alive || alive->viewportState() != std::optional(expected))
        return false;
      return endpoint.startWaylandQueue(window, *geometry, std::move(finished));
    };
    const auto exactFence = [alive](const GnomeViewportState &expected, const InputCutoffBinding &cutoff) {
      const auto *app = dynamic_cast<ViewerApplication *>(qApp);
      if (!alive || !alive->m_inputCapable || !validInputCutoffBinding(cutoff) || !app ||
          !app->ownsInputDrainDispatch() || !app->inputDispatchIdle() ||
          cutoff.viewerConnection != expected.connection || alive->viewportState() != std::optional(expected))
        return false;
      const auto found = alive->m_viewers.find(expected.session);
      return found != alive->m_viewers.end() && found->second.window &&
             (!alive->m_preparationRoute || (alive->m_preparationRoute->consumed &&
                                             sameInputCutoffLifetime(*alive->m_preparationRoute->consumed, cutoff))) &&
             found->second.window->allForwardingFenced(cutoff.forwarding);
    };
    endpoint.onInputDrainCheck = exactFence;
    const QPointer<ViewerViewportEndpoint> ownedEndpoint(&endpoint);
    endpoint.onInputDrain = [alive, ownedEndpoint, exactFence](
                                const GnomeViewportState &expected, const InputCutoffBinding &cutoff,
                                QDeadlineTimer deadline, std::function<void(bool)> finished
                            ) {
      if (!ownedEndpoint || !exactFence(expected, cutoff))
        return false;
      const auto found = alive->m_viewers.find(expected.session);
      auto *window = found->second.window->windowHandle();
      const auto geometry = readWaylandViewport(window);
      if (!geometry || !exactFence(expected, cutoff))
        return false;
      return ownedEndpoint->startWaylandInputDrain(
          window, *geometry, deadline, [exactFence, expected, cutoff] { return exactFence(expected, cutoff); },
          std::move(finished)
      );
    };
    endpoint.onPreparationInventory =
        [alive](const InputPreparationBinding &binding) -> std::optional<GnomePreparationInventory> {
      const auto *app = dynamic_cast<ViewerApplication *>(qApp);
      if (!alive || alive->m_hosting || alive->m_shuttingDown || alive->m_cleaning || !alive->m_established ||
          !alive->m_channel || !alive->m_inputCapable || alive->m_preparationRoute || !app ||
          !app->ownsInputDrainDispatch() || !app->inputDispatchIdle())
        return {};
      const auto peer = alive->m_channel->authenticatedPeerObservation();
      if (!peer || !peer->live())
        return {};
      GnomePreparationInventory result;
      result.connection = peer->ticket().incarnation();
      const auto &pin = peer->ticket().fingerprint();
      result.peerFingerprint =
          QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(pin.data()), pin.size()).toHex());
      std::vector<QPointer<Viewer>> viewers;
      for (const auto &[session, view] : alive->m_viewers) {
        if (!view.window)
          continue;
        viewers.push_back(view.window);
        if (const auto target = view.window->waylandPreparationTarget())
          result.targets.push_back(*target);
      }
      if (result.targets.empty())
        return result; // No gate was installed; this refusal needs no release.
      alive->m_preparationRoute = PreparationRoute{binding, result.connection, {}, {}};
      // Install the connection gate before any callback can deliver more input
      // or create another Viewer. Partial installation is terminal to the owned
      // endpoint; it is never interpreted as a completed cleanup.
      for (const auto &viewer : viewers) {
        if (!alive || !viewer || !viewer->beginInputPreparation(binding))
          return {};
      }
      if (!alive || !peer->live())
        return {};
      return result;
    };
    const auto preparationFence =
        [alive](const InputPreparationBinding &binding, const GnomePreparationTarget &target) {
          const auto *app = dynamic_cast<ViewerApplication *>(qApp);
          if (!alive || alive->m_shuttingDown || alive->m_cleaning || !alive->m_established || !alive->m_channel ||
              !alive->m_preparationRoute || alive->m_preparationRoute->binding != binding || !app ||
              !app->ownsInputDrainDispatch() || !app->inputDispatchIdle())
            return false;
          const auto peer = alive->m_channel->authenticatedPeerObservation();
          if (!peer || !peer->live() || peer->ticket().incarnation() != alive->m_preparationRoute->connection)
            return false;
          for (const auto &[session, view] : alive->m_viewers)
            if (view.window && !view.window->inputPreparationFenced(binding))
              return false;
          const auto found = alive->m_viewers.find(target.session);
          return found != alive->m_viewers.end() && found->second.window &&
                 found->second.window->waylandPreparationTarget() == std::optional(target);
        };
    endpoint.onPreparationCheck = preparationFence;
    endpoint.onPreparationMap = [alive, preparationFence](
                                    const InputPreparationBinding &binding, const GnomePreparationTarget &target,
                                    QPointF point
                                ) -> std::optional<QPointF> {
      if (!preparationFence(binding, target))
        return {};
      const auto mapped = alive->m_viewers.at(target.session).window->mapPreparationTarget(target, point);
      return preparationFence(binding, target) ? mapped : std::nullopt;
    };
    endpoint.onPreparationDrain = [alive, ownedEndpoint, preparationFence](
                                      const InputPreparationBinding &binding, const GnomePreparationTarget &target,
                                      QDeadlineTimer deadline, std::function<void(bool)> finished
                                  ) {
      if (!ownedEndpoint || !preparationFence(binding, target))
        return false;
      auto *window = alive->m_viewers.at(target.session).window->windowHandle();
      const auto geometry = readWaylandViewport(window);
      if (!geometry || !preparationFence(binding, target))
        return false;
      // Cleanup uses the preparation gate itself. Host All may already have
      // resumed; that neither opens this gate nor invalidates this witness.
      return ownedEndpoint->startWaylandInputDrain(
          window, *geometry, deadline,
          [preparationFence, binding, target] { return preparationFence(binding, target); }, std::move(finished)
      );
    };
    endpoint.onPreparationFocus = [alive, preparationFence](
                                      const InputPreparationBinding &binding, const GnomePreparationTarget &target
                                  ) -> std::optional<GnomeViewportState> {
      if (!preparationFence(binding, target))
        return {};
      alive->m_preparationRoute->focusIntent = Owner{target.session, target.epoch};
      const auto state = alive->m_viewers.at(target.session).window->requestPreparedFocus(binding, target);
      if (alive && alive->m_preparationRoute && alive->m_preparationRoute->binding == binding)
        alive->m_preparationRoute->focusIntent.reset();
      if (!state || !preparationFence(binding, target))
        return {};
      return alive->viewportState(); // Adds the actual retained TLS connection and pin.
    };
    endpoint.onPreparationFinish = [alive, preparationFence](
                                       const InputPreparationBinding &binding, const GnomePreparationTarget &target,
                                       const std::optional<InputCutoffBinding> &all
                                   ) {
      if (!preparationFence(binding, target))
        return false;
      if (all) {
        const auto state = alive->viewportState();
        const auto &route = all->forwarding;
        if (!validInputCutoffBinding(*all) || !state || all->viewerConnection != state->connection ||
            route.owner != Owner{state->session, state->epoch} || route.geometry != state->geometry ||
            route.focusRequest != state->focusRequest || route.focusRevision != state->focusRevision ||
            !alive->m_viewers.at(target.session).window->allForwardingFenced(route))
          return false;
        // Promotion consumes the preparation association, not its gate. An
        // independent Host All timeout cannot expose queued legacy input.
        alive->m_preparationRoute->consumed = *all;
        return true;
      }
      std::vector<QPointer<Viewer>> viewers;
      for (const auto &[session, view] : alive->m_viewers)
        if (view.window)
          viewers.push_back(view.window);
      for (const auto &viewer : viewers)
        if (!alive || !viewer || !viewer->finishInputPreparation(binding, std::nullopt))
          return false;
      if (!alive)
        return false;
      for (const auto &[session, view] : alive->m_viewers)
        if (view.window && view.window->inputPreparationFenced(binding))
          return false; // A reentrant new Viewer keeps the connection closed.
      alive->m_preparationRoute.reset();
      return true;
    };
  }
#endif
  // A local controller must revoke its private original-input authority before
  // either native admission or viewer forwarding can resume. Network messages
  // cannot install this callback, and main does not activate this route yet.
  // These callbacks borrow this controller. They may close the channel or
  // withdraw a route, but must use deleteLater for controller destruction.
  // Revocation is synchronous; quiesced notification is deferred outside the
  // transport/native callback stack and revalidated immediately before delivery.
  std::function<bool(const KeyboardRouteBinding &, KeyboardRouteReason)> revokeOriginalKeyboard;
  std::function<void(const KeyboardRouteBinding &)> onKeyboardQuiesced;
  std::function<void(const KeyboardRouteBinding &)> onKeyboardResumed;
  std::optional<LocalInputTarget> originalInputTarget(const KeyboardRouteBinding &binding) const
  {
    if (!m_mappedInputCapable || binding.scope != ForwardingScope::All || !m_keyboardRoute ||
        !m_keyboardRoute->viewerConnection ||
        (!keyboardAttemptCurrent(binding, KeyboardPhase::Quiesced) &&
         !keyboardAttemptCurrent(binding, KeyboardPhase::Active)))
      return {};
    const auto bytes = QByteArray::fromHex(m_identity.fingerprint().toLatin1());
    if (bytes.size() != 32)
      return {};
    LocalInputTarget target;
    target.viewerConnection = m_keyboardRoute->viewerConnection;
    std::copy_n(reinterpret_cast<const std::uint8_t *>(bytes.constData()), 32, target.viewerPeerFingerprint.begin());
    return validLocalInputTarget(target) ? std::optional(target) : std::nullopt;
  }
  std::optional<AuthenticatedPeerObservation> authenticatedPeerObservation() const
  {
    if (!m_established || !m_keyboardCapable || !m_channel || m_cleaning || m_shuttingDown)
      return std::nullopt;
    auto observation = m_channel->authenticatedPeerObservation();
    return observation && observation->live() ? observation : std::nullopt;
  }
  // Reserve before sending private preflight state. No viewer fence is requested
  // until start(), and slow native validation consumes this same deadline.
  std::optional<KeyboardRouteBinding>
  reserveOriginalKeyboard(std::uint64_t session, std::uint64_t epoch, ForwardingScope scope = ForwardingScope::Keyboard)
  {
    if (!m_native || !authenticatedPeerObservation() || m_keyboardRoute || m_reservingKeyboard ||
        !revokeOriginalKeyboard || m_keyboardGeneration == 9007199254740991ULL ||
        (scope != ForwardingScope::Keyboard && scope != ForwardingScope::All) ||
        (scope == ForwardingScope::All && !m_inputCapable)) {
      lifecycleDiagnostic(
          "Host", "input.reservation", "denied", "Local route cannot reserve this owner",
          {{"nativeHost", m_native != nullptr},
           {"authenticated", authenticatedPeerObservation().has_value()},
           {"routeHeld", m_keyboardRoute.has_value()},
           {"reserving", m_reservingKeyboard},
           {"releaseCallback", bool(revokeOriginalKeyboard)},
           {"fullInputCapable", m_inputCapable}}
      );
      return std::nullopt;
    }
    const auto found = m_hostSessions.find(session);
    if (found == m_hostSessions.end() || found->second != epoch)
      return std::nullopt;
    const QPointer<Bridge> alive(this);
    const auto connection = m_connectionGeneration;
    const auto next = m_keyboardGeneration + 1;
    const QDeadlineTimer deadline(m_keyboardTimeoutMs, Qt::PreciseTimer);
    m_reservingKeyboard = true;
    std::optional<KeyboardRouteBinding> binding;
    try {
      binding = m_native->keyboardRouteCandidate(session, epoch, next, scope);
    } catch (...) {
    }
    if (!alive)
      return std::nullopt;
    m_reservingKeyboard = false;
    const auto current = m_hostSessions.find(session);
    if (!binding || !validKeyboardRouteBinding(*binding) || binding->owner != Owner{session, epoch} ||
        binding->generation != next || binding->scope != scope || connection != m_connectionGeneration ||
        !authenticatedPeerObservation() || !revokeOriginalKeyboard || current == m_hostSessions.end() ||
        current->second != epoch || m_keyboardRoute || deadline.hasExpired())
      return std::nullopt;
    m_keyboardGeneration = binding->generation;
    m_keyboardRoute = KeyboardAttempt{*binding, connection};
    m_keyboardRoute->deadline = deadline;
    m_keyboardTimeout.start(static_cast<int>(deadline.remainingTime()));
    return binding;
  }
  // This negotiated fence covers keys and pointers in the actual Viewer and
  // NativeHost paths. It supplies neither compositor consumption evidence nor
  // original-input authority; the private controller must establish both.
  std::optional<KeyboardRouteBinding> reserveOriginalInput(std::uint64_t session, std::uint64_t epoch)
  {
    return reserveOriginalKeyboard(session, epoch, ForwardingScope::All);
  }
  std::optional<KeyboardRouteBinding> reserveOriginalKeyboard(ForwardingScope scope = ForwardingScope::Keyboard)
  {
    // The catalog is bounded; NativeHost admits only the actual focused owner.
    // A value copy also survives a callback that clears exported sessions.
    const QPointer<Bridge> alive(this);
    const auto sessions = m_hostSessions;
    for (const auto &[session, epoch] : sessions) {
      const auto binding = reserveOriginalKeyboard(session, epoch, scope);
      if (!alive || binding)
        return binding;
    }
    return std::nullopt;
  }
  bool nativeOriginalInputCurrent(const KeyboardRouteBinding &binding, std::uint64_t captureEpoch) const
  {
    if (!keyboardAttemptCurrent(binding, KeyboardPhase::Active))
      return false;
    const QPointer<const Bridge> alive(this);
    // NativeHost already checks the exact fence, ledger, focus and geometry.
    // Keep the bridge's connection/phase checks on both sides of native work.
    const bool current = m_native->originalInputCurrent(binding, captureEpoch);
    return alive && current && keyboardAttemptCurrent(binding, KeyboardPhase::Active);
  }
  bool nativeOriginalInputPending(const KeyboardRouteBinding &binding, std::uint64_t captureEpoch) const
  {
    return keyboardAttemptCurrent(binding, KeyboardPhase::Active) &&
           m_native->originalInputPending(binding, captureEpoch);
  }
  // Local route state only. Commit independently reads native authority, and
  // each pending delivery must pass the full fresh native check before posting.
  bool originalInputRouteActive(const KeyboardRouteBinding &binding) const
  {
    return binding.scope == ForwardingScope::All && keyboardAttemptCurrent(binding, KeyboardPhase::Active);
  }
  bool originalInputRouteReady(const KeyboardRouteBinding &binding) const
  {
    // Preparation grants no native delivery. Check the exact local route and
    // drained legacy ledger here; activation reads fresh native focus/geometry
    // before Commit, and native delivery rechecks it before posting any input.
    return originalInputRouteActive(binding) || originalInputBoundary(binding);
  }
  bool deliverNativeOriginalInput(
      const KeyboardRouteBinding &binding, const OriginalInputRecord &record, const LocalInputMappedPoint &point
  )
  {
    if (!keyboardAttemptCurrent(binding, KeyboardPhase::Active))
      return false;
    const QPointer<Bridge> alive(this);
    const bool delivered = m_native->deliverOriginalInput(binding, record, point);
    return alive && delivered && keyboardAttemptCurrent(binding, KeyboardPhase::Active);
  }
  void releaseNativeOriginalInput(const KeyboardRouteBinding &binding)
  {
    if (m_native)
      m_native->revokeOriginalInput(binding);
  }
  bool startOriginalKeyboard(KeyboardRouteBinding binding)
  {
    if (!m_keyboardRoute || m_keyboardRoute->binding != binding || m_keyboardRoute->phase != KeyboardPhase::Reserved)
      return false;
    if (m_keyboardRoute->deadline.hasExpired()) {
      withdrawOriginalKeyboard(KeyboardRouteReason::Timeout);
      return false;
    }
    if (!keyboardAttemptCurrent(binding, KeyboardPhase::Reserved)) {
      withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
      return false;
    }
    const QPointer<Bridge> alive(this);
    std::optional<KeyboardRouteBinding> current;
    try {
      current = m_native->keyboardRouteCandidate(
          binding.owner.session, binding.owner.epoch, binding.generation, binding.scope
      );
    } catch (...) {
    }
    if (!alive || !m_keyboardRoute || m_keyboardRoute->binding != binding ||
        m_keyboardRoute->phase != KeyboardPhase::Reserved)
      return false;
    if (m_keyboardRoute->deadline.hasExpired()) {
      withdrawOriginalKeyboard(KeyboardRouteReason::Timeout);
      return false;
    }
    if (current != binding || !keyboardAttemptCurrent(binding, KeyboardPhase::Reserved)) {
      withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
      return false;
    }
    m_keyboardRoute->phase = KeyboardPhase::AwaitQuiescence;
    m_keyboardRoute->viewerRequested = true;
    const bool sent = sendKeyboardRoute({binding, KeyboardRouteAction::Request, KeyboardRouteMode::Original});
    if (!alive)
      return false;
    if (!sent && m_keyboardRoute && m_keyboardRoute->binding == binding)
      withdrawOriginalKeyboard(KeyboardRouteReason::Failed);
    return sent && m_keyboardRoute && m_keyboardRoute->binding == binding && !m_keyboardRoute->revoked;
  }
  bool originalKeyboardQuiesced(KeyboardRouteBinding binding) const
  {
    if (!keyboardAttemptCurrent(binding, KeyboardPhase::Quiesced))
      return false;
    const QPointer<const Bridge> alive(this);
    bool neutral = false;
    try {
      // NativeHost rechecks current focus/geometry and all Qt key/button owners.
      neutral = m_native->keyboardRouteQuiesced(binding);
    } catch (...) {
    }
    return alive && neutral && keyboardAttemptCurrent(binding, KeyboardPhase::Quiesced);
  }
  bool originalInputBoundary(const KeyboardRouteBinding &binding) const
  {
    if (binding.scope != ForwardingScope::All || !keyboardAttemptCurrent(binding, KeyboardPhase::Quiesced))
      return false;
    const QPointer<const Bridge> alive(this);
    const bool fenced = m_native->keyboardRouteFenced(binding);
    return alive && fenced && keyboardAttemptCurrent(binding, KeyboardPhase::Quiesced);
  }
  // The local preflight calls this only after its other admission checks and
  // before acknowledging Commit. Network acknowledgments cannot perform this
  // transition. A setup reservation is bounded; active ownership is instead
  // retained only while that controller revalidates its live providers.
  bool activateOriginalKeyboard(KeyboardRouteBinding binding, std::uint64_t captureEpoch = 0)
  {
    if (captureEpoch && binding.scope != ForwardingScope::All)
      return false;
    if (!m_keyboardRoute || m_keyboardRoute->binding != binding || m_keyboardRoute->phase != KeyboardPhase::Quiesced)
      return false;
    if (m_keyboardRoute->deadline.hasExpired()) {
      withdrawOriginalKeyboard(KeyboardRouteReason::Timeout);
      return false;
    }
    if (!keyboardAttemptCurrent(binding, KeyboardPhase::Quiesced)) {
      withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
      return false;
    }
    const QPointer<Bridge> alive(this);
    // Guard native callbacks and nested event loops against a second commit.
    m_keyboardRoute->phase = KeyboardPhase::Activating;
    bool quiesced = false;
    try {
      // Establishing native original input includes its own fresh validation.
      // Perform it while activation is guarded, under the same deadline.
      quiesced = captureEpoch ? m_native->establishOriginalInput(binding, captureEpoch)
                              : m_native->keyboardRouteQuiesced(binding);
    } catch (...) {
    }
    if (!alive || !m_keyboardRoute || m_keyboardRoute->binding != binding ||
        m_keyboardRoute->phase != KeyboardPhase::Activating)
      return false;
    if (m_keyboardRoute->deadline.hasExpired()) {
      withdrawOriginalKeyboard(KeyboardRouteReason::Timeout);
      return false;
    }
    if (!quiesced || !keyboardAttemptCurrent(binding, KeyboardPhase::Activating)) {
      withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
      return false;
    }
    m_keyboardRoute->phase = KeyboardPhase::Active;
    m_keyboardRoute->deadline = QDeadlineTimer(QDeadlineTimer::Forever);
    m_keyboardTimeout.stop();
    return true;
  }
  bool originalKeyboardActive(KeyboardRouteBinding binding) const
  {
    if (!keyboardAttemptCurrent(binding, KeyboardPhase::Active))
      return false;
    const QPointer<const Bridge> alive(this);
    bool fenced = false;
    try {
      // This checks the native focus and the legacy Qt-input fence/ledger.
      // It does not require original-input presses to have been released.
      fenced = m_native->keyboardRouteQuiesced(binding);
    } catch (...) {
    }
    return alive && fenced && keyboardAttemptCurrent(binding, KeyboardPhase::Active);
  }
  bool requestOriginalKeyboard(std::uint64_t session, std::uint64_t epoch)
  {
    const auto binding = reserveOriginalKeyboard(session, epoch);
    return binding && startOriginalKeyboard(*binding);
  }
  void withdrawOriginalKeyboard(KeyboardRouteReason reason, bool resumeViewer = true)
  {
    if (!m_keyboardRoute)
      return;
    const QPointer<Bridge> alive(this);
    // A revocation callback may request terminal shutdown or enter a nested
    // event loop. Complete restoration only after that callback has returned.
    const auto shutdown = qScopeGuard([this, alive] {
      if (alive)
        finishShutdown();
    });
    const auto binding = m_keyboardRoute->binding;
    if (m_keyboardRoute->phase == KeyboardPhase::Revoking) {
      m_keyboardRoute->resumeViewer = m_keyboardRoute->resumeViewer && resumeViewer;
      return;
    }
    if (!m_keyboardRoute->revoked) {
      m_keyboardRoute->revoked = true;
      m_keyboardRoute->phase = KeyboardPhase::Revoking;
      m_keyboardRoute->resumeViewer = resumeViewer;
      m_keyboardTimeout.stop();
      bool released = false;
      try {
        const auto revoke = revokeOriginalKeyboard;
        released = revoke && revoke(binding, reason);
      } catch (...) {
      }
      if (!alive || !m_keyboardRoute || m_keyboardRoute->binding != binding)
        return;
      if (m_shuttingDown) {
        retireKeyboardRoute();
        return;
      }
      if (!released) {
        m_keyboardTimeout.stop();
        retireKeyboardRoute();
        if (m_channel)
          m_channel->close("Original keyboard authority could not be revoked");
        return;
      }
      if (!m_keyboardRoute->viewerRequested) {
        retireKeyboardRoute();
        const auto resumed = onKeyboardResumed;
        if (resumed)
          resumed(binding);
        return;
      }
      if (!m_native->resumeKeyboard(binding) && m_keyboardRoute->quiesced) {
        retireKeyboardRoute();
        if (m_channel)
          m_channel->close("Native keyboard fence could not be resumed");
        return;
      }
      resumeViewer = m_keyboardRoute->resumeViewer;
      m_keyboardRoute->phase = KeyboardPhase::AwaitViewer;
    } else if (resumeViewer)
      return;
    if (resumeViewer && m_channel && m_established && !m_cleaning && !m_shuttingDown) {
      m_keyboardRoute->deadline = QDeadlineTimer(m_keyboardTimeoutMs, Qt::PreciseTimer);
      m_keyboardTimeout.start(m_keyboardTimeoutMs);
      sendKeyboardRoute({binding, KeyboardRouteAction::Request, KeyboardRouteMode::Viewer});
    } else
      retireKeyboardRoute();
  }
  ~Bridge() override
  {
    stopSharing();
    // Native teardown can retry journal restoration after our maps are gone.
    // It must not call a controller whose members are being destroyed.
    if (m_native) {
      m_native->onError = {};
      m_native->onCatalog = {};
      m_native->onOpened = {};
      m_native->onFrame = {};
      m_native->onCommitted = {};
      m_native->onFocused = {};
      m_native->onKeyboardRouteInvalidated = {};
      m_native->onClosed = {};
      m_native->onClipboardTextChanged = {};
    }
  }
  // Terminal for this controller. This blocks admission synchronously even
  // when an outstanding revocation callback must unwind before restoration.
  // Like the authority callbacks, callers must use deleteLater for destruction
  // from a callback stack. Quitting is a separate lifetime-owner decision.
  void stopSharing()
  {
    if (m_shuttingDown) {
      finishShutdown();
      return;
    }
    m_shuttingDown = true;
    updateClipboard(false);
    m_established = false;
    m_negotiating = false;
    m_reconnectEnabled = false;
    m_retryRequested = false;
    m_initialWindow = 0;
    m_listener.close();
    m_reconnectTimer.stop();
    m_stableTimer.stop();
    m_keyboardTimeout.stop();
    clearKeyframeRetries();
    clearCatalog();
    if (m_channel) {
      m_channel->onAuthenticated = {};
      m_channel->onClosed = {};
      m_channel->onPacket = {};
    }
    const QPointer<Bridge> alive(this);
    withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost, false);
    if (alive)
      finishShutdown();
  }
  bool listen(const QHostAddress &address, quint16 port)
  {
    if (m_shuttingDown)
      return false;
    m_listener.incoming = [this](qintptr descriptor) {
      if (m_shuttingDown || m_channel || m_cleaning) {
        QTcpSocket socket;
        socket.setSocketDescriptor(descriptor);
        socket.abort();
        return;
      }
      prepareChannel();
      // A reconnect may arrive before the old peer's disconnect is observed.
      // Keep it in the bounded listen backlog until cleanup, rather than
      // sending an unauthenticated EOF that must be treated as terminal.
      m_listener.pauseAccepting();
      m_channel->accept(descriptor);
    };
    if (!m_listener.listen(address, port)) {
      status(m_listener.errorString());
      return false;
    }
    status(QString("Listening on %1:%2; awaiting approved peer").arg(address.toString()).arg(m_listener.serverPort()));
    return true;
  }
  void connectTo(const QString &host, quint16 port)
  {
    if (m_hosting || m_channel || m_cleaning || m_shuttingDown)
      return;
    m_connectHost = host;
    m_connectPort = port;
    retryConnection();
  }

protected:
  void closeEvent(QCloseEvent *event) override
  {
    event->accept();
    m_quitAfterShutdown = true;
    const QPointer<Bridge> alive(this);
    stopSharing();
    if (alive)
      notifyQuit();
  }

private:
  std::optional<ClipboardFocusObservation> clipboardFocus() const
  {
    if (!m_clipboard || !m_clipboard->enabled() || !m_established || !m_channel || !m_channel->authenticated() ||
        m_shuttingDown || m_cleaning)
      return {};
    const QPointer<const Bridge> alive(this);
    const auto channel = m_channel;
    const auto generation = m_connectionGeneration;
    std::optional<ClipboardFocusObservation> local;
    QPointer<Viewer> selected;
    if (m_native) {
      local = m_native->clipboardFocus();
      if (!alive)
        return {};
    } else {
      for (const auto &[session, view] : m_viewers) {
        (void)session;
        const auto focus = view.window ? view.window->clipboardFocus() : std::nullopt;
        if (!focus)
          continue;
        if (local)
          return {}; // No arbitrary choice when focus is ambiguous.
        local = focus;
        selected = view.window;
      }
    }
    if (!local)
      return {};
    const auto binding = local->binding;
    return ClipboardFocusObservation{
        binding, [alive, channel, generation, local = *local, selected, binding] {
          const auto connected = [&] {
            if (!alive || !channel || alive->m_channel != channel || alive->m_connectionGeneration != generation ||
                !alive->m_established || alive->m_shuttingDown || alive->m_cleaning || !alive->m_clipboard ||
                !alive->m_clipboard->enabled() || !channel->authenticated())
              return false;
            if (alive->m_hosting) {
              const auto found = alive->m_hostSessions.find(binding.owner.session);
              return found != alive->m_hostSessions.end() && found->second == binding.owner.epoch;
            }
            return selected && alive->currentViewer(binding.owner.session, binding.owner.epoch, selected, generation);
          };
          return connected() && local.live() && connected();
        }
    };
  }
  void initializeClipboard()
  {
    const QPointer<Bridge> alive(this);
    FocusedClipboard::Callbacks callbacks;
    callbacks.focus = [alive] { return alive ? alive->clipboardFocus() : std::nullopt; };
    callbacks.send = [alive](const ClipboardTextMessage &message) {
      return alive && alive->send(
                          MessageType::ClipboardText, encodeClipboardText(message), message.focus.owner.session,
                          message.focus.owner.epoch
                      );
    };
#ifndef WINDOWSHARE_BRIDGE_TEST
    callbacks.readText = []() -> std::optional<QString> {
      const QPointer<const QMimeData> mime(QApplication::clipboard()->mimeData(QClipboard::Clipboard));
      if (!mime || !mime->hasFormat(QStringLiteral("text/plain")) || !mime || mime->hasUrls() || !mime)
        return {};
      const auto text = mime->text();
      return validClipboardText(text) ? std::optional(text) : std::nullopt;
    };
    callbacks.writeText = [](const QString &text) {
      QApplication::clipboard()->setText(text, QClipboard::Clipboard);
      return true;
    };
#endif
    m_clipboard = std::make_unique<FocusedClipboard>(m_clipboardPolicy, std::move(callbacks));
    if (m_native) {
      m_native->onClipboardTextChanged = [alive](const ClipboardFocusObservation &observation) {
        if (alive && alive->m_clipboard)
          alive->m_clipboard->localChanged(observation);
      };
    }
  }
  void updateClipboard(bool negotiated)
  {
    if (!m_clipboard)
      return;
    m_clipboard->setNegotiated(negotiated && m_clipboardCapable);
    QObject::disconnect(m_clipboardConnection);
    m_clipboardConnection = {};
    const QPointer<Bridge> alive(this);
    if (m_native)
      m_native->setClipboardTextMonitoring(m_clipboard->enabled());
    if (!alive)
      return;
#ifndef WINDOWSHARE_BRIDGE_TEST
    // Neither construction, one-sided policy nor negotiation reads a clipboard.
    // Native Cocoa clipboard signals are activation-driven and could expose a
    // change from before source focus. Native hosts use the metadata/focus
    // sampler exclusively; Viewer peers use future local dataChanged signals.
    if (!m_native && m_clipboard->enabled()) {
      const QPointer<FocusedClipboard> clipboard(m_clipboard.get());
      m_clipboardConnection =
          connect(QApplication::clipboard(), &QClipboard::dataChanged, m_clipboard.get(), [clipboard] {
            if (clipboard)
              clipboard->localChanged();
          });
    }
#endif
  }
  void notifyQuit()
  {
    if (!m_shutdownComplete || !m_quitAfterShutdown || m_quitNotified)
      return;
    m_quitNotified = true;
    const auto quit = onQuitRequested;
    if (quit)
      quit();
  }
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
  std::optional<GnomeViewportState> viewportState() const
  {
    if (m_hosting || m_shuttingDown || m_cleaning || !m_established || !m_channel)
      return {};
    const auto peer = m_channel->authenticatedPeerObservation();
    if (!peer || !peer->live())
      return {};
    std::optional<GnomeViewportState> state;
    for (const auto &[session, view] : m_viewers) {
      if (!view.window)
        continue;
      auto candidate = view.window->waylandViewportState();
      if (!candidate)
        continue;
      if (state)
        return {}; // Two simultaneously eligible focused Viewers are ambiguous.
      state = std::move(candidate);
    }
    if (!state)
      return {};
    state->connection = peer->ticket().incarnation();
    const auto &fingerprint = peer->ticket().fingerprint();
    state->peerFingerprint =
        QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(fingerprint.data()), fingerprint.size()).toHex());
    return state;
  }
#endif
  void finishShutdown()
  {
    if (!m_shuttingDown || m_shutdownFinishing || m_shutdownComplete ||
        (m_keyboardRoute && m_keyboardRoute->phase == KeyboardPhase::Revoking))
      return;
    m_shutdownFinishing = true;
    const QPointer<Bridge> alive(this);
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
    if (m_viewportEndpoint)
      m_viewportEndpoint->close();
    if (!alive)
      return;
#endif
    const auto channel = m_channel;
    m_channel = nullptr;
    if (channel) {
      channel->onAuthenticated = {};
      channel->onClosed = {};
      channel->onPacket = {};
      channel->close();
      channel->deleteLater();
    }
    stopViewers();
    if (!alive)
      return;
    if (m_native) {
      m_native->releaseAll();
      if (!alive)
        return;
      m_native->restoreAll();
      if (!alive)
        return;
    }
    m_hostSessions.clear();
    m_cleaning = false;
    m_shutdownComplete = true;
    m_shutdownFinishing = false;
    const auto stopped = m_onSharingStopped;
    if (stopped)
      stopped();
    if (alive)
      notifyQuit();
  }
  enum class KeyboardPhase
  {
    Reserved,
    AwaitQuiescence,
    Quiesced,
    Activating,
    Active,
    Revoking,
    AwaitViewer
  };
  struct KeyboardAttempt
  {
    KeyboardRouteBinding binding;
    std::uint64_t connectionGeneration = 0;
    KeyboardPhase phase = KeyboardPhase::Reserved;
    bool revoked = false, quiesced = false, viewerRequested = false;
    bool resumeViewer = true;
    QDeadlineTimer deadline{QDeadlineTimer::Forever};
    std::uint64_t viewerConnection = 0;
  };
  bool keyboardAttemptCurrent(const KeyboardRouteBinding &binding, KeyboardPhase phase) const
  {
    const auto found = m_hostSessions.find(binding.owner.session);
    return m_native && revokeOriginalKeyboard && m_keyboardRoute && m_keyboardRoute->binding == binding &&
           m_keyboardRoute->phase == phase && !m_keyboardRoute->revoked && !m_keyboardRoute->deadline.hasExpired() &&
           m_keyboardRoute->connectionGeneration == m_connectionGeneration && found != m_hostSessions.end() &&
           found->second == binding.owner.epoch && authenticatedPeerObservation().has_value() &&
           (binding.scope != ForwardingScope::All || m_inputCapable);
  }
  bool sendKeyboardRoute(KeyboardRouteControl control)
  {
    if (!m_hosting && m_mappedInputCapable && control.binding.scope == ForwardingScope::All &&
        control.action == KeyboardRouteAction::Acknowledge && control.mode == KeyboardRouteMode::Original &&
        control.result == KeyboardRouteResult::Ready) {
      const auto peer = authenticatedPeerObservation();
      if (!peer)
        return false;
      control.viewerConnection = peer->ticket().incarnation();
    }
    return m_keyboardCapable && (control.binding.scope != ForwardingScope::All || m_inputCapable) &&
           send(
               forwardingRouteMessageType(control.binding), encodeKeyboardRouteControl(control),
               control.binding.owner.session, control.binding.owner.epoch
           );
  }
  void retireKeyboardRoute()
  {
    m_keyboardTimeout.stop();
    if (m_keyboardRoute)
      m_retiredKeyboardRoute = m_keyboardRoute->binding;
    m_keyboardRoute.reset();
  }
  void receiveKeyboardRoute(const Packet &packet)
  {
    if (m_shuttingDown)
      return;
    require(m_keyboardCapable, "Keyboard route capability was not negotiated");
    require(packet.type != MessageType::InputRoute || m_inputCapable, "Full input route capability was not negotiated");
    const auto control = parseKeyboardRouteControl(packet);
    require(
        !control.viewerConnection || (m_hosting && m_mappedInputCapable), "Unnegotiated viewer connection attestation"
    );
    if (!m_hosting) {
      require(control.action == KeyboardRouteAction::Request, "Invalid keyboard route direction");
      const auto found = m_viewers.find(packet.session);
      if (found != m_viewers.end() && found->second.epoch == packet.epoch && found->second.window)
        found->second.window->keyboardRoute(control);
      return;
    }
    require(control.action != KeyboardRouteAction::Request, "Invalid keyboard route direction");
    if (!m_keyboardRoute || m_keyboardRoute->binding != control.binding) {
      if (control.binding.generation < m_keyboardGeneration || m_retiredKeyboardRoute == control.binding)
        return;
      throw std::runtime_error("Unsolicited or mismatched keyboard route reply");
    }
    require(m_keyboardRoute->viewerRequested, "Keyboard route reply preceded its request");
    if (control.action == KeyboardRouteAction::Withdraw) {
      withdrawOriginalKeyboard(control.reason);
      return;
    }
    if (m_keyboardRoute->phase == KeyboardPhase::Revoking)
      return;
    if (m_keyboardRoute->phase == KeyboardPhase::AwaitViewer && m_keyboardRoute->deadline.hasExpired()) {
      if (m_channel)
        m_channel->close("Keyboard handoff resume timed out");
      return;
    }
    if (m_keyboardRoute->phase == KeyboardPhase::AwaitQuiescence && control.mode == KeyboardRouteMode::Original) {
      if (m_keyboardRoute->deadline.hasExpired()) {
        withdrawOriginalKeyboard(KeyboardRouteReason::Timeout);
        return;
      }
      if (control.result != KeyboardRouteResult::Ready) {
        withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost, false);
        return;
      }
      if (m_mappedInputCapable && control.binding.scope == ForwardingScope::All && !control.viewerConnection) {
        withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
        return;
      }
      const QPointer<Bridge> alive(this);
      const bool quiesced = keyboardAttemptCurrent(control.binding, KeyboardPhase::AwaitQuiescence) &&
                            m_native->quiesceKeyboard(control.binding);
      if (!alive || !m_keyboardRoute || m_keyboardRoute->binding != control.binding ||
          m_keyboardRoute->phase != KeyboardPhase::AwaitQuiescence)
        return;
      if (!quiesced) {
        withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
        return;
      }
      m_keyboardRoute->quiesced = true;
      m_keyboardRoute->viewerConnection = control.viewerConnection;
      if (m_keyboardRoute->deadline.hasExpired()) {
        withdrawOriginalKeyboard(KeyboardRouteReason::Timeout);
        return;
      }
      m_keyboardRoute->phase = KeyboardPhase::Quiesced;
      const auto binding = control.binding;
      const auto generation = m_connectionGeneration;
      QTimer::singleShot(0, this, [this, binding, generation] {
        if (!m_keyboardRoute || m_keyboardRoute->binding != binding ||
            m_keyboardRoute->phase != KeyboardPhase::Quiesced || m_connectionGeneration != generation ||
            !m_established || m_cleaning || m_shuttingDown)
          return;
        const QPointer<Bridge> alive(this);
        // All quiescence acknowledges only the closed legacy gate and empty
        // ledger. Prepare/Commit still require their own fresh native checks.
        const bool neutral =
            binding.scope == ForwardingScope::All ? originalInputBoundary(binding) : originalKeyboardQuiesced(binding);
        if (!alive || !m_keyboardRoute || m_keyboardRoute->binding != binding ||
            m_keyboardRoute->phase != KeyboardPhase::Quiesced || m_connectionGeneration != generation)
          return;
        if (m_keyboardRoute->deadline.hasExpired()) {
          withdrawOriginalKeyboard(KeyboardRouteReason::Timeout);
          return;
        }
        if (!neutral) {
          withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
          return;
        }
        try {
          const auto callback = onKeyboardQuiesced;
          if (callback)
            callback(binding);
        } catch (...) {
          if (alive && m_connectionGeneration == generation && m_keyboardRoute && m_keyboardRoute->binding == binding)
            withdrawOriginalKeyboard(KeyboardRouteReason::Failed);
        }
      });
    } else if (m_keyboardRoute->phase == KeyboardPhase::AwaitViewer && control.mode == KeyboardRouteMode::Viewer) {
      require(
          control.result == KeyboardRouteResult::Ready ||
              (!m_keyboardRoute->quiesced && control.result == KeyboardRouteResult::Stale),
          "Viewer did not resume the revoked keyboard route"
      );
      retireKeyboardRoute();
      const auto resumed = onKeyboardResumed;
      if (resumed)
        resumed(control.binding);
    }
    // An old Original ack may cross cancellation and the Viewer request. It
    // never reactivates a quiesced or revoked attempt.
  }
  void updateControls()
  {
    m_open->setEnabled(m_established && m_catalogReady && !m_cleaning && !m_shuttingDown);
    m_refresh->setEnabled(!m_cleaning && !m_shuttingDown && (m_hosting || m_established));
    if (m_retry)
      m_retry->setEnabled(!m_shuttingDown && !m_channel && !m_connectHost.isEmpty() && !m_retryRequested);
    if (m_cancel)
      m_cancel->setEnabled(!m_shuttingDown && (m_channel || m_cleaning || m_reconnectTimer.isActive()));
  }
  void clearCatalog()
  {
    m_catalogReady = false;
    m_lastCatalog = {};
    m_catalog->clear();
    updateControls();
  }
  void retryConnection()
  {
    if (m_hosting || m_shuttingDown || m_channel || m_connectHost.isEmpty())
      return;
    m_reconnectEnabled = true;
    m_retryStep = 0;
    m_reconnectTimer.stop();
    m_retryRequested = true;
    if (!m_cleaning)
      startConnection();
    updateControls();
  }
  void cancelConnection()
  {
    m_reconnectEnabled = false;
    m_retryRequested = false;
    m_initialWindow = 0;
    m_reconnectTimer.stop();
    m_stableTimer.stop();
    if (m_channel)
      m_channel->close("Connection canceled", CloseCause::Local);
    else if (!m_cleaning && !m_shuttingDown)
      status("Connection canceled. Retry to connect again.");
    updateControls();
  }
  void startConnection()
  {
    if (m_hosting || m_shuttingDown || !m_reconnectEnabled || m_cleaning || m_channel || m_connectHost.isEmpty())
      return;
    m_reconnectTimer.stop();
    m_retryRequested = false;
    try {
      prepareChannel();
      status("Connecting to approved Mac host…");
      m_channel->connectTo(m_connectHost, m_connectPort);
    } catch (const std::exception &error) {
      m_reconnectEnabled = false;
      status("Cannot connect: " + QString::fromUtf8(error.what()));
    }
    updateControls();
  }
  void scheduleReconnect()
  {
    if (m_hosting || m_shuttingDown || !m_reconnectEnabled || m_cleaning || m_channel || m_reconnectTimer.isActive())
      return;
    const int delay = m_retryDelays[m_retryStep];
    m_retryStep = std::min(m_retryStep + 1, m_retryDelays.size() - 1);
    m_reconnectTimer.start(delay);
    status(QString("Disconnected: %1\nRetrying in %2 seconds…").arg(m_disconnectReason).arg(delay / 1000.0));
    updateControls();
  }
  void status(const QString &message)
  {
    m_status->setText(message);
    QTextStream(stderr) << message << '\n';
  }
  bool send(MessageType type, const QJsonObject &object = {}, std::uint64_t session = 0, std::uint64_t epoch = 0)
  {
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
    if (m_preparationRoute && type == MessageType::Input && object.value("kind") != QStringLiteral("release") &&
        !(object.contains("down") && object.value("down").isBool() && !object.value("down").toBool()) &&
        !(object.value("kind") == QStringLiteral("focus") && m_preparationRoute->focusIntent == Owner{session, epoch}))
      return true; // Connection gate also covers reentrant Viewer creation/retirement.
#endif
    return !m_shuttingDown && m_established && m_channel && m_channel->send(type, object, session, epoch);
  }
  void clearKeyframeRetries()
  {
    m_keyframeRetry.stop();
    m_pendingKeyframes.clear();
    m_lastKeyframeRecoverySession = 0;
  }
  void retryKeyframe()
  {
    if (m_shuttingDown || !m_channel || !m_established || !m_native) {
      clearKeyframeRetries();
      return;
    }
    if (!m_channel->readyForFrame())
      return;
    if (m_pendingKeyframes.empty()) {
      m_keyframeRetry.stop();
      return;
    }
    // Limit recovery work to one window per tick. Erase before calling native
    // code: requestKeyframe invalidates old submissions before it can emit the
    // replacement, and a synchronous callback may queue a new recovery request.
    // A stream that repeatedly drops frames must not starve a second stream.
    // Select the next live session after the last attempted one, wrapping at
    // the end. The map is bounded by active sessions and erasure happens before
    // calling native code, so callback reentry can safely queue new work.
    auto next = m_pendingKeyframes.upper_bound(m_lastKeyframeRecoverySession);
    if (next == m_pendingKeyframes.end())
      next = m_pendingKeyframes.begin();
    const auto [session, epoch] = *next;
    m_pendingKeyframes.erase(next);
    m_lastKeyframeRecoverySession = session;
    if (m_pendingKeyframes.empty())
      m_keyframeRetry.stop();
    const auto found = m_hostSessions.find(session);
    if (found != m_hostSessions.end() && found->second == epoch)
      m_native->requestKeyframe(session, epoch);
  }
  void prepareChannel()
  {
    updateClipboard(false);
    m_established = false;
    m_negotiating = false;
    m_keyboardCapable = false;
    m_inputCapable = false;
    m_mappedInputCapable = false;
    m_mediaRecoveryCapable = false;
    m_clipboardCapable = false;
    m_familyCapable = false;
    m_stableTimer.stop();
    clearKeyframeRetries();
    clearCatalog();
    auto *channel = new Channel(m_identity, m_pin, this);
    m_channel = channel;
    const auto generation = ++m_connectionGeneration;
    channel->onAuthenticated = [this, channel, generation] {
      if (m_channel != channel || m_connectionGeneration != generation || m_shuttingDown)
        return;
      m_negotiating = true;
      if (!m_hosting)
        channel->send(
            MessageType::Hello,
            {{"version", static_cast<int>(protocolVersion)},
             {"role", "viewer"},
             {"codec", "h264"},
             {"capabilities", windowFamilyCapabilities(windowDragMoveCapabilities(
                                  windowDragMotionCapabilities(
                                      windowDragPreviewCapabilities(clipboardTextCapabilities(
                                          keyboardRouteCapabilities(true, true, true), m_clipboardPolicy
                                      )),
                                      nativeWindowDragAvailable()
                                  ),
                                  nativeWindowDragAvailable()
                              ))}}
        );
      QTimer::singleShot(5000, channel, [this, channel, generation] {
        if (m_channel == channel && m_connectionGeneration == generation && !m_established)
          channel->close("Window sharing negotiation timed out", CloseCause::Protocol);
      });
    };
    channel->onPacket = [this, channel, generation](Packet packet) {
      if (m_channel != channel || m_connectionGeneration != generation || m_shuttingDown)
        return;
      try {
        receive(packet);
      } catch (const std::exception &error) {
        channel->close(QString::fromUtf8(error.what()), CloseCause::Protocol);
      }
    };
    channel->onClosed = [this, channel, generation](QString reason, CloseCause cause) {
      if (m_shuttingDown || m_channel != channel || m_connectionGeneration != generation)
        return;
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
      if (m_viewportEndpoint) {
        const QPointer<Bridge> alive(this);
        m_viewportEndpoint->close(QStringLiteral("tls-channel: ") + reason.left(160));
        if (!alive || m_shuttingDown)
          return;
      }
#endif
      // A peer that drops TLS while rejecting Hello may be rejecting our
      // identity or protocol. Only a fully negotiated loss is retryable there.
      const bool retryable = cause == CloseCause::Network && (!m_negotiating || m_established);
      if (!retryable)
        m_reconnectEnabled = false;
      if (m_established)
        m_initialWindow = 0;
      m_established = false;
      updateClipboard(false);
      m_negotiating = false;
      m_channel = nullptr;
      m_stableTimer.stop();
      m_reconnectTimer.stop();
      clearKeyframeRetries();
      // send() may fail inside a NativeHost callback. Never destroy its entries
      // reentrantly while capture/geometry code is still using them.
      m_cleaning = true;
      withdrawOriginalKeyboard(KeyboardRouteReason::Disconnected, false);
      clearCatalog();
      QTimer::singleShot(0, this, [this, generation, retryable] {
        if (m_shuttingDown || m_connectionGeneration != generation)
          return;
        stopViewers();
        if (m_native) {
          m_native->releaseAll();
          m_native->restoreAll();
        }
        m_hostSessions.clear();
        m_hostGeometries.clear();
        m_hostMetadata.clear();
        m_nativeMoves.clear();
        m_hostMoveGates.clear();
        m_mediaRecoveryRequests.clear();
        m_mediaRecoveryCapable = false;
        m_cleaning = false;
        if (m_hosting && !m_shuttingDown)
          m_listener.resumeAccepting();
        updateControls();
        if (m_retryRequested)
          startConnection();
        else if (retryable)
          scheduleReconnect();
      });
      m_disconnectReason = reason;
      status("Disconnected: " + reason);
      channel->deleteLater();
    };
  }
  void receive(const Packet &packet)
  {
    if (m_shuttingDown)
      return;
    if (!m_established) {
      const auto object = parseObject(packet);
      require(packet.session == 0 && packet.epoch == 0, "Negotiation requires connection scope");
      require(
          integer(object, "version") == protocolVersion && object["codec"] == "h264",
          "Unsupported window sharing version or codec"
      );
      m_keyboardCapable = peerSupportsKeyboardRoute(object);
      m_inputCapable = peerSupportsInputRoute(object);
      m_mappedInputCapable = peerSupportsMappedInput(object);
      m_mediaRecoveryCapable = peerSupportsMediaRecovery(object);
      m_clipboardCapable = m_clipboardPolicy && peerSupportsClipboardText(object);
      m_windowDragCapable = peerSupportsWindowDragPreview(object);
      m_windowDragMotionCapable = m_windowDragCapable && peerSupportsWindowDragMotion(object);
      m_windowDragMoveCapable = m_windowDragMotionCapable && peerSupportsWindowDragMove(object);
      m_familyCapable = peerSupportsWindowFamily(object);
      if (m_hosting) {
        require(packet.type == MessageType::Hello && object["role"] == "viewer", "Expected viewer negotiation");
        m_native->setWindowFamiliesEnabled(m_familyCapable);
        m_established = true;
        if (!send(
                MessageType::Accepted,
                {{"version", static_cast<int>(protocolVersion)},
                 {"role", "host"},
                 {"codec", "h264"},
                 {"capabilities", windowFamilyCapabilities(
                                      windowDragMoveCapabilities(
                                          windowDragMotionCapabilities(
                                              windowDragPreviewCapabilities(
                                                  clipboardTextCapabilities(
                                                      m_keyboardCapable ? keyboardRouteCapabilities(
                                                                              m_inputCapable, m_mediaRecoveryCapable,
                                                                              m_mappedInputCapable
                                                                          )
                                                                        : QJsonArray{},
                                                      m_clipboardCapable
                                                  ),
                                                  m_windowDragCapable
                                              ),
                                              m_windowDragMotionCapable
                                          ),
                                          m_windowDragMoveCapable
                                      ),
                                      m_familyCapable
                                  )}}
            ))
          return;
        // Every new connection waits for enumeration rather than publishing a
        // prior peer's catalog. NativeHost revalidates identity again on export.
        clearCatalog();
        m_native->refresh();
        maybeInitialExport();
      } else {
        require(packet.type == MessageType::Accepted && object["role"] == "host", "Expected host negotiation");
        m_established = true;
      }
      m_negotiating = false;
      updateClipboard(true);
      updateControls();
      status("Connected to approved peer — window sharing enabled");
      return;
    }
    if (packet.type == MessageType::ClipboardText) {
      require(m_clipboard && m_clipboard->enabled(), "Focused text clipboard policy was not negotiated");
      m_clipboard->receive(parseClipboardText(packet));
      return;
    }
    if (packet.type == MessageType::KeyboardRoute || packet.type == MessageType::InputRoute) {
      receiveKeyboardRoute(packet);
      return;
    }
    if (packet.type == MessageType::Error) {
      status("Peer: " + parseObject(packet).value("message").toString().left(2048));
      return;
    }
    if (m_hosting)
      receiveHost(packet);
    else
      receiveViewer(packet);
  }
  void receiveHost(const Packet &packet)
  {
    if (m_shuttingDown)
      return;
    const auto object = parseObject(packet);
    if (packet.type == MessageType::Catalog || packet.type == MessageType::Export ||
        packet.type == MessageType::ReleaseAll) {
      require(packet.session == 0 && packet.epoch == 0, "Expected connection scope");
      if (packet.type == MessageType::Catalog)
        m_native->refresh();
      else if (packet.type == MessageType::ReleaseAll)
        returnAll();
      else {
        require(m_hostSessions.size() < 16, "Too many exported windows");
        const auto id = integer(object, "windowId", UINT32_MAX);
        require(id != 0, "Invalid window ID");
        m_native->exportWindow(static_cast<std::uint32_t>(id));
      }
      return;
    }
    const auto found = m_hostSessions.find(packet.session);
    // A delayed return/focus acknowledgement cannot address a newer incarnation.
    if (found == m_hostSessions.end() || found->second != packet.epoch)
      return;
    if (packet.type == MessageType::WindowDrag) {
      require(m_windowDragMotionCapable, "Window drag motion was not negotiated");
      if (object.value("kind") == QJsonValue("native-move")) {
        require(m_windowDragMoveCapable, "Native move observations were not negotiated");
        const auto observation = parseWindowDragMove(object);
        const auto geometry = m_hostGeometries.find(packet.session);
        if (geometry == m_hostGeometries.end() || geometry->second != observation.geometry)
          return;
        require(m_hostMoveGates[packet.session].accept(observation), "Native move changed its retained grab");
        m_nativeMoves[packet.session] = observation;
        const auto metadata = m_hostMetadata.find(packet.session);
        const auto callback = onWindowMove;
        if (callback && metadata != m_hostMetadata.end())
          callback(
              static_cast<std::uint32_t>(integer(metadata->second, "windowId", UINT32_MAX)),
              {packet.session, packet.epoch}, observation
          );
        return;
      }
      const auto reply = parseWindowDragPreviewReply(object);
      if (!m_hostDrag || m_hostDrag->owner != Owner{packet.session, packet.epoch})
        return;
      const auto command = m_hostDrag->command;
      require(
          reply.command.drag == command.drag && reply.command.geometry == command.geometry &&
              reply.command.sequence <= command.sequence,
          "Window drag preview reply changed its binding"
      );
      if (reply.command.sequence != command.sequence)
        return; // A coalesced older position cannot acknowledge the current drop.
      require(reply.command.samePosition(command), "Window drag preview reply changed its position");
      const auto binding = m_hostDrag->binding;
      const QPointer<Bridge> alive(this);
      if (reply.ok && reply.command.kind == WindowDragPreviewCommand::Kind::Drop) {
        require(
            command.kind == WindowDragPreviewCommand::Kind::Drop && m_hostDrag->release,
            "Unrequested window drag drop reply"
        );
        if (m_hostDrag->dropAccepted)
          return;
        m_hostDrag->dropAccepted = true;
        const auto release = *m_hostDrag->release;
        if (!m_native->releaseWindowDrag(binding, release)) {
          cancelWindowDrag(binding);
          return;
        }
        if (!alive)
          return;
        m_native->ready(packet.session, packet.epoch, reply.command.geometry);
        return;
      }
      if (reply.ok)
        m_hostDrag->painted = reply;
      const auto callback = onWindowDragPreview;
      if (callback)
        callback(binding, reply);
      if (alive && !reply.ok)
        cancelWindowDrag(binding);
      return;
    }
    if (packet.type == MessageType::Input && object.value("kind") == QLatin1String("video-recovery")) {
      // This is an authenticated, session/epoch-bound media-control request,
      // never an input/native authority. Reject injected fields and replayed
      // request IDs; an older geometry must not demand an IDR for a replacement.
      require(m_mediaRecoveryCapable, "Unnegotiated video recovery request");
      require(object.size() == 3, "Unexpected video recovery control fields");
      const auto geometry = integer(object, "geometry");
      const auto request = integer(object, "request");
      require(geometry && request, "Invalid video recovery request identity");
      const auto frame = m_hostGeometries.find(packet.session);
      if (frame == m_hostGeometries.end() || frame->second != geometry)
        return;
      auto &last = m_mediaRecoveryRequests[packet.session];
      if (request <= last)
        return;
      last = request;
      m_pendingKeyframes[packet.session] = packet.epoch;
      if (!m_keyframeRetry.isActive())
        m_keyframeRetry.start();
      return;
    }
    switch (packet.type) {
    case MessageType::Ready: {
      require(object.size() == 1, "Unexpected painted-frame acknowledgement fields");
      const auto geometry = integer(object, "geometry");
      require(geometry != 0, "Missing painted-frame geometry");
      const auto current = m_hostGeometries.find(packet.session);
      if (current != m_hostGeometries.end() && current->second == geometry)
        m_native->ready(packet.session, packet.epoch, geometry);
      break;
    }
    case MessageType::Input:
      if (object.value("kind") == QLatin1String("dismiss")) {
        require(
            m_familyCapable && object.size() == 2 && integer(object, "geometry") != 0,
            "Invalid or unnegotiated family dismissal"
        );
        const auto source = m_hostMetadata.find(packet.session);
        require(
            source != m_hostMetadata.end() && parseWindowFamily(source->second).has_value(),
            "Dismissal requires an attached window"
        );
      }
      m_native->input(packet.session, packet.epoch, object);
      break;
    case MessageType::Resize:
      m_native->resizeWindow(
          packet.session, packet.epoch, static_cast<int>(integer(object, "width", 8192)),
          static_cast<int>(integer(object, "height", 8192)), integer(object, "request")
      );
      break;
    case MessageType::Return:
      m_native->restore(packet.session);
      break;
    default:
      throw std::runtime_error("Message is not valid for the Mac host");
    }
  }
  void receiveViewer(const Packet &packet)
  {
    if (m_shuttingDown)
      return;
    if (packet.type == MessageType::Catalog) {
      require(packet.session == 0 && packet.epoch == 0, "Expected catalog connection scope");
      const auto value = parseObject(packet).value("windows");
      require(value.isArray(), "Expected catalog window array");
      const auto windows = value.toArray();
      require(windows.size() <= 4096, "Catalog too large");
      displayCatalog(windows);
      const bool firstCatalog = !m_catalogReady;
      m_catalogReady = true;
      if (firstCatalog && m_established)
        m_stableTimer.start(m_stableInterval);
      updateControls();
      maybeInitialExport();
      return;
    }
    if (packet.type == MessageType::Opened) {
      require(packet.session > 0 && packet.epoch > 0, "Invalid window session");
      const auto metadata = parseObject(packet);
      const auto family = parseWindowFamily(metadata);
      require(!family || m_familyCapable, "Window families were not negotiated");
      require(!family || metadata.value("windowDrag").toDouble(0) == 0, "Attached window cannot own a window drag");
      m_families.open({packet.session, packet.epoch}, family);
      require(
          metadata.value("windowDrag").toDouble(0) == 0 || m_windowDragCapable,
          "Window drag previews were not negotiated"
      );
      auto found = m_viewers.find(packet.session);
      if (found != m_viewers.end()) {
        require(found->second.epoch == packet.epoch, "Duplicate window session");
        found->second.window->applyMetadata(metadata);
        return;
      }
      require(m_viewers.size() < 16, "Too many viewer windows");
      QPointer<Viewer> familyParent;
      if (family) {
        const auto parent = m_viewers.find(family->parent.session);
        require(
            parent != m_viewers.end() && parent->second.window && parent->second.epoch == family->parent.epoch,
            "Attached viewer parent is unavailable"
        );
        familyParent = parent->second.window;
      }
      auto ownedViewer = std::make_unique<Viewer>(packet.session, packet.epoch, metadata);
      auto *viewer = ownedViewer.get();
      if (family)
        viewer->attachFamilyParent(familyParent);
      viewer->setMediaRecoveryEnabled(m_mediaRecoveryCapable);
      viewer->setAttribute(Qt::WA_DeleteOnClose);
      const auto id = packet.session;
      const auto generation = m_connectionGeneration;
      viewer->onSend = [this, viewer, generation](
                           MessageType type, const QJsonObject &object, std::uint64_t session, std::uint64_t epoch
                       ) {
        if (!currentViewer(session, epoch, viewer, generation))
          return false;
        return send(type, object, session, epoch);
      };
      viewer->onKeyboardRouteSend = [this, viewer, generation](const KeyboardRouteControl &control) {
        const auto owner = control.binding.owner;
        return currentViewer(owner.session, owner.epoch, viewer, generation) && sendKeyboardRoute(control);
      };
      viewer->onError = [this, id, epoch = packet.epoch, viewer, generation](QString error) {
        if (currentViewer(id, epoch, viewer, generation))
          status(error);
      };
      viewer->onReturned = [this, id, epoch = packet.epoch, viewer, generation] {
        forgetViewer(id, epoch, viewer, generation);
      };
      viewer->onDismissFamily = [this, id, epoch = packet.epoch, viewer, generation] {
        if (!currentViewer(id, epoch, viewer, generation))
          return;
        for (const auto &[childId, child] : m_viewers) {
          const auto *target = child.window.data();
          if (target && target->family() && target->family()->parent == Owner{id, epoch} &&
              target->family()->dismissible()) {
            send(
                MessageType::Input, {{"kind", "dismiss"}, {"geometry", number(target->geometryRevision())}}, childId,
                child.epoch
            );
            return;
          }
        }
      };
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
      viewer->onViewportInvalidated = [this, id, epoch = packet.epoch, viewer, generation] {
        if (currentViewer(id, epoch, viewer, generation) && m_viewportEndpoint)
          m_viewportEndpoint->invalidate();
      };
      viewer->onForwardingFenceChanged = [this, id, epoch = packet.epoch, viewer, generation] {
        if (currentViewer(id, epoch, viewer, generation) && m_viewportEndpoint)
          m_viewportEndpoint->forwardingFenceChanged();
      };
#endif
      connect(viewer, &QObject::destroyed, this, [this, id, epoch = packet.epoch, viewer, generation] {
        // QWidget emits this after Viewer members are gone, before its typed
        // QPointer necessarily clears. Retire the pointer without dereferencing
        // it before family cleanup can invoke any Viewer method.
        if (currentViewer(id, epoch, viewer, generation))
          m_viewers.at(id).window.clear();
        forgetViewer(id, epoch, viewer, generation);
      });
      m_viewers.emplace(id, View{packet.epoch, viewer, viewer});
      ownedViewer.release();
      const QPointer<Bridge> familyGuard(this);
      const QPointer<Viewer> childGuard(viewer);
      updateFamilyBlocks();
      if (!familyGuard || !childGuard || m_shuttingDown)
        return;
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
      if (!family && m_windowDragMoveCapable && m_viewportEndpoint) {
        const QPointer<Bridge> alive(this);
        const QPointer<Viewer> target(viewer);
        auto *watcher = new WindowDragWatcher(
            {[alive, target]() -> std::optional<GnomePreparationTarget> {
               if (!alive || !target || !alive->m_viewportEndpoint || !alive->m_viewportEndpoint->live())
                 return {};
               return target->waylandPreparationTarget();
             },
             [alive](auto request, auto complete) {
               return alive && alive->m_viewportEndpoint &&
                      alive->m_viewportEndpoint->requestWindowDrag(request, std::move(complete));
             },
             [alive, target, generation, id, epoch = packet.epoch](const WindowDragMove &observation) {
               if (alive && target && alive->currentViewer(id, epoch, target, generation))
                 alive->send(MessageType::WindowDrag, windowDragMoveJson(observation), id, epoch);
             }},
            viewer
        );
        m_viewers.at(id).watcher = watcher;
      }
      if (m_preparationRoute && !viewer->beginInputPreparation(m_preparationRoute->binding)) {
        if (m_viewportEndpoint)
          m_viewportEndpoint->close(QStringLiteral("new-viewer-preparation-gate-unavailable"));
        return;
      }
      if (m_viewportEndpoint)
        m_viewportEndpoint->invalidate(QStringLiteral("viewer-set-changed"));
      if (m_shuttingDown)
        return;
#endif
      if (!family)
        viewer->placeOnScreen(m_screen);
      viewer->show();
      return;
    }
    auto found = m_viewers.find(packet.session);
    if (found == m_viewers.end() || found->second.epoch != packet.epoch || !found->second.window)
      return;
    auto *viewer = found->second.window.data();
    switch (packet.type) {
    case MessageType::WindowDrag: {
      require(m_windowDragMotionCapable && nativeWindowDragAvailable(), "Native window drag was not negotiated");
      const auto command = parseWindowDragPreviewCommand(parseObject(packet));
      require(
          viewer->provisionalWindowDrag() && viewer->windowDragId() == command.drag,
          "Window drag control changed the exact preview"
      );
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
      auto *controller = found->second.drag.data();
      if (!controller) {
        const QPointer<Bridge> alive(this);
        const QPointer<Viewer> target(viewer);
        const auto generation = m_connectionGeneration;
        controller = new WindowDragPreviewController(
            {[target]() -> std::optional<WindowDragPreviewController::Paint> {
               const auto paint = target ? target->waylandDragTarget() : std::nullopt;
               if (!paint || !target->provisionalWindowDrag())
                 return {};
               return WindowDragPreviewController::Paint{
                   paint->windowAssociation, paint->geometry, target->framePaintSerial(), paint->surfaceIncarnation,
                   target->size()
               };
             },
             [target] {
               if (target)
                 target->update();
             },
             [alive](auto request, auto finished) {
               return alive && alive->m_viewportEndpoint &&
                      alive->m_viewportEndpoint->requestWindowDrag(request, std::move(finished));
             },
             [alive, target, generation, id = packet.session, epoch = packet.epoch](const auto &reply) {
               if (alive && target && alive->currentViewer(id, epoch, target, generation))
                 alive->send(MessageType::WindowDrag, windowDragPreviewJson(reply), id, epoch);
             }},
            viewer
        );
        found->second.drag = controller;
      }
      controller->receive(command);
#endif
      break;
    }
    case MessageType::Frame: {
      const auto frame = parseFramePayload(packet);
      viewer->receiveFrame(frame.accessUnit, frame.geometry);
      break;
    }
    case MessageType::Accepted:
      require(parseObject(packet)["kind"] == "committed", "Invalid session acknowledgement");
      viewer->committed();
      break;
    case MessageType::Focus: {
      const auto object = parseObject(packet);
      viewer->focusAcknowledged(integer(object, "focusRevision"), integer(object, "request"));
      break;
    }
    case MessageType::Closed:
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
      if (m_viewportEndpoint)
        m_viewportEndpoint->invalidate(QStringLiteral("viewer-closed"));
      if (m_shuttingDown)
        return;
#endif
      closeViewerFamily({packet.session, packet.epoch});
      break;
    default:
      throw std::runtime_error("Message is not valid for the viewer");
    }
  }
  void displayCatalog(const QJsonArray &windows)
  {
    m_catalog->clear();
    for (const auto &value : windows) {
      const auto window = value.toObject();
      const auto id = integer(window, "id", UINT32_MAX);
      auto *item = new QListWidgetItem(window["title"].toString().left(512), m_catalog);
      item->setData(Qt::UserRole, static_cast<quint32>(id));
    }
  }
  void maybeInitialExport()
  {
    if (m_shuttingDown || !m_established || !m_catalogReady || m_initialWindow == 0)
      return;
    const auto id = m_initialWindow;
    m_initialWindow = 0;
    if (m_native)
      m_native->exportWindow(id);
    else
      send(MessageType::Export, {{"windowId", static_cast<qint64>(id)}});
  }
  void openSelected()
  {
    if (m_shuttingDown || !m_established || !m_catalogReady || m_cleaning) {
      status("Connect an approved peer before sharing a window.");
      return;
    }
    auto *item = m_catalog->currentItem();
    if (!item)
      return;
    const auto id = item->data(Qt::UserRole).toUInt();
    if (m_native)
      m_native->exportWindow(id);
    else
      send(MessageType::Export, {{"windowId", static_cast<qint64>(id)}});
  }
  void returnAll()
  {
    if (m_shuttingDown)
      return;
    if (m_native) {
      clearKeyframeRetries();
      m_native->releaseAll();
      m_native->restoreAll();
    } else {
      send(MessageType::ReleaseAll);
      stopViewers();
    }
  }
  void stopViewers()
  {
    if (m_returnDrag)
      cancelWindowReturn(m_returnDrag->binding);
    auto viewers = std::move(m_viewers);
    m_viewers.clear();
    m_families.clear();
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
    if (m_viewportEndpoint)
      m_viewportEndpoint->invalidate(QStringLiteral("viewer-set-changed"));
#endif
    for (auto &[id, view] : viewers)
      if (view.window) {
        view.window->onReturned = {};
        view.window->onSend = {};
        view.window->onKeyboardRouteSend = {};
        view.window->onError = {};
        view.window->onViewportInvalidated = {};
        view.window->onForwardingFenceChanged = {};
        view.window->disconnected();
        view.window->close();
      }
  }
  bool currentViewer(std::uint64_t session, std::uint64_t epoch, const Viewer *window, std::uint64_t generation) const
  {
    const auto found = m_viewers.find(session);
    return !m_shuttingDown && m_connectionGeneration == generation && found != m_viewers.end() &&
           found->second.epoch == epoch && found->second.identity == window;
  }
  void forgetViewer(std::uint64_t session, std::uint64_t epoch, const Viewer *window, std::uint64_t generation)
  {
    if (currentViewer(session, epoch, window, generation)) {
      // A local Return concerns the complete source family. Native child
      // destruction uses Closed and retires only that child subtree instead.
      const auto root = m_families.root({session, epoch});
      const QPointer<Bridge> alive(this);
      closeViewerFamily(root.session ? root : Owner{session, epoch});
      if (!alive)
        return;
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
      if (m_viewportEndpoint)
        m_viewportEndpoint->invalidate(QStringLiteral("viewer-returned"));
#endif
    }
  }
  void updateFamilyBlocks()
  {
    std::vector<std::pair<QPointer<Viewer>, bool>> updates;
    for (const auto &[id, view] : m_viewers)
      updates.emplace_back(view.window, m_families.blocked({id, view.epoch}));
    const QPointer<Bridge> alive(this);
    for (const auto &[viewer, blocked] : updates) {
      if (viewer)
        viewer->setFamilyBlocked(blocked);
      if (!alive)
        return;
    }
  }
  void closeViewerFamily(Owner owner)
  {
    const auto removed = m_families.close(owner);
    std::vector<QPointer<Viewer>> windows;
    // Remove every authority first; close/release callbacks may reenter Bridge.
    for (const auto member : removed) {
      const auto it = m_viewers.find(member.session);
      if (it != m_viewers.end() && it->second.epoch == member.epoch) {
        windows.push_back(it->second.window);
        m_viewers.erase(it);
      }
    }
    const QPointer<Bridge> alive(this);
    for (const auto &viewer : windows) {
      if (viewer) {
        viewer->onReturned = {};
        viewer->disconnected();
        if (viewer)
          viewer->close();
      }
      if (!alive)
        return;
    }
    updateFamilyBlocks();
  }
  struct View
  {
    std::uint64_t epoch;
    QPointer<Viewer> window;
    // The destroyed callback clears window before retiring the family. Keep an
    // opaque identity so a delayed callback cannot erase a successor.
    const Viewer *identity;
    QPointer<WindowDragPreviewController> drag{};
    QPointer<WindowDragWatcher> watcher{};
  };
  bool m_hosting, m_established = false, m_cleaning = false, m_catalogReady = false;
  const bool m_clipboardPolicy;
  bool m_negotiating = false, m_reconnectEnabled = false, m_retryRequested = false, m_shuttingDown = false;
  bool m_shutdownFinishing = false, m_shutdownComplete = false, m_quitAfterShutdown = false, m_quitNotified = false;
  std::function<void()> m_onSharingStopped;
  Identity m_identity;
  QByteArray m_pin;
  int m_screen;
  std::uint32_t m_initialWindow;
  QLabel *m_status = nullptr;
  QListWidget *m_catalog = nullptr;
  QPushButton *m_open = nullptr, *m_refresh = nullptr, *m_retry = nullptr, *m_cancel = nullptr;
  Listener m_listener;
  QPointer<Channel> m_channel;
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
  QPointer<ViewerViewportEndpoint> m_viewportEndpoint;
  struct PreparationRoute
  {
    InputPreparationBinding binding;
    std::uint64_t connection = 0;
    std::optional<InputCutoffBinding> consumed;
    std::optional<Owner> focusIntent;
  };
  std::optional<PreparationRoute> m_preparationRoute;
#endif
  QTimer m_keyframeRetry;
  QTimer m_reconnectTimer, m_stableTimer;
  QTimer m_keyboardTimeout;
  int m_keyboardTimeoutMs = 3000;
  bool m_keyboardCapable = false;
  bool m_inputCapable = false;
  bool m_mappedInputCapable = false;
  bool m_mediaRecoveryCapable = false;
  bool m_clipboardCapable = false;
  bool m_windowDragCapable = false;
  bool m_windowDragMotionCapable = false;
  bool m_windowDragMoveCapable = false;
  bool m_familyCapable = false;
  WindowFamilies m_families;
  std::unique_ptr<FocusedClipboard> m_clipboard;
  QMetaObject::Connection m_clipboardConnection;
  bool m_reservingKeyboard = false;
  std::uint64_t m_keyboardGeneration = 0;
  std::optional<KeyboardAttempt> m_keyboardRoute;
  std::optional<KeyboardRouteBinding> m_retiredKeyboardRoute;
  QString m_connectHost, m_disconnectReason;
  quint16 m_connectPort = 0;
  std::uint64_t m_connectionGeneration = 0;
  std::size_t m_retryStep = 0;
  std::array<int, 6> m_retryDelays{1000, 2000, 4000, 8000, 16000, 30000};
  int m_stableInterval = 30000;
  std::unique_ptr<NativeHost> m_native;
  std::map<std::uint64_t, std::uint64_t> m_hostSessions;
  std::map<std::uint64_t, std::uint64_t> m_hostGeometries;
  std::map<std::uint64_t, WindowDragMoveGate> m_hostMoveGates;
  std::map<std::uint64_t, QJsonObject> m_hostMetadata;
  std::map<std::uint64_t, WindowDragMove> m_nativeMoves;
  struct ReturnDrag
  {
    WindowDragBinding binding;
    Owner owner;
    WindowDragMove native;
    LocalWindowDrag command;
    Point anchor;
    Rect frame;
    QPointer<Viewer> preview;
    std::uint64_t beforePaint = 0, acknowledged = 0;
    bool drop = false, committing = false;
  };
  std::optional<ReturnDrag> m_returnDrag;
  std::map<std::uint64_t, QPointF> m_dragAnchors;
  struct HostDrag
  {
    WindowDragBinding binding;
    Owner owner;
    WindowDragPreviewCommand command;
    std::optional<WindowDragPreviewReply> painted;
    std::optional<WindowDragRelease> release;
    bool dropAccepted = false;
  };
  std::optional<HostDrag> m_hostDrag;
  std::map<std::uint64_t, std::uint64_t> m_mediaRecoveryRequests;
  std::map<std::uint64_t, std::uint64_t> m_pendingKeyframes;
  // Round-robin cursor across live session IDs, not the last keyframe's epoch.
  // Reset only on terminal channel cleanup, not between successive retries.
  std::uint64_t m_lastKeyframeRecoverySession = 0;
  std::map<std::uint64_t, View> m_viewers;
  QJsonArray m_lastCatalog;
};

// Private-channel loss is terminal, including when observed before exec(). The
// latch guards startup; the queued quit survives the usual pre-exec quit race.
// An EOF still waiting in Qt's event queue is not yet an observed cancellation.
class HostShutdownGuard final : public QObject
{
public:
  explicit HostShutdownGuard(std::function<void()> quit) : m_quit(std::move(quit))
  {
  }
  bool requested() const
  {
    return m_requested;
  }
  void attach(Bridge &bridge)
  {
    m_bridge = &bridge;
    const QPointer<HostShutdownGuard> alive(this);
    bridge.m_onSharingStopped = [alive] {
      if (alive && alive->m_requested)
        alive->queueQuit();
    };
    if (m_requested)
      bridge.stopSharing();
  }
  void request()
  {
    if (m_requested)
      return;
    m_requested = true;
    const QPointer<HostShutdownGuard> alive(this);
    if (m_bridge)
      m_bridge->stopSharing();
    if (alive)
      queueQuit();
  }

private:
  void queueQuit()
  {
    if (m_quitQueued || m_quitDelivered)
      return;
    m_quitQueued = true;
    QTimer::singleShot(0, this, [this] {
      m_quitQueued = false;
      // A nested event loop inside revocation must not make quit overtake
      // release/restore. Completion calls queueQuit again after it unwinds.
      if (m_bridge && !m_bridge->m_shutdownComplete)
        return;
      m_quitDelivered = true;
      const auto quit = m_quit;
      if (quit)
        quit();
    });
  }
  QPointer<Bridge> m_bridge;
  std::function<void()> m_quit;
  bool m_requested = false, m_quitQueued = false, m_quitDelivered = false;
};

void configureApplicationLifetime(QApplication &application, Bridge &bridge, std::function<void()> quit)
{
  application.setQuitOnLastWindowClosed(false);
  bridge.onQuitRequested = std::move(quit);
}
} // namespace

#ifndef WINDOWSHARE_BRIDGE_TEST
int main(int argc, char **argv)
{
  try {
#ifdef __APPLE__
    // This exact private entry point deliberately runs before Qt/GUI,
    // transport identity parsing, permissions and any native window action.
    if (native::recoveryWatchdogMode(argc, argv))
      return native::runRecoveryWatchdog(argc, argv);
#endif
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
    auto viewportBootstrap = ViewerViewportBootstrap::inspect(argc, argv);
#endif
#ifdef WINDOWSHARE_LOCAL_INPUT
    auto inputBootstrap = LocalInputHostBootstrap::inspect(argc, argv);
#ifdef __APPLE__
    // The inherited Host is hidden before it has an export, including during
    // the input-disabled admission probe. Keep its private-channel timers
    // responsive from startup through teardown, not only while sharing a
    // window. This token grants no input authority and allows normal sleep.
    std::unique_ptr<LocalInputActivity> inputActivity;
    if (inputBootstrap.inherited()) {
      inputActivity = std::make_unique<LocalInputActivity>();
      lifecycleDiagnostic(
          "Host input", "input.process-activity", "observed",
          "Requested interactive scheduling for the inherited private session", {{"active", inputActivity->active()}}
      );
    }
#endif
#endif
    ViewerApplication app(argc, argv);
    // Declared before coreInput and Bridge: its callbacks remain valid through
    // both destructors, and the attached controller is held only by QPointer.
    HostShutdownGuard shutdown([&app] { app.quit(); });
    app.setApplicationName("DeskflowWindowShare");
    app.setOrganizationName("Deskflow");
    app.setApplicationVersion("0.1-experimental");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.setApplicationDescription(
        "Experimental per-window sharing. Separate from the working Deskflow installation."
    );
    parser.addOptions(
        {{"session-controls", "Open persistent pairing and Start/Stop controls without starting input."},
         {"enable-seamless", "Explicitly enable experimental window sharing."},
         {"holding-display", "Enable the experimental private virtual holding display on the Mac."},
         {"share-clipboard-text",
          "Share new plain-text clipboard changes only for the focused exported window; both peers must opt in."},
         {"capture-fps", "Mac host foreground capture rate, 30 or 60; viewers do not override their peer.", "fps",
          "30"},
         {"listen", "Listen on this Mac interface address.", "address"},
         {"connect", "Connect to the approved Mac host.", "host"},
         {"port", "Separate TLS control/media port.", "port", "24810"},
         {"identity", "Private identity directory.", "directory",
          QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/identity"},
         {"peer-pin", "Approved SHA-256 certificate fingerprint of the other computer.", "fingerprint"},
         {"init-identity", "Create a new private TLS identity; refuses overwrite."},
         {"fingerprint", "Print this identity's public certificate fingerprint."},
         {"probe", "Report macOS capture and Accessibility permissions without requesting them."},
         {"request-permissions", "Ask macOS for capture and Accessibility access."},
         {"recover", "Restore journaled windows without opening a network connection."},
         {"journal", "Persistent restore journal path.", "file",
          QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/restore.json"},
         {"screen", "Viewer screen index (zero based).", "index", "0"},
         {"export-window", "Explicit window ID to export once connected.", "id", "0"},
         {"quit-after", "Exit cleanly after this many seconds (diagnostics).", "seconds", "0"},
         {"headless", "Do not show the connection window (viewer windows still appear)."}}
    );
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
    QCommandLineOption viewportDescriptor(
        "gnome-viewport-fd", "Inherited viewport observation descriptor.", "descriptor"
    );
    viewportDescriptor.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(viewportDescriptor);
#endif
#ifdef WINDOWSHARE_LOCAL_INPUT
    QCommandLineOption inputDescriptor("core-input-fd", "Inherited private descriptor.", "descriptor");
    QCommandLineOption inputProbe("core-input-probe", "Private handshake diagnostic.");
    QCommandLineOption inputPeerPairs("core-input-peer-pairs", "Configured private input peer pairs.", "json");
    QCommandLineOption inputEvidenceOption(
        "input-evidence-config", "Explicit private bounded test evidence config.", "file"
    );
    inputEvidenceOption.setFlags(QCommandLineOption::HiddenFromHelp);
    inputDescriptor.setFlags(QCommandLineOption::HiddenFromHelp);
    inputProbe.setFlags(QCommandLineOption::HiddenFromHelp);
    inputPeerPairs.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOptions({inputDescriptor, inputProbe, inputPeerPairs, inputEvidenceOption});
#endif
    // Validate inherited-mode combinations before help/version or any identity,
    // recovery, permission, network, or NativeHost action can run.
    if (!parser.parse(app.arguments()))
      throw std::runtime_error(parser.errorText().toStdString());
#ifdef WINDOWSHARE_LOCAL_INPUT
    inputBootstrap.validateOptions(parser.optionNames());
#endif
    if (parser.isSet("session-controls")) {
      require(argc == 2 && app.arguments().at(1) == "--session-controls", "Use --session-controls alone");
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
      viewportBootstrap.validateOptions(parser.optionNames());
#endif
      initializeNativeApplication();
      SessionControls controls(SessionControls::packagedTools());
      controls.show();
      return app.exec();
    }
    if (parser.isSet("enable-seamless") && (parser.isSet("listen") != parser.isSet("connect")))
      initializeNativeApplication();
#ifdef WINDOWSHARE_LOCAL_INPUT
    // inputBootstrap retains the trusted core's immutable explicit peer map.
    // An absent/empty map denies every pair; no controller or input authorizer
    // is installed until all real capture/delivery/admission providers exist.
    std::unique_ptr<InputEvidenceRuntime> inputEvidenceRuntime;
    std::unique_ptr<LocalInputChannel> coreInput;
    std::unique_ptr<LocalInputCodeAdmission> inputAdmission;
    QTimer inputAdmissionTimer;
    if (inputBootstrap.inherited()) {
      coreInput = inputBootstrap.adopt([&] {
        if (parser.isSet(inputEvidenceOption))
          inputEvidenceRuntime = InputEvidenceRuntime::open(parser.value(inputEvidenceOption), InputEvidenceRole::Host);
      });
      if (inputBootstrap.probe())
        return localInputHostProbe(std::move(coreInput));
      coreInput->onClosed = [&shutdown, diagnostics = bool(inputEvidenceRuntime)](const QString &reason) {
        shutdown.request();
        if (diagnostics)
          QTextStream(stderr) << "Window sharing private input channel closed in Host: " << reason.left(512) << '\n';
      };
      if (!inputBootstrap.peerPolicy().empty()) {
        // Run the optional inspection only after the ordinary private handshake.
        // Expensive Security work never blocks this event loop or input hooks.
        coreInput->onAuthenticated = [&] {
          QString admissionError;
          inputAdmission = LocalInputCodeAdmission::request(*coreInput, &admissionError);
          if (inputAdmission)
            inputAdmissionTimer.start(100);
          else
            QTextStream(stderr) << "Window sharing code admission unavailable: " << admissionError << '\n';
        };
        QObject::connect(&inputAdmissionTimer, &QTimer::timeout, &inputAdmissionTimer, [&] {
          const auto state = inputAdmission->state();
          if (state == LocalInputCodeAdmission::State::Pending)
            return;
          inputAdmissionTimer.stop();
          if (state == LocalInputCodeAdmission::State::Ready)
            QTextStream(stderr) << "Window sharing exact library admission verified; original input requires an "
                                   "authenticated mapped route\n";
          else
            QTextStream(stderr) << "Window sharing code admission unavailable: " << inputAdmission->error() << '\n';
        });
      }
    }
#endif
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
    if (!parser.parse(app.arguments()))
      throw std::runtime_error(parser.errorText().toStdString());
    viewportBootstrap.validateOptions(parser.optionNames());
#endif
    parser.process(app);
    const auto captureRate = captureRateOption(parser);
    if (parser.isSet("init-identity"))
      initializeIdentity(parser.value("identity"));
    if (parser.isSet("init-identity") || parser.isSet("fingerprint")) {
      QTextStream(stdout) << Identity::load(parser.value("identity")).fingerprint() << '\n';
      return 0;
    }
    if (parser.isSet("request-permissions"))
      NativeHost::requestPermissions();
    if (parser.isSet("probe") || parser.isSet("request-permissions")) {
      QTextStream(stdout) << QJsonDocument(NativeHost::permissions()).toJson(QJsonDocument::Compact) << '\n';
      return 0;
    }
    const bool hosting = parser.isSet("listen");
    require(
        parser.isSet("recover") || (parser.isSet("enable-seamless") && hosting != parser.isSet("connect")),
        "Use --enable-seamless and exactly one of --listen ADDRESS or --connect HOST. See --help."
    );
    std::unique_ptr<QLockFile> journalLock;
    if (hosting || parser.isSet("recover")) {
      const auto journal = QFileInfo(parser.value("journal")).absoluteFilePath();
      require(QDir().mkpath(QFileInfo(journal).absolutePath()), "Cannot create journal directory");
      journalLock = std::make_unique<QLockFile>(journal + ".lock");
      journalLock->setStaleLockTime(0);
      require(journalLock->tryLock(), "Another window host owns this restore journal");
    }
    if (parser.isSet("recover")) {
      NativeHost native(parser.value("journal"));
      native.onError = [](const QString &error) { QTextStream(stderr) << error << '\n'; };
      return native.recover() ? 0 : 1;
    }
#ifdef __APPLE__
    // Arm an independent recovery process while we hold the exclusive
    // journal lock and *before* a source may be relocated to the holding
    // display. Startup must fail closed if the child cannot acknowledge its
    // pipe. Its recovery uses the existing exact-identity journal reader.
    std::unique_ptr<native::RecoveryWatchdog> watchdog;
    if (hosting) {
      std::string watchdogError;
      watchdog = std::make_unique<native::RecoveryWatchdog>(
          QCoreApplication::applicationFilePath().toStdString(),
          QFileInfo(parser.value("journal")).absoluteFilePath().toStdString(), &watchdogError
      );
      require(watchdog->armed(), watchdogError.c_str());
    }
#endif
    // Install shutdown handling before native startup so a core-requested stop
    // always follows the host's ordinary window restoration path.
    std::signal(SIGINT, interruptHandler);
    std::signal(SIGTERM, interruptHandler);
    bool valid = false;
    const auto port = parser.value("port").toUInt(&valid);
    require(valid && port > 0 && port <= 65535, "Invalid port");
    const auto screen = parser.value("screen").toInt(&valid);
    require(valid && screen >= 0, "Invalid screen index");
    const auto window = parser.value("export-window").toUInt(&valid);
    require(valid, "Invalid window ID");
    const auto seconds = parser.value("quit-after").toInt(&valid);
    require(valid && seconds >= 0 && seconds <= 86400, "Invalid exit timer");
    if (interrupted)
      shutdown.request();
    if (shutdown.requested())
      return 0;
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
    // The private owned GNOME startup alone opts into QPA depth observation.
    // Unsupported Qt/plugin builds or a foreign handler leave InputDrain
    // unavailable while ordinary viewport observations keep their behavior.
    if (viewportBootstrap.inherited())
      (void)app.enableInputDrainDispatch();
#endif
    Bridge bridge(
        hosting, Identity::load(parser.value("identity")), parseFingerprint(parser.value("peer-pin")),
        parser.value("journal"), screen, window, parser.isSet("holding-display"), parser.isSet("share-clipboard-text"),
        captureRate
    );
    configureApplicationLifetime(app, bridge, [&shutdown] { shutdown.request(); });
    shutdown.attach(bridge);
#ifdef WINDOWSHARE_LOCAL_INPUT
    std::unique_ptr<LocalInputPreflight> inputPreflight;
    std::unique_ptr<LocalWindowDragHost> windowDrag;
    std::optional<LocalInputLease> retiringInput;
    const auto inputBinding = [](const LocalInputLease &lease) {
      return KeyboardRouteBinding{{lease.session, lease.epoch}, lease.keyboardRouteGeneration, lease.focusRequest,
                                  lease.focusRevision,          lease.geometryRevision,        lease.scope};
    };
    if (coreInput && hosting && !inputBootstrap.peerPolicy().empty()) {
      windowDrag = std::make_unique<LocalWindowDragHost>(
          *coreInput, inputBootstrap.peerPolicy(),
          LocalWindowDragHost::Providers{
              [&] {
                const auto proof = inputAdmission ? inputAdmission->proof() : std::nullopt;
                return proof && proof->live();
              },
              [&] { return bridge.authenticatedPeerObservation(); },
              [&](const WindowDragBinding &binding, const LocalWindowDrag &command) {
                return bridge.prepareWindowDrag(binding, command);
              },
              [&](const WindowDragBinding &binding, const LocalWindowDrag &command) {
                bridge.moveWindowDrag(binding, command);
              },
              [&](const WindowDragBinding &binding) { bridge.cancelWindowDrag(binding); },
              [&](const WindowDragBinding &binding, const LocalWindowDrag &command, const WindowDragRelease &release) {
                return bridge.dropWindowDrag(binding, command, release);
              },
              [&](const WindowDragBinding &binding) { bridge.finishWindowDrag(binding); }
          }
      );
      bridge.onWindowMove = [&windowDrag](std::uint32_t window, Owner owner, const WindowDragMove &move) {
        if (!windowDrag)
          return;
        const auto &native = move.native;
        windowDrag->moved(
            {static_cast<LocalWindowMove::Phase>(static_cast<unsigned>(native.phase) + 1),
             window,
             0,
             owner.session,
             owner.epoch,
             move.geometry,
             native.grab,
             native.sequence,
             {},
             native.x,
             native.y,
             {native.frame.x(), native.frame.y(), native.frame.width(), native.frame.height()}}
        );
      };
      bridge.onWindowDragPreview =
          [&windowDrag](const WindowDragBinding &binding, const WindowDragPreviewReply &reply) {
            if (!windowDrag)
              return;
            if (reply.ok)
              windowDrag->ready(binding, reply.command.sequence);
            else
              windowDrag->cancelled(binding);
          };
      bridge.onWindowDragCommitted = [&windowDrag](const WindowDragBinding &binding) {
        if (windowDrag)
          windowDrag->committed(binding);
      };
      bridge.onWindowDragCancelled = [&windowDrag](const WindowDragBinding &binding) {
        if (windowDrag)
          windowDrag->cancelled(binding);
      };
      coreInput->onWindowPointer = [&bridge, &windowDrag](const LocalWindowPointer &pointer) {
        if (windowDrag)
          windowDrag->pointer(pointer);
        bridge.observeWindowPointer(pointer);
      };
      LocalInputPreflight::Checks checks;
      checks.codeAdmission = [&](const LocalInputLease &) {
        const auto proof = inputAdmission ? inputAdmission->proof() : std::nullopt;
        return proof && proof->live();
      };
      checks.deliveryReady = [&](const LocalInputLease &lease) {
        const auto binding = inputBinding(lease);
        return bridge.originalInputRouteReady(binding);
      };
      // Core independently proves the captured fresh transition. The host
      // proves its own old delivered owners are released under the exact All
      // gate; neither endpoint pretends this is a global physical snapshot.
      checks.acquisitionBoundary = [&](const LocalInputLease &lease) {
        return bridge.originalInputBoundary(inputBinding(lease));
      };
      checks.release = [&](const LocalInputLease &lease, LocalInputRevocation) {
        retiringInput = lease;
        bridge.releaseNativeOriginalInput(inputBinding(lease));
        bridge.withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
        return true; // Bridge keeps the Viewer fenced if native cleanup failed.
      };
      LocalInputPreflight::Host host;
      host.connection = [&] { return bridge.authenticatedPeerObservation(); };
      host.reserve = [&](ForwardingScope scope) {
        return scope == ForwardingScope::All && coreInput->supportsMappedInput() ? bridge.reserveOriginalKeyboard(scope)
                                                                                 : std::nullopt;
      };
      host.startQuiescence = [&](const auto &binding) { return bridge.startOriginalKeyboard(binding); };
      host.isQuiesced = [&](const auto &binding) { return bridge.originalInputBoundary(binding); };
      host.activate = [&](const auto &binding) {
        const auto lease = inputPreflight ? inputPreflight->lease() : std::nullopt;
        return lease && inputBinding(*lease) == binding &&
               bridge.activateOriginalKeyboard(binding, lease->captureEpoch);
      };
      host.isActive = [&](const auto &binding) {
        const auto lease = inputPreflight ? inputPreflight->lease() : std::nullopt;
        if (!lease || inputBinding(*lease) != binding)
          return false;
        // Commit still observes fresh native authority. Once active, this
        // admits only construction of a pending event, never its submission.
        // deliverNativeOriginalInput performs the full current native check
        // after construction, retaining the original mapped receipt deadline.
        return inputPreflight->phase() == LocalInputPreflight::Phase::Active
                   ? bridge.nativeOriginalInputPending(binding, lease->captureEpoch)
                   : bridge.nativeOriginalInputCurrent(binding, lease->captureEpoch);
      };
      inputPreflight =
          LocalInputPreflight::host(*coreInput, inputBootstrap.peerPolicy(), std::move(host), std::move(checks));
      bridge.revokeOriginalKeyboard = [&](const auto &binding, auto) {
        bridge.releaseNativeOriginalInput(binding);
        if (inputPreflight)
          inputPreflight->cancel(LocalInputRevocation::AuthorityLost);
        return true;
      };
      bridge.onKeyboardResumed = [&](const auto &binding) {
        if (!retiringInput || inputBinding(*retiringInput) != binding)
          return;
        const auto lease = std::exchange(retiringInput, {});
        if (!coreInput->sendReleased(*lease))
          coreInput->close("Completed native input release could not be acknowledged");
      };
      bridge.onKeyboardQuiesced = [&](const auto &binding) {
        const auto lease = inputPreflight ? inputPreflight->lease() : std::nullopt;
        const auto target = bridge.originalInputTarget(binding);
        if (!lease || inputBinding(*lease) != binding || !target || !coreInput->sendInputTarget(*lease, *target)) {
          if (inputPreflight)
            inputPreflight->cancel(LocalInputRevocation::AuthorityLost);
          return;
        }
        inputPreflight->quiesced(binding);
      };
      coreInput->onOriginal = [&](const auto &, const auto &) {
        coreInput->close("Production original input requires an exact mapped receipt");
      };
      coreInput->onMappedOriginal = [&](const auto &lease, const auto &record, const auto &point) {
        if (!inputPreflight || inputPreflight->phase() != LocalInputPreflight::Phase::Active ||
            inputPreflight->lease() != lease ||
            !bridge.deliverNativeOriginalInput(inputBinding(lease), record, point)) {
          if (inputPreflight)
            inputPreflight->cancel(LocalInputRevocation::AuthorityLost);
        }
      };
    }
    const auto stopInput = qScopeGuard([&] {
      if (windowDrag)
        windowDrag->stop();
      bridge.onWindowMove = {};
      bridge.onWindowDragPreview = {};
      bridge.onWindowDragCommitted = {};
      bridge.onWindowDragCancelled = {};
      if (inputPreflight)
        inputPreflight->stop();
      bridge.revokeOriginalKeyboard = {};
      bridge.onKeyboardQuiesced = {};
      bridge.onKeyboardResumed = {};
      if (coreInput) {
        coreInput->onWindowPointer = {};
        coreInput->onWindowDrag = {};
        coreInput->onOriginal = {};
        coreInput->onMappedOriginal = {};
      }
    });
#endif
#ifdef __APPLE__
    QTimer watchdogHealth;
    if (watchdog) {
      QObject::connect(&watchdogHealth, &QTimer::timeout, &shutdown, [&shutdown, &watchdog] {
        // A nested event loop during restoration must not keep extending a
        // stuck shutdown. The monitor also outlives Bridge's native destructors.
        if (shutdown.requested())
          return;
        if (!watchdog->heartbeat()) {
          QTextStream(stderr) << "Window sharing recovery watchdog or Host progress lost; restoring all exports\n";
          shutdown.request();
        }
      });
      watchdogHealth.start(250);
    }
#endif
#ifdef WINDOWSHARE_VIEWPORT_ENDPOINT
    std::unique_ptr<ViewerViewportEndpoint> viewport;
    if (viewportBootstrap.inherited()) {
      viewport = std::make_unique<ViewerViewportEndpoint>(viewportBootstrap.takeDescriptor());
      viewport->onClosed = [&shutdown, endpoint = viewport.get()] {
        // close() has already retired the descriptor and all retained gates.
        // Report before shutdown can synchronously destroy the Bridge/endpoint.
        QTextStream(stderr) << "Window sharing GNOME viewport closed: " << endpoint->closeReason() << '\n';
        shutdown.request();
      };
      bridge.attachViewportEndpoint(*viewport);
    }
#endif
    if (interrupted)
      shutdown.request();
    if (shutdown.requested())
      return 0;
    if (hosting) {
      QHostAddress address;
      require(address.setAddress(parser.value("listen")), "Listen address must be an explicit IP address");
      if (!bridge.listen(address, static_cast<quint16>(port)))
        return 1;
    } else
      bridge.connectTo(parser.value("connect"), static_cast<quint16>(port));
    if (shutdown.requested())
      return 0;
    if (!parser.isSet("headless"))
      bridge.show();
    if (seconds)
      QTimer::singleShot(seconds * 1000, &shutdown, [&shutdown] { shutdown.request(); });
    QTimer signals;
    QObject::connect(&signals, &QTimer::timeout, &shutdown, [&shutdown] {
      if (interrupted)
        shutdown.request();
    });
    signals.start(100);
    if (interrupted)
      shutdown.request();
    if (shutdown.requested())
      return 0;
    return app.exec();
  } catch (const std::exception &error) {
    QTextStream(stderr) << "Window sharing: " << error.what() << '\n';
    return 1;
  }
}
#endif
