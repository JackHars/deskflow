/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "EiPrefixProbe.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using deskflow::windowshare::EiPrefixProbe;
using namespace std::chrono_literals;

void check(bool good, const char *name)
{
  if (!good) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    std::exit(1);
  }
}
} // namespace

int main()
{
  const auto start = EiPrefixProbe::Instant{} + 1h;
  using State = EiPrefixProbe::State;
  EiPrefixProbe p;
  check(!p.begin(0, start, true), "zero ping ID refuses");
  check(!p.begin(42, start, false), "unsupported sender refuses");
  check(!p.begin(42, EiPrefixProbe::Instant::max() - 100ms, true), "clock overflow refuses");
  check(p.state() == State::Idle, "failed begin cannot mutate attempt");
  check(p.begin(42, start, true), "live sender initiates");
  check(!p.begin(43, start + 1ms, true), "only one outstanding ping");
  p.pong(42, start + 1ms);
  check(
      p.state() == State::PongWithinBatch && !p.observed(start + 2ms),
      "PONG is not a receipt before complete event batch"
  );
  p.invalidate();
  p.finishEventBatch(start + 3ms);
  check(!p.observed(start + 3ms) && p.state() == State::Denied, "PONG followed by synthetic DISCONNECT never succeeds");
  check(!p.begin(44, start + 4ms, true), "failed attempt terminal until actual connection teardown");

  p.resetAfterConnectionTeardown();
  check(p.begin(43, start, true), "new native connection permits new attempt");
  p.pong(42, start + 1ms);
  p.finishEventBatch(start + 2ms);
  check(p.state() == State::Denied, "wrong ping is terminal and cannot be retried in-place");

  p.resetAfterConnectionTeardown();
  check(p.begin(45, start, true), "exact new request");
  p.pong(45, start + 50ms);
  p.finishEventBatch(start + 100ms);
  check(p.observed(start + 101ms), "sender callback can be observed after entire batch");
  check(
      !p.observed(start + 250ms) && p.state() == State::Denied, "observation cannot be reused across absolute deadline"
  );
  p.pong(45, start + 251ms);
  check(!p.observed(start + 252ms), "late PONG cannot revive expired receiver");

  p.resetAfterConnectionTeardown();
  check(p.begin(70, start, true), "new timeout request");
  p.finishEventBatch(start + 249ms);
  check(p.state() == State::AwaitingPong, "other batches do not advance to observed");
  p.pong(70, start + 250ms);
  p.finishEventBatch(start + 250ms);
  check(p.state() == State::Denied, "deadline includes equality boundary");

  p.resetAfterConnectionTeardown();
  check(p.begin(71, start, true), "new mid-batch request");
  p.pong(71, start + 1ms);
  p.invalidate(); // seat removal or device pause can occur after PONG
  p.finishEventBatch(start + 2ms);
  check(p.state() == State::Denied, "native invalidation in the same batch outranks PONG");

  p.resetAfterConnectionTeardown();
  check(p.begin(std::numeric_limits<std::uint64_t>::max(), start, true), "max retained ping identity");
  p.pong(std::numeric_limits<std::uint64_t>::max(), start + 1ms);
  p.finishEventBatch(start + 2ms);
  check(p.observed(start + 3ms), "retained 64-bit identity is compared exactly");
  p.invalidate();
  check(!p.observed(start + 4ms), "later native disconnect revokes observation");

  p.resetAfterConnectionTeardown();
  check(!p.begin(1, 4, start, start, true), "expired caller deadline refuses");
  check(!p.begin(1, 4, start, start + 251ms, true), "caller cannot extend maximum lifetime");
  check(p.begin(1, 4, start, start + 20ms, true), "requested cutoff inherits earlier deadline");
  p.pong(4, start + 1ms);
  check(!p.observed(start + 2ms), "requested pong still needs whole event batch");
  p.finishEventBatch(start + 3ms);
  check(p.observed(start + 4ms), "requested prefix completes after batch");
  check(!p.retire(2) && p.observed(start + 5ms), "mismatched resume cannot retire current prefix");
  check(p.retire(1), "exact resume retires requested prefix");
  check(!p.begin(1, 5, start + 6ms, start + 20ms, true), "retired request cannot be reused");
  check(p.begin(2, 5, start + 6ms, start + 20ms, true), "new requested ping on same owned connection");
  p.pong(5, start + 19ms);
  p.finishEventBatch(start + 20ms);
  check(!p.observed(start + 20ms), "whole batch must finish within inherited absolute deadline");
  check(p.retire(2), "denied request still accepts exact cleanup");

  std::puts("PASS: EI probe rejects stale/wrong/late PONG and same-batch disconnect");
  return 0;
}
