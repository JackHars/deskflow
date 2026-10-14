/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "KeyboardRoute.h"
#include "LocalInputMapping.h"
#include "OriginalInput.h"

#include <algorithm>
#include <array>
#include <limits>

namespace deskflow::windowshare {

// Owner-thread bookkeeping for an independently authorized native endpoint.
// No capture epoch is fabricated here: the authenticated channel supplies the
// epoch number, and its coordinator must prove acquisition separately. This
// delivered-owner bookkeeping never asserts physical neutrality.
// prepare() never claims a delivered press. The adapter checks its exact native
// owner and deadline again, posts synchronously without dispatching callbacks,
// then calls submitted(). A cancelled preparation cannot revive its old owner.
class NativeOriginalInputState final
{
public:
  struct Prepared
  {
    std::uint64_t token = 0;
    OriginalInputRecord record;
    LocalInputMappedPoint point;
    std::optional<OriginalInputRecord> press;
    bool posts = true;
  };
  struct Cleanup
  {
    KeyboardRouteBinding binding;
    LocalInputMappedPoint point;
    std::uint64_t timestamp = 0;
    std::vector<OriginalInputRelease> releases;
    // A terminal output for a delivered gesture, never a captured event or a
    // new capture sequence. Its retained stamp is only the original owner key.
    std::optional<OriginalInputRecord> scroll;
  };

