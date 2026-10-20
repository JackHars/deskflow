/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <cstdint>
#include <set>
#include <vector>

namespace deskflow::windowshare {

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

} // namespace deskflow::windowshare
