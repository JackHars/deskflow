/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WindowDragMotionGate.h"
#include <iostream>
#include <stdexcept>

using namespace deskflow::windowshare;
using namespace std::chrono_literals;
namespace {
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
OriginalInputRecord down(std::uint64_t sequence = 1)
{
  return {7, sequence, sequence * 100, 0, OriginalInputButton{0, true, 1}};
}
OriginalInputRecord move(std::uint64_t sequence)
{
  return {7, sequence, sequence * 100, 0, OriginalInputMotion{15, -9, 0}};
}
OriginalInputRecord up(std::uint64_t sequence)
{
  return {7, sequence, sequence * 100, 0, OriginalInputButton{0, false, 0}};
}
void cycle()
{
  WindowDragMotionGate gate(7);
  const auto now = WindowDragMotionGate::Clock::now();
  const OriginalInputStamp press{7, 1, 0};
  check(!gate.observe(down(), now), "down stays native");
  check(!gate.observe(move(2), now), "unadmitted movement stays native");
  check(gate.arm(press, now), "exact retained press arms after admission");
  check(gate.observe(move(3), now + 20ms), "held movement is captured before native suppression");
  check(gate.lastSuppressedSequence() == 3, "suppression receipt names original movement");
  check(gate.refresh(press, now + 100ms), "owner heartbeat keeps hold live");
  check(!gate.observe(up(4), now + 200ms), "zero-click physical up always reaches native owner");
  check(!gate.active(press, now + 200ms) && !gate.refresh(press, now + 200ms), "real release retires hold");
  check(!gate.arm(press, now + 200ms), "a late preview cannot revive the released press");
  check(!gate.observe(down(5), now + 210ms), "next independent down stays native");
  check(gate.arm({7, 5, 0}, now + 210ms), "new physical press can be admitted");
  gate.cancel();
  check(!gate.observe(move(6), now + 220ms) && !gate.arm({7, 5, 0}, now + 220ms), "cancel is terminal per press");
}
void expiryAndInterruption()
{
  for (int scenario = 0; scenario < 8; ++scenario) {
    WindowDragMotionGate gate(7);
    const auto now = WindowDragMotionGate::Clock::now();
    const OriginalInputStamp press{7, 1, 0};
    gate.observe(down(), now);
    check(gate.arm(press, now), "fixture arm");
    auto interrupt = move(2);
    switch (scenario) {
    case 0:
      interrupt.payload = OriginalInputButton{1, true, 1};
      break;
    case 1:
      interrupt.payload = OriginalInputKey{};
      break;
    case 2:
      interrupt.payload = OriginalInputScroll{};
      break;
    case 3:
      interrupt.flags = originalKey::shiftFlag;
      break;
    case 4:
      interrupt.epoch = 8;
      break;
    case 5:
      interrupt.sequence = 1;
      break;
    case 6:
      std::get<OriginalInputMotion>(interrupt.payload).dragButton = originalInputButtonCount;
      break;
    case 7:
      gate.stop();
      break;
    }
    check(!gate.observe(interrupt, now + 1ms), "unexpected input is not suppressed");
    check(!gate.observe(move(3), now + 2ms) && !gate.refresh(press, now + 2ms), "interruption cannot revive hold");
    check(!gate.observe(up(4), now + 3ms), "release still passes after interruption");
  }
  WindowDragMotionGate gate(7);
  const auto now = WindowDragMotionGate::Clock::now();
  gate.observe(down(), now);
  check(gate.arm({7, 1, 0}, now), "expiry fixture");
  check(!gate.refresh({7, 1, 0}, now + 350ms), "late heartbeat cannot revive an expired hold");
  check(!gate.observe(move(2), now + 351ms), "stalled owner automatically returns movement locally");
  check(!gate.observe(up(3), now + 352ms), "release passes after timeout");
}
} // namespace
int main()
{
  try {
    cycle();
    expiryAndInterruption();
    std::cout << "PASS: exact-press motion hold, single native release, expiry and cancellation\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
