/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

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

  bool valid() const noexcept
  {
    return server && recipient && press && press.part == 1 && motion.part == 1 && motion.epoch == press.epoch &&
           motion.sequence > press.sequence && side >= WindowDragEdgeSide::Left && side <= WindowDragEdgeSide::Bottom &&
           source.contains(sourceX, sourceY) && destination.contains(destinationX, destinationY) && peer.live();
  }
  bool sameBoundary(const WindowDragEdge &other) const noexcept
  {
    return server == other.server && recipient == other.recipient && press == other.press && side == other.side &&
           source == other.source && destination == other.destination &&
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

  WindowDragEdgeMailbox() : m_token(std::make_shared<std::atomic<std::uint64_t>>(0))
  {
  }
  ~WindowDragEdgeMailbox()
  {
    close();
  }
  WindowDragEdgeMailbox(const WindowDragEdgeMailbox &) = delete;
  WindowDragEdgeMailbox &operator=(const WindowDragEdgeMailbox &) = delete;

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
    m_token->store(0, std::memory_order_release);
    m_value.reset();
  }
  mutable std::mutex m_mutex;
  const std::shared_ptr<std::atomic<std::uint64_t>> m_token;
  std::optional<Observation> m_value;
  std::uint64_t m_server = 0, m_generation = 0;
  bool m_closed = false;
};
} // namespace deskflow::windowshare
