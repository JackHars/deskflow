/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QJsonArray>
#include <QJsonObject>

namespace deskflow::windowshare {
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
