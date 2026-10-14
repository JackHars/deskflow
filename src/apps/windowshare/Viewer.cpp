/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "Viewer.h"
#include "VideoDecoder.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QScreen>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#ifdef WINDOWSHARE_X11
#include <QAbstractNativeEventFilter>
#endif

namespace deskflow::windowshare {
namespace {
constexpr int maxPendingInput = 128;
constexpr int maxPendingFrames = 16;
constexpr qsizetype maxPendingVideoBytes = 32 * 1024 * 1024;
constexpr std::uint64_t maxJsonInteger = (std::uint64_t{1} << 53) - 1;

std::uint64_t revision(const QJsonValue &value)
{
  const double number = value.toDouble(-1);
  if (!value.isDouble() || !std::isfinite(number) || number < 1 || number > static_cast<double>(maxJsonInteger) ||
      std::floor(number) != number)
    return 0;
  return static_cast<std::uint64_t>(number);
}
QJsonValue jsonNumber(std::uint64_t value)
{
  return QJsonValue(static_cast<qint64>(value));
}
} // namespace

#ifdef WINDOWSHARE_X11
class ViewerX11Observer final : public QAbstractNativeEventFilter
{
public:
  explicit ViewerX11Observer(Viewer &viewer) : m_viewer(viewer)
  {
#if QT_CONFIG(xcb)
    // The offscreen plugin can expose QX11Application for GLX while returning
    // no XCB connection. Its synthetic window IDs are not native X11 surfaces.
    if (QGuiApplication::platformName() != QStringLiteral("xcb"))
      return;
    auto *native = qApp->nativeInterface<QNativeInterface::QX11Application>();
    auto *connection = native ? native->connection() : nullptr;
    if (connection) {
      m_observer = std::make_unique<X11DisplayObserver>(connection);
      m_coordinates = std::make_unique<X11CoordinateEndpoint>(*m_observer);
      qApp->installNativeEventFilter(this);
      m_timer.setInterval(10);
      QObject::connect(&m_timer, &QTimer::timeout, &viewer, [this] { poll(); });
      m_timer.start();
    }
#endif
  }
  ~ViewerX11Observer() override
  {
    m_timer.stop();
    if (qApp)
      qApp->removeNativeEventFilter(this);
    if (m_coordinates)
      m_coordinates->close();
    if (m_observer)
      m_observer->close();
  }
  bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override
  {
    if (m_observer && type == "xcb_generic_event_t" && message) {
      m_observer->handleEvent(*static_cast<const xcb_generic_event_t *>(message));
      m_coordinates->handleEvent(*static_cast<const xcb_generic_event_t *>(message));
    }
    return false;
  }
  void invalidate()
  {
    if (m_coordinates)
      m_coordinates->invalidate();
    if (m_observer)
      m_observer->invalidate();
  }
  void fenceChanged()
  {
    if (m_coordinates && m_pointRequest)
      m_coordinates->cancel(m_pointRequest);
    m_pointRequest = 0;
    m_pointFence.reset();
  }
  bool read(X11WitnessLocator locator, X11CoordinateClock::time_point deadline)
  {
    const auto viewport = m_viewer.viewportObservation();
    if (!m_coordinates || !viewport || !m_coordinates->read(locator, deadline))
      return false;
    m_pendingViewport = viewport;
    return true;
  }
  bool validatePoint(X11PointRequest request, const KeyboardRouteBinding &fence)
  {
    const auto viewport = m_viewer.viewportObservation();
    if (!m_coordinates || !viewport || !m_viewer.allForwardingFenced(fence) || !m_coordinates->validatePoint(request))
      return false;
    m_pendingViewport = viewport;
    m_pointRequest = request.request;
    m_pointFence = fence;
    return true;
  }
  std::optional<ViewerCoordinateUpdate> takeUpdate()
  {
    auto update = m_coordinates ? m_coordinates->takeUpdate() : std::nullopt;
    if (!update)
      return {};
    ViewerCoordinateUpdate result{std::move(*update), std::exchange(m_pendingViewport, {}), {}};
    const auto refuse = [&](const char *reason) {
      m_coordinates->cancel(result.native.request);
      result.native.error = reason;
      result.native.witness.reset();
      result.native.point.reset();
      result.viewport.reset();
    };
    if (!result.native.error.empty())
      result.viewport.reset();
    else if (!result.viewport || !result.viewport->live())
      refuse("X11 painted viewport changed during the coordinate request");
    else if (result.native.point) {
      if (!m_pointFence || !m_viewer.allForwardingFenced(*m_pointFence) || !result.native.point->live())
        refuse("X11 full forwarding fence changed during the point request");
      else {
        const auto &point = result.native.point->snapshot();
        const auto &painted = result.viewport->snapshot();
        const auto mapped = mapPoint(
            {point.clientX / painted.devicePixelRatio, point.clientY / painted.devicePixelRatio},
            {painted.videoLogical.x(), painted.videoLogical.y(), painted.videoLogical.width(),
             painted.videoLogical.height()},
            {0, 0, painted.sourceLogical.width(), painted.sourceLogical.height()}, false
        );
        if (!mapped)
          refuse("X11 routed point is outside the last-painted video rectangle");
        else
          result.source = QPointF(mapped->x, mapped->y);
      }
    } else if (!result.native.witness || !result.native.witness->live())
      refuse("X11 namespace witness expired or was revoked");
    return result;
  }
  bool cancel(std::uint64_t request)
  {
    const bool canceled = m_coordinates && m_coordinates->cancel(request);
    if (canceled)
      m_pendingViewport.reset();
    if (request == m_pointRequest) {
      m_pointRequest = 0;
      m_pointFence.reset();
    }
    return canceled;
  }
  std::optional<X11DisplayObservation> observe() const
  {
    return m_observer ? m_observer->observe() : std::nullopt;
  }
  QString reason() const
  {
    return m_observer ? QString::fromStdString(m_observer->unavailableReason())
                      : QStringLiteral("Viewport observation requires the native Qt xcb plugin");
  }

private:
  void poll()
  {
    const auto surface = m_viewer.internalWinId();
    if (!m_viewer.m_connected || m_viewer.m_returned || m_viewer.m_failed || !m_viewer.isVisible() || surface == 0) {
      if (m_lastSurface || m_observer->pending() || m_observer->observe())
        invalidate();
      m_lastSurface = 0;
      return;
    }
    if (surface != m_lastSurface) {
      m_viewer.invalidateViewport();
      m_lastSurface = surface;
      if (m_viewer.m_surfaceIncarnation == std::numeric_limits<std::uint64_t>::max()) {
        m_viewer.m_viewportClosed = true;
        m_observer->close();
        return;
      }
      ++m_viewer.m_surfaceIncarnation;
    }
    m_observer->poll();
    m_coordinates->poll();
    if (!m_observer->pending())
      m_observer->refresh(static_cast<std::uint32_t>(surface));
  }
  Viewer &m_viewer;
  std::unique_ptr<X11DisplayObserver> m_observer;
  std::unique_ptr<X11CoordinateEndpoint> m_coordinates;
  std::optional<ViewerViewportObservation> m_pendingViewport;
  std::optional<KeyboardRouteBinding> m_pointFence;
  std::uint64_t m_pointRequest = 0;
  QTimer m_timer;
  WId m_lastSurface = 0;
};

bool ViewerViewportObservation::live() const noexcept
{
  return m_snapshot && m_revoked && !m_revoked->load(std::memory_order_acquire) && m_snapshot->native.live();
}
const ViewerViewportSnapshot &ViewerViewportObservation::snapshot() const noexcept
{
  static const ViewerViewportSnapshot empty;
  return m_snapshot ? *m_snapshot : empty;
}
#endif
void Viewer::invalidateViewport()
{
  m_viewportRevoked->store(true, std::memory_order_release);
  if (m_viewportRevision == std::numeric_limits<std::uint64_t>::max())
    m_viewportClosed = true;
  else
    ++m_viewportRevision;
  m_viewportRevoked = std::make_shared<std::atomic_bool>(m_viewportClosed);
#ifdef WINDOWSHARE_X11
  if (m_x11Observer)
    m_x11Observer->invalidate();
#endif
  if (!m_viewportNotification.isActive())
    m_viewportNotification.start(0);
}

#ifdef WINDOWSHARE_X11
std::optional<ViewerViewportObservation> Viewer::viewportObservation() const
{
  if (!viewportUnavailableReason().isEmpty())
    return {};
  const auto native = m_x11Observer->observe();
  if (!native)
    return {};
  const auto &display = native->snapshot();
  std::uint32_t output = 0;
  for (const auto &candidate : display.outputs) {
    const auto &r = candidate.native;
    if (display.client.x >= r.x && display.client.y >= r.y &&
        std::int64_t(display.client.x) + display.client.width <= std::int64_t(r.x) + r.width &&
        std::int64_t(display.client.y) + display.client.height <= std::int64_t(r.y) + r.height)
      output = candidate.output;
  }
  ViewerViewportObservation result;
  result.m_snapshot = std::make_shared<ViewerViewportSnapshot>(ViewerViewportSnapshot{
      *native, m_session, m_epoch, m_geometry, m_focusRequest, m_focusRevision, m_surfaceIncarnation,
      m_viewportRevision, QSizeF(m_paintedClient), m_source, m_paintedImage, m_paintedVideo, m_paintedDpr, output
  });
  result.m_revoked = m_viewportRevoked;
  return result.live() ? std::optional{std::move(result)} : std::nullopt;
}

bool Viewer::readX11Witness(X11WitnessLocator locator, X11CoordinateClock::time_point deadline)
{
  return m_x11Observer && m_x11Observer->read(locator, deadline);
}

bool Viewer::validateX11Point(X11PointRequest request, const KeyboardRouteBinding &fence)
{
  return m_x11Observer && m_x11Observer->validatePoint(request, fence);
}

std::optional<ViewerCoordinateUpdate> Viewer::takeX11CoordinateUpdate()
{
  return m_x11Observer ? m_x11Observer->takeUpdate() : std::nullopt;
}

bool Viewer::cancelX11Coordinate(std::uint64_t request)
{
  return m_x11Observer && m_x11Observer->cancel(request);
}
#endif

std::optional<GnomePreparationTarget> Viewer::waylandPreparationTarget() const
{
  return m_committed ? waylandDragTarget() : std::nullopt;
}
std::optional<GnomePreparationTarget> Viewer::waylandDragTarget() const
{
  if (!familyInputAvailable() || m_viewportClosed || !m_connected || m_failed || m_returned || m_waitingRecoveryPaint ||
      m_videoRecovering || m_videoPaused || !m_readySent || m_resizing || isMinimized() || !isVisible() ||
      !m_error.isEmpty() || m_paintedGeometry != m_geometry || !m_paintedWayland || m_paintedClient != size() ||
      m_paintedDpr != devicePixelRatioF() || m_paintedImage.isEmpty() || m_paintedVideo.isEmpty() ||
      !m_waylandAssociation)
    return {};
  const auto native = readWaylandViewport(windowHandle());
  if (!native || native != m_paintedWayland ||
      !currentWaylandAssociation(windowHandle(), *native, *m_waylandAssociation))
    return {};
  return GnomePreparationTarget{
      m_session,
      m_epoch,
      m_geometry,
      m_waylandSurfaceIncarnation,
      m_waylandPaintRevision,
      native->resource,
      m_waylandAssociation->token
  };
}
std::optional<QPointF> Viewer::mapPreparationTarget(const GnomePreparationTarget &expected, QPointF point) const
{
  if (waylandPreparationTarget() != std::optional(expected) || !m_paintedWayland)
    return {};
  const auto local = mapWaylandViewport(windowHandle(), *m_paintedWayland, point);
  if (!local)
    return {};
  const auto mapped = mapPoint(
      {local->x(), local->y()},
      {m_paintedVideo.x(), m_paintedVideo.y(), m_paintedVideo.width(), m_paintedVideo.height()},
      {0, 0, m_source.width(), m_source.height()}, false
  );
  return mapped && waylandPreparationTarget() == std::optional(expected) ? std::optional(QPointF(mapped->x, mapped->y))
                                                                         : std::nullopt;
}
bool Viewer::beginInputPreparation(const InputPreparationBinding &binding)
{
  if (!validInputPreparationBinding(binding) || !m_connected || m_failed || m_returned || m_keyboardBinding ||
      (m_inputPreparation && m_inputPreparation != binding) || m_preparationSerial == UINT64_MAX)
    return false;
  if (m_inputPreparation == binding)
    return true;
  m_inputPreparation = binding; // Close before callbacks or detached pending-input flushes.
  ++m_preparationSerial;
  m_requireFocusIntent = true;
  const QPointer<Viewer> guard(this);
  releaseInput();
  return guard && inputPreparationFenced(binding);
}
bool Viewer::inputPreparationFenced(const InputPreparationBinding &binding) const
{
  return m_connected && !m_failed && !m_returned && m_inputPreparation == binding;
}
bool Viewer::finishInputPreparation(
    const InputPreparationBinding &binding, const std::optional<KeyboardRouteBinding> &all
)
{
  if (!inputPreparationFenced(binding) || (all && !allForwardingFenced(*all)))
    return false;
  m_inputPreparation.reset(); // The genuine All gate, if supplied, remains closed.
  const QPointer<Viewer> guard(this);
  forwardingFenceChanged();
  return guard && m_connected && (!all || allForwardingFenced(*all));
}
std::optional<GnomeViewportState>
Viewer::requestPreparedFocus(const InputPreparationBinding &binding, const GnomePreparationTarget &target)
{
  if (!inputPreparationFenced(binding) || waylandPreparationTarget() != std::optional(target) || !m_localActive ||
      !isActiveWindow())
    return {};
  // The provider has already activated this exact native role. This requests
  // focus control only; no synthetic key/button enters queueInput or the Host.
  const QPointer<Viewer> guard(this);
  if (!requestInputPreparationFocus(binding))
    return {};
  if (!guard || !inputPreparationFenced(binding) || waylandPreparationTarget() != std::optional(target))
    return {};
  return waylandViewportState();
}
bool Viewer::requestInputPreparationFocus(const InputPreparationBinding &binding)
{
  if (!inputPreparationFenced(binding) || !m_localActive || !isActiveWindow())
    return false;
  m_requireFocusIntent = false;
  const QPointer<Viewer> guard(this);
  requestFocus();
  return guard && inputPreparationFenced(binding);
}

std::optional<GnomeViewportState> Viewer::waylandViewportState() const
{
  if (m_viewportClosed || !acceptsLocalInput() || !isVisible() || m_focusPending || !m_focusRevision ||
      !m_focusRequest || !m_error.isEmpty() || !m_paintedWayland || m_paintedClient != size() ||
      m_paintedDpr != devicePixelRatioF() || m_paintedImage.isEmpty() || m_paintedVideo.isEmpty())
    return {};
  const auto native = readWaylandViewport(windowHandle());
  if (!native || native != m_paintedWayland || !m_waylandAssociation ||
      !currentWaylandAssociation(windowHandle(), *native, *m_waylandAssociation))
    return {};
  GnomeViewportState state;
  state.session = m_session;
  state.epoch = m_epoch;
  state.geometry = m_geometry;
  state.focusRequest = m_focusRequest;
  state.focusRevision = m_focusRevision;
  state.surfaceIncarnation = m_waylandSurfaceIncarnation;
  state.viewportRevision = m_viewportRevision;
  state.nativeSurface = native->resource;
  state.windowAssociation = m_waylandAssociation->token;
  state.clientWidth = m_paintedClient.width();
  state.clientHeight = m_paintedClient.height();
  state.sourceWidth = m_source.width();
  state.sourceHeight = m_source.height();
  state.imageWidth = m_paintedImage.width();
  state.imageHeight = m_paintedImage.height();
  state.videoX = m_paintedVideo.x();
  state.videoY = m_paintedVideo.y();
  state.videoWidth = m_paintedVideo.width();
  state.videoHeight = m_paintedVideo.height();
  state.dpr = m_paintedDpr;
  state.surfaceWidth = native->extent.width();
  state.surfaceHeight = native->extent.height();
  state.contentX = native->contentOrigin.x();
  state.contentY = native->contentOrigin.y();
  state.logicalScale = native->logicalScale;
  return state;
}

std::optional<QPointF> Viewer::mapWaylandSurface(const GnomeViewportState &expected, QPointF point) const
{
  auto state = waylandViewportState();
  if (!state)
    return {};
  // The Bridge independently checks these two channel-owned fields.
  state->connection = expected.connection;
  state->peerFingerprint = expected.peerFingerprint;
  if (*state != expected || !m_paintedWayland)
    return {};
  const auto local = mapWaylandViewport(windowHandle(), *m_paintedWayland, point);
  if (!local)
    return {};
  const auto mapped = mapPoint(
      {local->x(), local->y()},
      {m_paintedVideo.x(), m_paintedVideo.y(), m_paintedVideo.width(), m_paintedVideo.height()},
      {0, 0, m_source.width(), m_source.height()}, false
  );
  auto fresh = waylandViewportState();
  if (!fresh)
    return {};
  fresh->connection = expected.connection;
  fresh->peerFingerprint = expected.peerFingerprint;
  if (*fresh != expected || !mapped)
    return {};
  return QPointF(mapped->x, mapped->y);
}

QString Viewer::viewportUnavailableReason() const
{
#ifdef WINDOWSHARE_X11
  if (!m_x11Observer || m_viewportClosed)
    return QStringLiteral("Native viewport observer is unavailable or closed");
  if (!acceptsLocalInput() || !isVisible() || m_focusPending || m_focusRevision == 0 || m_focusRequest == 0)
    return QStringLiteral("Viewport requires committed painted geometry and current acknowledged local focus");
  const auto observed = m_x11Observer->observe();
  if (!observed)
    return m_x11Observer->reason();
  const auto &display = observed->snapshot();
  if (display.surface != internalWinId() || !display.viewable || !display.focusWithinSurface)
    return QStringLiteral("Native X11 surface is not viewable and focused on this connection's master keyboard");
  if (m_paintedClient != size() || m_paintedImage.isEmpty() || m_paintedVideo.isEmpty() || m_paintedDpr <= 0 ||
      !std::isfinite(m_paintedDpr) || m_paintedDpr != devicePixelRatioF() ||
      std::abs(m_paintedClient.width() * m_paintedDpr - display.client.width) > 1e-9 ||
      std::abs(m_paintedClient.height() * m_paintedDpr - display.client.height) > 1e-9)
    return QStringLiteral("Painted Qt logical geometry does not map exactly to the observed native pixel extent");
  if (display.client.x < 0 || display.client.y < 0 ||
      std::int64_t(display.client.x) + display.client.width > display.desktop.width ||
      std::int64_t(display.client.y) + display.client.height > display.desktop.height)
    return QStringLiteral("Native client rectangle extends outside the observed root");
  unsigned containing = 0, intersecting = 0;
  for (const auto &output : display.outputs) {
    const auto &r = output.native;
    const auto right = std::int64_t(r.x) + r.width, bottom = std::int64_t(r.y) + r.height;
    const auto clientRight = std::int64_t(display.client.x) + display.client.width;
    const auto clientBottom = std::int64_t(display.client.y) + display.client.height;
    if (display.client.x < right && clientRight > r.x && display.client.y < bottom && clientBottom > r.y)
      ++intersecting;
    if (output.simpleMapping && display.client.x >= r.x && display.client.y >= r.y && clientRight <= right &&
        clientBottom <= bottom)
      ++containing;
  }
  if (containing != 1 || intersecting != 1)
    return QStringLiteral("Viewport needs one unambiguous untransformed, unpanned output containing the whole client");
  return {};
#else
  return QStringLiteral("Native viewport observation is available only on Linux with Qt xcb");
#endif
}

Viewer::Viewer(std::uint64_t session, std::uint64_t epoch, const QJsonObject &metadata, QWidget *parent)
    : QWidget(parent, Qt::Window),
      m_session(session),
      m_epoch(epoch),
      m_decoder(std::make_shared<VideoDecoder>())
{
  setAttribute(Qt::WA_OpaquePaintEvent);
  setFocusPolicy(Qt::StrongFocus);
  // Qt input methods can emit deferred preedit/commit after a native input
  // fence. No proven compositor->Qt IME drain exists yet, so never advertise
  // acceptance or synthesize text from composition events.
  setAttribute(Qt::WA_InputMethodEnabled, false);
  setMouseTracking(true);
  setMinimumSize(96, 64);
  setWindowTitle(QStringLiteral("Mac window — Deskflow"));
  m_viewportNotification.setSingleShot(true);
  connect(&m_viewportNotification, &QTimer::timeout, this, [this] {
    const auto callback = onViewportInvalidated;
    if (callback)
      callback();
  });
  m_focusTimeout.setSingleShot(true);
  m_resizeDebounce.setSingleShot(true);
  m_resizeTimeout.setSingleShot(true);
  m_videoRecoveryTimeout.setSingleShot(true);
  connect(&m_videoRecoveryTimeout, &QTimer::timeout, this, [this] {
    if (m_videoRecovering && !m_failed && !m_returned)
      fail(QStringLiteral("Video decoder recovery timed out; returning the source window"));
  });
  connect(&m_focusTimeout, &QTimer::timeout, this, [this] {
    const QPointer<Viewer> guard(this);
    releaseInput(KeyboardRouteReason::Timeout);
    if (!guard)
      return;
    if (onError)
      onError(QStringLiteral("The Mac did not acknowledge window focus; pending input was released"));
  });
  connect(&m_resizeDebounce, &QTimer::timeout, this, [this] {
    if (!m_connected || m_returned || !m_committed)
      return;
    if (m_resizeRequest == maxJsonInteger) {
      fail(QStringLiteral("Window resize sequence exhausted"));
      return;
    }
    ++m_resizeRequest;
    m_resizeBaseGeometry = m_geometry;
    if (!send(
            MessageType::Resize, {{"width", width()},
                                  {"height", height()},
                                  {"geometry", jsonNumber(m_geometry)},
                                  {"request", jsonNumber(m_resizeRequest)}}
        ))
      return;
    m_resizeTimeout.start(10000);
  });
  connect(&m_resizeTimeout, &QTimer::timeout, this, [this] {
    fail(QStringLiteral("The Mac did not acknowledge the new window size; returning the window"));
  });
  m_decodeWorker = new QObject;
  m_decodeWorker->moveToThread(&m_decodeThread);
  connect(&m_decodeThread, &QThread::finished, m_decodeWorker, &QObject::deleteLater);
  m_decodeThread.start(QThread::LowPriority);
  if (m_session == 0 || m_epoch == 0)
    fail(QStringLiteral("Invalid window session"));
  else
    applyMetadata(metadata);
#ifdef WINDOWSHARE_X11
  m_x11Observer = std::make_unique<ViewerX11Observer>(*this);
#endif
}

Viewer::~Viewer()
{
  *m_familyLife = false; // QWidget can emit parent signals after these members are gone.

  onViewportInvalidated = {};
  onForwardingFenceChanged = {};
  invalidateViewport();
#ifdef WINDOWSHARE_X11
  m_x11Observer.reset();
#endif
  releaseInput(KeyboardRouteReason::Returned);
  m_connected = false;
  m_videoRecoveryTimeout.stop();
  // Decoding never runs on the GUI/event-tap path. Wait only for the current
  // bounded access unit; queued invocations are discarded when the loop exits.
  m_decodeThread.quit();
  m_decodeThread.wait();
}

void Viewer::applyMetadata(const QJsonObject &metadata)
{
  if (m_failed || m_returned)
    return;
  const auto family = parseWindowFamily(metadata);
  if (m_geometry && (bool(family) != bool(m_family) || (family && !family->sameAttachment(*m_family)))) {
    fail(QStringLiteral("Window family attachment changed"));
    return;
  }
  const std::uint64_t geometry = revision(metadata.value(QStringLiteral("geometry")));
  const double sourceWidth = metadata.value(QStringLiteral("width")).toDouble(-1);
  const double sourceHeight = metadata.value(QStringLiteral("height")).toDouble(-1);
  if (geometry == 0 || !Rect{0, 0, sourceWidth, sourceHeight}.valid() ||
      metadata.value(QStringLiteral("codec")).toString(QStringLiteral("h264")) != QStringLiteral("h264")) {
    fail(QStringLiteral("The Mac sent unsupported window metadata"));
    return;
  }
  if (geometry < m_geometry)
    return;
  if (geometry == m_geometry && !m_source.isEmpty() && QSizeF(sourceWidth, sourceHeight) != m_source) {
    fail(QStringLiteral("Window dimensions changed without a geometry revision"));
    return;
  }
  const bool firstMetadata = m_geometry == 0;
  m_family = family;
  const auto dragValue = metadata.value(QStringLiteral("windowDrag"));
  const auto drag = revision(dragValue);
  if (m_family && drag) {
    fail(QStringLiteral("Attached windows cannot own a titlebar transfer"));
    return;
  }
  if ((!dragValue.isUndefined() && dragValue != QJsonValue(0) && !drag) || (!firstMetadata && drag != m_windowDrag)) {
    fail(QStringLiteral("Window drag identity changed during the preview"));
    return;
  }
  if (firstMetadata && drag) {
    m_windowDrag = drag;
    // The original press still belongs to the source. Painting a provisional
    // window must not request activation or deliver keyboard/pointer input.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowFlag(Qt::WindowDoesNotAcceptFocus);
    setFocusPolicy(Qt::NoFocus);
  }
  if (firstMetadata && m_family) {
    setMinimumSize(2, 2);
    setWindowFlag(Qt::FramelessWindowHint);
    if (!m_family->acceptsInput()) {
      setAttribute(Qt::WA_ShowWithoutActivating);
      setWindowFlag(Qt::WindowDoesNotAcceptFocus);
      setWindowFlag(Qt::WindowTransparentForInput);
      setFocusPolicy(Qt::NoFocus);
    }
  }
  if (!firstMetadata && geometry != m_geometry) {
    // The next access unit belongs to a different source geometry. Revoke
    // pending media recovery; a stale decoder completion cannot paint this
    // replacement or resume input on the new window revision.
    m_videoRecoveryTimeout.stop();
    m_videoRecovering = false;
    m_videoRecoveryRequestSent = false;
    m_videoRecoveryFrameQueued = false;
    m_videoRecoveryFramePaintReady = false;
    // A layout/size change while minimized must not remove the input gate;
    // the unminimized viewport still needs a decoder-safe painted IDR.
    m_waitingRecoveryPaint = m_videoPaused;
    if (m_decodeGeneration->load() == std::numeric_limits<std::uint64_t>::max()) {
      fail(QStringLiteral("Decoder lifetime sequence exhausted"));
      return;
    }
    m_decodeGeneration->fetch_add(1);
    const QPointer<Viewer> guard(this);
    releaseInput(KeyboardRouteReason::GeometryChanged);
    if (!guard)
      return;
  }
  m_geometry = geometry;
  m_source = QSizeF(sourceWidth, sourceHeight);
  QString title = metadata.value(QStringLiteral("title")).toString();
  title.remove(QChar('\0'));
  setWindowTitle((title.isEmpty() ? QStringLiteral("Mac window") : title.left(512)) + QStringLiteral(" — Deskflow"));
  m_applyingMetadata = true;
  if (firstMetadata) {
    QSize initial = m_source.toSize();
    if (QScreen *target = screen())
      initial = initial.boundedTo(target->availableGeometry().size() * 0.85);
    resize(initial.expandedTo(minimumSize()));
  }
  m_applyingMetadata = false;
  placeInFamily();
  const std::uint64_t acknowledged = revision(metadata.value(QStringLiteral("resizeRequest")));
  if (m_resizing && !m_resizeDebounce.isActive() &&
      ((acknowledged != 0 && acknowledged == m_resizeRequest) ||
       (acknowledged == 0 && geometry > m_resizeBaseGeometry))) {
    m_resizing = false;
    m_resizeTimeout.stop();
  }
  if (metadata.value(QStringLiteral("phase")).toString() == QStringLiteral("remote"))
    committed();
  update();
}

void Viewer::attachFamilyParent(Viewer *parent)
{
  if (!m_family || !parent || parent == this || isVisible() || m_familyAttached ||
      m_family->parent != Owner{parent->m_session, parent->m_epoch})
    throw std::runtime_error("Invalid viewer family parent");
  m_familyParent = parent;
  m_familyParentLife = parent->m_familyLife;
  m_familyAttached = true;
  // QWidget ownership would destroy children inside the parent's destructor,
  // before Bridge can retire their input routes. Use only a native transient.
  (void)parent->winId();
  (void)winId();
  windowHandle()->setTransientParent(parent->windowHandle());
  connect(windowHandle(), &QWindow::transientParentChanged, this, [this] {
    invalidateViewport();
    releaseInput(KeyboardRouteReason::AuthorityLost);
  });
  parent->installEventFilter(this);
  connect(parent, &QObject::destroyed, this, [this] {
    m_familyParent = nullptr;
    disconnected();
    close();
  });
  placeInFamily();
}
void Viewer::setFamilyBlocked(bool blocked)
{
  if (m_familyBlocked == blocked)
    return;
  m_familyBlocked = blocked;
  invalidateViewport();
  // A closed dialog never resurrects a focus acknowledgement or a held key.
  m_requireFocusIntent = true;
  releaseInput(KeyboardRouteReason::FocusChanged);
}
Viewer *Viewer::familyParent() const
{
  const auto live = m_familyParentLife.lock();
  // QPointer<Viewer>::data() itself is an invalid downcast once only QWidget's
  // base destructor remains. The separate lifetime closes before that point.
  return live && *live ? m_familyParent.data() : nullptr;
}
bool Viewer::familyInputAvailable() const
{
  if (m_familyBlocked || (m_family && !m_family->acceptsInput()))
    return false;
  const auto *child = this;
  for (unsigned depth = 0; depth < 16; ++depth) {
    if (!child->m_family)
      return true;
    const auto *parent = child->familyParent();
    if (!parent || !parent->m_connected || parent->m_returned || parent->m_failed || !parent->isVisible() ||
        parent->isMinimized() || !child->windowHandle() ||
        child->windowHandle()->transientParent() != parent->windowHandle())
      return false;
    child = parent;
  }
  return false;
}
void Viewer::placeInFamily()
{
  if (!m_family || !familyParent() || familyParent()->m_source.isEmpty())
    return;
  const auto video = familyParent()->videoRect();
  const auto scale = video.width() / familyParent()->m_source.width();
  if (!std::isfinite(scale) || scale <= 0)
    return;
  const bool applying = m_applyingMetadata;
  m_applyingMetadata = true;
  resize((m_source * scale).toSize().expandedTo(minimumSize()));
  // Wayland deliberately has no global-position fallback. Its compositor
  // places the exact transient parent/child pair; X11 honors this offset.
  if (QGuiApplication::platformName() != QStringLiteral("wayland"))
    move(familyParent()->mapToGlobal((video.topLeft() + m_family->offset * scale).toPoint()));
  m_applyingMetadata = applying;
}
bool Viewer::eventFilter(QObject *object, QEvent *event)
{
  if (object == familyParent() && (event->type() == QEvent::Move || event->type() == QEvent::Resize))
    QTimer::singleShot(0, this, [this] { placeInFamily(); });
  if (object == familyParent() && (event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange ||
                                   event->type() == QEvent::PlatformSurface || event->type() == QEvent::WinIdChange)) {
    invalidateViewport();
    releaseInput(KeyboardRouteReason::Hidden);
  }
  return false;
}

void Viewer::pauseVideoForHiddenViewer()
{
  if (!m_mediaRecoveryEnabled || !m_connected || m_failed || m_returned || !m_readySent || !m_committed) {
    releaseInput(KeyboardRouteReason::Hidden);
    return;
  }
  if (!m_videoPaused) {
    if (m_decodeGeneration->load() == std::numeric_limits<std::uint64_t>::max()) {
      fail(QStringLiteral("Decoder lifetime sequence exhausted"));
      return;
    }
    // Hidden windows may remain parked for hours. Unlike active media
    // recovery this is *not* an unbounded pending drain: it consumes zero
    // new video/inputs until visibility is restored, at which point a new
    // absolute 5s IDR budget starts. Old queued FFmpeg work is revoked.
    m_videoPaused = true;
    m_videoRecoveryTimeout.stop();
    m_videoRecovering = false;
    m_videoRecoveryRequestSent = false;
    m_videoRecoveryFrameQueued = false;
    m_videoRecoveryFramePaintReady = false;
    m_waitingRecoveryPaint = true;
    m_requireFocusIntent = true;
    m_decodeGeneration->fetch_add(1, std::memory_order_acq_rel);
  }
  releaseInput(KeyboardRouteReason::Hidden);
}

void Viewer::resumeVideoAfterVisibility()
{
  if (!m_videoPaused || !m_connected || m_returned || m_failed || !isVisible() || isMinimized())
    return;
  m_videoPaused = false;
  beginVideoRecovery();
}

void Viewer::beginVideoRecovery()
{
  if (!m_mediaRecoveryEnabled || m_failed || m_returned || !m_connected || m_videoRecovering) {
    fail(QStringLiteral("Video decoding cannot keep up; returning the window to the Mac"));
    return;
  }
  if (m_decodeGeneration->load() == std::numeric_limits<std::uint64_t>::max() ||
      m_mediaRecoverySequence == maxJsonInteger) {
    fail(QStringLiteral("Video decoder recovery sequence exhausted"));
    return;
  }
  m_videoRecovering = true;
  m_videoRecoveryRequestSent = false;
  m_videoRecoveryFrameQueued = false;
  m_videoRecoveryFramePaintReady = false;
  m_waitingRecoveryPaint = true;
  m_requireFocusIntent = true;
  m_decodeGeneration->fetch_add(1, std::memory_order_acq_rel);
  m_videoRecoveryTimeout.start(5000); // One absolute budget, never extended by frames or responses.
  // Never let input continue against a stale image while decoder state is
  // unknown. This also revokes any experimental original keyboard/pointer
  // forwarding lease before source input can be resumed.
  const QPointer<Viewer> guard(this);
  releaseInput(KeyboardRouteReason::Failed);
  if (guard && m_connected && !m_failed)
    requestVideoRecoveryWhenDrained();
}

void Viewer::requestVideoRecoveryWhenDrained()
{
  if (!m_videoRecovering || m_videoRecoveryRequestSent || m_pendingFrames != 0 || !m_connected || m_returned ||
      m_failed || m_videoRecoveryTimeout.isActive() == false)
    return;
  m_videoRecoveryRequestSent = true; // Before callback reentry.
  ++m_mediaRecoverySequence;
  if (m_mediaMetrics)
    ++m_mediaMetrics->recoveryRequests;
  send(
      MessageType::Input, {{"kind", "video-recovery"},
                           {"geometry", jsonNumber(m_geometry)},
                           {"request", jsonNumber(m_mediaRecoverySequence)}}
  );
}

bool Viewer::enableMediaMetrics()
{
  if (QThread::currentThread() != thread() || m_mediaMetrics || m_pendingFrames || m_readySent || !m_image.isNull())
    return false;
  m_mediaMetrics.emplace();
  m_mediaMetrics->frameCapacity = maxPendingFrames;
  m_mediaMetrics->byteCapacity = maxPendingVideoBytes;
  return true;
}

std::optional<MediaMetrics> Viewer::mediaMetrics() const
{
  if (QThread::currentThread() != thread() || !m_mediaMetrics)
    return {};
  auto result = *m_mediaMetrics;
  result.pendingFrames = m_pendingFrames;
  result.pendingBytes = m_pendingBytes;
  return result;
}

void Viewer::receiveFrame(const QByteArray &accessUnit, std::uint64_t geometry)
{
  const auto receivedAt = m_mediaMetrics ? MediaMetrics::Clock::now() : MediaMetrics::Clock::time_point{};
  if (m_mediaMetrics) {
    ++m_mediaMetrics->received;
    ++m_mediaMetrics->ignored; // Subtracted only at actual queue admission.
  }
  if (!m_connected || m_returned || m_failed || geometry != m_geometry || m_videoPaused)
    return;
  if (accessUnit.isEmpty() || accessUnit.size() > maxPayload) {
    fail(QStringLiteral("Invalid video packet size"));
    return;
  }
  bool resetDecoder = false;
  if (m_videoRecovering && !m_videoRecoveryFrameQueued) {
    // Once old queued work is drained, accept only one independently decodable
    // SPS/PPS/IDR. No interframe is ever forwarded into a reset FFmpeg context.
    if (!m_videoRecoveryRequestSent || m_pendingFrames ||
        !decoderSyncAccessUnit(
            std::span(reinterpret_cast<const std::uint8_t *>(accessUnit.constData()), std::size_t(accessUnit.size()))
        ))
      return;
    m_videoRecoveryFrameQueued = true;
    resetDecoder = true;
  }
  if (m_pendingFrames >= maxPendingFrames || accessUnit.size() > maxPendingVideoBytes - m_pendingBytes) {
    if (m_mediaMetrics)
      ++m_mediaMetrics->overflows;
    if (m_videoRecovering) {
      // A second overload before painting the recovery frame is not a safe
      // reason to silently reset the recovery deadline or repeat the request.
      fail(QStringLiteral("Video decoder overflow during recovery; returning the source window"));
    } else {
      beginVideoRecovery();
    }
    return;
  }
  ++m_pendingFrames;
  m_pendingBytes += accessUnit.size();
  std::uint64_t metricSequence = 0;
  if (m_mediaMetrics) {
    metricSequence = ++m_mediaMetrics->admitted;
    --m_mediaMetrics->ignored;
    m_mediaMetrics->peakFrames = std::max(m_mediaMetrics->peakFrames, std::uint64_t(m_pendingFrames));
    m_mediaMetrics->peakBytes = std::max(m_mediaMetrics->peakBytes, std::uint64_t(m_pendingBytes));
  }
  const QPointer<Viewer> target(this);
  const auto decoder = m_decoder;
  const auto generation = m_decodeGeneration;
  const auto capturedGeneration = generation->load(std::memory_order_acquire);
  QMetaObject::invokeMethod(
      m_decodeWorker,
      [target, decoder, generation, capturedGeneration, resetDecoder, accessUnit, geometry, metricSequence,
       receivedAt] {
        std::optional<VideoDecoder::Result> result;
        if (generation->load(std::memory_order_acquire) == capturedGeneration) {
          if (resetDecoder)
            decoder->reset();
          result = decoder->decode(accessUnit);
        }
        if (!target)
          return;
        QMetaObject::invokeMethod(
            target,
            [target, result = std::move(result), generation, capturedGeneration, resetDecoder, size = accessUnit.size(),
             geometry, metricSequence, receivedAt]() mutable {
              if (!target)
                return;
              auto *self = target.data();
              --self->m_pendingFrames;
              self->m_pendingBytes -= size;
              if (self->m_mediaMetrics)
                ++self->m_mediaMetrics->retired;
              if (self->m_videoRecovering && !self->m_videoRecoveryRequestSent && !self->m_pendingFrames) {
                const QPointer<Viewer> guard(self);
                self->requestVideoRecoveryWhenDrained();
                if (!guard)
                  return;
              }
              if (!result || generation->load(std::memory_order_acquire) != capturedGeneration || !self->m_connected ||
                  self->m_returned || self->m_failed || geometry != self->m_geometry) {
                if (self->m_mediaMetrics)
                  ++self->m_mediaMetrics->discardedCompletions;
                return;
              }
              if (!result->error.isEmpty()) {
                if (self->m_mediaMetrics)
                  ++self->m_mediaMetrics->decodeErrors;
                if (++self->m_decodeErrors >= 3)
                  self->fail(result->error);
                else if (self->onError)
                  self->onError(result->error);
                return;
              }
              if (result->image.isNull()) {
                if (self->m_mediaMetrics)
                  ++self->m_mediaMetrics->emptyDecodes;
                return;
              }
              self->m_decodeErrors = 0;
              if (self->m_mediaMetrics) {
                ++self->m_mediaMetrics->decoded;
                if (self->m_metricImage && self->m_metricImage != self->m_metricPaintedImage)
                  ++self->m_mediaMetrics->replacedBeforePaint;
                self->m_metricImage = metricSequence;
                self->m_metricReceivedAt = receivedAt;
              }
              self->m_image = std::move(result->image);
              self->m_frameGeometry = geometry;
              if (resetDecoder)
                self->m_videoRecoveryFramePaintReady = true;
              self->update();
            },
            Qt::QueuedConnection
        );
      },
      Qt::QueuedConnection
  );
}

void Viewer::committed()
{
  if (!m_connected || m_failed || m_returned)
    return;
  const bool endingPreview = m_windowDrag && !m_committed;
  m_committed = true;
  if (endingPreview) {
    const QPointer<Viewer> alive(this);
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    // QWidget::setWindowFlag reparents/hides an existing top-level window.
    // On Wayland that destroys the exact placed surface and GNOME recenters
    // its replacement. Change this hint on the live QWindow, and mirror it in
    // QWidget so a later legitimate recreation cannot restore no-focus.
    const auto flags = windowFlags() & ~Qt::WindowDoesNotAcceptFocus;
    overrideWindowFlags(flags);
    if (auto *native = windowHandle())
      native->setFlags(flags);
    if (!alive)
      return;
    setFocusPolicy(Qt::StrongFocus);
    invalidateViewport();
    if (!alive)
      return;
  }
  if (m_localActive)
    requestFocus();
}

void Viewer::focusAcknowledged(std::uint64_t lease, std::uint64_t request)
{
  if (!m_connected || !m_committed || request == 0 || request != m_focusRequest)
    return;
  if (lease == 0) {
    // The source app may change its focused window without a Qt focus event.
    // Cancel this exact lease/request, including any pending activating input.
    // Idle video paints and pointer movement must not steal focus back.
    if (!m_focusPending && m_focusRevision == 0)
      return;
    m_requireFocusIntent = true;
    releaseInput();
    return;
  }
  if (!m_focusPending || !m_localActive || m_resizing || lease > maxJsonInteger || isMinimized())
    return;
  m_focusPending = false;
  m_focusRevision = lease;
  m_focusTimeout.stop();
  const auto keyboardFence = m_keyboardFencedGeneration, pointerFence = m_pointerFencedGeneration;
  const auto preparationFence = m_preparationSerial;
  auto inputs = std::move(m_pendingInputs);
  m_pendingInputs.clear();
  for (auto &input : inputs) {
    if (!acceptsLocalInput() || m_focusRevision != lease ||
        revision(input.value(QStringLiteral("geometry"))) != m_geometry)
      break;
    // A send callback may complete both quiescence and resume. A current-state
    // check alone would then replay old entries detached from m_pendingInputs.
    const bool key = input.value(QStringLiteral("kind")) == QStringLiteral("key");
    if (fencesInput(input) || preparationFence != m_preparationSerial ||
        (key ? keyboardFence != m_keyboardFencedGeneration : pointerFence != m_pointerFencedGeneration))
      continue;
    if (!sendInput(std::move(input)))
      break;
  }
}

std::optional<ClipboardFocusObservation> Viewer::clipboardFocus() const
{
  if (QThread::currentThread() != thread() || m_viewportClosed || !acceptsLocalInput() || !isVisible() ||
      !m_visibility || !hasFocus() || !isActiveWindow() || !windowHandle() || !windowHandle()->isExposed() ||
      m_applyingMetadata || m_focusPending || !m_focusRequest || !m_focusRevision || !m_error.isEmpty() ||
      m_paintedClient != size() || m_paintedImage.isEmpty() || m_paintedVideo.isEmpty() ||
      m_paintedDpr != devicePixelRatioF())
    return {};
  const ClipboardFocusBinding binding{{m_session, m_epoch}, m_focusRequest, m_focusRevision, m_geometry};
  const QPointer<const Viewer> alive(this);
  const auto revoked = m_viewportRevoked;
  return ClipboardFocusObservation{binding, [alive, revoked, binding] {
                                     if (!alive || revoked->load() || QThread::currentThread() != alive->thread())
                                       return false;
                                     const auto fresh = alive->clipboardFocus();
                                     return fresh && fresh->binding == binding;
                                   }};
}

bool Viewer::currentKeyboardRoute(const KeyboardRouteBinding &binding) const
{
  return acceptsLocalInput() && isVisible() && m_visibility && !m_applyingMetadata && !m_focusPending &&
         binding.owner == Owner{m_session, m_epoch} && binding.focusRequest == m_focusRequest &&
         binding.focusRevision == m_focusRevision && binding.geometry == m_geometry;
}
bool Viewer::allForwardingFenced(const KeyboardRouteBinding &binding) const
{
  return validKeyboardRouteBinding(binding) && binding.scope == ForwardingScope::All && m_keyboardBindingValid &&
         !m_keyboardRouteHandling && m_keyboardBinding == binding && currentKeyboardRoute(binding);
}

bool Viewer::fencesPointer() const
{
  return m_inputPreparation || (m_keyboardBinding && m_keyboardBinding->scope == ForwardingScope::All) ||
         !m_suppressedButtons.isEmpty();
}

bool Viewer::fencesInput(const QJsonObject &input) const
{
  const auto kind = input.value(QStringLiteral("kind")).toString();
  return ((m_inputPreparation || m_keyboardBinding) && kind == QStringLiteral("key")) ||
         (fencesPointer() &&
          (kind == QStringLiteral("button") || kind == QStringLiteral("move") || kind == QStringLiteral("scroll")));
}

bool Viewer::sendKeyboardRoute(const KeyboardRouteControl &control)
{
  if (!m_connected)
    return false;
  const QPointer<Viewer> guard(this);
  const auto callback = onKeyboardRouteSend;
  bool sent = false;
  try {
    sent = callback && callback(control);
  } catch (...) {
    // A failed fence acknowledgement must never leave two keyboard routes live.
  }
  if (!guard)
    return false;
  if (!sent)
    disconnected();
  return sent && m_connected;
}

void Viewer::keyboardRoute(const KeyboardRouteControl &control)
{
  if (!m_connected || m_keyboardRouteHandling || !validKeyboardRouteBinding(control.binding) ||
      control.binding.owner != Owner{m_session, m_epoch} || control.action != KeyboardRouteAction::Request ||
      control.result != KeyboardRouteResult::None || control.reason != KeyboardRouteReason::None ||
      (control.mode != KeyboardRouteMode::Original && control.mode != KeyboardRouteMode::Viewer))
    return;
  // Sending can synchronously disconnect or invoke callbacks. A nested request
  // cannot resume forwarding in the middle of the release/acknowledgement fence.
  m_keyboardRouteHandling = true;
  const QPointer<Viewer> guard(this);
  handleKeyboardRoute(control);
  if (guard)
    m_keyboardRouteHandling = false;
}

void Viewer::handleKeyboardRoute(const KeyboardRouteControl &control)
{
  auto reply = control;
  reply.action = KeyboardRouteAction::Acknowledge;
  reply.result = KeyboardRouteResult::Stale;
  const auto binding = control.binding;
  if (control.mode == KeyboardRouteMode::Viewer) {
    if (m_keyboardBinding != binding) {
      sendKeyboardRoute(reply);
      return;
    }
    reply.result = KeyboardRouteResult::Ready;
    const QPointer<Viewer> guard(this);
    m_keyboardBindingValid = false;
    forwardingFenceChanged();
    if (!guard)
      return;
    // The acknowledgement precedes any newly forwarded input on the same stream.
    // An invalidated binding remains resumable by its exact host request.
    if (sendKeyboardRoute(reply) && guard && m_keyboardBinding == binding) {
      m_keyboardBinding.reset();
      m_keyboardBindingValid = false;
    }
    return;
  }

  const bool duplicate = m_keyboardBinding == binding && m_keyboardBindingValid;
  if (!duplicate && binding.generation <= m_keyboardGeneration) {
    sendKeyboardRoute(reply);
    return;
  }
  m_keyboardGeneration = std::max(m_keyboardGeneration, binding.generation);
  if (!currentKeyboardRoute(binding)) {
    sendKeyboardRoute(reply);
    return;
  }
  if ((!duplicate && m_keyboardBinding) || !m_heldButtons.isEmpty() || !m_deliveredButtons.isEmpty()) {
    reply.result = KeyboardRouteResult::Busy;
    sendKeyboardRoute(reply);
    return;
  }
  const QPointer<Viewer> guard(this);
  if (!duplicate) {
    // Install the whole requested fence before releasing keys or invoking either
    // callback. Pending pointer events cannot escape a full-input handoff.
    m_keyboardBinding = binding;
    m_keyboardBindingValid = true;
    forwardingFenceChanged();
    if (!guard)
      return;
    quiesceKeyboard();
  }
  if (!guard || !m_connected || m_keyboardBinding != binding || !m_keyboardBindingValid ||
      !currentKeyboardRoute(binding))
    return;
  reply.result = KeyboardRouteResult::Ready;
  sendKeyboardRoute(reply);
}

void Viewer::quiesceKeyboard()
{
  // Accepted generations never repeat or wrap. Retain each scope's cutover
  // marker after resume so an already-running queue flush cannot replay it.
  m_keyboardFencedGeneration = m_keyboardBinding->generation;
  if (m_keyboardBinding->scope == ForwardingScope::All)
    m_pointerFencedGeneration = m_keyboardBinding->generation;
  for (auto held = m_heldKeys.cbegin(); held != m_heldKeys.cend(); ++held)
    m_suppressedKeys.insert(held.key());
  m_heldKeys.clear();
  m_pendingInputs.erase(
      std::remove_if(
          m_pendingInputs.begin(), m_pendingInputs.end(),
          [this](const QJsonObject &input) { return fencesInput(input); }
      ),
      m_pendingInputs.end()
  );
  const auto keys = std::move(m_deliveredKeys);
  m_deliveredKeys.clear();
  const QPointer<Viewer> guard(this);
  for (const auto &down : keys) {
    auto up = down;
    up.insert(QStringLiteral("down"), false);
    up.insert(QStringLiteral("repeat"), false);
    up.insert(QStringLiteral("modifiers"), 0);
    if (!send(MessageType::Input, up) || !guard) {
      if (guard && m_connected)
        disconnected();
      return;
    }
  }
}

void Viewer::invalidateKeyboardRoute(KeyboardRouteReason reason)
{
  if (!m_keyboardBinding || !m_keyboardBindingValid)
    return;
  const auto binding = *m_keyboardBinding;
  m_keyboardBindingValid = false;
  const QPointer<Viewer> guard(this);
  forwardingFenceChanged();
  if (!guard)
    return;
  if (m_connected)
    sendKeyboardRoute(
        {binding, KeyboardRouteAction::Withdraw, KeyboardRouteMode::Original, KeyboardRouteResult::None, reason}
    );
}

void Viewer::forwardingFenceChanged()
{
#ifdef WINDOWSHARE_X11
  if (m_x11Observer)
    m_x11Observer->fenceChanged();
#endif
  const auto changed = onForwardingFenceChanged;
  if (changed)
    changed();
}

void Viewer::disconnected()
{
  // Controller/host releases native ownership on disconnect. No writes can be
  // attempted after the channel callback or its capture has been destroyed.
  m_connected = false;
  m_videoRecoveryTimeout.stop();
  const QPointer<Viewer> guard(this);
  releaseInput(KeyboardRouteReason::Disconnected);
  if (!guard)
    return;
  m_resizeDebounce.stop();
  m_resizeTimeout.stop();
  m_error = QStringLiteral("Disconnected — restoring this window on the Mac");
  setWindowTitle(windowTitle() + QStringLiteral(" (disconnected)"));
  update();
}

bool Viewer::placeOnScreen(int screenIndex)
{
  const auto screens = QGuiApplication::screens();
  if (screenIndex < 0 || screenIndex >= screens.size())
    return false;
  QScreen *target = screens.at(screenIndex);
  // Qt's native screen request also works before the first Wayland surface is
  // mapped. Exact placement on Wayland remains a compositor policy.
  winId();
  if (windowHandle())
    windowHandle()->setScreen(target);
  move(target->availableGeometry().center() - rect().center());
  return true;
}

QRectF Viewer::videoRect() const
{
  const QSizeF original = m_image.isNull() ? m_source : QSizeF(m_image.size());
  if (original.isEmpty() || width() <= 0 || height() <= 0)
    return {};
  const QSizeF fitted = original.scaled(QSizeF(size()), Qt::KeepAspectRatio);
  return QRectF((width() - fitted.width()) / 2, (height() - fitted.height()) / 2, fitted.width(), fitted.height());
}

std::optional<QPointF> Viewer::mapToSource(QPointF position, bool clampDrag) const
{
  const QRectF viewport = videoRect();
  const auto point = mapPoint(
      {position.x(), position.y()}, {viewport.x(), viewport.y(), viewport.width(), viewport.height()},
      {0, 0, m_source.width(), m_source.height()}, clampDrag
  );
  return point ? std::optional<QPointF>(QPointF(point->x, point->y)) : std::nullopt;
}

bool Viewer::event(QEvent *event)
{
  switch (event->type()) {
  case QEvent::Move:
  case QEvent::Resize:
  case QEvent::WinIdChange:
  case QEvent::ScreenChangeInternal:
  case QEvent::Show:
  case QEvent::Hide:
  case QEvent::WindowActivate:
  case QEvent::WindowDeactivate:
  case QEvent::WindowStateChange:
  case QEvent::PlatformSurface:
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide || event->type() == QEvent::PlatformSurface) {
      if (m_waylandSurfaceIncarnation == std::numeric_limits<std::uint64_t>::max())
        m_viewportClosed = true;
      else
        ++m_waylandSurfaceIncarnation;
    }
    invalidateViewport();
    break;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  case QEvent::DevicePixelRatioChange:
    invalidateViewport();
    break;
#endif
  default:
    break;
  }
  switch (event->type()) {
  case QEvent::WindowActivate:
    m_localActive = true;
    m_decodeThread.setPriority(QThread::NormalPriority);
    requestFocus();
    break;
  case QEvent::WindowDeactivate: {
    m_localActive = false;
    const QPointer<Viewer> guard(this);
    releaseInput();
    if (!guard)
      return true;
    m_decodeThread.setPriority(QThread::LowPriority);
    break;
  }
  case QEvent::WindowStateChange:
    if (isMinimized()) {
      m_localActive = false;
      const QPointer<Viewer> guard(this);
      pauseVideoForHiddenViewer();
      if (!guard)
        return true;
      sendVisibility(false);
      if (!guard)
        return true;
    } else {
      const QPointer<Viewer> guard(this);
      sendVisibility(true);
      if (!guard)
        return true;
      // Qt may dispatch WindowStateChange before the surface is actually
      // visible. Resume on the next owner-thread turn only when visible.
      QTimer::singleShot(0, this, [this] { resumeVideoAfterVisibility(); });
    }
    break;
  case QEvent::Hide: {
    m_localActive = false;
    const QPointer<Viewer> guard(this);
    pauseVideoForHiddenViewer();
    if (!guard)
      return true;
    sendVisibility(false);
    if (!guard)
      return true;
    break;
  }
  case QEvent::Show: {
    const QPointer<Viewer> guard(this);
    sendVisibility(true);
    if (!guard)
      return true;
    QTimer::singleShot(0, this, [this] { resumeVideoAfterVisibility(); });
    break;
  }
  case QEvent::ShortcutOverride:
    if (m_connected && m_committed && m_localActive) {
      event->accept();
      return true;
    }
    break;
  case QEvent::KeyPress:
    // QWidget ordinarily consumes Tab for focus traversal before keyPressEvent.
    keyPressEvent(static_cast<QKeyEvent *>(event));
    return true;
  case QEvent::KeyRelease:
    keyReleaseEvent(static_cast<QKeyEvent *>(event));
    return true;
  default:
    break;
  }
  return QWidget::event(event);
}

