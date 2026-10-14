/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeWindowIdentity_mac.h"

#import <AppKit/AppKit.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTextStream>
#include <QThread>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

using namespace deskflow::windowshare;
namespace {
struct AXReference
{
  AXUIElementRef value = nullptr;
  ~AXReference()
  {
    if (value)
      CFRelease(value);
  }
};

void require(bool condition, const char *reason)
{
  if (!condition)
    throw std::runtime_error(reason);
}

// This child owns every window queried by the parent. It never activates its
// application, requests permission, posts input or queries another application.
int fixture()
{
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSMutableArray<NSWindow *> *windows = [NSMutableArray array];
    NSWindow * (^makeWindow)(void) = ^{
      auto window = [[NSWindow alloc] initWithContentRect:NSMakeRect(120, 180, 420, 240)
                                                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
      window.title = @"Deskflow exact window identity test";
      window.releasedWhenClosed = NO;
      [window orderFront:nil];
      return window;
    };
    [windows addObject:makeWindow()];
    [windows addObject:makeWindow()];
    auto report = ^(const char *phase) {
      std::printf(
          "{\"phase\":\"%s\",\"pid\":%d,\"ids\":[%ld,%ld],\"sameFrame\":%s,\"sameTitle\":%s}\n", phase, getpid(),
          (long)windows[0].windowNumber, (long)windows[1].windowNumber,
          NSEqualRects(windows[0].frame, windows[1].frame) ? "true" : "false",
          [windows[0].title isEqualToString:windows[1].title] ? "true" : "false"
      );
      std::fflush(stdout);
    };
    report("ready");
    auto input = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, STDIN_FILENO, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(input, ^{
      char command[64] = {};
      const auto count = read(STDIN_FILENO, command, sizeof(command) - 1);
      if (count > 0 && std::strcmp(command, "rename\n") == 0) {
        windows[0].title = @"Deskflow identity renamed";
        report("renamed");
      } else if (count > 0 && std::strcmp(command, "replace\n") == 0) {
        [windows[0] close];
        windows[0] = makeWindow();
        report("replaced");
      } else {
        [windows[0] close];
        [windows[1] close];
        std::exit(0);
      }
    });
    dispatch_resume(input);
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC), dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
        ^{
          std::_Exit(124);
        }
    );
    [NSApp run];
  }
  return 0;
}

