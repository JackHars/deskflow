/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "EventQueueWatchdog.h"

#include <cstdlib>

namespace deskflow {

EventQueueWatchdog::EventQueueWatchdog(std::shared_ptr<EventQueueBudget> budget) : m_budget(std::move(budget))
{
  if (!m_budget)
    return;
  m_thread = std::thread([this] {
    std::unique_lock lock(m_mutex);
    while (!m_stopped) {
      if (m_budget->claimExpiredDispatch(EventQueueBudget::Clock::now())) {
        // Never wait for a stuck consumer, Qt, logging, revocation callback or
        // native destructor. Exiting only ourselves closes peer/Host channels;
        // their existing disconnect and independent Host recovery paths apply.
        std::_Exit(timeoutExitCode);
      }
      m_changed.wait_for(lock, pollInterval, [this] { return m_stopped; });
    }
  });
}

EventQueueWatchdog::~EventQueueWatchdog()
{
  {
    std::lock_guard lock(m_mutex);
    m_stopped = true;
  }
  m_changed.notify_one();
  if (m_thread.joinable())
    m_thread.join();
}
} // namespace deskflow
