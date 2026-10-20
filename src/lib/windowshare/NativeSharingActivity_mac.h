/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#import <Foundation/Foundation.h>

namespace deskflow::windowshare::native {
// Hidden Core and Host processes still serve interactive input and video.
// Keep App Nap from throttling them, but allow normal system/display sleep.
// Each export or private Core session owns its own scoped process token.
// This scheduling hint never supplies input authority or extends a deadline.
class SharingActivity final
{
public:
  static constexpr NSActivityOptions options = NSActivityUserInitiatedAllowingIdleSystemSleep;

  explicit SharingActivity(NSProcessInfo *process = NSProcessInfo.processInfo) : m_process(process)
  {
    m_token = [m_process beginActivityWithOptions:options
                                           reason:@"Sharing an interactive window with another computer"];
  }
  ~SharingActivity()
  {
    if (m_token)
      [m_process endActivity:m_token];
  }
  SharingActivity(const SharingActivity &) = delete;
  SharingActivity &operator=(const SharingActivity &) = delete;
  bool active() const
  {
    return m_token != nil;
  }

private:
  NSProcessInfo *m_process;
  id<NSObject> m_token;
};
} // namespace deskflow::windowshare::native