void Viewer::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  painter.fillRect(rect(), QColor(17, 20, 24));
  if (!m_image.isNull()) {
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(videoRect(), m_image);
    const auto wayland = readWaylandViewport(windowHandle());
    if (wayland &&
        (!m_waylandAssociation || !currentWaylandAssociation(windowHandle(), *wayland, *m_waylandAssociation))) {
      invalidateViewport();
      m_waylandAssociation = m_family ? (familyParent() && familyParent()->m_waylandAssociation
                                             ? associateWaylandWindow(
                                                   windowHandle(), *wayland, familyParent()->windowHandle(),
                                                   &*familyParent()->m_waylandAssociation
                                               )
                                             : std::nullopt)
                                      : associateWaylandWindow(windowHandle(), *wayland);
    } else if (!wayland)
      m_waylandAssociation.reset();
    if (m_paintedGeometry != m_frameGeometry || m_paintedClient != size() || m_paintedImage != m_image.size() ||
        m_paintedVideo != videoRect() || m_paintedDpr != devicePixelRatioF() || m_paintedWayland != wayland) {
      if (m_waylandPaintRevision == UINT64_MAX)
        m_viewportClosed = true;
      else
        ++m_waylandPaintRevision;
      invalidateViewport();
    }
    if (wayland && (!m_paintedWayland || wayland->surface != m_paintedWayland->surface ||
                    wayland->resource != m_paintedWayland->resource)) {
      if (m_waylandSurfaceIncarnation == std::numeric_limits<std::uint64_t>::max())
        m_viewportClosed = true;
      else
        ++m_waylandSurfaceIncarnation;
    }
    m_paintedWayland = wayland;
    m_paintedClient = size();
    m_paintedImage = m_image.size();
    m_paintedVideo = videoRect();
    m_paintedDpr = devicePixelRatioF();
    m_paintedGeometry = m_frameGeometry;
    if (m_framePaintSerial == ((std::uint64_t{1} << 53) - 1))
      m_viewportClosed = true;
    else
      ++m_framePaintSerial;
    if (m_waitingRecoveryPaint && m_videoRecoveryFramePaintReady && m_paintedGeometry == m_geometry) {
      // An actual Qt paint, rather than a backend submission or decode callback,
      // is the first point at which this viewer can offer interaction again.
      m_videoRecovering = false;
      m_waitingRecoveryPaint = false;
      m_videoRecoveryFramePaintReady = false;
      m_videoRecoveryTimeout.stop();
    }
  }
  if (m_image.isNull() || !m_error.isEmpty()) {
    const QString text = m_error.isEmpty() ? QStringLiteral("Preparing Mac window…") : m_error;
    if (!m_image.isNull())
      painter.fillRect(rect(), QColor(17, 20, 24, 210));
    painter.setPen(QColor(224, 227, 233));
    painter.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap, text);
  }
  painter.end();
  if (!m_image.isNull() && m_paintedGeometry == m_geometry && m_connected && !m_failed && !m_returned) {
    if (m_mediaMetrics && m_error.isEmpty() && m_metricImage && m_metricImage != m_metricPaintedImage) {
      m_metricPaintedImage = m_metricImage;
      m_mediaMetrics->paint(
          std::chrono::duration_cast<std::chrono::nanoseconds>(MediaMetrics::Clock::now() - m_metricReceivedAt).count()
      );
    }
    if (!m_readySent && !m_readyPaintScheduled) {
      m_readyPaintScheduled = true;
      QTimer::singleShot(0, this, [this] {
        m_readyPaintScheduled = false;
        if (!m_readySent && !m_image.isNull() && m_paintedGeometry == m_geometry && isVisible() &&
            send(MessageType::Ready, {{"geometry", jsonNumber(m_geometry)}})) {
          m_readySent = true;
          requestFocus();
        }
      });
    } else if (m_readySent && m_localActive && !m_focusPending && m_focusRevision == 0) {
      QTimer::singleShot(0, this, [this] { requestFocus(); });
    }
  }
}

