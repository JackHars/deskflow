/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deskflow::windowshare {

// Only documented Security.framework bits are interpreted. The macOS reader
// checks these values against the SDK constants; unknown status bits are not
// treated as additional guarantees or used as private kernel API contracts.
namespace localInputModulePolicyBits {
inline constexpr std::uint32_t adHoc = 0x0002;
inline constexpr std::uint32_t libraryValidation = 0x2000;
inline constexpr std::uint32_t hardenedRuntime = 0x10000;
inline constexpr std::uint32_t linkerSigned = 0x20000;
inline constexpr std::uint32_t dynamicallyValid = 0x00000001;
inline constexpr std::uint32_t debugged = 0x10000000;
} // namespace localInputModulePolicyBits

enum class LocalInputEntitlements
{
  Uninspected,
  Absent,
  EmptyDictionary,
  NonemptyDictionary,
  Malformed,
};

// Inputs to a pure policy calculation, NOT authenticated evidence. Only the
// PeerTrust reader may turn OS observations tied to its existing live exact
// process/code pins into a restricted-library-admission snapshot. In particular,
// a caller asserting these facts does not establish any code or process identity.
struct LocalInputModuleFacts
{
  std::optional<bool> exactIdentityValidated;
  std::optional<std::uint32_t> signatureFlags;
  std::optional<std::uint32_t> dynamicStatus;
  std::optional<std::string> teamIdentifier;
  // Successful `anchor apple generic and certificate leaf[subject.OU] = TEAM`
  // requirement evaluation, not merely a Team ID string or certificate name.
  std::optional<bool> appleIssuedSigner;
  LocalInputEntitlements entitlements = LocalInputEntitlements::Uninspected;
};

enum class LocalInputModulePolicyFailure
{
  None,
  MissingFacts,
  UnverifiedCodeIdentity,
  UnidentifiedSignature,
  MissingHardenedRuntime,
  MissingExplicitLibraryValidation,
  DynamicallyInvalid,
  Debugged,
  InvalidTeamIdentifier,
  TeamMismatch,
  NonAppleSigner,
  UninspectedEntitlements,
  MalformedEntitlements,
  NonemptyEntitlements,
};

bool validLocalInputModuleTeamIdentifier(std::string_view team) noexcept;
LocalInputModulePolicyFailure
evaluateLocalInputModulePolicy(const LocalInputModuleFacts &facts, std::string_view anchoredTeam) noexcept;
const char *localInputModulePolicyFailureText(LocalInputModulePolicyFailure failure) noexcept;

// Success establishes only the requested Apple-or-same-Team library admission
// policy, assuming trustworthy OS observations. It does not identify the exact
// images already loaded or permitted to load, attest process memory, authorize
// original input, or satisfy LocalInputPreflight::Checks::codeAdmission.

} // namespace deskflow::windowshare
