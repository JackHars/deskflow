/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "GnomeWindowDragProtocol.h"
#include <QJsonArray>
#include <optional>

namespace deskflow::windowshare {
// Session/epoch come from the authenticated packet envelope. These are native
// window observations, never proof of which device supplied the initiating up
// or down, and never an original-input delivery permit.
struct WindowDragMove
{
  std::uint64_t geometry = 0, surface = 0, paint = 0;
  GnomeWindowDragEvent native;
};
QJsonObject windowDragMoveJson(const WindowDragMove &);
WindowDragMove parseWindowDragMove(const QJsonObject &);
inline bool peerSupportsWindowDragMove(const QJsonObject &hello)
{
  return hello.value("capabilities").toArray().contains(QStringLiteral("window-drag-move-observation-v1"));
}
inline QJsonArray windowDragMoveCapabilities(QJsonArray capabilities, bool enabled)
{
  if (enabled)
    capabilities.append(QStringLiteral("window-drag-move-observation-v1"));
  return capabilities;
}
// One owned Viewer, with a bounded native grab history. A new grab may use a
// newly painted geometry/surface; a continuing grab may never replace either.
class WindowDragMoveGate final
{
public:
  bool accept(const WindowDragMove &);
  const std::optional<WindowDragMove> &current() const
  {
    return m_active;
  }
  void cancel()
  {
    m_active.reset();
  }

private:
  std::uint64_t m_lastGrab = 0;
  std::optional<WindowDragMove> m_active;
};
} // namespace deskflow::windowshare
