/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalInput.h"
#include "base/EventInputObserver.h"
#include "base/EventQueueBudget.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace deskflow::windowshare {

// Optional diagnostics, never admission or delivery authority. Storage is
// installed before producers start and retained for the process lifetime.
enum class InputEvidenceRole : std::uint8_t
{
  Core,
  Host,
  Native
};
enum class InputEvidenceQueue : std::uint8_t
{
  Capture,
  Translation,
  Completed,
  ServerPending,
  ServerCompleted,
  ServerExecuting,
  CoordinatorOperations,
  CoordinatorBatch,
  CoordinatorPreview,
  EventOutstanding,
  EventDispatch,
  EventAllOutstanding,
  EventAllDispatch,
  PrivateOutput,
  PrivateBufferedInput,
  PrivateIncoming,
  PrivateDispatch,
  Count
};
inline constexpr bool eventInputEvidenceQueue(InputEvidenceQueue queue) noexcept
{
  return queue == InputEvidenceQueue::EventOutstanding || queue == InputEvidenceQueue::EventDispatch ||
         queue == InputEvidenceQueue::EventAllOutstanding || queue == InputEvidenceQueue::EventAllDispatch;
}
inline constexpr bool nativeSpscInputEvidenceQueue(InputEvidenceQueue queue) noexcept
{
  return queue == InputEvidenceQueue::Capture || queue == InputEvidenceQueue::Translation ||
         queue == InputEvidenceQueue::Completed;
}
// Native capture queues and Core Event queues legitimately publish concurrently.
// Use atomic cumulative counters and ordered journal publication for both.
inline constexpr bool concurrentInputEvidenceQueue(InputEvidenceQueue queue) noexcept
{
  return nativeSpscInputEvidenceQueue(queue) || eventInputEvidenceQueue(queue);
}
enum class InputEvidenceLoss : std::uint8_t
{
  None,
  Contention,
  BufferFull,
  QueueIdentity,
  QueueAccounting,
  Baseline,
  OwnerChanged,
  CaptureChanged,
  PipelineRevoked,
  Deadline,
  Output,
  Malformed
};
enum class InputEvidenceState : std::uint8_t
{
  Waiting,
  Recording,
  Draining,
  Stopped,
  Denied
};
enum class InputEvidenceFailureSite : std::uint8_t
{
  Unspecified,
  OwnerBind,
  OwnerRevoke,
  QueueInitialize,
  QueueMutation,
  JournalReserve,
  PrivateOutputWritten,
  PrivateBufferObserved,
  PrivateBufferRead
};
enum class InputEvidenceKind : std::uint8_t
{
  Source,
  Native,
  Queue,
  Hover,
  EventQueue // Internal only; pop restores Queue in exact counter-update order.
};
inline std::uint64_t inputEvidenceNow() noexcept
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Exact scalar lease plus both incarnations and pins. The Native fixture uses
// only process/window identity, and cannot assert that a private lease exists.
struct InputEvidenceOwner
{
  std::array<std::uint64_t, 11> values{};
  std::array<std::uint8_t, 96> identity{};
  bool operator==(const InputEvidenceOwner &) const = default;
};
// Diagnostic samples from the existing readiness check, never arm authority.
struct InputEvidenceReadiness
{
  bool inspected = false, ownerLive = false, captureIdle = false;
  // Per queue: uninitialized, mutation busy, activity in flight, nonempty.
  std::array<std::uint8_t, static_cast<std::size_t>(InputEvidenceQueue::Count)> queues{};
};
struct InputEvidenceRaw
{
  std::uint64_t timestamp = 0, flags = 0;
  // CG type, key code/type/repeat, delta x/y, button/click count,
  // horizontal/vertical lines, points, fixed16.16, phase/momentum/continuous.
  std::array<std::int64_t, 17> fields{};
  bool operator==(const InputEvidenceRaw &) const = default;
};
inline std::optional<InputEvidenceRaw> inputEvidenceRaw(const OriginalInputRecord &record) noexcept
{
  InputEvidenceRaw result{record.sourceTimestampNs, record.flags};
  auto &f = result.fields;
  if (const auto *key = std::get_if<OriginalInputKey>(&record.payload)) {
    f[0] = key->kind == OriginalKeyKind::FlagsChanged ? 12 : key->kind == OriginalKeyKind::Down ? 10 : 11;
    f[1] = key->code;
    f[2] = key->keyboardType;
    f[3] = key->repeat;
  } else if (const auto *motion = std::get_if<OriginalInputMotion>(&record.payload)) {
    f[0] = motion->dragButton == 32 ? 5 : motion->dragButton == 0 ? 6 : motion->dragButton == 1 ? 7 : 27;
    f[4] = motion->dx;
    f[5] = motion->dy;
    f[6] = motion->dragButton == 32 ? 0 : motion->dragButton;
  } else if (const auto *button = std::get_if<OriginalInputButton>(&record.payload)) {
    f[0] = button->button == 0   ? (button->down ? 1 : 2)
           : button->button == 1 ? (button->down ? 3 : 4)
                                 : (button->down ? 25 : 26);
    f[6] = button->button;
    f[7] = button->clickCount;
  } else if (const auto *scroll = std::get_if<OriginalInputScroll>(&record.payload)) {
    f[0] = 22;
    for (std::size_t i = 0; i != 2; ++i) {
      f[8 + i] = scroll->lines[i];
      f[10 + i] = scroll->points[i];
      f[12 + i] = scroll->fixed16_16[i];
    }
    f[14] = static_cast<int>(scroll->phase);
    f[15] = static_cast<int>(scroll->momentum);
    f[16] = scroll->continuous;
  } else
    return {};
  return result;
}
struct InputEvidenceRow
{
  InputEvidenceKind kind = InputEvidenceKind::Queue;
  InputEvidenceQueue queue = InputEvidenceQueue::Capture;
  std::uint64_t time = 0, epoch = 0, sequence = 0, otherSequence = 0;
  std::uint64_t enqueued = 0, retired = 0, depth = 0, capacity = 0;
  InputEvidenceRaw raw;
};

