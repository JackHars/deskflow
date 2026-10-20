/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: MIT
 */
// Optional macOS diagnostic: compile with AppKit and ApplicationServices.
// Posts only to this process, using a private event source and a listen-only
// process tap. No user window, global event posting, sleep, or input authority.
// A complete probe reports observations; it does not certify preservation.
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <mach/mach_time.h>
#include <stdio.h>
#include <unistd.h>
static const uint64_t ages[] = {0, 50000000, 90000000, 99000000, 101000000, 125000000, 250000000};
static unsigned indexValue = 0;
static uint64_t submitted = 0;
static CGEventSourceRef source;
static CFMachPortRef tap;
static CFRunLoopRef loop;
static bool finished = false;
static unsigned preservedCount = 0;
static uint64_t nowNs(void)
{
  mach_timebase_info_data_t scale;
  mach_timebase_info(&scale);
  return mach_absolute_time() * scale.numer / scale.denom;
}
static void next(void)
{
  if (indexValue == sizeof(ages) / sizeof(ages[0])) {
    finished = true;
    CFRunLoopStop(loop);
    return;
  }
  CGEventRef event = CGEventCreateScrollWheelEvent(source, kCGScrollEventUnitPixel, 2, 1, 0);
  CGEventSetIntegerValueField(event, kCGEventSourceUserData, 0x445354494d455052LL);
  submitted = nowNs() - ages[indexValue];
  CGEventSetTimestamp(event, submitted);
  printf(
      "{\"stage\":\"submitted\",\"index\":%u,\"ageNs\":%llu,\"timestamp\":%llu,\"readback\":%llu,\"targetPid\":%d}\n",
      indexValue, (unsigned long long)ages[indexValue], (unsigned long long)submitted,
      (unsigned long long)CGEventGetTimestamp(event), getpid()
  );
  fflush(stdout);
  // This probe can only address itself; it never posts to a global event tap.
  CGEventPostToPid(getpid(), event);
  CFRelease(event);
}
static CGEventRef observe(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *context)
{
  if (type != kCGEventScrollWheel || CGEventGetIntegerValueField(event, kCGEventSourceUserData) != 0x445354494d455052LL)
    return event;
  uint64_t actual = CGEventGetTimestamp(event);
  if (actual == submitted)
    ++preservedCount;
  printf(
      "{\"stage\":\"received\",\"index\":%u,\"timestamp\":%llu,\"preserved\":%s,\"shiftNs\":%lld}\n", indexValue,
      (unsigned long long)actual, actual == submitted ? "true" : "false", (long long)(actual - submitted)
  );
  fflush(stdout);
  ++indexValue;
  CFRunLoopPerformBlock(loop, kCFRunLoopDefaultMode, ^{
    next();
  });
  CFRunLoopWakeUp(loop);
  return event;
}
int main(void)
{
  @autoreleasepool {
    if (!AXIsProcessTrusted() || !CGPreflightPostEventAccess()) {
      fprintf(stderr, "Owned diagnostic requires existing accessibility/posting access; no prompt requested\n");
      return 2;
    }
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    loop = CFRunLoopGetCurrent();
    tap = CGEventTapCreateForPid(
        getpid(), kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, CGEventMaskBit(kCGEventScrollWheel), observe, NULL
    );
    if (!source || !tap) {
      fprintf(stderr, "Owned diagnostic could not obtain source/tap\n");
      return 2;
    }
    CFRunLoopSourceRef runSource = CFMachPortCreateRunLoopSource(NULL, tap, 0);
    CFRunLoopAddSource(loop, runSource, kCFRunLoopDefaultMode);
    CFRunLoopPerformBlock(loop, kCFRunLoopDefaultMode, ^{
      next();
    });
    // One absolute timeout, no sleeps or timed event spacing.
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2, false);
    CFMachPortInvalidate(tap);
    CFRunLoopRemoveSource(loop, runSource, kCFRunLoopDefaultMode);
    CFRelease(runSource);
    CFRelease(tap);
    CFRelease(source);
    printf(
        "{\"stage\":\"end\",\"diagnosticOnly\":true,\"complete\":%s,\"received\":%u,\"preserved\":%u,"
        "\"physicalAcceptance\":\"not-assessed\"}\n",
        finished ? "true" : "false", indexValue, preservedCount
    );
    return finished ? 0 : 3;
  }
}