void Viewer::focusInEvent(QFocusEvent *event)
{
  m_localActive = true;
  requestFocus();
  QWidget::focusInEvent(event);
}
void Viewer::focusOutEvent(QFocusEvent *event)
{
  m_localActive = false;
  const QPointer<Viewer> guard(this);
  releaseInput();
  if (!guard)
    return;
  QWidget::focusOutEvent(event);
}

bool Viewer::acceptsLocalInput() const
{
  return familyInputAvailable() && m_connected && !m_failed && !m_returned && !m_waitingRecoveryPaint &&
         !m_videoRecovering && !m_videoPaused && m_readySent && m_committed && m_localActive && !m_resizing &&
         !isMinimized() && m_paintedGeometry == m_geometry;
}

void Viewer::requestFocus()
{
  if (!acceptsLocalInput() || m_requireFocusIntent || m_focusPending || m_focusRevision != 0)
    return;
  if (m_focusRequest == maxJsonInteger) {
    fail(QStringLiteral("Window focus sequence exhausted"));
    return;
  }
  ++m_focusRequest;
  m_focusPending = true;
  m_focusTimeout.start(2000);
  if (!send(
          MessageType::Input, {{"kind", "focus"},
                               {"geometry", jsonNumber(m_geometry)},
                               {"request", jsonNumber(m_focusRequest)},
                               {"active", true}}
      ))
    releaseInput();
}

