/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "Core.h"
#include <cmath>

namespace deskflow::windowshare::native {

// Call only after rechecking the retained AX object, CG window ID and process
// birth. AX and WindowServer positions are sampled independently during native
// movement, so equality of their origins is not an identity check. Both sizes
// must still match each other and the original titlebar press. This predicate
// alone cannot select or authenticate a window.
inline bool sameNativeTitlebarExtent(Rect initial, Rect accessibility, Rect windowServer)
{
  const auto same = [](Rect a, Rect b) {
    return std::abs(a.width - b.width) <= 1 && std::abs(a.height - b.height) <= 1;
  };
  return initial.valid() && accessibility.valid() && windowServer.valid() && same(initial, accessibility) &&
         same(initial, windowServer) && same(accessibility, windowServer);
}

} // namespace deskflow::windowshare::native
