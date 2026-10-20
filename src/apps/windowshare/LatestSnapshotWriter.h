/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace deskflow::windowshare {

// Disposable UI snapshots only. Evidence journals must never use this writer:
// one unpublished snapshot may replace another, while input evidence is lossless.
class LatestSnapshotWriter
{
public:
  explicit LatestSnapshotWriter(std::function<bool(const std::string &)> write)
      : m_write(std::move(write)),
        m_thread([this] { run(); })
  {
  }
  ~LatestSnapshotWriter()
  {
    finish();
  }
  bool publish(std::string snapshot)
  {
    {
      std::lock_guard lock(m_mutex);
      if (m_stopping)
        return false;
      m_pending = std::move(snapshot);
    }
    m_changed.notify_one();
    return true;
  }
  void finish()
  {
    {
      std::lock_guard lock(m_mutex);
      m_stopping = true;
    }
    m_changed.notify_one();
    if (m_thread.joinable())
      m_thread.join();
  }
  unsigned failures() const
  {
    std::lock_guard lock(m_mutex);
    return m_failures;
  }

private:
  void run()
  {
    for (;;) {
      std::string snapshot;
      {
        std::unique_lock lock(m_mutex);
        m_changed.wait(lock, [this] { return m_stopping || m_pending.has_value(); });
        if (!m_pending)
          return;
        snapshot = std::move(*m_pending);
        m_pending.reset();
      }
      bool written = false;
      try {
        written = m_write(snapshot);
      } catch (...) {
        // Failure is observable; never terminate the input fixture from its
        // diagnostic worker, nor retry indefinitely or reuse old snapshots.
      }
      if (!written) {
        std::lock_guard lock(m_mutex);
        ++m_failures;
      }
    }
  }
  std::function<bool(const std::string &)> m_write;
  mutable std::mutex m_mutex;
  std::condition_variable m_changed;
  std::optional<std::string> m_pending;
  bool m_stopping = false;
  unsigned m_failures = 0;
  std::thread m_thread;
};

} // namespace deskflow::windowshare
