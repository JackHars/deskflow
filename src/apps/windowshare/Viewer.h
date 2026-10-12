/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "Core.h"
#include "KeyboardRoute.h"
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <functional>
#include <memory>

namespace deskflow::windowshare {
class VideoDecoder;

class Viewer final : public QWidget
{
public:
  Viewer(std::uint64_t session, std::uint64_t epoch, const QJsonObject &metadata, QWidget *parent = nullptr);
  ~Viewer() override;
  void applyMetadata(const QJsonObject &metadata);
  void receiveFrame(const QByteArray &accessUnit, std::uint64_t geometry);
  void committed();
  void focusAcknowledged(std::uint64_t revision, std::uint64_t request);
  void keyboardRoute(const KeyboardRouteControl &control);
  void disconnected();
  bool placeOnScreen(int screenIndex);

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
  void requestFocus();
  void releaseInput(KeyboardRouteReason reason = KeyboardRouteReason::FocusChanged);
  void handleKeyboardRoute(const KeyboardRouteControl &control);
  bool sendKeyboardRoute(const KeyboardRouteControl &control);
  bool currentKeyboardRoute(const KeyboardRouteBinding &binding) const;
  void invalidateKeyboardRoute(KeyboardRouteReason reason);
  void quiesceKeyboard();
  void queueInput(QJsonObject input);
  bool sendInput(QJsonObject input);
  bool send(MessageType type, const QJsonObject &message);
  void fail(const QString &message);
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
  QVector<QJsonObject> m_pendingInputs;
  QTimer m_focusTimeout, m_resizeDebounce, m_resizeTimeout;
  QThread m_decodeThread;
  QObject *m_decodeWorker = nullptr;
  std::shared_ptr<VideoDecoder> m_decoder;
  int m_pendingFrames = 0, m_decodeErrors = 0;
  qsizetype m_pendingBytes = 0;
};

} // namespace deskflow::windowshare
