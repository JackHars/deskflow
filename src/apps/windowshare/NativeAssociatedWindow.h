/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "Core.h"

namespace deskflow::windowshare {

struct NativeAssociatedParent
{
  Owner owner;
  std::uint32_t window = 0;
  std::int32_t process = 0;
  std::int64_t birth = 0;
  bool operator==(const NativeAssociatedParent &) const = default;
};
enum class NativeAssociatedRole
{
  Other,
  Sheet,
  Drawer
};
struct NativeAssociatedEvidence
{
  NativeAssociatedParent observedParent;
  NativeAssociatedRole role = NativeAssociatedRole::Other;
  bool exactFocusedTopLevel = false, exactAttachedParent = false;
};

// Per-export terminal fallback, not child input authority. Once an exact
// attached child needs unsupported capture/input, callback reentry or a later
// focus change must not reopen this export while its owned return is queued.
class NativeAssociatedReturn final
{
public:
  bool observe(const NativeAssociatedParent &expected, const NativeAssociatedEvidence &evidence)
  {
    if (m_parent || !expected.owner.session || !expected.owner.epoch || !expected.window || expected.process <= 0 ||
        expected.birth <= 0 || evidence.observedParent != expected || !evidence.exactFocusedTopLevel ||
        !evidence.exactAttachedParent ||
        (evidence.role != NativeAssociatedRole::Sheet && evidence.role != NativeAssociatedRole::Drawer))
      return false;
    m_parent = expected;
    return true;
  }
  bool blocked() const noexcept
  {
    return m_parent.has_value();
  }
  bool returning(const NativeAssociatedParent &parent) const noexcept
  {
    return m_parent == parent;
  }

private:
  std::optional<NativeAssociatedParent> m_parent;
};

} // namespace deskflow::windowshare
