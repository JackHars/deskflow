/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <cstdint>

namespace deskflow::windowshare {

// These markers are shared by the separate capture host and Deskflow's platform
// backend. An event marker prevents routing loops; it is not authentication.
inline constexpr std::int64_t syntheticEventTag = 0x445357494e503031LL;
inline constexpr unsigned virtualDisplayVendor = 0x4453;
inline constexpr unsigned virtualDisplayProduct = 0x5753;
inline constexpr unsigned virtualDisplaySerial = 0x00010001;

constexpr bool isWindowshareSyntheticEvent(std::int64_t tag) noexcept
{
  return tag == syntheticEventTag;
}

// Match the complete identity: other virtual displays and displays with the same
// vendor must remain part of Deskflow's ordinary cursor topology.
constexpr bool isWindowshareHoldingDisplay(unsigned vendor, unsigned product, unsigned serial) noexcept
{
  return vendor == virtualDisplayVendor && product == virtualDisplayProduct && serial == virtualDisplaySerial;
}

} // namespace deskflow::windowshare
