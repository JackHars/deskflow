/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "windowshare/WindowDragEdgeJoin.h"
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace deskflow::windowshare;
using namespace std::chrono_literals;
namespace deskflow::windowshare {
struct WindowDragEdgeMailboxTestAccess
{
  static bool bind(WindowDragEdgeMailbox &box)
  {
    return box.bind(7);
  }
  static bool publish(
      WindowDragEdgeMailbox &box, WindowDragEdge edge,
      WindowDragEdgeMailbox::Clock::time_point now = WindowDragEdgeMailbox::Clock::now()
  )
  {
    return box.publish(std::move(edge), now);
  }
  static void clear(WindowDragEdgeMailbox &box)
  {
    box.clear();
  }
  static void exhaust(WindowDragEdgeMailbox &box)
  {
    box.m_generation = UINT64_MAX;
  }
};
} // namespace deskflow::windowshare
namespace {
using Access = WindowDragEdgeMailboxTestAccess;
void check(bool ok, const char *message)
{
  if (!ok)
    throw std::runtime_error(message);
}
struct Fixture
{
  OriginalInputStream stream;
  OriginalPointerJoin pointers{stream.epoch()};
  WindowDragEdgeJoin join;
  WindowDragEdgeMailbox mailbox;
  deskflow::AuthenticatedPeerIssuer issuer;
  OriginalInputCaptured down;
  OriginalPointerJoin::Match press;
  WindowDragEdge edge;
  Fixture()
  {
    check(Access::bind(mailbox), "one server binds mailbox");
    deskflow::AuthenticatedPeerTicket::Fingerprint pin{};
    pin.fill(0x31);
    const OriginalInputRecord record{stream.epoch().id(), 1, 100, 0, OriginalInputButton{0, true, 1}};
    check(stream.push(record), "source press");
    down = {
        *stream.pop(),
        {{record.epoch, 1, 0}, 1, false},
        {},
        OriginalPointerObservation{record.epoch, 1, 100, -1100, 110, 0, 0, 52, 0, 1, 3}
    };
    check(pointers.capture(down), "capture only once");
    check(pointers.annotate({100, 0, {0, true, 1}, -1100, 110, 812, 812, 52, 0, 1, 3}), "annotation joins");
    press = *pointers.pop();
    join.capture(down);
    join.pointer(press);
    const OriginalInputRecord moved{record.epoch, 2, 200, 0, OriginalInputMotion{5, 0, 0}};
    check(stream.push(moved), "source motion keeps capture sequence continuous");
    join.capture({*stream.pop(), {{record.epoch, 2, 0}, 1, false}, {}, {}});
    edge = {
        7,
        11,
        {record.epoch, 1, 1},
        {record.epoch, 2, 1},
        WindowDragEdgeSide::Right,
        {-1200, 0, 1200, 800},
        {100, -50, 1600, 900},
        -1,
        300,
        100,
        287,
        *issuer.issue(pin)
    };
  }
  WindowDragEdgeCandidate candidate()
  {
    check(Access::publish(mailbox, edge), "live configured boundary");
    const auto result = join.join(mailbox.read());
    check(result && result->live() && result->press.window() == 812, "exact local press joins configured boundary");
    return *result;
  }
  OriginalInputCaptured up()
  {
    const OriginalInputRecord record{stream.epoch().id(), 3, 300, 0, OriginalInputButton{0, false, 0}};
    check(stream.push(record), "real zero-count release");
    return {*stream.pop(), {{record.epoch, 3, 0}, 1, false}, {}, {}};
  }
};
void boundaries()
{
  Fixture f;
  const auto first = f.candidate();
  ++f.edge.motion.sequence;
  ++f.edge.destinationY;
  check(Access::publish(f.mailbox, f.edge), "later movement refreshes same boundary");
  const auto refresh = f.mailbox.read();
  check(refresh && refresh->generation() == first.boundary.generation(), "motion does not replace transaction");
  check(first.live(), "earlier observation retains its own short budget");
  check(!refresh->live(WindowDragEdgeMailbox::Clock::now() + 251ms), "stalled worker cannot renew retained data");
  f.edge.destination.width += 10;
  ++f.edge.motion.sequence;
  check(Access::publish(f.mailbox, f.edge), "changed layout starts a new boundary");
  check(!first.live() && !refresh->live(), "layout replacement revokes cached observations");
  const auto layout = f.mailbox.read();
  check(layout && layout->generation() > first.boundary.generation(), "new boundary never reuses generation");
  Access::clear(f.mailbox);
  check(!layout->live() && !f.mailbox.read(), "leaving edge revokes immediately");
  check(Access::publish(f.mailbox, f.edge), "return to edge gets fresh observation");
  const auto returned = f.mailbox.read();
  check(!Access::publish(f.mailbox, f.edge) && !returned->live(), "duplicate motion cannot renew freshness");
  ++f.edge.motion.sequence;
  check(Access::publish(f.mailbox, f.edge), "later motion after rejection");
  const auto beforeClose = f.mailbox.read();
  f.mailbox.close();
  check(!beforeClose->live() && !Access::publish(f.mailbox, f.edge), "shutdown is terminal");
}
void releaseAndRaces()
{
  for (int race = 0; race != 6; ++race) {
    Fixture f;
    const auto candidate = f.candidate();
    switch (race) {
    case 0:
      f.join.capture(f.up());
      break;
    case 1:
      f.issuer.revoke();
      break;
    case 2:
      f.pointers.revoke();
      break;
    case 3:
      f.stream.revoke(OriginalInputRevocation::Stopped);
      break;
    case 4:
      Access::clear(f.mailbox);
      break;
    case 5:
      f.join.revoke();
      break;
    }
    check(!candidate.live() && !f.join.join(f.mailbox.read()), "retained candidate dies independently of callbacks");
  }
  Fixture f;
  f.join.capture(f.up());
  f.join.pointer(f.press); // Down's annotation arrived after physical up.
  check(Access::publish(f.mailbox, f.edge), "worker can lag behind raw release");
  check(!f.join.join(f.mailbox.read()), "late annotated down cannot resurrect a released press");
}
void invalidIdentity()
{
  for (int field = 0; field != 9; ++field) {
    Fixture f;
    auto edge = f.edge;
    switch (field) {
    case 0:
      ++edge.press.epoch;
      break;
    case 1:
      edge.press.sequence = 2;
      edge.motion.sequence = 3;
      break;
    case 2:
      edge.press.part = 2;
      break;
    case 3:
      edge.motion.part = 0;
      break;
    case 4:
      edge.motion.sequence = 1;
      break;
    case 5:
      edge.server = 8;
      break;
    case 6:
      edge.destinationX = edge.destination.x - 1;
      break;
    case 7:
      edge.sourceX = 0;
      break;
    case 8:
      edge.peer = {};
      break;
    }
    Access::publish(f.mailbox, edge);
    check(!f.join.join(f.mailbox.read()), "malformed or different press cannot start drag");
  }
  for (int field = 0; field != 4; ++field) {
    Fixture f;
    f.join.revoke();
    auto press = f.press;
    switch (field) {
    case 0:
      press.captured.translation.uncertain = true;
      break;
    case 1:
      press.captured.translation.parts = 2;
      break;
    case 2:
      ++press.captured.translation.stamp.sequence;
      break;
    case 3:
      press.annotation->windowHandlingPointer = 0;
      break;
    }
    f.join.capture(press.captured);
    f.join.pointer(press);
    Access::publish(f.mailbox, f.edge);
    check(!f.join.join(f.mailbox.read()), "expanded or missing-identity press is not eligible");
  }
  Fixture exhausted;
  Access::exhaust(exhausted.mailbox);
  check(!Access::publish(exhausted.mailbox, exhausted.edge), "exhaustion cannot wrap to prior generation");
  check(!Access::publish(exhausted.mailbox, exhausted.edge), "exhaustion remains terminal");
}
void concurrentRevocation()
{
  Fixture f;
  const auto candidate = f.candidate();
  std::thread worker([&] {
    for (int i = 0; i != 500; ++i) {
      ++f.edge.motion.sequence;
      Access::publish(f.mailbox, f.edge);
    }
    f.mailbox.close();
  });
  for (int i = 0; i != 500; ++i)
    (void)f.join.join(f.mailbox.read());
  worker.join();
  check(!candidate.live() && !f.mailbox.read(), "worker shutdown invalidates reader copies");
}
} // namespace
int main()
{
  try {
    boundaries();
    releaseAndRaces();
    invalidIdentity();
    concurrentRevocation();
    std::cout << "Window drag edge observation and exact press join passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
