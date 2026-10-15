/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <utility>

struct EventQueueTestAccess;

namespace deskflow {

class EventQueueWatchdog;

// Production admission, independent of optional diagnostics. Explicit
// experimental launches install one budget before starting any producers.
// Failure is terminal: never resume a partially dropped input sequence.
class EventQueueBudget final
{
public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::uint64_t capacity = 4096;
  static constexpr std::uint64_t dispatchCapacity = 64;
  static constexpr auto noDispatch = dispatchCapacity;
  // Processing limit for one native libei batch, not a capacity claim about
  // libei's internal queues or the compositor/socket before it.
  static constexpr std::uint64_t nativeBatchCapacity = 4096;
  static constexpr auto maximumAge = std::chrono::seconds(2);
  enum class Failure : std::uint8_t
  {
    Healthy,
    Capacity,
    DispatchCapacity,
    Contention,
    Deadline,
    ForeignQueue,
    PlatformBuffer,
    NativeBatchCapacity,
    Accounting
  };
  struct Snapshot
  {
    std::uint64_t admitted, retired, outstanding, peak;
    Failure failure;
    std::uint64_t dispatching, dispatchPeak;
  };
  // Called at most once, on the failing producer/consumer thread. The callback
  // must only revoke thread-safe retained state; no UI, waiting or exceptions.
  explicit EventQueueBudget(std::function<void()> revoke = {}) : m_revoke(std::move(revoke))
  {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<Failure>::is_always_lock_free);
    static_assert(std::atomic<DispatchDeadline>::is_always_lock_free);
    for (auto &deadline : m_dispatchDeadlines)
      deadline.store(freeDispatch, std::memory_order_relaxed);
  }
  bool live() const noexcept
  {
    return m_failure.load(std::memory_order_acquire) == Failure::Healthy;
  }
  void fail(Failure reason) noexcept
  {
    auto expected = Failure::Healthy;
    if (reason != Failure::Healthy && m_failure.compare_exchange_strong(expected, reason, std::memory_order_acq_rel))
      if (m_revoke)
        m_revoke();
  }
  bool acquire() noexcept
  {
    auto depth = m_outstanding.load(std::memory_order_relaxed);
    for (unsigned attempt = 0; attempt != 16 && live(); ++attempt) {
      if (depth >= capacity) {
        fail(Failure::Capacity);
        return false;
      }
      if (m_outstanding.compare_exchange_strong(depth, depth + 1, std::memory_order_acq_rel)) {
        ++m_admitted;
        auto peak = m_peak.load(std::memory_order_relaxed);
        while (peak < depth + 1 && !m_peak.compare_exchange_strong(peak, depth + 1, std::memory_order_relaxed)) {
        }
        if (live())
          return true;
        release();
        return false;
      }
    }
    fail(Failure::Contention);
    return false;
  }
  void release() noexcept
  {
    const auto before = m_outstanding.fetch_sub(1, std::memory_order_acq_rel);
    if (!before)
      fail(Failure::Accounting);
    ++m_retired;
  }
  std::uint64_t enterDispatch(Clock::time_point entered) noexcept
  {
    auto count = m_dispatching.load(std::memory_order_relaxed);
    for (unsigned attempt = 0; attempt != 16 && live(); ++attempt) {
      if (count >= dispatchCapacity) {
        fail(Failure::DispatchCapacity);
        return noDispatch;
      }
      if (m_dispatching.compare_exchange_strong(count, count + 1, std::memory_order_acq_rel)) {
        auto peak = m_dispatchPeak.load(std::memory_order_relaxed);
        while (peak < count + 1 &&
               !m_dispatchPeak.compare_exchange_strong(peak, count + 1, std::memory_order_relaxed)) {
        }
        // Slots cover nested and concurrent callbacks independently. Publish
        // the original admission deadline, never a new lease at dispatch.
        const auto deadline = (entered + maximumAge).time_since_epoch().count();
        for (std::uint64_t slot = 0; slot != dispatchCapacity && live(); ++slot) {
          auto expected = freeDispatch;
          if (m_dispatchDeadlines[slot].compare_exchange_strong(expected, deadline, std::memory_order_acq_rel)) {
            if (live())
              return slot;
            leaveDispatch(slot);
            return noDispatch;
          }
        }
        --m_dispatching;
        fail(Failure::Contention);
        return noDispatch;
      }
    }
    fail(Failure::Contention);
    return noDispatch;
  }
  void leaveDispatch(std::uint64_t slot) noexcept
  {
    if (slot >= dispatchCapacity ||
        m_dispatchDeadlines[slot].exchange(freeDispatch, std::memory_order_acq_rel) == freeDispatch)
      fail(Failure::Accounting);
    if (!m_dispatching.fetch_sub(1, std::memory_order_acq_rel))
      fail(Failure::Accounting);
  }
  // Diagnostic counters are exact once producers/consumers are quiescent.
  Snapshot snapshot() const noexcept
  {
    return {m_admitted.load(), m_retired.load(),     m_outstanding.load(), m_peak.load(),
            m_failure.load(),  m_dispatching.load(), m_dispatchPeak.load()};
  }

private:
  friend class EventQueueWatchdog;
  friend struct ::EventQueueTestAccess;
  using DispatchDeadline = Clock::duration::rep;
  // Reserve the clock's extreme values, unreachable during a process lifetime.
  static constexpr auto freeDispatch = std::numeric_limits<DispatchDeadline>::max();
  static constexpr auto expiredDispatch = std::numeric_limits<DispatchDeadline>::min();
  bool claimExpiredDispatch(Clock::time_point now) noexcept
  {
    for (auto &slot : m_dispatchDeadlines) {
      auto deadline = slot.load(std::memory_order_acquire);
      // Claim the exact still-active deadline. A stale observation of a slot
      // that retired or acquired a later deadline cannot terminate the process.
      // Reuse with the same deadline is also overdue, so needs no generation.
      if (deadline == expiredDispatch ||
          (deadline != freeDispatch && deadline < now.time_since_epoch().count() &&
           slot.compare_exchange_strong(deadline, expiredDispatch, std::memory_order_acq_rel)))
        return true;
    }
    return false;
  }
  const std::function<void()> m_revoke;
  std::atomic<Failure> m_failure{Failure::Healthy};
  std::atomic<std::uint64_t> m_outstanding{0}, m_admitted{0}, m_retired{0}, m_peak{0};
  std::atomic<std::uint64_t> m_dispatching{0}, m_dispatchPeak{0};
  std::array<std::atomic<DispatchDeadline>, dispatchCapacity> m_dispatchDeadlines{};
};

