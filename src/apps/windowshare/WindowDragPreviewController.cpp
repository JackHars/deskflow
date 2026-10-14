/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WindowDragPreviewController.h"
#include <QPointer>
#include <utility>

namespace deskflow::windowshare {
WindowDragPreviewController::WindowDragPreviewController(Providers providers, QObject *parent)
    : QObject(parent),
      m_providers(std::move(providers))
{
  m_poll.setInterval(10);
  connect(&m_poll, &QTimer::timeout, this, [this] { poll(); });
}
WindowDragPreviewController::~WindowDragPreviewController()
{
  stop();
}
bool WindowDragPreviewController::current() const
{
  return !m_stopped && m_command && !m_deadline.hasExpired() && !m_silence.hasExpired();
}
bool WindowDragPreviewController::exactPaint(const Paint &paint) const
{
  return m_surface && m_surface->sameSurface(paint) && paint.serial && !paint.association.isEmpty() &&
         paint.client.width() > 0 && paint.client.height() > 0;
}
bool WindowDragPreviewController::receive(const WindowDragPreviewCommand &command)
{
  if (m_stopped)
    return false;
  (void)parseWindowDragPreviewCommand(windowDragPreviewJson(command));
  if (m_command) {
    if (!current() || m_dropped || command.drag != m_command->drag || command.geometry != m_command->geometry ||
        command.anchorX != m_command->anchorX || command.anchorY != m_command->anchorY ||
        (command.kind == WindowDragPreviewCommand::Kind::Move ? command.sequence <= m_command->sequence
                                                              : !command.samePosition(*m_command))) {
      deny();
      return false;
    }
  } else if (command.kind != WindowDragPreviewCommand::Kind::Move) {
    return false;
  }
  if (command.kind == WindowDragPreviewCommand::Kind::Cancel) {
    stop();
    return true;
  }
  if (m_drop) {
    deny();
    return false;
  }
  if (!m_command)
    m_deadline = QDeadlineTimer(30000);
  m_command = command;
  m_silence = QDeadlineTimer(350);
  m_poll.start();
  if (command.kind == WindowDragPreviewCommand::Kind::Drop) {
    m_drop = true;
    m_deadline = QDeadlineTimer(qMin<qint64>(m_deadline.remainingTime(), 1500));
    m_silence = m_deadline;
  }
  const QPointer<WindowDragPreviewController> alive(this);
  poll();
  return alive && !m_stopped;
}
bool WindowDragPreviewController::native(
    GnomeWindowDragAction action, const WindowDragPreviewCommand &command,
    std::function<void(const GnomeWindowDragReply &)> finished
)
{
  if (!current() || !m_surface || !m_providers.native)
    return false;
  GnomeWindowDragRequest request{0, command.drag, command.sequence, m_surface->association, action, 0, 0};
  if (action == GnomeWindowDragAction::Place) {
    const auto x = std::int64_t(command.x) - qRound(command.anchorX * m_surface->client.width());
    const auto y = std::int64_t(command.y) - qRound(command.anchorY * m_surface->client.height());
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
      return false;
    request.x = static_cast<int>(x);
    request.y = static_cast<int>(y);
  }
  m_busy = true;
  const auto generation = m_generation;
  const QPointer<WindowDragPreviewController> alive(this);
  const auto callback = m_providers.native;
  const bool started =
      callback(request, [alive, generation, request, finished = std::move(finished)](const auto &reply) {
        if (!alive || alive->m_generation != generation || alive->m_stopped)
          return;
        auto binding = reply.binding;
        binding.request = 0; // The endpoint owns and validates its separate counter.
        alive->m_busy = false;
        if (!alive->current() || !reply.ok || binding != request) {
          alive->deny();
          return;
        }
        finished(reply);
      });
  if (!alive)
    return false;
  if (!started)
    m_busy = false;
  return started;
}
void WindowDragPreviewController::startMove()
{
  if (!m_command || !m_providers.paint)
    return deny();
  const QPointer<WindowDragPreviewController> alive(this);
  const auto read = m_providers.paint;
  const auto paint = read();
  if (!alive)
    return;
  if (!paint || paint->geometry != m_command->geometry || !paint->serial || !paint->surface || paint->client.isEmpty())
    return; // Finite first-frame wait; a timer cannot fabricate a paint.
  if (!m_surface)
    m_surface = paint;
  if (!exactPaint(*paint))
    return deny();
  m_work = *m_command;
  m_work->kind = WindowDragPreviewCommand::Kind::Move;
  m_paint = 0;
  const auto work = *m_work;
  if (!native(
          GnomeWindowDragAction::Place, work,
          [this](const auto &placed) {
            const QPointer<WindowDragPreviewController> alive(this);
            const auto read = m_providers.paint;
            const auto paint = read ? read() : std::nullopt;
            if (!alive)
              return;
            if (!paint || !exactPaint(*paint))
              return deny();
            m_placement = placed;
            m_beforePaint = paint;
            const auto repaint = m_providers.repaint;
            if (repaint)
              repaint();
          }
      ) &&
      alive)
    deny();
}
void WindowDragPreviewController::verifyPaint()
{
  const QPointer<WindowDragPreviewController> alive(this);
  const auto read = m_providers.paint;
  const auto painted = read ? read() : std::nullopt;
  if (!alive)
    return;
  if (!painted || !exactPaint(*painted))
    return deny();
  if (painted->serial <= m_beforePaint->serial)
    return;
  m_paint = painted->serial;
  m_beforePaint.reset();
  const auto work = *m_work;
  if (!native(
          GnomeWindowDragAction::Verify, work,
          [this, work](const auto &verified) {
            if (!m_placement || verified.frame != m_placement->frame || verified.revision != m_placement->revision)
              return deny();
            acknowledge(work);
          }
      ) &&
      alive)
    deny();
}
void WindowDragPreviewController::acknowledge(const WindowDragPreviewCommand &work, bool dropped)
{
  const QPointer<WindowDragPreviewController> alive(this);
  const auto read = m_providers.paint;
  const auto paint = read ? read() : std::nullopt;
  if (!alive)
    return;
  if (!current() || !paint || !exactPaint(*paint) || !m_placement || !m_paint)
    return deny();
  auto acknowledged = work;
  if (dropped) {
    acknowledged.kind = WindowDragPreviewCommand::Kind::Drop;
    m_dropped = true;
    m_poll.stop();
  }
  const auto reply = m_providers.reply;
  const WindowDragPreviewReply result{acknowledged, true, m_placement->frame, m_paint, m_placement->revision};
  if (reply)
    reply(result);
}
void WindowDragPreviewController::drop()
{
  const auto work = *m_work;
  const QPointer<WindowDragPreviewController> alive(this);
  if (!native(
          GnomeWindowDragAction::Finish, work,
          [this, work](const auto &finished) {
            if (!m_placement || finished.frame != m_placement->frame)
              return deny();
            acknowledge(work, true);
          }
      ) &&
      alive)
    deny();
}
void WindowDragPreviewController::poll()
{
  if (m_stopped || m_dropped || !m_command)
    return;
  if (!current())
    return deny();
  if (m_busy)
    return;
  if (m_beforePaint)
    return verifyPaint();
  if (!m_work || m_work->sequence != m_command->sequence)
    return startMove();
  if (m_drop && m_placement && m_paint)
    return drop();
  if (m_command->kind == WindowDragPreviewCommand::Kind::Hold) {
    // One heartbeat per received Hold, not an autonomous liveness extension.
    m_command->kind = WindowDragPreviewCommand::Kind::Move;
    const QPointer<WindowDragPreviewController> alive(this);
    if (!native(
            GnomeWindowDragAction::Hold, *m_work,
            [this](const auto &held) {
              if (!m_placement || held.frame != m_placement->frame || held.revision != m_placement->revision)
                deny();
            }
        ) &&
        alive)
      deny();
  }
}
void WindowDragPreviewController::deny()
{
  const auto command = m_command;
  const auto reply = m_providers.reply;
  const QPointer<WindowDragPreviewController> alive(this);
  stop();
  if (alive && command && reply)
    reply(WindowDragPreviewReply{*command, false, {}, 0, 0});
}
void WindowDragPreviewController::stop()
{
  if (m_stopped)
    return;
  m_stopped = true;
  ++m_generation;
  m_poll.stop();
  const auto native = m_providers.native;
  m_providers = {};
  if (m_surface && m_work && native && !m_dropped) {
    const GnomeWindowDragRequest request{
        0, m_work->drag, m_work->sequence, m_surface->association, GnomeWindowDragAction::Cancel, 0, 0
    };
    // Cleanup is best effort; the source receives denial, the preview closes,
    // and the provider's independent deadline prevents a late result reviving it.
    try {
      native(request, [](const auto &) {});
    } catch (...) {
    }
  }
}
} // namespace deskflow::windowshare
