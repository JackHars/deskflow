/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "platform/OriginalInputCaptureSource_mac.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>
#include <unistd.h>

using namespace deskflow::windowshare;

// Explicit read-only desktop diagnostic. The window uses AppKit's ordinary
// titlebar; no view invokes performWindowDragWithEvent and no event is posted
// or suppressed. The production capture source has exactly one raw consumer.
namespace {
using Clock = std::chrono::steady_clock;
class Probe final
{
public:
  explicit Probe(bool annotated)
      : m_location(annotated ? kCGAnnotatedSessionEventTap : kCGHIDEventTap),
        m_deadline(Clock::now() + std::chrono::seconds(30))
  {
    m_watchdog = std::thread([this] {
      std::unique_lock lock(m_mutex);
      if (!m_finished.wait_until(lock, m_deadline, [this] { return m_done; }))
        std::_Exit(124);
    });
  }
  ~Probe()
  {
    teardown();
    {
      std::lock_guard lock(m_mutex);
      m_done = true;
    }
    m_finished.notify_one();
    m_watchdog.join();
  }
  bool start()
  {
    if (!CGPreflightListenEventAccess()) {
      std::fputs("NOT_RUN: existing Input Monitoring permission required\n", stderr);
      return false;
    }
    m_window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 340)
                                           styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                             backing:NSBackingStoreBuffered
                                               defer:NO];
    m_window.releasedWhenClosed = NO;
    m_window.title = @"Deskflow normal titlebar test - drag this title bar";
    m_window.movableByWindowBackground = NO;
    auto label = [NSTextField
        wrappingLabelWithString:
            @"Drag the normal title bar above this text.\nKeep moving for three seconds, then release.\n"
            @"This test only observes. It will not stop your drag.\nThe window closes after release or 30 seconds."];
    label.frame = NSMakeRect(28, 90, 584, 180);
    label.font = [NSFont systemFontOfSize:21];
    [m_window.contentView addSubview:label];
    [m_window center];
    [m_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    m_windowId = CGWindowID(m_window.windowNumber);
    m_lastFrame = frame();
    m_frameTime = Clock::now();
    if (!m_windowId || !m_lastFrame || !m_capture.start())
      return false;
    m_epoch = m_capture.epoch();
    const auto mask = CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp) |
                      CGEventMaskBit(kCGEventLeftMouseDragged) | CGEventMaskBit(kCGEventMouseMoved) |
                      CGEventMaskBit(kCGEventRightMouseDown) | CGEventMaskBit(kCGEventOtherMouseDown) |
                      CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventFlagsChanged);
    m_tap = CGEventTapCreate(m_location, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, mask, tapped, this);
    if (!m_tap)
      return false;
    m_source = CFMachPortCreateRunLoopSource(nullptr, m_tap, 0);
    if (!m_source)
      return false;
    CFRunLoopAddSource(CFRunLoopGetMain(), m_source, kCFRunLoopCommonModes);
    CGEventTapEnable(m_tap, true);
    m_timer = [NSTimer timerWithTimeInterval:0.004
                                     repeats:YES
                                       block:^(NSTimer *) {
                                         tick();
                                       }];
    [NSRunLoop.mainRunLoop addTimer:m_timer forMode:NSRunLoopCommonModes];
    std::printf(
        "{\"event\":\"ready\",\"pid\":%d,\"window\":%u,\"deadlineSeconds\":30,"
        "\"nativeTitlebar\":true,\"observationTap\":\"%s\",\"inputPosted\":false}\n",
        getpid(), m_windowId, m_location == kCGHIDEventTap ? "hid-head" : "annotated-session-head"
    );
    std::fflush(stdout);
    return true;
  }

