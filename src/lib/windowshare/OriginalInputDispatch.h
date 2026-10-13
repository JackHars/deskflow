/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalInputRoute.h"
#include "SelectedPeer.h"
#include <chrono>
#include <cstdint>
#include <optional>

namespace deskflow::windowshare {

// Trusted, local, core-thread interface. No method waits for another event
// loop. Native/Qt work is exchanged as bounded immutable values by the
// implementation; none of these values authorizes an untrusted peer to post
// native input. Every method, including poll, may cause callback reentry.
class OriginalInputDispatcher
{
public:
  using Clock = std::chrono::steady_clock;
  struct Reservation
  {
    std::uint64_t owner = 0, nonce = 0;
    bool operator==(const Reservation &) const = default;
  };
  enum class State
  {
    Refused,  // No delivery and no unfinished handoff. Legacy remains available.
    Pending,  // Capacity retained, but native authority is not ready. Never deliver.
    Accepted, // Exact authority and output capacity retained. Never fall back.
    Revoked   // An unfinished/accepted handoff failed. Suppress, release, never retarget.
  };
  struct Result
  {
    State state = State::Refused;
    // Nonzero, strictly increasing per dispatcher for asynchronous preparation.
    // Every poll result echoes that ticket. Synchronous results use zero.
    std::uint64_t ticket = 0;
    std::optional<Reservation> reservation;

    static Result refused(std::uint64_t ticket = 0)
    {
      return {State::Refused, ticket, std::nullopt};
    }
    static Result pending(std::uint64_t ticket)
    {
      return {State::Pending, ticket, std::nullopt};
    }
    static Result accepted(Reservation reservation, std::uint64_t ticket = 0)
    {
      return {State::Accepted, ticket, reservation};
    }
    static Result revoked(std::uint64_t ticket = 0)
    {
      return {State::Revoked, ticket, std::nullopt};
    }
    bool wellFormed() const noexcept
    {
      switch (state) {
      case State::Refused:
      case State::Revoked:
        return !reservation;
      case State::Pending:
        return ticket && !reservation;
      case State::Accepted:
        return reservation && reservation->owner && reservation->nonce;
      }
      return false;
    }
  };
  virtual ~OriginalInputDispatcher() = default;
  // Retain exact capture/recipient/selection/point and reserve output capacity
  // before returning Pending or Accepted. The one absolute deadline must not
  // be extended by traffic, polling, a native stage transition or a retry.
  // Refused is not a spelling of Pending. After a handoff starts, Refused is
  // legal only when its cancellation/release/drain has completely finished.
  virtual Result reserve(
      const OriginalInputCaptured &captured, const OriginalInputRouteDecision &route, const SelectedPeer &selected,
      std::uint64_t existingOwner, Clock::time_point deadline
  ) = 0;
  // A terminal result consumes the preparation. Accepted transfers its output
  // reservation to the caller; duplicate/retired polls must return Revoked.
  virtual Result poll(std::uint64_t ticket) = 0;
  // Idempotently cancel this preparation before any output, including an
  // accepted reply that has not been consumed. A late completion must release
  // its exact owner. Never reopen forwarding while native release is unfinished.
  virtual void cancelPreparation(std::uint64_t ticket) noexcept = 0;
  virtual bool commit(const Reservation &reservation) = 0;
  virtual void cancel(const Reservation &reservation) noexcept = 0;
  virtual void release(std::uint64_t owner) noexcept = 0;
  virtual bool live(std::uint64_t owner) const noexcept = 0;
  // Terminal, idempotent revocation of all pending/output authority. Keep exact
  // cleanup ownership until its native boundary completes, even after Server
  // destruction. This is not permission to reopen the viewer forwarding gate.
  virtual void abort() noexcept = 0;
};

} // namespace deskflow::windowshare
