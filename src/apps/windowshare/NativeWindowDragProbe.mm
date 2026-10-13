/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeWindowDragProbeState.h"
#include "PlatformMarkers.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unistd.h>

// Exercise the production private-source/window annotation constructor. This
// executable never constructs a NativeHost, starts capture, or moves any other
// application's window. Only --interactive can create the owned probe window.
namespace deskflow::windowshare::native {
bool targetedPointerAvailable();
CGEventRef targetedPointerEvent(
    CGEventSourceRef source, CGWindowID window, deskflow::windowshare::Rect frame, deskflow::windowshare::Point point,
    CGEventType type, std::uint32_t button, CGEventFlags modifiers, std::int64_t eventNumber, int clicks
);
} // namespace deskflow::windowshare::native

using namespace deskflow::windowshare;
namespace {
using State = NativeWindowDragProbeState;
using Outcome = State::Outcome;
constexpr deskflow::windowshare::Rect originalFrame{100, 200, 640, 400};
constexpr deskflow::windowshare::Rect movedFrame{120, 220, 640, 400};

void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
State readyState()
{
  State state(100);
  check(state.begin(71, 81, originalFrame, {130, 230}, 1000, 100), "bind the exact owned fixture window");
  state.pointer(1, 1100, {150, 250}, false, 200);
  state.frame(71, 81, movedFrame, 300);
  check(state.mayPost(600000100), "real pointer travel and window motion allow one probe submission");
  return state;
}
void post(State &state)
{
  check(state.submitted(1, 2000, movedFrame, 600000100), "retain native timestamp and callback cutoff at submission");
  check(!state.submitted(1, 2000, movedFrame, 600000100), "the owned probe cannot post a second release");
}
int pureTests()
{
  try {
    State idle(100);
    check(idle.outcome() == Outcome::NoDrag && !idle.mayPost(1000), "no real drag means no submission");
    check(!idle.begin(0, 81, originalFrame, {130, 230}, 1000, 100), "unknown native window rejected");
    check(!idle.begin(71, 0, originalFrame, {130, 230}, 1000, 100), "unknown native process rejected");
    check(!idle.begin(71, 81, originalFrame, {99, 230}, 1000, 100), "a down outside the owned window is ineligible");
    auto unchanged = readyState();
    post(unchanged);
    for (int i = 0; i < 100; ++i)
      unchanged.frame(71, 81, movedFrame, 600000200 + i);
    check(
        unchanged.outcome() == Outcome::NoPostMotion && unchanged.postFrameObservations() == 0,
        "quiet timer ticks and unchanged frames are not release evidence"
    );
    unchanged.appUp();
    check(unchanged.outcome() == Outcome::AppUpOnly, "an app-up receipt alone is not a server grab receipt");

    for (const bool appUp : {false, true}) {
      auto motion = readyState();
      post(motion);
      if (appUp)
        motion.appUp();
      for (std::uint64_t i = 2; i <= 4; ++i) {
        motion.pointer(i, 2000 + i, {150.0 + 10.0 * i, 250}, false, 600000200 + i);
        motion.frame(71, 81, movedFrame, 600000300 + i);
      }
      check(
          motion.outcome() == Outcome::MotionWithoutObservedFrameMovement && motion.postFrameObservations() == 3 &&
              motion.appReceivedUp() == appUp,
          "ordered physical motion and stable frames remain an observation with or without app receipt"
      );
      motion.pointer(5, 2005, {220, 250}, false, 600000500);
      motion.frame(71, 81, {140, 220, 640, 400}, 600000600);
      check(
          motion.outcome() == Outcome::FrameMovementAfterSubmission,
          "actual later frame movement is recorded independently of app-up receipt"
      );
    }
    auto stale = readyState();
    post(stale);
    stale.pointer(2, 1900, {200, 250}, false, 600000200);
    stale.frame(71, 81, movedFrame, 600000300);
    check(
        stale.postMotions() == 0 && stale.postFrameObservations() == 0,
        "a callback after posting with an older source timestamp contributes no fresh motion"
    );
    stale.pointer(2, 2100, {210, 250}, false, 600000400);
    check(stale.invalid(), "repeated callback ordinal invalidates the observation");

    auto earlyUp = readyState();
    earlyUp.pointer(2, 1200, {150, 250}, true, 400);
    check(
        !earlyUp.mayPost(600000100) && earlyUp.outcome() == Outcome::NoSubmission,
        "real release before submission cannot be replaced with a synthetic release"
    );
    auto laterUp = readyState();
    post(laterUp);
    laterUp.pointer(2, 2100, {150, 250}, true, 600000200);
    laterUp.pointer(3, 2200, {250, 250}, false, 600000300);
    laterUp.frame(71, 81, {220, 220, 640, 400}, 600000400);
    check(
        laterUp.outcome() == Outcome::NoPostMotion && laterUp.postFrameObservations() == 0,
        "physical release and later unpressed motion cannot prove earlier targeted release worked"
    );
    for (int variation = 0; variation < 3; ++variation) {
      auto changed = readyState();
      post(changed);
      if (variation == 0)
        changed.frame(72, 81, movedFrame, 600000200);
      else if (variation == 1)
        changed.frame(71, 82, movedFrame, 600000200);
      else
        changed.invalidate(); // Bounded native observation ring overflow / disabled tap.
      check(changed.outcome() == Outcome::InvalidObservation, "identity loss and observation loss cannot pass");
    }
    auto expired = readyState();
    check(!expired.mayPost(100 + State::durationNs), "absolute deadline stops a pending probe submission");
    State late(100);
    check(late.begin(71, 81, originalFrame, {130, 230}, 1000, State::durationNs), "late real down can be observed");
    late.pointer(1, 1100, {150, 250}, false, State::durationNs + 1);
    late.frame(71, 81, movedFrame, State::durationNs + 2);
    check(!late.mayPost(State::durationNs + State::beforePostNs), "late drag does not renew the global deadline");
    std::puts("PASS: owned drag observation identity, ordering, one-shot release and bounded deadline");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}

std::uint64_t nowNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
class Probe;
} // namespace

@interface DFWindowDragProbeWindow : NSWindow {
@public
  Probe *probe;
}
@end
@interface DFWindowDragProbeView : NSView {
@public
  Probe *probe;
}
@end
@interface DFWindowDragProbeDelegate : NSObject <NSWindowDelegate> {
@public
  Probe *probe;
}
@end

namespace {
class Probe final
{
public:
  Probe() : m_state(nowNs())
  {
    // A stalled WindowServer query or AppKit tracking loop must not leave a
    // fixture process/window around indefinitely. No subprocess is spawned.
    m_deadlineThread = std::thread([this] {
      std::unique_lock lock(m_deadlineMutex);
      if (!m_deadlineCondition.wait_for(lock, std::chrono::seconds(30), [this] { return m_finished; }))
        std::_Exit(124);
    });
  }
  ~Probe()
  {
    teardown();
    {
      std::lock_guard lock(m_deadlineMutex);
      m_finished = true;
    }
    m_deadlineCondition.notify_one();
    m_deadlineThread.join();
  }
  bool start()
  {
    // Read-only preflight; this tool never requests or changes permissions.
    if (!AXIsProcessTrusted() || !CGPreflightListenEventAccess() || !native::targetedPointerAvailable()) {
      std::fputs(
          "Probe unavailable: existing Accessibility, Input Monitoring and targeted-pointer APIs are required.\n",
          stderr
      );
      return false;
    }
    m_source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    if (!m_source)
      return false;
    CGEventSourceSetUserData(m_source, syntheticEventTag);
    CGEventSourceSetLocalEventsSuppressionInterval(m_source, 0);
    const auto mask = CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp) |
                      CGEventMaskBit(kCGEventLeftMouseDragged) | CGEventMaskBit(kCGEventMouseMoved);
    m_tap =
        CGEventTapCreate(kCGSessionEventTap, kCGTailAppendEventTap, kCGEventTapOptionListenOnly, mask, tapped, this);
    if (!m_tap)
      return false;
    m_tapSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, m_tap, 0);
    if (!m_tapSource)
      return false;
    CFRunLoopAddSource(CFRunLoopGetMain(), m_tapSource, kCFRunLoopCommonModes);
    CGEventTapEnable(m_tap, true);

