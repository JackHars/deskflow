/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>

class SecurePeerIdentityTests;

namespace deskflow {

class AuthenticatedPeerIssuer;

// Retainable evidence about one authenticated connection, not a peer-supplied
// assertion. Check live() at each use: copying this value never extends its
// authority. The incarnation is process-local; it is not a persistent or wire
// identity without a separately authenticated process incarnation.
class AuthenticatedPeerTicket
{
public:
  using Fingerprint = std::array<std::uint8_t, 32>;

  AuthenticatedPeerTicket() = default;
  const Fingerprint &fingerprint() const
  {
    return m_fingerprint;
  }
  std::uint64_t incarnation() const
  {
    return m_incarnation;
  }
  bool live() const
  {
    return m_live && m_live->load(std::memory_order_acquire);
  }

private:
  friend class AuthenticatedPeerIssuer;
  AuthenticatedPeerTicket(
      Fingerprint fingerprint, std::uint64_t incarnation, std::shared_ptr<const std::atomic_bool> live
  )
      : m_fingerprint(fingerprint),
        m_incarnation(incarnation),
        m_live(std::move(live))
  {
  }

  Fingerprint m_fingerprint{};
  std::uint64_t m_incarnation = 0;
  std::shared_ptr<const std::atomic_bool> m_live;
};

// Trusted, process-local source of evidence. Only the stream that performed
// authentication owns this object; tests may supply one for a fake stream.
// Issue at most once, after full peer authentication. revoke() is terminal even
// before issue(), so a handshake racing shutdown cannot resurrect authority.
// The source mutex is independent of TLS locks; no callback runs under it.
class AuthenticatedPeerIssuer
{
public:
  using Fingerprint = AuthenticatedPeerTicket::Fingerprint;
  AuthenticatedPeerIssuer() = default;
  AuthenticatedPeerIssuer(const AuthenticatedPeerIssuer &) = delete;
  AuthenticatedPeerIssuer &operator=(const AuthenticatedPeerIssuer &) = delete;
  ~AuthenticatedPeerIssuer()
  {
    revoke();
  }

  std::optional<AuthenticatedPeerTicket> issue(const Fingerprint &fingerprint)
  {
    std::scoped_lock lock(m_mutex);
    if (m_revoked || m_ticket)
      return std::nullopt;
    const auto incarnation = allocateIncarnation();
    if (incarnation == 0) {
      m_revoked = true;
      return std::nullopt;
    }
    m_live = std::make_shared<std::atomic_bool>(true);
    m_ticket = AuthenticatedPeerTicket(fingerprint, incarnation, m_live);
    return m_ticket;
  }

  std::optional<AuthenticatedPeerTicket> ticket() const
  {
    std::scoped_lock lock(m_mutex);
    return m_revoked ? std::nullopt : m_ticket;
  }

  void revoke()
  {
    std::scoped_lock lock(m_mutex);
    m_revoked = true;
    if (m_live)
      m_live->store(false, std::memory_order_release);
  }

private:
  friend class ::SecurePeerIdentityTests;

  static std::uint64_t reserveIncarnation(std::atomic<std::uint64_t> &next)
  {
    auto value = next.load(std::memory_order_relaxed);
    while (value != 0) {
      // Zero permanently exhausts the allocator; never wrap to a reused ID.
      const auto successor = value == std::numeric_limits<std::uint64_t>::max() ? 0 : value + 1;
      if (next.compare_exchange_weak(value, successor, std::memory_order_relaxed))
        return value;
    }
    return 0;
  }

  static std::uint64_t allocateIncarnation()
  {
    static std::atomic<std::uint64_t> next{1};
    return reserveIncarnation(next);
  }

  mutable std::mutex m_mutex;
  bool m_revoked = false;
  std::shared_ptr<std::atomic_bool> m_live;
  std::optional<AuthenticatedPeerTicket> m_ticket;
};

} // namespace deskflow
