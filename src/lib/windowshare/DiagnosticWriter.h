/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace deskflow::windowshare {
// Diagnostics only. No input payload, authority or receipt lives here. The
// sink executes outside the admission lock, so a full output pipe cannot hold
// a producer. Bounds include the record currently being written to the sink.
class DiagnosticWriter final
{
public:
  static constexpr std::size_t maximumRecords = 128, maximumBytes = 256 * 1024, maximumLine = 16 * 1024;
  using Sink = std::function<bool(std::string_view)>;
  using Loss = std::function<std::string(std::uint64_t, std::uint64_t)>;
  struct Status
  {
    std::size_t records = 0, bytes = 0;
    std::uint64_t dropped = 0, oversized = 0;
    bool failed = false, stopped = false;
  };
  DiagnosticWriter(Sink sink, Loss loss) : m_state(std::make_shared<State>(std::move(sink), std::move(loss)))
  {
    try {
      m_thread = std::thread([state = m_state] { state->run(); });
    } catch (const std::system_error &) {
      m_state->status.failed = m_state->status.stopped = true;
    }
  }
  ~DiagnosticWriter()
  {
    finish(std::chrono::milliseconds(250));
  }
  DiagnosticWriter(const DiagnosticWriter &) = delete;
  DiagnosticWriter &operator=(const DiagnosticWriter &) = delete;
  bool append(std::string line)
  {
    const auto state = m_state;
    std::lock_guard lock(state->mutex);
    if (state->closing || state->status.failed || line.empty())
      return false;
    const bool oversized = line.size() > maximumLine;
    if (oversized || state->status.records == maximumRecords || line.size() > maximumBytes - state->status.bytes) {
      State::increment(oversized ? state->status.oversized : state->status.dropped);
      State::increment(oversized ? state->oversized : state->dropped);
      state->wake.notify_one();
      return false;
    }
    state->queue.push_back(std::move(line));
    ++state->status.records;
    state->status.bytes += state->queue.back().size();
    state->wake.notify_one();
    return true;
  }
  Status status() const
  {
    std::lock_guard lock(m_state->mutex);
    return m_state->status;
  }
  // A stalled sink must not hang session teardown. A detached worker retains
  // only its shared bounded state and sink until completion or process exit.
  bool finish(std::chrono::milliseconds budget)
  {
    std::unique_lock lock(m_state->mutex);
    m_state->closing = true;
    m_state->wake.notify_one();
    if (!m_thread.joinable())
      return m_state->status.stopped;
    const bool done = m_state->finished.wait_for(lock, budget, [&] { return m_state->status.stopped; });
    lock.unlock();
    if (done)
      m_thread.join();
    else
      m_thread.detach();
    return done;
  }

private:
  struct State
  {
    State(Sink output, Loss report) : sink(std::move(output)), loss(std::move(report))
    {
    }
    static void increment(std::uint64_t &value)
    {
      if (value != std::numeric_limits<std::uint64_t>::max())
        ++value;
    }
    void run() noexcept
    {
      try {
        for (;;) {
          std::string line;
          std::uint64_t lost = 0, large = 0;
          {
            std::unique_lock lock(mutex);
            wake.wait(lock, [&] { return closing || !queue.empty() || dropped || oversized; });
            if (queue.empty() && !dropped && !oversized && closing)
              break;
            lost = std::exchange(dropped, 0);
            large = std::exchange(oversized, 0);
            if (!queue.empty()) {
              line = std::move(queue.front());
              queue.pop_front();
            }
          }
          bool good = true;
          if (lost || large) {
            const auto notice = loss(lost, large);
            good = notice.size() <= maximumLine && sink(notice);
          }
          if (good && !line.empty())
            good = sink(line);
          std::lock_guard lock(mutex);
          if (!good) {
            status.failed = true;
            break;
          }
          if (!line.empty()) {
            --status.records;
            status.bytes -= line.size();
          }
        }
      } catch (...) {
        std::lock_guard lock(mutex);
        status.failed = true;
      }
      std::lock_guard lock(mutex);
      queue.clear();
      status.records = status.bytes = 0;
      status.stopped = true;
      finished.notify_all();
    }
    std::mutex mutex;
    std::condition_variable wake, finished;
    std::deque<std::string> queue;
    Sink sink;
    Loss loss;
    Status status;
    std::uint64_t dropped = 0, oversized = 0;
    bool closing = false;
  };
  std::shared_ptr<State> m_state;
  std::thread m_thread;
};
} // namespace deskflow::windowshare
