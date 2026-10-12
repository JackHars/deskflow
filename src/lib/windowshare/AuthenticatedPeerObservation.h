/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "io/AuthenticatedPeerTicket.h"
#include <chrono>
#include <utility>

namespace deskflow::windowshare {

class Channel;

// Retainable connection evidence, not permission to route input. A refresh
// creates a new observation; it never extends the lifetime of an older copy.
// Channel access stays on its QObject thread, but this value has no Qt owner.
class AuthenticatedPeerObservation final
{
  friend class Channel;
  friend struct AuthenticatedPeerObservationTestAccess;
  using Clock = std::chrono::steady_clock;

public:
  static constexpr auto freshness = std::chrono::milliseconds(6000);

  AuthenticatedPeerObservation() = default;
  const AuthenticatedPeerTicket &ticket() const noexcept
  {
    return m_ticket;
  }
  bool live() const noexcept
  {
    return liveAt(Clock::now());
  }
  bool sameConnection(const AuthenticatedPeerObservation &other) const noexcept
  {
    return m_ticket.incarnation() != 0 && m_ticket.incarnation() == other.m_ticket.incarnation() &&
           m_ticket.fingerprint() == other.m_ticket.fingerprint();
  }

private:
  AuthenticatedPeerObservation(AuthenticatedPeerTicket ticket, Clock::time_point read)
      : m_ticket(std::move(ticket)),
        m_read(read),
        m_deadline(read + freshness)
  {
  }
  bool liveAt(Clock::time_point now) const noexcept
  {
    return m_ticket.live() && now >= m_read && now < m_deadline;
  }

  AuthenticatedPeerTicket m_ticket;
  Clock::time_point m_read{}, m_deadline{};
};

} // namespace deskflow::windowshare