bool Viewer::send(MessageType type, const QJsonObject &message)
{
  if (!m_connected || !onSend)
    return false;
  const QPointer<Viewer> guard(this);
  const auto callback = onSend;
  bool sent = false;
  try {
    sent = callback(type, message, m_session, m_epoch);
  } catch (...) {
  }
  if (!guard)
    return false;
  if (sent)
    return m_connected;
  disconnected();
  return false;
}

void Viewer::recordDelivered(const QJsonObject &input)
{
  const QString kind = input.value(QStringLiteral("kind")).toString();
  if (kind != QStringLiteral("key") && kind != QStringLiteral("button"))
    return;
  auto &owners = kind == QStringLiteral("key") ? m_deliveredKeys : m_deliveredButtons;
  const int code = input.value(QStringLiteral("code")).toInt();
  if (input.value(QStringLiteral("down")).toBool())
    owners.insert(code, input);
  else
    owners.remove(code);
}

bool Viewer::sendInput(QJsonObject input)
{
  if (!acceptsLocalInput() || m_focusRevision == 0 || fencesInput(input))
    return false;
  input.insert(QStringLiteral("geometry"), jsonNumber(m_geometry));
  input.insert(QStringLiteral("focusRevision"), jsonNumber(m_focusRevision));
  // Record before the callback so synchronous quiescence releases this press.
  // Failed sends disconnect and clear the ledger through releaseInput().
  recordDelivered(input);
  return send(MessageType::Input, input);
}

