/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "Core.h"
#include <ApplicationServices/ApplicationServices.h>
#include <optional>

namespace deskflow::windowshare {

struct NativeWindowState
{
  Rect frame;
  std::optional<bool> minimized;
};

// Read one fresh batch, never a cached observation. AX may report per-field
// failures even when the call succeeds. Only an unsupported/absent minimized
// attribute is optional (for associated windows); geometry must be complete.
template <class Read> std::optional<NativeWindowState> readNativeWindowState(AXUIElementRef window, Read &&read)
{
  if (!window)
    return {};
  const void *names[] = {kAXPositionAttribute, kAXSizeAttribute, kAXMinimizedAttribute};
  auto attributes = CFArrayCreate(nullptr, names, 3, &kCFTypeArrayCallBacks);
  if (!attributes)
    return {};
  CFArrayRef values = nullptr;
  const auto status = read(window, attributes, 0, &values);
  CFRelease(attributes);
  struct Release
  {
    CFArrayRef values;
    ~Release()
    {
      if (values)
        CFRelease(values);
    }
  } release{values};
  if (status != kAXErrorSuccess || !values || CFGetTypeID(values) != CFArrayGetTypeID() || CFArrayGetCount(values) != 3)
    return {};
  const auto position = CFArrayGetValueAtIndex(values, 0), size = CFArrayGetValueAtIndex(values, 1),
             minimized = CFArrayGetValueAtIndex(values, 2);
  if (!position || !size || !minimized || CFGetTypeID(position) != AXValueGetTypeID() ||
      CFGetTypeID(size) != AXValueGetTypeID())
    return {};
  CGPoint point{};
  CGSize dimensions{};
  if (!AXValueGetValue(static_cast<AXValueRef>(position), kAXValueTypeCGPoint, &point) ||
      !AXValueGetValue(static_cast<AXValueRef>(size), kAXValueTypeCGSize, &dimensions))
    return {};
  NativeWindowState result{{point.x, point.y, dimensions.width, dimensions.height}, {}};
  if (!result.frame.valid())
    return {};
  if (CFGetTypeID(minimized) == CFBooleanGetTypeID()) {
    result.minimized = CFBooleanGetValue(static_cast<CFBooleanRef>(minimized));
  } else if (CFGetTypeID(minimized) == AXValueGetTypeID()) {
    AXError error = kAXErrorFailure;
    if (!AXValueGetValue(static_cast<AXValueRef>(minimized), kAXValueTypeAXError, &error) ||
        (error != kAXErrorAttributeUnsupported && error != kAXErrorNoValue))
      return {};
  } else if (CFGetTypeID(minimized) != CFNullGetTypeID()) {
    return {};
  }
  return result;
}

inline std::optional<NativeWindowState> readNativeWindowState(AXUIElementRef window)
{
  return readNativeWindowState(window, AXUIElementCopyMultipleAttributeValues);
}

} // namespace deskflow::windowshare