private:
  struct RejectedEvent
  {
    CGEventType callbackType = kCGEventNull, nativeType = kCGEventNull;
    OriginalInputCapture::Status adapterStatus = OriginalInputCapture::Status::Ignored;
    std::uint64_t timestamp = 0, previousTimestamp = 0, flags = 0, callbackCount = 0;
    std::int64_t subtype = 0, button = 0, clicks = 0, dx = 0, dy = 0, sourcePid = 0, sourceState = 0;
  };
  static CGEventRef tapped(CGEventTapProxy, CGEventType type, CGEventRef event, void *context)
  {
    // No native query, allocation, log, wait, mutation or posting here. The
    // same adapter/completion queue used by OSXComputer retains event fields.
    auto &probe = *static_cast<Probe *>(context);
    const bool wasLive = probe.m_epoch.live();
    ++probe.m_callbackCount;
    probe.m_capture.capture(type, event);
    if (wasLive && !probe.m_epoch.live()) {
      // Preserve the first rejected event's scalar fields before the callback
      // returns. Re-reading supplied fields for diagnostics creates no second
      // queue consumer and cannot repair or restart the revoked source.
      auto &failure = probe.m_rejected.emplace();
      failure.callbackType = type;
      failure.callbackCount = probe.m_callbackCount;
      failure.previousTimestamp = probe.m_previousTimestamp;
      failure.adapterStatus = captureOriginalInputEvent(type, event, 1, 1).status;
      if (event && type != kCGEventTapDisabledByTimeout && type != kCGEventTapDisabledByUserInput) {
        failure.nativeType = CGEventGetType(event);
        failure.timestamp = CGEventGetTimestamp(event);
        failure.flags = CGEventGetFlags(event);
        failure.sourcePid = CGEventGetIntegerValueField(event, kCGEventSourceUnixProcessID);
        failure.sourceState = CGEventGetIntegerValueField(event, kCGEventSourceStateID);
        if (type == kCGEventMouseMoved || type == kCGEventLeftMouseDragged || type == kCGEventLeftMouseDown ||
            type == kCGEventLeftMouseUp || type == kCGEventRightMouseDown || type == kCGEventOtherMouseDown) {
          failure.subtype = CGEventGetIntegerValueField(event, kCGMouseEventSubtype);
          failure.button = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
          failure.clicks = CGEventGetIntegerValueField(event, kCGMouseEventClickState);
          failure.dx = CGEventGetIntegerValueField(event, kCGMouseEventDeltaX);
          failure.dy = CGEventGetIntegerValueField(event, kCGMouseEventDeltaY);
        }
      }
    } else if (wasLive && event) {
      probe.m_previousTimestamp = CGEventGetTimestamp(event);
    }
    return event;
  }
  std::optional<CGRect> frame() const
  {
    if (!m_window.visible || CGWindowID(m_window.windowNumber) != m_windowId)
      return {};
    auto rows = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, m_windowId);
    if (!rows)
      return {};
    std::optional<CGRect> result;
    if (CFArrayGetCount(rows) == 1) {
      auto row = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(rows, 0));
      auto pid = static_cast<CFNumberRef>(CFDictionaryGetValue(row, kCGWindowOwnerPID));
      auto id = static_cast<CFNumberRef>(CFDictionaryGetValue(row, kCGWindowNumber));
      auto bounds = static_cast<CFDictionaryRef>(CFDictionaryGetValue(row, kCGWindowBounds));
      std::int64_t observedPid = 0, observedId = 0;
      CGRect value;
      if (pid && id && bounds && CFNumberGetValue(pid, kCFNumberSInt64Type, &observedPid) &&
          CFNumberGetValue(id, kCFNumberSInt64Type, &observedId) && observedPid == getpid() &&
          observedId == m_windowId && CGRectMakeWithDictionaryRepresentation(bounds, &value) && !CGRectIsEmpty(value) &&
          !CGRectIsInfinite(value) && !CGRectIsNull(value))
        result = value;
    }
    CFRelease(rows);
    return result;
  }
  void tick()
  {
    if (m_stopping)
      return;
    if (Clock::now() >= m_deadline) {
      finish("deadline");
      return;
    }
    if (!m_capture.epoch().live()) {
      if (m_rejected) {
        const auto &f = *m_rejected;
        std::printf(
            "{\"event\":\"capture-rejection\",\"revocation\":%u,\"adapterStatus\":%u,"
            "\"callbackType\":%u,\"nativeType\":%u,\"timestamp\":%llu,\"previousTimestamp\":%llu,"
            "\"flags\":%llu,\"callbackCount\":%llu,\"subtype\":%lld,\"button\":%lld,\"clicks\":%lld,"
            "\"dx\":%lld,\"dy\":%lld,\"sourcePid\":%lld,\"sourceState\":%lld}\n",
            static_cast<unsigned>(m_epoch.reason()), static_cast<unsigned>(f.adapterStatus),
            static_cast<unsigned>(f.callbackType), static_cast<unsigned>(f.nativeType),
            static_cast<unsigned long long>(f.timestamp), static_cast<unsigned long long>(f.previousTimestamp),
            static_cast<unsigned long long>(f.flags), static_cast<unsigned long long>(f.callbackCount),
            static_cast<long long>(f.subtype), static_cast<long long>(f.button), static_cast<long long>(f.clicks),
            static_cast<long long>(f.dx), static_cast<long long>(f.dy), static_cast<long long>(f.sourcePid),
            static_cast<long long>(f.sourceState)
        );
      }
      finish("capture-revoked");
      return;
    }
    for (std::size_t i = 0; i < OriginalInputStream::capacity; ++i) {
      auto item = m_capture.popCaptured();
      if (!item)
        break;
      const auto &record = item->input.record;
      const auto *button = std::get_if<OriginalInputButton>(&record.payload);
      const auto *motion = std::get_if<OriginalInputMotion>(&record.payload);
      if ((!button && !motion) || (button && button->button != 0)) {
        finish("other-input");
        return;
      }
      if (!item->pointer || !item->pointer->matches(record)) {
        finish("pointer-metadata-unavailable");
        return;
      }
      const auto &p = *item->pointer;
      if (!m_down) {
        if (!button || !button->down)
          continue;
        // Geometry is known from this retained fixture, not inferred for an
        // arbitrary application. Preserve unavailable event window fields as
        // unavailable instead of substituting this fixture's known ID.
        const auto content = [m_window contentRectForFrameRect:m_window.frame];
        const auto titleHeight = NSHeight(m_window.frame) - NSHeight(content);
        if (!m_lastFrame || Clock::now() - m_frameTime > std::chrono::milliseconds(100) || !NSApp.active ||
            NSApp.keyWindow != m_window || button->clickCount != 1 ||
            record.flags & (originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag |
                            originalKey::commandFlag | originalKey::functionFlag) ||
            titleHeight <= 0 || p.x < m_lastFrame->origin.x + 90 || p.x >= CGRectGetMaxX(*m_lastFrame) - 10 ||
            p.y < m_lastFrame->origin.y || p.y >= m_lastFrame->origin.y + titleHeight) {
          finish("down-outside-owned-titlebar");
          return;
        }
        m_down = true;
        m_eventWindowsMatch = p.windowUnderPointer == m_windowId && p.windowHandlingPointer == m_windowId;
        std::printf(
            "{\"event\":\"titlebar-down\",\"epoch\":%llu,\"sequence\":%llu,\"sourceTime\":%llu,"
            "\"underPointer\":%u,\"handlingPointer\":%u,\"eventWindowsMatchOwned\":%s,"
            "\"sourcePid\":%lld,\"sourceState\":%lld,\"mouseSubtype\":%lld,\"eventNumber\":%lld,"
            "\"x\":%.3f,\"y\":%.3f}\n",
            static_cast<unsigned long long>(p.epoch), static_cast<unsigned long long>(p.sequence),
            static_cast<unsigned long long>(p.sourceTimestampNs), p.windowUnderPointer, p.windowHandlingPointer,
            m_eventWindowsMatch ? "true" : "false", static_cast<long long>(p.sourceProcess),
            static_cast<long long>(p.sourceState), static_cast<long long>(p.mouseSubtype),
            static_cast<long long>(p.eventNumber), p.x, p.y
        );
      } else if (button) {
        if (button->down) {
          finish("second-down");
          return;
        }
        m_up = true;
        std::printf(
            "{\"event\":\"physical-up\",\"sequence\":%llu,\"sourceTime\":%llu,"
            "\"clicks\":%u,\"mouseSubtype\":%lld,\"eventNumber\":%lld}\n",
            static_cast<unsigned long long>(p.sequence), static_cast<unsigned long long>(p.sourceTimestampNs),
            button->clickCount, static_cast<long long>(p.mouseSubtype), static_cast<long long>(p.eventNumber)
        );
      } else if (!m_up && motion->dragButton == 0) {
        ++m_dragged;
      }
    }
    std::fflush(stdout);
    // Drain the bounded input queue promptly, but query only this owned
    // window's independent frame at 50 Hz (or immediately at release).
    if (!m_up && Clock::now() - m_frameTime < std::chrono::milliseconds(20))
      return;
    const auto current = frame();
    if (!current) {
      finish("owned-window-unavailable");
      return;
    }
    if (m_down && m_lastFrame && !CGRectEqualToRect(*current, *m_lastFrame)) {
      ++m_frameChanges;
      std::printf(
          "{\"event\":\"owned-frame\",\"x\":%.3f,\"y\":%.3f,\"width\":%.3f,\"height\":%.3f}\n", current->origin.x,
          current->origin.y, current->size.width, current->size.height
      );
    }
    m_lastFrame = current;
    m_frameTime = Clock::now();
    std::fflush(stdout);
    if (m_up)
      finish("physical-release");
  }
  void finish(const char *reason)
  {
    if (m_stopping)
      return;
    m_stopping = true;
    std::printf(
        "{\"event\":\"complete\",\"reason\":\"%s\",\"titlebarDown\":%s,\"physicalUp\":%s,"
        "\"eventWindowsMatchOwned\":%s,\"draggedEvents\":%llu,\"frameChanges\":%llu,"
        "\"inputPosted\":false,\"productionDragAuthorized\":false}\n",
        reason, m_down ? "true" : "false", m_up ? "true" : "false", m_eventWindowsMatch ? "true" : "false",
        static_cast<unsigned long long>(m_dragged), static_cast<unsigned long long>(m_frameChanges)
    );
    std::fflush(stdout);
    teardown();
    [NSApp stop:nil];
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
  void teardown()
  {
    [m_timer invalidate];
    m_timer = nil;
    if (m_tap) {
      CGEventTapEnable(m_tap, false);
      CFMachPortInvalidate(m_tap);
    }
    if (m_source) {
      CFRunLoopRemoveSource(CFRunLoopGetMain(), m_source, kCFRunLoopCommonModes);
      CFRelease(m_source);
      m_source = nullptr;
    }
    if (m_tap) {
      CFRelease(m_tap);
      m_tap = nullptr;
    }
    m_capture.stop();
    [m_window close];
    m_window = nil;
  }
  OriginalInputCaptureSource m_capture;
  OriginalInputEpoch m_epoch;
  std::optional<RejectedEvent> m_rejected;
  std::uint64_t m_callbackCount = 0, m_previousTimestamp = 0;
  const CGEventTapLocation m_location;
  const Clock::time_point m_deadline;
  Clock::time_point m_frameTime;
  NSWindow *m_window = nil;
  NSTimer *m_timer = nil;
  CGWindowID m_windowId = 0;
  CFMachPortRef m_tap = nullptr;
  CFRunLoopSourceRef m_source = nullptr;
  std::optional<CGRect> m_lastFrame;
  std::uint64_t m_dragged = 0, m_frameChanges = 0;
  bool m_down = false, m_up = false, m_eventWindowsMatch = false, m_stopping = false;
  std::mutex m_mutex;
  std::condition_variable m_finished;
  bool m_done = false;
  std::thread m_watchdog;
};
} // namespace

int main(int argc, char **argv)
{
  const bool annotated = argc == 2 && std::strcmp(argv[1], "--interactive-annotated") == 0;
  if (argc != 2 || (std::strcmp(argv[1], "--interactive") != 0 && !annotated)) {
    std::fputs(
        "Usage: windowshare-native-titlebar-probe --interactive|--interactive-annotated\n"
        "Requires a ready user; posts no input. Annotated mode observes a later event stage; production Core stays at "
        "HID.\n",
        stderr
    );
    return 2;
  }
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    Probe probe(annotated);
    if (!probe.start())
      return 2;
    [NSApp run];
  }
  return 0;
}