  bool establish(const KeyboardRouteBinding &binding, std::uint64_t captureEpoch)
  {
    if (!validKeyboardRouteBinding(binding) || binding.scope != ForwardingScope::All || !captureEpoch)
      return false;
    if (m_binding)
      return *m_binding == binding && m_captureEpoch == captureEpoch;
    if (binding.generation <= m_lastGeneration || captureEpoch < m_captureEpoch)
      return false;
    m_binding = binding;
    m_lastGeneration = binding.generation;
    if (captureEpoch != m_captureEpoch)
      m_sequence = m_timestamp = 0;
    m_captureEpoch = captureEpoch;
    m_flags = 0;
    m_scrollState = OriginalScrollState::Idle; // Delivered state only, not physical state.
    return true;
  }
  bool current(const KeyboardRouteBinding &binding, std::uint64_t captureEpoch) const
  {
    return m_binding == binding && captureEpoch && m_captureEpoch == captureEpoch;
  }
  const std::optional<KeyboardRouteBinding> &binding() const
  {
    return m_binding;
  }
  bool empty() const
  {
    return std::none_of(m_pressed.begin(), m_pressed.end(), [](const auto &press) { return press.has_value(); }) &&
           !m_scroll;
  }
  std::optional<Prepared> prepare(
      const KeyboardRouteBinding &binding, const OriginalInputRecord &record, const LocalInputMappedPoint &point,
      double sourceWidth, double sourceHeight
  )
  {
    if (!current(binding, record.epoch) || m_pending || !validOriginalInputRecord(record) ||
        record.sequence <= m_sequence || record.sourceTimestampNs < m_timestamp || !liveLocalInputMappedPoint(point) ||
        point.width != sourceWidth || point.height != sourceHeight ||
        m_token == std::numeric_limits<std::uint64_t>::max())
      return std::nullopt;
    Pending pending;
    pending.prepared = {m_token + 1, record, point, std::nullopt, true};
    pending.scrollState = m_scrollState;
    auto expectedFlags = flagsFor();
    if (const auto *key = std::get_if<OriginalInputKey>(&record.payload)) {
      if (originalEmergencyChord(originalInputKeyEvent(record)->sample))
        return std::nullopt;
      pending.slot = key->code;
      pending.transition = key->transition;
    } else if (const auto *button = std::get_if<OriginalInputButton>(&record.payload)) {
      pending.slot = originalKey::keyCount + button->button;
      pending.transition = button->down ? OriginalKeyTransition::Down : OriginalKeyTransition::Up;
    } else if (const auto *motion = std::get_if<OriginalInputMotion>(&record.payload)) {
      if (motion->dragButton != originalInputButtonCount) {
        const auto &press = m_pressed[originalKey::keyCount + motion->dragButton];
        if (!press)
          return std::nullopt;
        pending.prepared.press = press;
      }
    } else if (const auto *scroll = std::get_if<OriginalInputScroll>(&record.payload)) {
      if (!advanceScroll(*scroll, pending.scrollState))
        return std::nullopt;
    } else {
      if (std::get<OriginalInputBoundary>(record.payload) != OriginalInputBoundary::Checkpoint)
        return std::nullopt;
      pending.prepared.posts = false;
    }
    if (pending.slot < m_pressed.size()) {
      const auto &press = m_pressed[pending.slot];
      switch (pending.transition) {
      case OriginalKeyTransition::Down:
        if (press)
          return std::nullopt;
        pending.prepared.press = record;
        if (pending.slot < originalKey::keyCount)
          expectedFlags |= originalKeyModifierFlags(static_cast<std::uint16_t>(pending.slot));
        break;
      case OriginalKeyTransition::Repeat:
      case OriginalKeyTransition::Up:
        if (!press)
          return std::nullopt;
        if (const auto *key = std::get_if<OriginalInputKey>(&record.payload)) {
          if (std::get<OriginalInputKey>(press->payload).keyboardType != key->keyboardType)
            return std::nullopt;
        } else if (const auto clicks = std::get<OriginalInputButton>(record.payload).clickCount;
                   clicks && std::get<OriginalInputButton>(press->payload).clickCount != clicks) {
          // Zero is a native drag-ending up with no click count. It still
          // needs this exact route's retained button press; nonzero counts
          // must match that press. Do not rewrite the captured release.
          return std::nullopt;
        }
        pending.prepared.press = press;
        if (pending.transition == OriginalKeyTransition::Up)
          expectedFlags = flagsFor(pending.slot);
        break;
      case OriginalKeyTransition::Toggle:
        break;
      }
    }
    if ((expectedFlags & originalKey::momentaryFlags) != (record.flags & originalKey::momentaryFlags))
      return std::nullopt;
    m_pending = std::move(pending);
    ++m_token;
    return m_pending->prepared;
  }
  bool pending(std::uint64_t token) const
  {
    return m_binding && m_pending && token && m_pending->prepared.token == token;
  }
  bool submitted(std::uint64_t token)
  {
    if (!pending(token))
      return false;
    const auto &operation = *m_pending;
    const auto &record = operation.prepared.record;
    if (operation.slot < m_pressed.size()) {
      if (operation.transition == OriginalKeyTransition::Down)
        m_pressed[operation.slot] = record;
      else if (operation.transition == OriginalKeyTransition::Up)
        m_pressed[operation.slot].reset();
    }
    m_sequence = record.sequence;
    m_timestamp = record.sourceTimestampNs;
    m_flags = record.flags;
    m_point = operation.prepared.point;
    if (std::holds_alternative<OriginalInputScroll>(record.payload)) {
      m_scrollState = operation.scrollState;
      if (m_scrollState == OriginalScrollState::Gesture || m_scrollState == OriginalScrollState::Momentum ||
          m_scrollState == OriginalScrollState::Unknown)
        m_scroll = record;
      else
        m_scroll.reset();
    }
    m_pending.reset();
    return true;
  }
  std::optional<Cleanup> revoke(const KeyboardRouteBinding &binding)
  {
    if (m_binding != binding)
      return std::nullopt;
    const auto exactBinding = binding;
    Cleanup cleanup{exactBinding, m_point, m_timestamp, {}, std::nullopt};
    cleanup.releases.reserve(m_pressed.size());
    // Invalidate before a caller can post cleanup or invoke a callback.
    m_binding.reset();
    m_pending.reset();
    if (m_scroll) {
      cleanup.scroll = *m_scroll;
      auto &scroll = std::get<OriginalInputScroll>(cleanup.scroll->payload);
      scroll.lines = scroll.points = scroll.fixed16_16 = {};
      scroll.phase =
          m_scrollState == OriginalScrollState::Momentum ? OriginalScrollPhase::None : OriginalScrollPhase::Cancelled;
      scroll.momentum =
          m_scrollState == OriginalScrollState::Momentum ? OriginalMomentumPhase::Ended : OriginalMomentumPhase::None;
      cleanup.scroll->flags = m_flags & ~originalKey::capsPhysicalFlags;
      m_scroll.reset();
    }
    // Ordinary keys and buttons release before modifier sides. An opposite
    // side remains in the flags until its own delivered press is released.
    for (const bool modifiers : {false, true}) {
      for (std::size_t slot = 0; slot != m_pressed.size(); ++slot) {
        auto &press = m_pressed[slot];
        if (!press || (slot < originalKey::keyCount && originalKeyIsModifier(slot)) != modifiers)
          continue;
        const auto saved = *press;
        press.reset();
        cleanup.releases.push_back({exactBinding.generation, saved, flagsFor()});
      }
    }
    return cleanup;
  }

private:
  struct Pending
  {
    Prepared prepared;
    std::size_t slot = originalKey::keyCount + originalInputButtonCount;
    OriginalKeyTransition transition = OriginalKeyTransition::Toggle;
    OriginalScrollState scrollState = OriginalScrollState::Idle;
  };
  std::uint64_t flagsFor(std::size_t excluding = originalKey::keyCount + originalInputButtonCount) const
  {
    auto flags = m_flags & ~(originalKey::momentaryFlags | originalKey::capsPhysicalFlags);
    for (std::size_t code = 0; code != originalKey::keyCount; ++code)
      if (code != excluding && m_pressed[code])
        flags |= originalKeyModifierFlags(static_cast<std::uint16_t>(code));
    return flags;
  }
  static bool advanceScroll(const OriginalInputScroll &scroll, OriginalScrollState &state)
  {
    using Phase = OriginalScrollPhase;
    using Momentum = OriginalMomentumPhase;
    using State = OriginalScrollState;
    switch (scroll.phase) {
    case Phase::MayBegin:
      if (state != State::Idle && state != State::PendingMomentum)
        return false;
      state = State::Unknown; // A delivered MayBegin owns a cancellable gesture.
      return true;
    case Phase::Began:
      if (state != State::Idle && state != State::PendingMomentum && state != State::Unknown)
        return false;
      state = State::Gesture;
      return true;
    case Phase::Changed:
    case Phase::Ended:
      if (state != State::Gesture)
        return false;
      if (scroll.phase == Phase::Ended)
        state = State::PendingMomentum;
      return true;
    case Phase::Cancelled:
      if (state != State::Gesture && state != State::Unknown)
        return false;
      state = State::Idle;
      return true;
    case Phase::None:
      break;
    }
    switch (scroll.momentum) {
    case Momentum::Began:
      if (state != State::PendingMomentum)
        return false;
      state = State::Momentum;
      return true;
    case Momentum::Continued:
    case Momentum::Ended:
      if (state != State::Momentum)
        return false;
      if (scroll.momentum == Momentum::Ended)
        state = State::Idle;
      return true;
    case Momentum::None:
      return state == State::Idle || state == State::PendingMomentum;
    }
    return false;
  }
  std::optional<KeyboardRouteBinding> m_binding;
  std::uint64_t m_captureEpoch = 0, m_lastGeneration = 0, m_token = 0, m_sequence = 0, m_timestamp = 0, m_flags = 0;
  std::array<std::optional<OriginalInputRecord>, originalKey::keyCount + originalInputButtonCount> m_pressed;
  LocalInputMappedPoint m_point;
  OriginalScrollState m_scrollState = OriginalScrollState::Idle;
  std::optional<OriginalInputRecord> m_scroll;
  std::optional<Pending> m_pending;
};

} // namespace deskflow::windowshare
