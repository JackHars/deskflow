/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "windowshare/OriginalPointerJoin.h"

#include <iostream>
#include <stdexcept>

using namespace deskflow::windowshare;
using namespace std::chrono_literals;

namespace {
void check(bool ok, const char *message)
{
  if (!ok)
    throw std::runtime_error(message);
}
struct Fixture
{
  OriginalInputStream stream;
  OriginalPointerJoin join{stream.epoch()};
  OriginalPointerJoin::Clock::time_point now{};
  std::uint64_t sequence = 0;
  OriginalInputCaptured input(bool down = true)
  {
    const OriginalInputRecord record{
        stream.epoch().id(), ++sequence, sequence * 100, 0, OriginalInputButton{0, down, down ? 1U : 0U}
    };
    check(stream.push(record), "fixture source accepts record");
    auto delivery = stream.pop();
    check(bool(delivery), "fixture has exactly one raw consumer");
    OriginalPointerObservation pointer{
        record.epoch, record.sequence, record.sourceTimestampNs, -1200.25, 410.5, 0, 0, 515, 0, 1, 3
    };
    return {*delivery, {{record.epoch, record.sequence, 0}, 0, false}, {}, pointer};
  }
  static OriginalPointerAnnotation annotation(const OriginalInputCaptured &input)
  {
    const auto &r = input.input.record;
    const auto &p = *input.pointer;
    return {
        r.sourceTimestampNs,
        r.flags,
        std::get<OriginalInputButton>(r.payload),
        p.x,
        p.y,
        10359,
        10359,
        p.eventNumber,
        p.sourceProcess,
        p.sourceState,
        p.mouseSubtype
    };
  }
};
void exactPairs()
{
  for (const bool annotationFirst : {false, true}) {
    Fixture f;
    for (const bool down : {true, false}) {
      const auto input = f.input(down);
      const auto annotation = Fixture::annotation(input);
      if (annotationFirst) {
        check(f.join.annotate(annotation, f.now) && !f.join.pop(f.now), "annotation alone has no HID identity");
        check(f.join.capture(input, f.now + 1ms), "later source copy joins");
      } else {
        check(f.join.capture(input, f.now) && !f.join.pop(f.now), "source alone has no window identity");
        check(f.join.annotate(annotation, f.now + 1ms), "later annotation joins");
      }
      const auto result = f.join.pop(f.now + 2ms);
      check(result && result->live() && result->window() == 10359, "both event stages identify the exact window");
      check(
          result->captured.input.record.sequence == input.input.record.sequence &&
              result->captured.pointer->windowHandlingPointer == 0 &&
              result->annotation->button.clickCount == (down ? 1U : 0U),
          "HID stamp and missing HID fields stay intact; zero-click up is not normalized"
      );
      check(!f.join.pop(f.now + 3ms), "pair is consumed once");
    }
  }
}
void mismatches()
{
  for (int field = 0; field < 11; ++field) {
    Fixture f;
    const auto input = f.input();
    auto annotation = Fixture::annotation(input);
    switch (field) {
    case 0:
      ++annotation.eventNumber;
      break;
    case 1:
      annotation.button.down = false;
      break;
    case 2:
      ++annotation.button.clickCount;
      break;
    case 3:
      ++annotation.x;
      break;
    case 4:
      ++annotation.y;
      break;
    case 5:
      ++annotation.flags;
      break;
    case 6:
      ++annotation.sourceProcess;
      break;
    case 7:
      ++annotation.sourceState;
      break;
    case 8:
      ++annotation.mouseSubtype;
      break;
    case 9:
      annotation.x = std::numeric_limits<double>::infinity();
      break;
    case 10:
      annotation.button.button = 1;
      break;
    }
    check(f.join.capture(input, f.now), "valid source before mismatched annotation");
    check(!f.join.annotate(annotation, f.now) && !f.join.live() && !f.join.pop(f.now), "changed event is never joined");
  }
  for (const auto ids : {std::pair{0U, 0U}, std::pair{10359U, 10400U}, std::pair{0U, 10359U}}) {
    Fixture f;
    const auto input = f.input();
    auto annotation = Fixture::annotation(input);
    annotation.windowUnderPointer = ids.first;
    annotation.windowHandlingPointer = ids.second;
    check(f.join.capture(input, f.now) && f.join.annotate(annotation, f.now), "missing IDs do not corrupt raw input");
    const auto result = f.join.pop(f.now);
    check(result && result->live() && !result->window(), "missing or disagreeing window IDs cannot identify a source");
  }
}
void deadlinesAndLifetime()
{
  Fixture f;
  const auto old = f.input();
  check(f.join.capture(old, f.now), "capture waits for annotation");
  check(f.join.annotate(Fixture::annotation(old), f.now + 49ms), "second half does not renew deadline");
  const auto expired = f.join.pop(f.now + 50ms);
  check(expired && !expired->annotation && !expired->window(), "absolute deadline discards a late completed pair");
  check(
      f.join.annotate(Fixture::annotation(old), f.now + 51ms) && !f.join.pop(f.now + 51ms), "late replay stays retired"
  );
  const auto fresh = f.input(false);
  check(
      f.join.capture(fresh, f.now + 52ms) && f.join.annotate(Fixture::annotation(fresh), f.now + 53ms),
      "new pair can join after an expired pair"
  );
  const auto result = f.join.pop(f.now + 54ms);
  check(result && result->window(), "fresh pair retains identity");
  check(
      !f.join.annotate(Fixture::annotation(fresh), f.now + 55ms), "duplicate successful annotation revokes ambiguity"
  );
  check(
      !result->live() && !result->window() && f.stream.epoch().live(),
      "join loss revokes retained identity without killing raw emergency capture"
  );
  Fixture stopped;
  const auto input = stopped.input();
  stopped.join.capture(input, stopped.now);
  stopped.join.annotate(Fixture::annotation(input), stopped.now);
  const auto retained = stopped.join.pop(stopped.now);
  stopped.stream.revoke(OriginalInputRevocation::Stopped);
  check(retained && !retained->live(), "source epoch loss revokes retained identity");
}
void boundsAndOrder()
{
  Fixture f;
  for (std::size_t n = 0; n < OriginalPointerJoin::capacity; ++n)
    check(f.join.capture(f.input(n % 2 == 0), f.now), "bounded pending slots fill without overwrite");
  check(!f.join.capture(f.input(), f.now) && !f.join.live(), "full join denies overflow without revoking raw capture");
  check(f.stream.epoch().live(), "optional identity loss cannot stop raw input");
  Fixture ordered;
  const auto first = ordered.input();
  const auto second = ordered.input(false);
  ordered.join.capture(first, ordered.now);
  ordered.join.capture(second, ordered.now);
  ordered.join.annotate(Fixture::annotation(second), ordered.now);
  check(!ordered.join.pop(ordered.now), "later up cannot overtake pending down");
  ordered.join.annotate(Fixture::annotation(first), ordered.now);
  check(
      ordered.join.pop(ordered.now)->captured.input.record.sequence == first.input.record.sequence,
      "down delivered first"
  );
  check(
      ordered.join.pop(ordered.now)->captured.input.record.sequence == second.input.record.sequence,
      "up delivered second"
  );
  check(!ordered.join.capture(second, ordered.now), "capture replay is terminal for optional identity");
}
} // namespace

int main()
{
  try {
    exactPairs();
    mismatches();
    deadlinesAndLifetime();
    boundsAndOrder();
    std::cout << "PASS: bounded same-event HID/annotation identity join\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
