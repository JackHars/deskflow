/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include <utility>

namespace deskflow::windowshare {

// The helper's reply is not proof that the parent's WindowServer cache has
// consumed the display change. Only a verified parent observation completes
// startup. Notifications wake this gate; there is no polling or sleep delay.
class HoldingDisplayStartup final : public QObject
{
public:
  using Probe = std::function<bool(QString *)>;
  using Completion = std::function<void(bool, QString)>;
  HoldingDisplayStartup(Probe probe, Completion completion)
      : m_probe(std::move(probe)),
        m_completion(std::move(completion))
  {
    m_deadline.setSingleShot(true);
    QObject::connect(&m_deadline, &QTimer::timeout, this, [this] { expire(); });
    m_deadline.start(5000);
  }
  void started()
  {
    if (!m_done && !m_started) {
      m_started = true;
      m_deadline.start(10000); // Existing helper deadline includes parent verification.
    }
  }
  void ready()
  {
    if (!m_done && m_started && !m_ready) {
      m_ready = true;
      changed();
    }
  }
  void changed()
  {
    if (m_done || !m_ready)
      return;
    if (m_probe(&m_reason))
      finish(true, {});
  }
  void fail(QString reason)
  {
    finish(false, std::move(reason));
  }
  void expire()
  {
    finish(
        false,
        m_ready ? "Holding display verification timed out: " + m_reason : "Holding-display helper readiness timed out"
    );
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
  QTimer m_deadline;
  QString m_reason;
  bool m_started = false, m_ready = false, m_done = false;
};
} // namespace deskflow::windowshare
