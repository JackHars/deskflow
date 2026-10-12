/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalInput.h"
#include "OriginalInputStamp.h"
#include "io/AuthenticatedPeerTicket.h"
#include <chrono>
#include <limits>

namespace deskflow::windowshare {

struct OriginalInputCaptured
{
  OriginalInputDelivery input;
  OriginalInputTranslation translation;
};

enum class OriginalInputRouteKind : std::uint8_t
{
  Key,
  Button,
  Motion,
  Scroll,
  Other
};
enum class OriginalInputRouteOutcome : std::uint8_t
{
  Dispatched,
  NoTranslation,
  Expanded,
  Uncorrelated,
  Filtered,
  NoSend,
  Local,
  Relative,
  Ambiguous,
  Unauthenticated,
  Failed,
  // The local dispatcher committed its reserved original output. Historical
  // routing evidence only; this is not proof of native application consumption.
  Original
};

// A historical core routing decision, not a current selection or permission to
// inject. Coordinates are the exact core decision in the recipient's desktop
// coordinate space; they do not attest the remote OS cursor or viewport.
struct OriginalInputRouteDecision
{
  OriginalInputStamp stamp;
  OriginalInputRouteKind kind = OriginalInputRouteKind::Other;
  OriginalInputRouteOutcome outcome = OriginalInputRouteOutcome::NoSend;
  std::uint64_t server = 0, recipient = 0;
  deskflow::AuthenticatedPeerTicket ticket;
  std::int32_t x = 0, y = 0;
  bool pointKnown = false;
};

// One core producer, one Session consumer, no overwrite/coalescing. bind is
// one-shot and performed before input dispatch. Revocation is thread-safe and
// survives destruction in every retained observation. Stop both workers before
// destruction. No callback into Server or a client is retained here.
class OriginalInputRoutes final
{
public:
  static constexpr std::size_t capacity = 1024;
  OriginalInputRoutes() = default;
  ~OriginalInputRoutes()
  {
    revoke();
  }
  OriginalInputRoutes(const OriginalInputRoutes &) = delete;
  OriginalInputRoutes &operator=(const OriginalInputRoutes &) = delete;
  bool bind(std::uint64_t server) noexcept;
  // Producer only, before deferring a Server action. Later immediate filter
  // decisions must not overtake it. publish() resolves this exact slot; only
  // the ready prefix reaches the existing diagnostic consumer.
  bool defer(OriginalInputStamp stamp) noexcept;
  bool publish(OriginalInputRouteDecision decision) noexcept;
  std::optional<OriginalInputRouteDecision> pop() noexcept;
  void revoke() noexcept
  {
    m_revoked->store(true, std::memory_order_release);
  }
  bool live() const noexcept
  {
    return !m_revoked->load(std::memory_order_acquire);
  }
  std::shared_ptr<const std::atomic_bool> evidence() const noexcept
  {
    return m_revoked;
  }

private:
  bool writeReady(OriginalInputRouteDecision decision) noexcept;
  bool flushDeferred() noexcept;
  bool stageAvailable() noexcept;
  struct Deferred
  {
    OriginalInputStamp stamp;
    std::optional<OriginalInputRouteDecision> decision;
  };
  std::array<OriginalInputRouteDecision, capacity> m_records;
  // Only the core producer accesses staging. The Session sees m_records via
  // the unchanged SPSC publication; it never reads unresolved staging slots.
  std::array<Deferred, capacity> m_deferred;
  std::uint64_t m_deferredWritten = 0, m_deferredRead = 0;
  std::shared_ptr<std::atomic_bool> m_revoked = std::make_shared<std::atomic_bool>(false);
  std::atomic<std::uint64_t> m_server{0}, m_written{0}, m_read{0};
};

inline bool OriginalInputRoutes::bind(std::uint64_t server) noexcept
{
  std::uint64_t expected = 0;
  if (server && live() && m_server.compare_exchange_strong(expected, server, std::memory_order_acq_rel))
    return true;
  revoke();
  return false;
}
inline bool OriginalInputRoutes::stageAvailable() noexcept
{
  if (m_deferredWritten == std::numeric_limits<std::uint64_t>::max() ||
      m_deferredWritten - m_deferredRead >= capacity) {
    revoke();
    return false;
  }
  return true;
}
inline bool OriginalInputRoutes::defer(OriginalInputStamp stamp) noexcept
{
  if (!live())
    return false;
  if (!m_server.load(std::memory_order_acquire) || !stamp || !stamp.part || stamp.part > maximumOriginalInputParts) {
    revoke();
    return false;
  }
  for (auto index = m_deferredRead; index < m_deferredWritten; ++index) {
    if (m_deferred[index % capacity].stamp == stamp) {
      revoke();
      return false;
    }
  }
  if (!stageAvailable())
    return false;
  m_deferred[m_deferredWritten % capacity] = {stamp, std::nullopt};
  ++m_deferredWritten;
  return live();
}
inline bool OriginalInputRoutes::publish(OriginalInputRouteDecision decision) noexcept
{
  if (!live())
    return false;
  const auto server = m_server.load(std::memory_order_acquire);
  if (!server || !decision.stamp || !decision.stamp.part || decision.stamp.part > maximumOriginalInputParts ||
      (decision.server && decision.server != server)) {
    revoke();
    return false;
  }
  decision.server = server;
  if (m_deferredRead == m_deferredWritten)
    return writeReady(std::move(decision));
  for (auto index = m_deferredRead; index < m_deferredWritten; ++index) {
    auto &entry = m_deferred[index % capacity];
    if (entry.stamp != decision.stamp)
      continue;
    if (entry.decision) {
      revoke();
      return false;
    }
    entry.decision = std::move(decision);
    return flushDeferred();
  }
  if (!stageAvailable())
    return false;
  const auto stamp = decision.stamp;
  m_deferred[m_deferredWritten % capacity] = {stamp, std::move(decision)};
  ++m_deferredWritten;
  return flushDeferred();
}
inline bool OriginalInputRoutes::flushDeferred() noexcept
{
  while (m_deferredRead < m_deferredWritten) {
    auto &entry = m_deferred[m_deferredRead % capacity];
    if (!entry.decision)
      break;
    auto decision = std::move(*entry.decision);
    entry = {};
    ++m_deferredRead;
    if (!writeReady(std::move(decision)))
      return false;
  }
  return live();
}
inline bool OriginalInputRoutes::writeReady(OriginalInputRouteDecision decision) noexcept
{
  if (!live())
    return false;
  const auto written = m_written.load(std::memory_order_relaxed);
  if (written == std::numeric_limits<std::uint64_t>::max() ||
      written - m_read.load(std::memory_order_acquire) >= capacity) {
    revoke();
    return false;
  }
  m_records[written % capacity] = std::move(decision);
  m_written.store(written + 1, std::memory_order_release);
  return live();
}
inline std::optional<OriginalInputRouteDecision> OriginalInputRoutes::pop() noexcept
{
  if (!live())
    return {};
  const auto read = m_read.load(std::memory_order_relaxed);
  if (read == m_written.load(std::memory_order_acquire))
    return {};
  auto result = m_records[read % capacity];
  m_read.store(read + 1, std::memory_order_release);
  if (!live())
    return {};
  return result;
}
struct OriginalInputRouted
{
  OriginalInputDelivery input;
  OriginalInputRouteDecision route;
  std::shared_ptr<const std::atomic_bool> routeRevoked;
  // Even a live Dispatched observation is only a join, never input authority.
  bool live() const noexcept;
};

// Session/consumer thread only. Raw captures and core decisions may arrive in
// either order. Output stays in capture order. Every raw record is consumed by
// the existing Session consumer, including records denied correlation. Missing
// decisions and bounded-buffer exhaustion revoke route evidence; quiet time
// never supplies a missing decision. No physical-neutral claim is made.
class OriginalInputRouteJoin final
{
public:
  using Clock = std::chrono::steady_clock;
  static constexpr auto timeout = std::chrono::milliseconds(250);
  static constexpr std::size_t capacity = OriginalInputStream::capacity;
  OriginalInputRouteJoin(OriginalInputEpoch epoch, std::shared_ptr<OriginalInputRoutes> routes);
  bool capture(OriginalInputCaptured captured, Clock::time_point now = Clock::now()) noexcept;
  bool decision(OriginalInputRouteDecision decision, Clock::time_point now = Clock::now()) noexcept;
  std::optional<OriginalInputRouted> pop(Clock::time_point now = Clock::now()) noexcept;
  bool live() const noexcept;
  void revoke() noexcept;

private:
  struct Slot
  {
    std::uint64_t sequence = 0;
    std::optional<OriginalInputCaptured> captured;
    std::optional<OriginalInputRouteDecision> decision;
    Clock::time_point deadline{};
  };
  Slot *slot(std::uint64_t sequence) noexcept;
  OriginalInputEpoch m_epoch;
  std::shared_ptr<OriginalInputRoutes> m_routes;
  std::array<Slot, capacity> m_slots;
  struct Retired
  {
    std::uint64_t sequence = 0;
    std::uint16_t parts = 0;
    bool allowLate = false;
  };
  std::array<Retired, capacity> m_retired{};
  std::uint64_t m_captured = 0, m_next = 1, m_lastDecisionSequence = 0;
  std::uint16_t m_lastDecisionPart = 0;
};

} // namespace deskflow::windowshare
