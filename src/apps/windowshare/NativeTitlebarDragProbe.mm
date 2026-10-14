/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeTitlebarObservation.h"
#include "platform/OriginalInputCaptureSource_mac.h"
#include <QJsonDocument>
#include <QJsonObject>

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
constexpr auto observationDuration = std::chrono::seconds(90);
constexpr auto cleanupGrace = std::chrono::seconds(5);
NSWindow *makeWindow(bool holdMotion = false)
{
  NSWindow *window;
  window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 340)
                                       styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                         backing:NSBackingStoreBuffered
                                           defer:NO];
  window.releasedWhenClosed = NO;
  window.title = @"Deskflow normal titlebar test - drag this title bar";
  window.movableByWindowBackground = NO;
  auto label = [NSTextField
      wrappingLabelWithString:
          holdMotion
              ? @"Drag the normal title bar for FIVE seconds.\nKeep holding and moving when the window stops.\n"
                @"Then release once. No button event is synthesized.\nThe test closes on release or after 90 seconds."
              : @"Drag the normal title bar above this text.\nKeep moving for three seconds, then release.\n"
                @"This test only observes. It will not stop your drag.\nThe window closes after release or 90 "
                @"seconds."];
  label.frame = NSMakeRect(28, 90, 584, 180);
  label.font = [NSFont systemFontOfSize:21];
  [window.contentView addSubview:label];
  [window center];
  [window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
  return window;
}
int ownedTitlebarFixture(bool holdMotion)
{
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    auto window = makeWindow(holdMotion);
    const auto content = [window contentRectForFrameRect:window.frame];
    std::printf(
        "{\"window\":%ld,\"pid\":%d,\"titleHeight\":%.3f}\n", (long)window.windowNumber, getpid(),
        NSHeight(window.frame) - NSHeight(content)
    );
    std::fflush(stdout);
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (observationDuration + cleanupGrace).count() * NSEC_PER_SEC),
        dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
          std::_Exit(124);
        }
    );
    [NSApp run];
  }
  return 0;
}

