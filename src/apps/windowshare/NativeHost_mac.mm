/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "HoldingDisplay.h"
#include "NativeHost.h"
#include "NativeKeyboardAdmission.h"

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
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimer>
#include <Qt>

#include <algorithm>
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
      eventNumber < 0 || clicks < 0 || clicks > 3)
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
// AX has no public API that maps an AX window to a CGWindowID. Match the CG
// window's current geometry and title, and fail closed when more than one AX
// window matches. The retained AX object becomes the identity for that session.
AXUIElementRef matchAX(const WindowIdentity &window, QString *diagnostic = nullptr, bool *retryable = nullptr)
{
  CF<AXUIElementRef> app(AXUIElementCreateApplication(window.pid));
  // The first AX request can launch an application's accessibility bridge.
  // This runs on the Qt control thread, never on an event tap/capture callback.
  AXUIElementSetMessagingTimeout(app.get(), 1.0f);
  CFTypeRef rawWindows = nullptr;
  const auto result = AXUIElementCopyAttributeValue(app.get(), kAXWindowsAttribute, &rawWindows);
  CF<CFTypeRef> array(rawWindows);
  if (!array || CFGetTypeID(array.get()) != CFArrayGetTypeID()) {
    if (diagnostic) {
      auto process = [NSRunningApplication runningApplicationWithProcessIdentifier:window.pid];
      *diagnostic = QString("AX window list status %1; registered %2, launched %3, policy %4")
                        .arg(result)
                        .arg(process != nil)
                        .arg(process.finishedLaunching)
                        .arg(process ? int(process.activationPolicy) : -1);
    }
    if (retryable)
      *retryable = result == kAXErrorCannotComplete || result == kAXErrorNoValue;
    return nullptr;
  }
  auto windows = static_cast<CFArrayRef>(array.get());
  std::vector<AXUIElementRef> geometryMatches, exactMatches;
  QStringList observed;
  for (CFIndex index = 0; index < CFArrayGetCount(windows); ++index) {
    auto candidate = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(windows, index));
    if (CFGetTypeID(candidate) != AXUIElementGetTypeID())
      continue;
    AXUIElementSetMessagingTimeout(candidate, 0.3f);
    auto frame = bounds(candidate);
    if (frame)
      observed.push_back(QString("%1,%2 %3x%4").arg(frame->x).arg(frame->y).arg(frame->width).arg(frame->height));
    else
      observed.push_back("unreadable bounds");
    if (!frame || !closeEnough(*frame, window.frame))
      continue;
    geometryMatches.push_back(candidate);
    CF<CFTypeRef> title(attribute(candidate, kAXTitleAttribute));
    if (title && CFGetTypeID(title.get()) == CFStringGetTypeID() &&
        string((__bridge NSString *)title.get()) == window.title)
      exactMatches.push_back(candidate);
  }
  AXUIElementRef match = exactMatches.size() == 1      ? exactMatches.front()
                         : geometryMatches.size() == 1 ? geometryMatches.front()
                                                       : nullptr;
  if (match)
    CFRetain(match);
  else if (diagnostic)
    *diagnostic = QString("CG %1,%2 %3x%4; AX windows %5 [%6]")
                      .arg(window.frame.x)
                      .arg(window.frame.y)
                      .arg(window.frame.width)
                      .arg(window.frame.height)
                      .arg(CFArrayGetCount(windows))
                      .arg(observed.join("; "));
  if (retryable && CFArrayGetCount(windows) == 0)
    *retryable = true;
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
std::optional<Rect> recoverFrame(Rect original)
{
  auto displays = physicalDisplays();
  if (displays.empty())
    return std::nullopt;
  auto best = displays.front();
  double bestArea = -1;
  for (auto display : displays) {
    const auto intersection = CGRectIntersection(rect(original), rect(display));
    const auto area = CGRectIsNull(intersection) ? 0 : intersection.size.width * intersection.size.height;
    if (area > bestArea) {
      best = display;
      bestArea = area;
    }
  }
  return fitInside(original, best);
}
bool setBounds(AXUIElementRef element, Rect desired)
{
  if (!element || !desired.valid())
    return false;
  CGPoint position{desired.x, desired.y};
  CGSize size{desired.width, desired.height};
  CF<AXValueRef> positionValue(AXValueCreate(kAXValueTypeCGPoint, &position));
  CF<AXValueRef> sizeValue(AXValueCreate(kAXValueTypeCGSize, &size));
  // Restore visibility before geometry, including after the app minimized itself.
  AXUIElementSetAttributeValue(element, kAXMinimizedAttribute, kCFBooleanFalse);
  AXUIElementSetAttributeValue(element, kAXSizeAttribute, sizeValue.get());
  AXUIElementSetAttributeValue(element, kAXPositionAttribute, positionValue.get());
  auto actual = bounds(element);
  CF<CFTypeRef> minimized(attribute(element, kAXMinimizedAttribute));
  // Some applications have a minimum size. Reaching a physical display is more
  // important than insisting on an exact size they cannot represent.
  return (!minimized || !CFEqual(minimized.get(), kCFBooleanTrue)) && actual && std::abs(actual->x - desired.x) < 5 &&
         std::abs(actual->y - desired.y) < 5;
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
  std::atomic<int> expectedWidth{0}, expectedHeight{0};
  std::atomic<unsigned> inFlight{0};
  std::atomic<qint64> lastSample{0};
  VTCompressionSessionRef encoder = nullptr; // used on the capture serial queue
  CVPixelBufferRef lastImage = nullptr;
  std::uint64_t lastImageGeometry = 0; // capture serial queue only
  int encodedWidth = 0, encodedHeight = 0;
  std::atomic<bool> hardware{false};
  CMTime nextTimestamp = kCMTimeZero;
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
  bool configure(std::uint64_t revision, int width, int height)
  {
    std::lock_guard lock(mutex);
    if (geometry == revision && expectedWidth == width && expectedHeight == height)
      return false;
    acceptingFrames = false;
    geometry = revision;
    expectedWidth = width;
    expectedHeight = height;
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

bool makeEncoder(const std::shared_ptr<Capture> &capture, int width, int height)
{
  if (capture->encoder && capture->encodedWidth == width && capture->encodedHeight == height)
    return true;
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
  VTSessionSetProperty(encoder, kVTCompressionPropertyKey_ExpectedFrameRate, (__bridge CFNumberRef) @30);
  VTSessionSetProperty(encoder, kVTCompressionPropertyKey_MaxKeyFrameInterval, (__bridge CFNumberRef) @60);
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

void encodeImage(const std::shared_ptr<Capture> &capture, CVPixelBufferRef image, bool replay = false)
{
  if (!image)
    return;
  SubmittedFrame stamp;
  int expectedWidth = 0, expectedHeight = 0;
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
  if (!makeEncoder(capture, width, height))
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
  const auto timestamp = capture->nextTimestamp;
  capture->nextTimestamp = CMTimeAdd(timestamp, CMTimeMake(1, 30));
  capture->inFlight.fetch_add(1);
  auto *submitted = new SubmittedFrame(stamp);
  auto status = VTCompressionSessionEncodeFrame(
      capture->encoder, image, timestamp, CMTimeMake(1, 30), (__bridge CFDictionaryRef)properties, submitted, nullptr
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
  deskflow::windowshare::native::encodeImage(capture, image);
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
    bool visible = true, moved = false;
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
  NativeHost *q;
  QString journalPath;
  std::map<std::uint64_t, std::unique_ptr<Entry>> entries;
  std::map<CGWindowID, SCWindow *> catalog;
  QJsonArray recovery;
  InputLedger ledger;
  FocusLease focus;
  Owner focusOwner;
  std::uint64_t focusOwnerRequest = 0, focusOwnerRevision = 0;
  NativeKeyboardAdmission keyboardAdmission;
  std::map<std::pair<std::uint64_t, std::uint32_t>, KeyPress> keys;
  QTimer timer;
  CGEventSourceRef eventSource = nullptr;
  CFMachPortRef tap = nullptr;
  CFRunLoopSourceRef tapSource = nullptr;
  id sleepObserver = nil, sessionObserver = nil;
  bool refreshing = false, destroying = false, recovered = false;
  bool holdingEnabled = false;
  HoldingDisplay holding;
  unsigned callbackDepth = 0;
  std::uint64_t exportGeneration = 0;
  std::int64_t mouseSequence = 0;
  struct CallbackScope
  {
    Impl &host;
    explicit CallbackScope(Impl &value) : host(value)
    {
      ++host.callbackDepth;
    }
    ~CallbackScope()
    {
      --host.callbackDepth;
    }
  };
  std::shared_ptr<Capture> lifetime = std::make_shared<Capture>();

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
      recoverJournal();
      installSafetyHooks();
    });
    QObject::connect(&timer, &QTimer::timeout, q, [this] { poll(); });
    timer.start(500);
  }
  ~Impl()
  {
    destroying = true;
    restoreAll();
    lifetime->shutdown();
    timer.stop();
    auto center = NSWorkspace.sharedWorkspace.notificationCenter;
    if (sleepObserver)
      [center removeObserver:sleepObserver];
    if (sessionObserver)
      [center removeObserver:sessionObserver];
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
      q->onError(std::move(message));
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
  std::optional<KeyboardRouteBinding>
  keyboardRouteCandidate(std::uint64_t session, std::uint64_t epoch, std::uint64_t generation) const
  {
    const Owner owner{session, epoch};
    const auto *e = entry(session, epoch);
    if (destroying || callbackDepth || !e || !e->visible || e->session.phase != Phase::Remote || focusOwner != owner ||
        !focus.permits(owner, focusOwnerRevision) || !e->capture || e->capture->stopped)
      return std::nullopt;
    const KeyboardRouteBinding binding{owner, generation, focusOwnerRequest, focusOwnerRevision, e->session.geometry};
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
    return binding;
  }
  bool qtInputEmpty() const
  {
    return ledger.size() == 0 && keys.empty() &&
           std::all_of(entries.begin(), entries.end(), [](const auto &item) { return item.second->buttons.empty(); });
  }
  bool quiesceKeyboard(const KeyboardRouteBinding &binding)
  {
    return keyboardAdmission.quiesce(
        binding, keyboardRouteCandidate(binding.owner.session, binding.owner.epoch, binding.generation), qtInputEmpty()
    );
  }
  bool keyboardRouteQuiesced(const KeyboardRouteBinding &binding) const
  {
    return keyboardAdmission.quiesced(
        binding, keyboardRouteCandidate(binding.owner.session, binding.owner.epoch, binding.generation), qtInputEmpty()
    );
  }
  void invalidateKeyboard(KeyboardRouteReason reason, std::optional<Owner> owner = std::nullopt)
  {
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
        {"resizeRequest", double(e.resizeRequest)},
        {"hardwareEncoder", e.capture && e.capture->hardware.load()}
    };
  }
  void opened(const Entry &e)
  {
    if (q->onOpened) {
      CallbackScope scope(*this);
      q->onOpened(e.session.id, e.session.epoch, metadata(e));
    }
  }
  QJsonObject journalRecord(const Entry &e) const
  {
    return {{"windowId", double(e.identity.id)},   {"pid", e.identity.pid},
            {"launch", double(e.identity.launch)}, {"bundle", e.identity.bundle},
            {"title", e.identity.title},           {"original", json(e.session.original)},
            {"session", double(e.session.id)},     {"epoch", double(e.session.epoch)}};
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
    if (!records.isEmpty() && !AXIsProcessTrusted()) {
      error("Accessibility permission is required to restore journaled windows; grant it and rerun --recover");
      return;
    }
    recovery = {};
    for (const auto &value : records) {
      const auto record = value.toObject();
      const auto id = integer(record["windowId"], UINT32_MAX), pid = integer(record["pid"], INT_MAX),
                 launch = integer(record["launch"]);
      const auto original = rect(record["original"].toObject());
      if (!id || !*id || !pid || !*pid || !launch || !*launch || !original.valid()) {
        recovery.append(value);
        error("Recovery journal contains an invalid window entry; export disabled");
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
  void installSafetyHooks()
  {
    auto life = lifetime;
    auto notify = ^(NSNotification *) {
      std::lock_guard lock(life->mutex);
      if (life->receiver)
        QMetaObject::invokeMethod(
            life->receiver, [this] { restoreAll(KeyboardRouteReason::AuthorityLost); }, Qt::QueuedConnection
        );
    };
    auto center = NSWorkspace.sharedWorkspace.notificationCenter;
    sleepObserver = [center addObserverForName:NSWorkspaceWillSleepNotification object:nil queue:nil usingBlock:notify];
    sessionObserver = [center addObserverForName:NSWorkspaceSessionDidResignActiveNotification
                                          object:nil
                                           queue:nil
                                      usingBlock:notify];
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
      QMetaObject::invokeMethod(
          self->q, [self] { self->restoreAll(KeyboardRouteReason::AuthorityLost); }, Qt::QueuedConnection
      );
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
    config.minimumFrameInterval = CMTimeMake(1, active ? 30 : (e.visible ? 10 : 2));
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
    const bool changed = capture->configure(revision, width, height);
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
                          capture->expectedHeight != height)
                        return;
                      // The output serial queue drains older callbacks before accepting the
                      // updated configuration. Dimensions independently reject stale buffers.
                      capture->acceptingFrames = true;
                      capture->forceKeyframe = true;
                    });
                }];
    capture->forceKeyframe = true;
  }
  void exportWindow(std::uint32_t id, unsigned attempt = 0)
  {
    recoverJournal();
    if (!recovered || !recovery.isEmpty()) {
      error("Unresolved recovery journal entries prevent a new export");
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
    if (!e->window) {
      if (retryable && attempt < 3) {
        const auto generation = exportGeneration;
        QTimer::singleShot(250, q, [this, id, attempt, generation] {
          if (generation == exportGeneration)
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
    CF<CFTypeRef> minimized(attribute(e->window, kAXMinimizedAttribute));
    if (minimized && CFEqual(minimized.get(), kCFBooleanTrue)) {
      error("Unminimize this window before exporting it");
      return;
    }
    e->identity = *current;
    e->shareable = found->second;
    auto axFrame = bounds(e->window);
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
    e->started = QDateTime::currentMSecsSinceEpoch();
    e->capture = std::make_shared<Capture>();
    e->capture->receiver = q;
    const auto session = e->session.id, epoch = e->session.epoch;
    e->capture->frame = [this, session, epoch](std::uint64_t geometry, QByteArray bytes) {
      auto *entry = this->entry(session, epoch);
      if (!entry || geometry != entry->session.geometry)
        return;
      if (!entry->session.firstFrame)
        entry->session.frameReady(epoch);
      if (q->onFrame) {
        CallbackScope scope(*this);
        q->onFrame(session, epoch, geometry, std::move(bytes));
      }
    };
    e->capture->error = [this, session, epoch](QString message) {
      releases(Owner{session, epoch}, KeyboardRouteReason::Failed);
      error(std::move(message));
      restore(session);
    };
    e->delegate = [[DFWindowStream alloc] init];
    e->delegate->capture = e->capture;
    e->configuration = configuration(*e, true);
    e->capture->geometry = e->session.geometry;
    e->capture->expectedWidth = e->pixelWidth;
    e->capture->expectedHeight = e->pixelHeight;
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
    entries.emplace(session, std::move(e));
    if (!writeJournal()) {
      auto it = entries.find(session);
      it->second->capture->shutdown();
      entries.erase(it);
      return;
    }
    auto &stored = *entries.at(session);
    auto capture = stored.capture;
    opened(stored);
    auto *stillOpen = entry(session, epoch);
    if (!stillOpen)
      return;
    [stillOpen->stream startCaptureWithCompletionHandler:^(NSError *failure) {
      if (failure)
        capture->fail("Cannot start window capture: " + string(failure.localizedDescription));
    }];
  }
  void ready(std::uint64_t session, std::uint64_t epoch)
  {
    auto *e = entry(session, epoch);
    if (!e || e->session.phase != Phase::Preparing || !e->session.firstFrame)
      return;
    if (holdingEnabled) {
      if (!holding.available()) {
        const bool hasHiddenSources =
            std::any_of(entries.begin(), entries.end(), [](const auto &item) { return item.second->moved; });
        if (hasHiddenSources) {
          error("Holding display changed while sources were exported; restoring all windows");
          restoreAll();
          return;
        }
      }
      QString failure;
      if (holding.available() || holding.create(&failure, 4096, 4096)) {
        const auto destination = fitInside(e->session.source, holding.bounds());
        if (e->session.journaled && writeJournal() && setBounds(e->window, destination)) {
          const auto actual = bounds(e->window);
          if (actual && CGRectContainsRect(rect(holding.bounds()), rect(*actual))) {
            e->session.source = *actual;
            e->moved = true;
            ++e->session.geometry;
            updateCapture(*e);
          } else {
            error("Holding display placement did not verify; restoring source window");
            restore(session);
            return;
          }
        } else {
          error("Cannot safely relocate the source window; restoring it");
          restore(session);
          return;
        }
      } else {
        error("Holding display unavailable; source remains visible: " + failure);
        // Reporting an error may cause the caller to disconnect and restore.
        e = entry(session, epoch);
        if (!e)
          return;
      }
    }
    // The default source-visible fallback never parks a window off-screen.
    e->session.commit(epoch);
    opened(*e);
    if (entry(session, epoch) && q->onCommitted) {
      CallbackScope scope(*this);
      q->onCommitted(session, epoch);
    }
  }
  bool post(Entry &e, CGEventRef event, CGEventFlags modifiers = 0)
  {
    if (!event || !AXIsProcessTrusted() || launchTime(e.identity.pid) != e.identity.launch)
      return false;
    CGEventSetIntegerValueField(event, kCGEventSourceUserData, syntheticEventTag);
    CGEventSetFlags(event, modifiers);
    CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointer, e.identity.id);
    CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, e.identity.id);
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
    if (!event)
      error("macOS targeted pointer construction failed; private window-location API required");
    return event;
  }
  void key(Entry &e, const KeyPress &press, bool down, bool repeat = false)
  {
    CF<CGEventRef> event(CGEventCreateKeyboardEvent(eventSource, press.code, down));
    if (!event)
      return;
    if (modifier(press.code))
      CGEventSetType(event.get(), kCGEventFlagsChanged);
    CGEventSetIntegerValueField(event.get(), kCGKeyboardEventAutorepeat, repeat);
    if (!press.text.isEmpty() && !(press.flags & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl))) {
      CGEventKeyboardSetUnicodeString(
          event.get(), press.text.size(), reinterpret_cast<const UniChar *>(press.text.utf16())
      );
    }
    post(e, event.get(), press.flags);
  }
  void button(Entry &e, std::uint32_t code, bool down, CGEventFlags modifiers = 0, int clicks = 1)
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
      post(e, event.get(), modifiers);
    }
  }
  void
  releases(std::optional<Owner> owner = std::nullopt, KeyboardRouteReason reason = KeyboardRouteReason::FocusChanged)
  {
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
    if (revoked.session && revokedRequest && !destroying && q->onFocused) {
      // Revoke the acknowledged request, not a newer request already received
      // by input(). Clear ownership before the callback can echo a release.
      CallbackScope scope(*this);
      q->onFocused(revoked.session, revoked.epoch, 0, revokedRequest);
    }
  }
  bool setFocus(Entry &e, std::uint64_t request)
  {
    releases();
    // A release/invalidation callback may have processed a newer focus request.
    // Do not overwrite that lease or acknowledge the now-obsolete request.
    if (e.focusRequest != request || focusOwner.session)
      return false;
    CF<AXUIElementRef> app(AXUIElementCreateApplication(e.identity.pid));
    AXUIElementSetMessagingTimeout(app.get(), 0.25f);
    AXUIElementPerformAction(e.window, kAXRaiseAction);
    AXUIElementSetAttributeValue(e.window, kAXMainAttribute, kCFBooleanTrue);
    AXUIElementSetAttributeValue(app.get(), kAXFocusedWindowAttribute, e.window);
    [[NSRunningApplication runningApplicationWithProcessIdentifier:e.identity.pid] activateWithOptions:0];
    CF<CFTypeRef> focused(attribute(app.get(), kAXFocusedWindowAttribute));
    if (!focused || !CFEqual(focused.get(), e.window)) {
      error("The app did not accept focus for this window");
      return false;
    }
    focusOwner = {e.session.id, e.session.epoch};
    const auto revision = focus.request(focusOwner);
    focus.acknowledge(focusOwner, revision);
    focusOwnerRequest = request;
    focusOwnerRevision = revision;
    for (auto &[id, other] : entries)
      updateCapture(*other);
    if (q->onFocused) {
      CallbackScope scope(*this);
      q->onFocused(e.session.id, e.session.epoch, revision, request);
    }
    return true;
  }
  void input(std::uint64_t session, std::uint64_t epoch, const QJsonObject &event)
  {
    auto *e = entry(session, epoch);
    if (!e)
      return;
    const Owner owner{session, epoch};
    const auto kind = event["kind"].toString();
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
      updateCapture(*e);
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
    auto geometry = integer(event["geometry"]);
    if (!geometry || !e->session.acceptsInput(epoch, *geometry) || !AXIsProcessTrusted())
      return;
    if (kind == "focus") {
      const auto request = integer(event["request"]);
      if (request && *request > e->focusRequest) {
        e->focusRequest = *request;
        setFocus(*e, *request);
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
          key(*e, saved->second, true, true);
        }
        return;
      }
      if (!ledger.down(true, std::uint32_t(*code), owner))
        return;
      KeyPress press{native.value_or(CGKeyCode(kVK_ANSI_A)), text, modifiers};
      keys[{session, std::uint32_t(*code)}] = press;
      key(*e, press, true);
      return;
    }
    const auto x = event["x"].toDouble(NAN), y = event["y"].toDouble(NAN);
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= e->session.source.width ||
        y >= e->session.source.height)
      return;
    e->pointer = QPointF(x, y);
    if (kind == "button") {
      if (code && *code <= 15 && event["down"].isBool() && down && ledger.down(false, std::uint32_t(*code), owner))
        button(
            *e, std::uint32_t(*code), true, modifiers,
            int(std::clamp(integer(event["clickCount"], 3).value_or(1), std::uint64_t(1), std::uint64_t(3)))
        );
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
      post(*e, motion.get(), modifiers);
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
        post(*e, scroll.get(), modifiers);
      }
    }
  }
  void resizeWindow(std::uint64_t session, std::uint64_t epoch, int width, int height, std::uint64_t request)
  {
    auto *e = entry(session, epoch);
    if (!e || e->session.phase != Phase::Remote || width < 64 || height < 64 || width > 8192 || height > 8192 ||
        request > maxJsonInteger)
      return;
    releases(Owner{session, epoch}, KeyboardRouteReason::GeometryChanged);
    if (!e->session.journaled || !writeJournal())
      return;
    e->resizeRequest = request;
    CGSize size{CGFloat(width), CGFloat(height)};
    CF<AXValueRef> value(AXValueCreate(kAXValueTypeCGSize, &size));
    if (AXUIElementSetAttributeValue(e->window, kAXSizeAttribute, value.get()) != kAXErrorSuccess) {
      error("This app window does not support resizing through Accessibility");
      opened(*e);
      return;
    }
    if (auto actual = bounds(e->window))
      e->session.source = *actual;
    ++e->session.geometry;
    updateCapture(*e);
    opened(*e);
  }
  void requestKeyframe(std::uint64_t session, std::uint64_t epoch)
  {
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
  void restore(std::uint64_t session)
  {
    auto found = entries.find(session);
    if (found == entries.end())
      return;
    auto &e = *found->second;
    e.session.beginReturn();
    releases(Owner{session, e.session.epoch}, KeyboardRouteReason::Returned);
    e.capture->shutdown();
    SCStream *stream = e.stream;
    DFWindowStream *delegate = e.delegate;
    [stream stopCaptureWithCompletionHandler:^(NSError *) {
      // Keep the stream and its output alive until asynchronous teardown ends.
      (void)stream;
      (void)delegate;
    }];
    bool missing = false;
    auto current = identity(e.identity.id, &missing);
    bool restored = missing || (current && (current->pid != e.identity.pid || current->launch != e.identity.launch));
    if (!restored) {
      // A failed WindowServer query is not proof that the window disappeared.
      // The retained AX identity can still restore the original window, but
      // only while its process-start identity continues to match.
      const auto destination = recoverFrame(e.session.original);
      restored = destination && launchTime(e.identity.pid) == e.identity.launch && setBounds(e.window, *destination);
      if (restored)
        AXUIElementPerformAction(e.window, kAXRaiseAction);
    }
    if (!restored) {
      recovery.append(journalRecord(e));
      error("Could not restore " + e.identity.title + "; journal retained for --recover");
    }
    entries.erase(found);
    writeJournal();
    if (entries.empty() && recovery.isEmpty())
      holding.shutdown();
    if (!destroying && q->onClosed) {
      CallbackScope scope(*this);
      q->onClosed(session);
    }
  }
  void restoreAll(KeyboardRouteReason reason = KeyboardRouteReason::Returned)
  {
    ++exportGeneration; // cancel pending AX-startup retries on disconnect/restore
    releases(std::nullopt, reason);
    std::vector<std::uint64_t> ids;
    for (const auto &[id, unused] : entries)
      ids.push_back(id);
    for (auto id : ids)
      restore(id);
  }
  void poll()
  {
    if (!recovered)
      return;
    if (AXIsProcessTrusted() && !tap)
      installTap();
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
      if (e->moved && !holding.available()) {
        releases(Owner{id, e->session.epoch}, KeyboardRouteReason::AuthorityLost);
        error("Holding display configuration changed; restoring source windows");
        restoreIds.push_back(id);
        continue;
      }
      if (!AXIsProcessTrusted() || !CGPreflightScreenCaptureAccess() ||
          (e->session.phase == Phase::Preparing && now - e->started > prepareTimeoutMs)) {
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
void NativeHost::refresh()
{
  m_impl->refresh();
}
void NativeHost::exportWindow(std::uint32_t id)
{
  m_impl->exportWindow(id);
}
void NativeHost::ready(std::uint64_t session, std::uint64_t epoch)
{
  m_impl->ready(session, epoch);
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
std::optional<KeyboardRouteBinding>
NativeHost::keyboardRouteCandidate(std::uint64_t session, std::uint64_t epoch, std::uint64_t generation) const
{
  return m_impl->keyboardRouteCandidate(session, epoch, generation);
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
  return m_impl->keyboardAdmission.resume(binding);
}
} // namespace deskflow::windowshare
