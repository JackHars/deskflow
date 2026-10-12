/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "InputRouteState.h"

#include <iostream>
#include <stdexcept>

using Routes = deskflow::windowshare::InputRouteState;

namespace {
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
void expect(const std::optional<Routes::Press> &press, Routes::Recipients recipients, const char *message)
{
  check(press && press->recipients == recipients, message);
}

void testComputerSwitchAndBroadcastSnapshot()
{
  Routes routes;
  const auto first = routes.attach(), second = routes.attach(), third = routes.attach();
  const auto key = Routes::keyControl('A', 31);
  expect(routes.down(key, 'A', 0, 31, {first}), {first}, "Initial key ownership was not captured");
  // Changing focus and broadcast rules may propose different recipients, but
  // an already-held key, including a misreported repeat/down, keeps its owner.
  expect(routes.down(key, 'A', 0, 31, {second}), {first}, "Repeated down migrated to new focus");
  expect(routes.repeat(key), {first}, "Repeat did not follow original down");
  expect(routes.up(Routes::keyControl(0, 31)), {first}, "Symbol-free macOS up lost physical-key ownership");
  check(!routes.up(key), "Duplicate up acquired a recipient");
  const auto broadcast = Routes::keyControl('B', 32);
  expect(
      routes.down(broadcast, 'B', 0, 32, {second, first, second, 0}), {first, second},
      "Broadcast recipients were not normalized"
  );
  expect(routes.down(broadcast, 'B', 0, 32, {third}), {first, second}, "Broadcast rule change retargeted held key");
  expect(routes.repeat(broadcast), {first, second}, "Broadcast repeat lost one press recipient");
  expect(routes.up(broadcast), {first, second}, "Broadcast up used new destinations");
}

void testLocalPassThroughAndUnknownUps()
{
  Routes routes;
  const auto remote = routes.attach();
  const auto local = Routes::keyControl('L', 41);
  expect(routes.down(local, 'L', 0, 41, {}), {}, "Local pass-through was not recorded");
  expect(routes.down(local, 'L', 0, 41, {remote}), {}, "Locally held input was replayed on entry");
  expect(routes.repeat(local), {}, "Locally held repeat leaked to remote");
  expect(routes.up(local), {}, "Local key-up leaked to remote");
  check(!routes.up(Routes::keyControl('U', 99)), "Unobserved key-up acquired new focus");
  check(!routes.repeat(Routes::keyControl('U', 99)), "Unobserved repeat acquired new focus");
  const Routes::Control mouse{Routes::Kind::Button, 1};
  expect(routes.down(mouse, 0, 0, 1, {}), {}, "Local button was not pass-through");
  expect(routes.up(mouse), {}, "Local mouse-up leaked to remote");
}

void testDisconnectAndReusedClientName()
{
  Routes routes;
  const auto lost = routes.attach(), other = routes.attach();
  const auto key = Routes::keyControl('D', 42);
  const Routes::Control mouse{Routes::Kind::Button, 42};
  routes.down(key, 'D', 2, 42, {lost, other});
  routes.down(mouse, 0, 0, 42, {lost});
  routes.observeModifiers(lost, 2);
  auto detached = routes.detach(lost);
  check(detached.presses.size() == 2 && detached.modifiers == 2, "Disconnect did not drain owned input");
  for (const auto &press : detached.presses)
    check(press.recipients == Routes::Recipients{lost}, "Disconnect cleanup addressed an unrelated recipient");
  // A new connection may reuse the same name and even allocator address. Its
  // opaque incarnation must still never inherit the old physical presses.
  const auto replacement = routes.attach();
  check(replacement != lost, "Connection incarnation was reused");
  expect(routes.repeat(key), {other}, "Disconnected broadcast member was not pruned");
  expect(routes.repeat(mouse), {}, "Disconnected mouse owner was not tombstoned");
  expect(routes.down(mouse, 0, 0, 42, {replacement}), {}, "Held button migrated after reconnect");
  expect(routes.up(mouse), {}, "Old mouse release addressed replacement connection");
  expect(routes.up(key), {other}, "Disconnect lost another broadcast member's release");
  expect(routes.down(mouse, 0, 0, 42, {replacement}), {replacement}, "Fresh press after physical up stayed tombstoned");
  check(routes.detach(lost).presses.empty(), "Repeated disconnect produced duplicate releases");
}

void testModifierCleanupIsPaired()
{
  Routes routes;
  const auto observed = routes.attach(), unrelated = routes.attach();
  routes.observeModifiers(observed, 1 | 2); // desired Shift+Control on a forwarded key
  auto released = routes.releaseModifiers(1);
  check(
      released.size() == 1 && released[0].recipient == observed && released[0].mask == 1,
      "Local-owned modifier release did not clear its paired remote mask"
  );
  check(released[0].recipient != unrelated, "Modifier release used current unrelated client");
  check(routes.releaseModifiers(1).empty(), "Modifier release was duplicated");
  routes.forgetModifiers(observed, 2);
  check(routes.releaseModifiers(2).empty(), "Already-cleared modifier remained owned");
  routes.observeModifiers(observed, 1);
  routes.detach(observed);
  check(routes.releaseModifiers(1).empty(), "Modifier cleanup retained a dead connection");
  routes.observeModifiers(observed, 1);
  check(routes.releaseModifiers(1).empty(), "Detached recipient accepted new modifier ownership");
}

void testCommandAndPhysicalNamespaces()
{
  Routes routes;
  const auto first = routes.attach(), second = routes.attach();
  const Routes::Control command{Routes::Kind::ModifierCommand, 1};
  const Routes::Control button{Routes::Kind::Button, 1};
  const auto physical = Routes::keyControl('P', 1);
  routes.down(command, 0xeeff, 2, 0, {first});
  routes.down(button, 0, 0, 1, {second});
  routes.down(physical, 'P', 0, 1, {first, second});
  expect(routes.up(command), {first}, "Modifier Clear command did not return to Set recipient");
  expect(routes.up(button), {second}, "Button ownership collided with key or modifier command");
  expect(routes.up(physical), {first, second}, "Physical key ownership collided with button");
  const auto mediaA = Routes::keyControl(0xef01, 0), mediaB = Routes::keyControl(0xef02, 0);
  routes.down(mediaA, 0xef01, 0, 0, {first});
  routes.down(mediaB, 0xef02, 0, 0, {second});
  expect(routes.up(mediaA), {first}, "Buttonless media keys collided");
  expect(routes.up(mediaB), {second}, "Second buttonless media key lost ownership");
}

void testBoundedOrphans()
{
  Routes routes;
  const auto old = routes.attach();
  for (std::size_t index = 0; index < Routes::maxPresses; ++index)
    check(routes.down({Routes::Kind::Key, index}, 65, 0, 1, {old}).has_value(), "Valid bounded press rejected");
  check(!routes.down({Routes::Kind::Key, Routes::maxPresses}, 66, 0, 2, {old}), "Unbounded press allocation accepted");
  const auto detached = routes.detach(old);
  check(
      detached.presses.size() == Routes::maxPresses && routes.size() == Routes::maxPresses,
      "Disconnect lost physical-up tombstones"
  );
  for (std::size_t index = 0; index < Routes::maxPresses; ++index) {
    expect(routes.up({Routes::Kind::Key, index}), {}, "Orphan key-up was routed");
  }
  check(routes.size() == 0, "Physical release did not reclaim orphan capacity");
}

void testOriginalAndLegacyPressesCannotExchangeOwners()
{
  Routes routes;
  const auto remote = routes.attach();
  const auto key = Routes::keyControl('A', 31);
  const auto original = routes.downOriginal(key, 'A', 2, 31, 17);
  check(original && original->originalOwner == 17 && original->recipients.empty(), "Original press acquired Linux");
  check(routes.down(key, 'A', 2, 31, {remote})->originalOwner == 17, "Original press migrated to legacy dispatch");
  check(routes.downOriginal(key, 'A', 2, 31, 18)->originalOwner == 17, "Original press migrated to a new owner");
  check(routes.repeat(key)->originalOwner == 17, "Original repeat lost its press owner");
  check(routes.up(Routes::keyControl(0, 31))->originalOwner == 17, "Original up lost its physical key owner");
  check(!routes.up(key), "Duplicate original up acquired an owner");
  expect(routes.down(key, 'A', 0, 31, {remote}), {remote}, "A fresh legacy down retained old original ownership");
  const auto legacy = routes.downOriginal(key, 'A', 0, 31, 19);
  check(
      legacy && !legacy->originalOwner && legacy->recipients == Routes::Recipients{remote},
      "Legacy-held press migrated to original delivery"
  );
  routes.up(key);
  routes.down(key, 'A', 0, 31, {});
  const auto local = routes.downOriginal(key, 'A', 0, 31, 20);
  check(local && !local->originalOwner && local->recipients.empty(), "Local-held press acquired original ownership");
  check(!routes.downOriginal({Routes::Kind::Button, 2}, 0, 0, 2, 0), "Zero original owner was admitted");
  check(
      !routes.downOriginal({Routes::Kind::ModifierCommand, 1}, 0xeeff, 2, 0, 21),
      "Configured modifier command acquired a physical original owner"
  );
  check(routes.originalOwners().empty(), "Nonoriginal presses appeared in original ownership inventory");
}

void testOriginalRevocationRetainsPhysicalTombstones()
{
  Routes routes;
  const auto remote = routes.attach();
  const auto key = Routes::keyControl('A', 31), other = Routes::keyControl('B', 32),
             legacy = Routes::keyControl('L', 33);
  const Routes::Control button{Routes::Kind::Button, 1};
  routes.downOriginal(key, 'A', 2, 31, 17);
  routes.downOriginal(button, 0, 0, 1, 17);
  routes.downOriginal(other, 'B', 0, 32, 18);
  routes.down(legacy, 'L', 3, 33, {remote});
  routes.observeModifiers(remote, 3);
  check(
      routes.originalOwners() == std::set<std::uint64_t>{17, 18},
      "Original ownership inventory duplicated or lost an owner"
  );
  const auto released = routes.revokeOriginal(17);
  check(released.size() == 2 && routes.size() == 4, "Original revocation lost a physical-up tombstone");
  for (const auto &press : released)
    check(press.originalOwner == 17 && press.recipients.empty(), "Original cleanup named an unrelated owner");
  check(
      routes.revokeOriginal(17).empty() && routes.revokeOriginal(0).empty(), "Repeated revocation duplicated cleanup"
  );
  check(routes.originalOwners() == std::set<std::uint64_t>{18}, "Revocation lost another original owner");
  for (auto control : {key, button}) {
    const auto tombstone = routes.repeat(control);
    check(
        tombstone && !tombstone->originalOwner && tombstone->recipients.empty(), "Revoked press remained deliverable"
    );
    expect(routes.down(control, 0, 0, 1, {remote}), {}, "Revoked original press migrated to Linux");
    check(!routes.downOriginal(control, 0, 0, 1, 19)->originalOwner, "Revoked press reacquired original delivery");
    expect(routes.up(control), {}, "Revoked release leaked to legacy focus");
  }
  check(
      routes.downOriginal(key, 'A', 0, 31, 19)->originalOwner == 19,
      "Fresh physical press could not acquire original owner"
  );
  const auto detached = routes.detach(remote);
  check(detached.presses.size() == 1 && detached.modifiers == 3, "Original revocation changed paired legacy cleanup");
  check(
      routes.repeat(other)->originalOwner == 18 && routes.repeat(key)->originalOwner == 19,
      "Legacy disconnect revoked unrelated original owners"
  );
  routes.up(other);
  routes.up(key);
  check(routes.originalOwners().empty(), "Physical releases left original owners active");
}

void testOriginalAndLegacyShareBoundedCapacity()
{
  Routes routes;
  const auto remote = routes.attach();
  for (std::size_t index = 0; index < Routes::maxPresses; ++index) {
    const Routes::Control control{Routes::Kind::Key, index};
    check(
        (index % 2 ? routes.downOriginal(control, 65, 0, 1, 23) : routes.down(control, 65, 0, 1, {remote})).has_value(),
        "Mixed bounded press rejected"
    );
  }
  check(
      !routes.downOriginal({Routes::Kind::Key, Routes::maxPresses}, 65, 0, 1, 23),
      "Original press exceeded shared capacity"
  );
  check(
      !routes.down({Routes::Kind::Key, Routes::maxPresses}, 65, 0, 1, {remote}), "Legacy press exceeded shared capacity"
  );
  check(
      routes.downOriginal({Routes::Kind::Key, 1}, 65, 0, 1, 24)->originalOwner == 23,
      "Full ledger replaced an existing original owner"
  );
  check(
      routes.revokeOriginal(23).size() == Routes::maxPresses / 2 && routes.size() == Routes::maxPresses,
      "Revocation reclaimed capacity before physical releases"
  );
  for (std::size_t index = 0; index < Routes::maxPresses; ++index)
    routes.up({Routes::Kind::Key, index});
  check(
      routes.size() == 0 && routes.downOriginal({Routes::Kind::Button, 1}, 0, 0, 1, 25).has_value(),
      "Mixed physical releases did not reclaim shared capacity"
  );
}

void testPressIdentitySurvivesRevocationAndChangesOnReuse()
{
  Routes routes;
  for (auto control : {Routes::keyControl('A', 31), Routes::Control{Routes::Kind::Button, 1}}) {
    const auto remote = routes.attach();
    const auto original = routes.downOriginal(control, 'A', 2, 31, 17);
    check(original && original->incarnation, "Original press has no retained identity");
    const auto first = original->incarnation;
    check(routes.down(control, 'B', 0, 31, {remote})->incarnation == first, "Repeated down replaced press identity");
    check(routes.repeat(control)->incarnation == first, "Repeat replaced original press identity");
    const auto cleanup = routes.revokeOriginal(17);
    check(cleanup.size() == 1 && cleanup.front().incarnation == first, "Revocation lost cleanup press identity");
    check(routes.repeat(control)->incarnation == first, "Revocation replaced tombstone identity");
    check(routes.up(control)->incarnation == first, "Physical release lost exact original identity");
    const auto legacy = routes.down(control, 'A', 0, 31, {remote});
    check(legacy && legacy->incarnation > first, "A fresh legacy press reused an old original identity");
    check(
        routes.downOriginal(control, 'A', 0, 31, 18)->incarnation == legacy->incarnation,
        "Denied original acquisition replaced legacy identity"
    );
    const auto detached = routes.detach(remote);
    check(
        detached.presses.size() == 1 && detached.presses.front().incarnation == legacy->incarnation,
        "Disconnect lost exact legacy cleanup identity"
    );
    check(routes.repeat(control)->incarnation == legacy->incarnation, "Disconnect replaced legacy tombstone identity");
    routes.up(control);
    const auto fresh = routes.downOriginal(control, 'A', 0, 31, 19);
    check(fresh && fresh->incarnation > legacy->incarnation, "Fresh original press reused retired legacy identity");
    routes.up(control);
  }
}
} // namespace

int main()
{
  try {
    testComputerSwitchAndBroadcastSnapshot();
    testLocalPassThroughAndUnknownUps();
    testDisconnectAndReusedClientName();
    testModifierCleanupIsPaired();
    testCommandAndPhysicalNamespaces();
    testBoundedOrphans();
    testOriginalAndLegacyPressesCannotExchangeOwners();
    testOriginalRevocationRetainsPhysicalTombstones();
    testOriginalAndLegacyShareBoundedCapacity();
    testPressIdentitySurvivesRevocationAndChangesOnReuse();
    std::cout << "PASS: switch/repeat ownership, broadcast snapshots, local pass-through, unknown ups, "
                 "disconnect/reconnect tombstones, paired modifiers, input namespaces, original/legacy separation, "
                 "exact original revocation, shared bounded state, immutable press identities\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
