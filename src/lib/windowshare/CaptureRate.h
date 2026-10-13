/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <optional>
#include <string_view>

namespace deskflow::windowshare {
inline constexpr unsigned defaultCaptureRate = 30;
constexpr bool validCaptureRate(unsigned rate) noexcept
{
  return rate == 30 || rate == 60;
}
constexpr std::optional<unsigned> parseCaptureRate(std::string_view value) noexcept
{
  if (value == "30")
    return 30;
  if (value == "60")
    return 60;
  return std::nullopt;
}
constexpr unsigned effectiveCaptureRate(unsigned selected, bool active, bool visible) noexcept
{
  return validCaptureRate(selected) ? (active ? selected : (visible ? 10U : 2U)) : 0;
}
} // namespace deskflow::windowshare
