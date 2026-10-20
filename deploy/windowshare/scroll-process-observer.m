/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>
#include <libproc.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct Row
{
  uint64_t timestamp, flags, tag, observedTicks;
  int64_t pid, raw[17];
  unsigned type;
};
static struct Row rows[4096];
static unsigned count, printed, dropped, disabled;
static pid_t target;
static struct proc_bsdinfo identity;
static bool ownerLost;
static bool currentOwner(void)
{
  struct proc_bsdinfo fresh;
  return proc_pidinfo(target, PROC_PIDTBSDINFO, 0, &fresh, sizeof(fresh)) == sizeof(fresh) &&
         fresh.pbi_uid == getuid() && fresh.pbi_start_tvsec == identity.pbi_start_tvsec &&
         fresh.pbi_start_tvusec == identity.pbi_start_tvusec;
}
static CGEventRef observe(CGEventTapProxy p, CGEventType t, CGEventRef e, void *c)
{
  (void)p;
  (void)c;
  if (t == kCGEventTapDisabledByTimeout || t == kCGEventTapDisabledByUserInput) {
    ++disabled;
    return e;
  }
  if (!e || CGEventGetIntegerValueField(e, kCGEventSourceUserData) != 0x445357494e503031LL)
    return e;
  if (!currentOwner()) {
    ownerLost = true;
    CFRunLoopStop(CFRunLoopGetCurrent());
    return e;
  }
  if (count >= 4096) {
    ++dropped;
    return e;
  }
  struct Row *r = &rows[count++];
  r->timestamp = CGEventGetTimestamp(e);
  r->flags = CGEventGetFlags(e);
  r->tag = CGEventGetIntegerValueField(e, kCGEventSourceUserData);
  r->pid = CGEventGetIntegerValueField(e, kCGEventSourceUnixProcessID);
  r->type = t;
  r->observedTicks = mach_absolute_time();
  r->raw[0] = t;
  if (t == kCGEventScrollWheel) {
    CGEventField f[] = {kCGScrollWheelEventDeltaAxis2,        kCGScrollWheelEventDeltaAxis1,
                        kCGScrollWheelEventPointDeltaAxis2,   kCGScrollWheelEventPointDeltaAxis1,
                        kCGScrollWheelEventFixedPtDeltaAxis2, kCGScrollWheelEventFixedPtDeltaAxis1,
                        kCGScrollWheelEventScrollPhase,       kCGScrollWheelEventMomentumPhase,
                        kCGScrollWheelEventIsContinuous};
    for (int i = 0; i < 9; ++i)
      r->raw[8 + i] = CGEventGetIntegerValueField(e, f[i]);
  } else if (t == 10 || t == 11 || t == 12) {
    r->raw[1] = CGEventGetIntegerValueField(e, kCGKeyboardEventKeycode);
    r->raw[2] = CGEventGetIntegerValueField(e, kCGKeyboardEventKeyboardType);
    r->raw[3] = CGEventGetIntegerValueField(e, kCGKeyboardEventAutorepeat);
  }
  return e;
}
static void drain(CFRunLoopTimerRef timer, void *context)
{
  (void)timer;
  (void)context;
  for (; printed < count; ++printed) {
    struct Row *r = &rows[printed];
    printf(
        "{\"step\":\"pid-tap-event\",\"timestamp\":\"%llu\",\"flags\":\"%llu\",\"tag\":\"%llx\",\"sourcePid\":%lld,"
        "\"type\":%u,\"observedTicks\":\"%llu\",\"raw\":[",
        (unsigned long long)r->timestamp, (unsigned long long)r->flags, (unsigned long long)r->tag, (long long)r->pid,
        r->type, (unsigned long long)r->observedTicks
    );
    for (int i = 0; i < 17; ++i)
      printf("%s\"%lld\"", i ? "," : "", (long long)r->raw[i]);
    puts("]}");
  }
  fflush(stdout);
  if (!currentOwner()) {
    ownerLost = true;
    CFRunLoopStop(CFRunLoopGetCurrent());
  }
}
// EOF on the harness-owned pipe requests a final drain. No signals, sleeps,
// polling process shutdown or forced termination are needed for the footer.
static void stopRequested(CFFileDescriptorRef descriptor, CFOptionFlags flags, void *context)
{
  (void)flags;
  (void)context;
  char byte;
  if (read(CFFileDescriptorGetNativeDescriptor(descriptor), &byte, 1) == 0)
    CFRunLoopStop(CFRunLoopGetCurrent());
  else {
    ++disabled;
    CFRunLoopStop(CFRunLoopGetCurrent());
  }
}
int main(int argc, char **argv)
{
  @autoreleasepool {
    if (argc != 2)
      return 1;
    char *end = NULL;
    long parsed = strtol(argv[1], &end, 10);
    if (!argv[1][0] || !end || *end || parsed <= 0 || parsed > INT32_MAX)
      return 1;
    target = (pid_t)parsed;
    if (proc_pidinfo(target, PROC_PIDTBSDINFO, 0, &identity, sizeof(identity)) != sizeof(identity) ||
        identity.pbi_uid != getuid() || !identity.pbi_start_tvsec)
      return 1;
    CFMachPortRef tap = CGEventTapCreateForPid(
        target, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly,
        (CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) | CGEventMaskBit(kCGEventFlagsChanged) |
         CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp) |
         CGEventMaskBit(kCGEventLeftMouseDragged) | CGEventMaskBit(kCGEventScrollWheel)),
        observe, NULL
    );
    if (!tap)
      return 2;
    CFRunLoopSourceRef loop = CFMachPortCreateRunLoopSource(NULL, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), loop, kCFRunLoopDefaultMode);
    mach_timebase_info_data_t clock;
    mach_timebase_info(&clock);
    printf(
        "{\"step\":\"observing-owned-fixture\",\"pid\":%d,\"birthMicroseconds\":%llu,\"clockNumer\":%u,\"clockDenom\":%"
        "u,\"scope\":\"diagnostic only; cannot replace AppKit evidence\"}\n",
        target, (unsigned long long)identity.pbi_start_tvsec * 1000000 + identity.pbi_start_tvusec, clock.numer,
        clock.denom
    );
    fflush(stdout);
    CFFileDescriptorRef stop = CFFileDescriptorCreate(NULL, STDIN_FILENO, false, stopRequested, NULL);
    CFRunLoopSourceRef stopSource = stop ? CFFileDescriptorCreateRunLoopSource(NULL, stop, 0) : NULL;
    if (!stopSource)
      return 2;
    CFRunLoopAddSource(CFRunLoopGetCurrent(), stopSource, kCFRunLoopDefaultMode);
    CFFileDescriptorEnableCallBacks(stop, kCFFileDescriptorReadCallBack);
    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent(), 0.1, 0, 0, drain, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopDefaultMode);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 55, false);
    drain(NULL, NULL);
    printf(
        "{\"step\":\"observer-end\",\"records\":%u,\"dropped\":%u,\"disabled\":%u,\"ownerLost\":%s}\n", count, dropped,
        disabled, ownerLost ? "true" : "false"
    );
    fflush(stdout);
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), stopSource, kCFRunLoopDefaultMode);
    CFFileDescriptorInvalidate(stop);
    CFRelease(stopSource);
    CFRelease(stop);
    CFRunLoopTimerInvalidate(timer);
    CFRelease(timer);
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), loop, kCFRunLoopDefaultMode);
    CFMachPortInvalidate(tap);
    CFRelease(loop);
    CFRelease(tap);
    return dropped || disabled || ownerLost ? 3 : 0;
  }
}
