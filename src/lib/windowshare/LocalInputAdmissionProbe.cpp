/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputCodeAdmission.h"
#include "LocalInputSession.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace deskflow::windowshare {
int localInputCodeAdmissionProbe(LocalInputSocketPair pair)
{
  QElapsedTimer total;
  total.start();
  QJsonObject result{
      {"privateInputHandshake", false},
      {"exactLibraryAdmission", false},
      {"companionStopped", false},
      {"originalInputEnabled", false},
      {"nativeInputHooks", false}
  };
  QString error;
  const auto session = LocalInputCoreSession::start(std::move(pair), {"--core-input-probe"}, &error);
  bool success = false;
  if (session) {
    result["privateInputHandshake"] = session->channel()->authenticated();
    QElapsedTimer admission;
    admission.start();
    auto guard = LocalInputCodeAdmission::request(*session->channel(), &error);
    if (guard) {
      QEventLoop loop;
      QTimer poll;
      poll.setInterval(10);
      QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        // state() enforces the production absolute deadline; this diagnostic
        // never supplies a longer budget or retries an expired inspection.
        if (guard->state() != LocalInputCodeAdmission::State::Pending)
          loop.quit();
      });
      poll.start();
      loop.exec();
      const auto proof = guard->proof();
      success = proof && proof->live();
      result["exactLibraryAdmission"] = success;
      if (!success)
        error = guard->error();
      guard->revoke();
    }
    result["admissionElapsedMs"] = admission.elapsed();
    const bool stopped = session->stop();
    const auto exit = session->exitResult();
    const bool clean = stopped && exit && exit->normalExit && exit->exitCode == 0;
    result["companionStopped"] = clean;
    success = success && clean;
    if (!clean && error.isEmpty())
      error = QStringLiteral("Code-admission probe companion did not exit cleanly");
  }
  result["totalElapsedMs"] = total.elapsed();
  if (!error.isEmpty())
    result["error"] = error.left(512);
  QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
  return success ? 0 : 1;
}
} // namespace deskflow::windowshare
