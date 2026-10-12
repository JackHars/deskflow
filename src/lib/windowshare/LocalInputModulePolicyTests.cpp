/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputModulePolicy.h"

#include <iostream>
#include <stdexcept>

using namespace deskflow::windowshare;
namespace {
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
LocalInputModuleFacts validFacts()
{
  LocalInputModuleFacts result;
  result.exactIdentityValidated = true;
  result.signatureFlags = localInputModulePolicyBits::hardenedRuntime | localInputModulePolicyBits::libraryValidation;
  result.dynamicStatus = localInputModulePolicyBits::dynamicallyValid;
  result.teamIdentifier = "AB12CD34E5";
  result.appleIssuedSigner = true;
  result.entitlements = LocalInputEntitlements::Absent;
  return result;
}
void expect(const LocalInputModuleFacts &facts, LocalInputModulePolicyFailure expected)
{
  const auto actual = evaluateLocalInputModulePolicy(facts, "AB12CD34E5");
  if (actual != expected)
    throw std::runtime_error(localInputModulePolicyFailureText(actual));
  check(*localInputModulePolicyFailureText(actual), "Policy outcome has no useful diagnostic");
}
} // namespace

int main()
{
  try {
    using enum LocalInputModulePolicyFailure;
    using namespace localInputModulePolicyBits;
    expect({}, MissingFacts);
    const auto valid = validFacts();
    expect(valid, None);
    auto facts = valid;
    facts.entitlements = LocalInputEntitlements::EmptyDictionary;
    expect(facts, None);
    for (const auto entitlements :
         {LocalInputEntitlements::Uninspected, LocalInputEntitlements::Malformed,
          LocalInputEntitlements::NonemptyDictionary, static_cast<LocalInputEntitlements>(99)}) {
      facts = valid;
      facts.entitlements = entitlements;
      check(evaluateLocalInputModulePolicy(facts, "AB12CD34E5") != None, "Unknown or nonempty entitlements accepted");
    }
    facts = valid;
    facts.exactIdentityValidated.reset();
    expect(facts, MissingFacts);
    facts.exactIdentityValidated = false;
    expect(facts, UnverifiedCodeIdentity);
    facts = valid;
    facts.signatureFlags.reset();
    expect(facts, MissingFacts);
    facts = valid;
    facts.dynamicStatus.reset();
    expect(facts, MissingFacts);
    facts = valid;
    facts.teamIdentifier.reset();
    expect(facts, MissingFacts);
    facts = valid;
    facts.appleIssuedSigner.reset();
    expect(facts, MissingFacts);
    facts.appleIssuedSigner = false;
    expect(facts, NonAppleSigner); // Matching claimed Team text is insufficient.
    for (const auto unsignedFlag : {adHoc, linkerSigned, adHoc | linkerSigned}) {
      facts = valid;
      *facts.signatureFlags |= unsignedFlag;
      expect(facts, UnidentifiedSignature); // Runtime bits cannot upgrade ad-hoc signing.
    }
    facts = valid;
    facts.signatureFlags = libraryValidation;
    expect(facts, MissingHardenedRuntime);
    facts.signatureFlags = hardenedRuntime;
    expect(facts, MissingExplicitLibraryValidation); // Default LV is not the explicit policy.
    facts = valid;
    facts.dynamicStatus = 0;
    expect(facts, DynamicallyInvalid);
    facts.dynamicStatus = dynamicallyValid | debugged;
    expect(facts, Debugged);
    facts.dynamicStatus = debugged;
    expect(facts, DynamicallyInvalid);
    facts = valid;
    *facts.signatureFlags |= 0x0100 | 0x0200; // Additional hard/kill flags are harmless.
    *facts.dynamicStatus |= 0x0100 | 0x0200;
    expect(facts, None);
    for (const auto &team :
         {std::string{}, std::string("SHORT"), std::string("AB12CD34E56"), std::string("ab12cd34e5"),
          std::string("AB12CD34E\""), std::string("AB12CD34E\0", 10), std::string("AB12CD34\xc3\x89")}) {
      check(!validLocalInputModuleTeamIdentifier(team), "Malformed Team identifier passed the grammar");
      facts = valid;
      facts.teamIdentifier = team;
      expect(facts, InvalidTeamIdentifier);
      check(
          evaluateLocalInputModulePolicy(valid, team) == InvalidTeamIdentifier,
          "Malformed anchor Team identifier was accepted"
      );
    }
    facts = valid;
    facts.teamIdentifier = "ZY98XW76V5";
    expect(facts, TeamMismatch);
    check(
        evaluateLocalInputModulePolicy(valid, "ZY98XW76V5") == TeamMismatch, "Expected anchored Team was not enforced"
    );
    check(
        *localInputModulePolicyFailureText(static_cast<LocalInputModulePolicyFailure>(999)),
        "Unknown failure has no diagnostic"
    );
    std::cout
        << "PASS: restricted-library policy facts, signer binding, required flags, entitlements and dynamic status\n";
    return 0;
  } catch (const std::exception &failure) {
    std::cerr << "FAIL: " << failure.what() << '\n';
    return 1;
  }
}
