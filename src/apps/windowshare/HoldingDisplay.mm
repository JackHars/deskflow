/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "HoldingDisplay.h"
#include "HoldingDisplayStartup.h"
#include "LifecycleDiagnostics.h"
#include "PlatformMarkers.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <unistd.h>
#include <vector>

// The private selector schema is cross-checked against Chromium's macOS virtual
// display utility. All private names and declarations remain in this file; the
// implementation resolves classes at runtime and validates their method ABIs.
// https://chromium.googlesource.com/chromium/src/+/HEAD/ui/display/mac/test/virtual_display_util_mac.mm
// No direct private-class symbols are referenced or weak-linked.
@protocol DSHoldingDescriptor <NSObject>
- (void)setVendorID:(unsigned int)value;
- (void)setProductID:(unsigned int)value;
- (void)setSerialNum:(unsigned int)value;
- (void)setSerialNumber:(unsigned int)value;
- (void)setName:(NSString *)value;
- (void)setSizeInMillimeters:(CGSize)value;
- (void)setMaxPixelsWide:(unsigned int)value;
- (void)setMaxPixelsHigh:(unsigned int)value;
- (void)setRedPrimary:(CGPoint)value;
- (void)setGreenPrimary:(CGPoint)value;
- (void)setBluePrimary:(CGPoint)value;
- (void)setWhitePoint:(CGPoint)value;
- (void)setQueue:(id)value;
@end

@protocol DSHoldingSettings <NSObject>
- (void)setHiDPI:(unsigned int)value;
- (void)setRotation:(unsigned int)value;
- (void)setModes:(NSArray *)value;
@end

@protocol DSHoldingMode <NSObject>
- (instancetype)initWithWidth:(unsigned int)width height:(unsigned int)height refreshRate:(double)rate;
@end

@protocol DSHoldingVirtualDisplay <NSObject>
- (instancetype)initWithDescriptor:(id)descriptor;
- (BOOL)applySettings:(id)settings;
- (unsigned int)displayID;
@end

namespace deskflow::windowshare {

HoldingResizeResult resizeHoldingWindow(int width, int height, const HoldingResizeAccess &access)
{
  using Status = HoldingResizeResult::Status;
  const auto contains = [](Rect outer, Rect inner) {
    return outer.valid() && inner.valid() && inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width && inner.y + inner.height <= outer.y + outer.height;
  };
  if (width < 64 || height < 64 || width > 8192 || height > 8192 || !access.display || !access.window ||
      !access.persist || !access.move || !access.resize)
    return {};
  const auto display = access.display();
  const auto before = access.window();
  if (!display || !display->id || !before || !contains(display->bounds, *before))
    return {Status::Restore, {}};
  if (width > display->bounds.width || height > display->bounds.height)
    return {};
  const auto current = [&]() -> std::optional<Rect> {
    const auto observed = access.display();
    const auto frame = access.window();
    if (!observed || observed->id != display->id || observed->bounds.x != display->bounds.x ||
        observed->bounds.y != display->bounds.y || observed->bounds.width != display->bounds.width ||
        observed->bounds.height != display->bounds.height || !frame || !contains(display->bounds, *frame))
      return std::nullopt;
    return frame;
  };
  if (!access.persist())
    return {};
  auto actual = current();
  if (!actual)
    return {Status::Restore, {}};
  const auto destination = fitInside({actual->x, actual->y, double(width), double(height)}, display->bounds);
  if (destination.x != actual->x || destination.y != actual->y) {
    // Move inward while the old, smaller frame still fits. Resizing first can
    // expose the source on a physical output until the periodic recovery poll.
    if (!access.move({destination.x, destination.y}))
      return {Status::Restore, {}};
    actual = current();
    if (!actual)
      return {Status::Restore, {}};
  }
  // AX applications may round/refuse a position. Never grow at an unverified
  // anchor, even when the old frame itself remains inside the holding display.
  if (!contains(display->bounds, {actual->x, actual->y, double(width), double(height)}))
    return {Status::Restore, {}};
  if (!access.resize(width, height))
    return {Status::Restore, {}};
  actual = current();
  // A size can be quantized by the source application; publish its actual
  // contained rectangle, or immediately return the source on any uncertainty.
  return actual ? HoldingResizeResult{Status::Applied, *actual} : HoldingResizeResult{Status::Restore, {}};
}

namespace {

bool fail(QString *error, const QString &message)
{
  lifecycleDiagnostic("Holding display", "create", "failed", message);
  if (error) {
    *error = message;
  }
  return false;
}

std::string normalizedType(const char *encoding)
{
  if (!encoding) {
    return {};
  }
  // Runtime encodings can contain qualifiers and quoted class/field names that
  // @encode omits. Preserve the ABI's scalar, pointer and aggregate structure.
  while (*encoding && std::strchr("rnNoORV", *encoding)) {
    ++encoding;
  }
  std::string result;
  bool quoted = false;
  for (; *encoding; ++encoding) {
    if (*encoding == '"') {
      quoted = !quoted;
    } else if (!quoted) {
      result += *encoding;
    }
  }
  return result;
}

bool methodMatches(Class type, SEL selector, const char *result, std::initializer_list<const char *> arguments)
{
  Method method = class_getInstanceMethod(type, selector);
  if (!method || method_getNumberOfArguments(method) != arguments.size() + 2) {
    return false;
  }
  char *actual = method_copyReturnType(method);
  const bool resultMatches = normalizedType(actual) == normalizedType(result);
  std::free(actual);
  if (!resultMatches) {
    return false;
  }
  unsigned index = 2;
  for (const auto *expected : arguments) {
    actual = method_copyArgumentType(method, index++);
    const bool argumentMatches = normalizedType(actual) == normalizedType(expected);
    std::free(actual);
    if (!argumentMatches) {
      return false;
    }
  }
  return true;
}

struct Runtime
{
  Class descriptor = Nil;
  Class settings = Nil;
  Class mode = Nil;
  Class display = Nil;

