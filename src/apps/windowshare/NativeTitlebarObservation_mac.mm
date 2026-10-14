/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeTitlebarObservation.h"
#include "NativeWindowIdentity_mac.h"

#include <ApplicationServices/ApplicationServices.h>
#include <climits>
#include <cmath>
#include <libproc.h>

namespace deskflow::windowshare::native {
namespace {
template <typename T> struct Owned
{
  T value = nullptr;
  explicit Owned(T value = nullptr) : value(value)
  {
  }
  ~Owned()
  {
    if (value)
      CFRelease(value);
  }
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;
};
CFTypeRef attr(AXUIElementRef element, CFStringRef name)
{
  CFTypeRef result = nullptr;
  if (AXUIElementCopyAttributeValue(element, name, &result) != kAXErrorSuccess)
    return nullptr;
  return result;
}
bool attributeEquals(AXUIElementRef element, CFStringRef name, CFTypeRef expected)
{
  const Owned<CFTypeRef> value(attr(element, name));
  return value.value && CFEqual(value.value, expected);
}
std::optional<Rect> frame(AXUIElementRef window)
{
  const Owned<CFTypeRef> p(attr(window, kAXPositionAttribute)), s(attr(window, kAXSizeAttribute));
  CGPoint point;
  CGSize size;
  if (!p.value || !s.value || CFGetTypeID(p.value) != AXValueGetTypeID() ||
      CFGetTypeID(s.value) != AXValueGetTypeID() ||
      !AXValueGetValue(static_cast<AXValueRef>(p.value), kAXValueTypeCGPoint, &point) ||
      !AXValueGetValue(static_cast<AXValueRef>(s.value), kAXValueTypeCGSize, &size))
    return std::nullopt;
  Rect result{point.x, point.y, size.width, size.height};
  return result.valid() ? std::optional(result) : std::nullopt;
}
std::uint64_t birth(pid_t process)
{
  proc_bsdinfo info{};
  if (process <= 0 || proc_pidinfo(process, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info) ||
      !info.pbi_start_tvsec || info.pbi_start_tvusec >= 1000000)
    return 0;
  return info.pbi_start_tvsec * 1000000 + info.pbi_start_tvusec;
}
struct Window
{
  pid_t process;
  Rect bounds;
};
std::optional<Window> nativeWindow(std::uint32_t window)
{
  const Owned<CFArrayRef> rows(CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, window));
  if (!rows.value || CFArrayGetCount(rows.value) != 1)
    return std::nullopt;
  const auto row = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(rows.value, 0));
  if (!row || CFGetTypeID(row) != CFDictionaryGetTypeID())
    return std::nullopt;
  const auto number = [&](CFStringRef key) -> std::optional<std::int64_t> {
    const auto value = static_cast<CFNumberRef>(CFDictionaryGetValue(row, key));
    std::int64_t result = 0;
    return value && CFGetTypeID(value) == CFNumberGetTypeID() && CFNumberGetValue(value, kCFNumberSInt64Type, &result)
               ? std::optional(result)
               : std::nullopt;
  };
  const auto id = number(kCGWindowNumber), process = number(kCGWindowOwnerPID), layer = number(kCGWindowLayer);
  const auto bounds = static_cast<CFDictionaryRef>(CFDictionaryGetValue(row, kCGWindowBounds));
  CGRect rect;
  if (!id || *id != window || !process || *process <= 0 || *process > INT_MAX || !layer || *layer != 0 || !bounds ||
      CFGetTypeID(bounds) != CFDictionaryGetTypeID() || !CGRectMakeWithDictionaryRepresentation(bounds, &rect) ||
      CFDictionaryGetValue(row, kCGWindowIsOnscreen) != kCFBooleanTrue)
    return std::nullopt;
  const Rect result{rect.origin.x, rect.origin.y, rect.size.width, rect.size.height};
  return result.valid() ? std::optional(Window{static_cast<pid_t>(*process), result}) : std::nullopt;
}
bool sameFrame(Rect a, Rect b)
{
  return std::abs(a.x - b.x) <= 1 && std::abs(a.y - b.y) <= 1 && std::abs(a.width - b.width) <= 1 &&
         std::abs(a.height - b.height) <= 1;
}
} // namespace

struct NativeTitlebarObservation::Impl
{
  Owned<AXUIElementRef> window;
  Snapshot initial{};
  bool live = true;
};
NativeTitlebarObservation::NativeTitlebarObservation() : m_impl(std::make_unique<Impl>())
{
}
NativeTitlebarObservation::~NativeTitlebarObservation() = default;
bool NativeTitlebarObservation::matchesNativeWindow(AXUIElementRef window) const noexcept
{
  return m_impl->live && window && m_impl->window.value && CFEqual(m_impl->window.value, window);
}

