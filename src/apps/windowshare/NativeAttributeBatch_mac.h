/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <ApplicationServices/ApplicationServices.h>
#include <array>
#include <utility>

namespace deskflow::windowshare {

// Own one fresh AX reply. Per-attribute errors are missing observations, never
// values or authority. Nothing is retained for a later input event.
class NativeAttributeBatch
{
public:
  explicit NativeAttributeBatch(CFArrayRef values = nullptr) : m_values(values)
  {
  }
  NativeAttributeBatch(const NativeAttributeBatch &) = delete;
  NativeAttributeBatch &operator=(const NativeAttributeBatch &) = delete;
  NativeAttributeBatch(NativeAttributeBatch &&other) noexcept : m_values(std::exchange(other.m_values, nullptr))
  {
  }
  ~NativeAttributeBatch()
  {
    if (m_values)
      CFRelease(m_values);
  }
  CFTypeRef get(CFIndex index) const
  {
    if (!m_values || index < 0 || index >= CFArrayGetCount(m_values))
      return nullptr;
    const auto value = CFArrayGetValueAtIndex(m_values, index);
    if (!value || CFGetTypeID(value) == CFNullGetTypeID())
      return nullptr;
    if (CFGetTypeID(value) == AXValueGetTypeID() &&
        AXValueGetType(static_cast<AXValueRef>(value)) == kAXValueTypeAXError)
      return nullptr;
    return value;
  }
  CFTypeRef copy(CFIndex index) const
  {
    const auto value = get(index);
    return value ? CFRetain(value) : nullptr;
  }

private:
  CFArrayRef m_values;
};

template <std::size_t N, class Read>
NativeAttributeBatch readNativeAttributes(AXUIElementRef element, const std::array<CFStringRef, N> &names, Read &&read)
{
  if (!element)
    return NativeAttributeBatch{};
  std::array<const void *, N> items;
  for (std::size_t i = 0; i < N; ++i)
    items[i] = names[i];
  auto attributes = CFArrayCreate(nullptr, items.data(), N, &kCFTypeArrayCallBacks);
  if (!attributes)
    return NativeAttributeBatch{};
  CFArrayRef values = nullptr;
  const auto status = read(element, attributes, 0, &values);
  CFRelease(attributes);
  if (status != kAXErrorSuccess || !values || CFGetTypeID(values) != CFArrayGetTypeID() ||
      CFArrayGetCount(values) != CFIndex(N)) {
    if (values)
      CFRelease(values);
    return NativeAttributeBatch{};
  }
  return NativeAttributeBatch{values};
}

template <std::size_t N>
NativeAttributeBatch readNativeAttributes(AXUIElementRef element, const std::array<CFStringRef, N> &names)
{
  return readNativeAttributes(element, names, AXUIElementCopyMultipleAttributeValues);
}

} // namespace deskflow::windowshare