  bool load(QString *reason)
  {
    descriptor = NSClassFromString(@"CGVirtualDisplayDescriptor");
    settings = NSClassFromString(@"CGVirtualDisplaySettings");
    mode = NSClassFromString(@"CGVirtualDisplayMode");
    display = NSClassFromString(@"CGVirtualDisplay");
    if (!descriptor || !settings || !mode || !display) {
      return fail(
          reason, QStringLiteral("Private macOS virtual-display classes are unavailable; source windows stay visible.")
      );
    }
    for (Class type : {descriptor, settings, mode, display}) {
      if (![type isSubclassOfClass:NSObject.class]) {
        return fail(
            reason, QStringLiteral("Private virtual-display class hierarchy changed; source windows stay visible.")
        );
      }
    }
    const auto check = [&](Class type, SEL selector, const char *result, std::initializer_list<const char *> args) {
      if (methodMatches(type, selector, result, args)) {
        return true;
      }
      return fail(
          reason,
          QStringLiteral("Private virtual-display method %1 has an unsupported signature; source windows stay visible.")
              .arg(QString::fromUtf8(sel_getName(selector)))
      );
    };
    for (SEL selector : {
           @selector(setVendorID:), @selector(setProductID:), @selector(setSerialNum:), @selector(setSerialNumber:),
               @selector(setMaxPixelsWide:), @selector(setMaxPixelsHigh:)
         }) {
      if (!check(descriptor, selector, @encode(void), { @encode(unsigned int) })) {
        return false;
      }
    }
    for (SEL selector : { @selector(setName:), @selector(setQueue:) }) {
      if (!check(descriptor, selector, @encode(void), { @encode(id) })) {
        return false;
      }
    }
    for (SEL selector : {
           @selector(setRedPrimary:), @selector(setGreenPrimary:), @selector(setBluePrimary:), @selector(setWhitePoint:)
         }) {
      if (!check(descriptor, selector, @encode(void), { @encode(CGPoint) })) {
        return false;
      }
    }
    return check(descriptor, @selector(setSizeInMillimeters:), @encode(void), { @encode(CGSize) }) &&
           check(settings, @selector(setHiDPI:), @encode(void), { @encode(unsigned int) }) &&
           check(settings, @selector(setRotation:), @encode(void), { @encode(unsigned int) }) &&
           check(settings, @selector(setModes:), @encode(void), { @encode(id) }) &&
           check(
               mode, @selector(initWithWidth:height:refreshRate:), @encode(id),
               { @encode(unsigned int), @encode(unsigned int), @encode(double) }
           ) &&
           check(display, @selector(initWithDescriptor:), @encode(id), { @encode(id) }) &&
           check(display, @selector(applySettings:), @encode(BOOL), { @encode(id) }) &&
           check(display, @selector(displayID), @encode(unsigned int), {});
  }
};

bool holdingIdentity(CGDirectDisplayID display)
{
  return isWindowshareHoldingDisplay(
      CGDisplayVendorNumber(display), CGDisplayModelNumber(display), CGDisplaySerialNumber(display)
  );
}

struct DisplaySnapshot
{
  CGDirectDisplayID id = kCGNullDirectDisplay;
  CGRect bounds = CGRectZero;
  CGDirectDisplayID mirror = kCGNullDirectDisplay;
};

bool snapshot(std::vector<DisplaySnapshot> &displays, QString *error)
{
  CGDirectDisplayID ids[64];
  uint32_t count = 0;
  if (CGGetActiveDisplayList(64, ids, &count) != kCGErrorSuccess || count == 0 || count == 64) {
    return fail(error, QStringLiteral("Cannot verify the active display layout; source windows stay visible."));
  }
  for (uint32_t index = 0; index < count; ++index) {
    if (holdingIdentity(ids[index])) {
      return fail(
          error, QStringLiteral("A Deskflow holding display already exists; refusing to take ownership of it.")
      );
    }
    const CGRect bounds = CGDisplayBounds(ids[index]);
    if (CGRectIsEmpty(bounds) || CGRectIsInfinite(bounds) || CGRectIsNull(bounds)) {
      return fail(error, QStringLiteral("An active display has invalid bounds; source windows stay visible."));
    }
    displays.push_back({ids[index], bounds, CGDisplayMirrorsDisplay(ids[index])});
  }
  return true;
}

Rect requestedHoldingBounds(const std::vector<DisplaySnapshot> &displays, unsigned width, unsigned height)
{
  if (displays.empty())
    return {};
  const DisplaySnapshot *rightmost = &displays.front();
  for (const auto &display : displays)
    if (CGRectGetMaxX(display.bounds) > CGRectGetMaxX(rightmost->bounds))
      rightmost = &display;
  return {
      std::ceil(CGRectGetMaxX(rightmost->bounds)), std::floor(CGRectGetMinY(rightmost->bounds)), double(width),
      double(height)
  };
}

#ifdef DESKFLOW_HOLDING_DISPLAY_HELPER
bool unchanged(const std::vector<DisplaySnapshot> &displays, CGDirectDisplayID originalMain)
{
  if (CGMainDisplayID() != originalMain) {
    return false;
  }
  for (const auto &display : displays) {
    if (!CGDisplayIsActive(display.id) || !CGRectEqualToRect(display.bounds, CGDisplayBounds(display.id)) ||
        display.mirror != CGDisplayMirrorsDisplay(display.id)) {
      return false;
    }
  }
  return true;
}
#endif

Rect asRect(CGRect bounds)
{
  return {bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height};
}

bool isolated(CGDirectDisplayID holding, CGRect bounds)
{
  CGDirectDisplayID ids[64];
  uint32_t count = 0;
  if (CGMainDisplayID() == holding || CGGetActiveDisplayList(64, ids, &count) != kCGErrorSuccess || count < 2 ||
      count == 64) {
    return false;
  }
  bool found = false;
  for (uint32_t index = 0; index < count; ++index) {
    if (ids[index] == holding) {
      found = true;
    } else if (CGRectIntersectsRect(bounds, CGDisplayBounds(ids[index]))) {
      return false;
    }
  }
  return found;
}

#ifndef DESKFLOW_HOLDING_DISPLAY_HELPER
void stopOwnedHelper(QProcess &process)
{
  const auto pid = process.processId();
  lifecycleDiagnostic(
      "Holding display", "helper.stop", "started", "Stop the exact owned holding helper", {{"helperPid", pid}}
  );
  // QProcess owns a dedicated managed stdin pipe. The child exits on EOF,
  // including if this host crashes; the child never inherits its write end.
  process.closeWriteChannel();
  if (process.state() != QProcess::NotRunning && !process.waitForFinished(2000)) {
    process.terminate();
    if (!process.waitForFinished(1000)) {
      process.kill();
      process.waitForFinished(1000);
    }
  }
  lifecycleDiagnostic(
      "Holding display", "helper.stop", process.state() == QProcess::NotRunning ? "completed" : "failed",
      "Holding helper stop returned", {{"helperPid", pid}, {"exitCode", process.exitCode()}}
  );
}
#endif

} // namespace

struct HoldingDisplay::Impl : QObject
{
#ifdef DESKFLOW_HOLDING_DISPLAY_HELPER
  id<DSHoldingVirtualDisplay> display = nil;
#else
  std::unique_ptr<QProcess> helper;
  std::unique_ptr<HoldingDisplayStartup> startup;
  std::vector<std::function<void(bool, QString)>> completions;
  QByteArray response;
  bool notifications = false, replied = false;
  Rect requestedBounds;
  static void displayChanged(CGDirectDisplayID display, CGDisplayChangeSummaryFlags flags, void *context)
  {
    auto *self = static_cast<Impl *>(context);
    // CoreGraphics says its state is current after the completion callback.
    // Queue verification so neither completion nor helper teardown reconfigures
    // displays from inside the native notification itself.
    if (!(flags & kCGDisplayBeginConfigurationFlag))
      QMetaObject::invokeMethod(
          self,
          [self, display, flags] {
            if (self->startup && self->startup->pending()) {
              lifecycleDiagnostic(
                  "Holding display", "geometry.notification", "observed", "Display change completed",
                  {{"displayId", qint64(display)}, {"flags", qint64(flags)}}
              );
              self->startup->changed();
            }
          },
          Qt::QueuedConnection
      );
  }
  void removeNotifications()
  {
    if (notifications) {
      CGDisplayRemoveReconfigurationCallback(displayChanged, this);
      notifications = false;
    }
  }
#endif
  CGDirectDisplayID id = kCGNullDirectDisplay;
  unsigned width = 0;
  unsigned height = 0;
};

HoldingDisplay::HoldingDisplay() : m_impl(std::make_unique<Impl>())
{
}

HoldingDisplay::~HoldingDisplay()
{
  shutdown();
}

bool HoldingDisplay::supported(QString *reason)
{
  if (reason) {
    reason->clear();
  }
  @autoreleasepool {
    Runtime runtime;
    return runtime.load(reason);
  }
}

bool HoldingDisplay::create(QString *error, unsigned width, unsigned height)
{
  lifecycleDiagnostic(
      "Holding display", "create", "started", "Create and verify the hidden display",
      {{"width", qint64(width)}, {"height", qint64(height)}}
  );
  if (error) {
    error->clear();
  }
  if (![NSThread isMainThread]) {
    return fail(error, QStringLiteral("Holding-display creation must run on the host's main thread."));
  }
  if (available()) {
    if (width != m_impl->width || height != m_impl->height)
      return fail(error, QStringLiteral("Restore exported windows before changing the holding-display mode."));
    lifecycleDiagnostic("Holding display", "create", "completed", "Existing holding display remains valid");
    return true;
  }
#ifdef DESKFLOW_HOLDING_DISPLAY_HELPER
  const bool previousOwner = m_impl->display != nil;
#else
  const bool previousOwner = m_impl->helper != nullptr;
#endif
  if (previousOwner) {
    return fail(error, QStringLiteral("The holding display was lost; restore exported windows before recreating it."));
  }
  if (width < 640 || height < 480 || width > 8192 || height > 8192 || width % 2 || height % 2) {
    return fail(error, QStringLiteral("Holding-display dimensions must be even and between 640x480 and 8192x8192."));
  }

#ifndef DESKFLOW_HOLDING_DISPLAY_HELPER
  // Synchronous adapter for the explicit standalone manual utility. The native
  // Host calls createAsync and never nests its event loop here.
  QEventLoop loop;
  bool done = false, created = false;
  QString failure;
  createAsync(
      [&](bool success, QString reason) {
        created = success;
        failure = std::move(reason);
        done = true;
        loop.quit();
      },
      width, height
  );
  if (!done)
    loop.exec();
  if (error)
    *error = failure;
  return created;
#else
  @autoreleasepool {
    @try {
      Runtime runtime;
      if (!runtime.load(error)) {
        return false;
      }
      std::vector<DisplaySnapshot> physical;
      if (!snapshot(physical, error)) {
        return false;
      }
      const CGDirectDisplayID originalMain = CGMainDisplayID();
      // Quartz removes gaps between displays. Request an adjacent location and
      // validate the actual result rather than claiming off-desktop placement.
      const auto requested = requestedHoldingBounds(physical, width, height);
      const double x = requested.x, y = requested.y;
      if (!requested.valid()) {
        return fail(error, QStringLiteral("No valid holding-display location is available."));
      }

      id<DSHoldingDescriptor> descriptor = [runtime.descriptor new];
      [descriptor setName:@"Deskflow Holding Display (Experimental)"];
      [descriptor setVendorID:virtualDisplayVendor];
      [descriptor setProductID:virtualDisplayProduct];
      [descriptor setSerialNum:virtualDisplaySerial];
      [descriptor setSerialNumber:virtualDisplaySerial];
      [descriptor setMaxPixelsWide:width];
      [descriptor setMaxPixelsHigh:height];
      [descriptor setSizeInMillimeters:CGSizeMake(25.4 * width / 110.0, 25.4 * height / 110.0)];
      [descriptor setRedPrimary:CGPointMake(0.64, 0.33)];
      [descriptor setGreenPrimary:CGPointMake(0.30, 0.60)];
      [descriptor setBluePrimary:CGPointMake(0.15, 0.06)];
      [descriptor setWhitePoint:CGPointMake(0.3127, 0.3290)];
      [descriptor setQueue:dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0)];

      id<DSHoldingVirtualDisplay> display =
          [(id<DSHoldingVirtualDisplay>)[runtime.display alloc] initWithDescriptor:descriptor];
      if (!display) {
        return fail(error, QStringLiteral("macOS refused virtual-display creation; source windows stay visible."));
      }
      id<DSHoldingMode> mode = [(id<DSHoldingMode>)[runtime.mode alloc] initWithWidth:width
                                                                               height:height
                                                                          refreshRate:60.0];
      id<DSHoldingSettings> settings = [runtime.settings new];
      if (!mode || !settings) {
        return fail(error, QStringLiteral("macOS refused virtual-display settings; source windows stay visible."));
      }
      [settings setHiDPI:0];
      [settings setRotation:0];
      [settings setModes:@[ mode ]];
      if (![display applySettings:settings]) {
        return fail(error, QStringLiteral("macOS refused the holding-display mode; source windows stay visible."));
      }
      const CGDirectDisplayID id = [display displayID];
      lifecycleDiagnostic(
          "Holding display", "helper.settings", "completed", "Applied virtual display mode", {{"displayId", qint64(id)}}
      );
      if (!id || !CGDisplayIsActive(id) || !holdingIdentity(id)) {
        return fail(
            error,
            QStringLiteral(
                "The holding-display identity or active state could not be verified; source windows stay visible."
            )
        );
      }

      CGDisplayConfigRef configuration = nullptr;
      CGError status = CGBeginDisplayConfiguration(&configuration);
      if (status != kCGErrorSuccess || !configuration) {
        return fail(
            error,
            QStringLiteral("Cannot begin holding-display placement (%1); source windows stay visible.").arg(status)
        );
      }
      status = CGConfigureDisplayOrigin(configuration, id, static_cast<int32_t>(x), static_cast<int32_t>(y));
      if (status != kCGErrorSuccess) {
        CGCancelDisplayConfiguration(configuration);
        return fail(
            error, QStringLiteral("Cannot position the holding display (%1); source windows stay visible.").arg(status)
        );
      }
      // Never save this experimental arrangement as a session/permanent layout.
      status = CGCompleteDisplayConfiguration(configuration, kCGConfigureForAppOnly);
      if (status != kCGErrorSuccess) {
        return fail(
            error,
            QStringLiteral("Holding-display placement was rejected (%1); source windows stay visible.").arg(status)
        );
      }
      const CGRect destination = CGDisplayBounds(id);
      lifecycleDiagnostic(
          "Holding display", "helper.geometry", "completed", "Helper-side display observation",
          {{"displayId", qint64(id)},
           {"physicalLayoutUnchanged", unchanged(physical, originalMain)},
           {"active", bool(CGDisplayIsActive(id))},
           {"mirrored", bool(CGDisplayIsInMirrorSet(id))},
           {"x", destination.origin.x},
           {"y", destination.origin.y},
           {"width", destination.size.width},
           {"height", destination.size.height}}
      );
      if (!unchanged(physical, originalMain) || !CGDisplayIsActive(id) || CGDisplayIsInMirrorSet(id) ||
          !asRect(destination).valid() || destination.size.width != width || destination.size.height != height) {
        return fail(
            error, QStringLiteral(
                       "Holding-display placement changed the physical layout or has invalid geometry; source windows "
                       "stay visible."
                   )
        );
      }
      for (const auto &existing : physical) {
        if (CGRectIntersectsRect(destination, existing.bounds)) {
          return fail(
              error, QStringLiteral("The holding display overlaps a physical display; source windows stay visible.")
          );
        }
      }
      CGDisplayModeRef actualMode = CGDisplayCopyDisplayMode(id);
      const bool correctMode =
          actualMode && CGDisplayModeGetWidth(actualMode) == width && CGDisplayModeGetHeight(actualMode) == height;
      if (actualMode) {
        CGDisplayModeRelease(actualMode);
      }
      if (!correctMode) {
        return fail(
            error, QStringLiteral("The active holding-display mode could not be verified; source windows stay visible.")
        );
      }
      m_impl->display = display;
      m_impl->id = id;
      m_impl->width = width;
      m_impl->height = height;
      lifecycleDiagnostic(
          "Holding display", "create", "completed", "Helper retained verified hidden display",
          {{"displayId", qint64(id)}}
      );
      return true;
    } @catch (NSException *exception) {
      return fail(
          error, QStringLiteral("Private macOS virtual-display API raised %1; source windows stay visible.")
                     .arg(QString::fromUtf8(exception.name.UTF8String))
      );
    }
  }
#endif
}

