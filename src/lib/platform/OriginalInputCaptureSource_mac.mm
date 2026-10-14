/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "OriginalInputCaptureSource_mac.h"
#include "OriginalKeyboard_mac.h"

#include <limits>
#include <utility>

namespace deskflow::windowshare {

OriginalInputCaptureSource::OriginalInputCaptureSource()
{
  inputEvidenceInitialize(InputEvidenceQueue::Translation, this, OriginalInputStream::capacity);
}

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
  InputEvidenceCaptureActivity evidenceActivity;
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
    // Preserve the source identity before the first production queue can lose it.
    if (result.record)
      if (auto *evidence = inputEvidence())
        evidence->source(*result.record);
    if (result.record && m_stream.push(*result.record)) {
      m_openTransition = classifyTransition(*result.record);
      m_openPointer = result.pointer;
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

bool OriginalInputCaptureSource::startAnnotations() noexcept
{
  auto ready = State::Ready;
  return epoch().live() && m_annotationState.compare_exchange_strong(ready, State::Running, std::memory_order_acq_rel);
}
bool OriginalInputCaptureSource::annotationsLive() const noexcept
{
  return m_annotationState.load(std::memory_order_acquire) == State::Running && epoch().live();
}
void OriginalInputCaptureSource::captureAnnotation(CGEventType type, CGEventRef event) noexcept
{
  if (!annotationsLive())
    return;
  if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
    m_annotationState.store(State::Stopped, std::memory_order_release);
    return;
  }
  if (type != kCGEventLeftMouseDown && type != kCGEventLeftMouseUp)
    return;
  // The dummy stamp validates native syntax only. It is never published as
  // raw input; the join must obtain the actual HID stamp from the sole consumer.
  const auto captured = captureOriginalInputEvent(type, event, 1, 1);
  if (captured.status == OriginalInputCapture::Status::Ignored)
    return;
  const auto written = m_annotationsWritten.load(std::memory_order_relaxed);
  if (!captured.record || !captured.pointer ||
      written - m_annotationsRead.load(std::memory_order_acquire) >= m_annotations.size()) {
    m_annotationState.store(State::Stopped, std::memory_order_release);
    return;
  }
  const auto &r = *captured.record;
  const auto &p = *captured.pointer;
  m_annotations[written % m_annotations.size()] = {
      r.sourceTimestampNs,
      r.flags,
      std::get<OriginalInputButton>(r.payload),
      p.x,
      p.y,
      p.windowUnderPointer,
      p.windowHandlingPointer,
      p.eventNumber,
      p.sourceProcess,
      p.sourceState,
      p.mouseSubtype
  };
  m_annotationsWritten.store(written + 1, std::memory_order_release);
}
std::optional<OriginalPointerAnnotation> OriginalInputCaptureSource::popAnnotation() noexcept
{
  if (!annotationsLive())
    return {};
  const auto read = m_annotationsRead.load(std::memory_order_relaxed);
  if (read == m_annotationsWritten.load(std::memory_order_acquire))
    return {};
  const auto value = m_annotations[read % m_annotations.size()];
  m_annotationsRead.store(read + 1, std::memory_order_release);
  return value;
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
  InputEvidenceMutation evidence(InputEvidenceQueue::Translation, this, 1, 0);
  m_completed[written % m_completed.size()] = {translation, m_openTransition, m_openPointer};
  m_openStamp = {};
  m_openTransition = {};
  m_openPointer.reset();
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

OriginalInputTransitionEvidence
OriginalInputCaptureSource::classifyTransition(const OriginalInputRecord &record) noexcept
{
  using Kind = OriginalInputTransitionEvidence::Kind;
  if (const auto *key = std::get_if<OriginalInputKey>(&record.payload)) {
    auto &released = m_releasedKeys[key->code];
    if (key->transition == OriginalKeyTransition::Up) {
      released = record.sequence;
      return {};
    }
    const auto previous = std::exchange(released, 0);
    // Repeats, duplicate downs and direction-ambiguous Caps toggles cannot
    // acquire, even if legacy filtering later emits a different transition.
    if (previous && key->transition == OriginalKeyTransition::Down && !key->repeat)
      return {m_stream.epoch(), record, Kind::KeyDown, previous};
    return {};
  }
  if (const auto *button = std::get_if<OriginalInputButton>(&record.payload)) {
    auto &released = m_releasedButtons[button->button];
    if (!button->down) {
      released = record.sequence;
      return {};
    }
    const auto previous = std::exchange(released, 0);
    return previous ? OriginalInputTransitionEvidence(m_stream.epoch(), record, Kind::ButtonDown, previous)
                    : OriginalInputTransitionEvidence{};
  }
  if (const auto *scroll = std::get_if<OriginalInputScroll>(&record.payload)) {
    if (!scroll->continuous)
      return {m_stream.epoch(), record, Kind::DiscreteScroll};
    if (scroll->phase == OriginalScrollPhase::Began && scroll->momentum == OriginalMomentumPhase::None) {
      const bool fresh = m_scroll == OriginalScrollState::Unknown || m_scroll == OriginalScrollState::Idle;
      m_scroll = OriginalScrollState::Gesture;
      return fresh ? OriginalInputTransitionEvidence(m_stream.epoch(), record, Kind::ScrollBegin)
                   : OriginalInputTransitionEvidence{};
    }
    if (scroll->phase == OriginalScrollPhase::Cancelled || scroll->momentum == OriginalMomentumPhase::Ended)
      m_scroll = OriginalScrollState::Idle;
    else if (scroll->momentum == OriginalMomentumPhase::Began || scroll->momentum == OriginalMomentumPhase::Continued)
      m_scroll = OriginalScrollState::Momentum;
    else if (scroll->phase == OriginalScrollPhase::Ended)
      m_scroll = OriginalScrollState::PendingMomentum;
    else if (scroll->phase == OriginalScrollPhase::Changed)
      m_scroll = OriginalScrollState::Gesture;
  }
  // Motion never creates a button owner. A named drag must resolve through
  // the exact press ledger, including an old-owner tombstone.
  return {};
}

std::optional<OriginalInputCaptured> OriginalInputCaptureSource::popCaptured() noexcept
{
  if (!m_stream.epoch().live())
    return {};
  const auto read = m_completedRead.load(std::memory_order_relaxed);
  if (read == m_completedWritten.load(std::memory_order_acquire))
    return {};
  const auto completed = m_completed[read % m_completed.size()];
  const auto &translation = completed.translation;
  const auto input = m_stream.pop();
  {
    InputEvidenceMutation evidence(InputEvidenceQueue::Translation, this, 0, 1);
    m_completedRead.store(read + 1, std::memory_order_release);
  }
  if (!input)
    return {};
  if (translation.stamp != OriginalInputStamp{input->record.epoch, input->record.sequence, 0}) {
    m_stream.revoke(OriginalInputRevocation::Discontinuity);
    return {};
  }
  return OriginalInputCaptured{*input, translation, completed.transition, completed.pointer};
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
  m_annotationState.store(State::Stopped, std::memory_order_release);
  // Revocation precedes terminal publication. A racing successful start cannot
  // revive continuity, and a consumer holding an older token sees the stop.
  m_completedInput->revoke();
  m_routes->revoke();
  m_stream.revoke(OriginalInputRevocation::Stopped);
  m_state.store(State::Stopped, std::memory_order_release);
}

} // namespace deskflow::windowshare