int nativeTest(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  if (!AXIsProcessTrusted() || !native::axWindowIdentityAvailable()) {
    QTextStream(stderr) << "NOT_RUN: existing Accessibility permission and native window identity SPI required\n";
    return 77;
  }
  QProcess source;
  source.setProgram(QCoreApplication::applicationFilePath());
  source.setArguments({"--fixture"});
  QJsonObject result{{"inputPosted", false}, {"productionDragAuthorized", false}};
  QJsonArray observations;
  const auto receive = [&](const QString &phase) {
    QElapsedTimer deadline;
    deadline.start();
    while (!source.canReadLine() && deadline.elapsed() < 4000 && source.state() != QProcess::NotRunning)
      source.waitForReadyRead(int(std::max<qint64>(1, 4000 - deadline.elapsed())));
    require(source.canReadLine(), "Owned fixture response timed out");
    const auto line = source.readLine(4097);
    require(line.size() <= 4096 && line.endsWith('\n'), "Owned fixture response exceeded the bound");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(line, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid owned fixture response");
    const auto row = document.object();
    require(row["phase"].toString() == phase && row["pid"].toInteger() == source.processId(), "Wrong fixture owner");
    observations.append(row);
    return row;
  };
  const auto command = [&](const char *value) {
    require(
        source.write(value) == qint64(std::strlen(value)) && source.waitForBytesWritten(1000), "Fixture pipe failed"
    );
  };
  bool passed = false;
  try {
    source.start();
    require(source.waitForStarted(2000), "Owned fixture failed to start");
    const auto ready = receive("ready");
    const auto ids = ready["ids"].toArray();
    require(
        ids.size() == 2 && ready["sameFrame"].toBool() && ready["sameTitle"].toBool(), "Fixture windows must collide"
    );
    const auto firstId = CGWindowID(ids[0].toInteger()), secondId = CGWindowID(ids[1].toInteger());
    require(firstId && secondId && firstId != secondId, "Fixture native IDs must be distinct");
    QString diagnostic;
    bool retryable = false;
    AXReference first, second;
    for (int attempt = 0; attempt < 3 && !first.value; ++attempt) {
      first.value = native::copyExactAXWindow(pid_t(source.processId()), firstId, &diagnostic, &retryable);
      if (!first.value && !retryable)
        break;
      if (!first.value)
        QThread::msleep(50);
    }
    second.value = native::copyExactAXWindow(pid_t(source.processId()), secondId, &diagnostic);
    require(
        first.value && second.value && !CFEqual(first.value, second.value), "Exact lookup confused colliding windows"
    );
    CGWindowID observed = 0;
    require(
        native::axWindowId(first.value, observed) == kAXErrorSuccess && observed == firstId, "Wrong first native ID"
    );
    require(
        native::axWindowId(second.value, observed) == kAXErrorSuccess && observed == secondId, "Wrong second native ID"
    );
    result["collidingWindowsDistinguished"] = true;

    command("rename\n");
    const auto renamed = receive("renamed");
    require(renamed["ids"].toArray() == ids && !renamed["sameTitle"].toBool(), "Fixture rename changed identity");
    AXReference renamedWindow{native::copyExactAXWindow(pid_t(source.processId()), firstId, &diagnostic)};
    require(
        renamedWindow.value && CFEqual(first.value, renamedWindow.value), "Title change replaced the exact AX object"
    );
    result["renameRetainsIdentity"] = true;

    command("replace\n");
    const auto replaced = receive("replaced");
    const auto replacementId = CGWindowID(replaced["ids"].toArray()[0].toInteger());
    require(
        replacementId && replacementId != firstId && replaced["sameFrame"].toBool() && replaced["sameTitle"].toBool(),
        "Replacement must reuse title/frame with a new native identity"
    );
    AXReference stale{native::copyExactAXWindow(pid_t(source.processId()), firstId, &diagnostic)};
    AXReference replacement{native::copyExactAXWindow(pid_t(source.processId()), replacementId, &diagnostic)};
    require(
        !stale.value && replacement.value && !CFEqual(first.value, replacement.value),
        "Replacement revived old identity"
    );
    require(
        native::axWindowId(first.value, observed) != kAXErrorSuccess || observed != replacementId,
        "Retained stale AX object aliased its replacement"
    );
    result["replacementRejectsOldIdentity"] = true;
    passed = true;
  } catch (const std::exception &error) {
    result["error"] = QString::fromUtf8(error.what());
  }
  if (source.state() != QProcess::NotRunning) {
    source.write("quit\n");
    if (!source.waitForFinished(2000)) {
      source.kill();
      source.waitForFinished(1000);
    }
  }
  const bool cleaned =
      source.state() == QProcess::NotRunning && source.exitStatus() == QProcess::NormalExit && source.exitCode() == 0;
  result["ownedChildExited"] = cleaned;
  result["observations"] = observations;
  result["status"] = passed && cleaned ? "passed" : "failed";
  QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Indented);
  QTextStream(stderr) << source.readAllStandardError().left(4096);
  return passed && cleaned ? 0 : 1;
}
} // namespace

int main(int argc, char **argv)
{
  if (argc == 2 && std::strcmp(argv[1], "--fixture") == 0)
    return fixture();
  if (argc == 2 && std::strcmp(argv[1], "--native") == 0)
    return nativeTest(argc, argv);
  std::fputs("Usage: windowshare-native-identity-tests --native\nCreates only owned test windows; no input.\n", stderr);
  return 2;
}
