/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <ApplicationServices/ApplicationServices.h>
#include <QJsonObject>

namespace deskflow::windowshare {
// Run as the actual packaged Core so TCC evaluates its own identity. This
// creates no tap, IPC endpoint, native input, window capture or settings change.
inline QJsonObject coreMacPermissions(bool request)
{
  if (request && !AXIsProcessTrusted()) {
    const void *key = kAXTrustedCheckOptionPrompt;
    const void *value = kCFBooleanTrue;
    const auto options = CFDictionaryCreate(
        kCFAllocatorDefault, &key, &value, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks
    );
    if (options) {
      AXIsProcessTrustedWithOptions(options);
      CFRelease(options);
    }
  }
  return {{"supported", true}, {"accessibility", bool(AXIsProcessTrusted())}, {"inputStarted", false}};
}
} // namespace deskflow::windowshare
