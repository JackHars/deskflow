/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WaylandViewport.h"
#include <QByteArrayView>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPointer>
#include <QThread>
#include <QWindow>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <utility>
#if defined(WINDOWSHARE_QT_WAYLAND) && QT_VERSION_MAJOR == 6 && (QT_VERSION_MINOR == 8 || QT_VERSION_MINOR == 10)
#define WINDOWSHARE_SUPPORTED_QT_WAYLAND 1
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/private/qhighdpiscaling_p.h>
#include <QtGui/qpa/qplatforminputcontext.h>
#include <QtGui/qpa/qplatformintegration.h>
#include <QtGui/qpa/qwindowsysteminterface.h>
#include <QtGui/qpa/qwindowsysteminterface_p.h>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#endif

namespace deskflow::windowshare {
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
namespace {
bool supportedInputContext(QPlatformInputContext *context)
{
  // In the pinned Qt 6.8/6.10 built-in Wayland context, filterEvent returns
  // false before compose state or callbacks when inputMethodAccepted is false.
  // Other plugins can retain keys before QPA delivery and are not supported.
  // Exact runtime class metadata is a capability check for our trusted Qt
  // build, not authentication of arbitrary code loaded into this process.
  return !context || (QByteArrayView(context->metaObject()->className()) == "QtWaylandClient::QWaylandInputContext" &&
                      !context->inputMethodAccepted());
}
} // namespace
#endif
struct ViewerApplication::Dispatch
{
  unsigned notifications = 0, native = 0;
  bool requested = false;
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  class Handler final : public QWindowSystemEventHandler
  {
  public:
    explicit Handler(std::weak_ptr<Dispatch> owner) : m_owner(std::move(owner))
    {
    }
    bool sendEvent(QWindowSystemInterfacePrivate::WindowSystemEvent *event) override
    {
      const auto owner = m_owner.lock();
      if (!owner)
        return QWindowSystemEventHandler::sendEvent(event);
      ++owner->native;
      struct Unwind
      {
        Dispatch &owner;
        ~Unwind()
        {
          --owner.native;
        }
      } unwind{*owner};
      // Preserve ordinary Qt delivery even if a nested callback withdraws the
      // fence or this operation times out. The guard observes, never filters.
      return QWindowSystemEventHandler::sendEvent(event);
    }

  private:
    std::weak_ptr<Dispatch> m_owner;
  };
  std::unique_ptr<Handler> handler;
#endif
};

ViewerApplication::ViewerApplication(int &argc, char **argv)
    : QApplication(argc, argv),
      m_dispatch(std::make_shared<Dispatch>())
{
}
ViewerApplication::~ViewerApplication()
{
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  if (m_dispatch->handler)
    QWindowSystemInterfacePrivate::removeWindowSystemEventhandler(m_dispatch->handler.get());
#endif
}
bool ViewerApplication::enableInputDrainDispatch()
{
  if (m_dispatch->requested)
    return ownsInputDrainDispatch();
  m_dispatch->requested = true;
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  if (QString::fromLatin1(qVersion()) != QStringLiteral(QT_VERSION_STR) ||
      !QGuiApplication::platformName().startsWith(QStringLiteral("wayland")) || QThread::currentThread() != thread() ||
      !inputDispatchIdle() || QWindowSystemInterfacePrivate::eventHandler)
    return false;
  m_dispatch->handler = std::make_unique<Dispatch::Handler>(m_dispatch);
  QWindowSystemInterfacePrivate::installWindowSystemEventHandler(m_dispatch->handler.get());
#endif
  return ownsInputDrainDispatch();
}
bool ViewerApplication::inputDispatchIdle() const noexcept
{
  return m_dispatch && m_dispatch->notifications == 0 && m_dispatch->native == 0;
}
bool ViewerApplication::ownsInputDrainDispatch() const noexcept
{
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  return m_dispatch && m_dispatch->handler && QWindowSystemInterfacePrivate::eventHandler == m_dispatch->handler.get();
#else
  return false;
#endif
}
bool ViewerApplication::notify(QObject *receiver, QEvent *event)
{
  if (QThread::currentThread() != thread())
    return QApplication::notify(receiver, event);
  const auto dispatch = m_dispatch;
  if (!dispatch || !dispatch->requested || !event ||
      (!event->isInputEvent() && event->type() != QEvent::InputMethod && event->type() != QEvent::InputMethodQuery &&
       event->type() != QEvent::Shortcut && event->type() != QEvent::ShortcutOverride))
    return QApplication::notify(receiver, event);
  ++dispatch->notifications;
  struct Unwind
  {
    Dispatch &owner;
    ~Unwind()
    {
      --owner.notifications;
    }
  } unwind{*dispatch};
  return QApplication::notify(receiver, event);
}

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
  m_inputContext.clear();
  m_hadInputContext = false;
  m_completion = {};
  m_operation.reset();
  m_exactFence = {};
  m_inputDrain = false;
  m_phase = 0;
}

