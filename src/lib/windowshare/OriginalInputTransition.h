/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalInput.h"
#include <utility>

namespace deskflow::windowshare {

class OriginalInputCaptureSource;

// Acquisition evidence for one serialized logical capture transition. This
// does not assert physical neutrality or drain upstream OS input. Every control
// starts unknown; a key/button needs a captured Up before its next fresh Down.
// Existing owners, their releases, modifier flags and native delivery authority
// remain independent requirements. Only the actual source may issue evidence.
class OriginalInputTransitionEvidence final
{
public:
  enum class Kind : std::uint8_t
  {
    None,
    KeyDown,
    ButtonDown,
    ScrollBegin,
    DiscreteScroll
  };
  OriginalInputTransitionEvidence() = default;
  Kind kind() const noexcept
  {
    return m_kind;
  }
  std::uint64_t precedingReleaseSequence() const noexcept
  {
    return m_release;
  }
  bool permitsAcquisition(const OriginalInputDelivery &delivery) const noexcept
  {
    const auto &record = delivery.record;
    if (m_kind == Kind::None || !delivery.live() || !m_epoch.sameEpoch(delivery.epoch) ||
        record.epoch != m_epoch.id() || record.sequence != m_sequence || record.sourceTimestampNs != m_timestamp ||
        record.flags != m_flags || !validOriginalInputRecord(record))
      return false;
    if (m_kind == Kind::KeyDown) {
      const auto *key = std::get_if<OriginalInputKey>(&record.payload);
      return key && key->code == m_control && key->keyboardType == m_keyboardType &&
             key->transition == OriginalKeyTransition::Down && !key->repeat && m_release && m_release < m_sequence;
    }
    if (m_kind == Kind::ButtonDown) {
      const auto *button = std::get_if<OriginalInputButton>(&record.payload);
      return button && button->button == m_control && button->down && m_release && m_release < m_sequence;
    }
    const auto *scroll = std::get_if<OriginalInputScroll>(&record.payload);
    return scroll && scroll->momentum == OriginalMomentumPhase::None &&
           ((m_kind == Kind::ScrollBegin && scroll->continuous && scroll->phase == OriginalScrollPhase::Began) ||
            (m_kind == Kind::DiscreteScroll && !scroll->continuous && scroll->phase == OriginalScrollPhase::None));
  }

private:
  friend class OriginalInputCaptureSource;
  OriginalInputTransitionEvidence(
      OriginalInputEpoch epoch, const OriginalInputRecord &record, Kind kind, std::uint64_t release = 0
  ) noexcept
      : m_epoch(std::move(epoch)),
        m_sequence(record.sequence),
        m_timestamp(record.sourceTimestampNs),
        m_flags(record.flags),
        m_release(release),
        m_kind(kind)
  {
    if (const auto *key = std::get_if<OriginalInputKey>(&record.payload)) {
      m_control = key->code;
      m_keyboardType = key->keyboardType;
    } else if (const auto *button = std::get_if<OriginalInputButton>(&record.payload))
      m_control = button->button;
  }
  OriginalInputEpoch m_epoch;
  std::uint64_t m_sequence = 0, m_timestamp = 0, m_flags = 0, m_release = 0;
  std::uint32_t m_keyboardType = 0;
  std::uint16_t m_control = 0;
  Kind m_kind = Kind::None;
};

} // namespace deskflow::windowshare
