/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

struct EventQueueTestAccess;

namespace deskflow {

// Production admission, independent of optional diagnostics. Explicit
// experimental launches install one budget before starting any producers.
// Failure is terminal: never resume a partially dropped input sequence.
class EventQueueBudget final
{
public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::uint64_t capacity = 4096;
  static constexpr std::uint64_t dispatchCapacity = 64;
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
  bool enterDispatch() noexcept
  {
    auto count = m_dispatching.load(std::memory_order_relaxed);
    for (unsigned attempt = 0; attempt != 16 && live(); ++attempt) {
      if (count >= dispatchCapacity) {
        fail(Failure::DispatchCapacity);
        return false;
      }
      if (m_dispatching.compare_exchange_strong(count, count + 1, std::memory_order_acq_rel)) {
        auto peak = m_dispatchPeak.load(std::memory_order_relaxed);
        while (peak < count + 1 &&
               !m_dispatchPeak.compare_exchange_strong(peak, count + 1, std::memory_order_relaxed)) {
        }
        if (live())
          return true;
        leaveDispatch();
        return false;
      }
    }
    fail(Failure::Contention);
    return false;
  }
  void leaveDispatch() noexcept
  {
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
  const std::function<void()> m_revoke;
  std::atomic<Failure> m_failure{Failure::Healthy};
  std::atomic<std::uint64_t> m_outstanding{0}, m_admitted{0}, m_retired{0}, m_peak{0};
  std::atomic<std::uint64_t> m_dispatching{0}, m_dispatchPeak{0};
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
    m_dispatching = m_owned && m_budget->enterDispatch();
    return m_dispatching;
  }

private:
  friend struct ::EventQueueTestAccess;
  void release() noexcept
  {
    if (std::exchange(m_owned, false)) {
      if (m_dispatching) {
        if (EventQueueBudget::Clock::now() - m_entered > EventQueueBudget::maximumAge)
          m_budget->fail(EventQueueBudget::Failure::Deadline);
        m_budget->leaveDispatch();
        m_dispatching = false;
      }
      m_budget->release();
    }
  }
  void take(EventQueueAdmission &other) noexcept
  {
    m_budget = std::move(other.m_budget);
    m_entered = other.m_entered;
    m_owned = std::exchange(other.m_owned, false);
    m_dispatching = std::exchange(other.m_dispatching, false);
  }
  std::shared_ptr<EventQueueBudget> m_budget;
  EventQueueBudget::Clock::time_point m_entered{};
  bool m_owned = false, m_dispatching = false;
};
} // namespace deskflow
