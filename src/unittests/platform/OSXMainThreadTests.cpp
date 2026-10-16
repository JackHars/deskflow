/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "deskflow/unix/AppUtilUnix.h"
#include "platform/OSXMainThread.h"
#include <QCoreApplication>
#include <QTimer>
#include <cstdlib>
#include <dispatch/dispatch.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  AppUtilUnix utility(nullptr);
  bool passed = false;
  const auto verifyMain = [] {
    dispatch_assert_queue(dispatch_get_main_queue());
    if (!pthread_main_np())
      throw std::runtime_error("Native input-source operation ran off the main thread");
  };
  onMacMainThread(verifyMain);
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &application, [] {
    std::cerr << "Main-thread input-source dispatch deadlocked\n";
    std::_Exit(1);
  });
  deadline.start(10000);
  std::thread worker([&] {
    try {
      if (pthread_main_np())
        throw std::runtime_error("Regression must originate on a worker thread");
      onMacMainThread([&] {
        verifyMain();
        onMacMainThread(verifyMain); // Main-thread reentry must not dispatch_sync itself.
      });
      const auto value = onMacMainThread([&] {
        verifyMain();
        return std::make_unique<int>(42);
      });
      if (!value || *value != 42)
        throw std::runtime_error("Native result ownership was not returned to its caller");
      bool caught = false;
      try {
        onMacMainThread([] { throw std::runtime_error("native failure"); });
      } catch (const std::runtime_error &) {
        caught = true;
      }
      if (!caught)
        throw std::runtime_error("Native exception did not reach its caller");
      // Exercise the startup call named in the actual SIGILL stack, plus the
      // current-layout reader. Read-only: no layout selection or input events.
      if (utility.getKeyboardLayoutList().empty() || utility.getCurrentLanguageCode().empty())
        throw std::runtime_error("Native keyboard input-source enumeration returned no language");
      passed = true;
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
    }
    QMetaObject::invokeMethod(&application, &QCoreApplication::quit, Qt::QueuedConnection);
  });
  application.exec();
  worker.join();
  return passed ? 0 : 1;
}
