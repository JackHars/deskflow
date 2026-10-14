/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "CaptureRate.h"
#include "HoldingDisplay.h"
#include "NativeAssociatedWindow.h"
#include "NativeCaptureTiming.h"
#include "NativeHost.h"
#include "NativeHostLifecycle.h"
#include "NativeKeyboardAdmission.h"
#include "NativeOriginalInputState.h"
#include "NativeSourceTransaction.h"
#include "NativeTitlebarObservation.h"
#include "NativeWindowIdentity_mac.h"
#include "platform/OriginalInput_mac.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <VideoToolbox/VideoToolbox.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointF>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <Qt>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <dlfcn.h>
#include <fcntl.h>
#include <libproc.h>
#include <map>
#include <mutex>
#include <optional>
#include <sys/proc_info.h>
#include <unistd.h>

namespace deskflow::windowshare::native {
constexpr int maxSessions = 16;
constexpr int maxEncodedDimension = 4096;
constexpr std::size_t maxAccessUnit = maxPayload - 8; // wire geometry prefix
constexpr qint64 maxJournalBytes = 1024 * 1024;
constexpr qint64 prepareTimeoutMs = 15000;
constexpr qint64 frameTimeoutMs = 30000;
constexpr std::uint64_t maxJsonInteger = (1ULL << 53) - 1;

// Experimental targeted pointer delivery needs this private CoreGraphics SPI.
// It is also used by Apple's WebKit test runner (CoreGraphicsTestSPI.h). The
// public global-location setter clears window-local coordinates; the public
// window-under-pointer fields alone do not bind AppKit's destination window.
// Resolve only from the system framework and keep it loaded for these pointers.
// No global-post/cursor-warp fallback is permitted when this API is absent.
struct WindowLocationApi
{
  using Get = CGPoint (*)(CGEventRef);
  using Set = void (*)(CGEventRef, CGPoint);
  Get get = nullptr;
  Set set = nullptr;
};
const WindowLocationApi &windowLocationApi()
{
  static const auto api = [] {
    WindowLocationApi result;
    const auto framework =
        dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_LAZY | RTLD_LOCAL);
    if (framework) {
      result.get = reinterpret_cast<WindowLocationApi::Get>(dlsym(framework, "CGEventGetWindowLocation"));
      result.set = reinterpret_cast<WindowLocationApi::Set>(dlsym(framework, "CGEventSetWindowLocation"));
    }
    return result;
  }();
  return api;
}
bool targetedPointerAvailable()
{
  const auto &api = windowLocationApi();
  return api.get && api.set;
}
CGEventRef targetedPointerEvent(
    CGEventSourceRef eventSource, CGWindowID window, Rect frame, Point point, CGEventType type, std::uint32_t button,
    CGEventFlags modifiers, std::int64_t eventNumber, int clicks
)
{
  const auto &location = windowLocationApi();
  const auto global = CGPointMake(frame.x + point.x, frame.y + point.y);
  const auto local = CGPointMake(point.x, point.y);
  const bool mouseType = type == kCGEventMouseMoved || type == kCGEventLeftMouseDown || type == kCGEventLeftMouseUp ||
                         type == kCGEventRightMouseDown || type == kCGEventRightMouseUp ||
                         type == kCGEventOtherMouseDown || type == kCGEventOtherMouseUp ||
                         type == kCGEventLeftMouseDragged || type == kCGEventRightMouseDragged ||
                         type == kCGEventOtherMouseDragged || type == kCGEventScrollWheel;
  if (!location.get || !location.set || !eventSource ||
      CGEventSourceGetSourceStateID(eventSource) == kCGEventSourceStateCombinedSessionState ||
      CGEventSourceGetSourceStateID(eventSource) == kCGEventSourceStateHIDSystemState || !window || !frame.valid() ||
      !std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.y < 0 || point.x >= frame.width ||
      point.y >= frame.height || !std::isfinite(global.x) || !std::isfinite(global.y) || !mouseType || button > 31 ||
      eventNumber < 0 || clicks < 0)
    return nullptr;
  const auto nativeType = type == kCGEventLeftMouseDown       ? NSEventTypeLeftMouseDown
                          : type == kCGEventLeftMouseUp       ? NSEventTypeLeftMouseUp
                          : type == kCGEventRightMouseDown    ? NSEventTypeRightMouseDown
                          : type == kCGEventRightMouseUp      ? NSEventTypeRightMouseUp
                          : type == kCGEventOtherMouseDown    ? NSEventTypeOtherMouseDown
                          : type == kCGEventOtherMouseUp      ? NSEventTypeOtherMouseUp
                          : type == kCGEventLeftMouseDragged  ? NSEventTypeLeftMouseDragged
                          : type == kCGEventRightMouseDragged ? NSEventTypeRightMouseDragged
                          : type == kCGEventOtherMouseDragged ? NSEventTypeOtherMouseDragged
                                                              : NSEventTypeMouseMoved;
  NSEvent *native = [NSEvent mouseEventWithType:nativeType
                                       location:NSMakePoint(point.x, frame.height - point.y)
                                  modifierFlags:NSEventModifierFlags(modifiers)
                                      timestamp:NSProcessInfo.processInfo.systemUptime
                                   windowNumber:window
                                        context:nil
                                    eventNumber:eventNumber
                                     clickCount:clicks
                                       pressure:(type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown ||
                                                 type == kCGEventOtherMouseDown)
                                                    ? 1.0f
                                                    : 0.0f];
  if (!native.CGEvent)
    return nullptr;
  auto event = CGEventCreateCopy(native.CGEvent);
  if (!event)
    return nullptr;
  CGEventSetSource(event, eventSource);
  CGEventSetType(event, type);
  CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, button);
  CGEventSetIntegerValueField(event, kCGEventSourceUserData, syntheticEventTag);
  CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointer, window);
  CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, window);
  // Setting the global location clears the separate window-local annotation.
  // Restore that annotation last, in top-left window coordinates.
  CGEventSetLocation(event, global);
  location.set(event, local);
  if (!CGPointEqualToPoint(CGEventGetLocation(event), global) || !CGPointEqualToPoint(location.get(event), local) ||
      [NSEvent eventWithCGEvent:event].windowNumber != window) {
    CFRelease(event);
    return nullptr;
  }
  return event;
}

// Add an AppKit window annotation to a factory-verified original event.
// Pointer delivery also needs the separate window-local addressing above.
// A key event's windowNumber alone does not override AppKit's key-window
// dispatch; keyboard delivery and cleanup must independently verify focus.
// Every retained field is copied without Qt key translation or delta scaling.
// This function only constructs events, never posts.
CGWindowID annotatedInputWindow(CGEventRef event)
{
  return event ? static_cast<CGWindowID>([NSEvent eventWithCGEvent:event].windowNumber) : 0;
}
CGEventRef targetedOriginalInputEvent(
    CGEventRef original, CGEventSourceRef source, CGWindowID window, Rect frame, Point point,
    std::int64_t eventNumber = 0, int clicks = 0
)
{
  if (!original || !source || !window || !frame.valid() || !std::isfinite(point.x) || !std::isfinite(point.y) ||
      point.x < 0 || point.y < 0 || point.x >= frame.width || point.y >= frame.height)
    return nullptr;
  const auto state = CGEventSourceGetSourceStateID(source);
  if (state == kCGEventSourceStateCombinedSessionState || state == kCGEventSourceStateHIDSystemState ||
      CGEventGetIntegerValueField(original, kCGEventSourceStateID) != state ||
      CGEventGetIntegerValueField(original, kCGEventSourceUserData) != syntheticEventTag)
    return nullptr;
  const auto type = CGEventGetType(original);
  if (type == kCGEventKeyDown || type == kCGEventKeyUp || type == kCGEventFlagsChanged) {
    // Preserve the factory's Unicode payload as well as its physical key
    // fields. Empty template strings must not change the default layout result.
    std::array<UniChar, 64> characters{}, readback{};
    UniCharCount length = 0;
    CGEventKeyboardGetUnicodeString(original, 0, &length, nullptr);
    if (length > characters.size())
      return nullptr;
    CGEventKeyboardGetUnicodeString(original, characters.size(), &length, characters.data());
    NSEvent *native = [NSEvent keyEventWithType:(type == kCGEventKeyUp ? NSEventTypeKeyUp : NSEventTypeKeyDown)
                                       location:NSMakePoint(point.x, frame.height - point.y)
                                  modifierFlags:NSEventModifierFlags(CGEventGetFlags(original))
                                      timestamp:NSProcessInfo.processInfo.systemUptime
                                   windowNumber:window
                                        context:nil
                                     characters:@""
                    charactersIgnoringModifiers:@""
                                      isARepeat:CGEventGetIntegerValueField(original, kCGKeyboardEventAutorepeat) != 0
                                        keyCode:CGEventGetIntegerValueField(original, kCGKeyboardEventKeycode)];
    if (!native.CGEvent)
      return nullptr;
    auto event = CGEventCreateCopy(native.CGEvent);
    if (!event)
      return nullptr;
    CGEventSetSource(event, source);
    CGEventSetType(event, type);
    bool exact = true;
    for (const auto field : {kCGKeyboardEventKeycode, kCGKeyboardEventKeyboardType, kCGKeyboardEventAutorepeat}) {
      const auto value = CGEventGetIntegerValueField(original, field);
      CGEventSetIntegerValueField(event, field, value);
      exact = exact && CGEventGetIntegerValueField(event, field) == value;
    }
    CGEventKeyboardSetUnicodeString(event, length, characters.data());
    UniCharCount readLength = 0;
    CGEventKeyboardGetUnicodeString(event, readback.size(), &readLength, readback.data());
    CGEventSetFlags(event, CGEventGetFlags(original));
    CGEventSetTimestamp(event, CGEventGetTimestamp(original));
    CGEventSetIntegerValueField(event, kCGEventSourceUserData, syntheticEventTag);
    CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointer, window);
    CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, window);
    if (!exact || readLength != length ||
        !std::equal(characters.begin(), characters.begin() + length, readback.begin()) ||
        CGEventGetType(event) != type || CGEventGetFlags(event) != CGEventGetFlags(original) ||
        CGEventGetTimestamp(event) != CGEventGetTimestamp(original) ||
        CGEventGetIntegerValueField(event, kCGEventSourceStateID) != state || annotatedInputWindow(event) != window) {
      CFRelease(event);
      return nullptr;
    }
    return event;
  }
  const auto button =
      type == kCGEventScrollWheel ? 0 : CGEventGetIntegerValueField(original, kCGMouseEventButtonNumber);
  auto event =
      targetedPointerEvent(source, window, frame, point, type, button, CGEventGetFlags(original), eventNumber, clicks);
  if (!event)
    return nullptr;
  const auto copyField = [&](CGEventField field) {
    const auto value = CGEventGetIntegerValueField(original, field);
    CGEventSetIntegerValueField(event, field, value);
    return CGEventGetIntegerValueField(event, field) == value;
  };
  bool exact = true;
  if (type == kCGEventScrollWheel) {
    for (const auto field :
         {kCGScrollWheelEventDeltaAxis1, kCGScrollWheelEventDeltaAxis2, kCGScrollWheelEventDeltaAxis3,
          kCGScrollWheelEventPointDeltaAxis1, kCGScrollWheelEventPointDeltaAxis2, kCGScrollWheelEventPointDeltaAxis3,
          kCGScrollWheelEventFixedPtDeltaAxis1, kCGScrollWheelEventFixedPtDeltaAxis2,
          kCGScrollWheelEventFixedPtDeltaAxis3, kCGScrollWheelEventIsContinuous, kCGScrollWheelEventScrollPhase,
          kCGScrollWheelEventMomentumPhase})
      exact = copyField(field) && exact;
  } else {
    for (const auto field : {kCGMouseEventButtonNumber, kCGMouseEventDeltaX, kCGMouseEventDeltaY, kCGMouseEventSubtype})
      exact = copyField(field) && exact;
    if (type == kCGEventLeftMouseDown || type == kCGEventLeftMouseUp || type == kCGEventRightMouseDown ||
        type == kCGEventRightMouseUp || type == kCGEventOtherMouseDown || type == kCGEventOtherMouseUp)
      exact = copyField(kCGMouseEventClickState) && exact;
  }
  CGEventSetFlags(event, CGEventGetFlags(original));
  CGEventSetTimestamp(event, CGEventGetTimestamp(original));
  if (!exact || CGEventGetFlags(event) != CGEventGetFlags(original) ||
      CGEventGetTimestamp(event) != CGEventGetTimestamp(original)) {
    CFRelease(event);
    return nullptr;
  }
  return event;
}

QString string(NSString *value)
{
  return value ? QString::fromUtf8(value.UTF8String) : QString();
}
NSString *string(const QString &value)
{
  return [NSString stringWithUTF8String:value.toUtf8().constData()];
}
Rect rect(CGRect value)
{
  return {value.origin.x, value.origin.y, value.size.width, value.size.height};
}
CGRect rect(Rect value)
{
  return CGRectMake(value.x, value.y, value.width, value.height);
}
QJsonObject json(Rect value)
{
  return {{"x", value.x}, {"y", value.y}, {"width", value.width}, {"height", value.height}};
}
Rect rect(const QJsonObject &value)
{
  return {
      value.value("x").toDouble(NAN), value.value("y").toDouble(NAN), value.value("width").toDouble(NAN),
      value.value("height").toDouble(NAN)
  };
}
std::optional<std::uint64_t> integer(const QJsonValue &value, std::uint64_t maximum = maxJsonInteger)
{
  if (!value.isDouble())
    return std::nullopt;
  const auto number = value.toDouble();
  if (!std::isfinite(number) || number < 0 || number > double(maximum) || std::floor(number) != number)
    return std::nullopt;
  return static_cast<std::uint64_t>(number);
}

// This filesystem-only primitive also lets recovery tests exercise rejection
// without constructing, capturing, or moving a native window.
bool persistJournal(const QString &path, const QJsonArray &records, QString *diagnostic)
{
  const auto fail = [diagnostic](QString message) {
    if (diagnostic)
      *diagnostic = std::move(message);
    return false;
  };
  const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"windows", records}}).toJson(QJsonDocument::Compact);
  // Match the recovery reader's byte limit before opening a replacement file.
  // Native identities are never shortened to make an oversized record fit.
  if (bytes.size() > maxJournalBytes)
    return fail("Window recovery journal exceeds the supported size; export disabled");
  const auto parent = QFileInfo(path).absolutePath();
  if (path.isEmpty() || !QDir().mkpath(parent))
    return fail("Cannot create window recovery journal directory");
  // Do not alter permissions on a user-selected parent directory.
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly))
    return fail("Cannot open window recovery journal: " + file.errorString());
  if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
    return fail("Cannot restrict window recovery journal permissions: " + file.errorString());
  if (file.write(bytes) != bytes.size() || !file.flush() || fsync(file.handle()) != 0 || !file.commit())
    return fail("Cannot persist window recovery journal: " + file.errorString());
  const int directory = open(QFile::encodeName(parent).constData(), O_RDONLY | O_DIRECTORY);
  if (directory < 0)
    return fail("Cannot open the recovery journal directory for durable synchronization");
  const auto synchronized = fsync(directory);
  close(directory);
  if (synchronized != 0)
    return fail("Cannot durably synchronize the recovery journal directory");
  return true;
}

template <class T> class CF final
{
public:
  explicit CF(T value = nullptr) : m_value(value)
  {
  }
  ~CF()
  {
    if (m_value)
      CFRelease(m_value);
  }
  CF(const CF &) = delete;
  CF &operator=(const CF &) = delete;
  CF(CF &&other) noexcept : m_value(other.m_value)
  {
    other.m_value = nullptr;
  }
  CF &operator=(CF &&other) noexcept
  {
    if (this != &other) {
      if (m_value)
        CFRelease(m_value);
      m_value = other.take();
    }
    return *this;
  }
  T get() const
  {
    return m_value;
  }
  explicit operator bool() const
  {
    return m_value != nullptr;
  }
  T take()
  {
    auto result = m_value;
    m_value = nullptr;
    return result;
  }

private:
  T m_value;
};

