/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "Core.h"
#include <ApplicationServices/ApplicationServices.h>
#include <QString>
#include <memory>

namespace deskflow::windowshare::native {

// Read-only native eligibility observation for an event-time window ID. The
// caller must obtain that ID from the verified HID/annotation join. This does
// not select a foreground window, claim a native grab, post input or move it.
// Only ordinary AX standard-window titlebars with an exact hit are supported.
class NativeTitlebarObservation final
{
public:
  static std::unique_ptr<NativeTitlebarObservation>
  begin(std::uint32_t window, Point press, QString *diagnostic = nullptr);
  ~NativeTitlebarObservation();
  NativeTitlebarObservation(const NativeTitlebarObservation &) = delete;
  NativeTitlebarObservation &operator=(const NativeTitlebarObservation &) = delete;
  struct Snapshot
  {
    std::uint32_t window;
    std::int64_t process;
    std::uint64_t processBirth;
    Rect initial, current;
    bool moved;
  };
  // Rechecks the retained AX object, native ID, process birth and size. Any
  // failure retires this observation permanently, even if an ID is later reused.
  std::optional<Snapshot> read();
  bool matchesNativeWindow(AXUIElementRef window) const noexcept;

private:
  NativeTitlebarObservation();
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace deskflow::windowshare::native
