/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "GnomeViewportProtocol.h"
#include "WindowDragMove.h"
#include <QDeadlineTimer>
#include <QObject>
#include <QTimer>
#include <functional>

namespace deskflow::windowshare {
// Watches one committed, painted native Viewer through its inherited provider
// endpoint. No window mutation, button injection or capture consumer is added.
class WindowDragWatcher final : public QObject
{
public:
  struct Providers
  {
    std::function<std::optional<GnomePreparationTarget>()> target;
    std::function<bool(GnomeWindowDragRequest, std::function<void(const GnomeWindowDragReply &)>)> request;
    std::function<void(const WindowDragMove &)> observe;
  };
  explicit WindowDragWatcher(Providers, QObject *parent = nullptr);
  ~WindowDragWatcher() override;
  void receive(const GnomeWindowDragEvent &);
  void stop();

private:
  void poll();
  void retire();
  bool sameTarget(const GnomePreparationTarget &) const;
  Providers m_providers;
  QTimer m_poll;
  QDeadlineTimer m_deadline;
  std::optional<GnomePreparationTarget> m_target, m_denied;
  WindowDragMoveGate m_gate;
  std::uint64_t m_generation = 0;
  bool m_enabled = false, m_pending = false, m_stopped = false;
};
} // namespace deskflow::windowshare
