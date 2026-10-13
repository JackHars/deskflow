/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "ClipboardText.h"
#include "Core.h"
#include "GnomeViewportProtocol.h"
#include "KeyboardRoute.h"
#include "WaylandViewport.h"
#include <QImage>
#include <QInputMethodEvent>
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <atomic>
#include <functional>
#include <memory>
#ifdef WINDOWSHARE_X11
#include "X11CoordinateEndpoint.h"
#endif

namespace deskflow::windowshare {
class VideoDecoder;
#ifdef WINDOWSHARE_X11
class ViewerX11Observer;
struct ViewerViewportSnapshot
{
  X11DisplayObservation native;
  std::uint64_t session = 0, epoch = 0, sourceGeometry = 0, focusRequest = 0, focusRevision = 0;
  std::uint64_t surfaceIncarnation = 0, viewportRevision = 0;
  QSizeF clientLogical, sourceLogical;
  QSize imagePixels;
  QRectF videoLogical;
  double devicePixelRatio = 0;
  std::uint32_t output = 0;
};

// This only describes the last painted, focused viewport. It neither suppresses
// Qt pointer forwarding nor authorizes delivery of original input. A future
// authenticated coordinate-space binding and ordered pointer fence are required.
class ViewerViewportObservation
{
public:
  ViewerViewportObservation() = default;
  bool live() const noexcept;
  const ViewerViewportSnapshot &snapshot() const noexcept;

private:
  friend class Viewer;
  std::shared_ptr<const ViewerViewportSnapshot> m_snapshot;
  std::shared_ptr<const std::atomic_bool> m_revoked;
};
struct ViewerCoordinateUpdate
{
  X11CoordinateEndpoint::Update native;
  std::optional<ViewerViewportObservation> viewport;
  // Present only for a point inside the last-painted video rectangle. Native
  // point and painted receipts remain independently revocable and bounded.
  std::optional<QPointF> source;
};
#endif

class Viewer final : public QWidget
{
public:
  Viewer(std::uint64_t session, std::uint64_t epoch, const QJsonObject &metadata, QWidget *parent = nullptr);
  ~Viewer() override;
  void applyMetadata(const QJsonObject &metadata);
  void receiveFrame(const QByteArray &accessUnit, std::uint64_t geometry);
  // Set only once by the authenticated Bridge's capability negotiation, before
  // the first frame. Legacy peers retain fail-closed decoder-overflow behavior.
  void setMediaRecoveryEnabled(bool enabled) noexcept
  {
    m_mediaRecoveryEnabled = enabled;
  }
  bool videoRecovering() const noexcept
  {
    return m_videoRecovering;
  }
  bool videoPaused() const noexcept
  {
    return m_videoPaused;
  }
  void committed();
  void focusAcknowledged(std::uint64_t revision, std::uint64_t request);
  void keyboardRoute(const KeyboardRouteControl &control);
  bool allForwardingFenced(const KeyboardRouteBinding &binding) const;
  void disconnected();
  std::optional<ClipboardFocusObservation> clipboardFocus() const;
  bool placeOnScreen(int screenIndex);
  // Unsupported platforms/plugins report unavailable. No Qt global-position
  // fallback is used to manufacture a native desktop coordinate space.
  QString viewportUnavailableReason() const;
  std::optional<GnomeViewportState> waylandViewportState() const;
  std::optional<QPointF> mapWaylandSurface(const GnomeViewportState &expected, QPointF point) const;
  std::optional<GnomePreparationTarget> waylandPreparationTarget() const;
  std::optional<QPointF> mapPreparationTarget(const GnomePreparationTarget &expected, QPointF point) const;
  bool beginInputPreparation(const InputPreparationBinding &binding);
  bool inputPreparationFenced(const InputPreparationBinding &binding) const;
  // Focus-control intent under an already closed preparation gate. The owned
  // provider must select/activate the native target before calling this; this
  // method never delivers a captured key or button.
  bool requestInputPreparationFocus(const InputPreparationBinding &binding);
  bool finishInputPreparation(const InputPreparationBinding &binding, const std::optional<KeyboardRouteBinding> &all);
  std::optional<GnomeViewportState>
  requestPreparedFocus(const InputPreparationBinding &binding, const GnomePreparationTarget &target);
  // Coalesced owner-thread notification, after the invalidating event unwinds.
  // A snapshot/map query still rechecks all state synchronously at use.
  std::function<void()> onViewportInvalidated;
  // Synchronous revocation for a retained private queue receipt, before a
  // resume acknowledgement or any newly forwarded input can be emitted.
  std::function<void()> onForwardingFenceChanged;
#ifdef WINDOWSHARE_X11
  std::optional<ViewerViewportObservation> viewportObservation() const;
  bool readX11Witness(X11WitnessLocator locator, X11CoordinateClock::time_point deadline);
  bool validateX11Point(X11PointRequest request, const KeyboardRouteBinding &fence);
  std::optional<ViewerCoordinateUpdate> takeX11CoordinateUpdate();
  bool cancelX11Coordinate(std::uint64_t request);
#endif

  std::function<bool(MessageType, const QJsonObject &, std::uint64_t, std::uint64_t)> onSend;
  std::function<bool(const KeyboardRouteControl &)> onKeyboardRouteSend;
  std::function<void(QString)> onError;
  std::function<void()> onReturned;

