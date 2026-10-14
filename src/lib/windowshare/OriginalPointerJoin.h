/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalInputRoute.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace deskflow::windowshare {

// An observation at the application-annotated tap. It has no capture sequence
// and is never an input record or permission to post an event.
struct OriginalPointerAnnotation
{
  std::uint64_t sourceTimestampNs = 0, flags = 0;
  OriginalInputButton button;
  double x = 0, y = 0;
  std::uint32_t windowUnderPointer = 0, windowHandlingPointer = 0;
  std::int64_t eventNumber = 0, sourceProcess = 0, sourceState = 0, mouseSubtype = 0;
};

// One consumer joins copies from the existing HID consumer with a separate
// passive annotation queue. Neither queue is allowed to delay normal input.
// The output names an event-time window, not a titlebar or native grab owner.
class OriginalPointerJoin final
{
public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::size_t capacity = 32;
  static constexpr auto budget = std::chrono::milliseconds(50);
  struct Match
  {
    OriginalInputCaptured captured;
    std::optional<OriginalPointerAnnotation> annotation;
    std::shared_ptr<const std::atomic_bool> continuity;
    bool live() const noexcept
    {
      return captured.input.live() && continuity && continuity->load(std::memory_order_acquire);
    }
    std::uint32_t window() const noexcept
    {
      return live() && annotation && annotation->windowUnderPointer == annotation->windowHandlingPointer
                 ? annotation->windowHandlingPointer
                 : 0;
    }
  };

  explicit OriginalPointerJoin(OriginalInputEpoch epoch) : m_epoch(std::move(epoch))
  {
  }
  OriginalPointerJoin(const OriginalPointerJoin &) = delete;
  OriginalPointerJoin &operator=(const OriginalPointerJoin &) = delete;
  ~OriginalPointerJoin()
  {
    revoke();
  }
  bool live() const noexcept
  {
    return m_epoch.live() && m_live->load(std::memory_order_acquire);
  }
  void revoke() noexcept
  {
    m_live->store(false, std::memory_order_release);
  }
  bool capture(const OriginalInputCaptured &input, Clock::time_point now = Clock::now()) noexcept
  {
    if (!live())
      return false;
    const auto &record = input.input.record;
    const auto *button = std::get_if<OriginalInputButton>(&record.payload);
    if (!button || button->button != 0)
      return true;
    if (!input.input.live() || record.epoch != m_epoch.id() || !validOriginalInputRecord(record) || !input.pointer ||
        !input.pointer->matches(record) || record.sequence <= m_lastSequence) {
      revoke();
      return false;
    }
    m_lastSequence = record.sequence;
    // A late observation cannot revive a retired or expired pair.
    if (record.sourceTimestampNs <= m_retiredTime)
      return true;
    auto *slot = find(record.sourceTimestampNs, input.pointer->eventNumber, button->down, now);
    if (!slot || slot->captured) {
      revoke();
      return false;
    }
    slot->captured = input;
    return validate(*slot);
  }
  bool annotate(const OriginalPointerAnnotation &value, Clock::time_point now = Clock::now()) noexcept
  {
    if (!live())
      return false;
    if (!value.sourceTimestampNs || value.button.button != 0 || value.eventNumber < 0 ||
        (value.button.down && !value.button.clickCount) ||
        value.button.clickCount > std::numeric_limits<std::int32_t>::max() || !std::isfinite(value.x) ||
        !std::isfinite(value.y)) {
      revoke();
      return false;
    }
    if (value.sourceTimestampNs <= m_retiredTime) {
      for (const auto &retired : m_retired)
        if (retired.first == value.sourceTimestampNs && retired.second) {
          revoke();
          return false;
        }
      return true;
    }
    auto *slot = find(value.sourceTimestampNs, value.eventNumber, value.button.down, now);
    if (!slot || slot->annotation) {
      revoke();
      return false;
    }
    slot->annotation = value;
    return validate(*slot);
  }
  std::optional<Match> pop(Clock::time_point now = Clock::now()) noexcept
  {
    if (!live())
      return {};
    // Retire in source-time order even when the annotation queue is drained
    // before the raw consumer. An expired pair reports no window identity.
    for (std::size_t n = 0; n < capacity; ++n) {
      Slot *first = nullptr;
      for (auto &slot : m_slots)
        if (slot.time && (!first || slot.time < first->time))
          first = &slot;
      if (!first)
        return {};
      const bool expired = now >= first->deadline;
      if (!expired && (!first->captured || !first->annotation))
        return {};
      std::optional<Match> result;
      if (first->captured)
        result = Match{*first->captured, expired ? std::nullopt : first->annotation, m_live};
      m_retiredTime = std::max(m_retiredTime, first->time);
      m_retired[m_retiredNext++ % capacity] = {first->time, !expired && bool(result)};
      *first = {};
      if (result)
        return result;
    }
    return {};
  }

private:
  struct Slot
  {
    std::uint64_t time = 0;
    std::int64_t number = 0;
    bool down = false;
    Clock::time_point deadline;
    std::optional<OriginalInputCaptured> captured;
    std::optional<OriginalPointerAnnotation> annotation;
  };
  Slot *find(std::uint64_t time, std::int64_t number, bool down, Clock::time_point now) noexcept
  {
    Slot *empty = nullptr;
    for (auto &slot : m_slots) {
      if (slot.time == time) {
        // An equal timestamp with another event identity is ambiguous.
        return slot.number == number && slot.down == down ? &slot : nullptr;
      }
      if (!slot.time && !empty)
        empty = &slot;
    }
    if (empty) {
      empty->time = time;
      empty->number = number;
      empty->down = down;
      empty->deadline = now + budget;
    }
    return empty;
  }
  bool validate(const Slot &slot) noexcept
  {
    if (!slot.captured || !slot.annotation)
      return true;
    const auto &record = slot.captured->input.record;
    const auto &pointer = *slot.captured->pointer;
    const auto &button = std::get<OriginalInputButton>(record.payload);
    const auto &annotation = *slot.annotation;
    if (record.flags != annotation.flags || button.clickCount != annotation.button.clickCount ||
        pointer.x != annotation.x || pointer.y != annotation.y || pointer.sourceProcess != annotation.sourceProcess ||
        pointer.sourceState != annotation.sourceState || pointer.mouseSubtype != annotation.mouseSubtype) {
      revoke();
      return false;
    }
    return true;
  }
  OriginalInputEpoch m_epoch;
  std::shared_ptr<std::atomic_bool> m_live = std::make_shared<std::atomic_bool>(true);
  std::array<Slot, capacity> m_slots;
  std::array<std::pair<std::uint64_t, bool>, capacity> m_retired{};
  std::size_t m_retiredNext = 0;
  std::uint64_t m_lastSequence = 0, m_retiredTime = 0;
};

} // namespace deskflow::windowshare
