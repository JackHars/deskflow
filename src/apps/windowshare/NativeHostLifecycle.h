/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace deskflow::windowshare {

// Notifications may arrive before the host's queued restore can run. Retire
// admission synchronously, with no native/Qt calls, then complete restoration
// on the owner thread. This is lifecycle exclusion, not an input queue drain.
class NativeHostLifecycle final
{
public:
  enum class Event
  {
    Sleep,
    Wake,
    Deactivate,
    Activate,
    Emergency,
    Shutdown
  };
  struct Admission
  {
    std::uint64_t value = 0;
    explicit operator bool() const noexcept
    {
      return value != 0;
    }
  };
  struct Cleanup
  {
    std::uint64_t value = 0;
    explicit operator bool() const noexcept
    {
      return value != 0;
    }
    bool operator==(const Cleanup &) const = default;
  };

  Admission admission() const noexcept
  {
    const auto value = m_state.load(std::memory_order_acquire);
    return {(value & flags) ? 0 : value};
  }
  bool current(Admission admission) const noexcept
  {
    return admission && !(admission.value & flags) && m_state.load(std::memory_order_acquire) == admission.value;
  }
  void signal(Event event) noexcept
  {
    auto before = m_state.load(std::memory_order_acquire);
    for (;;) {
      auto after = before | pending;
      switch (event) {
      case Event::Sleep:
        after |= sleeping;
        break;
      case Event::Wake:
        after &= ~sleeping;
        break;
      case Event::Deactivate:
        after |= inactive;
        break;
      case Event::Activate:
        after &= ~inactive;
        break;
      case Event::Emergency:
        after |= emergency;
        break;
      case Event::Shutdown:
      default:
        after |= shutdown;
        break;
      }
      // Generation exhaustion permanently closes admission instead of making
      // an old callback current. Emergency and destruction are also terminal
      // for this NativeHost instance; only explicit new host startup re-arms.
      if ((before & ~flags) == (std::numeric_limits<std::uint64_t>::max() & ~flags))
        after |= shutdown;
      else
        after += generation;
      if (m_state.compare_exchange_weak(before, after, std::memory_order_acq_rel, std::memory_order_acquire))
        return;
    }
  }
  Cleanup cleanup() const noexcept
  {
    const auto value = m_state.load(std::memory_order_acquire);
    return {(value & pending) ? value : 0};
  }
  // The caller must have finished native restoration and its durable journal
  // update. A later notification always makes this completion stale, even if
  // it represents a wake/activation which removed the last blocking reason.
  bool complete(Cleanup cleanup) noexcept
  {
    if (!cleanup || !(cleanup.value & pending))
      return false;
    auto expected = cleanup.value;
    return m_state.compare_exchange_strong(
        expected, cleanup.value & ~pending, std::memory_order_acq_rel, std::memory_order_acquire
    );
  }
  // Coalesce work while a restore task is queued or executing. The owner
  // releases this claim before checking for a newer cleanup ticket, so a
  // notification racing that check cannot be lost or create an unbounded queue.
  bool claimQueue() noexcept
  {
    bool expected = false;
    return m_queued.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
  }
  void releaseQueue() noexcept
  {
    m_queued.store(false, std::memory_order_release);
  }

private:
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
  static constexpr std::uint64_t sleeping = 1, inactive = 2, emergency = 4, shutdown = 8, pending = 16;
  static constexpr std::uint64_t flags = 31, generation = flags + 1;
  std::atomic<std::uint64_t> m_state{generation};
  std::atomic<bool> m_queued{false};
};

} // namespace deskflow::windowshare
