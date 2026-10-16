/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
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
} // namespace deskflow::windowshare
