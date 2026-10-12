/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "SelectedPeer.h"
#include <functional>
#include <memory>
#include <optional>

class IEventQueue;
class EventQueueTimer;

namespace deskflow::windowshare {
class SelectedPeerMailbox;

// Construct, dispatch and stop only on the core event thread. The reader never
// crosses threads; only immutable values reach the mailbox. Nothing is
// published until an ordinary core timer is dispatched after queue startup.
class SelectedPeerPublisher final
{
public:
  using Reader = std::function<std::optional<SelectedPeer>()>;
  SelectedPeerPublisher(IEventQueue &events, std::shared_ptr<SelectedPeerMailbox> mailbox, Reader reader);
  ~SelectedPeerPublisher();
  SelectedPeerPublisher(const SelectedPeerPublisher &) = delete;
  SelectedPeerPublisher &operator=(const SelectedPeerPublisher &) = delete;
  void stop();

private:
  struct State;
  IEventQueue &m_events;
  std::shared_ptr<State> m_state;
  EventQueueTimer *m_timer = nullptr;
};
} // namespace deskflow::windowshare
