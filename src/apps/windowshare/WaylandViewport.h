/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <cstdint>
#include <optional>
class QWindow;
namespace deskflow::windowshare {
struct WaylandViewportGeometry
{
  quintptr surface = 0;
  std::uint32_t resource = 0;
  QSizeF extent;
  QPointF contentOrigin;
  double logicalScale = 0;
  bool operator==(const WaylandViewportGeometry &) const = default;
};
// Borrows the actual platform window. Never opens/dispatches a second Wayland
// connection, commits a surface, changes event queues, or infers global origin.
std::optional<WaylandViewportGeometry> readWaylandViewport(QWindow *window, QString *reason = nullptr);
std::optional<QPointF>
mapWaylandViewport(QWindow *window, const WaylandViewportGeometry &expected, QPointF surfacePoint);
} // namespace deskflow::windowshare
