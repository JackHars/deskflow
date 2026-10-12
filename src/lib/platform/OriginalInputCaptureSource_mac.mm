/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "OriginalInputCaptureSource_mac.h"
#include "OriginalKeyboard_mac.h"

#include <limits>

namespace deskflow::windowshare {

OriginalInputCaptureSource::~OriginalInputCaptureSource()
{
  stop();
}

bool OriginalInputCaptureSource::start() noexcept
{
  static_assert(std::atomic<State>::is_always_lock_free);
  auto expected = State::Ready;
  return m_state.compare_exchange_strong(expected, State::Running, std::memory_order_acq_rel) &&
         m_stream.epoch().live();
}

void OriginalInputCaptureSource::capture(CGEventType type, CGEventRef event) noexcept
{
  if (m_state.load(std::memory_order_acquire) != State::Running)
    return;
  // Recognize the escape chord even after overflow/revocation and even when
  // side-specific modifier flags are ambiguous. This capture validates syntax
  // and ignores our synthetic marker without inventing a neutral observation.
  if (type == kCGEventKeyDown && event && CGEventGetType(event) == type) {
    const auto keyboard = captureOriginalKeyboardEvent(event, 1);
    if (keyboard.sample && originalEmergencyChord(*keyboard.sample)) {
      m_stream.revoke(OriginalInputRevocation::Emergency);
      return;
    }
  }
  if (!m_stream.epoch().live())
    return;
  if (m_sequence == std::numeric_limits<std::uint64_t>::max()) {
    m_stream.revoke(OriginalInputRevocation::Exhausted);
    return;
  }
  const auto result = captureOriginalInputEvent(type, event, m_stream.epoch().id(), m_sequence + 1);
  switch (result.status) {
  case OriginalInputCapture::Status::Ignored:
    return;
  case OriginalInputCapture::Status::Captured:
    if (result.record && m_stream.push(*result.record)) {
      ++m_sequence;
      return;
    }
    break;
  case OriginalInputCapture::Status::Malformed:
  case OriginalInputCapture::Status::Discontinuity:
    break;
  }
  m_stream.revoke(OriginalInputRevocation::Discontinuity);
}

bool OriginalInputCaptureSource::checkpoint(std::uint64_t timestampNs, std::uint64_t flags) noexcept
{
  if (m_state.load(std::memory_order_acquire) != State::Running || !m_stream.epoch().live())
    return false;
  if (m_sequence == std::numeric_limits<std::uint64_t>::max()) {
    m_stream.revoke(OriginalInputRevocation::Exhausted);
    return false;
  }
  if (!m_stream.push({m_stream.epoch().id(), m_sequence + 1, timestampNs, flags, OriginalInputBoundary::Checkpoint}))
    return false;
  ++m_sequence;
  return true;
}

std::optional<OriginalInputDelivery> OriginalInputCaptureSource::pop() noexcept
{
  return m_stream.pop();
}

OriginalInputEpoch OriginalInputCaptureSource::epoch() const noexcept
{
  if (m_state.load(std::memory_order_acquire) != State::Running || !m_stream.epoch().live())
    return {};
  return m_stream.epoch();
}

bool OriginalInputCaptureSource::emergencyRequested() const noexcept
{
  return m_stream.epoch().emergencyRequested();
}

void OriginalInputCaptureSource::stop() noexcept
{
  // Revocation precedes terminal publication. A racing successful start cannot
  // revive continuity, and a consumer holding an older token sees the stop.
  m_stream.revoke(OriginalInputRevocation::Stopped);
  m_state.store(State::Stopped, std::memory_order_release);
}

} // namespace deskflow::windowshare
