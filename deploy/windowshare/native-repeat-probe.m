/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: MIT
 */
// Optional macOS diagnostic. Compile with -fobjc-arc -fblocks and the AppKit
// and ApplicationServices frameworks. Uses an owned temporary window, existing
// permissions and self-PID posting only. No global input, sleeps or Qt/Deskflow
// event processing. Raw observations remain failures when a receipt is missing.
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <mach/mach_time.h>
#include <stdio.h>
#include <unistd.h>

static const int64_t tagBase = 0x4453524550505200LL;
static CGEventSourceRef source;
static NSWindow *window;
static CFMachPortRef tap;
static NSInteger stage = -1, observed = 0, exact = 0;
static uint64_t timestamps[6];
static BOOL finishing = NO, prepared = NO;

static NSInteger eventIndex(CGEventRef event)
{
  const int64_t tag = event ? CGEventGetIntegerValueField(event, kCGEventSourceUserData) : 0;
  return (tag & ~0xffLL) == tagBase ? tag & 0xff : -1;
}
static void emit(const char *where, CGEventRef event)
{
  const NSInteger index = eventIndex(event);
  if (index < 0 || index >= 6)
    return;
  printf(
      "{\"stage\":\"%s\",\"index\":%ld,\"annotated\":%s,\"type\":%u,\"repeat\":%lld,"
      "\"key\":%lld,\"timestamp\":\"%llu\",\"originalTimestamp\":\"%llu\","
      "\"applicationActive\":%s,\"windowKey\":%s,\"targetPid\":%d}\n",
      where, (long)index, index >= 3 ? "true" : "false", (unsigned)CGEventGetType(event),
      (long long)CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat),
      (long long)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode),
      (unsigned long long)CGEventGetTimestamp(event), (unsigned long long)timestamps[index],
      NSApp.isActive ? "true" : "false", window.isKeyWindow ? "true" : "false", getpid()
  );
  fflush(stdout);
}
static void finish(void)
{
  if (finishing)
    return;
  finishing = YES;
  // Retire only this diagnostic's private source. No global key state or
  // other application's delivered state is changed.
  if (stage >= 0 && stage < 6 && stage % 3 != 2) {
    CGEventRef up = CGEventCreateKeyboardEvent(source, 7, false);
    if (up) {
      CGEventSetIntegerValueField(up, kCGEventSourceUserData, tagBase | 6);
      CGEventPostToPid(getpid(), up);
      CFRelease(up);
    }
  }
  printf(
      "{\"stage\":\"end\",\"appKitReceipts\":%ld,\"exactTimestampReceipts\":%ld,"
      "\"expected\":6,\"started\":%s,\"selfPidOnly\":true,\"physicalAcceptance\":\"not-assessed\"}\n",
      (long)observed, (long)exact, stage >= 0 ? "true" : "false"
  );
  fflush(stdout);
  CFMachPortInvalidate(tap);
  [window orderOut:nil];
  [NSApp stop:nil];
  [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                      location:NSZeroPoint
                                 modifierFlags:0
                                     timestamp:0
                                  windowNumber:0
                                       context:nil
                                       subtype:0
                                         data1:0
                                         data2:0]
           atStart:NO];
}
static void next(void)
{
  if (finishing)
    return;
  if (++stage == 6) {
    finish();
    return;
  }
  if (!NSApp.isActive || !window.isKeyWindow) {
    printf("{\"stage\":\"owned-focus-lost\",\"index\":%ld}\n", (long)stage);
    finish();
    return;
  }
  const BOOL down = stage % 3 != 2, repeat = stage % 3 == 1;
  CGEventRef event;
  if (stage < 3)
    event = CGEventCreateKeyboardEvent(source, 7, down);
  else {
    NSEvent *native = [NSEvent keyEventWithType:down ? NSEventTypeKeyDown : NSEventTypeKeyUp
                                       location:NSMakePoint(30, 30)
                                  modifierFlags:(NSEventModifierFlags)0x20000000
                                      timestamp:NSProcessInfo.processInfo.systemUptime
                                   windowNumber:window.windowNumber
                                        context:nil
                                     characters:@"x"
                    charactersIgnoringModifiers:@"x"
                                      isARepeat:repeat
                                        keyCode:7];
    event = CGEventCreateCopy(native.CGEvent);
    if (event)
      CGEventSetSource(event, source);
  }
  if (!event) {
    finish();
    return;
  }
  CGEventSetType(event, down ? kCGEventKeyDown : kCGEventKeyUp);
  CGEventSetIntegerValueField(event, kCGKeyboardEventKeycode, 7);
  CGEventSetIntegerValueField(event, kCGKeyboardEventKeyboardType, 41);
  CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, repeat);
  CGEventSetIntegerValueField(event, kCGEventSourceUserData, tagBase | stage);
  CGEventSetFlags(event, 0x20000000);
  const UniChar x = 'x';
  CGEventKeyboardSetUnicodeString(event, 1, &x);
  mach_timebase_info_data_t scale;
  mach_timebase_info(&scale);
  timestamps[stage] = mach_absolute_time() * scale.numer / scale.denom;
  CGEventSetTimestamp(event, timestamps[stage]);
  emit("submitted", event);
  CGEventPostToPid(getpid(), event);
  CFRelease(event);
  const NSInteger expectedStage = stage;
  // Absolute receipt deadline; actual delivery advances immediately. A missing
  // repeat is retained before the following release is posted.
  [NSTimer scheduledTimerWithTimeInterval:0.25
                                  repeats:NO
                                    block:^(NSTimer *timer) {
                                      if (!finishing && stage == expectedStage) {
                                        printf("{\"stage\":\"receipt-timeout\",\"index\":%ld}\n", (long)stage);
                                        fflush(stdout);
                                        next();
                                      }
                                    }];
}
static void beginWhenReady(void)
{
  if (prepared && stage == -1 && !finishing && NSApp.isActive && window.isKeyWindow)
    next();
}
@interface DFRepeatProbeApplication : NSApplication
@end
@implementation DFRepeatProbeApplication
- (void)sendEvent:(NSEvent *)event
{
  CGEventRef raw = event.CGEvent;
  const NSInteger index = eventIndex(raw);
  if (index >= 0 && index < 6) {
    emit("app-send", raw);
    ++observed;
    exact += CGEventGetTimestamp(raw) == timestamps[index];
    dispatch_async(dispatch_get_main_queue(), ^{
      if (!finishing && stage == index)
        next();
    });
  }
  [super sendEvent:event];
}
@end
static CGEventRef observe(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *context)
{
  if (type == kCGEventKeyDown || type == kCGEventKeyUp)
    emit("pid-tap", event);
  return event;
}
int main(void)
{
  @autoreleasepool {
    if (!AXIsProcessTrusted() || !CGPreflightPostEventAccess()) {
      fprintf(stderr, "Owned diagnostic requires existing permissions; no prompt requested\n");
      return 2;
    }
    [DFRepeatProbeApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    tap = CGEventTapCreateForPid(
        getpid(), kCGHeadInsertEventTap, kCGEventTapOptionListenOnly,
        CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp), observe, NULL
    );
    if (!source || !tap)
      return 2;
    CFRunLoopSourceRef tapSource = CFMachPortCreateRunLoopSource(NULL, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), tapSource, kCFRunLoopCommonModes);
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 280, 80)
                                         styleMask:NSWindowStyleMaskTitled
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    window.title = @"Deskflow self-targeted repeat diagnostic";
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(10, 20, 260, 28)];
    [window.contentView addSubview:field];
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    id active = [center addObserverForName:NSApplicationDidBecomeActiveNotification
                                    object:NSApp
                                     queue:nil
                                usingBlock:^(NSNotification *notification) {
                                  beginWhenReady();
                                }];
    id key = [center addObserverForName:NSWindowDidBecomeKeyNotification
                                 object:window
                                  queue:nil
                             usingBlock:^(NSNotification *notification) {
                               beginWhenReady();
                             }];
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:field];
    [NSApp finishLaunching];
    prepared = YES;
    [NSApp activateIgnoringOtherApps:YES];
    dispatch_async(dispatch_get_main_queue(), ^{
      beginWhenReady();
    });
    [NSTimer scheduledTimerWithTimeInterval:3
                                    repeats:NO
                                      block:^(NSTimer *timer) {
                                        finish();
                                      }];
    [NSApp run];
    [center removeObserver:active];
    [center removeObserver:key];
    CFRunLoopRemoveSource(CFRunLoopGetMain(), tapSource, kCFRunLoopCommonModes);
    CFRelease(tapSource);
    CFRelease(tap);
    CFRelease(source);
    return observed == 6 && exact == 6 ? 0 : 3;
  }
}