CFTypeRef attribute(AXUIElementRef element, CFStringRef name)
{
  CFTypeRef result = nullptr;
  if (!element || AXUIElementCopyAttributeValue(element, name, &result) != kAXErrorSuccess)
    return nullptr;
  return result;
}
std::optional<Rect> bounds(AXUIElementRef element)
{
  CF<CFTypeRef> position(attribute(element, kAXPositionAttribute)), size(attribute(element, kAXSizeAttribute));
  if (!position || !size || CFGetTypeID(position.get()) != AXValueGetTypeID() ||
      CFGetTypeID(size.get()) != AXValueGetTypeID())
    return std::nullopt;
  CGPoint point{};
  CGSize dimensions{};
  if (!AXValueGetValue(static_cast<AXValueRef>(position.get()), kAXValueTypeCGPoint, &point) ||
      !AXValueGetValue(static_cast<AXValueRef>(size.get()), kAXValueTypeCGSize, &dimensions))
    return std::nullopt;
  Rect result{point.x, point.y, dimensions.width, dimensions.height};
  return result.valid() ? std::optional(result) : std::nullopt;
}
bool closeEnough(Rect a, Rect b)
{
  return std::abs(a.x - b.x) < 3 && std::abs(a.y - b.y) < 3 && std::abs(a.width - b.width) < 3 &&
         std::abs(a.height - b.height) < 3;
}
bool sameGeometry(Rect a, Rect b)
{
  return std::abs(a.x - b.x) < 0.25 && std::abs(a.y - b.y) < 0.25 && std::abs(a.width - b.width) < 0.25 &&
         std::abs(a.height - b.height) < 0.25;
}
struct WindowIdentity
{
  CGWindowID id = 0;
  pid_t pid = 0;
  qint64 launch = 0;
  QString bundle, title;
  Rect frame;
};
qint64 launchTime(pid_t pid)
{
  // Command-line Qt/AppKit applications may never publish launchDate. Kernel
  // start time also stays stable while LaunchServices registers an app later.
  proc_bsdinfo info{};
  if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) == sizeof(info) && info.pbi_start_tvsec > 0)
    return qint64(info.pbi_start_tvsec) * 1000 + qint64(info.pbi_start_tvusec) / 1000;
  NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
  return app.launchDate ? qRound64(app.launchDate.timeIntervalSince1970 * 1000.0) : 0;
}
std::optional<WindowIdentity> identity(CGWindowID id, bool *knownMissing = nullptr)
{
  if (knownMissing)
    *knownMissing = false;
  CF<CFArrayRef> list(CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, id));
  if (!list)
    return std::nullopt;
  if (CFArrayGetCount(list.get()) == 0 && knownMissing)
    *knownMissing = true;
  if (CFArrayGetCount(list.get()) != 1)
    return std::nullopt;
  NSDictionary *info = (__bridge NSDictionary *)CFArrayGetValueAtIndex(list.get(), 0);
  WindowIdentity result;
  result.id = [info[(__bridge NSString *)kCGWindowNumber] unsignedIntValue];
  result.pid = [info[(__bridge NSString *)kCGWindowOwnerPID] intValue];
  CGRect frame{};
  if (result.id != id || result.pid <= 0 ||
      !CGRectMakeWithDictionaryRepresentation(
          (__bridge CFDictionaryRef)info[(__bridge NSString *)kCGWindowBounds], &frame
      ))
    return std::nullopt;
  result.frame = rect(frame);
  result.title = string(info[(__bridge NSString *)kCGWindowName]);
  result.launch = launchTime(result.pid);
  result.bundle = string([NSRunningApplication runningApplicationWithProcessIdentifier:result.pid].bundleIdentifier);
  return result.frame.valid() && result.launch > 0 ? std::optional(result) : std::nullopt;
}
// An exact native window ID is required even when title and geometry collide.
// The SPI join supplies identity only; the existing session/focus/input gates
// remain independently required. Recheck the process incarnation after AX IPC.
AXUIElementRef matchAX(const WindowIdentity &window, QString *diagnostic = nullptr, bool *retryable = nullptr)
{
  auto match = copyExactAXWindow(window.pid, window.id, diagnostic, retryable);
  if (!match)
    return nullptr;
  const auto current = identity(window.id);
  if (!current || current->pid != window.pid || current->launch != window.launch) {
    CFRelease(match);
    if (diagnostic)
      *diagnostic = "Selected native window/process changed during Accessibility lookup";
    if (retryable)
      *retryable = false;
    return nullptr;
  }
  return match;
}
std::vector<Rect> physicalDisplays()
{
  CGDirectDisplayID ids[32];
  std::uint32_t count = 0;
  std::vector<Rect> result;
  if (CGGetActiveDisplayList(32, ids, &count) != kCGErrorSuccess)
    return result;
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto id = ids[index];
    if (CGDisplayVendorNumber(id) == virtualDisplayVendor && CGDisplayModelNumber(id) == virtualDisplayProduct &&
        CGDisplaySerialNumber(id) == virtualDisplaySerial)
      continue;
    const auto frame = rect(CGDisplayBounds(id));
    if (frame.valid())
      result.push_back(frame);
  }
  return result;
}
// Capture a bounded, *local* monitor-mode/identity fingerprint. The holding
// display never becomes a physical display witness. A failed enumeration or
// in-progress/unsupported mode is not proof the display layout stayed stable.
std::optional<std::vector<PhysicalOutputSignature>> physicalOutputSnapshot()
{
  CGDirectDisplayID ids[33];
  std::uint32_t count = 0;
  if (CGGetActiveDisplayList(33, ids, &count) != kCGErrorSuccess || !count || count >= 33)
    return std::nullopt;
  std::vector<PhysicalOutputSignature> physical;
  physical.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto id = ids[index];
    if (CGDisplayVendorNumber(id) == virtualDisplayVendor && CGDisplayModelNumber(id) == virtualDisplayProduct &&
        CGDisplaySerialNumber(id) == virtualDisplaySerial)
      continue;
    const auto frame = rect(CGDisplayBounds(id));
    CF<CGDisplayModeRef> mode(CGDisplayCopyDisplayMode(id));
    const auto rotation = CGDisplayRotation(id);
    if (!frame.valid() || !mode || !std::isfinite(rotation))
      return std::nullopt;
    const auto normalized = std::fmod(rotation + 360.0, 360.0);
    const auto rounded = std::round(normalized / 90.0) * 90.0;
    if (std::abs(normalized - rounded) > 0.01 || rounded > 270.0)
      return std::nullopt;
    const auto width = CGDisplayModeGetPixelWidth(mode.get());
    const auto height = CGDisplayModeGetPixelHeight(mode.get());
    if (!width || !height || width > 32768 || height > 32768)
      return std::nullopt;
    physical.push_back(
        {id, frame, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), static_cast<int>(rounded)}
    );
  }
  if (physical.empty() || !samePhysicalOutputLayout(physical, physical))
    return std::nullopt;
  return physical;
}
std::optional<Rect> recoverFrame(Rect original)
{
  // Use the same deterministic physical-output policy for ordinary return
  // and post-crash journal recovery; never choose the private holding output.
  return recoveryRect(original, physicalDisplays());
}
bool setBounds(AXUIElementRef element, Rect desired, const std::function<bool()> &admitted = {})
{
  const auto current = [&] { return !admitted || admitted(); };
  if (!element || !desired.valid() || !current())
    return false;
  CGPoint position{desired.x, desired.y};
  CGSize size{desired.width, desired.height};
  CF<AXValueRef> positionValue(AXValueCreate(kAXValueTypeCGPoint, &position));
  CF<AXValueRef> sizeValue(AXValueCreate(kAXValueTypeCGSize, &size));
  // Restore visibility before geometry, including after the app minimized itself.
  AXUIElementSetAttributeValue(element, kAXMinimizedAttribute, kCFBooleanFalse);
  // Export relocation retains its lifecycle token through each AX round trip.
  // Recovery deliberately passes no predicate so it can restore while fenced.
  if (!current())
    return false;
  AXUIElementSetAttributeValue(element, kAXSizeAttribute, sizeValue.get());
  if (!current())
    return false;
  AXUIElementSetAttributeValue(element, kAXPositionAttribute, positionValue.get());
  if (!current())
    return false;
  auto actual = bounds(element);
  CF<CFTypeRef> minimized(attribute(element, kAXMinimizedAttribute));
  // Some applications have a minimum size. Reaching a physical display is more
  // important than insisting on an exact size they cannot represent.
  return current() && (!minimized || !CFEqual(minimized.get(), kCFBooleanTrue)) && actual &&
         std::abs(actual->x - desired.x) < 5 && std::abs(actual->y - desired.y) < 5;
}

// All callbacks return to the QObject thread. Encoded access units are bounded;
// overflow discards the dependent GOP and requests a fresh IDR, never an
// arbitrary P-frame that would poison the decoder until its next keyframe.
struct Capture final : std::enable_shared_from_this<Capture>
{
  struct EncodedFrame
  {
    QByteArray bytes;
    std::uint64_t geometry = 0, generation = 0;
  };
  std::mutex mutex;
  NativeHost *receiver = nullptr; // mutex protects enqueue vs QObject destruction
  std::function<void(std::uint64_t, QByteArray)> frame;
  std::function<void(QString)> error;
  std::deque<EncodedFrame> pending;
  std::size_t pendingBytes = 0;
  bool scheduled = false, awaitingKeyframe = true;
  std::atomic<bool> stopped{false}, forceKeyframe{true};
  std::atomic<bool> failurePending{false};
  std::atomic<bool> acceptingFrames{true};
  std::atomic<std::uint64_t> geometry{1}, generation{1};
  std::atomic<std::uint64_t> configurationGeneration{1};
  std::atomic<int> expectedWidth{0}, expectedHeight{0};
  std::atomic<unsigned> frameRate{defaultCaptureRate};
  std::atomic<unsigned> inFlight{0};
  std::atomic<qint64> lastSample{0};
  VTCompressionSessionRef encoder = nullptr; // used on the capture serial queue
  CVPixelBufferRef lastImage = nullptr;
  std::uint64_t lastImageGeometry = 0; // capture serial queue only
  int encodedWidth = 0, encodedHeight = 0;
  unsigned encodedFrameRate = 0;
  std::atomic<bool> hardware{false};
  CaptureTimeline timeline;
  dispatch_queue_t queue = dispatch_queue_create("org.deskflow.windowshare.capture", DISPATCH_QUEUE_SERIAL);

  void fail(QString message)
  {
    std::lock_guard lock(mutex);
    if (!receiver || stopped || failurePending.exchange(true))
      return;
    auto self = shared_from_this();
    QMetaObject::invokeMethod(
        receiver,
        [self, message] {
          if (!self->stopped && self->error)
            self->error(message);
        },
        Qt::QueuedConnection
    );
  }
  void restartGop()
  {
    std::lock_guard lock(mutex);
    ++generation;
    pending.clear();
    pendingBytes = 0;
    awaitingKeyframe = true;
    forceKeyframe = true;
  }
  bool configure(std::uint64_t revision, int width, int height, unsigned rate)
  {
    std::lock_guard lock(mutex);
    if (geometry == revision && expectedWidth == width && expectedHeight == height && frameRate == rate)
      return false;
    acceptingFrames = false;
    geometry = revision;
    expectedWidth = width;
    expectedHeight = height;
    frameRate = rate;
    ++configurationGeneration;
    ++generation;
    pending.clear();
    pendingBytes = 0;
    awaitingKeyframe = true;
    forceKeyframe = true;
    return true;
  }
  void deliver(QByteArray bytes, bool keyframe, std::uint64_t revision, std::uint64_t submittedGeneration)
  {
    std::lock_guard lock(mutex);
    if (!receiver || stopped || !acceptingFrames || revision != geometry || submittedGeneration != generation)
      return;
    if (pending.size() >= 4 || pendingBytes + std::size_t(bytes.size()) > maxPayload) {
      ++generation;
      pending.clear();
      pendingBytes = 0;
      awaitingKeyframe = true;
      forceKeyframe = true;
      return;
    }
    if (awaitingKeyframe && !keyframe)
      return;
    if (keyframe)
      awaitingKeyframe = false;
    pendingBytes += bytes.size();
    pending.push_back({std::move(bytes), revision, submittedGeneration});
    if (scheduled)
      return;
    scheduled = true;
    auto self = shared_from_this();
    QMetaObject::invokeMethod(
        receiver,
        [self] {
          std::deque<EncodedFrame> frames;
          {
            std::lock_guard lock(self->mutex);
            frames.swap(self->pending);
            self->pendingBytes = 0;
            self->scheduled = false;
          }
          for (auto &frame : frames) {
            if (self->stopped)
              break;
            if (frame.geometry != self->geometry || frame.generation != self->generation || !self->acceptingFrames)
              continue;
            if (self->frame)
              self->frame(frame.geometry, std::move(frame.bytes));
          }
        },
        Qt::QueuedConnection
    );
  }
  void shutdown()
  {
    {
      std::lock_guard lock(mutex);
      stopped = true;
      receiver = nullptr;
      pending.clear();
      pendingBytes = 0;
    }
    // Serial cleanup runs after all SCK output callbacks. The shared block keeps
    // the callback refcon alive until VT has returned every pending callback.
    auto self = shared_from_this();
    dispatch_async(queue, ^{
      if (self->encoder) {
        VTCompressionSessionCompleteFrames(self->encoder, kCMTimeInvalid);
        VTCompressionSessionInvalidate(self->encoder);
        CFRelease(self->encoder);
        self->encoder = nullptr;
      }
      if (self->lastImage) {
        CVPixelBufferRelease(self->lastImage);
        self->lastImage = nullptr;
      }
    });
  }
};

struct SubmittedFrame
{
  std::uint64_t geometry, generation;
};
void encoded(void *reference, void *frameReference, OSStatus status, VTEncodeInfoFlags flags, CMSampleBufferRef sample)
{
  std::unique_ptr<SubmittedFrame> submitted(static_cast<SubmittedFrame *>(frameReference));
  auto *capture = static_cast<Capture *>(reference);
  capture->inFlight.fetch_sub(1);
  if (capture->stopped || !submitted || submitted->geometry != capture->geometry ||
      submitted->generation != capture->generation)
    return;
  if (status != noErr) {
    capture->fail(QString("VideoToolbox encode failed (%1)").arg(status));
    return;
  }
  if (!sample || (flags & kVTEncodeInfo_FrameDropped) || !CMSampleBufferDataIsReady(sample)) {
    capture->forceKeyframe = true;
    return;
  }
  bool keyframe = true;
  auto attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
  if (attachments && CFArrayGetCount(attachments)) {
    auto first = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
    keyframe = !CFDictionaryContainsKey(first, kCMSampleAttachmentKey_NotSync) ||
               CFDictionaryGetValue(first, kCMSampleAttachmentKey_NotSync) == kCFBooleanFalse;
  }
  auto format = CMSampleBufferGetFormatDescription(sample);
  int lengthSize = 0;
  size_t parameterCount = 0, parameterSize = 0;
  const std::uint8_t *parameter = nullptr;
  if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
          format, 0, &parameter, &parameterSize, &parameterCount, &lengthSize
      ) != noErr ||
      lengthSize < 1 || lengthSize > 4 || parameterCount > 32) {
    capture->fail("VideoToolbox returned invalid H.264 parameters");
    return;
  }
  QByteArray output;
  constexpr char startCode[] = {0, 0, 0, 1};
  if (keyframe) {
    for (size_t index = 0; index < parameterCount; ++index) {
      if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
              format, index, &parameter, &parameterSize, nullptr, nullptr
          ) != noErr ||
          parameterSize > maxAccessUnit)
        return;
      output.append(startCode, 4);
      output.append(reinterpret_cast<const char *>(parameter), qsizetype(parameterSize));
    }
  }
  auto block = CMSampleBufferGetDataBuffer(sample);
  if (!block)
    return;
  const auto size = CMBlockBufferGetDataLength(block);
  if (size == 0 || size > maxAccessUnit) {
    capture->fail("Encoded window frame exceeds protocol limit");
    return;
  }
  QByteArray avcc(qsizetype(size), Qt::Uninitialized);
  if (CMBlockBufferCopyDataBytes(block, 0, size, avcc.data()) != noErr)
    return;
  size_t offset = 0;
  while (offset < size) {
    if (size - offset < unsigned(lengthSize)) {
      capture->fail("Truncated H.264 NAL header");
      return;
    }
    std::uint32_t length = 0;
    for (int byte = 0; byte < lengthSize; ++byte)
      length = (length << 8) | std::uint8_t(avcc[qsizetype(offset++)]);
    if (length == 0 || length > size - offset || std::size_t(output.size()) + 4 + length > maxAccessUnit) {
      capture->fail("Invalid H.264 NAL size");
      return;
    }
    output.append(startCode, 4);
    output.append(avcc.constData() + offset, length);
    offset += length;
  }
  capture->deliver(std::move(output), keyframe, submitted->geometry, submitted->generation);
}

