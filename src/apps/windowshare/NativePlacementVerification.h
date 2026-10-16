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
  void issued()
  {
    if (m_done || m_issued)
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

private:
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
  bool m_issued = false, m_scheduled = false, m_done = false;
};
} // namespace deskflow::windowshare
