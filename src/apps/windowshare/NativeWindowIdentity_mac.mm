/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeWindowIdentity_mac.h"

#include <chrono>
#include <dlfcn.h>
#include <utility>

namespace deskflow::windowshare::native {
namespace {
using WindowLookup = AXError (*)(AXUIElementRef, CGWindowID *);

WindowLookup windowLookup() noexcept
{
  // Resolve from the system framework, never from an arbitrary loaded image.
  // Keep the framework loaded for the lifetime of the function pointer.
  static const auto lookup = [] {
    auto framework = dlopen(
        "/System/Library/Frameworks/ApplicationServices.framework/Frameworks/HIServices.framework/HIServices",
        RTLD_LAZY | RTLD_LOCAL
    );
    return framework ? reinterpret_cast<WindowLookup>(dlsym(framework, "_AXUIElementGetWindow")) : nullptr;
  }();
  return lookup;
}
} // namespace

bool axWindowIdentityAvailable() noexcept
{
  return windowLookup() != nullptr;
}

AXError axWindowId(AXUIElementRef element, CGWindowID &window) noexcept
{
  window = 0;
  if (!element || CFGetTypeID(element) != AXUIElementGetTypeID())
    return kAXErrorIllegalArgument;
  const auto lookup = windowLookup();
  if (!lookup)
    return kAXErrorNotImplemented;
  CGWindowID observed = 0;
  const auto error = lookup(element, &observed);
  if (error != kAXErrorSuccess)
    return error;
  if (!observed)
    return kAXErrorNoValue;
  window = observed;
  return kAXErrorSuccess;
}

AXUIElementRef copyExactAXWindow(pid_t process, CGWindowID window, QString *diagnostic, bool *retryable)
{
  if (retryable)
    *retryable = false;
  const auto fail = [&](QString reason, bool retry = false) -> AXUIElementRef {
    if (diagnostic)
      *diagnostic = std::move(reason);
    if (retryable)
      *retryable = retry;
    return nullptr;
  };
  if (process <= 0 || !window)
    return fail("Missing native process/window identity");
  if (!axWindowIdentityAvailable())
    return fail("Private macOS AX window identity API is unavailable");

  const auto app = AXUIElementCreateApplication(process);
  if (!app)
    return fail("Cannot create the selected process's Accessibility reference");
  // The initial request can start the application's accessibility bridge.
  // This is a bounded control-thread query, never an event-tap callback.
  AXUIElementSetMessagingTimeout(app, 1.0f);
  CFTypeRef raw = nullptr;
  const auto status = AXUIElementCopyAttributeValue(app, kAXWindowsAttribute, &raw);
  CFRelease(app);
  if (status != kAXErrorSuccess || !raw || CFGetTypeID(raw) != CFArrayGetTypeID()) {
    if (raw)
      CFRelease(raw);
    return fail(
        QString("AX window list status %1").arg(status), status == kAXErrorCannotComplete || status == kAXErrorNoValue
    );
  }
  const auto windows = static_cast<CFArrayRef>(raw);
  const auto count = CFArrayGetCount(windows);
  constexpr CFIndex maxWindows = 256;
  if (!count || count > maxWindows) {
    CFRelease(windows);
    return fail(QString("AX window list count %1 is unavailable or exceeds the limit").arg(count), !count);
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  AXUIElementRef match = nullptr;
  bool incomplete = false, duplicate = false;
  for (CFIndex index = 0; index < count; ++index) {
    if (std::chrono::steady_clock::now() >= deadline) {
      incomplete = true;
      break;
    }
    const auto candidate = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(windows, index));
    if (!candidate || CFGetTypeID(candidate) != AXUIElementGetTypeID()) {
      incomplete = true;
      break;
    }
    AXUIElementSetMessagingTimeout(candidate, 0.1f);
    pid_t actualProcess = 0;
    CGWindowID actualWindow = 0;
    if (AXUIElementGetPid(candidate, &actualProcess) != kAXErrorSuccess || actualProcess != process ||
        axWindowId(candidate, actualWindow) != kAXErrorSuccess) {
      incomplete = true;
      break;
    }
    if (actualWindow == window) {
      // The SPI also accepts contained elements (for example buttons). A
      // containing-window ID alone cannot authorize window placement/focus.
      CFTypeRef role = nullptr;
      const auto roleStatus = AXUIElementCopyAttributeValue(candidate, kAXRoleAttribute, &role);
      const bool isWindow = roleStatus == kAXErrorSuccess && role && CFGetTypeID(role) == CFStringGetTypeID() &&
                            CFEqual(role, kAXWindowRole);
      if (role)
        CFRelease(role);
      if (!isWindow) {
        incomplete = true;
        break;
      }
      if (match) {
        duplicate = true;
        break;
      }
      match = candidate;
    }
  }
  // Revalidate the selected object after enumeration, before retaining it.
  CGWindowID confirmed = 0;
  if (!incomplete && !duplicate && match && std::chrono::steady_clock::now() < deadline &&
      axWindowId(match, confirmed) == kAXErrorSuccess && confirmed == window &&
      std::chrono::steady_clock::now() < deadline)
    CFRetain(match);
  else
    match = nullptr;
  CFRelease(windows);
  if (!match)
    return fail(
        duplicate ? "Multiple AX objects claim the selected native window"
                  : "Exact AX/native window identity is unavailable or changed during enumeration",
        !duplicate
    );
  if (diagnostic)
    diagnostic->clear();
  return match;
}

} // namespace deskflow::windowshare::native
