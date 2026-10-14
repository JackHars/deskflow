/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalInput.h"
#include "OriginalInputStamp.h"
#include "io/AuthenticatedPeerTicket.h"
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

class Server;

namespace deskflow::windowshare {

enum class WindowDragEdgeSide : std::uint8_t
{
  Left = 1,
  Right,
  Top,
  Bottom
};
struct WindowDragScreen
{
  std::int32_t x = 0, y = 0, width = 0, height = 0;
  bool operator==(const WindowDragScreen &) const = default;
  bool contains(std::int32_t px, std::int32_t py) const noexcept
  {
    return width > 0 && height > 0 && px >= x && py >= y && std::int64_t(px) < std::int64_t(x) + width &&
           std::int64_t(py) < std::int64_t(y) + height;
  }
};

// Core's configured neighbour at one original motion, while the left press
// still belongs to the primary screen. No proxy, Server or QObject crosses
// threads. This is an observation, never permission to switch or post input.
struct WindowDragEdge
{
  std::uint64_t server = 0, recipient = 0;
  OriginalInputStamp press, motion;
  WindowDragEdgeSide side = WindowDragEdgeSide::Left;
  WindowDragScreen source, destination;
  std::int32_t sourceX = 0, sourceY = 0, destinationX = 0, destinationY = 0;
  AuthenticatedPeerTicket peer;
  bool returning = false;

  bool valid() const noexcept
  {
    return server && recipient && press && press.part == 1 && motion.part == 1 && motion.epoch == press.epoch &&
           motion.sequence > press.sequence && side >= WindowDragEdgeSide::Left && side <= WindowDragEdgeSide::Bottom &&
           source.contains(sourceX, sourceY) && destination.contains(destinationX, destinationY) && peer.live();
  }
  bool sameBoundary(const WindowDragEdge &other) const noexcept
  {
    return server == other.server && recipient == other.recipient && returning == other.returning &&
           press == other.press && side == other.side && source == other.source && destination == other.destination &&
           peer.incarnation() == other.peer.incarnation() && peer.fingerprint() == other.peer.fingerprint();
  }
};

// One latest value. Only Server publishes; the Qt owner reads and joins its
// press stamp to the existing raw consumer. Retained values expire even if a
// worker stalls. A changed boundary, release, topology change, shutdown or TLS
// revocation invalidates them without waiting for the reader's event loop.
class WindowDragEdgeMailbox final
{
  friend class ::Server;
  friend struct WindowDragEdgeMailboxTestAccess;

public:
  using Clock = std::chrono::steady_clock;
  static constexpr auto freshness = std::chrono::milliseconds(250);
  static constexpr auto dropBudget = std::chrono::milliseconds(1500);
  class Observation final
  {
    friend class WindowDragEdgeMailbox;

  public:
    const WindowDragEdge &edge() const noexcept
    {
      return m_edge;
    }
    std::uint64_t generation() const noexcept
    {
      return m_generation;
    }
    bool live(Clock::time_point now = Clock::now()) const noexcept
    {
      return now >= m_published && now - m_published < freshness && current();
    }
    // Retained continuity of a preparation that already began from live().
    // This does not refresh the motion sample or make it eligible to start a
    // new drag. Release/topology/peer loss still revoke it synchronously.
    bool current() const noexcept
    {
      return m_edge.peer.live() && m_token->load(std::memory_order_acquire) == m_generation;
    }

  private:
    Observation(
        WindowDragEdge edge, std::uint64_t generation, std::shared_ptr<const std::atomic<std::uint64_t>> token,
        Clock::time_point published
    )
        : m_edge(std::move(edge)),
          m_generation(generation),
          m_token(std::move(token)),
          m_published(published)
    {
    }
    WindowDragEdge m_edge;
    std::uint64_t m_generation;
    std::shared_ptr<const std::atomic<std::uint64_t>> m_token;
    Clock::time_point m_published;
  };

