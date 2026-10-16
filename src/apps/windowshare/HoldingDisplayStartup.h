/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <chrono>
#include <functional>
#include <utility>

namespace deskflow::windowshare {

// The helper's reply is not proof that the parent's WindowServer cache has
// consumed the display change. Only a verified parent observation completes
// startup. Notifications wake this gate; there is no polling or sleep delay.
class HoldingDisplayStartup final : public QObject
{
public:
  using Clock = std::chrono::steady_clock;
  using Now = std::function<Clock::time_point()>;
  using Probe = std::function<bool(QString *)>;
  using Completion = std::function<void(bool, QString)>;
  HoldingDisplayStartup(Probe probe, Completion completion, Now now = Clock::now)
      : m_probe(std::move(probe)),
        m_completion(std::move(completion)),
        m_now(std::move(now))
  {
    m_deadline.setSingleShot(true);
    m_deadline.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_deadline, &QTimer::timeout, this, [this] { expire(); });
    arm(std::chrono::seconds(5));
  }
  void started()
  {
    if (!m_done && !m_started && withinDeadline()) {
      m_started = true;
      arm(std::chrono::seconds(10)); // Existing helper deadline includes parent verification.
    }
  }
  void ready()
  {
    if (!m_done && m_started && !m_ready && withinDeadline()) {
      m_ready = true;
      changed();
    }
  }
  void changed()
  {
    if (m_done || !m_ready || !withinDeadline())
      return;
    const bool verified = m_probe(&m_reason);
    // A native probe can consume the remaining deadline. Notification order
    // must not grant readiness merely because the timer callback ran later.
    if (!m_done && withinDeadline() && verified)
      finish(true, {});
  }
  void fail(QString reason)
  {
    finish(false, std::move(reason));
  }
  void expire()
  {
    if (m_done)
      return;
    const auto remaining = m_due - m_now();
    if (remaining > Clock::duration::zero())
      m_deadline.start(std::chrono::ceil<std::chrono::milliseconds>(remaining));
    else
      timedOut();
  }
  void cancel()
  {
    m_done = true;
    m_deadline.stop();
    m_completion = {};
  }
  bool pending() const
  {
    return !m_done;
  }

private:
  void arm(std::chrono::milliseconds duration)
  {
    m_due = m_now() + duration;
    m_deadline.start(duration);
  }
  bool withinDeadline()
  {
    if (m_now() < m_due)
      return true;
    timedOut();
    return false; // Completion may have destroyed this gate.
  }
  void timedOut()
  {
    finish(
        false, !m_ready ? "Holding-display helper readiness timed out"
                        : "Holding display verification timed out" + (m_reason.isEmpty() ? QString{} : ": " + m_reason)
    );
  }
  void finish(bool success, QString reason)
  {
    if (m_done)
      return;
    m_done = true;
    m_deadline.stop();
    auto completion = std::move(m_completion);
    if (completion)
      completion(success, std::move(reason)); // May destroy this gate; no access follows.
  }
  Probe m_probe;
  Completion m_completion;
  Now m_now;
  Clock::time_point m_due;
  QTimer m_deadline;
  QString m_reason;
  bool m_started = false, m_ready = false, m_done = false;
};
} // namespace deskflow::windowshare
