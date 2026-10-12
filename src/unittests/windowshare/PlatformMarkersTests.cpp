/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "PlatformMarkers.h"

#include <iostream>
#include <limits>
#include <stdexcept>

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#endif

using namespace deskflow::windowshare;

namespace {
void check(bool condition, const char *message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

#ifdef __APPLE__
void checkQuartzEvent(CGEventRef event)
{
  check(event != nullptr, "create Quartz event without installing a tap");
  check(
      !isWindowshareSyntheticEvent(CGEventGetIntegerValueField(event, kCGEventSourceUserData)),
      "ordinary Quartz event remains routable"
  );
  CGEventSetIntegerValueField(event, kCGEventSourceUserData, syntheticEventTag);
  CGEventRef copy = CGEventCreateCopy(event);
  CFRelease(event);
  check(copy != nullptr, "copy Quartz event");
  const auto tag = CGEventGetIntegerValueField(copy, kCGEventSourceUserData);
  CFRelease(copy);
  check(isWindowshareSyntheticEvent(tag), "complete 64-bit marker survives Quartz event copy");
}
#endif
} // namespace

int main()
{
  try {
    check(isWindowshareSyntheticEvent(syntheticEventTag), "capture-host event bypasses Deskflow routing");
    check(!isWindowshareSyntheticEvent(0), "physical input remains routable");
    check(!isWindowshareSyntheticEvent(-1), "unrelated synthetic input remains routable");
    check(
        !isWindowshareSyntheticEvent(std::numeric_limits<std::int64_t>::max()), "other source marker remains routable"
    );
    for (unsigned bit = 0; bit < 63; ++bit) {
      check(
          !isWindowshareSyntheticEvent(syntheticEventTag ^ (std::int64_t{1} << bit)),
          "all 64-bit marker fields participate in loop suppression"
      );
    }
    check(
        !isWindowshareSyntheticEvent(syntheticEventTag ^ std::numeric_limits<std::int64_t>::min()),
        "marker sign bit participates in loop suppression"
    );

    check(
        isWindowshareHoldingDisplay(virtualDisplayVendor, virtualDisplayProduct, virtualDisplaySerial),
        "holding display excluded from physical cursor topology"
    );
    check(!isWindowshareHoldingDisplay(0, 0, 0), "unidentified physical display retained");
    check(
        !isWindowshareHoldingDisplay(virtualDisplayVendor, virtualDisplayProduct, virtualDisplaySerial + 1),
        "different virtual display serial retained"
    );
    check(
        !isWindowshareHoldingDisplay(virtualDisplayVendor, virtualDisplayProduct + 1, virtualDisplaySerial),
        "different virtual display product retained"
    );
    check(
        !isWindowshareHoldingDisplay(virtualDisplayVendor + 1, virtualDisplayProduct, virtualDisplaySerial),
        "different virtual display vendor retained"
    );

#ifdef __APPLE__
    checkQuartzEvent(CGEventCreateKeyboardEvent(nullptr, 0, true));
    checkQuartzEvent(CGEventCreateKeyboardEvent(nullptr, 0, false));
    checkQuartzEvent(CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseDown, CGPointZero, kCGMouseButtonLeft));
    checkQuartzEvent(CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseUp, CGPointZero, kCGMouseButtonLeft));
    checkQuartzEvent(CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseDragged, CGPointZero, kCGMouseButtonLeft));
    checkQuartzEvent(CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, CGPointZero, kCGMouseButtonLeft));
    checkQuartzEvent(CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitPixel, 1, 1));
#endif
    std::cout << "PASS: platform event and holding-display markers\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
