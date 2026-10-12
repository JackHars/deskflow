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
#include <QPointer>
#include <QResizeEvent>
#include <QScreen>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <limits>

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

Viewer::Viewer(std::uint64_t session, std::uint64_t epoch, const QJsonObject &metadata, QWidget *parent)
    : QWidget(parent, Qt::Window),
      m_session(session),
      m_epoch(epoch),
      m_decoder(std::make_shared<VideoDecoder>())
{
  setAttribute(Qt::WA_OpaquePaintEvent);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setMinimumSize(96, 64);
  setWindowTitle(QStringLiteral("Mac window — Deskflow"));
  m_focusTimeout.setSingleShot(true);
  m_resizeDebounce.setSingleShot(true);
  m_resizeTimeout.setSingleShot(true);
  connect(&m_focusTimeout, &QTimer::timeout, this, [this] {
    releaseInput(KeyboardRouteReason::Timeout);
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
}

Viewer::~Viewer()
{
  releaseInput(KeyboardRouteReason::Returned);
  m_connected = false;
  // Decoding never runs on the GUI/event-tap path. Wait only for the current
  // bounded access unit; queued invocations are discarded when the loop exits.
  m_decodeThread.quit();
  m_decodeThread.wait();
}

void Viewer::applyMetadata(const QJsonObject &metadata)
{
  if (m_failed || m_returned)
    return;
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
  if (!firstMetadata && geometry != m_geometry)
    releaseInput(KeyboardRouteReason::GeometryChanged);
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
  const std::uint64_t acknowledged = revision(metadata.value(QStringLiteral("resizeRequest")));
  if (m_resizing && !m_resizeDebounce.isActive() &&
      ((acknowledged != 0 && acknowledged == m_resizeRequest) || (acknowledged == 0 && geometry > m_resizeBaseGeometry)
      )) {
    m_resizing = false;
    m_resizeTimeout.stop();
  }
  if (metadata.value(QStringLiteral("phase")).toString() == QStringLiteral("remote"))
    committed();
  update();
}

void Viewer::receiveFrame(const QByteArray &accessUnit, std::uint64_t geometry)
{
  if (!m_connected || m_returned || m_failed)
    return;
  // An encoded frame carries the geometry of the capture that produced it.
  // Arrival after newer metadata does not make an older frame current.
  if (geometry != m_geometry)
    return;
  if (accessUnit.isEmpty() || accessUnit.size() > maxPayload) {
    fail(QStringLiteral("Invalid video packet size"));
    return;
  }
  if (m_pendingFrames >= maxPendingFrames || accessUnit.size() > maxPendingVideoBytes - m_pendingBytes) {
    // Dropping arbitrary interframes would corrupt H.264 reference state. Return
    // safely instead of building an unbounded stale-input/video queue.
    fail(QStringLiteral("Video decoding cannot keep up; returning the window to the Mac"));
    return;
  }
  ++m_pendingFrames;
  m_pendingBytes += accessUnit.size();
  const QPointer<Viewer> target(this);
  const auto decoder = m_decoder;
  QMetaObject::invokeMethod(
      m_decodeWorker,
      [target, decoder, accessUnit, geometry] {
        auto result = decoder->decode(accessUnit);
        if (!target)
          return;
        QMetaObject::invokeMethod(
            target,
            [target, result = std::move(result), size = accessUnit.size(), geometry]() mutable {
              if (!target)
                return;
              auto *self = target.data();
              --self->m_pendingFrames;
              self->m_pendingBytes -= size;
              if (!self->m_connected || self->m_returned || self->m_failed || geometry != self->m_geometry)
                return;
              if (!result.error.isEmpty()) {
                if (++self->m_decodeErrors >= 3)
                  self->fail(result.error);
                else if (self->onError)
                  self->onError(result.error);
                return;
              }
              if (result.image.isNull())
                return;
              self->m_decodeErrors = 0;
              self->m_image = std::move(result.image);
              self->m_frameGeometry = geometry;
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
  m_committed = true;
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
  auto inputs = std::move(m_pendingInputs);
  m_pendingInputs.clear();
  for (auto &input : inputs) {
    if (!acceptsLocalInput() || m_focusRevision != lease ||
        revision(input.value(QStringLiteral("geometry"))) != m_geometry)
      break;
    if (m_keyboardBinding && input.value(QStringLiteral("kind")) == QStringLiteral("key"))
      continue;
    if (!sendInput(std::move(input)))
      break;
  }
}

bool Viewer::currentKeyboardRoute(const KeyboardRouteBinding &binding) const
{
  return acceptsLocalInput() && isVisible() && m_visibility && !m_applyingMetadata && !m_focusPending &&
         binding.owner == Owner{m_session, m_epoch} && binding.focusRequest == m_focusRequest &&
         binding.focusRevision == m_focusRevision && binding.geometry == m_geometry;
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
    // The acknowledgement precedes any newly forwarded key on the same stream.
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
    // Install the fence before releasing keys or invoking either send callback.
    m_keyboardBinding = binding;
    m_keyboardBindingValid = true;
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
  for (auto held = m_heldKeys.cbegin(); held != m_heldKeys.cend(); ++held)
    m_suppressedKeys.insert(held.key());
  m_heldKeys.clear();
  m_pendingInputs.erase(
      std::remove_if(
          m_pendingInputs.begin(), m_pendingInputs.end(),
          [](const QJsonObject &input) { return input.value(QStringLiteral("kind")) == QStringLiteral("key"); }
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
  if (m_connected)
    sendKeyboardRoute(
        {binding, KeyboardRouteAction::Withdraw, KeyboardRouteMode::Original, KeyboardRouteResult::None, reason}
    );
}

void Viewer::disconnected()
{
  // Controller/host releases native ownership on disconnect. No writes can be
  // attempted after the channel callback or its capture has been destroyed.
  m_connected = false;
  releaseInput(KeyboardRouteReason::Disconnected);
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
  case QEvent::WindowActivate:
    m_localActive = true;
    m_decodeThread.setPriority(QThread::NormalPriority);
    requestFocus();
    break;
  case QEvent::WindowDeactivate:
    m_localActive = false;
    releaseInput();
    m_decodeThread.setPriority(QThread::LowPriority);
    break;
  case QEvent::WindowStateChange:
    if (isMinimized()) {
      m_localActive = false;
      releaseInput(KeyboardRouteReason::Hidden);
      sendVisibility(false);
    } else {
      sendVisibility(true);
    }
    break;
  case QEvent::Hide:
    m_localActive = false;
    releaseInput(KeyboardRouteReason::Hidden);
    sendVisibility(false);
    break;
  case QEvent::Show:
    sendVisibility(true);
    break;
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
    m_paintedGeometry = m_frameGeometry;
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
  releaseInput();
  QWidget::focusOutEvent(event);
}

bool Viewer::acceptsLocalInput() const
{
  return m_connected && !m_failed && !m_returned && m_readySent && m_committed && m_localActive && !m_resizing &&
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
  if (!acceptsLocalInput() || m_focusRevision == 0 ||
      (m_keyboardBinding && input.value(QStringLiteral("kind")) == QStringLiteral("key")))
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
  if (!acceptsLocalInput() || (m_keyboardBinding && input.value(QStringLiteral("kind")) == QStringLiteral("key")))
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
    releaseInput(KeyboardRouteReason::Failed);
    if (onError)
      onError(QStringLiteral("Too much input arrived before focus was acknowledged; input was released"));
    return;
  }
  requestFocus();
}

void Viewer::releaseInput(KeyboardRouteReason reason)
{
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
  if (m_keyboardBinding)
    for (auto held = m_heldKeys.cbegin(); held != m_heldKeys.cend(); ++held)
      m_suppressedKeys.insert(held.key());
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

void Viewer::keyPressEvent(QKeyEvent *event)
{
  event->accept();
  if (event->key() == Qt::Key_unknown)
    return;
  if (m_keyboardBinding || m_suppressedKeys.contains(event->key())) {
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
  const auto point = mapToSource(event->position());
  if (code < 0 || !point || !acceptsLocalInput() || m_heldButtons.contains(code))
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
  const auto held = m_heldButtons.find(code);
  if (code < 0 || held == m_heldButtons.end())
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
  if (!m_applyingMetadata && m_readySent && m_committed && m_connected && !isMinimized())
    scheduleResize();
}

void Viewer::scheduleResize()
{
  releaseInput(KeyboardRouteReason::GeometryChanged);
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
    releaseInput(KeyboardRouteReason::Returned);
    m_resizeDebounce.stop();
    m_resizeTimeout.stop();
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
  releaseInput(KeyboardRouteReason::Failed);
  m_failed = true;
  m_error = message;
  m_resizeDebounce.stop();
  m_resizeTimeout.stop();
  send(MessageType::Return, {{"reason", message}, {"geometry", jsonNumber(m_geometry)}});
  if (onError)
    onError(message);
  update();
}

} // namespace deskflow::windowshare
