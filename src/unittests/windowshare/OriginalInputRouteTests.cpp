/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "base/Event.h"
#include "windowshare/OriginalInputRoute.h"
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace deskflow::windowshare;
using namespace std::chrono_literals;
namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
struct Fixture
{
  OriginalInputStream source;
  std::shared_ptr<OriginalInputRoutes> routes = std::make_shared<OriginalInputRoutes>();
  deskflow::AuthenticatedPeerIssuer issuer;
  deskflow::AuthenticatedPeerTicket ticket;
  OriginalInputRouteJoin join{source.epoch(), routes};
  OriginalInputRouteJoin::Clock::time_point now = OriginalInputRouteJoin::Clock::now();
  std::uint64_t sequence = 0;
  Fixture()
  {
    deskflow::AuthenticatedPeerTicket::Fingerprint fingerprint{};
    fingerprint.fill(0x51);
    ticket = *issuer.issue(fingerprint);
    check(routes->bind(11), "bind once");
  }
  OriginalInputCaptured capture(std::uint16_t parts = 1, bool uncertain = false)
  {
    const auto next = ++sequence;
    check(source.push({source.epoch().id(), next, next, 0, OriginalInputMotion{3, -4}}), "push raw");
    auto raw = *source.pop();
    return {raw, {{raw.record.epoch, next, 0}, parts, uncertain}};
  }
  OriginalInputRouteDecision decision(std::uint64_t sequence, std::uint16_t part = 1)
  {
    return {
        {source.epoch().id(), sequence, part},
        OriginalInputRouteKind::Motion,
        OriginalInputRouteOutcome::Dispatched,
        11,
        7,
        ticket,
        203,
        99,
        true
    };
  }
};
void eventStamping()
{
  OriginalInputStampScope scope({9, 12, 0});
  Event unrelated(deskflow::EventTypes::Timer);
  check(!unrelated.originalInputStamp(), "unrelated event is not a translation part");
  Event input(deskflow::EventTypes::KeyStateKeyDown);
  check(input.originalInputStamp() == OriginalInputStamp{9, 12, 1}, "physical event part");
  {
    OriginalInputStampScope::Pause actions;
    Event generated(deskflow::EventTypes::KeyStateKeyDown);
    Event forwarded(input.getType(), nullptr, nullptr, 0, input.originalInputStamp());
    check(
        !generated.originalInputStamp() && forwarded.originalInputStamp() == input.originalInputStamp(),
        "filter action is unstamped but pass-through retains explicit stamp"
    );
  }
  Event moved(std::move(input));
  check(moved.originalInputStamp() == OriginalInputStamp{9, 12, 1}, "queue moves preserve POD metadata");
  Event up(deskflow::EventTypes::KeyStateKeyUp);
  check(up.originalInputStamp().part == 2 && scope.translation().parts == 2, "exact expansion count");
  OriginalInputStampScope::uncertain();
  check(scope.translation().uncertain, "uncorrelated source position is retained");
  for (unsigned i = 2; i < maximumOriginalInputParts + 1U; ++i) {
    Event expanded(deskflow::EventTypes::KeyStateKeyDown);
    if (i == maximumOriginalInputParts)
      check(!expanded.originalInputStamp(), "bounded part exhaustion denies");
  }
  check(scope.translation().parts == maximumOriginalInputParts && scope.translation().uncertain, "no part wrap");
}
void joinsAndDenials()
{
  Fixture f;
  auto first = f.capture(), second = f.capture();
  check(f.join.decision(f.decision(1), f.now), "route before capture");
  check(f.join.capture(first, f.now) && f.join.capture(second, f.now), "ordered captures");
  auto joined = f.join.pop(f.now);
  check(
      joined && joined->live() && joined->input.record.sequence == 1 && joined->route.x == 203 &&
          joined->route.recipient == 7 && joined->route.ticket.incarnation() == f.ticket.incarnation(),
      "exact joined decision"
  );
  check(!f.join.pop(f.now), "capture without route waits");
  check(f.join.decision(f.decision(2), f.now), "capture before route");
  check(f.join.pop(f.now)->input.record.sequence == 2, "output retains capture order");
  f.issuer.revoke();
  check(!joined->live(), "retained evidence follows exact TLS connection revocation");
  for (const auto &[parts, uncertain, outcome] :
       {std::tuple{std::uint16_t(0), false, OriginalInputRouteOutcome::NoTranslation},
        std::tuple{std::uint16_t(2), false, OriginalInputRouteOutcome::Expanded},
        std::tuple{std::uint16_t(1), true, OriginalInputRouteOutcome::Uncorrelated}}) {
    Fixture d;
    check(d.join.capture(d.capture(parts, uncertain), d.now), "denied capture accepted for accounting");
    auto denial = d.join.pop(d.now);
    check(denial && denial->route.outcome == outcome, "explicit non-correlatable outcome");
    if (parts)
      check(d.join.decision(d.decision(1), d.now) && !d.join.pop(d.now), "late route cannot revive retired denial");
    else
      check(!d.join.decision(d.decision(1), d.now), "zero-part translation cannot acquire a route later");
  }
  for (const auto outcome :
       {OriginalInputRouteOutcome::Filtered, OriginalInputRouteOutcome::Relative, OriginalInputRouteOutcome::NoSend,
        OriginalInputRouteOutcome::Ambiguous, OriginalInputRouteOutcome::Local}) {
    Fixture d;
    auto decision = d.decision(1);
    decision.outcome = outcome;
    check(d.join.capture(d.capture(), d.now) && d.join.decision(decision, d.now), "explicit core denial");
    check(d.join.pop(d.now)->route.outcome == outcome, "core denial never upgraded");
  }
  Fixture wrong;
  auto decision = wrong.decision(1);
  decision.kind = OriginalInputRouteKind::Key;
  wrong.join.capture(wrong.capture(), wrong.now);
  wrong.join.decision(decision, wrong.now);
  check(wrong.join.pop(wrong.now)->route.outcome == OriginalInputRouteOutcome::Uncorrelated, "type mismatch denies");
}
void expiryDiscontinuityAndRevocation()
{
  Fixture late;
  late.join.capture(late.capture(), late.now);
  check(
      !late.join.decision(late.decision(1), late.now + OriginalInputRouteJoin::timeout) && !late.join.live(),
      "late queued route cannot revive expired join"
  );
  Fixture missing;
  missing.join.capture(missing.capture(), missing.now);
  check(
      !missing.join.pop(missing.now + OriginalInputRouteJoin::timeout) && !missing.routes->live(),
      "missing route revokes"
  );
  Fixture stale;
  auto bad = stale.decision(1);
  ++bad.stamp.epoch;
  check(!stale.join.decision(bad, stale.now), "other capture epoch rejected");
  Fixture duplicate;
  check(
      duplicate.join.decision(duplicate.decision(1), duplicate.now) &&
          !duplicate.join.decision(duplicate.decision(1), duplicate.now),
      "duplicate part terminal"
  );
  Fixture extra;
  extra.join.capture(extra.capture(), extra.now);
  extra.join.decision(extra.decision(1), extra.now);
  auto exact = extra.join.pop(extra.now);
  check(
      exact && exact->live() && !extra.join.decision(extra.decision(1, 2), extra.now) && !exact->live(),
      "undeclared extra part revokes already retained single-part correlation"
  );
  Fixture order;
  auto raw = order.capture();
  ++raw.input.record.sequence;
  check(!order.join.capture(raw, order.now), "missing capture terminal");
  Fixture f;
  f.join.capture(f.capture(), f.now);
  f.join.decision(f.decision(1), f.now);
  auto retained = f.join.pop(f.now);
  check(retained && retained->live(), "retain before overflow");
  for (std::size_t i = 0; i < OriginalInputRoutes::capacity; ++i)
    check(f.routes->publish(f.decision(i + 2)), "fill route queue");
  check(!f.routes->publish(f.decision(10000)) && !retained->live(), "overflow after pop revokes retained values");
  Fixture replaced;
  check(!replaced.routes->bind(12) && !replaced.routes->live(), "new server cannot reuse bound publication");
}
void concurrency()
{
  auto routes = std::make_shared<OriginalInputRoutes>();
  check(routes->bind(1), "stress bind");
  std::atomic<std::uint64_t> consumed{0};
  std::atomic_bool failed{false};
  std::thread producer([&] {
    for (std::uint64_t seq = 1; seq <= 20000; ++seq) {
      while (seq - consumed.load(std::memory_order_acquire) > 32)
        std::this_thread::yield();
      OriginalInputRouteDecision d;
      d.stamp = {1, seq, 1};
      d.x = static_cast<std::int32_t>(seq);
      if (!routes->publish(d)) {
        failed = true;
        break;
      }
    }
  });
  for (std::uint64_t seq = 1; seq <= 20000 && !failed.load();) {
    if (auto decision = routes->pop()) {
      if (decision->stamp.sequence != seq || decision->x != static_cast<std::int32_t>(seq))
        failed = true;
      consumed.store(seq++, std::memory_order_release);
    } else
      std::this_thread::yield();
  }
  producer.join();
  check(!failed, "SPSC ordering and slot reuse");
  auto evidence = routes->evidence();
  routes.reset();
  check(evidence->load(), "destruction revokes retained publication evidence");
}
} // namespace
int main()
{
  try {
    eventStamping();
    joinsAndDenials();
    expiryDiscontinuityAndRevocation();
    concurrency();
    std::cout << "Original input route tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
