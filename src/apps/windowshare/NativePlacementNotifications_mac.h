/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <ApplicationServices/ApplicationServices.h>
#include <array>

namespace deskflow::windowshare {

// Notifications only wake the existing placement verifier. Success here is
// not placement evidence: the same owner, AX frame, WindowServer frame and
// absolute deadline must still agree, including when notifications are absent.
// Injecting the registrar lets regression tests exercise these outcomes without
// subscribing to another application or creating a native window.
inline auto placementNotificationNames()
{
  return std::array<CFStringRef, 5>{
      kAXMovedNotification, kAXResizedNotification, kAXWindowMiniaturizedNotification,
      kAXWindowDeminiaturizedNotification, kAXUIElementDestroyedNotification
  };
}
template <class Register, class Report>
bool registerPlacementNotification(CFStringRef name, Register &&subscribe, Report &&report)
{
  const auto status = subscribe(name);
  report(name, status);
  // A busy/unresponsive AX bridge may not register a notification. The Host
  // watchdog already wakes verification without it, just as for an app that
  // does not implement notifications. Do not retry registration or the move,
  // extend the deadline, or treat this as a successful frame observation.
  return status == kAXErrorSuccess || status == kAXErrorNotificationUnsupported || status == kAXErrorCannotComplete;
}
template <class Register, class Report> bool registerPlacementNotifications(Register &&subscribe, Report &&report)
{
  for (const auto name : placementNotificationNames()) {
    if (!registerPlacementNotification(name, subscribe, report))
      return false;
  }
  return true;
}

} // namespace deskflow::windowshare
