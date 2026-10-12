/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "windowshare/OriginalKeyboard.h"

#include <CoreGraphics/CoreGraphics.h>

namespace deskflow::windowshare {

struct OriginalKeyCapture
{
  enum class Status
  {
    Ignored,
    Captured,
    Malformed
  };
  Status status = Status::Ignored;
  std::optional<OriginalKeySample> sample;
};

// Read only the supplied event. Does not install a tap, inspect physical key
// state, request TCC, or post input. Capture before OSXKeyState translates keys.
// The caller supplies a monotonic capture sequence. Synthetic-tagged events are
// ignored; malformed supported events require route revocation/resynchronizing.
OriginalKeyCapture captureOriginalKeyboardEvent(CGEventRef event, std::uint64_t sequence) noexcept;

// Construct retained, tagged events without posting them. The caller must supply
// a private event source (never combined-session/HID-system state) and CFRelease
// the result. The captured keyboard type is set explicitly on every event;
// actual layout/IME interpretation still requires a later integration test.
// Only an authenticated local lease may eventually authorize posting elsewhere.
CGEventRef createOriginalKeyboardEvent(const OriginalKeyEvent &event, CGEventSourceRef privateSource) noexcept;
CGEventRef createOriginalKeyboardRelease(
    const OriginalKeyRelease &release, std::uint64_t timestampNs, CGEventSourceRef privateSource
) noexcept;

} // namespace deskflow::windowshare