bool QtWaylandQueueWitness::current()
{
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  if (!m_window || !m_display || !m_timeout.isActive() || !qGuiApp || QThread::currentThread() != qGuiApp->thread() ||
      QCoreApplication::closingDown())
    return false;
  const auto state = readWaylandViewport(m_window.data());
  if (!state || *state != m_expected)
    return false;
  const auto *platform = dynamic_cast<QtWaylandClient::QWaylandWindow *>(m_window->handle());
  if (!platform || !platform->display() || platform->display()->wl_display() != m_display ||
      wl_display_get_error(m_display) != 0)
    return false;
  if (!m_inputDrain)
    return true;
  const auto *app = dynamic_cast<ViewerApplication *>(qGuiApp);
  if (!app || !app->ownsInputDrainDispatch() || !app->inputDispatchIdle() || m_deadline.hasExpired())
    return false;
  const auto *integration = QGuiApplicationPrivate::platformIntegration();
  if (!integration || m_hadInputContext != !m_inputContext.isNull() ||
      integration->inputContext() != m_inputContext.data() || !supportedInputContext(integration->inputContext()))
    return false;
  const QPointer<QtWaylandQueueWitness> alive(this);
  const auto operation = m_operation;
  const auto check = m_exactFence;
  const bool valid = check && check();
  return alive && valid && operation && m_operation == operation && m_inputDrain && m_timeout.isActive() && m_window &&
         !m_deadline.hasExpired() && app == qGuiApp && app->ownsInputDrainDispatch() && app->inputDispatchIdle() &&
         m_hadInputContext == !m_inputContext.isNull() &&
         integration == QGuiApplicationPrivate::platformIntegration() &&
         integration->inputContext() == m_inputContext.data() && supportedInputContext(integration->inputContext()) &&
         readWaylandViewport(m_window.data()) == std::optional(m_expected);
#else
  return false;
#endif
}

bool QtWaylandQueueWitness::issueSync()
{
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  const QPointer<QtWaylandQueueWitness> alive(this);
  if (!current() || !alive || m_callback)
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

bool QtWaylandQueueWitness::begin(QWindow *window, const WaylandViewportGeometry &expected, Completion completion)
{
  return beginQueue(window, expected, std::move(completion));
}
bool QtWaylandQueueWitness::beginInputDrain(
    QWindow *window, const WaylandViewportGeometry &expected, QDeadlineTimer deadline, std::function<bool()> exactFence,
    Completion completion
)
{
  const auto *app = dynamic_cast<ViewerApplication *>(qGuiApp);
  if (m_callback || m_completion || !completion || !window || !exactFence || deadline.isForever() ||
      deadline.hasExpired() || deadline.remainingTime() > 250 || !app || !app->ownsInputDrainDispatch() ||
      !app->inputDispatchIdle())
    return false;
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  const auto *integration = QGuiApplicationPrivate::platformIntegration();
  if (!integration || !supportedInputContext(integration->inputContext()))
    return false;
  m_inputContext = integration->inputContext();
  m_hadInputContext = !m_inputContext.isNull();
#endif
  m_inputDrain = true;
  m_deadline = deadline;
  m_exactFence = std::move(exactFence);
  const QPointer<QtWaylandQueueWitness> alive(this);
  const bool started = beginQueue(window, expected, std::move(completion));
  if (alive && !started && !m_operation)
    cancel();
  return alive && started;
}
bool QtWaylandQueueWitness::beginQueue(QWindow *window, const WaylandViewportGeometry &expected, Completion completion)
{
#ifdef WINDOWSHARE_SUPPORTED_QT_WAYLAND
  if (m_callback || m_completion || !completion || !window || !qGuiApp ||
      QThread::currentThread() != qGuiApp->thread() || QCoreApplication::closingDown())
    return false;
  const auto fresh = readWaylandViewport(window);
  auto *platform = dynamic_cast<QtWaylandClient::QWaylandWindow *>(window->handle());
  if (!fresh || *fresh != expected || !platform || !platform->display() || !platform->display()->wl_display())
    return false;
  m_window = window;
  m_expected = expected;
  m_display = platform->display()->wl_display();
  m_phase = 0;
  m_operation = std::make_shared<const int>(0);
  const auto operation = m_operation;
  m_completion = std::move(completion);
  m_windowGone = connect(window, &QObject::destroyed, this, [this] { finish(false); });
  // This is a local budget inside the inherited snapshot's absolute 250ms
  // lifetime. A timeout cannot be extended by a first or late sync callback.
  m_timeout.start(m_inputDrain ? static_cast<int>(std::max<qint64>(1, m_deadline.remainingTime())) : 200);
  const QPointer<QtWaylandQueueWitness> alive(this);
  if (issueSync() && alive)
    return true;
  if (alive && m_operation == operation)
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
  const auto operation = self->m_operation;
  try {
    if (!self->current()) {
      if (alive && self->m_operation == operation)
        self->finish(false);
      return;
    }
    if (!alive || self->m_operation != operation)
      return;
    if (self->m_phase++ == 0) {
      if (!self->issueSync() && alive && self->m_operation == operation)
        self->finish(false);
      return;
    }
    // sendWindowSystemEvents returns whether it *sent* anything. That boolean
    // is NOT an input drain receipt and must never be interpreted as one.
    // Reentrant Qt event handlers can close the Viewer or this endpoint.
    (void)QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
    if (alive && self->m_operation == operation) {
      const bool current = self->current();
      if (alive && self->m_operation == operation)
        self->finish(current);
    }
  } catch (...) {
    if (alive && self->m_operation == operation)
      alive->finish(false);
  }
#else
  (void)data;
  (void)callback;
#endif
}
} // namespace deskflow::windowshare
