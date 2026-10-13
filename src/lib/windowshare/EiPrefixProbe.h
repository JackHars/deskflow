/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <chrono>
#include <cstdint>

namespace deskflow::windowshare {

// A strictly local, *non-authorizing* receipt for a sender-side libei roundtrip.
// The callback is only marked observed once its entire ei_dispatch/get_event
// batch has ended. libei can synthesize PONG before DISCONNECT in one batch;
// consuming the first PONG without the rest would admit an already-dead peer.
// This does NOT establish Mutter's Clutter/Qt consumption or the original-input
// route: it is only the first sender-side ordering witness needed for P2.
class EiPrefixProbe final
{
public:
  using Clock = std::chrono::steady_clock;
  using Instant = Clock::time_point;
  static constexpr auto lifetime = std::chrono::milliseconds(250);
  enum class State
  {
    Idle,
    AwaitingPong,
    PongWithinBatch,
    Observed,
    Denied
  };

  bool begin(std::uint64_t pingId, Instant at, bool ownedSender) noexcept
  {
    if (at > Instant::max() - lifetime)
      return false;
    return begin(pingId, pingId, at, at + lifetime, ownedSender);
  }

  bool begin(std::uint64_t request, std::uint64_t pingId, Instant at, Instant deadline, bool ownedSender) noexcept
  {
    if (m_state != State::Idle || !ownedSender || !pingId || !request || request <= m_requestHighWater ||
        at > Instant::max() - lifetime || deadline <= at || deadline > at + lifetime)
      return false;
    m_request = m_requestHighWater = request;
    m_id = pingId;
    m_deadline = deadline;
    m_state = State::AwaitingPong;
    return true;
  }

  // Exact cancellation/resume retires this bounded transaction. New requests
  // on the same native connection cannot reuse a prior request identity.
  bool retire(std::uint64_t request) noexcept
  {
    if (!request || request != m_request)
      return false;
    m_state = State::Idle;
    m_request = m_id = 0;
    m_deadline = {};
    return true;
  }

  void pong(std::uint64_t pingId, Instant at) noexcept
  {
    if (m_state != State::AwaitingPong)
      return;
    if (!pingId || pingId != m_id || at >= m_deadline) {
      m_state = State::Denied;
      return;
    }
    m_state = State::PongWithinBatch;
  }

  // An unplug, session loss or native device invalidation always overrides
  // earlier PONG, including those already processed in this event batch.
  void invalidate() noexcept
  {
    if (m_state != State::Idle)
      m_state = State::Denied;
  }

  void finishEventBatch(Instant at) noexcept
  {
    if (m_state == State::Idle || m_state == State::Denied)
      return;
    if (at >= m_deadline) {
      m_state = State::Denied;
      return;
    }
    if (m_state == State::PongWithinBatch)
      m_state = State::Observed;
  }

  // An expired observation is never reusable, even if a subsequent event batch
  // contains the correct ping, or the caller forgets to poll for a while.
  bool observed(Instant at) noexcept
  {
    if (m_state == State::Observed && at < m_deadline)
      return true;
    if (m_state != State::Idle && at >= m_deadline)
      m_state = State::Denied;
    return false;
  }

  // Observed is a point-in-time diagnostic; no output-capacity/native-input
  // permit is minted and no caller may extend the absolute deadline.
  State state() const noexcept
  {
    return m_state;
  }

  std::uint64_t pingId() const noexcept
  {
    return m_id;
  }

  bool awaitingPong() const noexcept
  {
    return m_state == State::AwaitingPong || m_state == State::PongWithinBatch;
  }

  void resetAfterConnectionTeardown() noexcept
  {
    m_state = State::Idle;
    m_id = 0;
    m_request = m_requestHighWater = 0;
    m_deadline = {};
  }

private:
  State m_state = State::Idle;
  std::uint64_t m_id = 0;
  std::uint64_t m_request = 0, m_requestHighWater = 0;
  Instant m_deadline{};
};

} // namespace deskflow::windowshare