void Viewer::queueInput(QJsonObject input)
{
  if (!acceptsLocalInput() || fencesInput(input))
    return;
  if (m_requireFocusIntent) {
    const QString kind = input.value(QStringLiteral("kind")).toString();
    const bool deliberate =
        kind == QStringLiteral("scroll") ||
        ((kind == QStringLiteral("key") || kind == QStringLiteral("button")) &&
         input.value(QStringLiteral("down")).toBool() && !input.value(QStringLiteral("repeat")).toBool());
    if (!deliberate)
      return;
    m_requireFocusIntent = false;
  }
  input.insert(QStringLiteral("geometry"), jsonNumber(m_geometry));
  if (m_focusRevision != 0) {
    sendInput(std::move(input));
    return;
  }
  // Preserve the click that activates a window. Only redundant pointer moves
  // are coalesced; down/up and scrolling retain their original ordering.
  if (!m_pendingInputs.isEmpty() && input.value(QStringLiteral("kind")) == QStringLiteral("move") &&
      m_pendingInputs.back().value(QStringLiteral("kind")) == QStringLiteral("move")) {
    m_pendingInputs.back() = std::move(input);
  } else if (m_pendingInputs.size() < maxPendingInput) {
    m_pendingInputs.push_back(std::move(input));
  } else {
    const QPointer<Viewer> guard(this);
    releaseInput(KeyboardRouteReason::Failed);
    if (!guard)
      return;
    if (onError)
      onError(QStringLiteral("Too much input arrived before focus was acknowledged; input was released"));
    return;
  }
  requestFocus();
}