void HoldingDisplay::createAsync(std::function<void(bool, QString)> completion, unsigned width, unsigned height)
{
#ifdef DESKFLOW_HOLDING_DISPLAY_HELPER
  QString reason;
  const bool success = create(&reason, width, height);
  completion(success, reason);
#else
  const auto rejected = [&](const QString &reason) {
    lifecycleDiagnostic("Holding display", "create", "failed", reason);
    completion(false, reason);
  };
  if (![NSThread isMainThread]) {
    rejected("Holding-display creation must run on the host's main thread");
    return;
  }
  if (width < 640 || height < 480 || width > 8192 || height > 8192 || width % 2 || height % 2) {
    rejected("Holding-display dimensions must be even and between 640x480 and 8192x8192");
    return;
  }
  if (m_impl->startup && m_impl->startup->pending()) {
    if (width != m_impl->width || height != m_impl->height)
      rejected("A different holding-display mode is already being prepared");
    else
      m_impl->completions.push_back(std::move(completion));
    return;
  }
  if (available()) {
    if (width != m_impl->width || height != m_impl->height)
      rejected("Restore exported windows before changing the holding-display mode");
    else
      completion(true, {});
    return;
  }
  if (m_impl->helper) {
    rejected("The holding display was lost; restore exported windows before recreating it");
    return;
  }
  const QString path = QCoreApplication::applicationDirPath() + "/deskflow-holding-display-helper";
  const QFileInfo executable(path);
  if (!executable.isFile() || !executable.isExecutable() ||
      (executable.ownerId() != getuid() && executable.ownerId() != 0) ||
      (executable.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther))) {
    rejected("The owned holding-display helper is missing or has unsafe permissions");
    return;
  }
  std::vector<DisplaySnapshot> physical;
  QString geometryError;
  if (!snapshot(physical, &geometryError)) {
    rejected(geometryError);
    return;
  }
  m_impl->requestedBounds = requestedHoldingBounds(physical, width, height);
  if (!m_impl->requestedBounds.valid()) {
    rejected("Cannot determine the requested holding-display position");
    return;
  }
  lifecycleDiagnostic(
      "Holding display", "create", "started", "Create hidden display without blocking native events",
      {{"width", qint64(width)}, {"height", qint64(height)}}
  );
  m_impl->width = width;
  m_impl->height = height;
  m_impl->response.clear();
  m_impl->replied = false;
  m_impl->completions.push_back(std::move(completion));
  m_impl->helper = std::make_unique<QProcess>();
  auto *helper = m_impl->helper.get();
  helper->setProcessChannelMode(QProcess::ForwardedErrorChannel);
  helper->setInputChannelMode(QProcess::ManagedInputChannel);
  helper->setReadChannel(QProcess::StandardOutput);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  helper->setUnixProcessParameters(QProcess::UnixProcessFlag::CloseFileDescriptors);
