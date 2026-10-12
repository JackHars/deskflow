/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace deskflow::windowshare {

inline constexpr std::size_t maxLocalInputLibraryConstraintBytes = 64 * 1024;
inline constexpr std::size_t maxLocalInputLibraryConstraintHashes = 1024;
using LocalInputLibraryCDHash = std::array<std::uint8_t, 20>;

// Parsed semantics only, not authenticated code/process evidence. A successful
// parse says exactly: team-identifier == teamIdentifier AND cdhash is a member
// of cdHashes. Hashes are nonempty, unique, and sorted lexicographically.
struct LocalInputLibraryConstraint
{
  std::string teamIdentifier;
  std::vector<LocalInputLibraryCDHash> cdHashes;
  bool operator==(const LocalInputLibraryConstraint &) const = default;
};

// Accepts the FULL embedded constraint blob, including big-endian magic and
// length. Supports only the observed DER version-1 wrapper with ccat=0, comp=1,
// vers=1 and the exact two-predicate schema. Team IDs must be ten ASCII capitals
// or digits. Rejects other operators, predicates, wrappers, duplicate keys or
// hashes, unsupported/noncanonical encodings, extra bytes, and allocation failure.
//
// The caller must separately authenticate the unique library slot (11) against
// its exact live-pinned CodeDirectory and compare this policy with its sealed
// expected allowlist. Parsing neither proves OS enforcement nor removes Apple's
// system-library exception, identifies loaded images, or authorizes input.
std::optional<LocalInputLibraryConstraint>
parseLocalInputLibraryConstraint(std::span<const std::uint8_t> fullBlob) noexcept;

} // namespace deskflow::windowshare
