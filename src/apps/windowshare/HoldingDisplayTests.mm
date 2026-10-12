/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "HoldingDisplay.h"

#include <CoreGraphics/CoreGraphics.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

using deskflow::windowshare::HoldingDisplay;

namespace {
void check(bool condition, const char *message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct Display
{
  CGDirectDisplayID id;
  CGRect bounds;
  int32_t mode;
  std::size_t pixelWidth;
  std::size_t pixelHeight;
  double refreshRate;
};

std::vector<Display> displays()
{
  std::array<CGDirectDisplayID, 64> ids{};
  uint32_t count = 0;
  check(
      CGGetActiveDisplayList(ids.size(), ids.data(), &count) == kCGErrorSuccess && count < ids.size(),
      "read active display snapshot"
  );
  std::vector<Display> result;
  for (uint32_t index = 0; index < count; ++index) {
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(ids[index]);
    const int32_t modeId = mode ? CGDisplayModeGetIODisplayModeID(mode) : 0;
    const auto pixelWidth = mode ? CGDisplayModeGetPixelWidth(mode) : 0;
    const auto pixelHeight = mode ? CGDisplayModeGetPixelHeight(mode) : 0;
    const auto refreshRate = mode ? CGDisplayModeGetRefreshRate(mode) : 0;
    if (mode) {
      CGDisplayModeRelease(mode);
    }
    result.push_back({ids[index], CGDisplayBounds(ids[index]), modeId, pixelWidth, pixelHeight, refreshRate});
  }
  std::sort(result.begin(), result.end(), [](const Display &a, const Display &b) { return a.id < b.id; });
  return result;
}

bool sameLayout(const std::vector<Display> &before, CGDirectDisplayID originalMain)
{
  const auto after = displays();
  if (before.size() != after.size() || CGMainDisplayID() != originalMain) {
    return false;
  }
  for (std::size_t index = 0; index < before.size(); ++index) {
    if (before[index].id != after[index].id || before[index].pixelWidth != after[index].pixelWidth ||
        before[index].pixelHeight != after[index].pixelHeight ||
        before[index].refreshRate != after[index].refreshRate ||
        !CGRectEqualToRect(before[index].bounds, after[index].bounds)) {
      return false;
    }
  }
  return true;
}

void printLayout(const char *label, const std::vector<Display> &snapshot, CGDirectDisplayID main)
{
  std::cout << label << ": main=" << main << " count=" << snapshot.size() << '\n';
  for (const auto &display : snapshot) {
    std::cout << "  id=" << display.id << " mode=" << display.mode << " bounds=" << display.bounds.origin.x << ','
              << display.bounds.origin.y << ' ' << display.bounds.size.width << 'x' << display.bounds.size.height
              << " pixels=" << display.pixelWidth << 'x' << display.pixelHeight << " refresh=" << display.refreshRate
              << '\n';
  }
  std::cout.flush();
}

bool drainUntilRestored(const std::vector<Display> &before, CGDirectDisplayID originalMain)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  do {
    if (sameLayout(before, originalMain)) {
      return true;
    }
    // Give WindowServer removal notifications time to arrive without an
    // unbounded wait. This is a standalone manual test, never an event tap.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } while (std::chrono::steady_clock::now() < deadline);
  return sameLayout(before, originalMain);
}

void creationCycle(HoldingDisplay &display, const std::vector<Display> &before, CGDirectDisplayID originalMain)
{
  printLayout("Before manual creation", before, originalMain);
  // A standalone command-line test needs the same reconfiguration notification
  // event source that a running Cocoa/Qt GUI already installs.
  const auto callback = [](CGDirectDisplayID, CGDisplayChangeSummaryFlags, void *) {};
  check(
      CGDisplayRegisterReconfigurationCallback(callback, nullptr) == kCGErrorSuccess,
      "register display notification event source"
  );
  struct CallbackScope
  {
    CGDisplayReconfigurationCallBack callback;
    ~CallbackScope()
    {
      CGDisplayRemoveReconfigurationCallback(callback, nullptr);
    }
  } callbackScope{callback};
  QString reason;
  bool created = false;
  std::exception_ptr validationFailure;
  CGDirectDisplayID createdId = 0;
  deskflow::windowshare::Rect destination;
  @autoreleasepool {
    created = display.create(&reason, 1920, 1080);
    if (created) {
      try {
        check(display.available(), "created holding display must remain available");
        createdId = display.displayId();
        destination = display.bounds();
        check(
            createdId != 0 &&
                deskflow::windowshare::isWindowshareHoldingDisplay(
                    CGDisplayVendorNumber(createdId), CGDisplayModelNumber(createdId), CGDisplaySerialNumber(createdId)
                ),
            "created display must carry all three Deskflow identity markers"
        );
        check(
            destination.valid() && destination.width == 1920 && destination.height == 1080,
            "created holding display must have verified 1920x1080 bounds"
        );
        const auto current = displays();
        check(
            current.size() == before.size() + 1 && CGMainDisplayID() == originalMain,
            "creation must add exactly one display without changing main display"
        );
        const CGRect holdingBounds = CGDisplayBounds(createdId);
        for (const auto &physical : before) {
          const auto found =
              std::find_if(current.begin(), current.end(), [&](const Display &item) { return item.id == physical.id; });
          // Quartz can assign another IODisplayModeID when adding an extended
          // display. Compare actual pixels, refresh rate and logical bounds.
          check(
              found != current.end() && found->pixelWidth == physical.pixelWidth &&
                  found->pixelHeight == physical.pixelHeight && found->refreshRate == physical.refreshRate &&
                  CGRectEqualToRect(found->bounds, physical.bounds),
              "creation must preserve each existing display's mode and bounds"
          );
          check(
              !CGRectIntersectsRect(holdingBounds, physical.bounds),
              "holding display cannot overlap an existing display"
          );
        }
        const auto fitted = deskflow::windowshare::fitInside({0, 0, 800, 600}, destination);
        check(
            fitted.valid() && destination.contains({fitted.x, fitted.y}) &&
                destination.contains({fitted.x + fitted.width - 1, fitted.y + fitted.height - 1}),
            "source-window destination must fit completely inside the holding display"
        );
      } catch (...) {
        validationFailure = std::current_exception();
      }
    }
    // No user windows are ever moved by this test. Release our display even when
    // creation/validation failed, and prove the physical layout recovered first.
    display.shutdown();
  }
  std::cout << (created ? "Manual creation succeeded; verifying removal"
                        : "Manual creation failed: " + reason.toStdString())
            << std::endl;
  const bool restored = drainUntilRestored(before, originalMain);
  if (!restored) {
    printLayout("After failed shutdown verification", displays(), CGMainDisplayID());
  }
  check(restored, "owned display removal and original physical layout did not verify within five seconds");
  check(!display.available() && display.displayId() == 0, "shutdown must clear owned display state");
  if (!created) {
    throw std::runtime_error(
        "create failed: " + reason.toStdString() + "; verified no display remains and original layout is unchanged"
    );
  }
  if (validationFailure) {
    std::rethrow_exception(validationFailure);
  }
  std::cout << "PASS: created holding display " << createdId << " at " << destination.x << ',' << destination.y
            << " 1920x1080; verified identity, isolation, unchanged physical modes/main, and complete removal\n";
}

int crashTestOwner()
{
  // Invoked only by the explicit crash test below. A pipe and timeout also
  // ensure this test owner exits if its supervising test fails or disappears.
  struct stat input{}, output{};
  if (fstat(STDIN_FILENO, &input) != 0 || !S_ISFIFO(input.st_mode) || fstat(STDOUT_FILENO, &output) != 0 ||
      !S_ISFIFO(output.st_mode)) {
    return 2;
  }
  auto *application = QCoreApplication::instance();
  QSocketNotifier parentInput(STDIN_FILENO, QSocketNotifier::Read, application);
  QObject::connect(&parentInput, &QSocketNotifier::activated, application, [application] { application->quit(); });
  QTimer::singleShot(20000, application, &QCoreApplication::quit);
  HoldingDisplay display;
  QString reason;
  if (!display.create(&reason, 1920, 1080)) {
    std::cerr << reason.toStdString() << '\n';
    return 1;
  }
  const auto response =
      QJsonDocument(QJsonObject{{"displayId", qint64(display.displayId())}}).toJson(QJsonDocument::Compact) + '\n';
  if (::write(STDOUT_FILENO, response.constData(), static_cast<std::size_t>(response.size())) != response.size()) {
    return 1;
  }
  return application->exec();
}

void crashCleanupCycle(const std::vector<Display> &before, CGDirectDisplayID originalMain)
{
  printLayout("Before owner crash test", before, originalMain);
  QProcess owner;
  owner.setProcessChannelMode(QProcess::SeparateChannels);
  owner.setInputChannelMode(QProcess::ManagedInputChannel);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  owner.setUnixProcessParameters(QProcess::UnixProcessFlag::CloseFileDescriptors);
#endif
  owner.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--crash-test-owner")});
  std::exception_ptr validationFailure;
  CGDirectDisplayID createdId = 0;
  try {
    check(owner.waitForStarted(5000), "start isolated host for crash cleanup test");
    std::cout << "Crash test owns child PID " << owner.processId() << std::endl;
    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    while (!response.contains('\n') && owner.state() != QProcess::NotRunning && timer.elapsed() < 15000) {
      response += owner.read(1025 - response.size());
      check(response.size() <= 1024, "crash test owner response stays bounded");
      if (response.contains('\n')) {
        break;
      }
      const qint64 remaining = 15000 - timer.elapsed();
      if (remaining <= 0) {
        break;
      }
      owner.waitForReadyRead(static_cast<int>(remaining));
    }
    response += owner.read(1025 - response.size());
    check(
        response.size() <= 1024 && response.endsWith('\n') && response.indexOf('\n') == response.size() - 1,
        "crash test owner returned one bounded readiness line"
    );
    QJsonParseError error;
    const auto ready = QJsonDocument::fromJson(response.trimmed(), &error);
    check(
        error.error == QJsonParseError::NoError && ready.isObject(), "crash test owner returned valid readiness data"
    );
    const auto reportedId = ready.object().value(QStringLiteral("displayId")).toInteger();
    check(
        reportedId > 0 && reportedId <= UINT32_MAX && owner.state() == QProcess::Running,
        "crash test owner holds an active display"
    );
    createdId = static_cast<CGDirectDisplayID>(reportedId);
    check(
        CGDisplayIsActive(createdId) &&
            deskflow::windowshare::isWindowshareHoldingDisplay(
                CGDisplayVendorNumber(createdId), CGDisplayModelNumber(createdId), CGDisplaySerialNumber(createdId)
            ),
        "crash test display carries all three identity markers"
    );
    check(displays().size() == before.size() + 1, "crash test creates exactly one owned display");
    std::cout << "Crash test child ready with display " << createdId << std::endl;
  } catch (...) {
    validationFailure = std::current_exception();
  }
  // SIGKILL deliberately bypasses the owner's destructor and HoldingDisplay's
  // normal shutdown. The helper must independently observe stdin EOF and exit.
  owner.kill();
  const bool stopped = owner.state() == QProcess::NotRunning || owner.waitForFinished(5000);
  const bool restored = drainUntilRestored(before, originalMain);
  if (!restored) {
    printLayout("After failed crash cleanup verification", displays(), CGMainDisplayID());
  }
  if (validationFailure) {
    try {
      std::rethrow_exception(validationFailure);
    } catch (const std::exception &error) {
      std::cerr << "Crash test startup/validation failed: " << error.what() << '\n';
    }
    const auto detail = owner.readAllStandardError().left(1024);
    if (!detail.isEmpty()) {
      std::cerr << detail.constData() << '\n';
    }
  }
  check(stopped, "killed crash test owner exited within five seconds");
  check(restored, "host crash did not remove its holding display and restore physical layout within five seconds");
  if (validationFailure) {
    std::rethrow_exception(validationFailure);
  }
  check(owner.exitStatus() == QProcess::CrashExit, "crash cleanup exercised a killed host without normal teardown");
  std::cout << "PASS: SIGKILL of the owning host removed holding display " << createdId
            << " via helper pipe EOF; original physical layout and display count restored\n";
}
} // namespace

int run(int argc, char **argv)
{
  const bool createAndDestroy = argc == 2 && std::string_view(argv[1]) == "--create-and-destroy";
  const bool crashCleanup = argc == 2 && std::string_view(argv[1]) == "--crash-owner-cleanup";
  if (argc == 2 && std::string_view(argv[1]) == "--crash-test-owner") {
    return crashTestOwner();
  }
  if (argc != 1 && !createAndDestroy && !crashCleanup) {
    std::cerr << "Usage: holding-display-tests [--create-and-destroy | --crash-owner-cleanup]\n";
    return 2;
  }
  try {
    const auto before = displays();
    const auto mainBefore = CGMainDisplayID();
    HoldingDisplay display;
    check(!display.available(), "construction must not create a display");
    check(display.displayId() == 0 && !display.bounds().valid(), "inactive display has no destination");
    QString reason;
    const bool supported = HoldingDisplay::supported(&reason);
    check(supported ? reason.isEmpty() : !reason.isEmpty(), "unsupported private API must report a reason");
    const QString supportReason = reason;

    // Only rejected inputs are exercised by default. The explicit manual flag
    // below is required to create a real display and change the configuration.
    for (const auto dimensions :
         {std::array<unsigned, 2>{0, 1080},
          {1920, 0},
          {639, 480},
          {640, 479},
          {8194, 1080},
          {1920, 8194},
          {1921, 1080},
          {1920, 1081}}) {
      check(
          !display.create(&reason, dimensions[0], dimensions[1]) && !reason.isEmpty(),
          "invalid dimensions rejected before display creation"
      );
      check(!display.available() && display.displayId() == 0, "rejection leaves source-visible fallback");
    }
    display.shutdown();
    display.shutdown();
    check(sameLayout(before, mainBefore), "probe and rejected requests preserve existing display geometry and modes");
    std::cout << "PASS: read-only holding-display probe and rejected requests; private API schema "
              << (supported ? "present" : "unavailable") << std::endl;
    if (!supported) {
      std::cout << supportReason.toStdString() << '\n';
    }
    if (createAndDestroy) {
      creationCycle(display, before, mainBefore);
    } else if (crashCleanup) {
      crashCleanupCycle(before, mainBefore);
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}

int main(int argc, char **argv)
{
  @autoreleasepool {
    QCoreApplication application(argc, argv);
    return run(argc, argv);
  }
}