#endif
  m_impl->startup = std::make_unique<HoldingDisplayStartup>(
      [this](QString *reason) {
        const bool verified = available(reason);
        const auto actual = CGDisplayBounds(m_impl->id);
        lifecycleDiagnostic(
            "Holding display", "geometry.inspect", "observed", "Independent parent-side display observation",
            {{"displayId", qint64(m_impl->id)},
             {"active", bool(CGDisplayIsActive(m_impl->id))},
             {"identityMatches", holdingIdentity(m_impl->id)},
             {"verified", verified},
             {"x", actual.origin.x},
             {"y", actual.origin.y},
             {"width", actual.size.width},
             {"height", actual.size.height},
             {"pendingReason", *reason}}
        );
        return verified;
      },
      [this](bool success, QString reason) {
        auto completions = std::move(m_impl->completions);
        m_impl->removeNotifications();
        if (!success) {
          stopOwnedHelper(*m_impl->helper);
          m_impl->helper.reset();
          m_impl->id = 0;
          m_impl->width = m_impl->height = 0;
        }
        lifecycleDiagnostic(
            "Holding display", "create", success ? "completed" : "failed",
            success ? "Hidden display independently verified" : reason
        );
        // A completion may close its Host. All state changes above precede it;
        // only detached callback values are touched from here onward.
        for (auto &callback : completions)
          callback(success, reason);
      }
  );
  auto *startup = m_impl->startup.get();
  // Queued slots never tear down QProcess from inside one of its own signals.
  QObject::connect(helper, &QProcess::started, startup, [startup] { startup->started(); }, Qt::QueuedConnection);
  QObject::connect(
      helper, &QProcess::errorOccurred, startup,
      [this](QProcess::ProcessError) {
        if (m_impl->startup->pending())
          m_impl->startup->fail("Holding-display helper failed: " + m_impl->helper->errorString());
      },
      Qt::QueuedConnection
  );
  QObject::connect(
      helper, &QProcess::finished, startup,
      [startup](int, QProcess::ExitStatus) {
        startup->fail("Holding-display helper exited before independent display readiness");
      },
      Qt::QueuedConnection
  );
  QObject::connect(
      helper, &QProcess::readyReadStandardOutput, startup,
      [this] {
        auto &state = *m_impl;
        if (!state.startup->pending() || !state.helper->bytesAvailable())
          return;
        state.response += state.helper->read(4097 - state.response.size());
        if (state.response.size() > 4096 || state.helper->bytesAvailable() > 0 || state.replied) {
          state.startup->fail("Holding-display helper response exceeded its bound");
          return;
        }
        if (!state.response.contains('\n'))
          return;
        if (!state.response.endsWith('\n') || state.response.indexOf('\n') != state.response.size() - 1) {
          state.startup->fail("The holding-display helper returned multiple readiness records");
          return;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(state.response.trimmed(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
          state.startup->fail("The holding-display helper returned invalid readiness data");
          return;
        }
        const auto ready = document.object();
        lifecycleDiagnostic("Holding display", "helper.reply", "completed", "Holding helper replied", ready);
        const auto id = ready.value("displayId").toInteger();
        if (ready.value("state").toString() != "ready" || id <= 0 || id > UINT32_MAX ||
            state.helper->state() != QProcess::Running) {
          state.startup->fail(
              "Holding display unavailable: " + ready.value("error").toString("Invalid helper identity")
          );
          return;
        }
        state.id = static_cast<CGDirectDisplayID>(id);
        state.replied = true;
        state.startup->ready();
      },
      Qt::QueuedConnection
  );
  const auto registered = CGDisplayRegisterReconfigurationCallback(Impl::displayChanged, m_impl.get());
  if (registered != kCGErrorSuccess) {
    startup->fail(QString("Cannot observe native display readiness (%1)").arg(registered));
    return;
  }
  m_impl->notifications = true;
  helper->start(path, {"--owned-stdio", QString::number(width), QString::number(height)});
#endif
}

std::optional<HoldingResizeDisplay> HoldingDisplay::verifiedSnapshot(QString *reason) const
{
  if (reason)
    reason->clear();
  const auto unavailable = [&](const QString &message) -> std::optional<HoldingResizeDisplay> {
    if (reason)
      *reason = message;
    return std::nullopt;
  };
#ifdef DESKFLOW_HOLDING_DISPLAY_HELPER
  const bool ownerAvailable = m_impl->display != nil;
#else
  const bool ownerAvailable = m_impl->helper && m_impl->helper->state() == QProcess::Running;
#endif
  if (!ownerAvailable)
    return unavailable("The holding display owner is not running");
  if (!m_impl->id)
    return unavailable("The holding display ID is missing");
  if (!CGDisplayIsActive(m_impl->id))
    return unavailable("The parent does not observe an active holding display");
  if (CGDisplayIsInMirrorSet(m_impl->id))
    return unavailable("The holding display is mirrored");
  if (!holdingIdentity(m_impl->id))
    return unavailable("The holding display vendor/product/serial identity changed");
  const CGRect bounds = CGDisplayBounds(m_impl->id);
  if (!asRect(bounds).valid())
    return unavailable("The holding display bounds are invalid");
  if (bounds.size.width != m_impl->width || bounds.size.height != m_impl->height)
    return unavailable(QStringLiteral("The holding display size is %1x%2; expected %3x%4")
                           .arg(bounds.size.width)
                           .arg(bounds.size.height)
                           .arg(m_impl->width)
                           .arg(m_impl->height));
#ifndef DESKFLOW_HOLDING_DISPLAY_HELPER
  // WindowServer can briefly report the right size at a transitional origin
  // (observed on macOS: y=-1 before the requested y=0). Keep native display
  // notifications armed until the requested placement is actually visible.
  // An active, isolated display alone is not proof that configuration finished.
  if (bounds.origin.x != m_impl->requestedBounds.x || bounds.origin.y != m_impl->requestedBounds.y)
    return unavailable(QStringLiteral("Holding display origin is (%1,%2); waiting for requested (%3,%4)")
                           .arg(bounds.origin.x)
                           .arg(bounds.origin.y)
                           .arg(m_impl->requestedBounds.x)
                           .arg(m_impl->requestedBounds.y));
#endif
  if (!isolated(m_impl->id, bounds))
    return unavailable("The holding display is not isolated from the physical displays");
  return HoldingResizeDisplay{m_impl->id, asRect(bounds)};
}

bool HoldingDisplay::available(QString *reason) const
{
  return verifiedSnapshot(reason).has_value();
}

std::uint32_t HoldingDisplay::displayId() const
{
  const auto observed = verifiedSnapshot();
  return observed ? observed->id : 0;
}

Rect HoldingDisplay::bounds() const
{
  const auto observed = verifiedSnapshot();
  return observed ? observed->bounds : Rect{};
}

void HoldingDisplay::shutdown()
{
#ifndef DESKFLOW_HOLDING_DISPLAY_HELPER
  m_impl->removeNotifications();
  if (m_impl->startup)
    m_impl->startup->cancel();
  m_impl->completions.clear();
  m_impl->id = kCGNullDirectDisplay;
  m_impl->width = 0;
  m_impl->height = 0;
  if (m_impl->helper) {
    stopOwnedHelper(*m_impl->helper);
    m_impl->helper.reset();
  }
#else
  // Release our strong reference before the helper exits. Process exit is the
  // verified removal boundary; some macOS versions retain the display after
  // the object releases. The native host must restore sources before shutdown.
  // Private teardown can create autoreleased objects. Drain them here even for
  // command-line callers that do not have AppKit's per-iteration pool.
  @autoreleasepool {
    m_impl->id = kCGNullDirectDisplay;
    m_impl->width = 0;
    m_impl->height = 0;
    m_impl->display = nil;
  }
#endif
}

} // namespace deskflow::windowshare