bool configureEncoderRate(const std::shared_ptr<Capture> &capture, unsigned rate)
{
  const auto expected = @(rate), keyframeInterval = @(rate * 2);
  const auto result = VTSessionSetProperty(
      capture->encoder, kVTCompressionPropertyKey_ExpectedFrameRate, (__bridge CFNumberRef)expected
  );
  const auto keyframes = VTSessionSetProperty(
      capture->encoder, kVTCompressionPropertyKey_MaxKeyFrameInterval, (__bridge CFNumberRef)keyframeInterval
  );
  if (result != noErr || keyframes != noErr) {
    capture->fail(QString("Cannot configure H.264 frame rate (%1/%2)").arg(result).arg(keyframes));
    return false;
  }
  capture->encodedFrameRate = rate;
  capture->forceKeyframe = true;
  return true;
}
bool makeEncoder(const std::shared_ptr<Capture> &capture, int width, int height, unsigned rate)
{
  if (capture->encoder && capture->encodedWidth == width && capture->encodedHeight == height)
    return capture->encodedFrameRate == rate || configureEncoderRate(capture, rate);
  if (capture->encoder) {
    VTCompressionSessionCompleteFrames(capture->encoder, kCMTimeInvalid);
    VTCompressionSessionInvalidate(capture->encoder);
    CFRelease(capture->encoder);
    capture->encoder = nullptr;
  }
  NSDictionary *specification =
      @{(__bridge NSString *)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder : @YES};
  auto status = VTCompressionSessionCreate(
      kCFAllocatorDefault, width, height, kCMVideoCodecType_H264, (__bridge CFDictionaryRef)specification, nullptr,
      nullptr, encoded, capture.get(), &capture->encoder
  );
  if (status != noErr || !capture->encoder) {
    capture->fail(QString("Cannot create H.264 VideoToolbox encoder (%1)").arg(status));
    return false;
  }
  auto encoder = capture->encoder;
  VTSessionSetProperty(encoder, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
  VTSessionSetProperty(encoder, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
  VTSessionSetProperty(encoder, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_Main_AutoLevel);
  NSNumber *bitrate = @(std::clamp(width * height * 3, 1000000, 16000000));
  VTSessionSetProperty(encoder, kVTCompressionPropertyKey_AverageBitRate, (__bridge CFNumberRef)bitrate);
  if (!configureEncoderRate(capture, rate))
    return false;
  VTSessionSetProperty(encoder, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, (__bridge CFNumberRef) @2);
  CFTypeRef hardware = nullptr;
  if (VTSessionCopyProperty(
          encoder, kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder, nullptr, &hardware
      ) == noErr &&
      hardware) {
    capture->hardware = CFEqual(hardware, kCFBooleanTrue);
    CFRelease(hardware);
  }
  status = VTCompressionSessionPrepareToEncodeFrames(encoder);
  if (status != noErr) {
    capture->fail(QString("Cannot prepare H.264 encoder (%1)").arg(status));
    return false;
  }
  capture->encodedWidth = width;
  capture->encodedHeight = height;
  capture->forceKeyframe = true;
  return true;
}

void encodeImage(
    const std::shared_ptr<Capture> &capture, CVPixelBufferRef image, bool replay = false,
    CMTime sourceTime = kCMTimeInvalid, CMTime sourceDuration = kCMTimeInvalid
)
{
  if (!image)
    return;
  SubmittedFrame stamp;
  int expectedWidth = 0, expectedHeight = 0;
  unsigned frameRate = 0;
  {
    // Configuration changes are published as one snapshot. Encoder creation
    // can drain VT callbacks and take long enough for the Qt thread to resize
    // the window again; the submitted image must retain its original stamp.
    std::lock_guard lock(capture->mutex);
    if (capture->stopped || !capture->acceptingFrames)
      return;
    stamp = {capture->geometry.load(), capture->generation.load()};
    expectedWidth = capture->expectedWidth;
    expectedHeight = capture->expectedHeight;
    frameRate = capture->frameRate;
  }
  if (replay && capture->lastImageGeometry != stamp.geometry)
    return;
  const int width = int(CVPixelBufferGetWidth(image)), height = int(CVPixelBufferGetHeight(image));
  if (width != expectedWidth || height != expectedHeight)
    return;
  if (width < 2 || height < 2 || width > maxEncodedDimension || height > maxEncodedDimension) {
    capture->fail("ScreenCaptureKit produced unsupported frame dimensions");
    return;
  }
  if (!replay) {
    if (!CaptureTimeline::valid(sourceTime)) {
      capture->fail("ScreenCaptureKit returned no usable source presentation timestamp");
      return;
    }
    if (!capture->timeline.observeSource(sourceTime))
      return;
    // A static window can replay its last image after backpressure. It may not
    // replay an image retained before a geometry/configuration change.
    CVPixelBufferRetain(image);
    if (capture->lastImage)
      CVPixelBufferRelease(capture->lastImage);
    capture->lastImage = image;
    capture->lastImageGeometry = stamp.geometry;
  }
  if (capture->inFlight.load() >= 3) {
    capture->forceKeyframe = true;
    return;
  }
  if (!makeEncoder(capture, width, height, frameRate))
    return;
  bool keyframe = false;
  {
    std::lock_guard lock(capture->mutex);
    if (capture->stopped || !capture->acceptingFrames || stamp.geometry != capture->geometry ||
        stamp.generation != capture->generation)
      return;
    keyframe = capture->forceKeyframe.exchange(false);
  }
  NSDictionary *properties = keyframe ? @{(__bridge NSString *)kVTEncodeFrameOptionKey_ForceKeyFrame : @YES} : nil;
  const auto timestamp =
      replay ? capture->timeline.replayPresentation(frameRate) : capture->timeline.sourcePresentation(sourceTime);
  if (!timestamp) {
    if (keyframe)
      capture->forceKeyframe = true;
    return;
  }
  // Recovery repeats have no new capture duration. Their tiny synthetic PTS
  // only satisfies encoder ordering; real samples retain their own timing.
  const auto duration = replay ? kCMTimeInvalid : CaptureTimeline::duration(sourceDuration, frameRate);
  capture->inFlight.fetch_add(1);
  auto *submitted = new SubmittedFrame(stamp);
  auto status = VTCompressionSessionEncodeFrame(
      capture->encoder, image, *timestamp, duration, (__bridge CFDictionaryRef)properties, submitted, nullptr
  );
  if (status != noErr) {
    delete submitted;
    capture->inFlight.fetch_sub(1);
    capture->forceKeyframe = true;
    capture->fail(QString("VideoToolbox rejected window frame (%1)").arg(status));
  }
}

std::optional<CGKeyCode> keyCode(std::uint32_t key)
{
  // These are macOS virtual key positions, never Linux native scan codes.
  static const std::map<std::uint32_t, CGKeyCode> keys = {
      {Qt::Key_A, kVK_ANSI_A},
      {Qt::Key_B, kVK_ANSI_B},
      {Qt::Key_C, kVK_ANSI_C},
      {Qt::Key_D, kVK_ANSI_D},
      {Qt::Key_E, kVK_ANSI_E},
      {Qt::Key_F, kVK_ANSI_F},
      {Qt::Key_G, kVK_ANSI_G},
      {Qt::Key_H, kVK_ANSI_H},
      {Qt::Key_I, kVK_ANSI_I},
      {Qt::Key_J, kVK_ANSI_J},
      {Qt::Key_K, kVK_ANSI_K},
      {Qt::Key_L, kVK_ANSI_L},
      {Qt::Key_M, kVK_ANSI_M},
      {Qt::Key_N, kVK_ANSI_N},
      {Qt::Key_O, kVK_ANSI_O},
      {Qt::Key_P, kVK_ANSI_P},
      {Qt::Key_Q, kVK_ANSI_Q},
      {Qt::Key_R, kVK_ANSI_R},
      {Qt::Key_S, kVK_ANSI_S},
      {Qt::Key_T, kVK_ANSI_T},
      {Qt::Key_U, kVK_ANSI_U},
      {Qt::Key_V, kVK_ANSI_V},
      {Qt::Key_W, kVK_ANSI_W},
      {Qt::Key_X, kVK_ANSI_X},
      {Qt::Key_Y, kVK_ANSI_Y},
      {Qt::Key_Z, kVK_ANSI_Z},
      {Qt::Key_0, kVK_ANSI_0},
      {Qt::Key_1, kVK_ANSI_1},
      {Qt::Key_2, kVK_ANSI_2},
      {Qt::Key_3, kVK_ANSI_3},
      {Qt::Key_4, kVK_ANSI_4},
      {Qt::Key_5, kVK_ANSI_5},
      {Qt::Key_6, kVK_ANSI_6},
      {Qt::Key_7, kVK_ANSI_7},
      {Qt::Key_8, kVK_ANSI_8},
      {Qt::Key_9, kVK_ANSI_9},
      {Qt::Key_Space, kVK_Space},
      {Qt::Key_Return, kVK_Return},
      {Qt::Key_Enter, kVK_ANSI_KeypadEnter},
      {Qt::Key_Escape, kVK_Escape},
      {Qt::Key_Tab, kVK_Tab},
      {Qt::Key_Backtab, kVK_Tab},
      {Qt::Key_Backspace, kVK_Delete},
      {Qt::Key_Delete, kVK_ForwardDelete},
      {Qt::Key_Left, kVK_LeftArrow},
      {Qt::Key_Right, kVK_RightArrow},
      {Qt::Key_Up, kVK_UpArrow},
      {Qt::Key_Down, kVK_DownArrow},
      {Qt::Key_Home, kVK_Home},
      {Qt::Key_End, kVK_End},
      {Qt::Key_PageUp, kVK_PageUp},
      {Qt::Key_PageDown, kVK_PageDown},
      {Qt::Key_Shift, kVK_Shift},
      {Qt::Key_Control, kVK_Control},
      {Qt::Key_Alt, kVK_Option},
      {Qt::Key_Meta, kVK_Command},
      {Qt::Key_CapsLock, kVK_CapsLock},
      {Qt::Key_Minus, kVK_ANSI_Minus},
      {Qt::Key_Equal, kVK_ANSI_Equal},
      {Qt::Key_BracketLeft, kVK_ANSI_LeftBracket},
      {Qt::Key_BracketRight, kVK_ANSI_RightBracket},
      {Qt::Key_Backslash, kVK_ANSI_Backslash},
      {Qt::Key_Semicolon, kVK_ANSI_Semicolon},
      {Qt::Key_Apostrophe, kVK_ANSI_Quote},
      {Qt::Key_Comma, kVK_ANSI_Comma},
      {Qt::Key_Period, kVK_ANSI_Period},
      {Qt::Key_Slash, kVK_ANSI_Slash},
      {Qt::Key_QuoteLeft, kVK_ANSI_Grave},
      {Qt::Key_Exclam, kVK_ANSI_1},
      {Qt::Key_At, kVK_ANSI_2},
      {Qt::Key_NumberSign, kVK_ANSI_3},
      {Qt::Key_Dollar, kVK_ANSI_4},
      {Qt::Key_Percent, kVK_ANSI_5},
      {Qt::Key_AsciiCircum, kVK_ANSI_6},
      {Qt::Key_Ampersand, kVK_ANSI_7},
      {Qt::Key_Asterisk, kVK_ANSI_8},
      {Qt::Key_ParenLeft, kVK_ANSI_9},
      {Qt::Key_ParenRight, kVK_ANSI_0},
      {Qt::Key_Underscore, kVK_ANSI_Minus},
      {Qt::Key_Plus, kVK_ANSI_Equal},
      {Qt::Key_BraceLeft, kVK_ANSI_LeftBracket},
      {Qt::Key_BraceRight, kVK_ANSI_RightBracket},
      {Qt::Key_Bar, kVK_ANSI_Backslash},
      {Qt::Key_Colon, kVK_ANSI_Semicolon},
      {Qt::Key_QuoteDbl, kVK_ANSI_Quote},
      {Qt::Key_Less, kVK_ANSI_Comma},
      {Qt::Key_Greater, kVK_ANSI_Period},
      {Qt::Key_Question, kVK_ANSI_Slash},
      {Qt::Key_AsciiTilde, kVK_ANSI_Grave},
      {Qt::Key_F1, kVK_F1},
      {Qt::Key_F2, kVK_F2},
      {Qt::Key_F3, kVK_F3},
      {Qt::Key_F4, kVK_F4},
      {Qt::Key_F5, kVK_F5},
      {Qt::Key_F6, kVK_F6},
      {Qt::Key_F7, kVK_F7},
      {Qt::Key_F8, kVK_F8},
      {Qt::Key_F9, kVK_F9},
      {Qt::Key_F10, kVK_F10},
      {Qt::Key_F11, kVK_F11},
      {Qt::Key_F12, kVK_F12},
      {Qt::Key_F13, kVK_F13},
      {Qt::Key_F14, kVK_F14},
      {Qt::Key_F15, kVK_F15},
      {Qt::Key_F16, kVK_F16},
      {Qt::Key_F17, kVK_F17},
      {Qt::Key_F18, kVK_F18},
      {Qt::Key_F19, kVK_F19},
      {Qt::Key_F20, kVK_F20}
  };
  const auto it = keys.find(key);
  return it == keys.end() ? std::nullopt : std::optional(it->second);
}
CGEventFlags flags(std::uint64_t modifiers)
{
  CGEventFlags result = 0;
  if (modifiers & Qt::ShiftModifier)
    result |= kCGEventFlagMaskShift;
  if (modifiers & Qt::ControlModifier)
    result |= kCGEventFlagMaskControl;
  if (modifiers & Qt::AltModifier)
    result |= kCGEventFlagMaskAlternate;
  if (modifiers & Qt::MetaModifier)
    result |= kCGEventFlagMaskCommand;
  return result;
}
bool modifier(CGKeyCode code)
{
  return code == kVK_Shift || code == kVK_Control || code == kVK_Option || code == kVK_Command || code == kVK_CapsLock;
}
} // namespace deskflow::windowshare::native

@interface DFWindowStream : NSObject <SCStreamOutput, SCStreamDelegate> {
@public
  std::shared_ptr<deskflow::windowshare::native::Capture> capture;
}
@end
@implementation DFWindowStream
- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
  (void)stream;
  if (capture && !capture->stopped)
    capture->fail(
        QString("Window capture stopped: %1").arg(deskflow::windowshare::native::string(error.localizedDescription))
    );
}
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type
{
  (void)stream;
  if (!capture || capture->stopped || type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sample))
    return;
  auto attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
  if (!attachments || !CFArrayGetCount(attachments))
    return;
  NSDictionary *info = (__bridge NSDictionary *)CFArrayGetValueAtIndex(attachments, 0);
  NSNumber *status = info[SCStreamFrameInfoStatus];
  if (!status)
    return;
  if (status.integerValue == SCFrameStatusIdle) {
    capture->lastSample = QDateTime::currentMSecsSinceEpoch();
    // A requested keyframe must also be produced for an idle/static app window.
    if (capture->forceKeyframe && capture->lastImage)
      deskflow::windowshare::native::encodeImage(capture, capture->lastImage, true);
    return;
  }
  if (status.integerValue != SCFrameStatusComplete && status.integerValue != SCFrameStatusStarted)
    return;
  capture->lastSample = QDateTime::currentMSecsSinceEpoch();
  auto image = CMSampleBufferGetImageBuffer(sample);
  if (!image)
    return;
  deskflow::windowshare::native::encodeImage(
      capture, image, false, CMSampleBufferGetPresentationTimeStamp(sample), CMSampleBufferGetDuration(sample)
  );
}
@end

namespace deskflow::windowshare {
using namespace native;

struct NativeHost::Impl
{
  struct Entry
  {
    Session session;
    WindowIdentity identity;
    AXUIElementRef window = nullptr;
    SCWindow *shareable = nil;
    SCStream *stream = nil;
    DFWindowStream *delegate = nil;
    SCStreamConfiguration *configuration = nil;
    std::shared_ptr<Capture> capture;
    qint64 started = 0;
    int pixelWidth = 0, pixelHeight = 0;
    std::uint64_t resizeRequest = 0;
    std::uint64_t focusRequest = 0;
    std::uint64_t producedGeometry = 0;
    bool visible = true, moved = false, relocating = false, dragReleaseFrameObserved = false;
    NativeAssociatedReturn associatedReturn;
    std::shared_ptr<NativeSourceTransaction> sourceTransaction = std::make_shared<NativeSourceTransaction>();
    std::shared_ptr<WindowDragTransaction> windowDrag;
    QPointF pointer;
    std::map<std::uint32_t, std::pair<std::int64_t, int>> buttons;
    ~Entry()
    {
      if (window)
        CFRelease(window);
    }
  };
  struct KeyPress
  {
    CGKeyCode code;
    QString text;
    CGEventFlags flags;
  };
  struct OriginalTarget
  {
    KeyboardRouteBinding binding;
    WindowIdentity identity;
    CF<AXUIElementRef> window;
    Rect frame;
    std::array<std::int64_t, originalInputButtonCount> buttons{};
    OriginalTarget(const KeyboardRouteBinding &binding, const Entry &entry)
        : binding(binding),
          identity(entry.identity),
          window(static_cast<AXUIElementRef>(CFRetain(entry.window))),
          frame(entry.session.source)
    {
    }
  };
  struct ClipboardTarget
  {
    ClipboardFocusBinding binding;
    WindowIdentity identity;
    CF<AXUIElementRef> window;
    Rect frame;
    NativeHostLifecycle::Admission admitted;
    ClipboardTarget(ClipboardFocusBinding binding, const Entry &entry, NativeHostLifecycle::Admission admitted)
        : binding(binding),
          identity(entry.identity),
          window(static_cast<AXUIElementRef>(CFRetain(entry.window))),
          frame(entry.session.source),
          admitted(admitted)
    {
    }
  };
  NativeHost *q;
  QString journalPath;
  std::map<std::uint64_t, std::unique_ptr<Entry>> entries;
  std::map<CGWindowID, SCWindow *> catalog;
  // Snapshot taken before any new export. A disconnection/hotplug/scale change
  // invalidates all old Viewer geometry and input ownership before restoration.
  std::optional<std::vector<PhysicalOutputSignature>> physicalAtExport;
  std::optional<std::vector<PhysicalOutputSignature>> titlebarOutputs;
  std::shared_ptr<NativeTitlebarObservation> titlebar;
  std::optional<LocalWindowPointer> titlebarPress;
  std::uint64_t titlebarObservationRevision = 0;
  WindowDragTransaction::Clock::time_point titlebarDeadline;
  QJsonArray recovery;
  InputLedger ledger;
  FocusLease focus;
  Owner focusOwner;
  std::uint64_t focusOwnerRequest = 0, focusOwnerRevision = 0;
  NativeKeyboardAdmission keyboardAdmission;
  NativeOriginalInputState originalInput;
  std::shared_ptr<OriginalTarget> originalTarget;
  bool originalCleanupFailed = false;
  std::map<std::pair<std::uint64_t, std::uint32_t>, KeyPress> keys;
  QTimer timer;
  QTimer clipboardTimer;
  std::unique_ptr<ClipboardChangeMonitor> clipboardMonitor;
  CGEventSourceRef eventSource = nullptr;
  CFMachPortRef tap = nullptr;
  CFRunLoopSourceRef tapSource = nullptr;
  id sleepObserver = nil, wakeObserver = nil, sessionObserver = nil, activeObserver = nil;
  bool refreshing = false, destroying = false, recovered = false;
  bool holdingEnabled = false;
  unsigned selectedCaptureRate = defaultCaptureRate;
  HoldingDisplay holding;
  unsigned callbackDepth = 0;
  std::uint64_t exportGeneration = 0;
  std::uint64_t lastWindowDrag = 0;
  std::int64_t mouseSequence = 0;
  struct CallbackScope
  {
    Impl &host;
    QPointer<NativeHost> alive;
    explicit CallbackScope(Impl &value) : host(value), alive(value.q)
    {
      ++host.callbackDepth;
    }
    ~CallbackScope()
    {
      if (alive)
        --host.callbackDepth;
    }
  };
  std::shared_ptr<Capture> lifetime = std::make_shared<Capture>();
  std::shared_ptr<NativeHostLifecycle> lifecycle = std::make_shared<NativeHostLifecycle>();

