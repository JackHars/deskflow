/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "DiagnosticOutput.h"
#include "DiagnosticWriter.h"
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
inline QByteArray lifecycleDiagnosticLine(
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
              {"threadId", QString::number(quintptr(QThread::currentThreadId()))},
              {"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
              {"monotonicNs", QString::number(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count())}
          }
      ).toJson(QJsonDocument::Compact);
  return QByteArray("DESKFLOW_STEP ") + bytes + '\n';
}

inline void lifecycleDiagnostic(
    const QString &component, const QString &step, const QString &state, const QString &message,
    const QJsonObject &details = {}
)
{
#ifndef Q_OS_WIN
  // The detached shutdown path must own everything it uses, and must not call
  // Qt after application/static teardown. Normal records are formatted with
  // their original producer metadata before admission; loss notices use only
  // standard-library state on the writer thread.
  static DiagnosticWriter writer(
      [output = std::make_shared<DiagnosticOutput>(STDERR_FILENO)](std::string_view line) {
        return output->write(line);
      },
      [pid = QCoreApplication::applicationPid()](std::uint64_t dropped, std::uint64_t oversized) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::string(
                   "DESKFLOW_STEP {\"component\":\"Diagnostics\",\"step\":\"diagnostic.loss\","
                   "\"state\":\"failed\",\"message\":\"Diagnostic output exceeded its bounded queue\","
                   "\"details\":{\"droppedRecords\":\""
               ) +
               std::to_string(dropped) + "\",\"oversizedRecords\":\"" + std::to_string(oversized) +
               "\"},\"pid\":" + std::to_string(pid) + " ,\"monotonicNs\":\"" +
               std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()) + "\"}\n";
      }
  );
  (void)writer.append(lifecycleDiagnosticLine(component, step, state, message, details).toStdString());
#else
  // Window sharing's native hosts are Mac/Linux. Preserve existing Windows
  // logging until a native descriptor sink is supplied for that platform.
  const auto line = lifecycleDiagnosticLine(component, step, state, message, details);
  std::fwrite(line.constData(), 1, std::size_t(line.size()), stderr);
  std::fflush(stderr);
#endif
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
          {{"elapsedMs", qint64(elapsed)}}
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
