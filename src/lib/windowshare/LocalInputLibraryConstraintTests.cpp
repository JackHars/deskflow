/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputLibraryConstraint.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string_view>

namespace {
bool failNextAllocation = false;
}
void *operator new(std::size_t size)
{
  if (failNextAllocation) {
    failNextAllocation = false;
    throw std::bad_alloc();
  }
  if (void *memory = std::malloc(size ? size : 1))
    return memory;
  throw std::bad_alloc();
}
void operator delete(void *memory) noexcept
{
  std::free(memory);
}
void operator delete(void *memory, std::size_t) noexcept
{
  std::free(memory);
}

using namespace deskflow::windowshare;
namespace {
using Bytes = std::vector<std::uint8_t>;

void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

Bytes hex(std::string_view text)
{
  check(text.size() % 2 == 0, "hex fixture length");
  Bytes result;
  const auto digit = [](char c) -> unsigned {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    throw std::runtime_error("invalid fixture hex");
  };
  for (std::size_t i = 0; i < text.size(); i += 2)
    result.push_back(static_cast<std::uint8_t>(digit(text[i]) * 16 + digit(text[i + 1])));
  return result;
}

// Extracted verbatim from the LC_CODE_SIGNATURE SuperBlob's library slot 11 of
// build/exact-library-exploration/loader and startup (macOS 26.1, x86_64), both
// Apple Development signed with runtime,library and --library-constraint. The
// loader was rechecked with public codesign --verify --strict. Expected Team
// and CDHash below come independently from constraint.plist/result.json, not
// from the parser. Embedding bytes keeps this pure test portable and offline.
const auto authentic =
    hex("fade818100000084707a020101b07530090c046363617402010030090c04636f6d7002010130520c0472657173b04a"
        "30290c06636468617368b01f301d0c0324696e30160414b6318e78379c45a23075b227c9d1b2254b344f50"
        "301d0c0f7465616d2d6964656e7469666965720c0a4d4d3550424a3944345930090c0476657273020101");
// Further disposable copies were signed through public codesign on the same
// OS with --sign - --library-constraint --enforce-constraint-validity. Neither
// copy was launched. The two-hash fixture deliberately preserves reverse hash
// order in its $in array; the OR fixture expresses weaker, unsupported policy.
const auto authenticTwo =
    hex("fade81810000009c708191020101b0818b30090c046363617402010030090c04636f6d7002010130680c0472657173b060"
        "303f0c06636468617368b03530330c0324696e302c0414b6318e78379c45a23075b227c9d1b2254b344f50"
        "04142bf12c53e8decbbf38f267568fd825d099751dde301d0c0f7465616d2d6964656e7469666965720c0a4d4d3550424a39443459"
        "30090c0476657273020101");
const auto authenticOr =
    hex("fade81810000008e708183020101b07e30090c046363617402010030090c04636f6d70020101305b0c0472657173b053"
        "30510c03246f72b04a30290c06636468617368b01f301d0c0324696e30160414b6318e78379c45a23075b227c9d1b2254b344f50"
        "301d0c0f7465616d2d6964656e7469666965720c0a4d4d3550424a3944345930090c0476657273020101");
const auto hashA = hex("b6318e78379c45a23075b227c9d1b2254b344f50");
const auto hashB = hex("2bf12c53e8decbbf38f267568fd825d099751dde");
constexpr std::string_view team = "MM5PBJ9D4Y";

Bytes concatenate(std::initializer_list<Bytes> elements)
{
  Bytes result;
  for (const auto &element : elements)
    result.insert(result.end(), element.begin(), element.end());
  return result;
}
Bytes concatenate(const std::vector<Bytes> &elements)
{
  Bytes result;
  for (const auto &element : elements)
    result.insert(result.end(), element.begin(), element.end());
  return result;
}
Bytes tlv(std::uint8_t tag, const Bytes &value)
{
  Bytes result{tag};
  if (value.size() < 128) {
    result.push_back(static_cast<std::uint8_t>(value.size()));
  } else {
    Bytes length;
    for (auto size = value.size(); size; size >>= 8)
      length.insert(length.begin(), static_cast<std::uint8_t>(size & 0xff));
    result.push_back(static_cast<std::uint8_t>(0x80 | length.size()));
    result.insert(result.end(), length.begin(), length.end());
  }
  result.insert(result.end(), value.begin(), value.end());
  return result;
}
Bytes string(std::string_view text)
{
  return tlv(0x0c, Bytes(text.begin(), text.end()));
}
Bytes integer(std::uint8_t value)
{
  return tlv(0x02, {value});
}
Bytes entry(std::string_view key, const Bytes &value)
{
  return tlv(0x30, concatenate({string(key), value}));
}
Bytes dictionary(const std::vector<Bytes> &entries)
{
  return tlv(0xb0, concatenate(entries));
}
Bytes array(const std::vector<Bytes> &elements)
{
  return tlv(0x30, concatenate(elements));
}
Bytes cdhash(const std::vector<Bytes> &elements)
{
  return entry("cdhash", dictionary({entry("$in", array(elements))}));
}
Bytes requirements(const std::vector<Bytes> &elements, Bytes teamValue = string(team))
{
  return dictionary({cdhash(elements), entry("team-identifier", teamValue)});
}
void setLength(Bytes &blob)
{
  check(blob.size() >= 8, "blob fixture header");
  for (unsigned i = 0; i < 4; ++i)
    blob[4 + i] = static_cast<std::uint8_t>(blob.size() >> ((3 - i) * 8));
}
Bytes blob(const Bytes &body)
{
  Bytes result{0xfa, 0xde, 0x81, 0x81, 0, 0, 0, 0};
  result.insert(result.end(), body.begin(), body.end());
  setLength(result);
  return result;
}
Bytes wrapped(const std::vector<Bytes> &metadata, Bytes version = integer(1))
{
  return blob(tlv(0x70, concatenate({version, dictionary(metadata)})));
}
std::vector<Bytes> metadata(const Bytes &reqs)
{
  return {entry("ccat", integer(0)), entry("comp", integer(1)), entry("reqs", reqs), entry("vers", integer(1))};
}
Bytes policy(const Bytes &reqs)
{
  return wrapped(metadata(reqs));
}
LocalInputLibraryCDHash typedHash(const Bytes &bytes)
{
  check(bytes.size() == 20, "expected hash size");
  LocalInputLibraryCDHash result{};
  std::copy(bytes.begin(), bytes.end(), result.begin());
  return result;
}
void rejected(const Bytes &value, const char *reason)
{
  check(!parseLocalInputLibraryConstraint(value), reason);
}

void testAuthenticBlobs()
{
  const auto result = parseLocalInputLibraryConstraint(authentic);
  check(
      result && result->teamIdentifier == team &&
          result->cdHashes == std::vector<LocalInputLibraryCDHash>{typedHash(hashA)},
      "authentic signed one-hash semantics"
  );
  const auto two = parseLocalInputLibraryConstraint(authenticTwo);
  check(
      two && two->teamIdentifier == team &&
          two->cdHashes == std::vector<LocalInputLibraryCDHash>{typedHash(hashB), typedHash(hashA)},
      "authentic long-length blob normalizes reversed hash array"
  );
  rejected(authenticOr, "authentic weaker OR policy rejected");
  check(
      policy(requirements({tlv(0x04, hashA)})) == authentic, "independent fixture writer matches actual signed output"
  );
  check(
      policy(requirements({tlv(0x04, hashA), tlv(0x04, hashB)})) == authenticTwo,
      "fixture writer handles observed long-length boundaries"
  );
  rejected(
      policy(requirements({tlv(0x04, hashA), tlv(0x04, hashA)})),
      "codesign-accepted duplicate hashes remain unsupported policy"
  );
}

void testEnvelopeAndTruncation()
{
  for (const auto *source : {&authentic, &authenticTwo}) {
    for (std::size_t size = 0; size < source->size(); ++size) {
      Bytes prefix(source->begin(), source->begin() + static_cast<std::ptrdiff_t>(size));
      rejected(prefix, "every truncated prefix denied");
      if (size >= 8) {
        setLength(prefix);
        rejected(prefix, "nested truncation denied even after outer length repair");
      }
    }
    auto trailing = *source;
    trailing.push_back(0);
    rejected(trailing, "trailing bytes violate full blob length");
    setLength(trailing);
    rejected(trailing, "trailing bytes after root cannot hide in repaired length");
  }
  for (const std::uint8_t magicByte : {std::uint8_t(0), std::uint8_t(0x71), std::uint8_t(0xcc)}) {
    auto wrong = authentic;
    wrong[2] = magicByte;
    rejected(wrong, "wrong embedded blob magic");
  }
  auto swapped = authentic;
  std::reverse(swapped.begin(), swapped.begin() + 4);
  rejected(swapped, "native-endian magic not supported");
  swapped = authentic;
  std::reverse(swapped.begin() + 4, swapped.begin() + 8);
  rejected(swapped, "native-endian length not supported");
  auto oversized = authentic;
  oversized.resize(maxLocalInputLibraryConstraintBytes + 1);
  setLength(oversized);
  rejected(oversized, "whole-blob limit applies before DER work");
  auto extraRoot = Bytes(authentic.begin() + 8, authentic.end());
  extraRoot.insert(extraRoot.end(), authentic.begin() + 8, authentic.end());
  rejected(blob(extraRoot), "two roots cannot grant combined authority");
}

void testEncodingStrictness()
{
  const Bytes body(authentic.begin() + 10, authentic.end());
  for (const Bytes &prefix :
       {Bytes{0x70, 0x80}, Bytes{0x70, 0x81, 0x7a}, Bytes{0x70, 0x82, 0, 0x7a}, Bytes{0x70, 0x83, 0, 0, 0x7a},
        Bytes{0x70, 0xff}, Bytes{0x7f, 0x10, 0x7a}, Bytes{0x50, 0x7a}, Bytes{0x30, 0x7a},
        Bytes{0x70, 0x82, 0xff, 0xff}}) {
    auto malformed = concatenate({prefix, body});
    rejected(blob(malformed), "unsupported/noncanonical root encoding denied");
  }
  auto indefinite = concatenate({Bytes{0x70, 0x80}, body, Bytes{0, 0}});
  rejected(blob(indefinite), "BER indefinite length with EOC denied");
  const auto validReqs = requirements({tlv(0x04, hashA)});
  for (const auto &version :
       {integer(0), integer(2), tlv(0x02, {}), tlv(0x02, {0, 1}), tlv(0x02, {0xff}), tlv(0x02, Bytes(16, 1)),
        tlv(0x01, {0xff}), tlv(0x22, {1})})
    rejected(wrapped(metadata(validReqs), version), "unsupported or noncanonical wrapper version");
  for (std::size_t field : {std::size_t(0), std::size_t(1), std::size_t(3)}) {
    for (const auto &value : {integer(2), tlv(0x02, {0, 1}), tlv(0x01, {1}), string("1")}) {
      auto values = metadata(validReqs);
      values[field] = entry(field == 0 ? "ccat" : field == 1 ? "comp" : "vers", value);
      rejected(wrapped(values), "unsupported metadata value/type/encoding");
    }
  }
  // Nesting within a hash is never traversed. This cannot grow parser stack.
  Bytes nested = tlv(0x04, hashA);
  for (unsigned i = 0; i < 256; ++i)
    nested = tlv(0x30, nested);
  rejected(policy(requirements({nested})), "nested expression in hash position denied");
  const auto nonminimalHash = concatenate({Bytes{0x04, 0x81, 20}, hashA});
  rejected(policy(requirements({nonminimalHash})), "nonminimal primitive data length denied");
  const auto longFormTeam = concatenate({Bytes{0x0c, 0x81, 10}, Bytes(team.begin(), team.end())});
  rejected(policy(requirements({tlv(0x04, hashA)}, longFormTeam)), "nonminimal team length denied");
}

void testExactPredicateSet()
{
  const auto hashes = cdhash({tlv(0x04, hashA)});
  const auto teamEntry = entry("team-identifier", string(team));
  for (const auto &entries :
       {std::vector<Bytes>{}, std::vector<Bytes>{hashes}, std::vector<Bytes>{teamEntry},
        std::vector<Bytes>{hashes, hashes, teamEntry}, std::vector<Bytes>{hashes, teamEntry, teamEntry},
        std::vector<Bytes>{teamEntry, hashes},
        std::vector<Bytes>{hashes, teamEntry, entry("validation-category", integer(1))},
        std::vector<Bytes>{entry("$or", dictionary({hashes, teamEntry}))},
        std::vector<Bytes>{entry("$optional", dictionary({hashes})), teamEntry},
        std::vector<Bytes>{entry("$and", dictionary({hashes, teamEntry}))}})
    rejected(policy(dictionary(entries)), "only exact fixed two-predicate dictionary accepted");
  for (const auto &name : {"$or", "$optional", "$and", "$gt", "$IN", "$in-extra"})
    rejected(
        policy(dictionary({entry("cdhash", dictionary({entry(name, array({tlv(0x04, hashA)}))})), teamEntry})),
        "only exact $in operator supported"
    );
  rejected(
      policy(dictionary({entry("cdhash", tlv(0x04, hashA)), teamEntry})), "direct hash fact is outside packaged schema"
  );
  rejected(
      policy(dictionary(
          {entry(
               "cdhash", dictionary({entry("$in", array({tlv(0x04, hashA)})), entry("$in", array({tlv(0x04, hashB)}))})
           ),
           teamEntry}
      )),
      "duplicate operator keys rejected"
  );
  rejected(
      policy(dictionary({hashes, entry("team-identifier", dictionary({entry("$in", array({string(team)}))}))})),
      "team membership expression is outside exact schema"
  );
  auto values = metadata(requirements({tlv(0x04, hashA)}));
  for (std::size_t field = 0; field < values.size(); ++field) {
    auto duplicate = values;
    duplicate.insert(duplicate.begin() + static_cast<std::ptrdiff_t>(field), values[field]);
    rejected(wrapped(duplicate), "duplicate metadata key denied");
    auto missing = values;
    missing.erase(missing.begin() + static_cast<std::ptrdiff_t>(field));
    rejected(wrapped(missing), "missing wrapper metadata denied");
  }
  std::swap(values[0], values[1]);
  rejected(wrapped(values), "noncanonical wrapper dictionary ordering rejected");
  values = metadata(requirements({tlv(0x04, hashA)}));
  values.push_back(entry("unknown", integer(0)));
  rejected(wrapped(values), "unknown wrapper metadata rejected");
  values = metadata(requirements({tlv(0x04, hashA)}));
  values[0] = tlv(0x30, concatenate({string("ccat"), integer(0), integer(0)}));
  rejected(wrapped(values), "pair cannot contain a third value");
  values[0] = tlv(0x30, string("ccat"));
  rejected(wrapped(values), "pair must contain its value");
  const auto nulKey = entry(std::string_view("cdhash\0", 7), dictionary({entry("$in", array({tlv(0x04, hashA)}))}));
  rejected(policy(dictionary({nulKey, teamEntry})), "embedded NUL cannot alias known key");
}

void testTeamAndHashValues()
{
  for (const std::string &invalid :
       {std::string{}, std::string("MM5PBJ9D4"), std::string("MM5PBJ9D4YY"), std::string("mm5pbj9d4y"),
        std::string("MM5P-J9D4Y"), std::string("MM5P\0J9D4Y", 10), std::string(10, '\xff')})
    rejected(policy(requirements({tlv(0x04, hashA)}, string(invalid))), "Team grammar strict ASCII ten characters");
  rejected(
      policy(requirements({tlv(0x04, hashA)}, tlv(0x16, Bytes(team.begin(), team.end())))),
      "alternate ASN.1 string encoding unsupported"
  );
  for (std::size_t size : {std::size_t(0), std::size_t(19), std::size_t(21), std::size_t(32)})
    rejected(policy(requirements({tlv(0x04, Bytes(size, 0x7a))})), "CDHash width is exactly twenty bytes");
  for (const std::uint8_t tag : {std::uint8_t(0x03), std::uint8_t(0x24), std::uint8_t(0x0c), std::uint8_t(0x30)})
    rejected(policy(requirements({tlv(tag, hashA)})), "hash must be primitive OCTET STRING");
  rejected(policy(requirements({})), "empty hash allowlist denied");
  rejected(
      policy(requirements({tlv(0x04, hashA), tlv(0x04, hashB), tlv(0x04, hashA)})),
      "nonadjacent duplicate hash denied after normalization"
  );
  const auto zero = parseLocalInputLibraryConstraint(policy(requirements({tlv(0x04, Bytes(20, 0))})));
  check(zero && zero->cdHashes[0] == LocalInputLibraryCDHash{}, "all binary hash bytes are data, not string sentinels");
  const auto ones = parseLocalInputLibraryConstraint(policy(requirements({tlv(0x04, Bytes(20, 0xff))})));
  check(ones && ones->cdHashes[0][19] == 0xff, "high-bit CDHash bytes are preserved");
}

void testBoundsAndAllocation()
{
  std::vector<Bytes> hashes;
  std::vector<LocalInputLibraryCDHash> expected;
  for (std::size_t i = 0; i < maxLocalInputLibraryConstraintHashes; ++i) {
    Bytes hash(20, 0);
    hash[18] = static_cast<std::uint8_t>(i >> 8);
    hash[19] = static_cast<std::uint8_t>(i);
    hashes.push_back(tlv(0x04, hash));
    expected.push_back(typedHash(hash));
  }
  std::reverse(hashes.begin(), hashes.end());
  const auto maximumBlob = policy(requirements(hashes));
  check(maximumBlob.size() < maxLocalInputLibraryConstraintBytes, "1024 hashes fit explicit byte budget");
  const auto maximum = parseLocalInputLibraryConstraint(maximumBlob);
  check(
      maximum && maximum->cdHashes == expected && maximum->teamIdentifier == team,
      "exact 1024-element bound accepted and sorted without losses"
  );
  hashes.push_back(tlv(0x04, hashA));
  rejected(policy(requirements(hashes)), "1025 hashes denied even within byte limit");
  failNextAllocation = true;
  const auto failed = parseLocalInputLibraryConstraint(authentic);
  check(!failed && !failNextAllocation, "allocation failure returns denial, never partial facts or exception");
  check(
      parseLocalInputLibraryConstraint(authentic).has_value(), "allocation failure does not change future parser state"
  );
}

void testIndependentByteMutations()
{
  // Structural bytes are fixed by the schema; data bytes are not opaque policy
  // identity. Independently mutate every byte to every other value, requiring
  // semantic acceptance only for a binary hash or a valid ASCII Team change.
  const auto hashAt = std::search(authentic.begin(), authentic.end(), hashA.begin(), hashA.end());
  const auto teamAt = std::search(authentic.begin(), authentic.end(), team.begin(), team.end());
  check(hashAt != authentic.end() && teamAt != authentic.end(), "known fixture payload offsets");
  const auto hashOffset = static_cast<std::size_t>(hashAt - authentic.begin());
  const auto teamOffset = static_cast<std::size_t>(teamAt - authentic.begin());
  for (std::size_t at = 0; at < authentic.size(); ++at) {
    for (unsigned value = 0; value <= 255; ++value) {
      if (value == authentic[at])
        continue;
      auto changed = authentic;
      changed[at] = static_cast<std::uint8_t>(value);
      const auto result = parseLocalInputLibraryConstraint(changed);
      const bool hashByte = at >= hashOffset && at < hashOffset + 20;
      const bool teamByte = at >= teamOffset && at < teamOffset + 10;
      const bool validTeamByte = (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9');
      check(
          result.has_value() == (hashByte || (teamByte && validTeamByte)),
          "every one-byte change is classified by semantics, not opaque identity"
      );
      if (result) {
        auto expectedHash = typedHash(hashA);
        std::string expectedTeam(team);
        if (hashByte)
          expectedHash[at - hashOffset] = static_cast<std::uint8_t>(value);
        if (teamByte)
          expectedTeam[at - teamOffset] = static_cast<char>(value);
        check(
            result->teamIdentifier == expectedTeam &&
                result->cdHashes == std::vector<LocalInputLibraryCDHash>{expectedHash},
            "accepted mutation changes the corresponding typed fact exactly"
        );
      }
    }
  }
}
} // namespace

int main()
{
  try {
    testAuthenticBlobs();
    testEnvelopeAndTruncation();
    testEncodingStrictness();
    testExactPredicateSet();
    testTeamAndHashValues();
    testBoundsAndAllocation();
    testIndependentByteMutations();
    std::cout << "LocalInputLibraryConstraint: all checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "LocalInputLibraryConstraint: " << error.what() << '\n';
    return 1;
  }
}
