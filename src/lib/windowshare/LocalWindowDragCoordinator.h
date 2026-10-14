/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "LocalInputPreflight.h"
#include "WindowDragEdgeJoin.h"
#include "WindowDragTransaction.h"

namespace deskflow::windowshare {
// Preparation is intentionally separate from input routing and native grab
// cutover. It may create a non-interactive preview; it cannot commit a drop.
// One Qt owner, no additional raw consumer, bounded traffic/storage/deadlines.
class LocalWindowDragCore final
{
public:
  LocalWindowDragCore(LocalInputChannel &, LocalInputPeerPolicy, std::function<bool()> admission);
  ~LocalWindowDragCore();
  LocalWindowDragCore(const LocalWindowDragCore &) = delete;
  LocalWindowDragCore &operator=(const LocalWindowDragCore &) = delete;
  void observe(const std::optional<WindowDragEdgeCandidate> &candidate);
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
  };
  LocalWindowDragHost(LocalInputChannel &, LocalInputPeerPolicy, Providers);
  ~LocalWindowDragHost();
  LocalWindowDragHost(const LocalWindowDragHost &) = delete;
  LocalWindowDragHost &operator=(const LocalWindowDragHost &) = delete;
  // Called before the NativeHost sees the same ordered pointer observation.
  // A real release cancels preparation. A later cutover coordinator must own
  // both the native grab receipt and drop permit before enabling commit.
  void pointer(const LocalWindowPointer &);
  void stop();

private:
  struct State;
  std::shared_ptr<State> m_state;
};
} // namespace deskflow::windowshare