  explicit Impl(NativeHost *owner, QString path) : q(owner), journalPath(std::move(path))
  {
    lifetime->receiver = q;
    eventSource = CGEventSourceCreate(kCGEventSourceStatePrivate);
    if (eventSource) {
      CGEventSourceSetUserData(eventSource, syntheticEventTag);
      CGEventSourceSetLocalEventsSuppressionInterval(eventSource, 0);
    }
    // Recovery is deferred so the caller can attach error callbacks first.
    QTimer::singleShot(0, q, [this] {
      const QPointer<NativeHost> alive(q);
      recoverJournal();
      if (alive)
        installSafetyHooks();
    });
    QObject::connect(&timer, &QTimer::timeout, q, [this] { poll(); });
    timer.start(500);
    const QPointer<NativeHost> clipboardOwner(q);
    ClipboardChangeMonitor::Callbacks clipboardCallbacks;
    clipboardCallbacks.focus = [clipboardOwner] {
      return clipboardOwner ? clipboardOwner->clipboardFocus() : std::nullopt;
    };
    clipboardCallbacks.readRevision = []() -> std::optional<std::int64_t> {
      return NSPasteboard.generalPasteboard.changeCount;
    };
    clipboardCallbacks.changed = [clipboardOwner](const ClipboardFocusObservation &observation) {
      if (!clipboardOwner)
        return;
      // No retained Entry and no NativeHost callback-depth scope. The exact
      // sampled focus is checked again before any text read in the controller.
      const auto changed = clipboardOwner->onClipboardTextChanged;
      if (changed)
        changed(observation);
    };
    clipboardMonitor = std::make_unique<ClipboardChangeMonitor>(std::move(clipboardCallbacks));
    QObject::connect(&clipboardTimer, &QTimer::timeout, q, [this] { clipboardMonitor->poll(); });
  }
  ~Impl()
  {
    lifecycle->signal(NativeHostLifecycle::Event::Shutdown);
    destroying = true;
    clipboardTimer.stop();
    clipboardMonitor->setEnabled(false);
    restoreAll();
    lifetime->shutdown();
    timer.stop();
    auto center = NSWorkspace.sharedWorkspace.notificationCenter;
    if (sleepObserver)
      [center removeObserver:sleepObserver];
    if (wakeObserver)
      [center removeObserver:wakeObserver];
    if (sessionObserver)
      [center removeObserver:sessionObserver];
    if (activeObserver)
      [center removeObserver:activeObserver];
    if (tap) {
      CGEventTapEnable(tap, false);
      CFMachPortInvalidate(tap);
      CFRelease(tap);
    }
    if (tapSource) {
      CFRunLoopRemoveSource(CFRunLoopGetMain(), tapSource, kCFRunLoopCommonModes);
      CFRelease(tapSource);
    }
    if (eventSource)
      CFRelease(eventSource);
  }
  void error(QString message)
  {
    if (!destroying && q->onError) {
      CallbackScope scope(*this);
      const auto callback = q->onError;
      callback(std::move(message));
    }
  }
  Entry *entry(std::uint64_t session, std::uint64_t epoch)
  {
    auto it = entries.find(session);
    return it != entries.end() && it->second->session.epoch == epoch ? it->second.get() : nullptr;
  }
  const Entry *entry(std::uint64_t session, std::uint64_t epoch) const
  {
    auto it = entries.find(session);
    return it != entries.end() && it->second->session.epoch == epoch ? it->second.get() : nullptr;
  }
  std::optional<KeyboardRouteBinding> keyboardRouteCandidate(
      std::uint64_t session, std::uint64_t epoch, std::uint64_t generation,
      ForwardingScope scope = ForwardingScope::Keyboard
  ) const
  {
    const QPointer<NativeHost> alive(q);
    if (QThread::currentThread() != q->thread() || q->m_impl->handleAssociatedChild({session, epoch}) || !alive)
      return {};
    const auto admitted = lifecycle->admission();
    const Owner owner{session, epoch};
    const auto *e = entry(session, epoch);
    if (!admitted || destroying || callbackDepth || !e || !e->visible || e->session.phase != Phase::Remote ||
        focusOwner != owner || !focus.permits(owner, focusOwnerRevision) || !e->capture || e->capture->stopped)
      return std::nullopt;
    const KeyboardRouteBinding binding{owner, generation, focusOwnerRequest, focusOwnerRevision, e->session.geometry,
                                       scope};
    if (!validKeyboardRouteBinding(binding) || !AXIsProcessTrusted() || !CGPreflightScreenCaptureAccess() ||
        launchTime(e->identity.pid) != e->identity.launch)
      return std::nullopt;
    const auto active = NSWorkspace.sharedWorkspace.frontmostApplication;
    if (!active || active.processIdentifier != e->identity.pid)
      return std::nullopt;
    CF<AXUIElementRef> app(AXUIElementCreateApplication(e->identity.pid));
    if (!app)
      return std::nullopt;
    AXUIElementSetMessagingTimeout(app.get(), 0.25f);
    CF<CFTypeRef> actualFocus(attribute(app.get(), kAXFocusedWindowAttribute));
    CF<CFTypeRef> minimized(attribute(e->window, kAXMinimizedAttribute));
    const auto actualBounds = bounds(e->window);
    if (!actualFocus || !CFEqual(actualFocus.get(), e->window) || !minimized ||
        !CFEqual(minimized.get(), kCFBooleanFalse) || !actualBounds ||
        !sameGeometry(*actualBounds, e->session.source) || (e->moved && !holding.available()))
      return std::nullopt;
    return lifecycle->current(admitted) ? std::optional(binding) : std::nullopt;
  }
  bool clipboardCurrent(const std::shared_ptr<const ClipboardTarget> &target) const
  {
    const QPointer<NativeHost> alive(q);
    if (QThread::currentThread() != q->thread() || q->m_impl->handleAssociatedChild(target->binding.owner) || !alive)
      return false;
    const auto lifecycle = this->lifecycle;
    const auto current = [&] {
      if (!alive || QThread::currentThread() != alive->thread() || !lifecycle->current(target->admitted))
        return false;
      const auto *host = alive->m_impl.get();
      const auto &binding = target->binding;
      const auto *e = host->entry(binding.owner.session, binding.owner.epoch);
      return !host->destroying && !host->callbackDepth && e && e->visible && e->session.phase == Phase::Remote &&
             e->capture && !e->capture->stopped && host->focusOwner == binding.owner &&
             host->focusOwnerRequest == binding.focusRequest && host->focusOwnerRevision == binding.focusRevision &&
             host->focus.permits(binding.owner, binding.focusRevision) && e->session.geometry == binding.geometry &&
             e->identity.id == target->identity.id && e->identity.pid == target->identity.pid &&
             e->identity.launch == target->identity.launch && CFEqual(e->window, target->window.get()) &&
             sameGeometry(e->session.source, target->frame) && (!e->moved || host->holding.available());
    };
    if (!current() || !AXIsProcessTrusted() || !CGPreflightScreenCaptureAccess() || !current())
      return false;
    const auto native = identity(target->identity.id);
    if (!current() || !native || native->pid != target->identity.pid || native->launch != target->identity.launch ||
        !sameGeometry(native->frame, target->frame))
      return false;
    const auto active = NSWorkspace.sharedWorkspace.frontmostApplication;
    if (!current() || !active || active.processIdentifier != target->identity.pid)
      return false;
    CF<AXUIElementRef> app(AXUIElementCreateApplication(target->identity.pid));
    if (!current() || !app)
      return false;
    AXUIElementSetMessagingTimeout(app.get(), 0.25f);
    if (!current())
      return false;
    CF<CFTypeRef> focused(attribute(app.get(), kAXFocusedWindowAttribute));
    if (!current() || !focused || !CFEqual(focused.get(), target->window.get()))
      return false;
    CF<CFTypeRef> minimized(attribute(target->window.get(), kAXMinimizedAttribute));
    if (!current() || !minimized || !CFEqual(minimized.get(), kCFBooleanFalse))
      return false;
    const auto frame = bounds(target->window.get());
    return current() && frame && sameGeometry(*frame, target->frame);
  }
  std::optional<ClipboardFocusObservation> clipboardFocus() const
  {
    if (QThread::currentThread() != q->thread())
      return {};
    const QPointer<NativeHost> guard(q);
    if (q->m_impl->handleAssociatedChild(focusOwner) || !guard)
      return {};
    const auto admitted = lifecycle->admission();
    const auto *e = entry(focusOwner.session, focusOwner.epoch);
    const ClipboardFocusBinding binding{focusOwner, focusOwnerRequest, focusOwnerRevision, e ? e->session.geometry : 0};
    if (!admitted || !e || !e->window || !validClipboardFocus(binding))
      return {};
    const auto target = std::make_shared<const ClipboardTarget>(binding, *e, admitted);
    const QPointer<NativeHost> alive(q);
    if (!clipboardCurrent(target) || !alive)
      return {};
    return ClipboardFocusObservation{binding, [alive, target] {
                                       return alive && alive->m_impl->clipboardCurrent(target);
                                     }};
  }
  void completeAssociatedReturn(const NativeAssociatedParent &parent)
  {
    const auto *e = entry(parent.owner.session, parent.owner.epoch);
    if (!e || !e->associatedReturn.returning(parent))
      return;
    if (callbackDepth) {
      // At most one queued return per export. Never erase an Entry underneath
      // an outgoing NativeHost callback running a nested Qt loop.
      QTimer::singleShot(10, q, [this, parent] { completeAssociatedReturn(parent); });
      return;
    }
    restore(parent.owner.session, false);
  }
  bool handleAssociatedChild(Owner owner)
  {
    if (QThread::currentThread() != q->thread())
      return false;
    const auto admitted = lifecycle->admission();
    auto *e = entry(owner.session, owner.epoch);
    if (!e)
      return false;
    if (e->associatedReturn.blocked())
      return true;
    if (!admitted || destroying || !e->visible || !e->window ||
        (e->session.phase != Phase::Preparing && e->session.phase != Phase::Remote))
      return false;
    const QPointer<NativeHost> alive(q);
    const auto fence = lifecycle;
    const NativeAssociatedParent parent{owner, e->identity.id, e->identity.pid, e->identity.launch};
    CF<AXUIElementRef> retained(static_cast<AXUIElementRef>(CFRetain(e->window)));
    const auto current = [&] {
      if (!alive || !fence->current(admitted))
        return false;
      const auto *item = alive->m_impl->entry(owner.session, owner.epoch);
      return item && !alive->m_impl->destroying && item->identity.id == parent.window &&
             item->identity.pid == parent.process && item->identity.launch == parent.birth &&
             CFEqual(item->window, retained.get()) && !item->associatedReturn.blocked() &&
             (item->session.phase == Phase::Preparing || item->session.phase == Phase::Remote);
    };
    if (!AXIsProcessTrusted() || !current())
      return false;
    const auto active = NSWorkspace.sharedWorkspace.frontmostApplication;
    if (!current() || !active || active.processIdentifier != parent.process)
      return false;
    const auto native = identity(parent.window);
    if (!current() || !native || native->pid != parent.process || native->launch != parent.birth)
      return false;
    CF<AXUIElementRef> app(AXUIElementCreateApplication(parent.process));
    if (!current() || !app)
      return false;
    AXUIElementSetMessagingTimeout(app.get(), 0.25f);
    if (!current())
      return false;
    const auto element = [](CFTypeRef value) {
      return value && CFGetTypeID(value) == AXUIElementGetTypeID() ? static_cast<AXUIElementRef>(value) : nullptr;
    };
    CF<CFTypeRef> focused(attribute(app.get(), kAXFocusedUIElementAttribute));
    if (!current() || !element(focused.get()))
      return false;
    CF<CFTypeRef> top(attribute(element(focused.get()), kAXTopLevelUIElementAttribute));
    if (!current())
      return false;
    if (!element(top.get()) || CFEqual(top.get(), retained.get())) {
      // Some apps focus the Sheet/Drawer itself. Its parent shortcut can then
      // name the Window; retain the explicitly typed focused object instead.
      CF<CFTypeRef> focusedRole(attribute(element(focused.get()), kAXRoleAttribute));
      if (!current() || !focusedRole || CFGetTypeID(focusedRole.get()) != CFStringGetTypeID() ||
          (!CFEqual(focusedRole.get(), kAXSheetRole) && !CFEqual(focusedRole.get(), kAXDrawerRole)))
        return false;
      top = CF<CFTypeRef>(CFRetain(focused.get()));
    }
    if (!element(top.get()) || CFEqual(top.get(), retained.get()))
      return false;
    CF<CFTypeRef> role(attribute(element(top.get()), kAXRoleAttribute));
    if (!current() || !role || CFGetTypeID(role.get()) != CFStringGetTypeID())
      return false;
    const auto kind = CFEqual(role.get(), kAXSheetRole)    ? NativeAssociatedRole::Sheet
                      : CFEqual(role.get(), kAXDrawerRole) ? NativeAssociatedRole::Drawer
                                                           : NativeAssociatedRole::Other;
    if (kind == NativeAssociatedRole::Other)
      return false;
    // Apple's AXAttributeConstants.h specifies AXWindow as the attached Window
    // parent of a Sheet/Drawer, while AXTopLevelUIElement names that child. A
    // matching process alone is never sufficient to return an exported window.
    CF<CFTypeRef> attached(attribute(element(top.get()), kAXWindowAttribute));
    if (!current() || !element(attached.get()) || !CFEqual(attached.get(), retained.get()))
      return false;
    for (const auto item : {element(focused.get()), element(top.get()), element(attached.get())}) {
      pid_t pid = 0;
      if (AXUIElementGetPid(item, &pid) != kAXErrorSuccess || !current() || pid != parent.process)
        return false;
    }
    CF<CFTypeRef> freshFocus(attribute(app.get(), kAXFocusedUIElementAttribute));
    if (!current() || !freshFocus || !CFEqual(freshFocus.get(), focused.get()))
      return false;
    CF<CFTypeRef> freshParent(attribute(element(top.get()), kAXWindowAttribute));
    if (!current() || !freshParent || !CFEqual(freshParent.get(), retained.get()))
      return false;
    const auto freshNative = identity(parent.window);
    if (!current() || !freshNative || freshNative->pid != parent.process || freshNative->launch != parent.birth)
      return false;
    const auto freshActive = NSWorkspace.sharedWorkspace.frontmostApplication;
    if (!current() || !freshActive || freshActive.processIdentifier != parent.process)
      return false;
    e = entry(owner.session, owner.epoch);
    const NativeAssociatedEvidence evidence{{owner, native->id, native->pid, native->launch}, kind, true, true};
    if (!e || !e->associatedReturn.observe(parent, evidence))
      return false;
    // Fence first, before queueing restoration or invoking any user callback.
    // Existing owned releases can still run, but there is no new focus/input,
    // clipboard, capture relocation or re-export for this session incarnation.
    e->session.beginReturn();
    QTimer::singleShot(0, q, [this, parent] { completeAssociatedReturn(parent); });
    releases(owner, KeyboardRouteReason::FocusChanged);
    if (alive)
      error(
          kind == NativeAssociatedRole::Sheet
              ? "Attached macOS sheet interaction is unsupported remotely; returning its exact parent to the Mac"
              : "Attached macOS drawer interaction is unsupported remotely; returning its exact parent to the Mac"
      );
    return true;
  }
  bool qtInputEmpty() const
  {
    return ledger.size() == 0 && keys.empty() &&
           std::all_of(entries.begin(), entries.end(), [](const auto &item) { return item.second->buttons.empty(); });
  }
  bool quiesceKeyboard(const KeyboardRouteBinding &binding)
  {
    return keyboardAdmission.quiesce(
        binding, keyboardRouteCandidate(binding.owner.session, binding.owner.epoch, binding.generation, binding.scope),
        qtInputEmpty()
    );
  }
  bool keyboardRouteQuiesced(const KeyboardRouteBinding &binding) const
  {
    return keyboardAdmission.quiesced(
        binding, keyboardRouteCandidate(binding.owner.session, binding.owner.epoch, binding.generation, binding.scope),
        qtInputEmpty()
    );
  }
  bool originalInputCurrent(const KeyboardRouteBinding &binding, std::uint64_t captureEpoch) const
  {
    const auto admitted = lifecycle->admission();
    if (!admitted || QThread::currentThread() != q->thread() || destroying || callbackDepth || originalCleanupFailed ||
        binding.scope != ForwardingScope::All || !originalInput.current(binding, captureEpoch) || !originalTarget ||
        originalTarget->binding != binding)
      return false;
    QPointer<NativeHost> alive(q);
    const auto target = originalTarget;
    const auto candidate =
        keyboardRouteCandidate(binding.owner.session, binding.owner.epoch, binding.generation, ForwardingScope::All);
    if (!alive || !keyboardAdmission.quiesced(binding, candidate, qtInputEmpty()))
      return false;
    const auto current = identity(target->identity.id);
    if (!alive)
      return false;
    const auto frame = bounds(target->window.get());
    if (!alive)
      return false;
    const auto *e = entry(binding.owner.session, binding.owner.epoch);
    return lifecycle->current(admitted) && originalTarget == target && originalInput.current(binding, captureEpoch) &&
           current && frame && e && CFEqual(e->window, target->window.get()) && current->pid == target->identity.pid &&
           current->launch == target->identity.launch && sameGeometry(*frame, target->frame) &&
           sameGeometry(e->session.source, target->frame);
  }
  bool establishOriginalInput(const KeyboardRouteBinding &binding, std::uint64_t captureEpoch)
  {
    const auto admitted = lifecycle->admission();
    if (!admitted || QThread::currentThread() != q->thread() || destroying || callbackDepth || originalCleanupFailed ||
        !captureEpoch || binding.scope != ForwardingScope::All)
      return false;
    if (originalInput.binding())
      return originalInputCurrent(binding, captureEpoch);
    QPointer<NativeHost> alive(q);
    if (!keyboardRouteQuiesced(binding) || !alive)
      return false;
    const auto *e = entry(binding.owner.session, binding.owner.epoch);
    if (!e || !e->window || !eventSource || !targetedPointerAvailable() || !lifecycle->current(admitted))
      return false;
    auto target = std::make_shared<OriginalTarget>(binding, *e);
    if (!originalInput.establish(binding, captureEpoch))
      return false;
    originalTarget = std::move(target);
    if (originalInputCurrent(binding, captureEpoch) && lifecycle->current(admitted))
      return true;
    if (alive)
      revokeOriginalInput(binding);
    return false;
  }
  void revokeOriginalInput(const KeyboardRouteBinding &binding)
  {
    const auto target = originalTarget;
    const auto cleanup = originalInput.revoke(binding);
    if (!cleanup)
      return;
    originalTarget.reset();
    if (!target || target->binding != binding)
      return;
    // The endpoint is already dead before any native cleanup. Its retained AX
    // object/process birth names the destination. AppKit routes keyboard input
    // through its key window despite an event's window annotation, so a key
    // release also requires that exact original window to remain focused.
    // https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/EventOverview/EventArchitecture/EventArchitecture.html
    // Never redirect cleanup or raise/refocus a window to make it possible.
    const auto timestamp = std::max(cleanup->timestamp, localInputMonotonicNs());
    const auto postCleanup = [&](const OriginalInputRelease *release, const OriginalInputRecord *scroll) {
      bool missing = false;
      const auto current = identity(target->identity.id, &missing);
      if (missing || (current && (current->pid != target->identity.pid || current->launch != target->identity.launch)))
        return true; // The original recipient is gone, never send to its replacement.
      const auto frame = bounds(target->window.get());
      if (!current || !frame || !closeEnough(*frame, current->frame) || !AXIsProcessTrusted() ||
          launchTime(target->identity.pid) != target->identity.launch)
        return false;
      const bool keyboard = release && std::holds_alternative<OriginalInputKey>(release->press.payload);
      const auto keyDestinationCurrent = [&] {
        if (!keyboard)
          return true;
        const auto *owner = entry(target->binding.owner.session, target->binding.owner.epoch);
        if (owner && owner->associatedReturn.blocked())
          return false; // An attached sheet may consume parent-key-window ups.
        CF<AXUIElementRef> app(AXUIElementCreateApplication(target->identity.pid));
        CF<CFTypeRef> focused(attribute(app.get(), kAXFocusedWindowAttribute));
        return focused && CFEqual(focused.get(), target->window.get());
      };
      if (!keyDestinationCurrent())
        return false;
      const Point point{
          std::clamp(cleanup->point.x, 0.0, std::nextafter(frame->width, 0.0)),
          std::clamp(cleanup->point.y, 0.0, std::nextafter(frame->height, 0.0))
      };
      const auto global = CGPointMake(frame->x + point.x, frame->y + point.y);
      CF<CGEventRef> raw;
      std::int64_t number = 0;
      int clicks = 0;
      if (release) {
        raw = CF<CGEventRef>(createOriginalInputRelease(*release, timestamp, global, eventSource));
        if (const auto *button = std::get_if<OriginalInputButton>(&release->press.payload)) {
          number = target->buttons[button->button];
          clicks = int(button->clickCount);
        }
      } else {
        auto ending = *scroll;
        ending.sourceTimestampNs = timestamp;
        raw = CF<CGEventRef>(createOriginalInputEvent(ending, global, eventSource));
      }
      CF<CGEventRef> event(
          targetedOriginalInputEvent(raw.get(), eventSource, target->identity.id, *frame, point, number, clicks)
      );
      if (!event || !AXIsProcessTrusted() || launchTime(target->identity.pid) != target->identity.launch ||
          !keyDestinationCurrent())
        return false;
      CGEventPostToPid(target->identity.pid, event.get());
      return true;
    };
    bool complete = true;
    if (cleanup->scroll)
      complete = postCleanup(nullptr, &*cleanup->scroll);
    for (const auto &release : cleanup->releases)
      complete = postCleanup(&release, nullptr) && complete;
    if (!complete) {
      // Never reopen Qt acquisition over a native press whose release could
      // not be verified/submitted. Restart/recovery remains available.
      originalCleanupFailed = true;
      // Report after cleanup callers have unwound: onError may destroy the
      // host or initiate another restore, but the safety latch is immediate.
      const QPointer<NativeHost> alive(q);
      QTimer::singleShot(0, q, [alive] {
        if (alive)
          alive->m_impl->error("Original-input owner release could not be submitted; input admission remains closed");
      });
    }
  }
  bool deliverOriginalInput(
      const KeyboardRouteBinding &binding, const OriginalInputRecord &record, const LocalInputMappedPoint &point
  )
  {
    const auto admitted = lifecycle->admission();
    if (!admitted || QThread::currentThread() != q->thread() || !originalInput.current(binding, record.epoch))
      return false;
    QPointer<NativeHost> alive(q);
    const auto deny = [&] {
      if (alive)
        revokeOriginalInput(binding);
      return false;
    };
    if (!originalInputCurrent(binding, record.epoch) || !alive)
      return deny();
    const auto target = originalTarget;
    const auto prepared = originalInput.prepare(binding, record, point, target->frame.width, target->frame.height);
    if (!prepared)
      return deny();
    std::int64_t number = 0;
    int clicks = 0;
    std::optional<std::uint8_t> buttonSlot;
    bool buttonDown = false;
    if (const auto *button = std::get_if<OriginalInputButton>(&record.payload)) {
      buttonSlot = button->button;
      buttonDown = button->down;
      if (buttonDown && mouseSequence == std::numeric_limits<std::int64_t>::max())
        return deny();
      number = buttonDown ? mouseSequence + 1 : target->buttons[*buttonSlot];
      clicks = int(button->clickCount);
    } else if (
        const auto *motion = std::get_if<OriginalInputMotion>(&record.payload);
        motion && motion->dragButton < originalInputButtonCount
    ) {
      number = target->buttons[motion->dragButton];
      clicks = int(std::get<OriginalInputButton>(prepared->press->payload).clickCount);
    }
    CF<CGEventRef> event;
    if (prepared->posts) {
      CF<CGEventRef> raw(createOriginalInputEvent(
          record, CGPointMake(target->frame.x + point.x, target->frame.y + point.y), eventSource
      ));
      event = CF<CGEventRef>(targetedOriginalInputEvent(
          raw.get(), eventSource, target->identity.id, target->frame, {point.x, point.y}, number, clicks
      ));
      if (!event)
        return deny();
    }
    // Native identity/focus reads may block. The receipt is never renewed by
    // them; recheck it and the pending exact owner immediately before posting.
    if (!originalInputCurrent(binding, record.epoch) || !alive || !liveLocalInputMappedPoint(point) ||
        originalTarget != target || !originalInput.pending(prepared->token) || !lifecycle->current(admitted))
      return deny();
    if (prepared->posts)
      CGEventPostToPid(target->identity.pid, event.get());
    // CGEventPostToPid is a synchronous submission and runs no application/Qt
    // callbacks. Record the submitted press before any further native queries.
    if (!originalInput.submitted(prepared->token))
      return deny();
    if (buttonSlot) {
      target->buttons[*buttonSlot] = buttonDown ? number : 0;
      if (buttonDown)
        mouseSequence = number;
    }
    if (!liveLocalInputMappedPoint(point) || !originalInputCurrent(binding, record.epoch) || !alive ||
        !lifecycle->current(admitted))
      return deny();
    return true;
  }
  void invalidateKeyboard(KeyboardRouteReason reason, std::optional<Owner> owner = std::nullopt)
  {
    if (const auto binding = originalInput.binding(); binding && (!owner || binding->owner == *owner))
      revokeOriginalInput(*binding);
    const auto binding = keyboardAdmission.invalidate(owner);
    if (binding && !destroying && q->onKeyboardRouteInvalidated) {
      CallbackScope scope(*this);
      const auto callback = q->onKeyboardRouteInvalidated;
      callback(*binding, reason);
    }
  }
  QJsonObject metadata(const Entry &e) const
  {
    return {
        {"title", e.identity.title.left(512)},
        {"windowId", double(e.identity.id)},
        {"width", qRound(e.session.source.width)},
        {"height", qRound(e.session.source.height)},
        {"pixelWidth", e.pixelWidth},
        {"pixelHeight", e.pixelHeight},
        {"geometry", double(e.session.geometry)},
        {"codec", "h264"},
        {"phase", e.session.phase == Phase::Remote ? "remote" : "preparing"},
        {"sourceVisible", !e.moved},
        {"windowDrag", e.windowDrag ? double(e.windowDrag->binding().drag) : 0},
        {"resizeRequest", double(e.resizeRequest)},
        {"hardwareEncoder", e.capture && e.capture->hardware.load()}
    };
  }
  void opened(const Entry &e)
  {
    if (lifecycle->admission() && q->onOpened) {
      CallbackScope scope(*this);
      q->onOpened(e.session.id, e.session.epoch, metadata(e));
    }
  }
  QJsonObject journalRecord(const Entry &e) const
  {
    return {
        {"windowId", double(e.identity.id)},
        {"pid", e.identity.pid},
        {"launch", double(e.identity.launch)},
        {"bundle", e.identity.bundle},
        {"title", e.identity.title},
        {"original", json(e.session.original)},
        {"session", double(e.session.id)},
        {"epoch", double(e.session.epoch)},
        {"restoreRequired", e.sourceTransaction->journalRestoreRequired()}
    };
  }
  bool writeJournal()
  {
    QJsonArray records = recovery;
    for (const auto &[id, e] : entries)
      if (e->session.journaled)
        records.append(journalRecord(*e));
    QString diagnostic;
    if (!persistJournal(journalPath, records, &diagnostic)) {
      error(std::move(diagnostic));
      return false;
    }
    return true;
  }
  void recoverJournal()
  {
    if (recovered)
      return;
    QFile file(journalPath);
    if (!file.exists()) {
      recovered = true;
      return;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxJournalBytes) {
      error("Cannot read window recovery journal; export disabled");
      return;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject() || document["version"].toInt() != 1 ||
        !document["windows"].isArray() || document["windows"].toArray().size() > maxSessions * 2) {
      error("Invalid window recovery journal; preserve it and recover windows manually before exporting");
      return;
    }
    const auto records = document["windows"].toArray();
    recovery = {};
    for (const auto &value : records) {
      const auto record = value.toObject();
      const auto id = integer(record["windowId"], UINT32_MAX), pid = integer(record["pid"], INT_MAX),
                 launch = integer(record["launch"]);
      const auto original = rect(record["original"].toObject());
      const auto restoreRequired = nativeJournalRestoreRequired(record);
      const auto savedSession = integer(record["session"]), savedEpoch = integer(record["epoch"]);
      if (!id || !*id || !pid || !*pid || !launch || !*launch || !original.valid() || !restoreRequired ||
          (!*restoreRequired && (!savedSession || !*savedSession || !savedEpoch || !*savedEpoch))) {
        recovery.append(value);
        error("Recovery journal contains an invalid window entry; export disabled");
        continue;
      }
      // A durable untouched preparation has never owned native placement or
      // remote input. Retire only its record; do not query/move/raise the source
      // or disturb a local drag that continued after this host exited.
      if (!*restoreRequired)
        continue;
      if (!AXIsProcessTrusted()) {
        recovery.append(value);
        error("Accessibility permission is required to restore journaled windows; grant it and rerun --recover");
        continue;
      }
      bool missing = false;
      auto current = identity(CGWindowID(*id), &missing);
      // An exited process/window needs no restoration. Never move a replacement
      // process merely because it reused the saved PID or window number.
      if (missing || (current && (current->pid != pid_t(*pid) || current->launch != qint64(*launch))))
        continue;
      if (!current) {
        recovery.append(value);
        error("Cannot verify a journaled window's identity; recovery entry retained");
        continue;
      }
      CF<AXUIElementRef> window(matchAX(*current));
      const auto destination = recoverFrame(original);
      if (!window || !destination || !setBounds(window.get(), *destination)) {
        recovery.append(value);
        error(QString("Could not restore %1; recovery journal retained").arg(record["title"].toString()));
      } else
        AXUIElementPerformAction(window.get(), kAXRaiseAction);
    }
    // A failed durable rewrite must remain a failed --recover result. Retrying
    // the original journal is safe even if some of its windows already moved.
    recovered = writeJournal();
  }
  void queueLifecycleRestore()
  {
    // Owner-thread/event-tap path: no mutex, native queries, I/O or waits.
    if (lifecycle->cleanup() && lifecycle->claimQueue() &&
        !QMetaObject::invokeMethod(q, [this] { restoreLifecycle(); }, Qt::QueuedConnection))
      lifecycle->releaseQueue();
  }
  void restoreLifecycle()
  {
    const auto fence = lifecycle;
    const auto ticket = fence->cleanup();
    const QPointer<NativeHost> alive(q);
    if (ticket) {
      restoreAll(KeyboardRouteReason::AuthorityLost);
      if (!alive) {
        fence->releaseQueue();
        return;
      }
      // A failed original release or unresolved restoration never reopens
      // admission merely because the desktop became active again.
      if (entries.empty() && recovery.isEmpty() && recovered && !originalCleanupFailed && writeJournal() && alive)
        fence->complete(ticket);
    }
    fence->releaseQueue();
    // A notification during restoration needs fresh cleanup. Failure of this
    // same ticket stays closed without spinning an unbounded retry queue.
    if (alive && fence->cleanup() && fence->cleanup() != ticket)
      queueLifecycleRestore();
  }
  void installSafetyHooks()
  {
    auto life = lifetime;
    auto fence = lifecycle;
    auto notify = ^(NativeHostLifecycle::Event event) {
      // Do this before acquiring the enqueue/destruction mutex. Even a busy
      // owner thread immediately loses its old admission/relocation token.
      fence->signal(event);
      if (!fence->claimQueue())
        return;
      std::lock_guard lock(life->mutex);
      if (!life->receiver ||
          !QMetaObject::invokeMethod(
              life->receiver, [owner = life->receiver] { owner->m_impl->restoreLifecycle(); }, Qt::QueuedConnection
          ))
        fence->releaseQueue();
    };
    auto center = NSWorkspace.sharedWorkspace.notificationCenter;
    sleepObserver = [center addObserverForName:NSWorkspaceWillSleepNotification
                                        object:nil
                                         queue:nil
                                    usingBlock:^(NSNotification *) {
                                      notify(NativeHostLifecycle::Event::Sleep);
                                    }];
    wakeObserver = [center addObserverForName:NSWorkspaceDidWakeNotification
                                       object:nil
                                        queue:nil
                                   usingBlock:^(NSNotification *) {
                                     notify(NativeHostLifecycle::Event::Wake);
                                   }];
    sessionObserver = [center addObserverForName:NSWorkspaceSessionDidResignActiveNotification
                                          object:nil
                                           queue:nil
                                      usingBlock:^(NSNotification *) {
                                        notify(NativeHostLifecycle::Event::Deactivate);
                                      }];
    activeObserver = [center addObserverForName:NSWorkspaceSessionDidBecomeActiveNotification
                                         object:nil
                                          queue:nil
                                     usingBlock:^(NSNotification *) {
                                       notify(NativeHostLifecycle::Event::Activate);
                                     }];
    installTap();
  }
  void installTap()
  {
    if (tap || !AXIsProcessTrusted())
      return;
    tap = CGEventTapCreate(
        kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, CGEventMaskBit(kCGEventKeyDown),
        emergency, this
    );
    if (!tap) {
      error("Emergency restore hotkey unavailable; use --recover or disconnect to restore windows");
      return;
    }
    tapSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    if (tapSource)
      CFRunLoopAddSource(CFRunLoopGetMain(), tapSource, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);
  }
  static CGEventRef emergency(CGEventTapProxy, CGEventType type, CGEventRef event, void *context)
  {
    auto *self = static_cast<Impl *>(context);
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
      if (self->tap)
        CGEventTapEnable(self->tap, true);
      return event;
    }
    if (type != kCGEventKeyDown || CGEventGetIntegerValueField(event, kCGEventSourceUserData) == syntheticEventTag)
      return event;
    const auto modifiers = CGEventGetFlags(event);
    const auto required = kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskShift;
    if (CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode) == kVK_Escape &&
        (modifiers & required) == required) {
      // No AX calls, I/O, or waits in an event tap.
      self->lifecycle->signal(NativeHostLifecycle::Event::Emergency);
      self->queueLifecycleRestore();
    }
    return event;
  }
  void refresh()
  {
    if (refreshing)
      return;
    if (!CGPreflightScreenCaptureAccess()) {
      error(
          "Screen Recording permission is required; use --request-permissions and enable Deskflow Windowshare in "
          "System Settings"
      );
      return;
    }
    refreshing = true;
    auto life = lifetime;
    [SCShareableContent
        getShareableContentExcludingDesktopWindows:YES
                               onScreenWindowsOnly:NO
                                 completionHandler:^(SCShareableContent *content, NSError *failure) {
                                   std::lock_guard lock(life->mutex);
                                   if (!life->receiver)
                                     return;
                                   QMetaObject::invokeMethod(
                                       life->receiver,
                                       [this, content, failure] {
                                         refreshing = false;
                                         if (failure || !content) {
                                           error(
                                               "Cannot enumerate capturable windows: " +
                                               string(failure.localizedDescription)
                                           );
                                           return;
                                         }
                                         catalog.clear();
                                         QVector<Candidate> candidates;
                                         for (SCWindow *window in content.windows) {
                                           if (window.windowLayer != 0 || !window.owningApplication ||
                                               window.owningApplication.processID == getpid() ||
                                               window.frame.size.width < 32 || window.frame.size.height < 32)
                                             continue;
                                           catalog[window.windowID] = window;
                                           const auto application = string(window.owningApplication.applicationName);
                                           auto title = string(window.title);
                                           if (title.isEmpty())
                                             title = application;
                                           else
                                             title = application + " — " + title;
                                           candidates.push_back({window.windowID, title});
                                         }
                                         if (q->onCatalog) {
                                           CallbackScope scope(*this);
                                           q->onCatalog(std::move(candidates));
                                         }
                                       },
                                       Qt::QueuedConnection
                                   );
                                 }];
  }
  SCStreamConfiguration *configuration(Entry &e, bool active)
  {
    auto config = [[SCStreamConfiguration alloc] init];
    double scale = 1;
    if (@available(macOS 14.0, *)) {
      auto filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:e.shareable];
      scale = std::clamp(double(filter.pointPixelScale), 1.0, 2.0);
    }
    const double reduction = std::min(
        1.0, double(maxEncodedDimension) / std::max(e.session.source.width * scale, e.session.source.height * scale)
    );
    e.pixelWidth = std::max(2, int(std::ceil(e.session.source.width * scale * reduction)) & ~1);
    e.pixelHeight = std::max(2, int(std::ceil(e.session.source.height * scale * reduction)) & ~1);
    config.width = e.pixelWidth;
    config.height = e.pixelHeight;
    config.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    config.minimumFrameInterval = CMTimeMake(1, effectiveCaptureRate(selectedCaptureRate, active, e.visible));
    config.queueDepth = 3;
    config.showsCursor = NO;
    config.scalesToFit = YES;
    config.capturesAudio = NO;
    config.colorSpaceName = kCGColorSpaceSRGB;
    config.colorMatrix = kCGDisplayStreamYCbCrMatrix_ITU_R_709_2;
    if (@available(macOS 14.0, *)) {
      config.ignoreShadowsSingleWindow = YES;
      config.ignoreGlobalClipSingleWindow = YES;
      config.preservesAspectRatio = NO;
    }
    if (@available(macOS 14.2, *))
      config.includeChildWindows = YES;
    return config;
  }
  void updateCapture(Entry &e)
  {
    if (!e.stream)
      return;
    e.configuration = configuration(e, focusOwner == Owner{e.session.id, e.session.epoch});
    auto capture = e.capture;
    const auto revision = e.session.geometry;
    const int width = e.pixelWidth, height = e.pixelHeight;
    const auto rate = static_cast<unsigned>(e.configuration.minimumFrameInterval.timescale);
    const bool changed = capture->configure(revision, width, height, rate);
    const auto configurationGeneration = capture->configurationGeneration.load();
    [e.stream updateConfiguration:e.configuration
                completionHandler:^(NSError *failure) {
                  if (failure) {
                    capture->fail("Cannot update window capture: " + string(failure.localizedDescription));
                    return;
                  }
                  if (changed)
                    dispatch_async(capture->queue, ^{
                      std::lock_guard lock(capture->mutex);
                      if (capture->stopped || capture->geometry != revision || capture->expectedWidth != width ||
                          capture->expectedHeight != height || capture->frameRate != rate ||
                          capture->configurationGeneration != configurationGeneration)
                        return;
                      // The output serial queue drains older callbacks before accepting the
                      // updated configuration. Dimensions independently reject stale buffers.
                      capture->acceptingFrames = true;
                      capture->forceKeyframe = true;
                    });
                }];
    capture->forceKeyframe = true;
  }
  void exportWindow(std::uint32_t id, unsigned attempt = 0, std::optional<WindowDragBinding> drag = std::nullopt)
  {
    const auto admitted = lifecycle->admission();
    const QPointer<NativeHost> alive(q);
    if (!admitted || destroying)
      return;
    recoverJournal();
    if (!alive || !lifecycle->current(admitted))
      return;
    if (!recovered || !recovery.isEmpty()) {
      error("Unresolved recovery journal entries prevent a new export");
      return;
    }
    const auto currentOutputs = physicalOutputSnapshot();
    if (!lifecycle->current(admitted))
      return;
    if (!currentOutputs) {
      error("Cannot verify active physical monitor identity/mode; export disabled");
      return;
    }
    if (entries.empty())
      physicalAtExport = *currentOutputs;
    else if (!physicalAtExport || !samePhysicalOutputLayout(*physicalAtExport, *currentOutputs)) {
      error("Physical output identity/mode changed during export; restoring old sessions");
      restoreAll(KeyboardRouteReason::AuthorityLost);
      return;
    }
    if (!CGPreflightScreenCaptureAccess() || !AXIsProcessTrusted()) {
      error("Export requires Screen Recording and Accessibility permission");
      return;
    }
    if (!eventSource) {
      error("Cannot create private input event source");
      return;
    }
    if (!targetedPointerAvailable()) {
      error("Targeted pointer input requires unavailable private macOS window-location API; export disabled");
      return;
    }
    if (entries.size() >= maxSessions) {
      error("At most 16 windows may be exported at once");
      return;
    }
    for (const auto &[session, e] : entries)
      if (e->identity.id == id) {
        error("This window is already exported");
        return;
      }
    const auto found = catalog.find(id);
    if (found == catalog.end()) {
      error("Window is no longer in the capture catalog; refresh and retry");
      return;
    }
    auto current = identity(id);
    if (!current || current->pid != found->second.owningApplication.processID) {
      error("Window identity changed; refresh and retry");
      return;
    }
    auto e = std::make_unique<Entry>();
    QString diagnostic;
    bool retryable = false;
    e->window = matchAX(*current, &diagnostic, &retryable);
    if (!alive || !lifecycle->current(admitted))
      return;
    if (!e->window) {
      if (!drag && retryable && attempt < 3) {
        const auto generation = exportGeneration;
        QTimer::singleShot(250, q, [this, id, attempt, generation, admitted] {
          if (generation == exportGeneration && lifecycle->current(admitted))
            exportWindow(id, attempt + 1);
        });
        return;
      }
      error(QString("Cannot unambiguously match selected window %1 (process %2) through Accessibility: %3")
                .arg(id)
                .arg(current->pid)
                .arg(diagnostic));
      return;
    }
    if (drag) {
      const auto retainedTitlebar = titlebar;
      const auto observed = retainedTitlebar ? retainedTitlebar->read() : std::nullopt;
      if (!alive || !lifecycle->current(admitted))
        return;
      if (!observed || titlebar != retainedTitlebar || observed->process != current->pid ||
          !retainedTitlebar->matchesNativeWindow(e->window) ||
          WindowDragTransaction::Clock::now() >= titlebarDeadline) {
        error("Titlebar identity changed before drag preparation; preserving the source window");
        return;
      }
    }
    CF<CFTypeRef> minimized(attribute(e->window, kAXMinimizedAttribute));
    if (minimized && CFEqual(minimized.get(), kCFBooleanTrue)) {
      error("Unminimize this window before exporting it");
      return;
    }
    e->identity = *current;
    e->shareable = found->second;
    auto axFrame = bounds(e->window);
    if (!lifecycle->current(admitted))
      return;
    if (!axFrame) {
      error("Cannot read this window's Accessibility geometry");
      return;
    }
    do {
      e->session.id = QRandomGenerator::system()->generate64() & maxJsonInteger;
    } while (!e->session.id || entries.count(e->session.id));
    e->session.original = *axFrame;
    e->session.journaled = true;
    e->session.prepare(1, *axFrame, *axFrame);
    if (drag) {
      e->windowDrag = std::make_shared<WindowDragTransaction>();
      if (!e->windowDrag->begin(*drag, e->session.geometry))
        return;
    }
    e->started = QDateTime::currentMSecsSinceEpoch();
    e->capture = std::make_shared<Capture>();
    e->capture->receiver = q;
    const auto session = e->session.id, epoch = e->session.epoch;
    e->capture->frame = [this, session, epoch](std::uint64_t geometry, QByteArray bytes) {
      auto *entry = this->entry(session, epoch);
      if (!lifecycle->admission() || !entry || geometry != entry->session.geometry ||
          (entry->session.phase != Phase::Preparing && entry->session.phase != Phase::Remote))
        return;
      entry->producedGeometry = geometry;
      if (entry->windowDrag && entry->session.phase == Phase::Preparing && !entry->windowDrag->produced(geometry))
        return;
      if (!entry->session.firstFrame)
        entry->session.frameReady(epoch);
      if (q->onFrame) {
        CallbackScope scope(*this);
        q->onFrame(session, epoch, geometry, std::move(bytes));
      }
    };
    e->capture->error = [this, session, epoch](QString message) {
      const QPointer<NativeHost> alive(q);
      if (!entry(session, epoch))
        return;
      releases(Owner{session, epoch}, KeyboardRouteReason::Failed);
      if (!alive || !entry(session, epoch))
        return;
      error(std::move(message));
      if (alive && entry(session, epoch))
        restore(session);
    };
    e->delegate = [[DFWindowStream alloc] init];
    e->delegate->capture = e->capture;
    e->configuration = configuration(*e, true);
    e->capture->geometry = e->session.geometry;
    e->capture->expectedWidth = e->pixelWidth;
    e->capture->expectedHeight = e->pixelHeight;
    e->capture->frameRate = selectedCaptureRate;
    auto filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:e->shareable];
    e->stream = [[SCStream alloc] initWithFilter:filter configuration:e->configuration delegate:e->delegate];
    NSError *failure = nil;
    if (![e->stream addStreamOutput:e->delegate
                               type:SCStreamOutputTypeScreen
                 sampleHandlerQueue:e->capture->queue
                              error:&failure]) {
      error("Cannot attach window capture output: " + string(failure.localizedDescription));
      return;
    }
    if (!lifecycle->current(admitted)) {
      e->capture->shutdown();
      return;
    }
    entries.emplace(session, std::move(e));
    const bool persisted = writeJournal();
    if (!alive)
      return;
    if (!persisted) {
      auto it = entries.find(session);
      if (it != entries.end()) {
        it->second->capture->shutdown();
        entries.erase(it);
      }
      return;
    }
    if (!lifecycle->current(admitted))
      return;
    auto &stored = *entries.at(session);
    auto capture = stored.capture;
    opened(stored);
    if (!alive || !lifecycle->current(admitted))
      return;
    auto *stillOpen = entry(session, epoch);
    if (!stillOpen)
      return;
    [stillOpen->stream startCaptureWithCompletionHandler:^(NSError *failure) {
      if (failure)
        capture->fail("Cannot start window capture: " + string(failure.localizedDescription));
    }];
  }
  void ready(std::uint64_t session, std::uint64_t epoch, std::uint64_t paintedGeometry)
  {
    const QPointer<NativeHost> childGuard(q);
    if (handleAssociatedChild({session, epoch}) || !childGuard)
      return;
    const auto admitted = lifecycle->admission();
    if (!admitted || QThread::currentThread() != q->thread() || destroying)
      return;
    auto *e = entry(session, epoch);
    if (!e || e->session.phase != Phase::Preparing || !e->session.firstFrame || !paintedGeometry ||
        paintedGeometry != e->session.geometry || e->producedGeometry != paintedGeometry || e->relocating)
      return;
    const auto drag = e->windowDrag;
    if (drag) {
      if (!drag->painted(paintedGeometry) || !e->dragReleaseFrameObserved || !drag->reserveCommit(drag->binding()))
        return; // A painted preview cannot end the original native drag.
    }
    const QPointer<NativeHost> alive(q);
    const auto expected = e->identity;
    const auto paintedSource = e->session.source;
    CF<AXUIElementRef> window(static_cast<AXUIElementRef>(CFRetain(e->window)));
    const auto capture = e->capture;
    const auto sourceTransaction = e->sourceTransaction;
    bool placementStarted = false;
    e->relocating = true; // A nested/duplicate Ready cannot own the same move.
    const auto current = [&]() -> Entry * {
      if (!alive || !lifecycle->current(admitted))
        return nullptr;
      auto *item = entry(session, epoch);
      return item && item->session.phase == Phase::Preparing && item->relocating && item->visible &&
                     item->windowDrag == drag && (!drag || drag->committing(drag->binding())) &&
                     item->session.geometry == paintedGeometry && item->producedGeometry == paintedGeometry &&
                     item->capture == capture && item->sourceTransaction == sourceTransaction && !capture->stopped &&
                     !capture->failurePending && item->identity.id == expected.id &&
                     item->identity.pid == expected.pid && item->identity.launch == expected.launch &&
                     CFEqual(item->window, window.get())
                 ? item
                 : nullptr;
    };
    const auto readWindow = [&]() -> std::optional<HoldingRelocationWindow> {
      if (!current())
        return std::nullopt;
      const auto observed = identity(expected.id);
      const auto actual = bounds(window.get());
      CF<CFTypeRef> minimized(attribute(window.get(), kAXMinimizedAttribute));
      if (!current() || !observed || observed->pid != expected.pid || observed->launch != expected.launch || !actual ||
          !closeEnough(*actual, observed->frame) || (!placementStarted && !sameGeometry(*actual, paintedSource)) ||
          (minimized && CFEqual(minimized.get(), kCFBooleanTrue)) || !AXIsProcessTrusted() ||
          !CGPreflightScreenCaptureAccess())
        return std::nullopt;
      return HoldingRelocationWindow{{session, epoch}, paintedGeometry, paintedGeometry, *actual};
    };
    const auto layoutCurrent = [&] {
      if (!current())
        return false;
      const auto outputs = physicalOutputSnapshot();
      return current() && physicalAtExport && outputs && samePhysicalOutputLayout(*physicalAtExport, *outputs);
    };
    const auto failed = [&](QString message, bool all = false) {
      if (!alive)
        return;
      if (auto *item = entry(session, epoch))
        item->relocating = false;
      error(std::move(message));
      if (!alive)
        return;
      if (all)
        restoreAll(KeyboardRouteReason::AuthorityLost);
      else if (entry(session, epoch))
        restore(session);
    };
    const auto before = readWindow();
    if (!before || !current() || !sameGeometry(before->frame, current()->session.source)) {
      failed("Source identity or geometry changed after the painted frame; preserving its physical visibility");
      return;
    }
    if (!layoutCurrent()) {
      failed("Physical monitor topology changed before source relocation; restoring all exported windows", true);
      return;
    }
    if (holdingEnabled) {
      if (!holding.available()) {
        const bool hasHiddenSources =
            std::any_of(entries.begin(), entries.end(), [](const auto &item) { return item.second->moved; });
        if (hasHiddenSources) {
          failed("Holding display changed while sources were exported; restoring all windows", true);
          return;
        }
      }
      QString failure;
      if (holding.available() || holding.create(&failure, 4096, 4096)) {
        // Creating the virtual holding display can itself rearrange existing
        // physical outputs. Never move a window using a stale source layout.
        if (!alive)
          return;
        if (!layoutCurrent()) {
          failed("Physical outputs changed during holding-display creation; restoring sources", true);
          return;
        }
        HoldingRelocationAccess access;
        access.window = readWindow;
        access.display = [&]() -> std::optional<HoldingResizeDisplay> {
          if (!layoutCurrent() || !holding.available())
            return std::nullopt;
          return HoldingResizeDisplay{holding.displayId(), holding.bounds()};
        };
        access.persist = [&] {
          return current() && current()->session.journaled &&
                 sourceTransaction->arm([&] { return current() && writeJournal() && current(); });
        };
        access.place = [&](Rect destination) {
          if (!current() || !sourceTransaction->attemptMutation())
            return false;
          placementStarted = true;
          return setBounds(window.get(), destination, [&] { return current() != nullptr; });
        };
        const auto result = relocateHoldingWindow({session, epoch}, paintedGeometry, access);
        if (!alive)
          return;
        e = current();
        if (!e || result.status != HoldingRelocationResult::Status::Applied) {
          failed("Holding display placement did not verify; restoring source window");
          return;
        }
        e->session.source = result.actual;
        e->moved = true;
        ++e->session.geometry;
        updateCapture(*e);
      } else {
        if (!alive)
          return;
        error("Holding display unavailable; source remains visible: " + failure);
        if (!alive)
          return;
        e = current();
        if (!e || !readWindow() || !layoutCurrent()) {
          failed("Source-visible fallback changed during holding-display creation; restoring source");
          return;
        }
      }
    }
    // The default source-visible fallback never parks a window off-screen.
    if (!alive)
      return;
    e = entry(session, epoch);
    if (!e || !e->relocating || e->session.phase != Phase::Preparing || !lifecycle->current(admitted))
      return;
    // A source-visible committed session can still receive native input and
    // resize/focus changes. Persist its recovery intent before enabling those
    // operations, just as for a holding-display mutation.
    if (e->sourceTransaction != sourceTransaction || !sourceTransaction->arm([&] {
          const auto *item = entry(session, epoch);
          return item && item->sourceTransaction == sourceTransaction && writeJournal() && alive;
        })) {
      failed("Cannot persist source recovery before committing the export");
      return;
    }
    if (!alive || !lifecycle->current(admitted))
      return;
    e = entry(session, epoch);
    if (!e || e->sourceTransaction != sourceTransaction || e->session.phase != Phase::Preparing || !e->relocating ||
        !sourceTransaction->commit())
      return;
    if (drag && !drag->committed(drag->binding())) {
      failed("Window drag expired during source relocation; restoring source");
      return;
    }
    e->relocating = false;
    e->session.commit(epoch);
    opened(*e);
    if (alive && lifecycle->current(admitted) && entry(session, epoch) && q->onCommitted) {
      CallbackScope scope(*this);
      q->onCommitted(session, epoch);
    }
  }
  bool post(
      Entry &e, CGEventRef event, CGEventFlags modifiers, NativeHostLifecycle::Admission admitted, bool cleanup = false
  )
  {
    if (cleanup && e.associatedReturn.blocked() && event &&
        (CGEventGetType(event) == kCGEventKeyUp || CGEventGetType(event) == kCGEventFlagsChanged)) {
      // A sheet can share its parent's AXFocusedWindow while owning AppKit key
      // dispatch. Do not redirect the parent's release into that child or claim
      // successful cleanup. Restore remains possible with this latch closed.
      originalCleanupFailed = true;
      return false;
    }
    if (!event || (!cleanup && !lifecycle->current(admitted)) || !AXIsProcessTrusted() ||
        launchTime(e.identity.pid) != e.identity.launch)
      return false;
    CGEventSetIntegerValueField(event, kCGEventSourceUserData, syntheticEventTag);
    CGEventSetFlags(event, modifiers);
    CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointer, e.identity.id);
    CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, e.identity.id);
    if (!cleanup && !lifecycle->current(admitted))
      return false;
    CGEventPostToPid(e.identity.pid, event);
    return true;
  }
  CGEventRef pointerEvent(
      Entry &e, CGEventType type, std::uint32_t button, CGEventFlags modifiers, std::int64_t eventNumber = 0,
      int clicks = 0
  )
  {
    auto event = targetedPointerEvent(
        eventSource, e.identity.id, e.session.source, {e.pointer.x(), e.pointer.y()}, type, button, modifiers,
        eventNumber, clicks
    );
    if (!event) {
      // Native construction can fail while a caller retains an Entry. Report
      // after that caller unwinds rather than allowing onError to destroy it.
      QTimer::singleShot(0, q, [this] {
        error("macOS targeted pointer construction failed; private window-location API required");
      });
    }
    return event;
  }
  bool
  key(Entry &e, const KeyPress &press, bool down, bool repeat = false, NativeHostLifecycle::Admission admitted = {})
  {
    CF<CGEventRef> event(CGEventCreateKeyboardEvent(eventSource, press.code, down));
    if (!event)
      return false;
    if (modifier(press.code))
      CGEventSetType(event.get(), kCGEventFlagsChanged);
    CGEventSetIntegerValueField(event.get(), kCGKeyboardEventAutorepeat, repeat);
    if (!press.text.isEmpty() && !(press.flags & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl))) {
      CGEventKeyboardSetUnicodeString(
          event.get(), press.text.size(), reinterpret_cast<const UniChar *>(press.text.utf16())
      );
    }
    return post(e, event.get(), press.flags, admitted, !down);
  }
  bool button(
      Entry &e, std::uint32_t code, bool down, CGEventFlags modifiers = 0, int clicks = 1,
      NativeHostLifecycle::Admission admitted = {}
  )
  {
    const CGEventType type = code == 0   ? (down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp)
                             : code == 1 ? (down ? kCGEventRightMouseDown : kCGEventRightMouseUp)
                                         : (down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp);
    if (down)
      e.buttons[code] = {++mouseSequence, clicks};
    const auto found = e.buttons.find(code);
    const auto number = found == e.buttons.end() ? ++mouseSequence : found->second.first;
    if (!down && found != e.buttons.end()) {
      clicks = found->second.second;
      e.buttons.erase(found);
    }
    CF<CGEventRef> event(pointerEvent(e, type, code, modifiers, number, clicks));
    if (event) {
      CGEventSetIntegerValueField(event.get(), kCGMouseEventClickState, clicks);
      if (post(e, event.get(), modifiers, admitted, !down))
        return true;
    }
    if (down)
      e.buttons.erase(code);
    return false;
  }
  void
  releases(std::optional<Owner> owner = std::nullopt, KeyboardRouteReason reason = KeyboardRouteReason::FocusChanged)
  {
    const QPointer<NativeHost> alive(q);
    if (!owner || *owner == focusOwner)
      clipboardMonitor->resetBaseline();
    if (const auto binding = originalInput.binding(); binding && (!owner || binding->owner == *owner))
      revokeOriginalInput(*binding);
    Owner revoked;
    std::uint64_t revokedRequest = 0;
    if (!owner || *owner == focusOwner) {
      revoked = focusOwner;
      revokedRequest = focusOwnerRequest;
      focus.revoke();
      focusOwner = {};
      focusOwnerRequest = 0;
      focusOwnerRevision = 0;
    }
    for (const auto &press : ledger.release(owner)) {
      auto *e = entry(press.owner.session, press.owner.epoch);
      if (press.keyboard) {
        const auto saved = keys.find({press.owner.session, press.code});
        if (saved != keys.end()) {
          auto release = saved->second;
          release.flags = 0;
          if (e)
            key(*e, release, false);
          keys.erase(saved);
        }
      } else if (e)
        button(*e, press.code, false);
    }
    invalidateKeyboard(reason, owner);
    if (alive && revoked.session && revokedRequest && !destroying && q->onFocused) {
      // Revoke the acknowledged request, not a newer request already received
      // by input(). Clear ownership before the callback can echo a release.
      CallbackScope scope(*this);
      q->onFocused(revoked.session, revoked.epoch, 0, revokedRequest);
    }
  }
  bool setFocus(Entry &e, std::uint64_t request, NativeHostLifecycle::Admission admitted)
  {
    if (!lifecycle->current(admitted))
      return false;
    const QPointer<NativeHost> alive(q);
    const Owner owner{e.session.id, e.session.epoch};
    const auto pid = e.identity.pid;
    CF<AXUIElementRef> window(static_cast<AXUIElementRef>(CFRetain(e.window)));
    releases();
    // A release/invalidation callback may have processed a newer focus request.
    // Do not overwrite that lease or acknowledge the now-obsolete request.
    const auto current = [&]() -> Entry * {
      if (!alive || !lifecycle->current(admitted))
        return nullptr;
      auto *item = entry(owner.session, owner.epoch);
      return item && item->session.phase == Phase::Remote && item->focusRequest == request && !focusOwner.session &&
                     CFEqual(item->window, window.get())
                 ? item
                 : nullptr;
    };
    if (!current())
      return false;
    CF<AXUIElementRef> app(AXUIElementCreateApplication(pid));
    if (!app)
      return false;
    AXUIElementSetMessagingTimeout(app.get(), 0.25f);
    AXUIElementPerformAction(window.get(), kAXRaiseAction);
    if (!current())
      return false;
    AXUIElementSetAttributeValue(window.get(), kAXMainAttribute, kCFBooleanTrue);
    if (!current())
      return false;
    AXUIElementSetAttributeValue(app.get(), kAXFocusedWindowAttribute, window.get());
    if (!current())
      return false;
    [[NSRunningApplication runningApplicationWithProcessIdentifier:pid] activateWithOptions:0];
    if (!current())
      return false;
    if (handleAssociatedChild(owner) || !alive)
      return false;
    CF<CFTypeRef> focused(attribute(app.get(), kAXFocusedWindowAttribute));
    if (!current())
      return false;
    if (!focused || !CFEqual(focused.get(), window.get())) {
      error("The app did not accept focus for this window");
      return false;
    }
    focusOwner = owner;
    const auto revision = focus.request(focusOwner);
    focus.acknowledge(focusOwner, revision);
    focusOwnerRequest = request;
    focusOwnerRevision = revision;
    clipboardMonitor->resetBaseline();
    for (auto &[id, other] : entries)
      updateCapture(*other);
    if (lifecycle->current(admitted) && q->onFocused) {
      CallbackScope scope(*this);
      q->onFocused(owner.session, owner.epoch, revision, request);
    }
    return true;
  }
  void input(std::uint64_t session, std::uint64_t epoch, const QJsonObject &event)
  {
    const auto admitted = lifecycle->admission();
    const QPointer<NativeHost> alive(q);
    auto *e = entry(session, epoch);
    if (!e)
      return;
    const Owner owner{session, epoch};
    const auto kind = event["kind"].toString();
    if (kind != "release" && kind != "visibility") {
      if (handleAssociatedChild(owner) || !alive)
        return;
      e = entry(session, epoch);
      if (!e)
        return;
    }
    // Owner releases remain valid after geometry/focus changes and never release
    // another session's keys. This is also the focus-cancellation path.
    if (kind == "release") {
      releases(owner);
      return;
    }
    if (kind == "visibility") {
      if (!event["visible"].isBool())
        return;
      e->visible = event["visible"].toBool();
      if (!e->visible)
        releases(owner, KeyboardRouteReason::Hidden);
      if (alive)
        if (auto *current = entry(session, epoch))
          updateCapture(*current);
      return;
    }
    const auto code = integer(event["code"], 0x01ffffff);
    const bool down = event["down"].toBool();
    // Up events follow the ledger owner, even if the focus lease was superseded.
    if ((kind == "key" || kind == "button") && event["down"].isBool() && !down && code) {
      if (!ledger.up(kind == "key", std::uint32_t(*code), owner))
        return;
      if (kind == "key") {
        auto saved = keys.find({session, std::uint32_t(*code)});
        if (saved != keys.end()) {
          auto release = saved->second;
          release.flags = flags(integer(event["modifiers"], UINT32_MAX).value_or(0));
          key(*e, release, false);
          keys.erase(saved);
        }
      } else if (*code <= 15) {
        const auto x = event["x"].toDouble(NAN), y = event["y"].toDouble(NAN);
        if (std::isfinite(x) && std::isfinite(y) && x >= 0 && y >= 0 && x < e->session.source.width &&
            y < e->session.source.height)
          e->pointer = QPointF(x, y);
        button(
            *e, std::uint32_t(*code), false, flags(integer(event["modifiers"], UINT32_MAX).value_or(0)),
            int(std::clamp(integer(event["clickCount"], 3).value_or(1), std::uint64_t(1), std::uint64_t(3)))
        );
      }
      return;
    }
    if (!admitted)
      return;
    auto geometry = integer(event["geometry"]);
    if (!geometry || !e->session.acceptsInput(epoch, *geometry) || !AXIsProcessTrusted())
      return;
    if (kind == "focus") {
      const auto request = integer(event["request"]);
      if (request && *request > e->focusRequest) {
        e->focusRequest = *request;
        setFocus(*e, *request, admitted);
      }
      return;
    }
    auto revision = integer(event["focusRevision"]);
    const bool permitted = revision && focus.permits(owner, *revision);
    if (!permitted)
      return;
    const auto modifiers = flags(integer(event["modifiers"], UINT32_MAX).value_or(0));
    if (kind == "key") {
      if (keyboardAdmission.blocksKeys() || !code || !event["down"].isBool() || !down)
        return;
      auto text = event["text"].toString().left(32);
      auto native = keyCode(std::uint32_t(*code));
      if (!native && text.isEmpty())
        return;
      // A printable Unicode fallback is safe for text insertion. Guessing a
      // physical key for a shortcut could execute an unrelated app command.
      if (!native && (modifiers & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl)))
        return;
      const bool repeat = event["repeat"].toBool();
      auto saved = keys.find({session, std::uint32_t(*code)});
      if (repeat) {
        if (ledger.ownerOf(true, std::uint32_t(*code)) == owner && saved != keys.end()) {
          // The physical key owner survives a modifier change. Its repeated
          // characters and flags must follow the current validated event.
          saved->second.flags = modifiers;
          saved->second.text = text;
          key(*e, saved->second, true, true, admitted);
        }
        return;
      }
      if (!ledger.down(true, std::uint32_t(*code), owner))
        return;
      KeyPress press{native.value_or(CGKeyCode(kVK_ANSI_A)), text, modifiers};
      keys[{session, std::uint32_t(*code)}] = press;
      if (!key(*e, press, true, false, admitted)) {
        ledger.up(true, std::uint32_t(*code), owner);
        keys.erase({session, std::uint32_t(*code)});
      }
      return;
    }
    if (keyboardAdmission.blocksPointers())
      return;
    const auto x = event["x"].toDouble(NAN), y = event["y"].toDouble(NAN);
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= e->session.source.width ||
        y >= e->session.source.height)
      return;
    e->pointer = QPointF(x, y);
    if (kind == "button") {
      if (code && *code <= 15 && event["down"].isBool() && down && ledger.down(false, std::uint32_t(*code), owner)) {
        if (!button(
                *e, std::uint32_t(*code), true, modifiers,
                int(std::clamp(integer(event["clickCount"], 3).value_or(1), std::uint64_t(1), std::uint64_t(3))),
                admitted
            ))
          ledger.up(false, std::uint32_t(*code), owner);
      }
    } else if (kind == "move") {
      CGEventType type = kCGEventMouseMoved;
      CGMouseButton active = kCGMouseButtonLeft;
      for (unsigned button = 0; button <= 15; ++button)
        if (ledger.ownerOf(false, button) == owner) {
          active = CGMouseButton(button);
          type = button == 0   ? kCGEventLeftMouseDragged
                 : button == 1 ? kCGEventRightMouseDragged
                               : kCGEventOtherMouseDragged;
          break;
        }
      CF<CGEventRef> motion(pointerEvent(*e, type, active, modifiers));
      post(*e, motion.get(), modifiers, admitted);
    } else if (kind == "scroll") {
      const double dx = event["dx"].toDouble(NAN), dy = event["dy"].toDouble(NAN);
      if (!std::isfinite(dx) || !std::isfinite(dy) || std::abs(dx) > 10000 || std::abs(dy) > 10000)
        return;
      CF<CGEventRef> source(
          CGEventCreateScrollWheelEvent(eventSource, kCGScrollEventUnitPixel, 2, qRound(dy), qRound(dx))
      );
      CF<CGEventRef> scroll(pointerEvent(*e, kCGEventScrollWheel, 0, modifiers));
      if (scroll && source) {
        for (const auto field :
             {kCGScrollWheelEventDeltaAxis1, kCGScrollWheelEventDeltaAxis2, kCGScrollWheelEventPointDeltaAxis1,
              kCGScrollWheelEventPointDeltaAxis2, kCGScrollWheelEventIsContinuous, kCGScrollWheelEventScrollPhase,
              kCGScrollWheelEventMomentumPhase})
          CGEventSetIntegerValueField(scroll.get(), field, CGEventGetIntegerValueField(source.get(), field));
        for (const auto field : {kCGScrollWheelEventFixedPtDeltaAxis1, kCGScrollWheelEventFixedPtDeltaAxis2})
          CGEventSetDoubleValueField(scroll.get(), field, CGEventGetDoubleValueField(source.get(), field));
        post(*e, scroll.get(), modifiers, admitted);
      }
    }
  }
  void resizeWindow(std::uint64_t session, std::uint64_t epoch, int width, int height, std::uint64_t request)
  {
    const auto admitted = lifecycle->admission();
    const QPointer<NativeHost> alive(q);
    auto *e = entry(session, epoch);
    if (!admitted || !e || e->session.phase != Phase::Remote || width < 64 || height < 64 || width > 8192 ||
        height > 8192 || request > maxJsonInteger)
      return;
    if (e->moved) {
      const auto current = [&]() -> Entry * {
        if (!alive || !lifecycle->current(admitted))
          return nullptr;
        auto *item = entry(session, epoch);
        return item && item->moved && item->session.phase == Phase::Remote &&
                       launchTime(item->identity.pid) == item->identity.launch
                   ? item
                   : nullptr;
      };
      HoldingResizeAccess access;
      access.display = [&]() -> std::optional<HoldingResizeDisplay> {
        const auto outputs = physicalOutputSnapshot();
        if (!current() || !holding.available() || !physicalAtExport || !outputs ||
            !samePhysicalOutputLayout(*physicalAtExport, *outputs))
          return std::nullopt;
        return HoldingResizeDisplay{holding.displayId(), holding.bounds()};
      };
      access.window = [&]() -> std::optional<Rect> {
        auto *item = current();
        return item ? bounds(item->window) : std::nullopt;
      };
      access.persist = [&] {
        releases(Owner{session, epoch}, KeyboardRouteReason::GeometryChanged);
        auto *item = current();
        return item && item->session.journaled && writeJournal();
      };
      access.move = [&](Point point) {
        auto *item = current();
        CGPoint position{point.x, point.y};
        CF<AXValueRef> value(AXValueCreate(kAXValueTypeCGPoint, &position));
        return item && value &&
               AXUIElementSetAttributeValue(item->window, kAXPositionAttribute, value.get()) == kAXErrorSuccess;
      };
      access.resize = [&](double requestedWidth, double requestedHeight) {
        auto *item = current();
        CGSize size{requestedWidth, requestedHeight};
        CF<AXValueRef> value(AXValueCreate(kAXValueTypeCGSize, &size));
        return item && value &&
               AXUIElementSetAttributeValue(item->window, kAXSizeAttribute, value.get()) == kAXErrorSuccess;
      };
      const auto result = resizeHoldingWindow(width, height, access);
      if (!alive)
        return;
      if (result.status == HoldingResizeResult::Status::Restore) {
        error("Holding-display resize did not verify; restoring the source window");
        if (alive)
          restore(session);
        return;
      }
      e = current();
      if (!e)
        return;
      e->resizeRequest = request;
      if (result.status == HoldingResizeResult::Status::Applied) {
        e->session.source = result.actual;
        ++e->session.geometry;
        updateCapture(*e);
      } else {
        error("Requested source size cannot be safely applied inside the holding display");
        e = current();
        if (!e)
          return;
      }
      opened(*e);
      return;
    }
    releases(Owner{session, epoch}, KeyboardRouteReason::GeometryChanged);
    if (!alive || !lifecycle->current(admitted))
      return;
    e = entry(session, epoch);
    if (!e || !e->session.journaled || !writeJournal() || !alive || !lifecycle->current(admitted))
      return;
    e->resizeRequest = request;
    CGSize size{CGFloat(width), CGFloat(height)};
    CF<AXValueRef> value(AXValueCreate(kAXValueTypeCGSize, &size));
    if (AXUIElementSetAttributeValue(e->window, kAXSizeAttribute, value.get()) != kAXErrorSuccess) {
      error("This app window does not support resizing through Accessibility");
      if (alive)
        if (auto *current = entry(session, epoch))
          opened(*current);
      return;
    }
    if (auto actual = bounds(e->window))
      e->session.source = *actual;
    if (!lifecycle->current(admitted))
      return;
    ++e->session.geometry;
    updateCapture(*e);
    opened(*e);
  }
  void requestKeyframe(std::uint64_t session, std::uint64_t epoch)
  {
    if (!lifecycle->admission())
      return;
    auto *e = entry(session, epoch);
    if (!e || !e->capture)
      return;
    auto capture = e->capture;
    capture->restartGop();
    dispatch_async(capture->queue, ^{
      if (!capture->stopped && capture->lastImage)
        encodeImage(capture, capture->lastImage, true);
    });
  }
  void restore(std::uint64_t session, bool raise = true)
  {
    const QPointer<NativeHost> alive(q);
    auto found = entries.find(session);
    if (found == entries.end())
      return;
    const auto &e = *found->second;
    if (e.windowDrag)
      e.windowDrag->cancel();
    const Owner owner{session, e.session.epoch};
    const auto transaction = e.sourceTransaction;
    const auto expected = e.identity;
    const auto original = e.session.original;
    CF<AXUIElementRef> window(e.window ? static_cast<AXUIElementRef>(CFRetain(e.window)) : nullptr);
    const auto capture = e.capture;
    SCStream *stream = e.stream;
    DFWindowStream *delegate = e.delegate;
    raise = raise && !e.associatedReturn.blocked();
    const auto current = [&]() -> Entry * {
      if (!alive)
        return nullptr;
      auto *item = entry(owner.session, owner.epoch);
      return item && item->sourceTransaction == transaction && item->capture == capture ? item : nullptr;
    };
    NativeSourceReturnAccess access;
    access.current = [&] { return current() != nullptr; };
    access.fence = [&] {
      if (auto *item = current(); item && item->session.phase != Phase::Returning)
        item->session.beginReturn();
    };
    access.releaseInput = [&] { releases(owner, KeyboardRouteReason::Returned); };
    access.stopCapture = [capture, stream, delegate] {
      capture->shutdown();
      [stream stopCaptureWithCompletionHandler:^(NSError *) {
        // Keep the stream and its output alive until asynchronous teardown ends.
        (void)stream;
        (void)delegate;
      }];
    };
    access.restoreSource = [&] {
      bool missing = false;
      const auto observed = identity(expected.id, &missing);
      if (!current())
        return false;
      if (missing || (observed && (observed->pid != expected.pid || observed->launch != expected.launch)))
        return true; // The old recipient is gone, never mutate its replacement.
      // A failed WindowServer query is not proof that the window disappeared.
      // Restoration can use retained AX identity only at the same process birth.
      const auto destination = recoverFrame(original);
      if (!current() || !destination || launchTime(expected.pid) != expected.launch || !current() ||
          !setBounds(window.get(), *destination, [&] { return current() != nullptr; }))
        return false;
      if (current() && raise)
        AXUIElementPerformAction(window.get(), kAXRaiseAction);
      return current() != nullptr;
    };
    access.retainFailure = [&] {
      if (const auto *item = current()) {
        recovery.append(journalRecord(*item));
        error("Could not restore " + expected.title + "; journal retained for --recover");
      }
    };
    access.finish = [&] {
      if (!current())
        return;
      entries.erase(owner.session);
      writeJournal();
      if (!alive)
        return;
      if (entries.empty() && recovery.isEmpty())
        holding.shutdown();
      if (!destroying && q->onClosed) {
        CallbackScope scope(*this);
        q->onClosed(owner.session);
      }
    };
    (void)returnNativeSource(*transaction, access);
  }
  void restoreAll(KeyboardRouteReason reason = KeyboardRouteReason::Returned)
  {
    const QPointer<NativeHost> alive(q);
    ++exportGeneration; // cancel pending AX-startup retries on disconnect/restore
    if (titlebarObservationRevision != std::numeric_limits<std::uint64_t>::max())
      ++titlebarObservationRevision;
    titlebar.reset();
    titlebarPress.reset();
    titlebarOutputs.reset();
    releases(std::nullopt, reason);
    if (!alive)
      return;
    std::vector<std::uint64_t> ids;
    for (const auto &[id, unused] : entries)
      ids.push_back(id);
    for (auto id : ids) {
      restore(id);
      if (!alive)
        return;
    }
  }
  void poll()
  {
    if (!recovered || !lifecycle->admission())
      return;
    if (titlebar && WindowDragTransaction::Clock::now() >= titlebarDeadline) {
      titlebar.reset();
      titlebarPress.reset();
      titlebarOutputs.reset();
    }
    if (entries.empty())
      physicalAtExport.reset();
    else {
      const auto outputs = physicalOutputSnapshot();
      if (!physicalAtExport || !outputs || !samePhysicalOutputLayout(*physicalAtExport, *outputs)) {
        // The existing 500ms owner-thread poll is deliberately a conservative
        // recovery trigger, not a proof that old compositor input was drained.
        error("Physical display disconnected, rotated or changed resolution; restoring exact source owners");
        restoreAll(KeyboardRouteReason::AuthorityLost);
        physicalAtExport.reset();
        return;
      }
    }
    if (AXIsProcessTrusted() && !tap)
      installTap();
    const QPointer<NativeHost> childGuard(q);
    std::vector<Owner> parents;
    for (const auto &[id, e] : entries)
      parents.push_back({id, e->session.epoch});
    for (const auto owner : parents) {
      handleAssociatedChild(owner);
      if (!childGuard)
        return;
    }
    if (focusOwner.session) {
      auto *focused = entry(focusOwner.session, focusOwner.epoch);
      const auto active = NSWorkspace.sharedWorkspace.frontmostApplication;
      if (!focused || !active || active.processIdentifier != focused->identity.pid)
        releases();
      else {
        CF<AXUIElementRef> app(AXUIElementCreateApplication(focused->identity.pid));
        AXUIElementSetMessagingTimeout(app.get(), 0.25f);
        CF<CFTypeRef> actualFocus(attribute(app.get(), kAXFocusedWindowAttribute));
        if (!actualFocus || !CFEqual(actualFocus.get(), focused->window))
          releases();
      }
    }
    const auto now = QDateTime::currentMSecsSinceEpoch();
    std::vector<std::uint64_t> restoreIds;
    for (auto &[id, e] : entries) {
      if (e->associatedReturn.blocked())
        continue; // One separately queued, exact-parent return owns this entry.
      if (e->windowDrag && e->session.phase == Phase::Preparing && !e->windowDrag->current(e->windowDrag->binding())) {
        restoreIds.push_back(id);
        continue;
      }
      if (e->moved && !holding.available()) {
        releases(Owner{id, e->session.epoch}, KeyboardRouteReason::AuthorityLost);
        error("Holding display configuration changed; restoring source windows");
        restoreIds.push_back(id);
        continue;
      }
      if (!AXIsProcessTrusted() || !CGPreflightScreenCaptureAccess() ||
          (e->session.phase == Phase::Preparing && !e->windowDrag && now - e->started > prepareTimeoutMs)) {
        releases(
            Owner{id, e->session.epoch},
            e->session.phase == Phase::Preparing ? KeyboardRouteReason::Timeout : KeyboardRouteReason::AuthorityLost
        );
        error("Window export timed out or macOS permission was revoked; restoring source");
        restoreIds.push_back(id);
        continue;
      }
      const auto last = e->capture->lastSample.load();
      if (last && now - last > frameTimeoutMs) {
        releases(Owner{id, e->session.epoch}, KeyboardRouteReason::Timeout);
        error("Window capture stopped delivering frames; restoring source");
        restoreIds.push_back(id);
        continue;
      }
      auto current = identity(e->identity.id);
      if (!current || current->pid != e->identity.pid || current->launch != e->identity.launch) {
        releases(Owner{id, e->session.epoch}, KeyboardRouteReason::AuthorityLost);
        restoreIds.push_back(id);
        continue;
      }
      CF<CFTypeRef> minimized(attribute(e->window, kAXMinimizedAttribute));
      if (minimized && CFEqual(minimized.get(), kCFBooleanTrue)) {
        releases(Owner{id, e->session.epoch}, KeyboardRouteReason::Hidden);
        restoreIds.push_back(id);
        continue;
      }
      auto actual = bounds(e->window);
      if (!actual) {
        releases(Owner{id, e->session.epoch}, KeyboardRouteReason::AuthorityLost);
        restoreIds.push_back(id);
        continue;
      }
      if (e->moved && !CGRectContainsRect(rect(holding.bounds()), rect(*actual))) {
        releases(Owner{id, e->session.epoch}, KeyboardRouteReason::GeometryChanged);
        restoreIds.push_back(id);
        continue;
      }
      if (!sameGeometry(*actual, e->session.source)) {
        releases(Owner{id, e->session.epoch}, KeyboardRouteReason::GeometryChanged);
        e->session.source = *actual;
        ++e->session.geometry;
        if (e->windowDrag && e->session.phase == Phase::Preparing)
          e->windowDrag->geometryChanged(e->session.geometry);
        updateCapture(*e);
        opened(*e);
      }
      if (current->title != e->identity.title && !current->title.isEmpty()) {
        e->identity.title = current->title;
        opened(*e);
      }
    }
    for (auto id : restoreIds)
      restore(id);
  }
};