  // Read-only state also permits headless tests without exposing input bypasses.
  QRectF videoRect() const;
  std::optional<QPointF> mapToSource(QPointF position, bool clampDrag = false) const;
  bool firstFramePainted() const
  {
    return m_readySent;
  }
  std::uint64_t geometryRevision() const
  {
    return m_geometry;
  }

protected:
  bool event(QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void focusInEvent(QFocusEvent *event) override;
  void focusOutEvent(QFocusEvent *event) override;
  void inputMethodEvent(QInputMethodEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void keyReleaseEvent(QKeyEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void closeEvent(QCloseEvent *event) override;

private:
  void invalidateViewport();
  void forwardingFenceChanged();
  std::shared_ptr<std::atomic_bool> m_viewportRevoked = std::make_shared<std::atomic_bool>(false);
  std::uint64_t m_viewportRevision = 1, m_waylandSurfaceIncarnation = 1, m_waylandPaintRevision = 1;
  std::optional<InputPreparationBinding> m_inputPreparation;
  std::uint64_t m_preparationSerial = 0;
  QSize m_paintedClient, m_paintedImage;
  QRectF m_paintedVideo;
  double m_paintedDpr = 0;
  bool m_viewportClosed = false;
  std::optional<WaylandViewportGeometry> m_paintedWayland;
  std::optional<WaylandWindowAssociation> m_waylandAssociation;
  QTimer m_viewportNotification;
#ifdef WINDOWSHARE_X11
  friend class ViewerX11Observer;
  std::unique_ptr<ViewerX11Observer> m_x11Observer;
  std::uint64_t m_surfaceIncarnation = 0;
#endif
  void requestFocus();
  void releaseInput(KeyboardRouteReason reason = KeyboardRouteReason::FocusChanged);
  void handleKeyboardRoute(const KeyboardRouteControl &control);
  bool sendKeyboardRoute(const KeyboardRouteControl &control);
  bool currentKeyboardRoute(const KeyboardRouteBinding &binding) const;
  bool fencesInput(const QJsonObject &input) const;
  bool fencesPointer() const;
  void invalidateKeyboardRoute(KeyboardRouteReason reason);
  void quiesceKeyboard();
  void queueInput(QJsonObject input);
  bool sendInput(QJsonObject input);
  bool send(MessageType type, const QJsonObject &message);
  void fail(const QString &message);
  void beginVideoRecovery();
  void requestVideoRecoveryWhenDrained();
  void pauseVideoForHiddenViewer();
  void resumeVideoAfterVisibility();
  void scheduleResize();
  void sendVisibility(bool visible);
  bool acceptsLocalInput() const;
  void recordDelivered(const QJsonObject &input);
  QJsonObject keyInput(const QKeyEvent *event, bool down) const;
  void mouseButtonDown(QMouseEvent *event, int clickCount);
  static int buttonCode(Qt::MouseButton button);

  const std::uint64_t m_session, m_epoch;
  std::uint64_t m_geometry = 0, m_frameGeometry = 0, m_paintedGeometry = 0;
  std::uint64_t m_focusRevision = 0, m_focusRequest = 0;
  std::uint64_t m_resizeRequest = 0, m_resizeBaseGeometry = 0;
  std::uint64_t m_keyboardGeneration = 0;
  std::uint64_t m_keyboardFencedGeneration = 0, m_pointerFencedGeneration = 0;
  std::optional<KeyboardRouteBinding> m_keyboardBinding;
  bool m_keyboardBindingValid = false, m_keyboardRouteHandling = false;
  QSizeF m_source;
  QImage m_image;
  QPointF m_lastSource;
  bool m_connected = true, m_committed = false, m_readySent = false;
  bool m_localActive = false, m_focusPending = false, m_resizing = false;
  bool m_requireFocusIntent = false;
  bool m_applyingMetadata = false, m_returned = false, m_failed = false;
  bool m_visibility = true;
  bool m_readyPaintScheduled = false;
  QString m_error;
  QMap<int, QJsonObject> m_heldKeys, m_heldButtons, m_deliveredKeys, m_deliveredButtons;
  QSet<int> m_suppressedKeys;
  QSet<int> m_suppressedButtons;
  QVector<QJsonObject> m_pendingInputs;
  QTimer m_focusTimeout, m_resizeDebounce, m_resizeTimeout, m_videoRecoveryTimeout;
  bool m_mediaRecoveryEnabled = false, m_videoRecovering = false, m_videoPaused = false;
  bool m_videoRecoveryRequestSent = false, m_videoRecoveryFrameQueued = false;
  bool m_videoRecoveryFramePaintReady = false, m_waitingRecoveryPaint = false;
  std::uint64_t m_mediaRecoverySequence = 0;
  // Only the decoder worker uses the FFmpeg state. This atomic lets stale
  // queued work skip decoding after a recovery invalidation without calling
  // FFmpeg from the GUI thread. GUI completions still account every byte.
  std::shared_ptr<std::atomic<std::uint64_t>> m_decodeGeneration = std::make_shared<std::atomic<std::uint64_t>>(1);
  QThread m_decodeThread;
  QObject *m_decodeWorker = nullptr;
  std::shared_ptr<VideoDecoder> m_decoder;
  int m_pendingFrames = 0, m_decodeErrors = 0;
  qsizetype m_pendingBytes = 0;
};

} // namespace deskflow::windowshare
