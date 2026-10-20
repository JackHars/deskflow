/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QObject>
#include <QThread>
#include <QTimer>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace deskflow::windowshare {

// One read-only native inspection, never an input-authority cache. A stalled
// system query must not stall native input or create more worker requests.
// Completion runs on the owner thread and expires at the original deadline.
template <class Result> class AsyncNativeInspection final : public QObject
{
public:
  using Clock = std::chrono::steady_clock;
  using Completion = std::function<void(std::optional<Result>)>;
  static constexpr auto maximumAge = std::chrono::milliseconds(250);
  explicit AsyncNativeInspection(std::function<Clock::time_point()> now = Clock::now) : m_now(std::move(now))
  {
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_timer, &QTimer::timeout, this, [this] { expire(); });
  }
  bool request(std::function<Result()> inspect, Completion completion)
  {
    if (m_running || !inspect || !completion)
      return false;
    auto result = std::make_shared<std::optional<Result>>();
    // The worker captures only its query and result, never this or its owner.
    auto *worker = QThread::create([inspect = std::move(inspect), result] {
      try {
        *result = inspect();
      } catch (...) {
        // Missing results fail closed on the owner thread.
      }
    });
    QObject::connect(
        worker, &QThread::finished, this,
        [this, result] {
          m_running = false;
          finish(m_now() < m_due ? std::move(*result) : std::nullopt);
        },
        Qt::QueuedConnection
    );
    // A destroyed owner cancels its callback, without blocking on system IPC.
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    m_running = true;
    m_due = m_now() + maximumAge;
    m_completion = std::move(completion);
    m_timer.start(maximumAge);
    worker->start();
    return true;
  }
  void cancel()
  {
    m_timer.stop();
    m_completion = {};
    // An uncancellable native read still occupies the single worker slot.
  }
  void expire()
  {
    if (!m_completion)
      return;
    const auto remaining = m_due - m_now();
    if (remaining > Clock::duration::zero())
      m_timer.start(std::chrono::ceil<std::chrono::milliseconds>(remaining));
    else
      finish(std::nullopt);
  }
  bool busy() const
  {
    return m_running;
  }

private:
  void finish(std::optional<Result> result)
  {
    m_timer.stop();
    auto completion = std::exchange(m_completion, {});
    if (completion)
      completion(std::move(result)); // May destroy this inspection or its owner.
  }
  std::function<Clock::time_point()> m_now;
  Completion m_completion;
  Clock::time_point m_due;
  QTimer m_timer;
  bool m_running = false;
};
} // namespace deskflow::windowshare
