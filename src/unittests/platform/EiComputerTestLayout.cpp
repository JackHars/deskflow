/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/EiComputer.h"

#include <array>
#include <cstddef>

// This translation unit belongs to platform only in test-enabled builds.
// It deliberately has no production API or separately copied feature flags.
std::array<std::size_t, 2> eiComputerPlatformLayout() noexcept
{
  return {sizeof(deskflow::EiComputer), alignof(deskflow::EiComputer)};
}
