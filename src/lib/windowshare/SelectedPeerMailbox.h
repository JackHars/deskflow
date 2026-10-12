/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "SelectedPeer.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace deskflow::windowshare {

// One latest value, with no queue, callbacks, QObject or borrowed core pointer.
// Endpoints retain shared ownership while calling it; the session must close it
// before shutdown even if the core publisher still retains its shared_ptr.
class SelectedPeerMailbox final
{
  friend struct SelectedPeerMailboxTestAccess;
  using Clock = std::chrono::steady_clock;

public:
  static constexpr auto freshness = std::chrono::milliseconds(250);

  class Observation final
  {
    friend class SelectedPeerMailbox;
    friend struct SelectedPeerMailboxTestAccess;

  public:
    const SelectedPeer &peer() const noexcept;
    std::uint64_t revision() const noexcept;
    // Check at use. Retained observations expire independently of subsequent
    // refreshes. A true result is not a reservation against later revocation.
    bool live() const noexcept;

  private:
    Observation(
        SelectedPeer peer, std::uint64_t revision, std::uint64_t selection,
        std::shared_ptr<const std::atomic<std::uint64_t>> token, Clock::time_point published
    );
    bool liveAt(Clock::time_point now) const noexcept;

    SelectedPeer m_peer;
    std::uint64_t m_revision, m_selection;
    std::shared_ptr<const std::atomic<std::uint64_t>> m_token;
    Clock::time_point m_published;
  };

  struct ReadResult
  {
    std::uint64_t revision = 0;
    bool closed = false;
    std::optional<Observation> selected;
  };

  SelectedPeerMailbox();
  ~SelectedPeerMailbox();
  SelectedPeerMailbox(const SelectedPeerMailbox &) = delete;
  SelectedPeerMailbox &operator=(const SelectedPeerMailbox &) = delete;

  // No authority initially. Invalid/revoked input clears the current selection
  // and returns false. Refreshing the exact same live selection renews only the
  // newly returned observation; replacement and clear revoke retained values.
  bool replace(const SelectedPeer &peer);
  bool clear();
  // Terminal, idempotent, and also performed by the destructor. Publication
  // after close or revision exhaustion is denied; counters never wrap.
  void close();
  ReadResult read() const;
  // Reports publication changes even when the latest value has since expired
  // or been revoked externally. Consumers must still call observation.live().
  std::optional<ReadResult> readAfter(std::uint64_t revision) const;

private:
  bool replaceAt(const SelectedPeer &peer, Clock::time_point now);
  bool advanceLocked();
  bool clearLocked();
  void closeLocked();
  ReadResult readLocked() const;

  mutable std::mutex m_mutex;
  const std::shared_ptr<std::atomic<std::uint64_t>> m_token;
  std::optional<Observation> m_selected;
  std::uint64_t m_revision = 0;
  bool m_closed = false;
};

} // namespace deskflow::windowshare
