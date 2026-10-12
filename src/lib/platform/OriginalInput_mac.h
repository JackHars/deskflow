/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "windowshare/OriginalInput.h"

#include <CoreGraphics/CoreGraphics.h>

namespace deskflow::windowshare {

struct OriginalInputCapture
{
  enum class Status
  {
    Ignored,
    Captured,
    Malformed,
    Discontinuity
  };
  Status status = Status::Ignored;
  std::optional<OriginalInputRecord> record;
};

// Read only the supplied event, using no allocating containers or physical
// state queries. Pass the callback type separately: tap-disabled notifications
// report Discontinuity even with a null event, and never invent a record or
// timestamp. Synthetic events and unsupported event types are Ignored.
// Malformed supported events require revocation, not silent skipping. Only a
// Captured result contains a record and consumes the caller's proposed sequence.
// Key transitions classify syntax/side flags only; they establish neither
// physical continuity, ownership, nor the neutral startup barrier.
OriginalInputCapture captureOriginalInputEvent(
    CGEventType callbackType, CGEventRef event, std::uint64_t epoch, std::uint64_t sequence
) noexcept;

// Construct retained, loop-tagged events without posting them. Require a
// private source; CFRelease the returned event. Pointer target coordinates are
// explicit geometry, not delivery authority. Unsupported or lossy construction
// returns null. Boundaries cannot be constructed as OS events. No source
// location, Unicode text, tablet fields, or third scroll axis is transferred.
CGEventRef createOriginalInputEvent(
    const OriginalInputRecord &record, CGPoint targetPoint, CGEventSourceRef privateSource
) noexcept;

// Construct only an owned key/button release, preserving the press's code,
// keyboard type/button and click count, with release flags/time/target. Does
// not invent a new capture sequence or grant authority to the named owner.
CGEventRef createOriginalInputRelease(
    const OriginalInputRelease &release, std::uint64_t timestampNs, CGPoint targetPoint, CGEventSourceRef privateSource
) noexcept;

} // namespace deskflow::windowshare
