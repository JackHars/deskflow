/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalInput_mac.h"
#include "windowshare/OriginalInputCapturedInbox.h"
#include "windowshare/OriginalInputRoute.h"

namespace deskflow::windowshare {

// Allocate before starting the event tap and retain until producer and consumer
// have stopped. One instance is one capture epoch: stop, overflow, malformed
// input or tap disable cannot be repaired by calling start again. An epoch is
// continuity evidence only, never a physical-neutral snapshot or input grant.
class OriginalInputCaptureSource final
{
public:
  OriginalInputCaptureSource();
  ~OriginalInputCaptureSource();
  OriginalInputCaptureSource(const OriginalInputCaptureSource &) = delete;
  OriginalInputCaptureSource &operator=(const OriginalInputCaptureSource &) = delete;

  // Producer thread only. No allocation, wait, tap registration or OS posting.
  bool start() noexcept;
  // Production brackets existing legacy translation with begin/complete. The
  // convenience capture() records an explicitly untranslated sample.
  OriginalInputStamp beginCapture(CGEventType type, CGEventRef event) noexcept;
  void completeCapture(OriginalInputTranslation translation) noexcept;
  void capture(CGEventType type, CGEventRef event) noexcept;
  bool checkpoint(std::uint64_t timestampNs, std::uint64_t flags) noexcept;

  // Exactly one consumer, outside the tap. Check live evidence on every use.
  std::optional<OriginalInputCaptured> popCaptured() noexcept;
  std::optional<OriginalInputDelivery> pop() noexcept;
  std::shared_ptr<OriginalInputRoutes> routes() const noexcept
  {
    return m_routes;
  }
  // Copies published by the existing Session consumer, never a second raw
  // capture consumer. A retained inbox is revoked with this source's stop.
  std::shared_ptr<OriginalInputCapturedInbox> completedInput() const noexcept
  {
    return m_completedInput;
  }
  // Thread-safe. Empty until started, or once continuity is lost. Previously
  // returned tokens remain revoked even after this object is destroyed.
  OriginalInputEpoch epoch() const noexcept;
  bool emergencyRequested() const noexcept;
  void stop() noexcept;

private:
  OriginalInputTransitionEvidence classifyTransition(const OriginalInputRecord &) noexcept;
  enum class State : std::uint8_t
  {
    Ready,
    Running,
    Stopped
  };
  OriginalInputStream m_stream;
  std::atomic<State> m_state{State::Ready};
  std::uint64_t m_sequence = 0;
  OriginalInputStamp m_openStamp;
  OriginalInputTransitionEvidence m_openTransition;
  // Zero means Unknown or held, never "up by default". Only a captured Up
  // establishes a released control; each subsequent Down consumes that edge.
  std::array<std::uint64_t, originalKey::keyCount> m_releasedKeys{};
  std::array<std::uint64_t, originalInputButtonCount> m_releasedButtons{};
  OriginalScrollState m_scroll = OriginalScrollState::Unknown;
  struct Completed
  {
    OriginalInputTranslation translation;
    OriginalInputTransitionEvidence transition;
  };
  std::array<Completed, OriginalInputStream::capacity> m_completed;
  std::atomic<std::uint64_t> m_completedWritten{0}, m_completedRead{0};
  std::shared_ptr<OriginalInputRoutes> m_routes = std::make_shared<OriginalInputRoutes>();
  std::shared_ptr<OriginalInputCapturedInbox> m_completedInput =
      std::make_shared<OriginalInputCapturedInbox>(m_stream.epoch());
};

} // namespace deskflow::windowshare
