/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <chrono>
#include <cstdio>

namespace deskflow::windowshare {
// Low-frequency lifecycle observations only. No event payload, key text,
// clipboard contents, credentials or input-authority decisions are logged.
inline void lifecycleDiagnostic(
    const QString &component, const QString &step, const QString &state, const QString &message,
    const QJsonObject &details = {}
)
{
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  const auto bytes =
      QJsonDocument(
          QJsonObject{
              {"component", component},
              {"step", step},
              {"state", state},
              {"message", message},
              {"details", details},
              {"pid", QCoreApplication::applicationPid()},
              {"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
              {"monotonicNs", QString::number(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count())}
          }
      ).toJson(QJsonDocument::Compact);
  const auto line = QByteArray("DESKFLOW_STEP ") + bytes + '\n';
  std::fwrite(line.constData(), 1, line.size(), stderr);
  std::fflush(stderr);
}

// Report expensive synchronous native work without printing every motion or
// recording input contents. This observes duration; it never extends a lease.
class SlowOperationDiagnostic final
{
public:
  SlowOperationDiagnostic(const char *component, const char *step)
      : m_component(component),
        m_step(step),
        m_started(std::chrono::steady_clock::now())
  {
  }
  ~SlowOperationDiagnostic()
  {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_started).count();
    if (elapsed >= 25)
      lifecycleDiagnostic(
          m_component, m_step, "slow", "Operation exceeded the diagnostic duration threshold",
          {{"elapsedMs", qint64(elapsed)},
           {"applicationThread",
            QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread()}}
      );
  }
  SlowOperationDiagnostic(const SlowOperationDiagnostic &) = delete;
  SlowOperationDiagnostic &operator=(const SlowOperationDiagnostic &) = delete;

private:
  const char *m_component;
  const char *m_step;
  std::chrono::steady_clock::time_point m_started;
};
} // namespace deskflow::windowshare