std::unique_ptr<NativeTitlebarObservation>
NativeTitlebarObservation::begin(std::uint32_t window, Point press, QString *diagnostic)
{
  const auto fail = [&](const char *reason) -> std::unique_ptr<NativeTitlebarObservation> {
    if (diagnostic)
      *diagnostic = QString::fromLatin1(reason);
    return {};
  };
  if (!window || !std::isfinite(press.x) || !std::isfinite(press.y) || !AXIsProcessTrusted())
    return fail("Exact window, finite event point and existing Accessibility permission required");
  const auto native = nativeWindow(window);
  if (!native || !native->bounds.contains(press))
    return fail("Event point is outside the exact visible native window");
  const auto processBirth = birth(native->process);
  if (!processBirth)
    return fail("Cannot retain native process birth");
  auto result = std::unique_ptr<NativeTitlebarObservation>(new NativeTitlebarObservation);
  result->m_impl->window.value = copyExactAXWindow(native->process, window);
  const auto ax = result->m_impl->window.value;
  if (!ax || !attributeEquals(ax, kAXRoleAttribute, kAXWindowRole) ||
      !attributeEquals(ax, kAXSubroleAttribute, kAXStandardWindowSubrole) ||
      !attributeEquals(ax, kAXMinimizedAttribute, kCFBooleanFalse))
    return fail("Exact standard non-minimized AX window unavailable");
  AXUIElementSetMessagingTimeout(ax, 0.05f);
  const auto initial = frame(ax);
  if (!initial || !sameFrame(*initial, native->bounds))
    return fail("AX and WindowServer frames disagree at the press");
  const Owned<AXUIElementRef> app(AXUIElementCreateApplication(native->process));
  if (!app.value)
    return fail("Cannot create the exact application's AX observer");
  AXUIElementSetMessagingTimeout(app.value, 0.05f);
  AXUIElementRef hitValue = nullptr;
  const auto hitResult = AXUIElementCopyElementAtPosition(app.value, press.x, press.y, &hitValue);
  const Owned<AXUIElementRef> hit(hitValue);
  CGWindowID hitWindow = 0;
  if (hitResult != kAXErrorSuccess || !hit.value || axWindowId(hit.value, hitWindow) != kAXErrorSuccess ||
      hitWindow != window)
    return fail("AX hit does not identify the event's exact native window");
  const Owned<CFTypeRef> titleValue(attr(ax, kAXTitleUIElementAttribute));
  if (!titleValue.value || CFGetTypeID(titleValue.value) != AXUIElementGetTypeID())
    return fail("Native titlebar has no exact title UI element");
  const auto title = static_cast<AXUIElementRef>(titleValue.value);
  const auto titleFrame = frame(title);
  if (!titleFrame || titleFrame->y < initial->y || titleFrame->y + titleFrame->height > initial->y + 80 ||
      press.y < initial->y || press.y >= titleFrame->y + titleFrame->height + 2)
    return fail("Event point is outside the native titlebar band");
  // A close/zoom/minimize button, toolbar control, tab or content view is not
  // a titlebar, even when its coordinates fall in the same horizontal band.
  if (!CFEqual(hit.value, ax) && !CFEqual(hit.value, title))
    return fail("Native titlebar hit is an interactive child or content element");
  if (birth(native->process) != processBirth)
    return fail("Native process changed during the titlebar query");
  result->m_impl->initial = {window, native->process, processBirth, *initial, *initial, false};
  if (diagnostic)
    diagnostic->clear();
  return result;
}

std::optional<NativeTitlebarObservation::Snapshot> NativeTitlebarObservation::read()
{
  auto &impl = *m_impl;
  if (!impl.live)
    return std::nullopt;
  const auto fail = [&]() -> std::optional<Snapshot> {
    impl.live = false;
    return {};
  };
  const auto &saved = impl.initial;
  const auto current = nativeWindow(saved.window);
  const auto actual = frame(impl.window.value);
  CGWindowID id = 0;
  if (!AXIsProcessTrusted() || !current || !actual || current->process != saved.process ||
      birth(current->process) != saved.processBirth || axWindowId(impl.window.value, id) != kAXErrorSuccess ||
      id != saved.window || !sameFrame(*actual, current->bounds) || std::abs(actual->width - saved.initial.width) > 1 ||
      std::abs(actual->height - saved.initial.height) > 1 ||
      !attributeEquals(impl.window.value, kAXMinimizedAttribute, kCFBooleanFalse))
    return fail();
  auto result = saved;
  result.current = *actual;
  result.moved = std::hypot(actual->x - saved.initial.x, actual->y - saved.initial.y) >= 3;
  return result;
}
} // namespace deskflow::windowshare::native
