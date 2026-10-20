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
    const NativeSurfaceInventoryCheck deferred{1, {1, 2}, {2}};
    check(deferred.finish(1, {2, 1}), "final identity snapshot could not finish the same input inspection");
    check(!deferred.finish(2, {1, 2}), "deferred inventory changed its exact source window");
    check(!deferred.finish(1, {1, 2, 3}), "popup appearing during later native checks was ignored");
    check(!deferred.finish(1, {2}), "source disappearing during later native checks was ignored");
    check(!deferred.finish(1, {1, 2, 2}), "ambiguous final identity snapshot was accepted");
    check(!deferred.finish(1, {}), "missing final inventory accepted an initial observation");
    const NativeSurfaceInventoryCheck attributedLater{1, {1, 2}, {2}, std::vector<std::uint32_t>{1, 2, 3}};
    check(attributedLater.finish(1, {2, 1}, {3, 2, 1}), "stable global IDs and exact final attribution were rejected");
    check(!attributedLater.finish(1, {1, 2, 3}, {1, 2, 3}), "unclassified source-process popup was accepted");
    check(!attributedLater.finish(1, {1, 2}, {1, 2}), "unclassified disappearing window escaped the global check");
    check(!attributedLater.finish(1, {1, 2}, {1, 2, 3, 4}), "new native window escaped the global check");
    check(!attributedLater.finish(1, {1, 2}, {1, 2, 3, 3}), "duplicate global ID was accepted");
    check(!attributedLater.finish(1, {1, 2}), "missing final global observation was accepted");
    check(!attributedLater.finish(1, {1}, {1, 2, 3}), "stale AX sibling classification was accepted");
    check(
        !NativeSurfaceInventoryCheck{1, {1, 2}, {2}, std::vector<std::uint32_t>{1, 2, 2}}.finish(1, {1, 2}, {1, 2}),
        "ambiguous preliminary global IDs were accepted"
    );
    check(
        !NativeSurfaceInventoryCheck{1, {1, 2}, {2}, std::vector<std::uint32_t>{1, 3}}.finish(1, {1, 2}, {1, 3}),
        "AX sibling absent from native inventory was accepted"
    );
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
