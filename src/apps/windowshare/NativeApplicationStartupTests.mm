/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeApplicationStartup.h"
#include <AppKit/AppKit.h>
#include <QApplication>
#include <QTimer>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
} // namespace

// No window, capture, permission request, network or input injection. Use the
// real Cocoa dispatcher: an offscreen fixture cannot observe AppKit launch.
int main(int argc, char **argv)
{
  @autoreleasepool {
    unsigned launches = 0;
    auto *const launchCount = &launches;
    auto *center = NSNotificationCenter.defaultCenter;
    id observer = [center addObserverForName:NSApplicationDidFinishLaunchingNotification
                                      object:nil
                                       queue:nil
                                  usingBlock:^(NSNotification *) {
                                    ++*launchCount;
                                  }];
    int result = 1;
    try {
      QApplication application(argc, argv);
      application.setQuitOnLastWindowClosed(false);
      check(QGuiApplication::platformName() == QStringLiteral("cocoa"), "Native startup requires Cocoa");
      deskflow::windowshare::initializeNativeApplication();
      check(launches == 1, "Native session construction would precede AppKit launch completion");
      check(application.topLevelWidgets().empty(), "Startup unexpectedly opened a window");
      deskflow::windowshare::initializeNativeApplication();
      check(launches == 1, "Initializing again repeated AppKit launch");

      bool entered = false, launchedBeforeWork = false;
      QTimer::singleShot(0, &application, [&] {
        entered = true;
        launchedBeforeWork = launches == 1;
        application.quit();
      });
      check(application.exec() == 0 && entered && launchedBeforeWork, "Session work preceded AppKit initialization");
      check(launches == 1, "Entering the production event loop repeated AppKit launch");
      std::puts("Native application initialization precedes session work and occurs once");
      result = 0;
    } catch (const std::exception &error) {
      std::fprintf(stderr, "Native application startup: %s\n", error.what());
    }
    [center removeObserver:observer];
    return result;
  }
}
