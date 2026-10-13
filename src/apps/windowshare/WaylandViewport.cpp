/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WaylandViewport.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPointer>
#include <QThread>
#include <QWindow>
#include <cmath>
#include <cerrno>
#include <utility>
#if defined(WINDOWSHARE_QT_WAYLAND) && QT_VERSION_MAJOR == 6 && (QT_VERSION_MINOR == 8 || QT_VERSION_MINOR == 10)
#define WINDOWSHARE_SUPPORTED_QT_WAYLAND 1
#include <QtGui/private/qhighdpiscaling_p.h>
#include <QtGui/qpa/qwindowsysteminterface.h>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#endif

namespace deskflow::windowshare {
std::optional<WaylandViewportGeometry> readWaylandViewport(QWindow *window, QString *reason)
{
  auto deny = [&](const char *text) -> std::optional<WaylandViewportGeometry> {
    if (reason)
      *reason = QString::fromLatin1(text);
    return {};
  };
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  if (QString::fromLatin1(qVersion()) != QStringLiteral(QT_VERSION_STR))
    return deny("unsupported-qt-wayland-version");
  if (!qGuiApp || QThread::currentThread() != qGuiApp->thread() ||
      !QGuiApplication::platformName().startsWith(QStringLiteral("wayland")) || !window || !window->isVisible())
    return deny("native-wayland-window-unavailable");
  auto *platform = dynamic_cast<QtWaylandClient::QWaylandWindow *>(window->handle());
  if (!platform || !platform->wlSurface() || platform->subSurfaceWindow() ||
      window->surfaceType() != QSurface::RasterSurface || !platform->isExposed())
    return deny("unsupported-wayland-surface");
  const auto margins = platform->clientSideMargins();
  const auto extent = platform->surfaceSize();
  const double factor = QHighDpiScaling::factor(window);
  if (extent.isEmpty() || !std::isfinite(factor) || factor < 0.0625 || factor > 16 || margins.left() < 0 ||
      margins.top() < 0 || margins.right() < 0 || margins.bottom() < 0)
    return deny("unsupported-wayland-content-transform");
  auto *surface = platform->wlSurface();
  const auto id = wl_proxy_get_id(reinterpret_cast<wl_proxy *>(surface));
  if (!id)
    return deny("missing-wayland-surface-identity");
  if (reason)
    reason->clear();
  return WaylandViewportGeometry{
      reinterpret_cast<quintptr>(surface), id, QSizeF(extent), QPointF(margins.left(), margins.top()), factor
  };
#else
  (void)window;
  return deny("unsupported-qt-wayland-build");
#endif
}
std::optional<QPointF> mapWaylandViewport(QWindow *window, const WaylandViewportGeometry &expected, QPointF point)
{
  const auto current = readWaylandViewport(window);
  if (!current || *current != expected || !std::isfinite(point.x()) || !std::isfinite(point.y()))
    return {};
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  auto *platform = dynamic_cast<QtWaylandClient::QWaylandWindow *>(window->handle());
  const auto local = QHighDpi::fromNativeLocalPosition(platform->mapFromWlSurface(point), window);
  const auto fresh = readWaylandViewport(window);
  if (!fresh || *fresh != expected || !std::isfinite(local.x()) || !std::isfinite(local.y()))
    return {};
  return local;
#else
  return {};
#endif
}

QtWaylandQueueWitness::QtWaylandQueueWitness(QObject *parent) : QObject(parent)
{
  m_timeout.setSingleShot(true);
  connect(&m_timeout, &QTimer::timeout, this, [this] { finish(false); });
}

QtWaylandQueueWitness::~QtWaylandQueueWitness()
{
  cancel();
}

void QtWaylandQueueWitness::cancel() noexcept
{
  m_timeout.stop();
  if (m_windowGone)
    QObject::disconnect(m_windowGone);
  m_windowGone = {};
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  if (m_callback)
    wl_callback_destroy(m_callback);
#endif
  m_callback = nullptr;
  m_display = nullptr;
  m_window.clear();
  m_completion = {};
  m_phase = 0;
}

bool QtWaylandQueueWitness::current() const
{
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  if (!m_window || !m_display || !m_timeout.isActive() || !qGuiApp ||
      QThread::currentThread() != qGuiApp->thread() || QCoreApplication::closingDown())
    return false;
  const auto state = readWaylandViewport(m_window.data());
  if (!state || *state != m_expected)
    return false;
  const auto *platform = dynamic_cast<QtWaylandClient::QWaylandWindow *>(m_window->handle());
  return platform && platform->display() && platform->display()->wl_display() == m_display &&
         wl_display_get_error(m_display) == 0;
#else
  return false;
#endif
}

bool QtWaylandQueueWitness::issueSync()
{
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  if (!current() || m_callback)
    return false;
  auto *callback = wl_display_sync(m_display);
  if (!callback)
    return false;
  static const wl_callback_listener listener{&QtWaylandQueueWitness::syncDone};
  if (wl_callback_add_listener(callback, &listener, this) != 0) {
    wl_callback_destroy(callback);
    return false;
  }
  m_callback = callback;
  // The Wayland display is owned by Qt's real QPA event reader. Flushing
  // requests is nonblocking, and EAGAIN merely defers transmission to Qt.
  const auto flushed = wl_display_flush(m_display);
  if (flushed < 0 && errno != EAGAIN) {
    wl_callback_destroy(std::exchange(m_callback, nullptr));
    return false;
  }
  return true;
#else
  return false;
#endif
}

bool QtWaylandQueueWitness::begin(
    QWindow *window, const WaylandViewportGeometry &expected, Completion completion
)
{
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  if (m_callback || m_completion || !completion || !window || !qGuiApp ||
      QThread::currentThread() != qGuiApp->thread() || QCoreApplication::closingDown())
    return false;
  const auto fresh = readWaylandViewport(window);
  auto *platform = dynamic_cast<QtWaylandClient::QWaylandWindow *>(window->handle());
  if (!fresh || *fresh != expected || !platform || !platform->display() ||
      !platform->display()->wl_display())
    return false;
  m_window = window;
  m_expected = expected;
  m_display = platform->display()->wl_display();
  m_phase = 0;
  m_completion = std::move(completion);
  m_windowGone = connect(window, &QObject::destroyed, this, [this] { finish(false); });
  // This is a local budget inside the inherited snapshot's absolute 250ms
  // lifetime. A timeout cannot be extended by a first or late sync callback.
  m_timeout.start(200);
  if (issueSync())
    return true;
  cancel(); // begin(false) never calls the completion.
#else
  (void)window;
  (void)expected;
  (void)completion;
#endif
  return false;
}

void QtWaylandQueueWitness::finish(bool observed)
{
  auto completion = std::move(m_completion);
  cancel();
  if (completion)
    completion(observed);
}

void QtWaylandQueueWitness::syncDone(void *data, wl_callback *callback, std::uint32_t) noexcept
{
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  auto *self = static_cast<QtWaylandQueueWitness *>(data);
  // The registered callback is dispatched only on QtWayland's GUI-owned
  // default queue. Destruction/cancellation destroys the proxy, not a new
  // display or a second dispatch loop.
  if (!self || callback != self->m_callback)
    return;
  self->m_callback = nullptr;
  wl_callback_destroy(callback);
  // A Qt focus/window event may destroy the endpoint while its QPA queue is
  // drained. Retain only a weak lifetime, including across the catch path.
  const QPointer<QtWaylandQueueWitness> alive(self);
  try {
    if (!self->current()) {
      self->finish(false);
      return;
    }
    if (self->m_phase++ == 0) {
      if (!self->issueSync())
        self->finish(false);
      return;
    }
    // sendWindowSystemEvents returns whether it *sent* anything. That boolean
    // is NOT an input drain receipt and must never be interpreted as one.
    // Reentrant Qt event handlers can close the Viewer or this endpoint.
    (void)QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
    if (alive)
      self->finish(self->current());
  } catch (...) {
    if (alive)
      alive->finish(false);
  }
#else
  (void)data;
  (void)callback;
#endif
}
} // namespace deskflow::windowshare