    m_window = [[DFWindowDragProbeWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 380)
                                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                            backing:NSBackingStoreBuffered
                                                              defer:NO];
    if (!m_window)
      return false;
    m_window->probe = this;
    m_window.title = @"Deskflow owned WindowServer drag probe";
    m_window.releasedWhenClosed = NO;
    m_delegate = [[DFWindowDragProbeDelegate alloc] init];
    if (!m_delegate)
      return false;
    m_delegate->probe = this;
    m_window.delegate = m_delegate;
    m_view = [[DFWindowDragProbeView alloc] initWithFrame:NSMakeRect(0, 0, 640, 380)];
    if (!m_view)
      return false;
    m_view->probe = this;
    m_window.contentView = m_view;
    [m_window center];
    [m_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    m_windowId = static_cast<CGWindowID>(m_window.windowNumber);
    m_timer = [NSTimer timerWithTimeInterval:0.02
                                     repeats:YES
                                       block:^(NSTimer *) {
                                         tick();
                                       }];
    [NSRunLoop.mainRunLoop addTimer:m_timer forMode:NSRunLoopCommonModes];
    std::printf(
        "{\"event\":\"ready\",\"pid\":%d,\"window\":%u,\"deadlineSeconds\":30,"
        "\"instruction\":\"Hold the left button in the blue area, move the window, keep holding and moving after the "
        "targeted-up marker, then release.\"}\n",
        getpid(), m_windowId
    );
    std::fflush(stdout);
    return true;
  }
  bool begin(NSEvent *event)
  {
    if (m_stopping || event.window != m_window || event.type != NSEventTypeLeftMouseDown || !event.CGEvent ||
        isWindowshareSyntheticEvent(CGEventGetIntegerValueField(event.CGEvent, kCGEventSourceUserData)))
      return false;
    const auto flags = CGEventGetFlags(event.CGEvent);
    if (flags & momentaryModifiers)
      return false;
    const auto frame = ownedFrame();
    const auto point = CGEventGetLocation(event.CGEvent);
    if (!frame ||
        !m_state.begin(m_windowId, getpid(), *frame, {point.x, point.y}, CGEventGetTimestamp(event.CGEvent), nowNs()))
      return false;
    m_localPoint = {point.x - frame->x, point.y - frame->y};
    m_eventNumber = CGEventGetIntegerValueField(event.CGEvent, kCGMouseEventNumber);
    m_clicks = int(CGEventGetIntegerValueField(event.CGEvent, kCGMouseEventClickState));
    m_read = m_written; // Earlier events cannot become post-down observations.
    std::printf(
        "{\"event\":\"real-down\",\"window\":%u,\"sourceTime\":%llu}\n", m_windowId,
        static_cast<unsigned long long>(CGEventGetTimestamp(event.CGEvent))
    );
    std::fflush(stdout);
    return true;
  }
  void appEvent(NSEvent *event)
  {
    if (!m_stopping && event.window == m_window && event.type == NSEventTypeLeftMouseUp && event.CGEvent &&
        CGEventGetIntegerValueField(event.CGEvent, kCGEventSourceUserData) == syntheticEventTag && m_state.posted()) {
      m_state.appUp();
      std::puts("{\"event\":\"targeted-up-received-by-app\",\"serverReleaseProven\":false}");
      std::fflush(stdout);
    }
  }
  void finish(const char *reason)
  {
    if (m_stopping)
      return;
    m_stopping = true;
    std::printf(
        "{\"event\":\"complete\",\"reason\":\"%s\",\"outcome\":\"%s\",\"appUp\":%s,"
        "\"physicalUp\":%s,\"postMotions\":%llu,\"postFrameObservations\":%llu,"
        "\"serverReleaseProven\":false,\"productionDragAuthorized\":false}\n",
        reason, State::name(m_state.outcome()), m_state.appReceivedUp() ? "true" : "false",
        m_state.physicalUp() ? "true" : "false", static_cast<unsigned long long>(m_state.postMotions()),
        static_cast<unsigned long long>(m_state.postFrameObservations())
    );
    std::fflush(stdout);
    teardown();
    [NSApp stop:nil];
    // Wake NSApplication's own loop after stop. This is an app-defined wakeup,
    // not a hardware event, global post, cursor warp or release to another app.
    auto wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                   location:NSZeroPoint
                              modifierFlags:0
                                  timestamp:0
                               windowNumber:0
                                    context:nil
                                    subtype:0
                                      data1:0
                                      data2:0];
    [NSApp postEvent:wake atStart:NO];
  }

private:
  struct Pointer
  {
    std::uint64_t serial = 0, sourceTime = 0;
    deskflow::windowshare::Point point;
    CGEventType type = kCGEventNull;
  };
  static constexpr CGEventFlags momentaryModifiers = kCGEventFlagMaskShift | kCGEventFlagMaskControl |
                                                     kCGEventFlagMaskAlternate | kCGEventFlagMaskCommand |
                                                     kCGEventFlagMaskSecondaryFn;
  static CGEventRef tapped(CGEventTapProxy, CGEventType type, CGEventRef event, void *context)
  {
    auto *self = static_cast<Probe *>(context);
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
      self->m_lost = true;
      return event;
    }
    if (!event || !self->m_state.begun() || self->m_stopping ||
        isWindowshareSyntheticEvent(CGEventGetIntegerValueField(event, kCGEventSourceUserData)))
      return event;
    // Main-run-loop producer and consumer. Fixed storage; no I/O, AX, Qt,
    // allocation, waits, mutation or posting in this listen-only callback.
    if (self->m_written - self->m_read == self->m_pointer.size() || self->m_serial == UINT64_MAX) {
      self->m_lost = true;
      return event;
    }
    const auto point = CGEventGetLocation(event);
    self->m_pointer[self->m_written++ % self->m_pointer.size()] = {
        ++self->m_serial, CGEventGetTimestamp(event), {point.x, point.y}, type
    };
    return event;
  }
  std::optional<deskflow::windowshare::Rect> ownedFrame() const
  {
    auto infos = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, m_windowId);
    if (!infos)
      return std::nullopt;
    std::optional<deskflow::windowshare::Rect> result;
    if (CFArrayGetCount(infos) == 1) {
      const auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(infos, 0));
      const auto number = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowNumber));
      const auto pid = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowOwnerPID));
      const auto bounds = static_cast<CFDictionaryRef>(CFDictionaryGetValue(info, kCGWindowBounds));
      std::int64_t actualWindow = 0, actualPid = 0;
      CGRect frame;
      if (number && pid && bounds && CFNumberGetValue(number, kCFNumberSInt64Type, &actualWindow) &&
          CFNumberGetValue(pid, kCFNumberSInt64Type, &actualPid) && actualWindow == m_windowId &&
          actualPid == getpid() && CGRectMakeWithDictionaryRepresentation(bounds, &frame)) {
        deskflow::windowshare::Rect value{frame.origin.x, frame.origin.y, frame.size.width, frame.size.height};
        if (value.valid())
          result = value;
      }
    }
    CFRelease(infos);
    return result;
  }
  void tick()
  {
    if (m_stopping)
      return;
    const auto now = nowNs();
    if (m_state.expired(now)) {
      finish("deadline");
      return;
    }
    if (m_lost) {
      m_state.invalidate();
      finish("pointer-observation-lost");
      return;
    }
    while (m_read < m_written) {
      const auto observation = m_pointer[m_read++ % m_pointer.size()];
      if (observation.type == kCGEventLeftMouseDown) {
        m_state.invalidate();
        finish("second-physical-down");
        return;
      }
      m_state.pointer(
          observation.serial, observation.sourceTime, observation.point, observation.type == kCGEventLeftMouseUp, now
      );
      std::printf(
          "{\"event\":\"physical-pointer\",\"serial\":%llu,\"sourceTime\":%llu,"
          "\"up\":%s,\"x\":%.3f,\"y\":%.3f}\n",
          static_cast<unsigned long long>(observation.serial), static_cast<unsigned long long>(observation.sourceTime),
          observation.type == kCGEventLeftMouseUp ? "true" : "false", observation.point.x, observation.point.y
      );
    }
    if (!m_state.begun())
      return;
    const auto frame = ownedFrame();
    if (!frame) {
      m_state.invalidate();
      finish("owned-window-identity-lost");
      return;
    }
    m_state.frame(m_windowId, getpid(), *frame, now);
    std::printf(
        "{\"event\":\"owned-frame\",\"afterPointerSerial\":%llu,\"x\":%.3f,\"y\":%.3f,"
        "\"width\":%.3f,\"height\":%.3f}\n",
        static_cast<unsigned long long>(m_serial), frame->x, frame->y, frame->width, frame->height
    );
    if (m_state.mayPost(now)) {
      const auto flags = CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState);
      if (flags & momentaryModifiers) {
        finish("modifier-changed-before-submission");
        return;
      }
      auto event = native::targetedPointerEvent(
          m_source, m_windowId, *frame, m_localPoint, kCGEventLeftMouseUp, 0, flags, m_eventNumber, m_clicks
      );
      if (!event) {
        finish("targeted-up-construction-denied");
        return;
      }
      const auto sourceTime = CGEventGetTimestamp(event);
      // Reserve the one shot immediately before the synchronous submission.
      // postToPid has no WindowServer-grab completion result to interpret.
      if (!m_state.submitted(m_serial, sourceTime, *frame, nowNs())) {
        CFRelease(event);
        finish("submission-deadline-or-correlation-changed");
        return;
      }
      CGEventPostToPid(getpid(), event);
      CFRelease(event);
      m_window.title = @"Targeted up submitted — keep holding and moving, then release";
      std::printf(
          "{\"event\":\"targeted-up-submitted\",\"afterPointerSerial\":%llu,\"sourceTime\":%llu,"
          "\"serverReleaseProven\":false}\n",
          static_cast<unsigned long long>(m_serial), static_cast<unsigned long long>(sourceTime)
      );
    }
    std::fflush(stdout);
    if (m_state.invalid())
      finish("invalid-observation");
    else if (m_state.physicalUp())
      finish("physical-release");
  }
  void teardown()
  {
    [m_timer invalidate];
    m_timer = nil;
    if (m_tap) {
      CGEventTapEnable(m_tap, false);
      CFMachPortInvalidate(m_tap);
    }
    if (m_tapSource) {
      CFRunLoopRemoveSource(CFRunLoopGetMain(), m_tapSource, kCFRunLoopCommonModes);
      CFRelease(m_tapSource);
      m_tapSource = nullptr;
    }
    if (m_tap) {
      CFRelease(m_tap);
      m_tap = nullptr;
    }
    if (m_window) {
      m_window->probe = nullptr;
      if (m_delegate)
        m_delegate->probe = nullptr;
      if (m_view)
        m_view->probe = nullptr;
      [m_window close];
      m_window = nil;
      m_delegate = nil;
      m_view = nil;
    }
    if (m_source) {
      CFRelease(m_source);
      m_source = nullptr;
    }
  }
  State m_state;
  DFWindowDragProbeWindow *m_window = nil;
  DFWindowDragProbeDelegate *m_delegate = nil;
  DFWindowDragProbeView *m_view = nil;
  NSTimer *m_timer = nil;
  CGWindowID m_windowId = 0;
  CGEventSourceRef m_source = nullptr;
  CFMachPortRef m_tap = nullptr;
  CFRunLoopSourceRef m_tapSource = nullptr;
  std::array<Pointer, 256> m_pointer{};
  std::uint64_t m_written = 0, m_read = 0, m_serial = 0;
  deskflow::windowshare::Point m_localPoint;
  std::int64_t m_eventNumber = 0;
  int m_clicks = 0;
  bool m_lost = false, m_stopping = false;
  std::mutex m_deadlineMutex;
  std::condition_variable m_deadlineCondition;
  bool m_finished = false;
  std::thread m_deadlineThread;
};
} // namespace

