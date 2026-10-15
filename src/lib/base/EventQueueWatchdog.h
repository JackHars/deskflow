/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "EventQueueBudget.h"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace deskflow {

// Explicit experimental Core launches only. Keep this guard alive until all
// producers, callback scopes and EventQueues have finished. Startup must refuse
// the session if construction throws. A null budget creates no monitor thread.
class EventQueueWatchdog final
{
public:
  explicit EventQueueWatchdog(std::shared_ptr<EventQueueBudget> budget);
  ~EventQueueWatchdog();
  EventQueueWatchdog(const EventQueueWatchdog &) = delete;
  EventQueueWatchdog &operator=(const EventQueueWatchdog &) = delete;
  static constexpr int timeoutExitCode = 71;
  static constexpr auto pollInterval = std::chrono::milliseconds(25);

private:
  const std::shared_ptr<EventQueueBudget> m_budget;
  std::mutex m_mutex;
  std::condition_variable m_changed;
  bool m_stopped = false;
  std::thread m_thread;
};
} // namespace deskflow
