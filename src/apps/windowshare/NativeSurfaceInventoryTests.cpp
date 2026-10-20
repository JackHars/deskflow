/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeSurfaceInventory.h"
#include <cstdio>
#include <stdexcept>

using namespace deskflow::windowshare;
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
int main()
{
  try {
    check(completeUnrelatedSurfaceInventory(1, {1}, {1}, {}), "sole source requires no child search");
    check(completeUnrelatedSurfaceInventory(1, {1, 2}, {2, 1}, {2}), "fresh unrelated window was not accounted");
    check(!completeUnrelatedSurfaceInventory(1, {1, 2, 3}, {1, 2, 3}, {2}), "unknown popup skipped inspection");
    check(!completeUnrelatedSurfaceInventory(1, {1, 2}, {1, 2, 3}, {2}), "new popup skipped inspection");
    check(!completeUnrelatedSurfaceInventory(1, {1, 2, 3}, {1, 2}, {2}), "changed inventory skipped inspection");
    check(!completeUnrelatedSurfaceInventory(1, {2}, {2}, {2}), "missing source was accepted");
    check(!completeUnrelatedSurfaceInventory(1, {1, 2}, {1, 2}, {2, 3}), "stale unrelated proof was accepted");
    check(!completeUnrelatedSurfaceInventory(1, {1}, {1}, {1}), "source classified as unrelated");
    check(!completeUnrelatedSurfaceInventory(0, {}, {}, {}), "absent owner was accepted");
    check(!completeUnrelatedSurfaceInventory(1, {1, 0}, {1, 0}, {0}), "invalid window was accepted");
    check(!completeUnrelatedSurfaceInventory(1, {1, 2, 2}, {1, 2}, {2}), "duplicate initial ID was accepted");
    check(!completeUnrelatedSurfaceInventory(1, {1, 2}, {1, 2, 2}, {2}), "duplicate final ID was accepted");
    check(!completeUnrelatedSurfaceInventory(1, {1, 2}, {1, 2}, {2, 2}), "ambiguous AX classification was accepted");
    const auto dialogInventory = [](bool applicationParent, std::optional<bool> modal) {
      const std::vector<std::uint32_t> siblings =
          unrelatedApplicationWindow(NativeRootWindowKind::Dialog, applicationParent, modal)
              ? std::vector<std::uint32_t>{2}
              : std::vector<std::uint32_t>{};
      return completeUnrelatedSurfaceInventory(1, {1, 2}, {1, 2}, siblings);
    };
    check(dialogInventory(true, false), "independent nonmodal dialog forced a source control-tree scan");
    check(!dialogInventory(true, true), "application-modal dialog skipped inspection");
    check(!dialogInventory(true, std::nullopt), "missing modality skipped inspection");
    check(!dialogInventory(false, false), "attached nonmodal dialog skipped inspection");
    check(
        unrelatedApplicationWindow(NativeRootWindowKind::Standard, true, false) &&
            !unrelatedApplicationWindow(NativeRootWindowKind::Standard, true, true) &&
            !unrelatedApplicationWindow(NativeRootWindowKind::Standard, true, std::nullopt),
        "standard-window subrole substituted for a fresh nonmodal observation"
    );
    check(
        !unrelatedApplicationWindow(NativeRootWindowKind::Unknown, true, false),
        "unclassified application-owned window skipped inspection"
    );
    std::puts("PASS: complete fresh native surface inventories and conservative fallbacks");
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
