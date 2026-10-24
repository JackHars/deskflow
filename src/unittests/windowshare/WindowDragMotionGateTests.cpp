/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WindowDragMotionGate.h"
#include <array>
#include <barrier>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

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
void capturePrefix()
{
  WindowDragMotionGate gate(7);
  const auto now = WindowDragMotionGate::Clock::now();
  gate.observe(down(), now);
  for (std::uint64_t sequence = 2; sequence <= 12; ++sequence)
    check(!gate.observe(move(sequence), now), "pre-arm movement belongs to the native owner");
  const auto prefix = gate.armWithPrefix({7, 1, 1}, now);
  check(prefix == OriginalInputStamp{7, 12, 0}, "arming names the native capture prefix, not the press alone");
  check(gate.observe(move(13), now), "later admitted movement is suppressed");
  check(!gate.armWithPrefix({7, 1, 1}, now), "prefix admission cannot rearm the same press");
  check(!gate.observe(up(14), now), "prefix admission still passes the real release");
  gate.observe(down(15), now);
  check(gate.armWithPrefix({7, 15, 1}, now) == OriginalInputStamp{7, 15, 0}, "next press gets its own prefix");
}
void concurrentPrefix()
{
  // Real producer/coordinator racing; barriers bound fixture phases, not the
  // arm/observe order. No sleep, native input or extra consumer is involved.
  constexpr int rounds = 2000;
  const auto now = WindowDragMotionGate::Clock::now();
  std::barrier phase(2);
  std::array<std::unique_ptr<WindowDragMotionGate>, rounds> gates;
  std::array<bool, rounds> suppressed{};
  std::array<std::optional<OriginalInputStamp>, rounds> prefixes;
  for (auto &gate : gates)
    gate = std::make_unique<WindowDragMotionGate>(7);
  std::thread producer([&] {
    for (int i = 0; i < rounds; ++i) {
      gates[i]->observe(down(), now);
      phase.arrive_and_wait();
      suppressed[i] = gates[i]->observe(move(2), now);
      phase.arrive_and_wait();
    }
  });
  for (int i = 0; i < rounds; ++i) {
    phase.arrive_and_wait();
    prefixes[i] = gates[i]->armWithPrefix({7, 1, 1}, now);
    phase.arrive_and_wait();
  }
  producer.join();
  for (int i = 0; i < rounds; ++i) {
    // A concurrently observed event is either withheld or explicitly included
    // in the prefix. An unheld event beyond it would be a false gate-loss signal.
    const auto &prefix = prefixes[i];
    check(
        prefix && prefix->epoch == 7 && prefix->part == 0 && prefix->sequence >= 1 && prefix->sequence <= 2 &&
            (suppressed[i] || prefix->sequence == 2),
        "concurrent native movement escaped the arming prefix"
    );
  }
}
} // namespace
int main()
{
  try {
    cycle();
    expiryAndInterruption();
    capturePrefix();
    concurrentPrefix();
    std::cout << "PASS: exact-press motion hold, capture prefix/race, single native release, expiry and cancellation\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
