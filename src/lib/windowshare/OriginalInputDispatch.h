/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalInputRoute.h"
#include "SelectedPeer.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

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
  struct Preview
  {
    OriginalInputCaptured captured;
    OriginalInputRouteDecision route;
    SelectedPeer selected;
    Clock::time_point deadline;
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
  // Scheduling only: may run on the private coordinator thread while its
  // state is locked. The callback must only enqueue a bounded Core wake and
  // must never call dispatcher/Server methods inline. Empty clears it.
  virtual void setProgressNotifier(std::function<void()>) noexcept
  {
  }
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
  // A mapped dispatcher presents the frozen cursor before its native cutoff.
  // Server must not write a second legacy motion after accepting that receipt.
  virtual bool presentsCursor() const noexcept
  {
    return false;
  }
  // An active absolute-position route may explicitly allow replacement of
  // adjacent, unreserved hover motions. Server still requires no held press,
  // identical selection/flags and exact consecutive one-part captures. The
  // raw capture stream and each historical routing outcome remain intact.
  virtual bool canCoalesceHoverMotion() const noexcept
  {
    return false;
  }
  // Ordinary fallback can acquire input only after the exact recipient's
  // private handoff has fully released. Unsupported captures must not reopen
  // the Core stream while an independent Viewer All gate is still closed.
  // Existing exact legacy-owned releases remain the Server's responsibility.
  virtual bool legacyAcquisitionReady(std::uint64_t) const noexcept
  {
    return true;
  }
  // Advisory bounded lookahead only. It may prepare exact point observations
  // under an already active owner, but cannot acquire a press or deliver input.
  // Server still reserves/commits every record in order through the normal API.
  virtual void preview(std::span<const Preview>)
  {
  }
  // Called on the Core event thread even when no input arrives. Progress
  // retained cleanup without renewing a delivery deadline or acquiring input.
  virtual void maintain() noexcept
  {
  }
  // Terminal, idempotent revocation of all pending/output authority. Keep exact
  // cleanup ownership until its native boundary completes, even after Server
  // destruction. This is not permission to reopen the viewer forwarding gate.
  virtual void abort() noexcept = 0;
};

} // namespace deskflow::windowshare