// Owns one budget slot across pending, saved, retrieved and callback storage.
// A retained shared state outlives a handler that destroys its queue or Event.
class EventQueueAdmission final
{
public:
  EventQueueAdmission() = default;
  EventQueueAdmission(const EventQueueAdmission &) = delete;
  EventQueueAdmission &operator=(const EventQueueAdmission &) = delete;
  EventQueueAdmission(EventQueueAdmission &&other) noexcept
  {
    take(other);
  }
  EventQueueAdmission &operator=(EventQueueAdmission &&other) noexcept
  {
    if (this != &other) {
      release();
      take(other);
    }
    return *this;
  }
  ~EventQueueAdmission()
  {
    release();
  }
  bool admit(const std::shared_ptr<EventQueueBudget> &budget) noexcept
  {
    if (m_budget && m_budget != budget) {
      m_budget->fail(EventQueueBudget::Failure::ForeignQueue);
      if (budget)
        budget->fail(EventQueueBudget::Failure::ForeignQueue);
      return false;
    }
    if (!budget)
      return true;
    if (!m_budget) {
      m_budget = budget;
      m_entered = EventQueueBudget::Clock::now();
    }
    if (EventQueueBudget::Clock::now() - m_entered > EventQueueBudget::maximumAge) {
      budget->fail(EventQueueBudget::Failure::Deadline);
      return false;
    }
    if (m_owned)
      return budget->live();
    m_owned = budget->acquire();
    return m_owned;
  }
  EventQueueAdmission takeForDispatch() noexcept
  {
    EventQueueAdmission scope;
    scope.m_budget = m_budget;
    scope.m_entered = m_entered;
    scope.m_owned = std::exchange(m_owned, false);
    return scope;
  }
  bool beginDispatch() noexcept
  {
    if (!m_budget)
      return true;
    if (!m_owned)
      return false;
    m_dispatchSlot = m_budget->enterDispatch(m_entered);
    return m_dispatchSlot != EventQueueBudget::noDispatch;
  }

private:
  friend struct ::EventQueueTestAccess;
  void release() noexcept
  {
    if (std::exchange(m_owned, false)) {
      if (m_dispatchSlot != EventQueueBudget::noDispatch) {
        if (EventQueueBudget::Clock::now() - m_entered > EventQueueBudget::maximumAge)
          m_budget->fail(EventQueueBudget::Failure::Deadline);
        m_budget->leaveDispatch(std::exchange(m_dispatchSlot, EventQueueBudget::noDispatch));
      }
      m_budget->release();
    }
  }
  void take(EventQueueAdmission &other) noexcept
  {
    m_budget = std::move(other.m_budget);
    m_entered = other.m_entered;
    m_owned = std::exchange(other.m_owned, false);
    m_dispatchSlot = std::exchange(other.m_dispatchSlot, EventQueueBudget::noDispatch);
  }
  std::shared_ptr<EventQueueBudget> m_budget;
  EventQueueBudget::Clock::time_point m_entered{};
  bool m_owned = false;
  std::uint64_t m_dispatchSlot = EventQueueBudget::noDispatch;
};
} // namespace deskflow
