/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "LocalInputCompanion.h"
#include "SelectedPeerMailbox.h"

namespace deskflow::windowshare {

#ifdef __APPLE__
class OriginalInputCaptureSource;
#endif

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
  std::shared_ptr<SelectedPeerMailbox> selectedPeerMailbox() const;
#ifdef __APPLE__
  std::shared_ptr<OriginalInputCaptureSource> captureSource() const;
#endif
  std::function<void(QString)> onStopped;

private:
  LocalInputCoreSession() = default;
  std::shared_ptr<SelectedPeerMailbox> m_selectedPeerMailbox = std::make_shared<SelectedPeerMailbox>();
  std::unique_ptr<LocalInputChannel> m_channel;
  std::unique_ptr<LocalInputCompanion> m_companion;
  int m_stopWaitMs = 2000;
#ifdef __APPLE__
  void drainCapturedInput();
  std::shared_ptr<OriginalInputCaptureSource> m_captureSource;
  std::unique_ptr<OriginalInputNeutralBarrier> m_captureBarrier;
  QTimer m_captureTimer;
#endif
};

// Diagnostic only: perform the real private handshake and shut down the owned
// child. No Deskflow shared state, input hooks, network listeners or NativeHost.
int localInputCoreProbe(LocalInputSocketPair pair);
int localInputHostProbe(std::unique_ptr<LocalInputChannel> channel);

} // namespace deskflow::windowshare
