/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace deskflow::windowshare {
struct WindowDragPreviewCommand
{
  enum class Kind
  {
    Move,
    Hold,
    Drop,
    Cancel
  };
  Kind kind = Kind::Move;
  std::uint64_t drag = 0, sequence = 0, geometry = 0;
  int x = 0, y = 0;
  double anchorX = 0, anchorY = 0;
  bool operator==(const WindowDragPreviewCommand &) const = default;
  bool samePosition(const WindowDragPreviewCommand &other) const
  {
    auto copy = *this;
    copy.kind = other.kind;
    return copy == other;
  }
};
struct WindowDragPreviewReply
{
  WindowDragPreviewCommand command;
  bool ok = false;
  QRect frame;
  std::uint64_t paint = 0;
  std::uint32_t placement = 0;
};
QJsonObject windowDragPreviewJson(const WindowDragPreviewCommand &);
QJsonObject windowDragPreviewJson(const WindowDragPreviewReply &);
WindowDragPreviewCommand parseWindowDragPreviewCommand(const QJsonObject &);
WindowDragPreviewReply parseWindowDragPreviewReply(const QJsonObject &);
inline bool peerSupportsWindowDragMotion(const QJsonObject &hello)
{
  return hello.value("capabilities").toArray().contains(QStringLiteral("window-drag-motion-v1"));
}
inline QJsonArray windowDragMotionCapabilities(QJsonArray capabilities, bool enabled)
{
  if (enabled)
    capabilities.append(QStringLiteral("window-drag-motion-v1"));
  return capabilities;
}
inline bool peerSupportsWindowDragPreview(const QJsonObject &hello)
{
  return hello.value("capabilities").toArray().contains(QStringLiteral("window-drag-preview-v1"));
}
inline QJsonArray windowDragPreviewCapabilities(QJsonArray capabilities, bool enabled = true)
{
  if (enabled)
    capabilities.append(QStringLiteral("window-drag-preview-v1"));
  return capabilities;
}
} // namespace deskflow::windowshare