void Viewer::releaseInput(KeyboardRouteReason reason)
{
  invalidateViewport();
  const QPointer<Viewer> guard(this);
  const bool wasHandling = m_keyboardRouteHandling;
  m_keyboardRouteHandling = true;
  invalidateKeyboardRoute(reason);
  if (!guard)
    return;
  const bool hadLease =
      m_focusRevision != 0 || m_focusPending || !m_deliveredKeys.isEmpty() || !m_deliveredButtons.isEmpty();
  const auto keys = std::move(m_deliveredKeys);
  const auto buttons = std::move(m_deliveredButtons);
  m_deliveredKeys.clear();
  m_deliveredButtons.clear();
  m_pendingInputs.clear();
  if (m_inputPreparation || m_keyboardBinding)
    for (auto held = m_heldKeys.cbegin(); held != m_heldKeys.cend(); ++held)
      m_suppressedKeys.insert(held.key());
  if (m_inputPreparation || (m_keyboardBinding && m_keyboardBinding->scope == ForwardingScope::All))
    for (auto held = m_heldButtons.cbegin(); held != m_heldButtons.cend(); ++held)
      m_suppressedButtons.insert(held.key());
  m_heldKeys.clear();
  m_heldButtons.clear();
  m_focusTimeout.stop();
  if (m_connected && onSend) {
    for (const auto &down : keys) {
      auto up = down;
      up.insert(QStringLiteral("down"), false);
      up.insert(QStringLiteral("repeat"), false);
      up.insert(QStringLiteral("modifiers"), 0);
      send(MessageType::Input, up);
      if (!guard)
        return;
    }
    for (const auto &down : buttons) {
      auto up = down;
      up.insert(QStringLiteral("down"), false);
      up.insert(QStringLiteral("modifiers"), 0);
      up.insert(QStringLiteral("x"), m_lastSource.x());
      up.insert(QStringLiteral("y"), m_lastSource.y());
      send(MessageType::Input, up);
      if (!guard)
        return;
    }
    if (hadLease) {
      send(
          MessageType::Input, {{"kind", "release"},
                               {"geometry", jsonNumber(m_geometry)},
                               {"focusRevision", jsonNumber(m_focusRevision)},
                               {"request", jsonNumber(m_focusRequest)},
                               {"active", false}}
      );
      if (!guard)
        return;
    }
  }
  m_focusRevision = 0;
  m_focusPending = false;
  m_keyboardRouteHandling = wasHandling;
}

