/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "GnomeWindowDragProtocol.h"
#include "WindowDragPreviewProtocol.h"
#include <QDeadlineTimer>
#include <QObject>
#include <QSize>
#include <QTimer>
#include <functional>
#include <optional>

namespace deskflow::windowshare {
// Native placement, followed by a fresh actual Qt paint, followed by a native
// position recheck. No stage grants focus, redirects input or invents a release.
class WindowDragPreviewController final : public QObject
{
public:
  struct Paint
  {
    QString association;
    std::uint64_t geometry = 0, serial = 0, surface = 0;
    QSize client;
    bool sameSurface(const Paint &other) const
    {
      return association == other.association && geometry == other.geometry && surface == other.surface &&
             client == other.client;
    }
  };
  struct Providers
  {
    std::function<std::optional<Paint>()> paint;
    std::function<void()> repaint;
    std::function<bool(GnomeWindowDragRequest, std::function<void(const GnomeWindowDragReply &)>)> native;
    std::function<void(const WindowDragPreviewReply &)> reply;
  };
  explicit WindowDragPreviewController(Providers, QObject *parent = nullptr);
  ~WindowDragPreviewController() override;
  bool receive(const WindowDragPreviewCommand &);
  void stop();

private:
  void poll();
  void startMove();
  void verifyPaint();
  void drop();
  void deny();
  bool current() const;
  bool exactPaint(const Paint &) const;
  bool
  native(GnomeWindowDragAction, const WindowDragPreviewCommand &, std::function<void(const GnomeWindowDragReply &)>);
  void acknowledge(const WindowDragPreviewCommand &, bool dropped = false);
  Providers m_providers;
  QTimer m_poll;
  std::optional<WindowDragPreviewCommand> m_command, m_work;
  std::optional<Paint> m_surface, m_beforePaint;
  std::optional<GnomeWindowDragReply> m_placement;
  QDeadlineTimer m_deadline, m_silence;
  std::uint64_t m_generation = 1, m_paint = 0;
  bool m_stopped = false, m_busy = false, m_drop = false, m_dropped = false;
};
} // namespace deskflow::windowshare
