/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <cstdint>
#include <type_traits>

namespace deskflow::windowshare {

// Trivial event metadata only. It owns no pointer, process, route or authority.
struct OriginalInputStamp
{
  std::uint64_t epoch = 0, sequence = 0;
  std::uint16_t part = 0;
  bool operator==(const OriginalInputStamp &) const = default;
  explicit operator bool() const noexcept
  {
    return epoch && sequence;
  }
};
static_assert(std::is_trivially_copyable_v<OriginalInputStamp>);
inline constexpr std::uint16_t maximumOriginalInputParts = 16;

struct OriginalInputTranslation
{
  OriginalInputStamp stamp;
  std::uint16_t parts = 0;
  bool uncertain = false;
};

// Producer-thread scope around one existing native-event translation. Existing
// Event creations obtain numbered stamps; no extra event, allocation or queue
// operation occurs. Only Event's relevant-input constructors call next().
class OriginalInputStampScope final
{
public:
  explicit OriginalInputStampScope(OriginalInputStamp stamp) noexcept : m_previous(s_current), m_value{stamp, 0, false}
  {
    if (m_previous)
      m_previous->m_value.uncertain = true;
    s_current = this;
  }
  ~OriginalInputStampScope()
  {
    s_current = m_previous;
  }
  OriginalInputStampScope(const OriginalInputStampScope &) = delete;
  OriginalInputStampScope &operator=(const OriginalInputStampScope &) = delete;
  static OriginalInputStamp next() noexcept
  {
    if (!s_current || !s_current->m_value.stamp)
      return {};
    auto &value = s_current->m_value;
    if (value.parts == maximumOriginalInputParts) {
      value.uncertain = true;
      return {};
    }
    auto stamp = value.stamp;
    stamp.part = ++value.parts;
    return stamp;
  }
  // Filtering may synchronously create configured actions. Their provenance
  // is configuration, never the physical capture being forwarded.
  class Pause final
  {
  public:
    Pause() noexcept : m_saved(s_current)
    {
      s_current = nullptr;
    }
    ~Pause()
    {
      s_current = m_saved;
    }
    Pause(const Pause &) = delete;
    Pause &operator=(const Pause &) = delete;

  private:
    OriginalInputStampScope *m_saved;
  };
  static void uncertain() noexcept
  {
    if (s_current)
      s_current->m_value.uncertain = true;
  }
  OriginalInputTranslation translation() const noexcept
  {
    return m_value;
  }

private:
  inline static thread_local OriginalInputStampScope *s_current = nullptr;
  OriginalInputStampScope *m_previous;
  OriginalInputTranslation m_value;
};

} // namespace deskflow::windowshare