  // Issued by Server only after resolving the original local press's real up.
  // The raw producer's sequence fence revokes this on any subsequent input,
  // without waiting for Qt or the Server worker to drain another queue.
  class Release final
  {
    friend class WindowDragEdgeMailbox;

  public:
    const WindowDragEdge &edge() const noexcept
    {
      return m_boundary.edge();
    }
    std::uint64_t generation() const noexcept
    {
      return m_boundary.generation();
    }
    OriginalInputStamp up() const noexcept
    {
      return m_up;
    }
    bool live(Clock::time_point now = Clock::now()) const noexcept
    {
      return now < m_deadline && m_epoch.live() && edge().peer.live() && m_position &&
             m_position->load(std::memory_order_acquire) == m_up.sequence &&
             m_token->load(std::memory_order_acquire) == generation();
    }

  private:
    Release(
        Observation boundary, OriginalInputStamp up, OriginalInputEpoch epoch,
        std::shared_ptr<const std::atomic<std::uint64_t>> position,
        std::shared_ptr<const std::atomic<std::uint64_t>> token, Clock::time_point deadline
    )
        : m_boundary(std::move(boundary)),
          m_up(up),
          m_epoch(std::move(epoch)),
          m_position(std::move(position)),
          m_token(std::move(token)),
          m_deadline(deadline)
    {
    }
    Observation m_boundary;
    OriginalInputStamp m_up;
    OriginalInputEpoch m_epoch;
    std::shared_ptr<const std::atomic<std::uint64_t>> m_position, m_token;
    Clock::time_point m_deadline;
  };
  struct Transfer
  {
    Release release;
    std::int32_t x = 0, y = 0;
  };
  struct TransferResult
  {
    std::uint64_t generation = 0;
    OriginalInputStamp up;
    bool switched = false;
  };

  WindowDragEdgeMailbox() : m_token(std::make_shared<std::atomic<std::uint64_t>>(0))
  {
  }
  ~WindowDragEdgeMailbox()
  {
    close();
  }
  WindowDragEdgeMailbox(const WindowDragEdgeMailbox &) = delete;
  WindowDragEdgeMailbox &operator=(const WindowDragEdgeMailbox &) = delete;

  // Startup only, before the capture producer or Server worker starts.
  bool bindCapture(OriginalInputEpoch epoch, std::shared_ptr<const std::atomic<std::uint64_t>> position)
  {
    std::lock_guard lock(m_mutex);
    if (m_closed || m_position || !epoch.live() || !position)
      return false;
    m_epoch = std::move(epoch);
    m_position = std::move(position);
    return true;
  }
  std::optional<Release> readRelease() const
  {
    std::lock_guard lock(m_mutex);
    return !m_closed && m_release && m_release->live() ? m_release : std::nullopt;
  }
  bool requestTransfer(const Release &release, std::int32_t x, std::int32_t y)
  {
    std::lock_guard lock(m_mutex);
    if (m_closed || m_transfer || m_inflight || !m_release || !release.live() || !m_release->live() ||
        release.m_token != m_releaseToken || release.generation() != m_release->generation() ||
        release.up() != m_release->up() || !release.edge().destination.contains(x, y))
      return false;
    m_result.reset();
    m_transfer = Transfer{release, x, y};
    return true;
  }
  std::optional<TransferResult> transferResult() const
  {
    std::lock_guard lock(m_mutex);
    return m_result;
  }
  void cancelRelease(std::uint64_t generation)
  {
    std::lock_guard lock(m_mutex);
    if (m_release && m_release->generation() == generation)
      clearReleaseLocked();
  }

