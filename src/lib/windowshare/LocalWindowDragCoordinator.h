/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "LocalInputPreflight.h"
#include "WindowDragEdgeJoin.h"
#include "WindowDragTransaction.h"

namespace deskflow::windowshare {
// The original native owner retains every button transition. Only admitted
// held movement is withheld; a joined real release and Server completion are
// required before committing the preview and switching the cursor.
// One Qt owner, no additional raw consumer, bounded traffic/storage/deadlines.
class LocalWindowDragCore final
{
public:
  struct Providers
  {
    std::function<bool()> admission;
    std::function<bool(OriginalInputStamp)> armMotion, refreshMotion;
    std::function<void()> cancelMotion;
    std::shared_ptr<WindowDragEdgeMailbox> mailbox;
  };
  LocalWindowDragCore(LocalInputChannel &, LocalInputPeerPolicy, std::function<bool()> admission);
  LocalWindowDragCore(LocalInputChannel &, LocalInputPeerPolicy, Providers);
  ~LocalWindowDragCore();
  LocalWindowDragCore(const LocalWindowDragCore &) = delete;
  LocalWindowDragCore &operator=(const LocalWindowDragCore &) = delete;
  void observe(const std::optional<WindowDragEdgeCandidate> &candidate);
  void capture(const OriginalInputCaptured &);
  void pointer(const OriginalPointerJoin::Match &);
  void routed(const OriginalInputRouted &);
  void moved(const LocalWindowMove &);
  void stop();

private:
  struct State;
  std::shared_ptr<State> m_state;
};

class LocalWindowDragHost final
{
public:
  struct Providers
  {
    std::function<bool()> admission;
    std::function<std::optional<AuthenticatedPeerObservation>()> connection;
    std::function<bool(const WindowDragBinding &, const LocalWindowDrag &)> prepare;
    std::function<void(const WindowDragBinding &, const LocalWindowDrag &)> motion;
    std::function<void(const WindowDragBinding &)> cancel;
    std::function<bool(const WindowDragBinding &, const LocalWindowDrag &, const WindowDragRelease &)> drop;
    std::function<void(const WindowDragBinding &)> finish;
  };
  LocalWindowDragHost(LocalInputChannel &, LocalInputPeerPolicy, Providers);
  ~LocalWindowDragHost();
  LocalWindowDragHost(const LocalWindowDragHost &) = delete;
  LocalWindowDragHost &operator=(const LocalWindowDragHost &) = delete;
  // Called before the NativeHost sees the same ordered pointer observation.
  // A matching real release is retained for a bounded, explicit Core Drop.
  void pointer(const LocalWindowPointer &);
  void ready(const WindowDragBinding &, std::uint64_t sequence);
  void committed(const WindowDragBinding &);
  void cancelled(const WindowDragBinding &);
  // Exact exported window ID is supplied by the local Bridge catalog, never
  // by the remote event. Native grab observations still do not identify a device.
  void moved(LocalWindowMove);
  void stop();

private:
  struct State;
  std::shared_ptr<State> m_state;
};
} // namespace deskflow::windowshare
