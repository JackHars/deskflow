/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "InputEvidence.h"
#include <QFile>
#include <QJsonObject>
#include <QTimer>
#include <memory>

namespace deskflow::windowshare {
// Explicit experimental launch only. Own after QApplication and before every
// producer; destroy after the producers stop. No environment activation.
class InputEvidenceRuntime final : public QObject
{
public:
  // Only the disposable native fixture may allow extra time for its visible
  // Start button. This one-shot preparation budget never extends recording.
  static std::unique_ptr<InputEvidenceRuntime>
  open(const QString &path, InputEvidenceRole role, std::uint64_t preparationTimeoutMs = 180000);
  ~InputEvidenceRuntime() override;
  InputEvidenceRecorder &recorder() const
  {
    return *m_recorder;
  }
  // Native fixture UI only. This explicit request is not an arm or authority.
  bool requestRecord(const QString &privateNewPath);

private:
  friend struct InputEvidenceTestAccess;
  InputEvidenceRuntime() = default;
  void tick();
  void drain(bool yield = true);
  void finishJournalIfDrained();
  void endJournal();
  void write(const QJsonObject &value);
  InputEvidenceRecorder *m_recorder = nullptr;
  QFile m_output;
  QTimer m_timer;
  QString m_run, m_control;
  std::uint64_t m_duration = 0, m_rows = 0, m_bytes = 0, m_nextReadiness = 0, m_created = 0;
  std::uint64_t m_preparationTimeout = 0, m_armRequestedAt = 0;
  bool m_armSeen = false, m_stopSeen = false, m_endWritten = false, m_closeSeen = false, m_recordRequested = false;
  bool m_drainQueued = false;
};
} // namespace deskflow::windowshare
