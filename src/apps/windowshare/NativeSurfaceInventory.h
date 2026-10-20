/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace deskflow::windowshare {

enum class NativeRootWindowKind
{
  Unknown,
  Standard,
  Dialog
};

// A root-level nonmodal window is a sibling, not an attached source surface.
// Unknown modality is not evidence that a window is nonmodal.
inline bool unrelatedApplicationWindow(NativeRootWindowKind kind, bool applicationParent, std::optional<bool> modal)
{
  return applicationParent && modal && !*modal &&
         (kind == NativeRootWindowKind::Standard || kind == NativeRootWindowKind::Dialog);
}

// This only permits skipping a child-tree search. Every live independent
// native window must be the source or freshly proven unrelated in AX. Missing,
// changing or ambiguous inventories require the ordinary full search instead.
inline bool completeUnrelatedSurfaceInventory(
    std::uint32_t source, const std::vector<std::uint32_t> &before, const std::vector<std::uint32_t> &after,
    const std::vector<std::uint32_t> &unrelated
)
{
  const auto unique = [](const auto &ids) {
    const std::set<std::uint32_t> values(ids.begin(), ids.end());
    return !values.contains(0) && values.size() == ids.size();
  };
  if (!source || !unique(before) || !unique(after) || !unique(unrelated))
    return false;
  const std::set<std::uint32_t> first(before.begin(), before.end()), last(after.begin(), after.end());
  std::set<std::uint32_t> accounted(unrelated.begin(), unrelated.end());
  if (accounted.contains(source))
    return false;
  accounted.insert(source);
  return first == last && first == accounted;
}

// One stack-local input inspection can join its final inventory with the final
// native identity read. Initial AX classification is provisional until finish;
// no observation is reused by another input, and a changed native list fails.
struct NativeSurfaceInventoryCheck
{
  std::uint32_t source;
  std::vector<std::uint32_t> before, unrelated;
  bool finish(std::uint32_t expectedSource, const std::vector<std::uint32_t> &after) const
  {
    return source == expectedSource && completeUnrelatedSurfaceInventory(source, before, after, unrelated);
  }
};

} // namespace deskflow::windowshare
