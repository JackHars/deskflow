/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "LocalInputChannel.h"
#include <QPointer>
#include <QStringList>

namespace deskflow::windowshare {

class LocalInputPeerTrust;

struct LocalInputCompanionLimits
{
  // Covers child launch, synchronous code verification, and private handshake.
  // Verification cannot be preempted; its return is checked against this age.
  int startupMs = 15000;
  int gracefulMs = 300;
  int terminateMs = 1000;
  LocalInputLimits channel;
  // Explicit diagnostics only: inherit stderr without a parent-side output queue.
  bool forwardStandardError = false;
};

// Main Qt thread only. Create the socketpair before QApplication or any worker
// threads, then move it here. The packaged Host verifier supplies the only
// executable this launcher can start; arguments cannot override its private FD.
class LocalInputCompanion final : public QObject
{
public:
  struct ExitResult
  {
    bool normalExit = false;
    int exitCode = -1;
  };
  enum class State
  {
    Idle,
    Starting,
    Running,
    Stopping,
    Stopped
  };

  static std::unique_ptr<LocalInputCompanion>
  create(LocalInputSocketPair pair, QString *error = nullptr, LocalInputCompanionLimits limits = {});
  ~LocalInputCompanion() override;

  // One launch attempt. Set onChannel before start(). Starting includes the
  // private handshake; Running means the retained channel is authenticated.
  bool start(QStringList hostArguments);
  void stop();
  State state() const;
  std::optional<LocalInputProcess> childProcess() const;
  // Present only after a successfully started child has been reaped. Available
  // before onStopped, including when stop detects exit before finished arrives.
  std::optional<ExitResult> exitResult() const;

  // Configure and retain this unique_ptr. Dropping/closing it stops the child.
  // Authorization remains default-deny. Callbacks may stop/delete the launcher
  // or delete the channel; callback exceptions stop the companion. No callback
  // is invoked after launcher destruction. onStopped runs once after exit or a
  // rejected launch; it never permits a second start on this launcher.
  std::function<void(std::unique_ptr<LocalInputChannel>)> onChannel;
  std::function<void(QString)> onStopped;

private:
  struct Run;
  LocalInputCompanion(
      LocalInputSocketPair pair, std::unique_ptr<LocalInputPeerTrust> trust, LocalInputCompanionLimits limits
  );
  void started();
  void observeChannel();
  void beginStop(const QString &reason);
  void completed(const QString &reason, std::optional<ExitResult> result = std::nullopt);

  std::unique_ptr<LocalInputSocketPair> m_pair;
  std::unique_ptr<LocalInputPeerTrust> m_trust;
  LocalInputCompanionLimits m_limits;
  QPointer<LocalInputChannel> m_channel;
  std::optional<LocalInputProcess> m_child;
  std::optional<ExitResult> m_exitResult;
  Run *m_run = nullptr;
  State m_state = State::Idle;
};

} // namespace deskflow::windowshare
