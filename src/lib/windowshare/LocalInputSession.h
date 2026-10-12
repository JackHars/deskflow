/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "LocalInputCompanion.h"

namespace deskflow::windowshare {

// Qt main-thread owner. Startup/shutdown run bounded local event loops before
// ordinary core startup and after core shutdown. No input authorizer is set.
class LocalInputCoreSession final
{
public:
  static std::unique_ptr<LocalInputCoreSession> start(
      LocalInputSocketPair pair, const QStringList &arguments, QString *error = nullptr,
      LocalInputCompanionLimits limits = {}
  );
  ~LocalInputCoreSession();
  // Returns false if the child did not exit within the drain deadline.
  bool stop();
  LocalInputChannel *channel() const;
  std::optional<LocalInputCompanion::ExitResult> exitResult() const;
  std::function<void(QString)> onStopped;

private:
  LocalInputCoreSession() = default;
  std::unique_ptr<LocalInputChannel> m_channel;
  std::unique_ptr<LocalInputCompanion> m_companion;
  int m_stopWaitMs = 2000;
};

// Diagnostic only: perform the real private handshake and shut down the owned
// child. No Deskflow shared state, input hooks, network listeners or NativeHost.
int localInputCoreProbe(LocalInputSocketPair pair);
int localInputHostProbe(std::unique_ptr<LocalInputChannel> channel);

} // namespace deskflow::windowshare
