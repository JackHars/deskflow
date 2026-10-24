/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalInputRoute.h"
#include <atomic>
#include <chrono>
#include <optional>

namespace deskflow::windowshare {

// One capture producer and one coordinator owner. Only movement belonging to
// the exact retained left press may be withheld from local native dispatch.
// Every down/up/key/scroll still reaches its original native owner. The sole
// raw consumer receives all records, including withheld motion. No allocation,
// lock, native query, posting or second queue consumer occurs in observe().
// Arming is an internal operation AFTER native titlebar + remote preview
// admission; a press stamp by itself does not supply that admission.
class WindowDragMotionGate final
{
public:
  using Clock = std::chrono::steady_clock;
  static constexpr auto silence = std::chrono::milliseconds(350);
  explicit WindowDragMotionGate(std::uint64_t epoch) : m_epoch(epoch)
  {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<std::int64_t>::is_always_lock_free);
  }
  // Coordinator thread. One attempt per original press; cancellation/expiry
  // cannot be undone by a late ready or heartbeat for that press.
  bool arm(OriginalInputStamp press, Clock::time_point now = Clock::now()) noexcept
  {
    return armWithPrefix(press, now).has_value();
  }
  // The returned capture prefix can still contain queued native movement.
  // The producer publishes its sequence BEFORE deciding whether to suppress;
  // the request and sequence use one SC order so an unsuppressed decision made
  // before arming cannot fall beyond this prefix. This does not authorize a
  // button transition or excuse any later loss of suppression.
  std::optional<OriginalInputStamp>
  armWithPrefix(OriginalInputStamp press, Clock::time_point now = Clock::now()) noexcept
  {
    if (m_stopped.load(std::memory_order_acquire) || press.epoch != m_epoch || !press.sequence ||
        press.sequence <= m_lastAttempt)
      return {};
    m_lastAttempt = press.sequence;
    if (m_press.load(std::memory_order_acquire) != press.sequence)
      return {};
    m_deadline.store(ticks(now + silence), std::memory_order_release);
    m_requested.store(press.sequence, std::memory_order_seq_cst);
    const auto prefix = m_observingSequence.load(std::memory_order_seq_cst);
    if (!active(press, now))
      return {};
    return OriginalInputStamp{m_epoch, prefix, 0};
  }
  bool refresh(OriginalInputStamp press, Clock::time_point now = Clock::now()) noexcept
  {
    if (!active(press, now)) {
      cancel();
      return false;
    }
    m_deadline.store(ticks(now + silence), std::memory_order_release);
    return active(press, now);
  }
  bool active(OriginalInputStamp press, Clock::time_point now = Clock::now()) const noexcept
  {
    return !m_stopped.load(std::memory_order_acquire) && press.epoch == m_epoch && press.sequence &&
           m_requested.load(std::memory_order_seq_cst) == press.sequence &&
           m_press.load(std::memory_order_acquire) == press.sequence &&
           ticks(now) < m_deadline.load(std::memory_order_acquire);
  }
  void cancel() noexcept
  {
    m_requested.store(0, std::memory_order_seq_cst);
  }
  void stop() noexcept
  {
    m_stopped.store(true, std::memory_order_release);
    cancel();
  }
  std::uint64_t lastSuppressedSequence() const noexcept
  {
    return m_lastSuppressed.load(std::memory_order_acquire);
  }
  // Producer only; true authorizes withholding THIS captured movement, never
  // a native release. Unknown, additional or changed input cancels the hold.
  bool observe(const OriginalInputRecord &record, Clock::time_point now = Clock::now()) noexcept
  {
    if (m_stopped.load(std::memory_order_acquire) || record.epoch != m_epoch || !record.sequence ||
        record.sequence <= m_lastSequence) {
      cancel();
      m_press.store(0, std::memory_order_release);
      return false;
    }
    m_lastSequence = record.sequence;
    m_observingSequence.store(record.sequence, std::memory_order_seq_cst);
    if (const auto *button = std::get_if<OriginalInputButton>(&record.payload)) {
      cancel();
      const auto previous = m_press.exchange(0, std::memory_order_acq_rel);
      if (!previous && button->button == 0 && button->down && button->clickCount == 1 && !modified(record.flags))
        m_press.store(record.sequence, std::memory_order_release);
      return false;
    }
    const auto *motion = std::get_if<OriginalInputMotion>(&record.payload);
    if (!motion || motion->dragButton != 0 || modified(record.flags)) {
      cancel();
      m_press.store(0, std::memory_order_release);
      return false;
    }
    const auto press = m_press.load(std::memory_order_acquire);
    auto requested = m_requested.load(std::memory_order_seq_cst);
    if (!active({m_epoch, press, 0}, now)) {
      // Preserve an unarmed press for later preview admission. An expired
      // request is retired, while a later heartbeat cannot rearm it.
      if (requested && ticks(now) >= m_deadline.load(std::memory_order_acquire))
        m_requested.compare_exchange_strong(requested, 0, std::memory_order_seq_cst);
      return false;
    }
    m_lastSuppressed.store(record.sequence, std::memory_order_release);
    return true;
  }

private:
  static std::int64_t ticks(Clock::time_point point) noexcept
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(point.time_since_epoch()).count();
  }
  static bool modified(std::uint64_t flags) noexcept
  {
    return flags & (originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag |
                    originalKey::commandFlag | originalKey::functionFlag);
  }
  const std::uint64_t m_epoch;
  std::atomic_bool m_stopped{false};
  std::atomic<std::uint64_t> m_press{0}, m_requested{0}, m_lastSuppressed{0}, m_observingSequence{0};
  std::atomic<std::int64_t> m_deadline{0};
  std::uint64_t m_lastAttempt = 0;  // Coordinator only.
  std::uint64_t m_lastSequence = 0; // Producer only.
};
} // namespace deskflow::windowshare
