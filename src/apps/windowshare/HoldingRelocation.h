/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "Core.h"
#include <functional>
#include <memory>
#include <utility>

namespace deskflow::windowshare {
struct HoldingResizeDisplay
{
  std::uint32_t id = 0;
  Rect bounds;
};
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
  std::function<void(Rect, std::function<void(bool)>)> place;
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
// Callbacks retain their own authority and native handles across the async
// placement. Completion verifies the same display, owner and painted geometry
// again. A native write/notification alone cannot commit an export.
inline void relocateHoldingWindow(
    Owner owner, std::uint64_t paintedGeometry, HoldingRelocationAccess access,
    std::function<void(HoldingRelocationResult)> completion
)
{
  using Status = HoldingRelocationResult::Status;
  auto once = std::make_shared<std::function<void(HoldingRelocationResult)>>(std::move(completion));
  const auto finish = [once](HoldingRelocationResult result) {
    auto callback = std::move(*once);
    *once = {};
    if (callback)
      callback(result);
  };
  if (!owner.session || !owner.epoch || !paintedGeometry || !access.window || !access.display || !access.persist ||
      !access.place) {
    finish({Status::Refused, {}, "request"});
    return;
  }
  const auto sameFrame = [](Rect a, Rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
  };
  const auto bound = [owner, paintedGeometry](const std::optional<HoldingRelocationWindow> &window) {
    return window && window->owner == owner && window->geometry == paintedGeometry &&
           window->producedGeometry == paintedGeometry && window->frame.valid();
  };
  try {
    const auto before = access.window();
    if (!bound(before)) {
      finish({Status::Refused, {}, "initial-window"});
      return;
    }
    const auto display = access.display();
    if (!display || !display->id || !display->bounds.valid()) {
      finish({Status::Restore, {}, "display"});
      return;
    }
    const auto current = [getDisplay = access.display, getWindow = access.window, display, sameFrame,
                          bound]() -> std::optional<HoldingRelocationWindow> {
      const auto observed = getDisplay();
      const auto window = getWindow();
      if (!observed || observed->id != display->id || !sameFrame(observed->bounds, display->bounds) || !bound(window))
        return std::nullopt;
      return window;
    };
    if (!access.persist()) {
      finish({Status::Restore, {}, "journal"});
      return;
    }
    const auto prepared = current();
    if (!prepared || !sameFrame(prepared->frame, before->frame)) {
      finish({Status::Restore, {}, "before-placement"});
      return;
    }
    access.place(fitInside(before->frame, display->bounds), [once, finish, current, display](bool placed) {
      if (!*once)
        return; // A duplicate or late native completion cannot re-enter ownership.
      try {
        if (!placed) {
          finish({Status::Restore, {}, "native-placement"});
          return;
        }
        const auto after = current();
        if (!after) {
          finish({Status::Restore, {}, "after-placement"});
          return;
        }
        if (after->frame.x < display->bounds.x || after->frame.y < display->bounds.y ||
            after->frame.x + after->frame.width > display->bounds.x + display->bounds.width ||
            after->frame.y + after->frame.height > display->bounds.y + display->bounds.height) {
          finish({Status::Restore, {}, "containment"});
          return;
        }
        finish({Status::Applied, after->frame});
      } catch (...) {
        finish({Status::Restore, {}, "callback-exception"});
      }
    });
  } catch (...) {
    finish({Status::Restore, {}, "callback-exception"});
  }
}
} // namespace deskflow::windowshare
