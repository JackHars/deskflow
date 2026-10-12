/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "SelectedPeerPublisher.h"
#include "SelectedPeerMailbox.h"
#include "base/IEventQueue.h"
#include <stdexcept>

namespace deskflow::windowshare {
struct SelectedPeerPublisher::State
{
  std::shared_ptr<SelectedPeerMailbox> mailbox;
  Reader reader;
  bool active = true;
};

SelectedPeerPublisher::SelectedPeerPublisher(
    IEventQueue &events, std::shared_ptr<SelectedPeerMailbox> mailbox, Reader reader
)
    : m_events(events)
{
  if (!mailbox || !reader)
    throw std::invalid_argument("Selected peer publication requires a mailbox and a core reader");
  m_state = std::make_shared<State>(State{std::move(mailbox), std::move(reader)});
  m_state->mailbox->clear();
  m_timer = m_events.newTimer(0.020, m_state.get());
  if (!m_timer)
    throw std::runtime_error("Cannot create selected peer publication timer");
  try {
    m_events.addHandler(EventTypes::Timer, m_state.get(), [weak = std::weak_ptr<State>(m_state)](const Event &) {
      const auto state = weak.lock();
      if (!state || !state->active)
        return;
      try {
        // Retain the callable across a reader that synchronously stops or
        // destroys the publisher. Never dereference the publisher in a callback.
        const auto read = state->reader;
        const auto selected = read();
        if (!state->active)
          return;
        if (selected && selected->live())
          state->mailbox->replace(*selected);
        else
          state->mailbox->clear();
      } catch (...) {
        if (state->active) {
          state->active = false;
          state->mailbox->clear();
        }
      }
    });
  } catch (...) {
    m_state->active = false;
    m_events.removeHandler(EventTypes::Timer, m_state.get());
    m_events.deleteTimer(m_timer);
    m_timer = nullptr;
    throw;
  }
}

SelectedPeerPublisher::~SelectedPeerPublisher()
{
  stop();
}

void SelectedPeerPublisher::stop()
{
  if (!m_state)
    return;
  m_state->active = false;
  m_state->mailbox->clear();
  m_state->reader = {};
  if (m_timer) {
    m_events.removeHandler(EventTypes::Timer, m_state.get());
    m_events.deleteTimer(m_timer);
    m_timer = nullptr;
  }
  m_state.reset();
}
} // namespace deskflow::windowshare
