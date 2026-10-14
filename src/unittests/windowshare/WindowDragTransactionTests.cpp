/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WindowDragTransaction.h"
#include <cstdlib>
#include <iostream>

using namespace deskflow::windowshare;
using namespace std::chrono_literals;
namespace {
void check(bool result, const char *message)
{
  if (!result) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
const WindowDragTransaction::Clock::time_point start{};
WindowDragBinding binding{7, 9, 11, 3, 12, 1000, 10383, 516};
WindowDragRelease release{3, 426, 3000, 10383, 516};
void painted(WindowDragTransaction &drag, std::uint64_t geometry = 1)
{
  check(drag.produced(geometry, start + 10ms), "frame produced");
  check(drag.painted(geometry, start + 20ms), "frame painted");
}
void ordinaryDrag()
{
  for (const auto direction : {WindowDragDirection::Export, WindowDragDirection::Return}) {
    auto b = binding;
    b.direction = direction;
    WindowDragTransaction drag;
    check(drag.begin(b, 1, start), "begin one native drag");
    painted(drag);
    check(!drag.reserveCommit(b, start + 25ms) && drag.held(), "painted preview cannot commit a held press");
    check(drag.release(b, release, start + 30ms), "only real original release permits drop");
    check(drag.reserveCommit(b, start + 31ms), "release and painted frame reserve drop");
    check(!drag.reserveCommit(b, start + 32ms), "duplicate callbacks cannot move twice");
    check(drag.committed(b, start + 33ms), "complete exact reservation");
    check(!drag.committed(b, start + 34ms) && !drag.release(b, release, start + 35ms), "late frames and ups are inert");
    check(!drag.begin(b, 1, start + 40ms), "transaction cannot be recycled");
  }
}
void lateFrameAndGeometry()
{
  WindowDragTransaction drag;
  check(drag.begin(binding, 1, start), "begin late-frame drag");
  check(drag.release(binding, release, start + 30ms), "release may precede the first frame");
  check(!drag.painted(1, start + 40ms), "peer cannot acknowledge a frame never produced");
  check(!drag.reserveCommit(binding, start + 40ms), "late frame keeps source visible");
  check(drag.produced(1, start + 50ms) && drag.painted(1, start + 60ms), "late first frame accepted within budget");
  check(drag.geometryChanged(2, start + 70ms), "source moved before native mutation");
  check(!drag.reserveCommit(binding, start + 80ms), "old painted frame cannot hide moved source");
  check(
      !drag.geometryChanged(1, start + 80ms) && !drag.painted(1, start + 80ms), "stale metadata cannot revert geometry"
  );
  check(drag.produced(2, start + 90ms) && drag.painted(2, start + 100ms), "current source frame painted");
  check(drag.reserveCommit(binding, start + 110ms), "drop uses current geometry");
}
void identityFailures()
{
  for (int field = 0; field < 9; ++field) {
    WindowDragTransaction drag;
    check(drag.begin(binding, 1, start), "begin identity fixture");
    painted(drag);
    check(drag.release(binding, release, start + 30ms), "record original up");
    auto changed = binding;
    switch (field) {
    case 0:
      ++changed.drag;
      break;
    case 1:
      ++changed.connection;
      break;
    case 2:
      ++changed.topology;
      break;
    case 3:
      ++changed.captureEpoch;
      break;
    case 4:
      ++changed.pressSequence;
      break;
    case 5:
      ++changed.pressTime;
      break;
    case 6:
      ++changed.window;
      break;
    case 7:
      ++changed.eventNumber;
      break;
    case 8:
      changed.direction = WindowDragDirection::Return;
      break;
    }
    check(!drag.reserveCommit(changed, start + 40ms), "identity change refuses native mutation");
    check(!drag.reserveCommit(binding, start + 50ms), "old identity cannot revive cancellation");
  }
  for (int field = 0; field < 5; ++field) {
    WindowDragTransaction drag;
    check(drag.begin(binding, 1, start), "begin release fixture");
    painted(drag);
    auto wrong = release;
    switch (field) {
    case 0:
      ++wrong.captureEpoch;
      break;
    case 1:
      wrong.sequence = binding.pressSequence;
      break;
    case 2:
      wrong.sourceTime = binding.pressTime;
      break;
    case 3:
      ++wrong.window;
      break;
    case 4:
      ++wrong.eventNumber;
      break;
    }
    check(!drag.release(binding, wrong, start + 30ms), "foreign, stale or replayed release refused");
    check(!drag.release(binding, release, start + 40ms), "later valid release cannot revive failed drop");
  }
}
void faultsAtEveryTransition()
{
  // Disconnect/layout/source loss/cancel all take this path. Cover each
  // asynchronous boundary, especially cancellation inside the native commit.
  for (int stage = 0; stage < 5; ++stage) {
    WindowDragTransaction drag;
    check(drag.begin(binding, 1, start), "begin cancellation fixture");
    if (stage >= 1)
      check(drag.produced(1, start + 10ms), "produce before cancellation");
    if (stage >= 2)
      check(drag.painted(1, start + 20ms), "paint before cancellation");
    if (stage >= 3)
      check(drag.release(binding, release, start + 30ms), "release before cancellation");
    if (stage >= 4)
      check(drag.reserveCommit(binding, start + 40ms), "reserve before callback");
    drag.cancel();
    check(
        !drag.current(binding, start + 50ms) && !drag.committing(binding, start + 50ms),
        "borrowed callback loses authority"
    );
    check(
        !drag.painted(1, start + 50ms) && !drag.release(binding, release, start + 50ms) &&
            !drag.reserveCommit(binding, start + 50ms) && !drag.committed(binding, start + 50ms),
        "all late callbacks stay inert"
    );
  }
}
void absoluteDeadlines()
{
  WindowDragTransaction drag;
  check(drag.begin(binding, 1, start), "begin timeout fixture");
  painted(drag);
  check(drag.release(binding, release, start + 1s), "drop starts short absolute deadline");
  check(!drag.release(binding, release, start + 2s), "duplicate release cannot extend deadline");
  check(drag.painted(1, start + 2499ms), "last on-time frame");
  check(!drag.reserveCommit(binding, start + 2500ms), "deadline is exclusive and frames do not renew it");
  WindowDragTransaction held;
  check(held.begin(binding, 1, start), "begin long held fixture");
  painted(held);
  check(held.release(binding, release, start + 29999ms), "release just before drag timeout");
  check(!held.reserveCommit(binding, start + 30s), "drop cannot extend original drag budget");
  WindowDragTransaction nested;
  check(nested.begin(binding, 1, start), "begin callback timeout");
  painted(nested);
  check(
      nested.release(binding, release, start + 1s) && nested.reserveCommit(binding, start + 2s),
      "reserve before slow callback"
  );
  check(!nested.committed(binding, start + 2500ms), "slow native callback cannot complete stale reservation");
}
} // namespace
int main()
{
  ordinaryDrag();
  lateFrameAndGeometry();
  identityFailures();
  faultsAtEveryTransition();
  absoluteDeadlines();
  std::cout << "Window drag transaction tests passed\n";
}