QJsonObject Viewer::keyInput(const QKeyEvent *event, bool down) const
{
  return {
      {"kind", "key"},
      {"code", event->key()},
      {"down", down},
      {"repeat", event->isAutoRepeat()},
      {"text", event->text()},
      {"native", static_cast<qint64>(event->nativeScanCode())},
      {"modifiers", static_cast<int>(event->modifiers())}
  };
}

void Viewer::inputMethodEvent(QInputMethodEvent *event)
{
  // Explicitly reject even directly delivered or queued composition events.
  // Ordinary physical key routing remains governed by the existing owner fence.
  event->ignore();
}

void Viewer::keyPressEvent(QKeyEvent *event)
{
  event->accept();
  if (event->key() == Qt::Key_unknown)
    return;
  if (m_inputPreparation || m_keyboardBinding || m_suppressedKeys.contains(event->key())) {
    m_suppressedKeys.insert(event->key());
    return;
  }
  if (!acceptsLocalInput())
    return;
  if (m_heldKeys.contains(event->key()) && !event->isAutoRepeat())
    return;
  if (!m_heldKeys.contains(event->key()) && event->isAutoRepeat())
    return;
  const auto input = keyInput(event, true);
  m_heldKeys.insert(event->key(), input);
  queueInput(input);
}

