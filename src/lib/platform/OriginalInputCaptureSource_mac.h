/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalInput_mac.h"
#include "windowshare/OriginalInputCapturedInbox.h"
#include "windowshare/OriginalInputRoute.h"
#include "windowshare/OriginalPointerJoin.h"
#include "windowshare/WindowDragMotionGate.h"

namespace deskflow::windowshare {

// Allocate before starting the event tap and retain until producer and consumer
// have stopped. One instance is one capture epoch: stop, overflow, malformed
// input or tap disable cannot be repaired by calling start again. An epoch is
// continuity evidence only, never a physical-neutral snapshot or input grant.
class OriginalInputCaptureSource final
{
public:
  enum class FailureStage : std::uint8_t
  {
    OpenCapture,
    NativeEvent,
    StreamAdmission,
    Translation,
    TranslationCapacity,
    Checkpoint,
    ConsumerJoin
  };
  struct Failure
  {
    FailureStage stage{};
    OriginalInputCapture::Status nativeStatus = OriginalInputCapture::Status::Ignored;
    std::uint32_t callbackType = 0, eventType = 0;
    std::uint64_t sequence = 0, timestampNs = 0, previousTimestampNs = 0, flags = 0;
    std::int64_t sourcePid = 0, mouseSubtype = 0, scrollPhase = 0, momentumPhase = 0, continuous = 0;
    std::array<std::int64_t, 3> thirdAxis{};
    const char *stageName() const noexcept
    {
      switch (stage) {
      case FailureStage::OpenCapture:
        return "unfinished-previous-capture";
      case FailureStage::NativeEvent:
        return "native-event-rejected";
      case FailureStage::StreamAdmission:
        return "capture-stream-admission";
      case FailureStage::Translation:
        return "translation-mismatch";
      case FailureStage::TranslationCapacity:
        return "translation-capacity";
      case FailureStage::Checkpoint:
        return "checkpoint";
      case FailureStage::ConsumerJoin:
        return "consumer-join";
      }
      return "unknown";
    }
  };
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
  // Optional passive application-stage observer. Both tap callbacks run on
  // the same producer thread; the existing Session is the only consumer.
  bool startAnnotations() noexcept;
  void captureAnnotation(CGEventType type, CGEventRef event) noexcept;
  std::optional<OriginalPointerAnnotation> popAnnotation() noexcept;
  bool annotationsLive() const noexcept;
  WindowDragMotionGate &windowDragMotionGate() noexcept
  {
    return m_dragMotion;
  }
  std::shared_ptr<const std::atomic<std::uint64_t>> capturePosition() const noexcept
  {
    return m_capturePosition;
  }
  // Producer only, immediately after beginCapture for that same event.
  bool suppressWindowDragMotion() const noexcept
  {
    return m_suppressMotion && m_stream.epoch().live();
  }

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
  // One immutable diagnostic, published without allocation or logging in the
  // event tap. The consumer prints it; it never grants or repairs continuity.
  std::optional<Failure> failure() const noexcept;
  void stop() noexcept;

private:
  void recordFailure(
      FailureStage stage, std::uint64_t sequence, std::uint64_t previousTimestampNs = 0,
      OriginalInputCapture::Status status = OriginalInputCapture::Status::Ignored,
      CGEventType callbackType = kCGEventNull, CGEventRef event = nullptr
  ) noexcept;
  OriginalInputTransitionEvidence classifyTransition(const OriginalInputRecord &) noexcept;
  enum class State : std::uint8_t
  {
    Ready,
    Running,
    Stopped
  };
  OriginalInputStream m_stream;
  std::shared_ptr<std::atomic<std::uint64_t>> m_capturePosition = std::make_shared<std::atomic<std::uint64_t>>(0);
  WindowDragMotionGate m_dragMotion{m_stream.epoch().id()};
  bool m_suppressMotion = false;
  std::atomic<State> m_state{State::Ready};
  // Empty -> Writing -> Published. Multiple failure sites may race; only the
  // winning writer touches m_failure, and no writer changes it after release.
  enum class FailureState : std::uint8_t
  {
    Empty,
    Writing,
    Published
  };
  std::atomic<FailureState> m_failureState{FailureState::Empty};
  Failure m_failure;
  std::atomic<State> m_annotationState{State::Ready};
  std::array<OriginalPointerAnnotation, OriginalPointerJoin::capacity> m_annotations;
  std::atomic<std::uint64_t> m_annotationsWritten{0}, m_annotationsRead{0};
  std::uint64_t m_sequence = 0;
  std::uint64_t m_lastTimestamp = 0;
  OriginalInputStamp m_openStamp;
  OriginalInputTransitionEvidence m_openTransition;
  std::optional<OriginalPointerObservation> m_openPointer;
  // Zero means Unknown or held, never "up by default". Only a captured Up
  // establishes a released control; each subsequent Down consumes that edge.
  std::array<std::uint64_t, originalKey::keyCount> m_releasedKeys{};
  std::array<std::uint64_t, originalInputButtonCount> m_releasedButtons{};
  OriginalScrollState m_scroll = OriginalScrollState::Unknown;
  struct Completed
  {
    OriginalInputTranslation translation;
    OriginalInputTransitionEvidence transition;
    std::optional<OriginalPointerObservation> pointer;
    bool windowDragMotionHeld = false;
  };
  std::array<Completed, OriginalInputStream::capacity> m_completed;
  std::atomic<std::uint64_t> m_completedWritten{0}, m_completedRead{0};
  std::shared_ptr<OriginalInputRoutes> m_routes = std::make_shared<OriginalInputRoutes>();
  std::shared_ptr<OriginalInputCapturedInbox> m_completedInput =
      std::make_shared<OriginalInputCapturedInbox>(m_stream.epoch());
};

} // namespace deskflow::windowshare
