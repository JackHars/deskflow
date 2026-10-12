/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputLibraryConstraint.h"

#include <algorithm>
#include <string_view>

namespace deskflow::windowshare {
namespace {
// Apple documents the implicit top-level AND, string Team fact, and binary
// cdhash/$in semantics at:
// https://developer.apple.com/documentation/security/defining-launch-environment-and-library-constraints
// The magic/header and 20-byte CDHash are specified in Apple's cs_blobs.h:
// https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/kern/cs_blobs.h
//
// The exact supported DER shape below was independently extracted from the
// public codesign tool's signed library-constraint output (macOS 26.1), including
// the four metadata entries. We require their exact values, not an assumption
// about unknown versions/categories. No private decoder or API is called.
constexpr std::uint32_t constraintMagic = 0xfade8181;
constexpr std::uint8_t application = 0x70, dictionary = 0xb0, sequence = 0x30, integer = 0x02, utf8String = 0x0c,
                       octetString = 0x04;

std::uint32_t bigEndian32(std::span<const std::uint8_t> bytes) noexcept
{
  return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) | (std::uint32_t(bytes[2]) << 8) |
         std::uint32_t(bytes[3]);
}

// A bounded, nonrecursive reader. Each schema level consumes a subspan and must
// exhaust it explicitly. Only definite, minimally encoded DER lengths are
// supported; the whole-blob bound means at most two length octets are needed.
class Reader
{
public:
  explicit Reader(std::span<const std::uint8_t> bytes) : m_bytes(bytes)
  {
  }
  bool empty() const noexcept
  {
    return m_bytes.empty();
  }
  std::span<const std::uint8_t> bytes() const noexcept
  {
    return m_bytes;
  }
  std::optional<Reader> take(std::uint8_t tag) noexcept
  {
    if (m_bytes.size() < 2 || m_bytes[0] != tag)
      return std::nullopt;
    std::size_t header = 2, length = m_bytes[1];
    if (length & 0x80) {
      const std::size_t count = length & 0x7f;
      if (!count || count > 2 || m_bytes.size() < header + count || !m_bytes[header])
        return std::nullopt;
      length = 0;
      for (std::size_t i = 0; i < count; ++i)
        length = (length << 8) | m_bytes[header++];
      if (length < 128 || (count == 2 && length <= 255))
        return std::nullopt;
    }
    if (length > m_bytes.size() - header)
      return std::nullopt;
    const auto value = m_bytes.subspan(header, length);
    m_bytes = m_bytes.subspan(header + length);
    return Reader(value);
  }
  bool expectString(std::string_view expected) noexcept
  {
    const auto value = take(utf8String);
    return value && value->bytes().size() == expected.size() &&
           std::equal(value->bytes().begin(), value->bytes().end(), expected.begin());
  }
  bool expectInteger(std::uint8_t expected) noexcept
  {
    const auto value = take(integer);
    return value && value->bytes().size() == 1 && value->bytes()[0] == expected;
  }

private:
  std::span<const std::uint8_t> m_bytes;
};

bool integerEntry(Reader &parent, std::string_view name, std::uint8_t expected) noexcept
{
  auto entry = parent.take(sequence);
  return entry && entry->expectString(name) && entry->expectInteger(expected) && entry->empty();
}

bool hashEntry(Reader &parent, std::vector<LocalInputLibraryCDHash> &hashes)
{
  auto entry = parent.take(sequence);
  if (!entry || !entry->expectString("cdhash"))
    return false;
  auto operators = entry->take(dictionary);
  if (!operators || !entry->empty())
    return false;
  auto in = operators->take(sequence);
  if (!in || !operators->empty() || !in->expectString("$in"))
    return false;
  auto array = in->take(sequence);
  if (!array || !in->empty() || array->empty())
    return false;
  // Each supported element is exactly the primitive 0x04, short length 20,
  // and 20 data bytes. Avoid allocating from an untrusted declared count.
  if (array->bytes().size() % 22 || array->bytes().size() / 22 > maxLocalInputLibraryConstraintHashes)
    return false;
  hashes.reserve(array->bytes().size() / 22);
  while (!array->empty()) {
    const auto data = array->take(octetString);
    if (!data || data->bytes().size() != 20 || hashes.size() == maxLocalInputLibraryConstraintHashes)
      return false;
    LocalInputLibraryCDHash hash{};
    std::copy(data->bytes().begin(), data->bytes().end(), hash.begin());
    hashes.push_back(hash);
  }
  std::sort(hashes.begin(), hashes.end());
  return std::adjacent_find(hashes.begin(), hashes.end()) == hashes.end();
}

bool teamEntry(Reader &parent, std::string &team)
{
  auto entry = parent.take(sequence);
  if (!entry || !entry->expectString("team-identifier"))
    return false;
  const auto value = entry->take(utf8String);
  if (!value || !entry->empty() || value->bytes().size() != 10 ||
      !std::all_of(value->bytes().begin(), value->bytes().end(), [](std::uint8_t byte) {
        return (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9');
      }))
    return false;
  team.assign(value->bytes().begin(), value->bytes().end());
  return true;
}
} // namespace

std::optional<LocalInputLibraryConstraint>
parseLocalInputLibraryConstraint(std::span<const std::uint8_t> fullBlob) noexcept
{
  if (fullBlob.size() < 8 || fullBlob.size() > maxLocalInputLibraryConstraintBytes ||
      bigEndian32(fullBlob.first(4)) != constraintMagic || bigEndian32(fullBlob.subspan(4, 4)) != fullBlob.size())
    return std::nullopt;
  try {
    Reader blob(fullBlob.subspan(8));
    auto root = blob.take(application);
    if (!root || !blob.empty() || !root->expectInteger(1))
      return std::nullopt;
    auto metadata = root->take(dictionary);
    if (!metadata || !root->empty() || !integerEntry(*metadata, "ccat", 0) || !integerEntry(*metadata, "comp", 1))
      return std::nullopt;
    auto entry = metadata->take(sequence);
    if (!entry || !entry->expectString("reqs"))
      return std::nullopt;
    auto requirements = entry->take(dictionary);
    if (!requirements || !entry->empty())
      return std::nullopt;
    LocalInputLibraryConstraint result;
    if (!hashEntry(*requirements, result.cdHashes) || !teamEntry(*requirements, result.teamIdentifier) ||
        !requirements->empty() || !integerEntry(*metadata, "vers", 1) || !metadata->empty())
      return std::nullopt;
    return result;
  } catch (...) {
    // Allocation failure also denies the policy; never return partial facts.
    return std::nullopt;
  }
}

} // namespace deskflow::windowshare
