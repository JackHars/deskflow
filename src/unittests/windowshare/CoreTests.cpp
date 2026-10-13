/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "Core.h"
#include <iostream>
#include <limits>
#include <random>
#include <string_view>
using namespace deskflow::windowshare;
namespace {
unsigned checks = 0;
void check(bool condition, const char *what)
{
  ++checks;
  if (!condition)
    throw std::runtime_error(what);
}
template <class Fn> void rejects(Fn fn)
{
  bool rejected = false;
  try {
    fn();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "expected rejection");
}
} // namespace
int main()
{
  try {
    Rect source{-800, -50, 800, 600};
    auto p = mapPoint({210, 170}, {10, 20, 400, 300}, source);
    check(p && p->x == -400 && p->y == 250, "retina / negative-origin mapping");
    check(!mapPoint({0, 0}, {10, 20, 400, 300}, source), "black bars reject clicks");
    check(!mapPoint({std::numeric_limits<double>::quiet_NaN(), 0}, {0, 0, 10, 10}, source), "NaN rejected");
    check(!Rect{0, 0, 0, 100}.valid(), "zero size rejected");
    check(!Rect{0, 0, 20000, 100}.valid(), "excessive size rejected");
    p = mapPoint({900, 900}, {0, 0, 100, 100}, source, true);
    check(p && p->x <= 0 && p->y <= 550, "drag clamp");
    const auto r = fitInside({900, 900, 800, 600}, {-100, 0, 1024, 768});
    check(r.x == 124 && r.y == 168, "recover after monitor removal");
    const std::array<Rect, 3> outputs{{{-1920, 0, 1920, 1080}, {0, 0, 1920, 1080},
                                       {1920, -1080, 2560, 1440}}};
    auto restored = recoveryRect({-1800, 400, 800, 600}, outputs);
    check(restored && restored->x == -1800 && restored->y == 400, "negative physical screen preserved");
    restored = recoveryRect({2100, -800, 500, 400}, outputs);
    check(restored && restored->x == 2100 && restored->y == -800, "upper output preserved");
    restored = recoveryRect({2400, 600, 600, 400}, outputs);
    check(restored && restored->x == 2400 && restored->y == -40,
          "offscreen source chooses nearest remaining upper-right display");
    restored = recoveryRect({-2500, 150, 400, 200}, outputs);
    check(restored && restored->x == -1920 && restored->y == 150,
          "missing left output recovers to the closest screen");
    const std::array<Rect, 2> reversed{{outputs[2], outputs[0]}};
    auto choiceOne = recoveryRect({0, -600, 500, 500}, reversed);
    auto choiceTwo = recoveryRect({0, -600, 500, 500}, std::array<Rect, 2>{{outputs[0], outputs[2]}});
    check(choiceOne && choiceTwo && choiceOne->x == choiceTwo->x && choiceOne->y == choiceTwo->y,
          "output enumeration is irrelevant");
    restored = recoveryRect({1000, 1000, 1600, 700}, outputs);
    check(restored && restored->width == 1600 && restored->height == 700 && restored->x == 320 &&
              restored->y == 380, "overlap selects physical output even near another monitor");
    check(!recoveryRect({0, 0, 800, 400}, {}), "absent outputs do not trigger unsafe move");
    check(!recoveryRect({0, 0, 0, 400}, outputs), "invalid saved window does not trigger move");
    const std::array<Rect, 1> invalid{{{0, 0, 0, 400}}};
    check(!recoveryRect({0, 0, 200, 200}, invalid), "invalid only output rejected");
    const std::array<Rect, 2> mixed{{invalid[0], outputs[0]}};
    check(recoveryRect({-400, 30, 100, 80}, mixed).has_value(), "malformed output is ignored");
    using Output = PhysicalOutputSignature;
    const std::array<Output, 2> topology{{{11, {-1920, 0, 1920, 1080}, 3840, 2160, 0},
                                           {22, {0, -1080, 2560, 1440}, 2560, 1440, 90}}};
    const std::array<Output, 2> permuted{{topology[1], topology[0]}};
    check(samePhysicalOutputLayout(topology, permuted), "display enumeration reorder preserves physical identity");
    check(!samePhysicalOutputLayout(topology, {}), "missing outputs revoke retained topology");
    check(!samePhysicalOutputLayout({}, {}), "two absent topologies cannot establish identity");
    check(!samePhysicalOutputLayout(topology, std::array<Output, 2>{{topology[0], topology[0]}}),
          "duplicate output cannot replace another display identity");
    check(!samePhysicalOutputLayout(topology, std::array<Output, 2>{{topology[0], {23, topology[1].bounds, 2560, 1440, 90}}}),
          "reused geometry cannot substitute a different physical display");
    check(!samePhysicalOutputLayout(topology, std::array<Output, 2>{{topology[0], {22, topology[1].bounds, 5120, 2880, 90}}}),
          "pixel scale/mode transition invalidates topology even with unchanged logical bounds");
    check(!samePhysicalOutputLayout(topology, std::array<Output, 2>{{topology[0], {22, topology[1].bounds, 2560, 1440, 0}}}),
          "display rotation invalidates retained native geometry");
    check(!samePhysicalOutputLayout(topology, std::array<Output, 2>{{topology[0], {22, {1, -1080, 2560, 1440}, 2560, 1440, 90}}}),
          "negative origin/layout shift invalidates old window routing");
    check(!samePhysicalOutputLayout(topology, std::array<Output, 2>{{topology[0], {22, {0, -1080, 0, 1440}, 2560, 1440, 90}}}),
          "invalid native monitor dimensions cannot be treated as unchanged");
    check(!samePhysicalOutputLayout(topology, std::array<Output, 2>{{topology[0], {22, topology[1].bounds, 0, 1440, 90}}}),
          "unavailable native pixel mode refuses topology equality");
    check(!samePhysicalOutputLayout(topology, std::array<Output, 2>{{topology[0], {22, topology[1].bounds, 2560, 1440, 45}}}),
          "unsupported rotation cannot be used for a native permit");
    check(!samePhysicalOutputLayout(std::array<Output, 2>{{topology[0], topology[0]}}, permuted),
          "invalid baseline with duplicate ids cannot be retained");
    const auto sync = [](std::initializer_list<unsigned> data) {
      std::vector<std::uint8_t> bytes;
      for (auto value : data)
        bytes.push_back(std::uint8_t(value));
      return decoderSyncAccessUnit(bytes);
    };
    check(sync({0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1e, 0, 0, 1, 0x68, 0xce,
                0, 0, 0, 1, 0x65, 0x88}), "SPS/PPS/IDR with mixed Annex B prefixes");
    check(!sync({0, 0, 0, 1, 0x41, 0x33, 0, 0, 1, 0x67, 0x42, 0, 0, 1, 0x68, 0xce,
                 0, 0, 1, 0x65, 0x88}), "dependent video slice must not restart a decoder");
    check(!sync({0, 0, 1, 0x65, 0x88, 0, 0, 1, 0x67, 0x42, 0, 0, 1, 0x68, 0xce}),
          "IDR without preceding parameter sets denied");
    check(!sync({0, 0, 1, 0x67, 0x42, 0, 0, 1, 0x68, 0xce, 0, 0, 1, 0x41, 0x11}),
          "SPS and PPS cannot make P frame decoder-safe");
    check(!sync({0, 0, 1, 0xe7, 0x42, 0, 0, 1, 0x68, 0xce, 0, 0, 1, 0x65, 0x88}),
          "forbidden H.264 header bit denied");
    check(!sync({0, 0, 1, 0x67, 0x42, 0, 0, 1, 0x68, 0xce, 0, 0, 1, 0x65}),
          "header-only NAL is not decoder-safe");
    check(!sync({0, 0, 1, 0x67, 0x42, 0, 0, 1, 0x68, 0xce, 0, 0, 1, 0x65, 0x88, 0, 0, 1}),
          "trailing empty NAL must be denied");
    check(!sync({0, 0, 1, 0x67, 0x42, 0, 0, 1, 0x68, 0xce, 0, 0, 1, 0x65, 0x88, 0, 0, 0}),
          "trailing unframed bytes denied");
    check(!decoderSyncAccessUnit({}), "empty video cannot restart decoder");
    Session s;
    s.id = 42;
    rejects([&] { s.prepare(1, source, source); });
    s.journaled = true;
    s.prepare(1, source, source);
    check(!s.acceptsInput(1, 1), "preparing not interactive");
    rejects([&] { s.commit(1); });
    rejects([&] { s.frameReady(2); });
    s.frameReady(1);
    s.commit(1);
    check(s.acceptsInput(1, 1), "remote input allowed");
    check(!s.acceptsInput(2, 1) && !s.acceptsInput(1, 2), "stale input rejected");
    s.suspend();
    check(!s.acceptsInput(1, 1), "suspend revokes input");
    s.beginReturn();
    s.restored();
    check(s.phase == Phase::Local && !s.journaled, "restore transaction");
    s.journaled = true;
    rejects([&] { s.prepare(1, source, source); });
    InputLedger ledger;
    const Owner a{1, 1}, b{2, 1};
    check(ledger.down(true, 12, a), "key down");
    check(!ledger.down(true, 12, b), "no double key down");
    check(!ledger.up(true, 12, b) && ledger.ownerOf(true, 12) == a, "wrong owner cannot consume release");
    check(ledger.up(true, 12, a) == a, "up belongs to original owner");
    check(!ledger.up(true, 12), "duplicate up ignored");
    check(ledger.down(false, 1, a) && ledger.down(true, 1, b), "key/button namespaces");
    check(ledger.release(a).size() == 1 && ledger.size() == 1, "owner release");
    check(ledger.release().size() == 1 && ledger.size() == 0, "disconnect release");
    check(ledger.down(true, 0x01000020, a), "Qt Shift accepted without truncation");
    check(ledger.down(true, 0x01000012, b), "Qt arrow accepted");
    check(!ledger.up(true, 0x01000020, b), "special key release retains owner");
    check(ledger.up(true, 0x01000020, a) == a, "special key up matches");
    check(ledger.release().size() == 1, "special keys released on disconnect");
    check(!ledger.down(true, 0x02000000, a), "invalid key rejected");
    check(!ledger.down(false, 32, a), "invalid button rejected");
    FocusLease focus;
    auto rev = focus.request(a);
    check(!focus.permits(a, rev), "focus pending");
    check(focus.acknowledge(a, rev) && focus.permits(a, rev), "focus ack");
    auto next = focus.request(b);
    check(!focus.acknowledge(a, rev) && !focus.permits(a, rev), "stale focus ack");
    check(focus.acknowledge(b, next), "new focus ack");
    focus.revoke();
    check(!focus.permits(b, next), "focus revoked");
    SequenceGate gate;
    check(!gate.accept(0) && gate.accept(2) && !gate.accept(1) && !gate.accept(2) && gate.accept(3), "replay gate");
    Packet packet{MessageType::Frame, 55, 3, 1, {0, 1, 2, 3, 255}};
    const auto encoded = encode(packet);
    check(encoded.size() == headerSize + 5, "wire length");
    for (std::size_t split = 0; split <= encoded.size(); ++split) {
      Decoder decoder;
      unsigned received = 0;
      auto receive = [&](Packet got) {
        ++received;
        check(
            got.session == packet.session && got.epoch == packet.epoch && got.sequence == 1 &&
                got.type == packet.type && got.payload == packet.payload,
            "fragmented roundtrip"
        );
      };
      decoder.feed(std::span(encoded).first(split), receive);
      decoder.feed(std::span(encoded).subspan(split), receive);
      check(received == 1 && decoder.buffered() == 0, "one complete packet");
    }
    auto coalesced = encoded;
    coalesced.insert(coalesced.end(), encoded.begin(), encoded.end());
    Decoder multiple;
    unsigned count = 0;
    multiple.feed(coalesced, [&](Packet) { ++count; });
    check(count == 2, "coalesced packets");
    for (unsigned offset : {0u, 8u, 10u, 36u}) {
      auto bad = encoded;
      bad[offset] = 255;
      Decoder decoder;
      rejects([&] { decoder.feed(bad, [](Packet) {}); });
      check(decoder.failed() && decoder.buffered() == 0, "failed decoder clears buffer");
      rejects([&] { decoder.feed(encoded, [](Packet) {}); });
    }
    auto oversized = encoded;
    oversized[4] = 0x7f;
    Decoder decoder;
    rejects([&] { decoder.feed(oversized, [](Packet) {}); });
    Packet empty{MessageType::Ping, 0, 0, 9, {}};
    Decoder d0;
    count = 0;
    d0.feed(encode(empty), [&](Packet) { ++count; });
    check(count == 1, "empty payload");
    std::mt19937 random(37);
    for (unsigned i = 0; i < 10000; ++i) {
      const double width = 1 + random() % 8000, height = 1 + random() % 8000;
      const double x = random() % static_cast<unsigned>(width), y = random() % static_cast<unsigned>(height);
      auto mapped = mapPoint({x, y}, {0, 0, width, height}, source);
      check(mapped && source.contains(*mapped), "random point remains inside source");
      auto mutated = encoded;
      mutated[random() % headerSize] ^= static_cast<std::uint8_t>(1 + random() % 255);
      Decoder fuzz;
      try {
        fuzz.feed(mutated, [](Packet) {});
      } catch (const std::exception &) {
      }
      check(fuzz.buffered() <= maxPayload + headerSize, "bounded decoder");
    }
    std::cout << "PASS: " << checks << " checks\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
