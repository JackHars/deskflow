// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#include "../../lib/windowshare/WindowSession.h"
#include "../../lib/windowshare/InputOwnership.h"
#include <cassert>

using namespace deskflow::windowshare;

int main()
{
  WindowSession session({1, 42, 73, 5});
  assert(!session.beginTransfer());
  assert(session.prepareExport({10, 20, 800, 600, 1}));
  assert(!session.acknowledgeDestination(4));
  assert(!session.beginTransfer());
  assert(session.acknowledgeDestination(5));
  assert(session.beginTransfer());
  assert(session.commitExport());
  assert(session.prepareReturn());
  assert(session.commitReturn());
  assert(session.phase() == Phase::Local);

  InputOwnership input;
  assert(input.setFocus({InputTarget::Ubuntu, 0, 1}));
  assert(input.press(30).has_value());
  assert(input.setFocus({InputTarget::RemoteMacWindow, 1, 2}));
  const auto released = input.release(30);
  assert(released && released->target == InputTarget::Ubuntu);
  assert(!input.release(30));
  assert(input.press(31));
  assert(input.drain().size() == 1);
  return 0;
}
