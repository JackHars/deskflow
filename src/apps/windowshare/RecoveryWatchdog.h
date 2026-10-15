/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <memory>
#include <string>
#include <sys/types.h>

namespace deskflow::windowshare::native {

// Only the Mac host creates this child. It must arm successfully before any
// source-window export can start. The read end of a private pipe is held by an
// independent process; when the host exits, that process runs the existing
// identity-checked, journal-locked --recover path. Neither the watchdog nor
// its pipe grants native input/window-control authority to a remote peer.
class RecoveryWatchdog final
{
public:
  RecoveryWatchdog(const std::string &executable, const std::string &journal, std::string *error);
  ~RecoveryWatchdog();
  RecoveryWatchdog(const RecoveryWatchdog &) = delete;
  RecoveryWatchdog &operator=(const RecoveryWatchdog &) = delete;
  bool armed() const;
  bool alive();
  // Called only by the creating (Host event-loop) thread. A separate thread
  // exits this process if startup takes 15 seconds, or progress stops for five
  // seconds after the first heartbeat. A late heartbeat cannot renew the lease.
  // Stop renewing when shutdown begins; the guard covers native destructors too.
  bool heartbeat();
  static constexpr int progressTimeoutExitCode = 70;

private:
  struct Progress;
  std::unique_ptr<Progress> m_progress;
  int m_write = -1;
  pid_t m_pid = -1;
};

// Called before Qt or any network/native host initialization. Mode can only be
// entered by an exact private argv shape; the child inherits both pipe FDs.
bool recoveryWatchdogMode(int argc, char **argv);
int runRecoveryWatchdog(int argc, char **argv);

} // namespace deskflow::windowshare::native