class InputEvidenceRecorder final
{
public:
  static constexpr std::size_t capacity = 8192;
  static constexpr std::size_t queueCount = static_cast<std::size_t>(InputEvidenceQueue::Count);
  explicit InputEvidenceRecorder(InputEvidenceRole role)
      : m_role(role),
        m_eventObserver{this, initializeEventQueue, observeEvent}
  {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<InputEvidenceRecorder *>::is_always_lock_free);
  }
  InputEvidenceRecorder(const InputEvidenceRecorder &) = delete;
  const deskflow::EventInputObserver &eventObserver() const noexcept
  {
    return m_eventObserver;
  }
  InputEvidenceState state() const noexcept
  {
    return m_state.load(std::memory_order_acquire);
  }
  InputEvidenceLoss loss() const noexcept
  {
    return static_cast<InputEvidenceLoss>(m_failure.load(std::memory_order_acquire) & 0xff);
  }
  InputEvidenceFailureSite failureSite() const noexcept
  {
    return static_cast<InputEvidenceFailureSite>((m_failure.load(std::memory_order_acquire) >> 8) & 0xff);
  }
  InputEvidenceQueue failureQueue() const noexcept
  {
    return static_cast<InputEvidenceQueue>((m_failure.load(std::memory_order_acquire) >> 16) & 0xff);
  }
  InputEvidenceRole role() const noexcept
  {
    return m_role;
  }
  void fail(
      InputEvidenceLoss reason, InputEvidenceFailureSite site = InputEvidenceFailureSite::Unspecified,
      InputEvidenceQueue queue = InputEvidenceQueue::Count
  ) noexcept
  {
    if ((state() == InputEvidenceState::Stopped || state() == InputEvidenceState::Denied) &&
        reason != InputEvidenceLoss::Output)
      return;
    if (reason == InputEvidenceLoss::None)
      return;
    // Publish the first reason and its scalar context together. Producers do
    // no formatting, allocation or logging, and a later failure cannot replace
    // the event that actually invalidated this diagnostic run.
    std::uint32_t expected = 0;
    const auto failure = static_cast<std::uint32_t>(reason) | (static_cast<std::uint32_t>(site) << 8) |
                         (static_cast<std::uint32_t>(queue) << 16);
    m_failure.compare_exchange_strong(expected, failure, std::memory_order_acq_rel);
  }
  bool recording() noexcept
  {
    if (state() != InputEvidenceState::Recording)
      return false;
    if (inputEvidenceNow() > m_deadline.load(std::memory_order_acquire)) {
      stop();
      return false;
    }
    return loss() == InputEvidenceLoss::None;
  }
  void bindOwner(const InputEvidenceOwner &owner) noexcept
  {
    if (m_ownerBusy.test_and_set(std::memory_order_acquire)) {
      fail(InputEvidenceLoss::Contention, InputEvidenceFailureSite::OwnerBind);
      return;
    }
    if ((state() == InputEvidenceState::Recording || state() == InputEvidenceState::Draining) &&
        (!m_ownerLive || m_owner != owner))
      fail(InputEvidenceLoss::OwnerChanged);
    m_owner = owner;
    m_ownerLive =
        m_role == InputEvidenceRole::Native
            ? owner.values[0] && owner.values[1]
            : std::all_of(owner.values.begin(), owner.values.end(), [](auto value) { return value != 0; }) &&
                  std::any_of(owner.identity.begin(), owner.identity.end(), [](auto value) { return value != 0; });
    m_captureEpoch.store(m_role == InputEvidenceRole::Native ? 0 : owner.values[4], std::memory_order_release);
    m_ownerBusy.clear(std::memory_order_release);
  }
  void revokeOwner() noexcept
  {
    if (state() == InputEvidenceState::Recording || state() == InputEvidenceState::Draining)
      fail(InputEvidenceLoss::OwnerChanged);
    if (m_ownerBusy.test_and_set(std::memory_order_acquire)) {
      fail(InputEvidenceLoss::Contention, InputEvidenceFailureSite::OwnerRevoke);
      return;
    }
    m_ownerLive = false;
    m_ownerBusy.clear(std::memory_order_release);
  }
  // Collector thread only; no producer waits for these try-locks. A producer
  // racing arm invalidates the run rather than hiding an intermediate queue.
  bool arm(std::uint64_t durationNs) noexcept
  {
    if (state() != InputEvidenceState::Waiting)
      return false;
    if (!durationNs || durationNs > 180000000000ULL || m_ownerBusy.test_and_set(std::memory_order_acquire)) {
      fail(InputEvidenceLoss::Baseline);
      m_state.store(InputEvidenceState::Denied);
      return false;
    }
    const bool captureHeld = lockCaptureBoundary();
    bool okay = captureHeld && m_ownerLive && loss() == InputEvidenceLoss::None;
    std::array<bool, queueCount> held{}, activityHeld{};
    for (std::size_t i = 0; i != queueCount; ++i) {
      if (!expectedQueue(static_cast<InputEvidenceQueue>(i)))
        continue;
      auto &q = m_queues[i];
      std::uint64_t idle = 0;
      activityHeld[i] = q.activity.compare_exchange_strong(idle, captureBoundary, std::memory_order_acq_rel);
      held[i] = !q.busy.test_and_set(std::memory_order_acquire);
      okay = okay && activityHeld[i] && held[i] && q.initialized && q.depth == 0;
    }
    if (okay) {
      m_start = inputEvidenceNow();
      m_deadline.store(m_start + durationNs);
      m_state.store(InputEvidenceState::Recording, std::memory_order_release);
      for (std::size_t i = 0; i != queueCount; ++i)
        if (held[i]) {
          auto &q = m_queues[i];
          q.enqueued = q.retired = 0;
          q.eventCounts.store(0, std::memory_order_relaxed);
          append(
              {InputEvidenceKind::Queue, static_cast<InputEvidenceQueue>(i), m_start, 0, 0, 0, 0, 0, 0, q.capacity, {}}
          );
        }
    }
    if (captureHeld)
      okay = unlockCaptureBoundary() && okay;
    okay = okay && loss() == InputEvidenceLoss::None;
    if (!okay) {
      fail(InputEvidenceLoss::Baseline);
      m_state.store(InputEvidenceState::Denied);
    }
    for (std::size_t i = 0; i != queueCount; ++i)
      if (held[i]) {
        m_queues[i].busy.clear(std::memory_order_release);
      }
    for (std::size_t i = 0; i != queueCount; ++i)
      if (activityHeld[i])
        m_queues[i].activity.fetch_and(~captureBoundary, std::memory_order_acq_rel);
    m_ownerBusy.clear(std::memory_order_release);
    return okay && loss() == InputEvidenceLoss::None;
  }
  void stop() noexcept
  {
    // Producers may discover the deadline. Publish its boundary before the
    // Draining state; observers must never see a zero/unfinished stop boundary.
    // Repeated stop requests need no wait and cannot extend this one-shot run.
    if (m_stopBusy.test_and_set(std::memory_order_acquire))
      return;
    if (state() == InputEvidenceState::Recording) {
      m_sourceEnd.store(std::min(inputEvidenceNow(), m_deadline.load()), std::memory_order_release);
      m_state.store(InputEvidenceState::Draining, std::memory_order_release);
    } else if (state() == InputEvidenceState::Waiting)
      m_state.store(InputEvidenceState::Denied);
    m_stopBusy.clear(std::memory_order_release);
  }
  // Collector only. Record an explicit source boundary, then retain queue
  // mutations and native receipts for at most 250 ms. Queue emptiness is a
  // declared local-queue observation, never an OS/compositor drain assertion.
  void finish(bool force = false) noexcept
  {
    if (state() != InputEvidenceState::Draining)
      return;
    const auto now = inputEvidenceNow();
    const bool expired = now >= m_sourceEnd.load(std::memory_order_acquire) + 250000000;
    if (m_role == InputEvidenceRole::Native && !expired && !force)
      return;
    std::array<bool, queueCount> held{}, activityHeld{};
    bool captureHeld = lockCaptureBoundary();
    bool empty = captureHeld;
    for (std::size_t i = 0; i != queueCount; ++i)
      if (expectedQueue(static_cast<InputEvidenceQueue>(i))) {
        auto &q = m_queues[i];
        std::uint64_t idle = 0;
        activityHeld[i] = q.activity.compare_exchange_strong(idle, captureBoundary, std::memory_order_acq_rel);
        held[i] = !q.busy.test_and_set(std::memory_order_acquire);
        empty = empty && activityHeld[i] && held[i] && q.initialized && q.depth == 0;
      }
    if (empty || expired || force) {
      if (captureHeld) {
        empty = unlockCaptureBoundary() && empty;
        captureHeld = false;
      }
      if (!empty)
        fail(InputEvidenceLoss::Deadline);
      m_stop.store(now, std::memory_order_release);
      for (std::size_t i = 0; i != queueCount; ++i)
        if (held[i]) {
          auto &q = m_queues[i];
          const auto counts = q.eventCounts.load(std::memory_order_acquire);
          const auto added =
              concurrentInputEvidenceQueue(static_cast<InputEvidenceQueue>(i)) ? counts >> 32 : q.enqueued;
          const auto retired =
              concurrentInputEvidenceQueue(static_cast<InputEvidenceQueue>(i)) ? counts & UINT32_MAX : q.retired;
          append(
              {InputEvidenceKind::Queue,
               static_cast<InputEvidenceQueue>(i),
               now,
               0,
               0,
               0,
               added,
               retired,
               q.depth.load(),
               q.capacity,
               {}}
          );
        }
      m_state.store(InputEvidenceState::Stopped, std::memory_order_release);
    }
    if (captureHeld)
      (void)unlockCaptureBoundary();
    for (std::size_t i = 0; i != queueCount; ++i)
      if (held[i])
        m_queues[i].busy.clear(std::memory_order_release);
    for (std::size_t i = 0; i != queueCount; ++i)
      if (activityHeld[i])
        m_queues[i].activity.fetch_and(~captureBoundary, std::memory_order_acq_rel);
  }
  std::uint64_t sourceEnd() const noexcept
  {
    return m_sourceEnd.load(std::memory_order_acquire);
  }
  void beginCaptureActivity() noexcept
  {
    // One atomic word makes source entry and the measured-start boundary
    // mutually observable. Never wait: overlap invalidates diagnostics only.
    if (m_captureGate.fetch_add(1, std::memory_order_acq_rel) & captureBoundary)
      fail(InputEvidenceLoss::Baseline);
  }
  void endCaptureActivity() noexcept
  {
    m_captureGate.fetch_sub(1, std::memory_order_acq_rel);
  }
  std::uint64_t stoppedAt() const noexcept
  {
    return m_stop.load(std::memory_order_acquire);
  }
  InputEvidenceOwner ownerForRuntime() noexcept
  {
    if (m_ownerBusy.test_and_set(std::memory_order_acquire))
      return {};
    auto result = m_owner;
    m_ownerBusy.clear(std::memory_order_release);
    return result;
  }
  std::optional<InputEvidenceOwner> readyOwner(InputEvidenceReadiness *diagnostic = nullptr) noexcept
  {
    if (diagnostic)
      *diagnostic = {};
    if (state() != InputEvidenceState::Waiting || loss() != InputEvidenceLoss::None ||
        m_ownerBusy.test_and_set(std::memory_order_acquire))
      return {};
    const bool captureIdle = m_captureGate.load(std::memory_order_acquire) == 0;
    bool ready = m_ownerLive && captureIdle;
    if (diagnostic) {
      diagnostic->inspected = true;
      diagnostic->ownerLive = m_ownerLive;
      diagnostic->captureIdle = captureIdle;
    }
    for (std::size_t i = 0; i != queueCount; ++i)
      if (expectedQueue(static_cast<InputEvidenceQueue>(i))) {
        const auto &q = m_queues[i];
        const std::uint8_t blocked = (!q.initialized.load(std::memory_order_acquire) ? 1 : 0) |
                                     (q.busy.test(std::memory_order_acquire) ? 2 : 0) |
                                     (q.activity.load(std::memory_order_acquire) ? 4 : 0) |
                                     (q.depth.load(std::memory_order_acquire) ? 8 : 0);
        ready = ready && !blocked;
        if (diagnostic)
          diagnostic->queues[i] = blocked;
      }
    auto result = ready ? std::optional(m_owner) : std::nullopt;
    m_ownerBusy.clear(std::memory_order_release);
    return result;
  }
  std::uint64_t start() const noexcept
  {
    return m_start;
  }
  bool expectedQueue(InputEvidenceQueue queue) const noexcept
  {
    if (static_cast<std::size_t>(queue) >= queueCount)
      return false;
    if (m_role == InputEvidenceRole::Native)
      return false;
    return m_role == InputEvidenceRole::Core || queue == InputEvidenceQueue::PrivateOutput ||
           queue == InputEvidenceQueue::PrivateBufferedInput || queue == InputEvidenceQueue::PrivateIncoming ||
           queue == InputEvidenceQueue::PrivateDispatch;
  }
  void initialize(InputEvidenceQueue queue, const void *identity, std::uint64_t limit) noexcept
  {
    if (!expectedQueue(queue)) {
      fail(InputEvidenceLoss::QueueIdentity);
      return;
    }
    auto &q = m_queues[static_cast<std::size_t>(queue)];
    if (q.busy.test_and_set(std::memory_order_acquire)) {
      fail(InputEvidenceLoss::Contention, InputEvidenceFailureSite::QueueInitialize, queue);
      return;
    }
    const auto actualEventLimit =
        queue == InputEvidenceQueue::EventDispatch || queue == InputEvidenceQueue::EventAllDispatch
            ? deskflow::EventQueueBudget::dispatchCapacity
            : deskflow::EventQueueBudget::capacity;
    if (q.initialized || !identity ||
        (eventInputEvidenceQueue(queue) ? limit != 0 && limit != actualEventLimit : limit == 0))
      fail(InputEvidenceLoss::QueueIdentity);
    else {
      q.identity = identity;
      q.capacity = limit;
      q.initialized = true;
    }
    q.busy.clear(std::memory_order_release);
  }
  bool beginMutation(InputEvidenceQueue queue, const void *identity) noexcept
  {
    if (!expectedQueue(queue)) {
      fail(InputEvidenceLoss::QueueIdentity);
      return false;
    }
    auto &q = m_queues[static_cast<std::size_t>(queue)];
    if (nativeSpscInputEvidenceQueue(queue)) {
      if (!q.initialized.load(std::memory_order_acquire) || q.identity != identity) {
        fail(InputEvidenceLoss::QueueIdentity, InputEvidenceFailureSite::QueueMutation, queue);
        return false;
      }
      // The caller's activity guard excludes arm/finish. A capture producer
      // must never wait for, or reject, its legitimately concurrent consumer.
      return true;
    }
    if (q.busy.test_and_set(std::memory_order_acquire)) {
      fail(InputEvidenceLoss::Contention, InputEvidenceFailureSite::QueueMutation, queue);
      return false;
    }
    if (!q.initialized || q.identity != identity) {
      fail(InputEvidenceLoss::QueueIdentity);
      q.busy.clear(std::memory_order_release);
      return false;
    }
    return true;
  }
  bool beginQueueActivity(InputEvidenceQueue queue, const void *identity) noexcept
  {
    if (!expectedQueue(queue)) {
      fail(InputEvidenceLoss::QueueIdentity);
      return false;
    }
    auto &q = m_queues[static_cast<std::size_t>(queue)];
    if (q.activity.fetch_add(1, std::memory_order_acq_rel) & captureBoundary) {
      fail(InputEvidenceLoss::Baseline, InputEvidenceFailureSite::QueueMutation, queue);
      q.activity.fetch_sub(1, std::memory_order_acq_rel);
      return false;
    }
    if (!q.initialized.load(std::memory_order_acquire) || q.identity != identity) {
      fail(InputEvidenceLoss::QueueIdentity, InputEvidenceFailureSite::QueueMutation, queue);
      q.activity.fetch_sub(1, std::memory_order_acq_rel);
      return false;
    }
    return true;
  }
  void endQueueActivity(InputEvidenceQueue queue) noexcept
  {
    m_queues[static_cast<std::size_t>(queue)].activity.fetch_sub(1, std::memory_order_acq_rel);
  }
  void endMutation(InputEvidenceQueue queue, std::uint64_t added, std::uint64_t removed) noexcept
  {
    if (!expectedQueue(queue)) {
      fail(InputEvidenceLoss::QueueIdentity);
      return;
    }
    if (nativeSpscInputEvidenceQueue(queue)) {
      if ((added && removed) || added > 1 || removed > 1) {
        fail(InputEvidenceLoss::QueueAccounting, InputEvidenceFailureSite::QueueMutation, queue);
        return;
      }
      if (added || removed)
        commitConcurrentMutation(queue, added != 0);
      return;
    }
    auto &q = m_queues[static_cast<std::size_t>(queue)];
    const auto depth = q.depth.load(std::memory_order_relaxed);
    if ((added && removed) || removed > depth || added > UINT64_MAX - depth ||
        (q.capacity && added > q.capacity - depth) || added > UINT64_MAX - q.enqueued ||
        removed > UINT64_MAX - q.retired)
      fail(InputEvidenceLoss::QueueAccounting);
    else {
      q.depth += added;
      q.depth -= removed;
      q.enqueued += added;
      q.retired += removed;
      if (recording() || (state() == InputEvidenceState::Draining && loss() == InputEvidenceLoss::None))
        append(
            {InputEvidenceKind::Queue,
             queue,
             inputEvidenceNow(),
             0,
             0,
             0,
             q.enqueued,
             q.retired,
             q.depth,
             q.capacity,
             {}}
        );
    }
    q.busy.clear(std::memory_order_release);
  }
  void source(const OriginalInputRecord &record) noexcept
  {
    if (!recording())
      return;
    if (record.epoch != m_captureEpoch.load(std::memory_order_acquire)) {
      fail(InputEvidenceLoss::CaptureChanged);
      return;
    }
    if (!validOriginalInputRecord(record) || (m_sourceSequence && record.sequence != m_sourceSequence + 1) ||
        record.sourceTimestampNs < m_sourceTimestamp) {
      fail(InputEvidenceLoss::Malformed);
      return;
    }
    m_sourceSequence = record.sequence;
    m_sourceTimestamp = record.sourceTimestampNs;
    const auto raw = inputEvidenceRaw(record);
    if (!raw) {
      fail(InputEvidenceLoss::Malformed);
      return;
    }
    InputEvidenceRow row;
    row.kind = InputEvidenceKind::Source;
    row.time = inputEvidenceNow();
    row.epoch = record.epoch;
    row.sequence = record.sequence;
    row.raw = *raw;
    if (const auto *key = std::get_if<OriginalInputKey>(&record.payload))
      row.otherSequence = key->repeat                                      ? 2
                          : key->transition == OriginalKeyTransition::Down ? 0
                          : key->transition == OriginalKeyTransition::Up   ? 1
                                                                           : 8;
    else if (const auto *button = std::get_if<OriginalInputButton>(&record.payload))
      row.otherSequence = button->down ? 3 : 4;
    else if (const auto *motion = std::get_if<OriginalInputMotion>(&record.payload))
      row.otherSequence = motion->dragButton == 32 ? 7 : 5;
    else
      row.otherSequence = 6;
    append(row);
  }
  void native(const InputEvidenceRaw &raw, std::uint64_t window, std::uint64_t sourcePid, std::uint64_t tag) noexcept
  {
    // The collector may now arm/finish independently of the AppKit thread.
    // Include the entire admission-to-publication interval in its boundary;
    // reserving a journal slot alone misses a paused observer before append().
    beginCaptureActivity();
    struct Observation
    {
      InputEvidenceRecorder &recorder;
      ~Observation()
      {
        recorder.endCaptureActivity();
      }
    } observation{*this};
    if (!recording() && !(state() == InputEvidenceState::Draining && loss() == InputEvidenceLoss::None &&
                          inputEvidenceNow() <= m_sourceEnd.load(std::memory_order_acquire) + 250000000))
      return;
    InputEvidenceRow row;
    row.kind = InputEvidenceKind::Native;
    row.time = inputEvidenceNow();
    row.sequence = window;
    row.epoch = sourcePid;
    row.otherSequence = tag;
    row.raw = raw;
    append(row);
  }
  void hover(std::uint64_t epoch, std::uint64_t removed, std::uint64_t retained) noexcept
  {
    if (!recording() && !(state() == InputEvidenceState::Draining && loss() == InputEvidenceLoss::None))
      return;
    if (epoch != m_captureEpoch.load(std::memory_order_acquire) || !removed || retained <= removed) {
      fail(InputEvidenceLoss::Malformed);
      return;
    }
    InputEvidenceRow row;
    row.kind = InputEvidenceKind::Hover;
    row.time = inputEvidenceNow();
    row.epoch = epoch;
    row.sequence = removed;
    row.otherSequence = retained;
    append(row);
  }
  std::optional<InputEvidenceRow> pop() noexcept
  {
    // Consumer only. Counter updates can finish their journal copies in a
    // different order. Retain their original timestamps and exact snapshots;
    // publish each concurrent queue in the order of its atomic counter updates.
    for (std::size_t examined = 0; examined != capacity; ++examined) {
      if (const auto pending = popPendingConcurrent())
        return pending;
      const auto read = m_read.load(std::memory_order_relaxed);
      auto &slot = m_rows[read % capacity];
      if (slot.ready.load(std::memory_order_acquire) != read + 1) {
        if (m_pendingConcurrent && (state() == InputEvidenceState::Stopped || state() == InputEvidenceState::Denied) &&
            read == m_written.load(std::memory_order_acquire) && m_occupied.load(std::memory_order_acquire) == 0) {
          // A failed producer may leave a counter gap. Preserve the remaining
          // real rows for diagnosis, but never certify this as complete input.
          fail(InputEvidenceLoss::Output, InputEvidenceFailureSite::JournalReserve);
          return popPendingConcurrent(true);
        }
        return {};
      }
      auto row = slot.row;
      slot.ready.store(0, std::memory_order_release);
      m_read.store(read + 1, std::memory_order_release);
      m_occupied.fetch_sub(1, std::memory_order_release);
      if (row.kind != InputEvidenceKind::EventQueue)
        return row;
      if (!concurrentInputEvidenceQueue(row.queue) || !row.sequence || row.sequence != row.enqueued + row.retired) {
        fail(InputEvidenceLoss::Output, InputEvidenceFailureSite::JournalReserve, row.queue);
        continue;
      }
      const auto queue = concurrentQueueIndex(row.queue);
      if (row.sequence == m_nextConcurrent[queue]) {
        ++m_nextConcurrent[queue];
        row.kind = InputEvidenceKind::Queue;
        return row;
      }
      auto &pending = m_pendingConcurrentRows[queue][row.sequence % concurrentPendingCapacity];
      if (row.sequence < m_nextConcurrent[queue] || pending) {
        fail(InputEvidenceLoss::Output, InputEvidenceFailureSite::JournalReserve, row.queue);
        continue;
      }
      pending = row;
      ++m_pendingConcurrent;
    }
    return {};
  }
  bool drained() const noexcept
  {
    // A producer can be inside its observation/mutation before reserving a
    // slot. Even a failed forced stop must retain that producer's eventual row
    // before writing a terminal footer. These checks never wait for producers.
    return m_read.load() == m_written.load() && m_occupied.load(std::memory_order_acquire) == 0 &&
           !m_pendingConcurrent && m_captureGate.load(std::memory_order_acquire) == 0 &&
           std::all_of(m_queues.begin(), m_queues.end(), [](const auto &queue) {
             return queue.activity.load(std::memory_order_acquire) == 0;
           });
  }
  std::uint64_t written() const noexcept
  {
    return m_written.load();
  }

