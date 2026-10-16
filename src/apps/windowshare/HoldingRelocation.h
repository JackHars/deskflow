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
  using WindowCompletion = std::function<void(std::optional<HoldingRelocationWindow>)>;
  std::function<std::optional<HoldingRelocationWindow>()> window;
  // Native readers may need to yield while the application's AX bridge is
  // busy. Each reader owns a bounded deadline and rechecks authority before
  // completing. A missing result is terminal, never permission to use a cache.
  std::function<void(WindowCompletion)> observeWindow;
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
  if (!owner.session || !owner.epoch || !paintedGeometry || (!access.window && !access.observeWindow) ||
      !access.display || !access.persist || !access.place) {
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
  if (!access.observeWindow)
    access.observeWindow = [read = access.window](HoldingRelocationAccess::WindowCompletion complete) {
      complete(read());
    };
  // Suppress duplicate callbacks at each stage, including callbacks arriving
  // while a later asynchronous read is pending.
  const auto stage = std::make_shared<unsigned>(0);
  const auto observe = [read = access.observeWindow, once, finish, stage](unsigned expected, auto complete) {
    try {
      read([once, finish, stage, expected, complete](auto window) {
        if (!*once || *stage != expected)
          return;
        ++*stage;
        try {
          complete(window);
        } catch (...) {
          finish({Status::Restore, {}, "callback-exception"});
        }
      });
    } catch (...) {
      finish({Status::Restore, {}, "callback-exception"});
    }
  };
  observe(0, [=](const auto &before) {
    if (!bound(before)) {
      finish({Status::Refused, {}, "initial-window"});
      return;
    }
    const auto display = access.display();
    if (!display || !display->id || !display->bounds.valid()) {
      finish({Status::Restore, {}, "display"});
      return;
    }
    const auto current = [getDisplay = access.display, display, sameFrame, bound](const auto &window) {
      const auto observed = getDisplay();
      return observed && observed->id == display->id && sameFrame(observed->bounds, display->bounds) && bound(window);
    };
    if (!access.persist()) {
      finish({Status::Restore, {}, "journal"});
      return;
    }
    observe(1, [=](const auto &prepared) {
      if (!current(prepared) || !sameFrame(prepared->frame, before->frame)) {
        finish({Status::Restore, {}, "before-placement"});
        return;
      }
      access.place(fitInside(before->frame, display->bounds), [=](bool placed) {
        if (!*once || *stage != 2)
          return;
        ++*stage;
        if (!placed) {
          finish({Status::Restore, {}, "native-placement"});
          return;
        }
        observe(3, [=](const auto &after) {
          if (!current(after)) {
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
        });
      });
    });
  });
}
} // namespace deskflow::windowshare
