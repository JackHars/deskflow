/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "Core.h"

#include <QString>
#include <functional>
#include <memory>

namespace deskflow::windowshare {

// A held-window resize is a journaled geometry transaction. The callbacks let
// the native host revalidate its exact window/display identity at each step;
// isolated tests use rectangles only, without creating or moving any display.
struct HoldingResizeDisplay
{
  std::uint32_t id = 0;
  Rect bounds;
};
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

// The painted acknowledgement must name the current source geometry and a
// frame actually produced for that geometry. Native callbacks additionally
// verify the retained AX/window/process identity and physical output layout.
struct HoldingRelocationWindow
{
  Owner owner;
  std::uint64_t geometry = 0, producedGeometry = 0;
  Rect frame;
};
struct HoldingRelocationAccess
{
  std::function<std::optional<HoldingRelocationWindow>()> window;
  std::function<std::optional<HoldingResizeDisplay>()> display;
  std::function<bool()> persist;
  std::function<bool(Rect)> place;
};
struct HoldingRelocationResult
{
  enum class Status
  {
    Applied,
    Refused, // A stale/malformed acknowledgement performs no native mutation.
    Restore  // A failed/changed owned transaction must return the source.
  };
  Status status = Status::Refused;
  Rect actual;
  // Diagnostic only; never grants ownership or changes rollback policy.
  const char *failureStep = "";
};
HoldingRelocationResult
relocateHoldingWindow(Owner owner, std::uint64_t paintedGeometry, const HoldingRelocationAccess &access);

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