@implementation DFWindowDragProbeWindow
- (void)sendEvent:(NSEvent *)event
{
  if (probe)
    probe->appEvent(event);
  [super sendEvent:event];
}
@end
@implementation DFWindowDragProbeView
- (void)drawRect:(NSRect)dirty
{
  (void)dirty;
  [[NSColor colorWithCalibratedRed:0.08 green:0.25 blue:0.45 alpha:1] setFill];
  NSRectFill(self.bounds);
  [@"Hold and drag in this blue area.\nKeep holding and moving after the targeted-up marker.\nThen release. This owned "
   @"probe closes after 30 seconds." drawInRect:NSInsetRect(self.bounds, 24, 90)
                                 withAttributes:@{
                                   NSFontAttributeName : [NSFont systemFontOfSize:20],
                                   NSForegroundColorAttributeName : NSColor.whiteColor
                                 }];
}
- (void)mouseDown:(NSEvent *)event
{
  if (probe && probe->begin(event)) {
    // This is a real user down delivered to this owned view. Apple documents
    // that this method hands dragging to WindowServer and may omit app mouse-up:
    // https://developer.apple.com/documentation/appkit/nswindow/performdrag(with:)
    [self.window performWindowDragWithEvent:event];
  }
}
@end
@implementation DFWindowDragProbeDelegate
- (void)windowWillClose:(NSNotification *)notification
{
  (void)notification;
  if (probe)
    probe->finish("user-closed-owned-window");
}
@end

int main(int argc, char **argv)
{
  // No NSApplication, window, event source or event tap in the default mode.
  if (argc == 1)
    return pureTests();
  if (argc != 2 || std::strcmp(argv[1], "--interactive") != 0) {
    std::fputs("Usage: windowshare-native-drag-probe [--interactive]\n", stderr);
    return 2;
  }
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    Probe probe;
    if (!probe.start())
      return 2;
    [NSApp run];
  }
  return 0;
}
