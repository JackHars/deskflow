/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputModulePolicy.h"

#include <algorithm>

namespace deskflow::windowshare {

bool validLocalInputModuleTeamIdentifier(std::string_view team) noexcept
{
  // Apple-issued Team IDs are ten ASCII letters/digits. Restricting the grammar
  // also prevents requirement-language interpolation from accepting syntax.
  return team.size() == 10 && std::all_of(team.begin(), team.end(), [](unsigned char value) {
           return (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9');
         });
}

LocalInputModulePolicyFailure
evaluateLocalInputModulePolicy(const LocalInputModuleFacts &facts, std::string_view anchoredTeam) noexcept
{
  using enum LocalInputModulePolicyFailure;
  using namespace localInputModulePolicyBits;
  if (!facts.exactIdentityValidated || !facts.signatureFlags || !facts.dynamicStatus)
    return MissingFacts;
  if (!*facts.exactIdentityValidated)
    return UnverifiedCodeIdentity;
  if (*facts.signatureFlags & (adHoc | linkerSigned))
    return UnidentifiedSignature;
  if (!(*facts.signatureFlags & hardenedRuntime))
    return MissingHardenedRuntime;
  if (!(*facts.signatureFlags & libraryValidation))
    return MissingExplicitLibraryValidation;
  if (!(*facts.dynamicStatus & dynamicallyValid))
    return DynamicallyInvalid;
  if (*facts.dynamicStatus & debugged)
    return Debugged;
  if (!facts.teamIdentifier || !facts.appleIssuedSigner)
    return MissingFacts;
  if (!validLocalInputModuleTeamIdentifier(anchoredTeam) || !validLocalInputModuleTeamIdentifier(*facts.teamIdentifier))
    return InvalidTeamIdentifier;
  if (*facts.teamIdentifier != anchoredTeam)
    return TeamMismatch;
  if (!*facts.appleIssuedSigner)
    return NonAppleSigner;
  switch (facts.entitlements) {
  case LocalInputEntitlements::Absent:
  case LocalInputEntitlements::EmptyDictionary:
    return None;
  case LocalInputEntitlements::NonemptyDictionary:
    return NonemptyEntitlements;
  case LocalInputEntitlements::Malformed:
    return MalformedEntitlements;
  case LocalInputEntitlements::Uninspected:
  default:
    return UninspectedEntitlements;
  }
}

const char *localInputModulePolicyFailureText(LocalInputModulePolicyFailure failure) noexcept
{
  switch (failure) {
  case LocalInputModulePolicyFailure::None:
    return "Restricted Apple-or-same-Team library admission observed";
  case LocalInputModulePolicyFailure::MissingFacts:
    return "Missing restricted-library-admission facts";
  case LocalInputModulePolicyFailure::UnverifiedCodeIdentity:
    return "Exact live code identity was not verified";
  case LocalInputModulePolicyFailure::UnidentifiedSignature:
    return "Ad-hoc or linker-only signing is ineligible for restricted library admission";
  case LocalInputModulePolicyFailure::MissingHardenedRuntime:
    return "Hardened runtime is not enabled";
  case LocalInputModulePolicyFailure::MissingExplicitLibraryValidation:
    return "Explicit library validation is not enabled";
  case LocalInputModulePolicyFailure::DynamicallyInvalid:
    return "Running code is dynamically invalid";
  case LocalInputModulePolicyFailure::Debugged:
    return "Running code has been debugged";
  case LocalInputModulePolicyFailure::InvalidTeamIdentifier:
    return "Invalid Apple Team identifier";
  case LocalInputModulePolicyFailure::TeamMismatch:
    return "Running code does not belong to the anchored bundle Team";
  case LocalInputModulePolicyFailure::NonAppleSigner:
    return "The matching Team identity is not established by an Apple-issued signing requirement";
  case LocalInputModulePolicyFailure::UninspectedEntitlements:
    return "Entitlements were not inspected";
  case LocalInputModulePolicyFailure::MalformedEntitlements:
    return "Entitlement metadata is malformed or unsupported";
  case LocalInputModulePolicyFailure::NonemptyEntitlements:
    return "Restricted library admission requires absent or empty entitlements";
  }
  return "Unknown restricted-library-admission policy failure";
}

} // namespace deskflow::windowshare
