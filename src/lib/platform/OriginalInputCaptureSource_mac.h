/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalInput_mac.h"

namespace deskflow::windowshare {

// Allocate before starting the event tap and retain until producer and consumer
// have stopped. One instance is one capture epoch: stop, overflow, malformed
// input or tap disable cannot be repaired by calling start again. An epoch is
// continuity evidence only, never a physical-neutral snapshot or input grant.
class OriginalInputCaptureSource final
{
public:
  OriginalInputCaptureSource() = default;
  ~OriginalInputCaptureSource();
  OriginalInputCaptureSource(const OriginalInputCaptureSource &) = delete;
  OriginalInputCaptureSource &operator=(const OriginalInputCaptureSource &) = delete;

  // Producer thread only. No allocation, wait, tap registration or OS posting.
  bool start() noexcept;
  void capture(CGEventType type, CGEventRef event) noexcept;
  bool checkpoint(std::uint64_t timestampNs, std::uint64_t flags) noexcept;

  // Exactly one consumer, outside the tap. Check live evidence on every use.
  std::optional<OriginalInputDelivery> pop() noexcept;
  // Thread-safe. Empty until started, or once continuity is lost. Previously
  // returned tokens remain revoked even after this object is destroyed.
  OriginalInputEpoch epoch() const noexcept;
  bool emergencyRequested() const noexcept;
  void stop() noexcept;

private:
  enum class State : std::uint8_t
  {
    Ready,
    Running,
    Stopped
  };
  OriginalInputStream m_stream;
  std::atomic<State> m_state{State::Ready};
  std::uint64_t m_sequence = 0;
};

} // namespace deskflow::windowshare
