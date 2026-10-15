/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "InputEvidence.h"
#include "OriginalInputRoute.h"
#include <limits>
#include <utility>

namespace deskflow::windowshare {

// Completed copies from the existing Session capture consumer to one Server.
// This is not a second raw-stream consumer or an input authority. Allocate and
// bind outside the event tap; one Session producer calls publish, one Server
// consumer calls pop. Stop both workers before destruction. Revocation and
// retained evidence queries are thread-safe and never revoke the raw epoch.
class OriginalInputCapturedInbox final
{
public:
  static constexpr std::size_t capacity = 256;
  explicit OriginalInputCapturedInbox(OriginalInputEpoch epoch) : m_epoch(std::move(epoch))
  {
    inputEvidenceInitialize(InputEvidenceQueue::Completed, this, capacity);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic_bool::is_always_lock_free);
  }
  ~OriginalInputCapturedInbox()
  {
    revoke();
  }
  OriginalInputCapturedInbox(const OriginalInputCapturedInbox &) = delete;
  OriginalInputCapturedInbox &operator=(const OriginalInputCapturedInbox &) = delete;

  // One-shot server identity. Even a duplicate bind to the same server revokes
  // the conduit, preventing another consumer from silently taking ownership.
  bool bind(std::uint64_t server) noexcept
  {
    std::uint64_t expected = 0;
    if (server && live() && m_server.compare_exchange_strong(expected, server, std::memory_order_acq_rel))
      return live();
    revoke();
    return false;
  }
  bool bound() const noexcept
  {
    return m_server.load(std::memory_order_acquire) != 0;
  }
  OriginalInputEpoch epoch() const noexcept
  {
    return m_epoch;
  }
  bool publish(const OriginalInputCaptured &captured) noexcept
  {
    // Session drains normally before Server registration; those records must
    // not fill or poison a conduit that has no consumer yet.
    if (!bound() || !live())
      return false;
    const auto &record = captured.input.record;
    const auto &translation = captured.translation;
    if (!captured.input.live() || !captured.input.epoch.sameEpoch(m_epoch) || !validOriginalInputRecord(record) ||
        (captured.pointer && !captured.pointer->matches(record)) ||
        translation.stamp != OriginalInputStamp{record.epoch, record.sequence, 0} ||
        translation.parts > maximumOriginalInputParts || m_lastSequence == std::numeric_limits<std::uint64_t>::max() ||
        (m_lastSequence && record.sequence != m_lastSequence + 1) || record.sourceTimestampNs < m_lastTimestamp) {
      revoke();
      return false;
    }
    const auto written = m_written.load(std::memory_order_relaxed);
    if (written == std::numeric_limits<std::uint64_t>::max() ||
        written - m_read.load(std::memory_order_acquire) >= capacity) {
      revoke();
      return false;
    }
    InputEvidenceMutation evidence(InputEvidenceQueue::Completed, this, 1, 0);
    m_records[written % capacity] = captured;
    m_lastSequence = record.sequence;
    m_lastTimestamp = record.sourceTimestampNs;
    evidence.commit();
    m_written.store(written + 1, std::memory_order_release);
    return live();
  }
  std::optional<OriginalInputCaptured> pop() noexcept
  {
    if (!bound() || !live())
      return {};
    const auto read = m_read.load(std::memory_order_relaxed);
    if (read == m_written.load(std::memory_order_acquire))
      return {};
    InputEvidenceMutation evidence(InputEvidenceQueue::Completed, this, 0, 1);
    auto result = m_records[read % capacity];
    evidence.commit();
    m_read.store(read + 1, std::memory_order_release);
    // A successful copy is still only evidence. The consumer must recheck
    // this conduit and the retained original epoch at every subsequent use.
    if (!live() || !result.input.live())
      return {};
    return result;
  }
  bool live() const noexcept
  {
    if (!m_epoch.live())
      m_revoked->store(true, std::memory_order_release);
    return !m_revoked->load(std::memory_order_acquire);
  }
  void revoke() noexcept
  {
    inputEvidenceInvalidate(InputEvidenceLoss::PipelineRevoked);
    m_revoked->store(true, std::memory_order_release);
  }
  std::shared_ptr<const std::atomic_bool> evidence() const noexcept
  {
    (void)live();
    return m_revoked;
  }

private:
  const OriginalInputEpoch m_epoch;
  std::array<OriginalInputCaptured, capacity> m_records;
  std::shared_ptr<std::atomic_bool> m_revoked = std::make_shared<std::atomic_bool>(false);
  std::atomic<std::uint64_t> m_server{0}, m_written{0}, m_read{0};
  std::uint64_t m_lastSequence = 0, m_lastTimestamp = 0; // Session producer only.
};

} // namespace deskflow::windowshare
