/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "io/AuthenticatedPeerTicket.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

class Server;

namespace deskflow::windowshare {

// An in-process value snapshot, not permission to route original input. It owns
// no proxy, stream, or Server pointer. The Server's selection fence and the TLS
// connection's independent fence must both remain live at the point of use.
class SelectedPeer final
{
public:
  SelectedPeer() = default;

  std::uint64_t serverIncarnation() const noexcept
  {
    return m_serverIncarnation;
  }
  std::uint64_t recipient() const noexcept
  {
    return m_recipient;
  }
  std::uint64_t generation() const noexcept
  {
    return m_generation;
  }
  const AuthenticatedPeerTicket &ticket() const noexcept
  {
    return m_ticket;
  }
  bool live() const noexcept
  {
    return m_serverIncarnation && m_recipient && m_generation && m_revoked &&
           !m_revoked->load(std::memory_order_acquire) && m_ticket.live();
  }
  bool sameSelection(const SelectedPeer &other) const noexcept
  {
    // Token identity prevents a reconstructed field tuple from reviving an
    // earlier selection. Each token is permanently bound to its copied ticket.
    return m_revoked && m_revoked == other.m_revoked && m_serverIncarnation == other.m_serverIncarnation &&
           m_recipient == other.m_recipient && m_generation == other.m_generation &&
           m_ticket.incarnation() == other.m_ticket.incarnation() &&
           m_ticket.fingerprint() == other.m_ticket.fingerprint();
  }

private:
  friend class ::Server;
  friend class SelectedPeerTestAccess;
  SelectedPeer(
      std::uint64_t server, std::uint64_t recipient, std::uint64_t generation, AuthenticatedPeerTicket ticket,
      std::shared_ptr<const std::atomic_bool> revoked
  )
      : m_serverIncarnation(server),
        m_recipient(recipient),
        m_generation(generation),
        m_ticket(std::move(ticket)),
        m_revoked(std::move(revoked))
  {
  }

  std::uint64_t m_serverIncarnation = 0, m_recipient = 0, m_generation = 0;
  AuthenticatedPeerTicket m_ticket;
  std::shared_ptr<const std::atomic_bool> m_revoked;
};

} // namespace deskflow::windowshare
