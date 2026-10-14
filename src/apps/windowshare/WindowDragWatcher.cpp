/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WindowDragWatcher.h"
#include <QPointer>
#include <utility>

namespace deskflow::windowshare {
namespace {
bool sameIdentity(const GnomePreparationTarget &a, const GnomePreparationTarget &b)
{
  return a.session == b.session && a.epoch == b.epoch && a.geometry == b.geometry &&
         a.surfaceIncarnation == b.surfaceIncarnation && a.nativeSurface == b.nativeSurface &&
         a.windowAssociation == b.windowAssociation;
}
} // namespace
WindowDragWatcher::WindowDragWatcher(Providers providers, QObject *parent)
    : QObject(parent),
      m_providers(std::move(providers))
{
  connect(&m_poll, &QTimer::timeout, this, [this] { poll(); });
  m_poll.start(25);
}
WindowDragWatcher::~WindowDragWatcher()
{
  stop();
}
bool WindowDragWatcher::sameTarget(const GnomePreparationTarget &value) const
{
  return m_target && sameIdentity(value, *m_target);
}
void WindowDragWatcher::retire()
{
  const auto old = std::exchange(m_target, {});
  const auto active = m_gate.current();
  const auto observe = m_providers.observe;
  const auto request = m_providers.request;
  m_gate.cancel();
  m_enabled = m_pending = false;
  ++m_generation;
  // All borrowed callbacks may destroy this object. Use retained locals only.
  try {
    if (old && request)
      request({0, 1, 1, old->windowAssociation, GnomeWindowDragAction::Unwatch, 0, 0}, [](const auto &) {});
  } catch (...) {
  }
  if (active && observe) {
    auto cancelled = *active;
    cancelled.native.phase = GnomeWindowDragEvent::Phase::Cancel;
    cancelled.native.buttonReleased = false;
    try {
      observe(cancelled);
    } catch (...) {
    }
  }
}
void WindowDragWatcher::stop()
{
  if (m_stopped)
    return;
  m_stopped = true;
  m_poll.stop();
  retire();
}
void WindowDragWatcher::poll()
{
  if (m_stopped)
    return;
  const QPointer<WindowDragWatcher> alive(this);
  const auto read = m_providers.target;
  std::optional<GnomePreparationTarget> target;
  try {
    if (read)
      target = read();
  } catch (...) {
  }
  if (!alive)
    return;
  if (m_target && (!target || !sameTarget(*target)))
    return retire();
  if (m_pending && m_deadline.hasExpired()) {
    m_denied = m_target;
    return retire();
  }
  if (!target || m_target || (m_denied && sameIdentity(*m_denied, *target)))
    return;
  m_target = target;
  m_pending = true;
  m_deadline = QDeadlineTimer(250);
  const auto generation = ++m_generation;
  const auto request = m_providers.request;
  bool sent = false;
  try {
    sent = request && request(
                          {0, 1, 1, target->windowAssociation, GnomeWindowDragAction::Watch, 0, 0},
                          [alive, generation, target, request](const GnomeWindowDragReply &reply) {
                            if (!alive || alive->m_stopped || alive->m_generation != generation) {
                              // A pending request may succeed after cancellation/destruction.
                              try {
                                if (reply.ok && request)
                                  request(
                                      {0, 1, 1, target->windowAssociation, GnomeWindowDragAction::Unwatch, 0, 0},
                                      [](const auto &) {}
                                  );
                              } catch (...) {
                              }
                              return;
                            }
                            alive->m_pending = false;
                            if (!reply.ok || reply.binding.association != target->windowAssociation ||
                                reply.binding.action != GnomeWindowDragAction::Watch) {
                              alive->m_denied = target;
                              alive->retire();
                            } else
                              alive->m_enabled = true;
                          }
                      );
  } catch (...) {
  }
  if (alive && !sent && m_generation == generation) {
    m_denied = target;
    retire();
  }
}
void WindowDragWatcher::receive(const GnomeWindowDragEvent &event)
{
  if (m_stopped || !m_enabled || !m_target || event.association != m_target->windowAssociation)
    return;
  const QPointer<WindowDragWatcher> alive(this);
  const auto read = m_providers.target;
  std::optional<GnomePreparationTarget> target;
  try {
    if (read)
      target = read();
  } catch (...) {
  }
  if (!alive)
    return;
  if (!target || !sameTarget(*target))
    return retire();
  const WindowDragMove value{target->geometry, target->surfaceIncarnation, target->paintRevision, event};
  if (!m_gate.accept(value))
    return retire();
  const auto observe = m_providers.observe;
  if (observe) {
    try {
      observe(value);
    } catch (...) {
      if (alive)
        retire();
    }
  }
}
} // namespace deskflow::windowshare