void Viewer::keyReleaseEvent(QKeyEvent *event)
{
  event->accept();
  if (event->isAutoRepeat() || m_suppressedKeys.remove(event->key()) || !m_heldKeys.remove(event->key()))
    return;
  queueInput(keyInput(event, false));
}

int Viewer::buttonCode(Qt::MouseButton button)
{
  if (button == Qt::LeftButton)
    return 0;
  if (button == Qt::RightButton)
    return 1;
  if (button == Qt::MiddleButton)
    return 2;
  const unsigned bits = static_cast<unsigned>(button);
  if (bits == 0 || (bits & (bits - 1)) != 0)
    return -1;
  int code = 0;
  for (unsigned value = bits; value > 1; value >>= 1)
    ++code;
  return code >= 3 && code <= 15 ? code : -1;
}

void Viewer::mousePressEvent(QMouseEvent *event)
{
  mouseButtonDown(event, 1);
}

void Viewer::mouseDoubleClickEvent(QMouseEvent *event)
{
  mouseButtonDown(event, 2);
}

void Viewer::mouseButtonDown(QMouseEvent *event, int clickCount)
{
  event->accept();
  const int code = buttonCode(event->button());
  if (code < 0)
    return;
  if (fencesPointer()) {
    m_suppressedButtons.insert(code);
    return;
  }
  if (m_familyBlocked && onDismissFamily && m_connected && !m_failed && !m_returned && isVisible()) {
    // Consume this actual outside press as dismissal, never replay it into the
    // parent after the popup disappears. A native input fence still wins.
    m_suppressedButtons.insert(code);
    const auto dismiss = onDismissFamily;
    dismiss();
    return;
  }
  const auto point = mapToSource(event->position());
  if (!point || !acceptsLocalInput() || m_heldButtons.contains(code))
    return;
  m_lastSource = *point;
  QJsonObject input{
      {"kind", "button"},
      {"code", code},
      {"down", true},
      {"x", point->x()},
      {"y", point->y()},
      {"clickCount", clickCount},
      {"modifiers", static_cast<int>(event->modifiers())}
  };
  m_heldButtons.insert(code, input);
  queueInput(input);
}

void Viewer::mouseReleaseEvent(QMouseEvent *event)
{
  event->accept();
  const int code = buttonCode(event->button());
  if (code < 0 || m_suppressedButtons.remove(code))
    return;
  const auto held = m_heldButtons.find(code);
  if (held == m_heldButtons.end())
    return;
  const int clickCount = held->value(QStringLiteral("clickCount")).toInt(1);
  m_heldButtons.erase(held);
  const auto point = mapToSource(event->position(), true);
  if (!point)
    return;
  m_lastSource = *point;
  queueInput(
      {{"kind", "button"},
       {"code", code},
       {"down", false},
       {"x", point->x()},
       {"y", point->y()},
       {"clickCount", clickCount},
       {"modifiers", static_cast<int>(event->modifiers())}}
  );
}

void Viewer::mouseMoveEvent(QMouseEvent *event)
{
  event->accept();
  if (fencesPointer())
    return;
  const auto point = mapToSource(event->position(), !m_heldButtons.isEmpty());
  if (!point || !acceptsLocalInput())
    return;
  m_lastSource = *point;
  queueInput(
      {{"kind", "move"}, {"x", point->x()}, {"y", point->y()}, {"modifiers", static_cast<int>(event->modifiers())}}
  );
}

void Viewer::wheelEvent(QWheelEvent *event)
{
  event->accept();
  if (fencesPointer())
    return;
  const auto point = mapToSource(event->position());
  if (!point || !acceptsLocalInput())
    return;
  m_lastSource = *point;
  const QPointF delta = event->pixelDelta().isNull() ? QPointF(event->angleDelta()) / 3 : QPointF(event->pixelDelta());
  queueInput(
      {{"kind", "scroll"},
       {"dx", delta.x()},
       {"dy", delta.y()},
       {"x", point->x()},
       {"y", point->y()},
       {"modifiers", static_cast<int>(event->modifiers())}}
  );
}

void Viewer::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  if (!m_family && !m_applyingMetadata && m_readySent && m_committed && m_connected && !isMinimized())
    scheduleResize();
}

void Viewer::scheduleResize()
{
  const QPointer<Viewer> guard(this);
  releaseInput(KeyboardRouteReason::GeometryChanged);
  if (!guard)
    return;
  m_resizing = true;
  m_resizeDebounce.start(150);
}

void Viewer::sendVisibility(bool visible)
{
  if (m_visibility == visible || !m_connected || m_returned)
    return;
  m_visibility = visible;
  send(
      MessageType::Input, {{"kind", "visibility"},
                           {"visible", visible},
                           {"active", false},
                           {"geometry", jsonNumber(m_geometry)},
                           {"focusRevision", jsonNumber(m_focusRevision)}}
  );
}

void Viewer::closeEvent(QCloseEvent *event)
{
  if (!m_returned) {
    const QPointer<Viewer> guard(this);
    releaseInput(KeyboardRouteReason::Returned);
    if (!guard)
      return;
    m_resizeDebounce.stop();
    m_resizeTimeout.stop();
    m_videoRecoveryTimeout.stop();
    send(MessageType::Return, {{"geometry", jsonNumber(m_geometry)}});
    m_returned = true;
    if (onReturned)
      QTimer::singleShot(0, this, [this] {
        if (onReturned)
          onReturned();
      });
  }
  event->accept();
}

void Viewer::fail(const QString &message)
{
  if (m_failed)
    return;
  const QPointer<Viewer> guard(this);
  releaseInput(KeyboardRouteReason::Failed);
  if (!guard)
    return;
  m_failed = true;
  m_error = message;
  m_resizeDebounce.stop();
  m_resizeTimeout.stop();
  m_videoRecoveryTimeout.stop();
  send(MessageType::Return, {{"reason", message}, {"geometry", jsonNumber(m_geometry)}});
  if (onError)
    onError(message);
  update();
}

} // namespace deskflow::windowshare
