/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "LocalInputCompanion.h"
#include "OriginalInputRoute.h"
#include "OriginalPointerJoin.h"
#include "SelectedPeerMailbox.h"
#include "WindowDragEdgeJoin.h"

namespace deskflow::windowshare {

#ifdef __APPLE__
class OriginalInputCaptureSource;
#endif

// Qt main-thread owner. Startup/shutdown run bounded local event loops before
// ordinary core startup and after core shutdown. No input authorizer is set.
class LocalInputCoreSession final
{
  friend struct LocalInputCoreSessionTestAccess;

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
  std::shared_ptr<WindowDragEdgeMailbox> windowDragEdgeMailbox() const;
#ifdef __APPLE__
  std::shared_ptr<OriginalInputCaptureSource> captureSource() const;
  // Owner-thread observations only. This callback reports a historical exact
  // join (or denial); it neither suppresses legacy input nor grants delivery.
  // It may stop/destroy the session synchronously.
  std::function<void(const OriginalInputRouted &)> onRoutedInput;
  std::function<void(const OriginalInputCaptured &)> onCapturedInput;
  std::function<void(const OriginalPointerJoin::Match &)> onPointerWindow;
  // Exact original press plus configured edge, or null when withdrawn. A
  // callback must recheck candidate.live(); this does not switch computers.
  std::function<void(const std::optional<WindowDragEdgeCandidate> &)> onWindowDragEdge;
#endif
  std::function<void(QString)> onStopped;

private:
  LocalInputCoreSession() = default;
  std::shared_ptr<SelectedPeerMailbox> m_selectedPeerMailbox = std::make_shared<SelectedPeerMailbox>();
  std::shared_ptr<WindowDragEdgeMailbox> m_windowDragEdgeMailbox = std::make_shared<WindowDragEdgeMailbox>();
  std::unique_ptr<LocalInputChannel> m_channel;
  std::unique_ptr<LocalInputCompanion> m_companion;
  int m_stopWaitMs = 2000;
#ifdef __APPLE__
  void drainCapturedInput();
  // Terminal even if the underlying epoch was already revoked by overflow or
  // discontinuity. Capture-timer shutdown never enters stop()'s bounded wait.
  bool m_captureEmergencyHandled = false;
  bool m_pointerNotificationsStopped = false;
  std::shared_ptr<OriginalInputCaptureSource> m_captureSource;
  std::unique_ptr<OriginalInputNeutralBarrier> m_captureBarrier;
  std::unique_ptr<OriginalInputRouteJoin> m_captureJoin;
  std::unique_ptr<OriginalPointerJoin> m_pointerJoin;
  WindowDragEdgeJoin m_windowDragEdgeJoin;
  std::uint64_t m_windowDragEdgeGeneration = 0, m_windowDragEdgeSequence = 0;
  std::shared_ptr<bool> m_callbackLifetime = std::make_shared<bool>(true);
  QTimer m_captureTimer;
#endif
};

// Diagnostic only: perform the real private handshake and shut down the owned
// child. No Deskflow shared state, input hooks, network listeners or NativeHost.
int localInputCoreProbe(LocalInputSocketPair pair);
int localInputHostProbe(std::unique_ptr<LocalInputChannel> channel);

} // namespace deskflow::windowshare
