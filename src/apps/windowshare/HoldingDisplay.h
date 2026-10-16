/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "Core.h"
#include "HoldingRelocation.h"

#include <QString>
#include <functional>
#include <memory>

namespace deskflow::windowshare {

// A held-window resize is a journaled geometry transaction. The callbacks let
// the native host revalidate its exact window/display identity at each step;
// isolated tests use rectangles only, without creating or moving any display.
struct HoldingResizeAccess
{
  std::function<std::optional<HoldingResizeDisplay>()> display;
  std::function<std::optional<Rect>()> window;
  std::function<bool()> persist;
  std::function<bool(Point)> move;
  std::function<bool(double, double)> resize;
};
struct HoldingResizeResult
{
  enum class Status
  {
    Applied,
    Refused, // No native mutation was attempted; the existing export survives.
    Restore  // Identity/containment was lost or a native mutation partly failed.
  };
  Status status = Status::Refused;
  Rect actual;
};
HoldingResizeResult resizeHoldingWindow(int width, int height, const HoldingResizeAccess &access);

// Experimental macOS-only owner of one virtual holding display via a dedicated
// child process. The helper lives alongside the host executable. Construction
// and supported() do not change the display configuration. Call create() only
// after explicit runtime opt-in, from the host's main thread, never an event tap.
// This uses private APIs and may be unavailable on any macOS release.
class HoldingDisplay final
{
public:
  HoldingDisplay();
  ~HoldingDisplay();
  HoldingDisplay(const HoldingDisplay &) = delete;
  HoldingDisplay &operator=(const HoldingDisplay &) = delete;

  // Reports the runtime API schema, not proof that display creation will work.
  static bool supported(QString *reason = nullptr);
  bool create(QString *error = nullptr, unsigned width = 1920, unsigned height = 1080);
  // Production creation returns to the event loop while helper/display events
  // arrive. shutdown() cancels pending completions. Same-mode callers join the
  // same owned creation; each must revalidate its source before continuing.
  void createAsync(std::function<void(bool, QString)> completion, unsigned width = 1920, unsigned height = 1080);
  // Return the ID and bounds from one verified native observation. Callers
  // must still revalidate before/after mutations, rather than cache authority.
  std::optional<HoldingResizeDisplay> verifiedSnapshot(QString *reason = nullptr) const;
  bool available(QString *reason = nullptr) const;
  std::uint32_t displayId() const;
  Rect bounds() const;

  // Restore every source window while this object is alive, then shut it down.
  // Closing the helper's owned pipe exits it; host crashes also close that pipe.
  // Failed create() leaves the caller in source-visible mode: never minimize or
  // move a source unless available() and its actual destination bounds verify.
  void shutdown();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace deskflow::windowshare
