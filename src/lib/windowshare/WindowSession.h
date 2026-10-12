// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace deskflow::windowshare {

enum class Phase : std::uint8_t {
  Local,
  PreparingExport,
  Transferring,
  Remote,
  PreparingReturn
};

struct WindowIdentity {
  std::uint64_t sessionId = 0;
  std::uint32_t processId = 0;
  std::uint32_t windowId = 0;
  std::uint64_t generation = 0;
};

struct WindowGeometry {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t revision = 0;
};

class WindowSession {
public:
  explicit WindowSession(WindowIdentity identity) : m_identity(identity) {}

  const WindowIdentity &identity() const { return m_identity; }
  Phase phase() const { return m_phase; }
  bool hasRecoverySnapshot() const { return m_original.has_value(); }
  bool destinationReady() const { return m_destinationReady; }
  const std::optional<WindowGeometry> &originalGeometry() const { return m_original; }

  bool prepareExport(WindowGeometry original)
  {
    if (m_phase != Phase::Local || m_identity.sessionId == 0 ||
        m_identity.processId == 0 || m_identity.windowId == 0 ||
        original.width == 0 || original.height == 0)
      return false;
    m_original = original;
    m_destinationReady = false;
    m_phase = Phase::PreparingExport;
    return true;
  }

  bool acknowledgeDestination(std::uint64_t generation)
  {
    if (m_phase != Phase::PreparingExport || generation != m_identity.generation)
      return false;
    m_destinationReady = true;
    return true;
  }

  bool beginTransfer()
  {
    if (m_phase != Phase::PreparingExport || !m_destinationReady || !m_original)
      return false;
    m_phase = Phase::Transferring;
    return true;
  }

  bool commitExport()
  {
    if (m_phase != Phase::Transferring)
      return false;
    m_phase = Phase::Remote;
    return true;
  }

  bool prepareReturn()
  {
    if (m_phase != Phase::Remote || !m_original)
      return false;
    m_phase = Phase::PreparingReturn;
    return true;
  }

  bool commitReturn()
  {
    if (m_phase != Phase::PreparingReturn)
      return false;
    reset();
    return true;
  }

  // Caller must restore the source window using originalGeometry()
  // before invoking resetAfterRestore(). Never discard recovery state first.
  bool resetAfterRestore()
  {
    if (m_phase == Phase::Local)
      return false;
    reset();
    return true;
  }

private:
  void reset()
  {
    m_phase = Phase::Local;
    m_original.reset();
    m_destinationReady = false;
  }

  WindowIdentity m_identity;
  Phase m_phase = Phase::Local;
  std::optional<WindowGeometry> m_original;
  bool m_destinationReady = false;
};

} // namespace deskflow::windowshare
