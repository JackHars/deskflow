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
    std::cout << "PASS: switch/repeat ownership, broadcast snapshots, local pass-through, unknown ups, "
                 "disconnect/reconnect tombstones, paired modifiers, input namespaces, bounded state\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