NativeHost::NativeHost(QString journalPath, QObject *parent)
    : QObject(parent),
      m_impl(std::make_unique<Impl>(this, std::move(journalPath)))
{
}
NativeHost::~NativeHost() = default;
QJsonObject NativeHost::permissions()
{
  return {
      {"supported", true},
      {"screenCapture", bool(CGPreflightScreenCaptureAccess())},
      {"accessibility", bool(AXIsProcessTrusted())},
      {"targetedPointer", targetedPointerAvailable()},
      {"targetedPointerApi", "private CoreGraphics window-location SPI"},
      {"exactWindowIdentity", axWindowIdentityAvailable()},
      {"exactWindowIdentityApi", "private HIServices _AXUIElementGetWindow SPI"},
      {"holdingDisplay", HoldingDisplay::supported()},
      {"emergencyRestore", "Control+Option+Shift+Escape"}
  };
}
void NativeHost::requestPermissions()
{
  if (!CGPreflightScreenCaptureAccess())
    CGRequestScreenCaptureAccess();
  NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt : @YES};
  AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}
bool NativeHost::recover()
{
  m_impl->recoverJournal();
  return m_impl->recovered && m_impl->recovery.isEmpty();
}
void NativeHost::setHoldingDisplayEnabled(bool enabled)
{
  m_impl->holdingEnabled = enabled;
}
bool NativeHost::setCaptureFrameRate(unsigned framesPerSecond)
{
  if (QThread::currentThread() != thread() || !validCaptureRate(framesPerSecond) || !m_impl->entries.empty())
    return false;
  m_impl->selectedCaptureRate = framesPerSecond;
  return true;
}
unsigned NativeHost::captureFrameRate() const
{
  return m_impl->selectedCaptureRate;
}
void NativeHost::refresh()
{
  m_impl->refresh();
}
void NativeHost::exportWindow(std::uint32_t id)
{
  m_impl->exportWindow(id);
}
void NativeHost::observeWindowPointer(const LocalWindowPointer &pointer)
{
  if (QThread::currentThread() != thread())
    return;
  const QPointer<NativeHost> alive(this);
  m_impl->titlebar.reset();
  m_impl->titlebarPress.reset();
  m_impl->titlebarOutputs.reset();
  const bool canObserve = m_impl->titlebarObservationRevision != std::numeric_limits<std::uint64_t>::max();
  const auto revision = canObserve ? ++m_impl->titlebarObservationRevision : m_impl->titlebarObservationRevision;
  // There is one physical left button. A new down, lost annotation or
  // mismatched up cancels old provisional work before observing another press.
  std::vector<WindowDragBinding> pending;
  for (const auto &[id, entry] : m_impl->entries)
    if (entry->windowDrag && entry->session.phase == Phase::Preparing)
      pending.push_back(entry->windowDrag->binding());
  for (const auto &binding : pending) {
    const WindowDragRelease release{
        pointer.captureEpoch, pointer.sequence, pointer.sourceTime, pointer.window, pointer.eventNumber
    };
    if (!canObserve || !validLocalWindowPointer(pointer) || pointer.kind != LocalWindowPointer::Kind::Up ||
        !releaseWindowDrag(binding, release))
      cancelWindowDrag(binding);
    if (!alive)
      return;
  }
  if (!canObserve || !validLocalWindowPointer(pointer) || pointer.kind != LocalWindowPointer::Kind::Down ||
      !pointer.window || pointer.clicks != 1 ||
      (pointer.flags & (originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag |
                        originalKey::commandFlag | originalKey::functionFlag)) ||
      !m_impl->lifecycle->admission() || m_impl->destroying)
    return;
  auto observation = NativeTitlebarObservation::begin(pointer.window, {pointer.x, pointer.y});
  const auto outputs = physicalOutputSnapshot();
  if (!alive || !observation || !outputs || m_impl->titlebarObservationRevision != revision ||
      !m_impl->lifecycle->admission() ||
      !CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, kCGMouseButtonLeft))
    return;
  m_impl->titlebar = std::move(observation);
  m_impl->titlebarPress = pointer;
  m_impl->titlebarOutputs = outputs;
  m_impl->titlebarDeadline = WindowDragTransaction::Clock::now() + WindowDragTransaction::dragBudget;
}
std::optional<Owner> NativeHost::prepareWindowDrag(const WindowDragBinding &binding)
{
  if (QThread::currentThread() != thread() || !validWindowDragBinding(binding) ||
      binding.direction != WindowDragDirection::Export || binding.drag > maxJsonInteger ||
      binding.drag <= m_impl->lastWindowDrag || !m_impl->lifecycle->admission() || m_impl->destroying)
    return std::nullopt;
  // Retire the request even when capture/identity admission fails. A delayed
  // retry must not export a later window that happens to reuse the native ID.
  m_impl->lastWindowDrag = binding.drag;
  const QPointer<NativeHost> alive(this);
  const auto press = m_impl->titlebarPress;
  if (!press || !m_impl->titlebar || !m_impl->titlebarOutputs ||
      WindowDragTransaction::Clock::now() >= m_impl->titlebarDeadline || press->captureEpoch != binding.captureEpoch ||
      press->sequence != binding.pressSequence || press->sourceTime != binding.pressTime ||
      press->window != binding.window || press->eventNumber != binding.eventNumber)
    return std::nullopt;
  const auto retainedTitlebar = m_impl->titlebar;
  const auto observed = retainedTitlebar->read();
  const auto outputs = physicalOutputSnapshot();
  if (!alive || !observed || !observed->moved || m_impl->titlebar != retainedTitlebar || !outputs ||
      !m_impl->titlebarOutputs || !samePhysicalOutputLayout(*m_impl->titlebarOutputs, *outputs) ||
      m_impl->titlebarPress != press ||
      !CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, kCGMouseButtonLeft))
    return std::nullopt;
  m_impl->exportWindow(binding.window, 0, binding);
  if (!alive)
    return std::nullopt;
  for (const auto &[id, entry] : m_impl->entries)
    if (entry->windowDrag && entry->windowDrag->binding() == binding && entry->windowDrag->current(binding) &&
        entry->session.phase == Phase::Preparing)
      return Owner{id, entry->session.epoch};
  return std::nullopt;
}
bool NativeHost::releaseWindowDrag(const WindowDragBinding &binding, const WindowDragRelease &release)
{
  if (QThread::currentThread() != thread() || !m_impl->lifecycle->admission())
    return false;
  for (const auto &[id, entry] : m_impl->entries) {
    const auto drag = entry->windowDrag;
    if (!drag || drag->binding() != binding || entry->session.phase != Phase::Preparing)
      continue;
    if (!drag->release(binding, release))
      return false;
    const Owner owner{id, entry->session.epoch};
    // Let the original release finish normal native dispatch before querying
    // the source frame. This timer is not a fabricated native receipt:
    // exact identity/layout/frame are rechecked by ready before any mutation.
    QTimer::singleShot(32, this, [this, owner, binding, drag] {
      auto *item = m_impl->entry(owner.session, owner.epoch);
      if (!item || item->windowDrag != drag || item->session.phase != Phase::Preparing || !drag->current(binding))
        return;
      const auto expected = item->identity;
      CF<AXUIElementRef> window(static_cast<AXUIElementRef>(CFRetain(item->window)));
      const QPointer<NativeHost> alive(this);
      const auto actual = bounds(window.get());
      const auto observed = identity(expected.id);
      if (!alive)
        return;
      item = m_impl->entry(owner.session, owner.epoch);
      if (!item || item->windowDrag != drag || !drag->current(binding))
        return;
      if (!actual || !observed || observed->pid != expected.pid || observed->launch != expected.launch ||
          !closeEnough(*actual, observed->frame)) {
        cancelWindowDrag(binding);
        return;
      }
      // If cancellation later needs native rollback, recover the observed
      // local frame, not a stale position from the moving preview's creation.
      item->session.original = *actual;
      item->dragReleaseFrameObserved = true;
      if (!sameGeometry(*actual, item->session.source)) {
        item->session.source = *actual;
        ++item->session.geometry;
        if (!drag->geometryChanged(item->session.geometry)) {
          cancelWindowDrag(binding);
          return;
        }
        m_impl->updateCapture(*item);
        m_impl->opened(*item);
        return; // Wait for a newly produced and painted current frame.
      }
      if (drag->paintedGeometry())
        m_impl->ready(owner.session, owner.epoch, drag->paintedGeometry());
    });
    return true;
  }
  return false;
}
void NativeHost::cancelWindowDrag(const WindowDragBinding &binding)
{
  if (QThread::currentThread() != thread())
    return;
  for (const auto &[id, entry] : m_impl->entries) {
    if (!entry->windowDrag || entry->windowDrag->binding() != binding)
      continue;
    entry->windowDrag->cancel(); // Revoke before a deferred/reentrant return.
    restore(id);
    return;
  }
}
void NativeHost::ready(std::uint64_t session, std::uint64_t epoch, std::uint64_t paintedGeometry)
{
  m_impl->ready(session, epoch, paintedGeometry);
}
void NativeHost::input(std::uint64_t session, std::uint64_t epoch, const QJsonObject &event)
{
  m_impl->input(session, epoch, event);
}
void NativeHost::resizeWindow(std::uint64_t session, std::uint64_t epoch, int width, int height, std::uint64_t request)
{
  m_impl->resizeWindow(session, epoch, width, height, request);
}
void NativeHost::requestKeyframe(std::uint64_t session, std::uint64_t epoch)
{
  m_impl->requestKeyframe(session, epoch);
}
void NativeHost::restore(std::uint64_t session)
{
  if (m_impl->callbackDepth) {
    QTimer::singleShot(0, this, [this, session] { restore(session); });
    return;
  }
  m_impl->restore(session);
}
void NativeHost::restoreAll()
{
  if (m_impl->callbackDepth) {
    QTimer::singleShot(0, this, [this] { restoreAll(); });
    return;
  }
  m_impl->restoreAll();
}
void NativeHost::releaseAll()
{
  m_impl->releases(std::nullopt, KeyboardRouteReason::AuthorityLost);
}
std::optional<KeyboardRouteBinding> NativeHost::keyboardRouteCandidate(
    std::uint64_t session, std::uint64_t epoch, std::uint64_t generation, ForwardingScope scope
) const
{
  return m_impl->keyboardRouteCandidate(session, epoch, generation, scope);
}
std::optional<ClipboardFocusObservation> NativeHost::clipboardFocus() const
{
  return m_impl->clipboardFocus();
}
void NativeHost::setClipboardTextMonitoring(bool enabled)
{
  if (QThread::currentThread() != thread())
    return;
  m_impl->clipboardTimer.stop();
  m_impl->clipboardMonitor->setEnabled(false);
  if (!enabled || m_impl->destroying)
    return;
  m_impl->clipboardMonitor->setEnabled(true);
  m_impl->clipboardTimer.start(100);
}
bool NativeHost::quiesceKeyboard(const KeyboardRouteBinding &binding)
{
  return m_impl->quiesceKeyboard(binding);
}
bool NativeHost::keyboardRouteQuiesced(const KeyboardRouteBinding &binding) const
{
  return m_impl->keyboardRouteQuiesced(binding);
}
bool NativeHost::resumeKeyboard(const KeyboardRouteBinding &binding)
{
  if (QThread::currentThread() != thread())
    return false;
  m_impl->revokeOriginalInput(binding);
  if (m_impl->originalCleanupFailed)
    return false;
  return m_impl->keyboardAdmission.resume(binding);
}
bool NativeHost::establishOriginalInput(const KeyboardRouteBinding &binding, std::uint64_t captureEpoch)
{
  return m_impl->establishOriginalInput(binding, captureEpoch);
}
bool NativeHost::originalInputCurrent(const KeyboardRouteBinding &binding, std::uint64_t captureEpoch) const
{
  return m_impl->originalInputCurrent(binding, captureEpoch);
}
bool NativeHost::deliverOriginalInput(
    const KeyboardRouteBinding &binding, const OriginalInputRecord &record, const LocalInputMappedPoint &point
)
{
  return m_impl->deliverOriginalInput(binding, record, point);
}
void NativeHost::revokeOriginalInput(const KeyboardRouteBinding &binding)
{
  if (QThread::currentThread() == thread())
    m_impl->revokeOriginalInput(binding);
}
} // namespace deskflow::windowshare
