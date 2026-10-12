/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WaylandViewport.h"
#include <QGuiApplication>
#include <QThread>
#include <QWindow>
#include <cmath>
#if defined(WINDOWSHARE_QT_WAYLAND) && QT_VERSION_MAJOR == 6 && (QT_VERSION_MINOR == 8 || QT_VERSION_MINOR == 10)
#define WINDOWSHARE_SUPPORTED_QT_WAYLAND 1
#include <QtGui/private/qhighdpiscaling_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <wayland-client-core.h>
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
} // namespace deskflow::windowshare