class Probe final
{
public:
  explicit Probe(bool annotated, bool joined, bool eligible, bool holdMotion)
      : m_location(annotated ? kCGAnnotatedSessionEventTap : kCGHIDEventTap),
        m_joinedMode(joined),
        m_eligibleMode(eligible),
        m_holdMotion(holdMotion),
        m_deadline(Clock::now() + observationDuration)
  {
    m_watchdog = std::thread([this] {
      std::unique_lock lock(m_mutex);
      if (!m_finished.wait_until(lock, m_deadline + cleanupGrace, [this] { return m_done; }))
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
    const auto fail = [](const char *stage) {
      const auto line =
          QJsonDocument(QJsonObject{{"event", "startup-failed"}, {"stage", stage}}).toJson(QJsonDocument::Compact);
      std::printf("%s\n", line.constData());
      std::fflush(stdout);
      return false;
    };
    if (!CGPreflightListenEventAccess() || (m_eligibleMode && !AXIsProcessTrusted())) {
      std::fputs(
          "NOT_RUN: existing Input Monitoring and (for eligibility) Accessibility permission required\n", stderr
      );
      return fail("permissions");
    }
    if (m_eligibleMode) {
      // Query a separate owned application, as the production Host does.
      // A main-thread AX RPC into this same process could block its own reply.
      m_fixture = [[NSTask alloc] init];
      m_fixture.executableURL = [NSURL fileURLWithPath:NSProcessInfo.processInfo.arguments[0]];
      m_fixture.arguments = @[ m_holdMotion ? @"--owned-titlebar-hold-fixture" : @"--owned-titlebar-fixture" ];
      auto output = [NSPipe pipe];
      m_fixture.standardOutput = output;
      m_fixture.standardError = [NSFileHandle fileHandleWithStandardError];
      NSError *failure = nil;
      if (![m_fixture launchAndReturnError:&failure])
        return fail("fixture-launch");
      auto data = [output.fileHandleForReading availableData];
      const auto info =
          QJsonDocument::fromJson(QByteArray(static_cast<const char *>(data.bytes), data.length)).object();
      m_windowId = CGWindowID(info["window"].toInteger());
      m_windowProcess = pid_t(info["pid"].toInteger());
      m_titleHeight = info["titleHeight"].toDouble();
      const auto fixtureLine = QJsonDocument(
                                   QJsonObject{
                                       {"event", "owned-fixture-ready"},
                                       {"window", qint64(m_windowId)},
                                       {"pid", qint64(m_windowProcess)},
                                       {"titleHeight", m_titleHeight}
                                   }
      ).toJson(QJsonDocument::Compact);
      std::printf("%s\n", fixtureLine.constData());
      std::fflush(stdout);
      if (m_windowProcess != m_fixture.processIdentifier || m_titleHeight <= 0 || m_titleHeight > 80)
        return fail("fixture-identity");
    } else {
      m_window = makeWindow();
      m_windowId = CGWindowID(m_window.windowNumber);
      m_windowProcess = getpid();
      const auto content = [m_window contentRectForFrameRect:m_window.frame];
      m_titleHeight = NSHeight(m_window.frame) - NSHeight(content);
    }
    m_lastFrame = frame();
    // The fixture reports its identity before its first AppKit run-loop turn.
    // WindowServer may not have published that exact window's frame yet.
    // Wait only during startup, before installing either observation tap.
    const auto frameDeadline = Clock::now() + std::chrono::seconds(1);
    while (!m_lastFrame && m_fixture.running && Clock::now() < frameDeadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      m_lastFrame = frame();
    }
    m_frameTime = Clock::now();
    if (!m_windowId)
      return fail("fixture-window-id");
    if (!m_lastFrame)
      return fail("fixture-native-frame");
    if (!m_capture.start())
      return fail("capture-start");
    m_epoch = m_capture.epoch();
    if (m_joinedMode) {
      m_join = std::make_unique<OriginalPointerJoin>(m_epoch);
      m_annotationTap = CGEventTapCreate(
          kCGAnnotatedSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly,
          CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp),
          [](CGEventTapProxy, CGEventType type, CGEventRef event, void *context) -> CGEventRef {
            static_cast<Probe *>(context)->m_capture.captureAnnotation(type, event);
            return event;
          },
          this
      );
      if (!m_annotationTap)
        return fail("annotation-tap");
      if (!m_capture.startAnnotations())
        return fail("annotation-start");
      m_annotationSource = CFMachPortCreateRunLoopSource(nullptr, m_annotationTap, 0);
      if (!m_annotationSource)
        return fail("annotation-source");
      CFRunLoopAddSource(CFRunLoopGetMain(), m_annotationSource, kCFRunLoopCommonModes);
    }
    const auto mask = CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp) |
                      CGEventMaskBit(kCGEventLeftMouseDragged) | CGEventMaskBit(kCGEventMouseMoved) |
                      CGEventMaskBit(kCGEventRightMouseDown) | CGEventMaskBit(kCGEventOtherMouseDown) |
                      CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventFlagsChanged);
    m_tap = CGEventTapCreate(
        m_location, kCGHeadInsertEventTap, m_holdMotion ? kCGEventTapOptionDefault : kCGEventTapOptionListenOnly, mask,
        tapped, this
    );
    if (!m_tap)
      return fail("capture-tap");
    m_source = CFMachPortCreateRunLoopSource(nullptr, m_tap, 0);
    if (!m_source)
      return fail("capture-source");
    CFRunLoopAddSource(CFRunLoopGetMain(), m_source, kCFRunLoopCommonModes);
    CGEventTapEnable(m_tap, true);
    m_timer = [NSTimer timerWithTimeInterval:0.004
                                     repeats:YES
                                       block:^(NSTimer *) {
                                         tick();
                                       }];
    [NSRunLoop.mainRunLoop addTimer:m_timer forMode:NSRunLoopCommonModes];
    std::printf(
        "{\"event\":\"ready\",\"pid\":%d,\"window\":%u,\"sourcePid\":%d,\"deadlineSeconds\":90,"
        "\"nativeTitlebar\":true,\"observationTap\":\"%s\",\"inputPosted\":false}\n",
        getpid(), m_windowId, m_windowProcess,
        m_joinedMode                   ? "hid-with-annotation-join"
        : m_location == kCGHIDEventTap ? "hid-head"
                                       : "annotated-session-head"
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
    if (probe.m_holdMotion && probe.m_capture.suppressWindowDragMotion()) {
      ++probe.m_suppressedMotions;
      const auto sequence = probe.m_capture.windowDragMotionGate().lastSuppressedSequence();
      if (!probe.m_firstSuppressed)
        probe.m_firstSuppressed = sequence;
      return nullptr;
    }
    return event;
  }
  std::optional<CGRect> frame() const
  {
    if (m_fixture ? !m_fixture.running : (!m_window.visible || CGWindowID(m_window.windowNumber) != m_windowId))
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
          CFNumberGetValue(id, kCFNumberSInt64Type, &observedId) && observedPid == m_windowProcess &&
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
    if (m_join) {
      if (!m_capture.annotationsLive())
        m_join->revoke();
      for (std::size_t n = 0; n < OriginalPointerJoin::capacity; ++n) {
        const auto annotation = m_capture.popAnnotation();
        if (!annotation)
          break;
        m_join->annotate(*annotation);
      }
    }
    for (std::size_t i = 0; i < OriginalInputStream::capacity; ++i) {
      auto item = m_capture.popCaptured();
      if (!item)
        break;
      if (m_join)
        m_join->capture(*item);
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
        const auto titleHeight = m_titleHeight;
        const bool active = m_fixture
                                ? NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == m_windowProcess
                                : NSApp.active && NSApp.keyWindow == m_window;
        if (!m_lastFrame || Clock::now() - m_frameTime > std::chrono::milliseconds(100) || !active ||
            button->clickCount != 1 ||
            record.flags & (originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag |
                            originalKey::commandFlag | originalKey::functionFlag) ||
            titleHeight <= 0 || p.x < m_lastFrame->origin.x + 90 || p.x >= CGRectGetMaxX(*m_lastFrame) - 10 ||
            p.y < m_lastFrame->origin.y || p.y >= m_lastFrame->origin.y + titleHeight) {
          finish("down-outside-owned-titlebar");
          return;
        }
        m_down = true;
        m_downAt = Clock::now();
        m_downSequence = record.sequence;
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
        m_upSequence = record.sequence;
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
    if (m_holdMotion && m_eligibleDrag && !m_up) {
      auto &gate = m_capture.windowDragMotionGate();
      const OriginalInputStamp press{m_epoch.id(), m_downSequence, 0};
      if (!m_holdAttempted && Clock::now() - m_downAt >= std::chrono::milliseconds(500)) {
        m_holdAttempted = true;
        if (!gate.arm(press)) {
          finish("motion-hold-denied");
          return;
        }
        std::printf(
            "{\"event\":\"motion-hold-began\",\"pressSequence\":%llu,\"inputPosted\":false}\n",
            static_cast<unsigned long long>(m_downSequence)
        );
      } else if (m_holdAttempted && !gate.refresh(press)) {
        finish("motion-hold-lost");
        return;
      }
    }
    if (m_join) {
      if (!m_join->live()) {
        finish("pointer-join-revoked");
        return;
      }
      for (std::size_t n = 0; n < OriginalPointerJoin::capacity; ++n) {
        const auto match = m_join->pop();
        if (!match)
          break;
        const auto sequence = match->captured.input.record.sequence;
        if (sequence != m_downSequence && sequence != m_upSequence)
          continue;
        const auto &p = *match->captured.pointer;
        const auto &a = match->annotation;
        const bool exact = match->window() == m_windowId;
        if (sequence == m_downSequence) {
          m_joinedDown = exact;
          if (exact && m_eligibleMode) {
            QString diagnostic;
            m_titlebar = native::NativeTitlebarObservation::begin(match->window(), {p.x, p.y}, &diagnostic);
            const QJsonObject result{
                {"event", "native-titlebar-candidate"}, {"accepted", bool(m_titlebar)}, {"diagnostic", diagnostic}
            };
            const auto line = QJsonDocument(result).toJson(QJsonDocument::Compact);
            std::printf("%s\n", line.constData());
          }
        } else {
          m_joinedReleaseDone = true;
          m_joinedUp = exact;
        }
        std::printf(
            "{\"event\":\"pointer-window-join\",\"sequence\":%llu,\"sourceTime\":%llu,"
            "\"eventNumber\":%lld,\"hidUnderPointer\":%u,\"annotatedUnderPointer\":%u,"
            "\"annotatedHandlingPointer\":%u,\"exactOwnedWindow\":%s,\"annotationPresent\":%s}\n",
            static_cast<unsigned long long>(sequence), static_cast<unsigned long long>(p.sourceTimestampNs),
            static_cast<long long>(p.eventNumber), p.windowUnderPointer, a ? a->windowUnderPointer : 0,
            a ? a->windowHandlingPointer : 0, exact ? "true" : "false", a ? "true" : "false"
        );
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
    if (m_titlebar) {
      QString diagnostic;
      const auto observed = m_titlebar->read(&diagnostic);
      if (!observed) {
        const auto line = QJsonDocument(
                              QJsonObject{{"event", "native-titlebar-read-failed"}, {"diagnostic", diagnostic}}
        ).toJson(QJsonDocument::Compact);
        std::printf("%s\n", line.constData());
        finish("native-titlebar-identity-lost");
        return;
      }
      if (!m_up && observed->moved && m_dragged > 0 && !m_eligibleDrag) {
        m_eligibleDrag = true;
        std::printf(
            "{\"event\":\"native-titlebar-moved\",\"window\":%u,\"process\":%lld,\"processBirth\":%llu}\n",
            observed->window, static_cast<long long>(observed->process),
            static_cast<unsigned long long>(observed->processBirth)
        );
      }
    }
    if (m_down && m_lastFrame && !CGRectEqualToRect(*current, *m_lastFrame)) {
      ++m_frameChanges;
      std::printf(
          "{\"event\":\"owned-frame\",\"x\":%.3f,\"y\":%.3f,\"width\":%.3f,\"height\":%.3f,\"suppressedMotions\":%llu}"
          "\n",
          current->origin.x, current->origin.y, current->size.width, current->size.height,
          static_cast<unsigned long long>(m_suppressedMotions)
      );
    }
    m_lastFrame = current;
    m_frameTime = Clock::now();
    std::fflush(stdout);
    if (m_up && (!m_join || m_joinedReleaseDone))
      finish("physical-release");
  }
  void finish(const char *reason)
  {
    if (m_stopping)
      return;
    m_stopping = true;
    if (m_holdMotion)
      std::printf(
          "{\"event\":\"motion-hold-summary\",\"attempted\":%s,\"suppressedMotions\":%llu,\"firstSuppressed\":%llu,"
          "\"lastSuppressed\":%llu,\"buttonEventsSuppressed\":0,\"inputPosted\":false}\n",
          m_holdAttempted ? "true" : "false", static_cast<unsigned long long>(m_suppressedMotions),
          static_cast<unsigned long long>(m_firstSuppressed),
          static_cast<unsigned long long>(m_capture.windowDragMotionGate().lastSuppressedSequence())
      );
    std::printf(
        "{\"event\":\"complete\",\"reason\":\"%s\",\"titlebarDown\":%s,\"physicalUp\":%s,"
        "\"eventWindowsMatchOwned\":%s,\"draggedEvents\":%llu,\"frameChanges\":%llu,"
        "\"joinedDown\":%s,\"joinedUp\":%s,"
        "\"eligibleSourceDrag\":%s,"
        "\"inputPosted\":false,\"productionDragAuthorized\":false}\n",
        reason, m_down ? "true" : "false", m_up ? "true" : "false", m_eventWindowsMatch ? "true" : "false",
        static_cast<unsigned long long>(m_dragged), static_cast<unsigned long long>(m_frameChanges),
        m_joinedDown ? "true" : "false", m_joinedUp ? "true" : "false", m_eligibleDrag ? "true" : "false"
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
    if (m_annotationTap) {
      CGEventTapEnable(m_annotationTap, false);
      CFMachPortInvalidate(m_annotationTap);
    }
    if (m_annotationSource) {
      CFRunLoopRemoveSource(CFRunLoopGetMain(), m_annotationSource, kCFRunLoopCommonModes);
      CFRelease(m_annotationSource);
      m_annotationSource = nullptr;
    }
    if (m_annotationTap) {
      CFRelease(m_annotationTap);
      m_annotationTap = nullptr;
    }
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
    if (m_fixture.running) {
      [m_fixture terminate];
      [m_fixture waitUntilExit];
      std::printf(
          "{\"event\":\"owned-fixture-exit\",\"pid\":%d,\"status\":%d}\n", m_windowProcess,
          int(m_fixture.terminationStatus)
      );
      std::fflush(stdout);
    }
    m_fixture = nil;
    [m_window close];
    m_window = nil;
  }
  OriginalInputCaptureSource m_capture;
  OriginalInputEpoch m_epoch;
  std::unique_ptr<OriginalPointerJoin> m_join;
  std::unique_ptr<native::NativeTitlebarObservation> m_titlebar;
  std::optional<RejectedEvent> m_rejected;
  std::uint64_t m_callbackCount = 0, m_previousTimestamp = 0;
  const CGEventTapLocation m_location;
  const bool m_joinedMode;
  const bool m_eligibleMode;
  const bool m_holdMotion;
  bool m_holdAttempted = false;
  std::uint64_t m_suppressedMotions = 0, m_firstSuppressed = 0;
  Clock::time_point m_downAt;
  const Clock::time_point m_deadline;
  Clock::time_point m_frameTime;
  NSWindow *m_window = nil;
  NSTask *m_fixture = nil;
  pid_t m_windowProcess = 0;
  double m_titleHeight = 0;
  NSTimer *m_timer = nil;
  CGWindowID m_windowId = 0;
  CFMachPortRef m_tap = nullptr;
  CFRunLoopSourceRef m_source = nullptr;
  CFMachPortRef m_annotationTap = nullptr;
  CFRunLoopSourceRef m_annotationSource = nullptr;
  std::optional<CGRect> m_lastFrame;
  std::uint64_t m_dragged = 0, m_frameChanges = 0;
  std::uint64_t m_downSequence = 0, m_upSequence = 0;
  bool m_joinedDown = false, m_joinedUp = false, m_joinedReleaseDone = false;
  bool m_eligibleDrag = false;
  bool m_down = false, m_up = false, m_eventWindowsMatch = false, m_stopping = false;
  std::mutex m_mutex;
  std::condition_variable m_finished;
  bool m_done = false;
  std::thread m_watchdog;
};
} // namespace

int main(int argc, char **argv)
{
  if (argc == 2 && std::strcmp(argv[1], "--owned-titlebar-fixture") == 0)
    return ownedTitlebarFixture(false);
  if (argc == 2 && std::strcmp(argv[1], "--owned-titlebar-hold-fixture") == 0)
    return ownedTitlebarFixture(true);
  const bool holdMotion = argc == 2 && std::strcmp(argv[1], "--interactive-hold-motion") == 0;
  const bool annotated = argc == 2 && std::strcmp(argv[1], "--interactive-annotated") == 0;
  const bool eligible = holdMotion || (argc == 2 && std::strcmp(argv[1], "--interactive-eligible") == 0);
  const bool joined = eligible || (argc == 2 && std::strcmp(argv[1], "--interactive-joined") == 0);
  if (argc != 2 || (std::strcmp(argv[1], "--interactive") != 0 && !annotated && !joined)) {
    std::fputs(
        "Usage: windowshare-native-titlebar-probe "
        "--interactive|--interactive-annotated|--interactive-joined|--interactive-eligible|--interactive-hold-motion\n"
        "Requires a ready user; posts no input. Annotated mode observes a later event stage; production Core stays at "
        "HID.\n",
        stderr
    );
    return 2;
  }
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp
        setActivationPolicy:eligible ? NSApplicationActivationPolicyProhibited : NSApplicationActivationPolicyRegular];
    Probe probe(annotated, joined, eligible, holdMotion);
    if (!probe.start())
      return 2;
    [NSApp run];
  }
  return 0;
}
