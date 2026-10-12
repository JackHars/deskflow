/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <CoreFoundation/CoreFoundation.h>

// The caller retains the published loop through the producer's join. A direct
// CFRunLoopStop can be lost between publication and CFRunLoopRun: there is no
// active run to stop yet. A queued block runs inside that first activation.
inline void stopOSXEventTapRunLoop(CFRunLoopRef loop)
{
  CFRunLoopPerformBlock(loop, kCFRunLoopDefaultMode, ^{
    CFRunLoopStop(loop);
  });
  CFRunLoopWakeUp(loop);
}
