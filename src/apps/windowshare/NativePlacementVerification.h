/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <chrono>
#include <functional>
#include <utility>
#include <vector>

namespace deskflow::windowshare {

// A successful native write is only a request. A fresh observation, under the
// same authority, must verify it before the absolute deadline. Native events
// and the existing host watchdog request coalesced checks; neither is proof of
// placement. There are no sleeps, recursive event loops or automatic rewrites.
class NativePlacementVerification final : public QObject
{
public:
  enum class Observation
  {
    Pending,
    Verified,
    Invalid
  };
  using Clock = std::chrono::steady_clock;
  using Now = std::function<Clock::time_point()>;
  using Probe = std::function<Observation(QString *)>;
  using Completion = std::function<void(bool, QString)>;
  using Preparation = std::vector<std::function<bool()>>;

  NativePlacementVerification(Probe probe, Completion completion, Now now = Clock::now)
      : m_probe(std::move(probe)),
        m_completion(std::move(completion)),
        m_now(std::move(now)),
        m_due(m_now() + std::chrono::seconds(2))
  {
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_timer, &QTimer::timeout, this, [this] { expire(); });
    m_timer.start(std::chrono::seconds(2));
  }
  // Each native registration/write returns to Qt before the next operation.
  // The original deadline includes preparation and is never renewed by a step.
  void prepare(Preparation steps)
  {
    if (m_done || m_issued || m_preparing)
      return;
    m_preparing = true;
    m_preparation = std::move(steps);
    queuePreparation();
  }
  void issued()
  {
    if (m_done || m_issued || m_preparing)
      return;
    m_issued = true;
    changed();
  }
  void changed()
  {
    if (m_done || !m_issued || m_scheduled)
      return;
    m_scheduled = true;
    QMetaObject::invokeMethod(this, [this] { check(); }, Qt::QueuedConnection);
  }
  void expire()
  {
    if (m_done)
      return;
    const auto remaining = m_due - m_now();
    if (remaining > Clock::duration::zero())
      m_timer.start(std::chrono::ceil<std::chrono::milliseconds>(remaining));
    else
      finish(false, "Native placement verification timed out" + (m_reason.isEmpty() ? QString{} : ": " + m_reason));
  }
  void fail(QString reason)
  {
    finish(false, std::move(reason));
  }
  void cancel()
  {
    m_done = true;
    m_timer.stop();
    m_completion = {};
  }
  bool pending() const
  {
    return !m_done;
  }
  bool withinDeadline() const
  {
    return !m_done && m_now() < m_due;
  }
  // Native observations can block. Fence one fresh observation with cheap
  // lifecycle checks, rather than making the native observer its own fence
  // and repeating permission/display IPC around every individual read.
  bool observeAuthority(std::function<bool()> current, std::function<bool()> observe) const
  {
    const QPointer<const NativePlacementVerification> alive(this);
    if (!withinDeadline() || !current() || !alive || !withinDeadline())
      return false;
    const bool observed = observe();
    return alive && observed && current() && alive && withinDeadline();
  }

private:
  void queuePreparation()
  {
    QMetaObject::invokeMethod(this, [this] { prepareNext(); }, Qt::QueuedConnection);
  }
  void prepareNext()
  {
    if (m_done)
      return;
    if (m_now() >= m_due) {
      expire();
      return;
    }
    if (m_preparationIndex == m_preparation.size()) {
      m_preparing = false;
      issued();
      return;
    }
    const QPointer<NativePlacementVerification> alive(this);
    // Either callback may destroy/cancel its owner. Keep the current action
    // alive independently, and never run another action in this dispatch turn.
    const auto step = m_preparation[m_preparationIndex++];
    const bool success = step();
    if (!alive || m_done)
      return;
    if (m_now() >= m_due) {
      expire();
      return;
    }
    if (!success) {
      fail("Native placement preparation failed");
      return;
    }
    queuePreparation();
  }
  void check()
  {
    m_scheduled = false;
    if (m_done)
      return;
    if (m_now() >= m_due) {
      expire();
      return;
    }
    const QPointer<NativePlacementVerification> alive(this);
    // A probe may revoke or destroy its owner. Keep the callable and output
    // alive independently of this object throughout that callback.
    const auto probe = m_probe;
    QString reason;
    const auto result = probe(&reason);
    if (!alive || m_done)
      return;
    m_reason = std::move(reason);
    if (m_now() >= m_due) {
      expire();
      return;
    }
    if (result != Observation::Pending)
      finish(result == Observation::Verified, m_reason);
  }
  void finish(bool success, QString reason)
  {
    if (m_done)
      return;
    m_done = true;
    m_timer.stop();
    auto completion = std::move(m_completion);
    if (completion)
      completion(success, std::move(reason)); // May destroy this gate.
  }
  Probe m_probe;
  Completion m_completion;
  Now m_now;
  Clock::time_point m_due;
  QTimer m_timer;
  QString m_reason;
  Preparation m_preparation;
  std::size_t m_preparationIndex = 0;
  bool m_issued = false, m_scheduled = false, m_done = false, m_preparing = false;
};
} // namespace deskflow::windowshare
