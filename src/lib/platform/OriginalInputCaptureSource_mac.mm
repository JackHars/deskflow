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

OriginalInputStamp OriginalInputCaptureSource::beginCapture(CGEventType type, CGEventRef event) noexcept
{
  if (m_state.load(std::memory_order_acquire) != State::Running)
    return {};
  // Recognize the escape chord even after overflow/revocation and even when
  // side-specific modifier flags are ambiguous. This capture validates syntax
  // and ignores our synthetic marker without inventing a neutral observation.
  if (type == kCGEventKeyDown && event && CGEventGetType(event) == type) {
    const auto keyboard = captureOriginalKeyboardEvent(event, 1);
    if (keyboard.sample && originalEmergencyChord(*keyboard.sample)) {
      m_stream.revoke(OriginalInputRevocation::Emergency);
      return {};
    }
  }
  if (m_openStamp) {
    m_stream.revoke(OriginalInputRevocation::Discontinuity);
    return {};
  }
  if (!m_stream.epoch().live())
    return {};
  if (m_sequence == std::numeric_limits<std::uint64_t>::max()) {
    m_stream.revoke(OriginalInputRevocation::Exhausted);
    return {};
  }
  const auto result = captureOriginalInputEvent(type, event, m_stream.epoch().id(), m_sequence + 1);
  switch (result.status) {
  case OriginalInputCapture::Status::Ignored:
    return {};
  case OriginalInputCapture::Status::Captured:
    if (result.record && m_stream.push(*result.record)) {
      ++m_sequence;
      m_openStamp = {m_stream.epoch().id(), m_sequence, 0};
      return m_openStamp;
    }
    break;
  case OriginalInputCapture::Status::Malformed:
  case OriginalInputCapture::Status::Discontinuity:
    break;
  }
  m_stream.revoke(OriginalInputRevocation::Discontinuity);
  return {};
}

void OriginalInputCaptureSource::completeCapture(OriginalInputTranslation translation) noexcept
{
  if (!translation.stamp)
    return;
  if (!m_stream.epoch().live())
    return;
  if (translation.stamp != m_openStamp || translation.parts > maximumOriginalInputParts) {
    m_stream.revoke(OriginalInputRevocation::Discontinuity);
    return;
  }
  const auto written = m_completedWritten.load(std::memory_order_relaxed);
  if (written - m_completedRead.load(std::memory_order_acquire) >= m_completed.size()) {
    m_stream.revoke(OriginalInputRevocation::Overflow);
    return;
  }
  m_completed[written % m_completed.size()] = translation;
  m_openStamp = {};
  m_completedWritten.store(written + 1, std::memory_order_release);
}
void OriginalInputCaptureSource::capture(CGEventType type, CGEventRef event) noexcept
{
  completeCapture({beginCapture(type, event), 0, false});
}

bool OriginalInputCaptureSource::checkpoint(std::uint64_t timestampNs, std::uint64_t flags) noexcept
{
  if (m_state.load(std::memory_order_acquire) != State::Running || !m_stream.epoch().live())
    return false;
  if (m_openStamp) {
    m_stream.revoke(OriginalInputRevocation::Discontinuity);
    return false;
  }
  if (m_sequence == std::numeric_limits<std::uint64_t>::max()) {
    m_stream.revoke(OriginalInputRevocation::Exhausted);
    return false;
  }
  if (!m_stream.push({m_stream.epoch().id(), m_sequence + 1, timestampNs, flags, OriginalInputBoundary::Checkpoint}))
    return false;
  ++m_sequence;
  m_openStamp = {m_stream.epoch().id(), m_sequence, 0};
  completeCapture({m_openStamp, 0, false});
  return m_stream.epoch().live();
}

std::optional<OriginalInputCaptured> OriginalInputCaptureSource::popCaptured() noexcept
{
  if (!m_stream.epoch().live())
    return {};
  const auto read = m_completedRead.load(std::memory_order_relaxed);
  if (read == m_completedWritten.load(std::memory_order_acquire))
    return {};
  const auto translation = m_completed[read % m_completed.size()];
  const auto input = m_stream.pop();
  m_completedRead.store(read + 1, std::memory_order_release);
  if (!input)
    return {};
  if (translation.stamp != OriginalInputStamp{input->record.epoch, input->record.sequence, 0}) {
    m_stream.revoke(OriginalInputRevocation::Discontinuity);
    return {};
  }
  return OriginalInputCaptured{*input, translation};
}
std::optional<OriginalInputDelivery> OriginalInputCaptureSource::pop() noexcept
{
  const auto captured = popCaptured();
  return captured ? std::optional(captured->input) : std::nullopt;
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
  m_routes->revoke();
  m_stream.revoke(OriginalInputRevocation::Stopped);
  m_state.store(State::Stopped, std::memory_order_release);
}

} // namespace deskflow::windowshare