  std::optional<Observation> read() const
  {
    std::lock_guard lock(m_mutex);
    return !m_closed && m_value && m_value->live() ? m_value : std::nullopt;
  }
  void close() noexcept
  {
    std::lock_guard lock(m_mutex);
    m_closed = true;
    clearLocked();
  }

private:
  bool release(OriginalInputStamp press, OriginalInputStamp up, Clock::time_point now = Clock::now())
  {
    std::lock_guard lock(m_mutex);
    if (m_closed || !m_value || !m_value->current() || m_value->edge().press != press || up.part != 1 ||
        up.epoch != press.epoch || up.sequence <= m_value->edge().motion.sequence || !m_epoch.live() ||
        m_epoch.id() != up.epoch || !m_position || m_position->load(std::memory_order_acquire) != up.sequence) {
      clearLocked();
      return false;
    }
    clearReleaseLocked();
    m_releaseToken->store(m_value->generation(), std::memory_order_release);
    m_release = Release{*m_value, up, m_epoch, m_position, m_releaseToken, now + dropBudget};
    m_token->store(0, std::memory_order_release);
    m_value.reset();
    return true;
  }
  std::optional<Transfer> takeTransfer()
  {
    std::lock_guard lock(m_mutex);
    if (m_closed || !m_transfer || m_inflight)
      return {};
    if (!m_transfer->release.live()) {
      clearReleaseLocked();
      return {};
    }
    m_inflight = std::exchange(m_transfer, {});
    return m_inflight;
  }
  void completeTransfer(const Transfer &request, bool switched)
  {
    std::lock_guard lock(m_mutex);
    if (m_inflight && m_inflight->release.generation() == request.release.generation() &&
        m_inflight->release.up() == request.release.up()) {
      m_result = TransferResult{request.release.generation(), request.release.up(), switched};
      m_inflight.reset();
      clearReleaseLocked();
    }
  }
  void clearReleaseLocked() noexcept
  {
    m_releaseToken->store(0, std::memory_order_release);
    m_release.reset();
    if (m_transfer)
      m_result = TransferResult{m_transfer->release.generation(), m_transfer->release.up(), false};
    m_transfer.reset();
  }
  bool bind(std::uint64_t server) noexcept
  {
    std::lock_guard lock(m_mutex);
    if (!server || m_server || m_closed)
      return false;
    m_server = server;
    return true;
  }
  bool publish(WindowDragEdge edge, Clock::time_point now = Clock::now())
  {
    std::lock_guard lock(m_mutex);
    if (m_closed)
      return false;
    if (edge.server != m_server || !edge.valid()) {
      clearLocked();
      return false;
    }
    const bool same = m_value && m_value->current() && m_value->edge().sameBoundary(edge);
    if (same && edge.motion.sequence <= m_value->edge().motion.sequence) {
      clearLocked();
      return false;
    }
    if (!same) {
      clearLocked();
      if (m_generation == std::numeric_limits<std::uint64_t>::max()) {
        m_closed = true;
        return false;
      }
      ++m_generation;
      m_token->store(m_generation, std::memory_order_release);
    }
    m_value = Observation{std::move(edge), m_generation, m_token, now};
    return true;
  }
  void clear() noexcept
  {
    std::lock_guard lock(m_mutex);
    clearLocked();
  }
  void clearLocked() noexcept
  {
    clearReleaseLocked();
    m_token->store(0, std::memory_order_release);
    m_value.reset();
  }
  mutable std::mutex m_mutex;
  const std::shared_ptr<std::atomic<std::uint64_t>> m_token;
  const std::shared_ptr<std::atomic<std::uint64_t>> m_releaseToken = std::make_shared<std::atomic<std::uint64_t>>(0);
  OriginalInputEpoch m_epoch;
  std::shared_ptr<const std::atomic<std::uint64_t>> m_position;
  std::optional<Release> m_release;
  std::optional<Transfer> m_transfer, m_inflight;
  std::optional<TransferResult> m_result;
  std::optional<Observation> m_value;
  std::uint64_t m_server = 0, m_generation = 0;
  bool m_closed = false;
};
} // namespace deskflow::windowshare