private:
  friend struct InputEvidenceTestAccess;
  static constexpr std::size_t firstEventQueue = static_cast<std::size_t>(InputEvidenceQueue::EventOutstanding);
  static constexpr std::size_t concurrentQueueCount = 7, concurrentPendingCapacity = capacity / concurrentQueueCount;
  static constexpr std::size_t concurrentQueueIndex(InputEvidenceQueue queue)
  {
    return nativeSpscInputEvidenceQueue(queue) ? static_cast<std::size_t>(queue)
                                               : 3 + static_cast<std::size_t>(queue) - firstEventQueue;
  }
  std::optional<InputEvidenceRow> popPendingConcurrent(bool allowGap = false) noexcept
  {
    for (std::size_t queue = 0; queue != concurrentQueueCount; ++queue) {
      auto *pending = &m_pendingConcurrentRows[queue][m_nextConcurrent[queue] % concurrentPendingCapacity];
      if (!*pending || (*pending)->sequence != m_nextConcurrent[queue]) {
        if (!allowGap)
          continue;
        pending = nullptr;
        for (auto &candidate : m_pendingConcurrentRows[queue])
          if (candidate && (!pending || candidate->sequence < (*pending)->sequence))
            pending = &candidate;
        if (!pending)
          continue;
      }
      auto row = **pending;
      pending->reset();
      --m_pendingConcurrent;
      m_nextConcurrent[queue] = row.sequence + 1;
      row.kind = InputEvidenceKind::Queue;
      return row;
    }
    return {};
  }
  static void
  initializeEventQueue(void *context, const void *queue, std::uint64_t capacity, std::uint64_t dispatch) noexcept
  {
    auto &recorder = *static_cast<InputEvidenceRecorder *>(context);
    // The queue supplies its actual production admission bound. Ordinary
    // unguarded queues still use zero internally and serialize explicit null.
    recorder.initialize(InputEvidenceQueue::EventOutstanding, queue, capacity);
    recorder.initialize(InputEvidenceQueue::EventDispatch, queue, dispatch);
    recorder.initialize(InputEvidenceQueue::EventAllOutstanding, queue, capacity);
    recorder.initialize(InputEvidenceQueue::EventAllDispatch, queue, dispatch);
  }
  void commitConcurrentMutation(InputEvidenceQueue counter, bool added) noexcept
  {
    auto &q = m_queues[static_cast<std::size_t>(counter)];
    if (q.busy.test(std::memory_order_acquire))
      fail(InputEvidenceLoss::Contention, InputEvidenceFailureSite::QueueMutation, counter);
    else {
      // Native SPSC producers/consumers and Core Event workers legitimately overlap.
      // Before arm there are no measured cumulative counters or journal rows;
      // retain the exact live depth atomically instead of taking their lock.
      // The activity boundary excludes arm until every update has returned.
      const auto before =
          added ? q.depth.fetch_add(1, std::memory_order_acq_rel) : q.depth.fetch_sub(1, std::memory_order_acq_rel);
      if (added ? before == UINT64_MAX || (q.capacity && before >= q.capacity) : before == 0)
        fail(InputEvidenceLoss::QueueAccounting, InputEvidenceFailureSite::QueueMutation, counter);
      (void)recording(); // Preserve the absolute source deadline.
      if (state() == InputEvidenceState::Recording || state() == InputEvidenceState::Draining) {
        auto previous = q.eventCounts.load(std::memory_order_acquire);
        bool committed = false;
        // Each failed CAS means another writer completed an update. This is
        // bounded progress, never a wait for a suspended lock holder. The
        // journal/output row limits are far below either 32-bit counter bound.
        for (unsigned attempt = 0; attempt != 16; ++attempt) {
          const auto enqueued = previous >> 32, retired = previous & UINT32_MAX;
          if (enqueued < retired || (added ? enqueued == UINT32_MAX : retired == UINT32_MAX || retired >= enqueued)) {
            fail(InputEvidenceLoss::QueueAccounting, InputEvidenceFailureSite::QueueMutation, counter);
            break;
          }
          const auto next = previous + (added ? 1ULL << 32 : 1);
          // Read time after observing the prior counter value and before CAS.
          // Successful update order therefore preserves these actual times.
          const auto at = inputEvidenceNow();
          if (!q.eventCounts.compare_exchange_strong(previous, next, std::memory_order_acq_rel))
            continue;
          const auto nextAdded = next >> 32, nextRetired = next & UINT32_MAX;
          // Keep actual lifetime accounting after a sticky diagnostic loss,
          // as other queues do, without resuming payload/journal collection.
          if (loss() == InputEvidenceLoss::None)
            append(
                {InputEvidenceKind::EventQueue,
                 counter,
                 at,
                 0,
                 nextAdded + nextRetired,
                 0,
                 nextAdded,
                 nextRetired,
                 nextAdded - nextRetired,
                 q.capacity,
                 {}}
            );
          committed = true;
          break;
        }
        if (!committed)
          fail(InputEvidenceLoss::Contention, InputEvidenceFailureSite::QueueMutation, counter);
      }
    }
  }
  static void observeEvent(
      void *context, const void *queue, OriginalInputStamp stamp, deskflow::EventInputObservation action
  ) noexcept
  {
    auto &recorder = *static_cast<InputEvidenceRecorder *>(context);
    using Action = deskflow::EventInputObservation;
    const auto all = action >= Action::AllAdmit;
    if (all)
      action = static_cast<Action>(static_cast<unsigned>(action) - static_cast<unsigned>(Action::AllAdmit));
    if (action == Action::Lost) {
      recorder.fail(stamp || all ? InputEvidenceLoss::PipelineRevoked : InputEvidenceLoss::QueueIdentity);
      return;
    }
    if (!all && (!stamp || !stamp.part || stamp.part > maximumOriginalInputParts)) {
      recorder.fail(InputEvidenceLoss::Malformed);
      return;
    }
    if (!all &&
        (recorder.state() == InputEvidenceState::Recording || recorder.state() == InputEvidenceState::Draining) &&
        stamp.epoch != recorder.m_captureEpoch.load(std::memory_order_acquire))
      recorder.fail(InputEvidenceLoss::CaptureChanged);
    const bool dispatch = action == Action::Dispatch || action == Action::Return;
    const bool added = action == Action::Admit || action == Action::Dispatch;
    const auto counter =
        all ? (dispatch ? InputEvidenceQueue::EventAllDispatch : InputEvidenceQueue::EventAllOutstanding)
            : (dispatch ? InputEvidenceQueue::EventDispatch : InputEvidenceQueue::EventOutstanding);
    if (!recorder.beginQueueActivity(counter, queue))
      return;
    recorder.commitConcurrentMutation(counter, added);
    recorder.endQueueActivity(counter);
  }
  static constexpr std::uint64_t captureBoundary = 1ULL << 63;
  bool lockCaptureBoundary() noexcept
  {
    std::uint64_t expected = 0;
    return m_captureGate.compare_exchange_strong(expected, captureBoundary, std::memory_order_acq_rel);
  }
  bool unlockCaptureBoundary() noexcept
  {
    const auto previous = m_captureGate.fetch_and(~captureBoundary, std::memory_order_acq_rel);
    if (previous != captureBoundary) {
      fail(InputEvidenceLoss::Baseline);
      return false;
    }
    return true;
  }
  std::optional<std::uint64_t> reserveRow() noexcept
  {
    // Independent input and event queues are concurrent producers. Reserve a
    // capacity credit before taking a unique ticket; a competing writer does
    // not mean an entry was lost. No retry, allocation or producer wait occurs.
    // Credits include unpublished reservations and are returned only after pop
    // copies the row, so a slow writer cannot be overwritten on ring wrap.
    if (m_occupied.fetch_add(1, std::memory_order_acq_rel) >= capacity) {
      m_occupied.fetch_sub(1, std::memory_order_release);
      fail(InputEvidenceLoss::BufferFull, InputEvidenceFailureSite::JournalReserve);
      return {};
    }
    return m_written.fetch_add(1, std::memory_order_acq_rel);
  }
  void append(const InputEvidenceRow &row) noexcept
  {
    const auto next = reserveRow();
    if (!next)
      return;
    auto &slot = m_rows[*next % capacity];
    slot.row = row;
    slot.ready.store(*next + 1, std::memory_order_release);
  }
  struct Queue
  {
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    const void *identity = nullptr;
    std::uint64_t capacity = 0, enqueued = 0, retired = 0;
    std::atomic<std::uint64_t> depth{0};
    std::atomic<std::uint64_t> eventCounts{0};
    std::atomic<std::uint64_t> activity{0};
    std::atomic<bool> initialized{false};
  };
  struct Slot
  {
    std::atomic<std::uint64_t> ready{0};
    InputEvidenceRow row;
  };
  const InputEvidenceRole m_role;
  const deskflow::EventInputObserver m_eventObserver;
  std::array<Queue, queueCount> m_queues;
  std::array<Slot, capacity> m_rows;
  std::array<std::array<std::optional<InputEvidenceRow>, concurrentPendingCapacity>, concurrentQueueCount>
      m_pendingConcurrentRows;
  std::array<std::uint64_t, concurrentQueueCount> m_nextConcurrent{1, 1, 1, 1, 1, 1, 1};
  std::size_t m_pendingConcurrent = 0; // Consumer thread only, like pop()/drained().
  std::atomic<std::uint64_t> m_written{0}, m_read{0}, m_occupied{0}, m_deadline{0}, m_captureEpoch{0}, m_captureGate{0},
      m_stop{0}, m_sourceEnd{0};
  std::atomic<InputEvidenceState> m_state{InputEvidenceState::Waiting};
  std::atomic<std::uint32_t> m_failure{0};
  std::atomic_flag m_ownerBusy = ATOMIC_FLAG_INIT;
  std::atomic_flag m_stopBusy = ATOMIC_FLAG_INIT;
  InputEvidenceOwner m_owner;
  bool m_ownerLive = false;
  std::uint64_t m_start = 0, m_sourceSequence = 0, m_sourceTimestamp = 0;
};
inline std::atomic<InputEvidenceRecorder *> inputEvidenceInstalled{nullptr};
inline InputEvidenceRecorder *inputEvidence() noexcept
{
  return inputEvidenceInstalled.load(std::memory_order_acquire);
}
inline void inputEvidenceFail(
    InputEvidenceLoss reason, InputEvidenceFailureSite site = InputEvidenceFailureSite::Unspecified,
    InputEvidenceQueue queue = InputEvidenceQueue::Count
) noexcept
{
  if (auto *r = inputEvidence())
    r->fail(reason, site, queue);
}
inline void inputEvidenceInitialize(InputEvidenceQueue q, const void *identity, std::uint64_t capacity) noexcept
{
  if (auto *r = inputEvidence())
    r->initialize(q, identity, capacity);
}
// Keep the native operation visible to arm/drain boundaries without holding
// the diagnostic counter lock across a queue's publication. SPSC consumers
// may legally enter while the producer is finishing its published operation.
// commit() records the counters before publishing availability or free space;
// the activity guard stays live until the complete native mutation returns.
// Counter contention or journal overflow still denies diagnostics and never
// blocks input. Independent queues can reserve journal entries concurrently.
class InputEvidenceMutation final
{
public:
  InputEvidenceMutation(
      InputEvidenceQueue queue, const void *identity, std::uint64_t added, std::uint64_t removed
  ) noexcept
      : m_recorder(inputEvidence()),
        m_queue(queue),
        m_identity(identity),
        m_added(added),
        m_removed(removed)
  {
    if (m_recorder && !m_recorder->beginQueueActivity(queue, identity))
      m_recorder = nullptr;
  }
  ~InputEvidenceMutation()
  {
    if (m_recorder) {
      commit();
      m_recorder->endQueueActivity(m_queue);
    }
  }
  InputEvidenceMutation(const InputEvidenceMutation &) = delete;
  void commit() noexcept
  {
    if (m_recorder && !m_committed) {
      m_committed = true;
      if (m_recorder->beginMutation(m_queue, m_identity))
        m_recorder->endMutation(m_queue, m_added, m_removed);
    }
  }
  void removed(std::uint64_t count) noexcept
  {
    if (m_committed && m_recorder)
      m_recorder->fail(InputEvidenceLoss::QueueAccounting);
    m_removed = count;
  }

private:
  InputEvidenceRecorder *m_recorder;
  InputEvidenceQueue m_queue;
  const void *m_identity;
  std::uint64_t m_added, m_removed;
  bool m_committed = false;
};
template <class Function>
decltype(auto) inputEvidenceApply(
    InputEvidenceQueue queue, const void *identity, std::uint64_t added, std::uint64_t removed, Function &&function
)
{
  InputEvidenceMutation evidence(queue, identity, added, removed);
  return std::forward<Function>(function)();
}
class InputEvidenceCaptureActivity final
{
public:
  InputEvidenceCaptureActivity() : m_recorder(inputEvidence())
  {
    if (m_recorder)
      m_recorder->beginCaptureActivity();
  }
  ~InputEvidenceCaptureActivity()
  {
    if (m_recorder)
      m_recorder->endCaptureActivity();
  }

private:
  InputEvidenceRecorder *m_recorder;
};
class InputEvidenceExecution final
{
public:
  explicit InputEvidenceExecution(const void *identity, InputEvidenceQueue queue = InputEvidenceQueue::ServerExecuting)
      : m_identity(identity),
        m_queue(queue)
  {
    InputEvidenceMutation evidence(m_queue, identity, 1, 0);
  }
  ~InputEvidenceExecution()
  {
    InputEvidenceMutation evidence(m_queue, m_identity, 0, 1);
  }

private:
  const void *m_identity;
  InputEvidenceQueue m_queue;
};
inline void inputEvidenceInvalidate(InputEvidenceLoss reason) noexcept
{
  if (auto *r = inputEvidence();
      r && (r->state() == InputEvidenceState::Recording || r->state() == InputEvidenceState::Draining))
    r->fail(reason);
}
} // namespace deskflow::windowshare
