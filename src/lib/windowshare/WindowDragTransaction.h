/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>

namespace deskflow::windowshare {

enum class WindowDragDirection : std::uint8_t
{
  Export = 1,
  Return = 2
};

// Local coordinator identity, never an input lease or a network assertion that
// a physical button is up. A native adapter must retain the exact window and
// independently validate the configured edge, connection and physical layout.
struct WindowDragBinding
{
  std::uint64_t drag = 0, connection = 0, topology = 0;
  std::uint64_t captureEpoch = 0, pressSequence = 0, pressTime = 0;
  std::uint32_t window = 0;
  std::int64_t eventNumber = 0;
  WindowDragDirection direction = WindowDragDirection::Export;
  bool operator==(const WindowDragBinding &) const = default;
};
struct WindowDragRelease
{
  std::uint64_t captureEpoch = 0, sequence = 0, sourceTime = 0;
  std::uint32_t window = 0;
  std::int64_t eventNumber = 0;
  bool operator==(const WindowDragRelease &) const = default;
};

inline bool validWindowDragBinding(const WindowDragBinding &binding) noexcept
{
  return binding.drag && binding.connection && binding.topology && binding.captureEpoch && binding.pressSequence &&
         binding.pressTime && binding.window && binding.eventNumber >= 0 &&
         (binding.direction == WindowDragDirection::Export || binding.direction == WindowDragDirection::Return);
}

// One ordinary drag, including a provisional preview. The physical press and
// its one real release stay with the original native owner. A painted preview
// alone cannot move the source. No timer, callback, input synthesis or native
// mutation occurs here; the adapter performs rollback through its journal.
class WindowDragTransaction final
{
public:
  using Clock = std::chrono::steady_clock;
  static constexpr auto dragBudget = std::chrono::seconds(30);
  static constexpr auto dropBudget = std::chrono::milliseconds(1500);
  enum class Phase
  {
    Idle,
    Preparing,
    Released,
    Committing,
    Committed,
    Cancelled
  };
  WindowDragTransaction() = default;
  WindowDragTransaction(const WindowDragTransaction &) = delete;
  WindowDragTransaction &operator=(const WindowDragTransaction &) = delete;

  bool begin(WindowDragBinding binding, std::uint64_t geometry, Clock::time_point now = Clock::now()) noexcept
  {
    if (m_phase != Phase::Idle || !validWindowDragBinding(binding) || !geometry)
      return false;
    m_binding = binding;
    m_geometry = geometry;
    m_deadline = now + dragBudget;
    m_phase = Phase::Preparing;
    return true;
  }
  Phase phase() const noexcept
  {
    return m_phase;
  }
  const WindowDragBinding &binding() const noexcept
  {
    return m_binding;
  }
  std::uint64_t geometry() const noexcept
  {
    return m_geometry;
  }
  std::uint64_t paintedGeometry() const noexcept
  {
    return m_painted;
  }
  bool held() const noexcept
  {
    return m_phase == Phase::Preparing;
  }
  bool terminal() const noexcept
  {
    return m_phase == Phase::Committed || m_phase == Phase::Cancelled;
  }

  // Called on identity/connection/layout loss, leaving the edge, Escape,
  // source destruction, preview closure or transport failure. Irreversible
  // for this transaction, including while a native commit callback is active.
  void cancel() noexcept
  {
    if (m_phase != Phase::Committed)
      m_phase = Phase::Cancelled;
  }
  bool current(const WindowDragBinding &binding, Clock::time_point now = Clock::now()) noexcept
  {
    if (m_phase == Phase::Idle || terminal())
      return false;
    if (binding != m_binding || now >= m_deadline) {
      cancel();
      return false;
    }
    return true;
  }
  // A new geometry invalidates both production and painted acknowledgements.
  // Old frames cannot satisfy a newer drop; delayed old metadata is ignored.
  bool geometryChanged(std::uint64_t geometry, Clock::time_point now = Clock::now()) noexcept
  {
    if (!current(m_binding, now) || m_phase == Phase::Committing || !geometry || geometry <= m_geometry)
      return false;
    m_geometry = geometry;
    m_produced = m_painted = 0;
    return true;
  }
  bool produced(std::uint64_t geometry, Clock::time_point now = Clock::now()) noexcept
  {
    if (!current(m_binding, now) || m_phase == Phase::Committing || geometry != m_geometry)
      return false;
    m_produced = geometry;
    return true;
  }
  bool painted(std::uint64_t geometry, Clock::time_point now = Clock::now()) noexcept
  {
    if (!current(m_binding, now) || m_phase == Phase::Committing || geometry != m_geometry || m_produced != geometry)
      return false;
    m_painted = geometry;
    return true;
  }
  bool
  release(const WindowDragBinding &binding, WindowDragRelease release, Clock::time_point now = Clock::now()) noexcept
  {
    if (!current(binding, now) || m_phase != Phase::Preparing)
      return false;
    if (release.captureEpoch != binding.captureEpoch || release.sequence <= binding.pressSequence ||
        release.sourceTime <= binding.pressTime || release.window != binding.window ||
        release.eventNumber != binding.eventNumber) {
      cancel();
      return false;
    }
    m_release = release;
    m_phase = Phase::Released;
    // An absolute budget, not renewed by frames, retries or duplicate ups.
    m_deadline = std::min(m_deadline, now + dropBudget);
    return true;
  }
  // Reserve once BEFORE a borrowed/native callback. A reentrant callback can
  // cancel the reservation; the adapter must check it again before each write.
  bool reserveCommit(const WindowDragBinding &binding, Clock::time_point now = Clock::now()) noexcept
  {
    if (!current(binding, now) || m_phase != Phase::Released || !m_release || m_painted != m_geometry ||
        m_produced != m_geometry)
      return false;
    m_phase = Phase::Committing;
    return true;
  }
  bool committing(const WindowDragBinding &binding, Clock::time_point now = Clock::now()) noexcept
  {
    return current(binding, now) && m_phase == Phase::Committing;
  }
  bool committed(const WindowDragBinding &binding, Clock::time_point now = Clock::now()) noexcept
  {
    if (!committing(binding, now))
      return false;
    m_phase = Phase::Committed;
    return true;
  }

private:
  WindowDragBinding m_binding;
  std::optional<WindowDragRelease> m_release;
  Phase m_phase = Phase::Idle;
  Clock::time_point m_deadline;
  std::uint64_t m_geometry = 0, m_produced = 0, m_painted = 0;
};
} // namespace deskflow::windowshare
