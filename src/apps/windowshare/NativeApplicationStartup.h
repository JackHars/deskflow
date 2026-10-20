/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#ifdef __APPLE__
#include "LifecycleDiagnostics.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#endif

namespace deskflow::windowshare {
// Call once after QApplication construction, before creating native hosts,
// transport, private channels or watchdog timers. There is no input authority
// or source window to service during this initialization turn.
inline void initializeNativeApplication()
{
#ifdef __APPLE__
  if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
    return;
  lifecycleDiagnostic("Host", "application.initialize", "started", "Initialize AppKit before native session work");
  const auto started = std::chrono::steady_clock::now();
  // Qt's Cocoa dispatcher ensures NSApplication has finished launching when
  // processEvents is called outside exec(). It guards Qt sources during that
  // initialization. Calling finishLaunching ourselves would bypass Qt's
  // once-only bookkeeping. This is one initialization turn, not a wait loop.
  QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::ExcludeSocketNotifiers);
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
  lifecycleDiagnostic(
      "Host", "application.initialize", "completed", "AppKit initialization returned before native session work",
      {{"elapsedMs", qint64(elapsed)}}
  );
#endif
}
} // namespace deskflow::windowshare
