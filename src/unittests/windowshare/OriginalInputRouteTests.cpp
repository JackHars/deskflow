/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "base/Event.h"
#include "windowshare/OriginalInputCapturedInbox.h"
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
void deferredRouteOrder()
{
  Fixture fixture;
  for (unsigned index = 0; index < 3; ++index)
    check(fixture.join.capture(fixture.capture(), fixture.now), "deferred route captures retain input order");
  auto first = fixture.decision(1), filtered = fixture.decision(2), third = fixture.decision(3);
  first.outcome = OriginalInputRouteOutcome::Original;
  filtered.outcome = OriginalInputRouteOutcome::Filtered;
  check(fixture.routes->defer(first.stamp), "first Server action is explicitly deferred");
  check(
      fixture.routes->publish(filtered) && fixture.routes->publish(third) && !fixture.routes->pop() &&
          !fixture.join.pop(fixture.now),
      "later filtered and immediate decisions cannot overtake the unresolved first action"
  );
  check(fixture.routes->publish(first), "exact first stamp resolves its registered slot");
  for (const auto &expected : {first, filtered, third}) {
    const auto decision = fixture.routes->pop();
    check(
        decision && decision->stamp == expected.stamp && decision->outcome == expected.outcome &&
            decision->server == expected.server && decision->recipient == expected.recipient &&
            decision->ticket.incarnation() == expected.ticket.incarnation() && decision->x == expected.x &&
            decision->y == expected.y && decision->pointKnown == expected.pointKnown,
        "deferred publication preserves exact decisions in registration order"
    );
    check(fixture.join.decision(*decision, fixture.now), "ordered decisions remain valid diagnostic join input");
    const auto joined = fixture.join.pop(fixture.now);
    check(
        joined && joined->live() && joined->input.record.sequence == expected.stamp.sequence &&
            joined->route.outcome == expected.outcome,
        "the join retains original, filtered and dispatched outcomes without an ordering failure"
    );
  }
  check(!fixture.routes->pop() && !fixture.join.pop(fixture.now), "resolved actions are emitted only once");

  Fixture split;
  const std::array decisions{split.decision(1, 1), split.decision(1, 2), split.decision(2), split.decision(3)};
  for (const auto &decision : decisions)
    check(split.routes->defer(decision.stamp), "distinct parts and captures register independent pending slots");
  check(
      split.routes->publish(decisions[3]) && split.routes->publish(decisions[1]) && !split.routes->pop(),
      "out-of-order resolutions remain staged behind the earliest pending action"
  );
  check(split.routes->publish(decisions[0]), "resolving the head releases its ready prefix");
  for (std::size_t index = 0; index < 2; ++index) {
    const auto decision = split.routes->pop();
    check(decision && decision->stamp == decisions[index].stamp, "parts retain their registered order");
  }
  check(!split.routes->pop(), "a second unresolved action still fences the later ready result");
  check(split.routes->publish(decisions[2]), "resolving the next gap releases the remaining ready prefix");
  for (std::size_t index = 2; index < decisions.size(); ++index) {
    const auto decision = split.routes->pop();
    check(decision && decision->stamp == decisions[index].stamp, "later capture resolutions retain event order");
  }
  check(!split.routes->pop() && split.routes->live(), "partial flush leaves no duplicate decisions");

  Fixture reuse;
  for (std::uint64_t round = 0; round < OriginalInputRoutes::capacity + 3; ++round) {
    const auto head = reuse.decision(round * 2 + 1), tail = reuse.decision(round * 2 + 2);
    check(
        reuse.routes->defer(head.stamp) && reuse.routes->publish(tail) && !reuse.routes->pop() &&
            reuse.routes->publish(head),
        "pending and ready staging slots can be reused after their prefix drains"
    );
    const auto firstOut = reuse.routes->pop(), secondOut = reuse.routes->pop();
    check(
        firstOut && secondOut && firstOut->stamp == head.stamp && secondOut->stamp == tail.stamp &&
            !reuse.routes->pop(),
        "staging wraparound neither replays an old resolution nor drops a fresh decision"
    );
  }
}
void deferredRouteFailures()
{
  OriginalInputRoutes unbound;
  check(!unbound.defer({1, 1, 1}) && !unbound.live(), "unbound diagnostic producer cannot stage actions");
  for (const auto stamp :
       {OriginalInputStamp{}, OriginalInputStamp{0, 1, 1}, OriginalInputStamp{1, 0, 1}, OriginalInputStamp{1, 1, 0},
        OriginalInputStamp{1, 1, maximumOriginalInputParts + 1}}) {
    Fixture fixture;
    const auto evidence = fixture.routes->evidence();
    check(
        !fixture.routes->defer(stamp) && !fixture.routes->live() && evidence->load() && !fixture.routes->pop() &&
            fixture.source.epoch().live() && fixture.capture().input.live(),
        "malformed deferred stamps revoke only diagnostics and preserve ordinary capture"
    );
  }
  for (unsigned duplicate = 0; duplicate < 3; ++duplicate) {
    Fixture fixture;
    const auto first = fixture.decision(1), second = fixture.decision(2);
    check(fixture.routes->defer(first.stamp), "duplicate fixture registers its pending head");
    bool rejected = false;
    if (duplicate == 0)
      rejected = !fixture.routes->defer(first.stamp);
    else {
      check(
          fixture.routes->defer(second.stamp) && fixture.routes->publish(second) && !fixture.routes->pop(),
          "duplicate fixture retains a resolved slot behind its pending head"
      );
      rejected = duplicate == 1 ? !fixture.routes->publish(second) : !fixture.routes->defer(second.stamp);
    }
    check(
        rejected && !fixture.routes->live() && !fixture.routes->pop() && fixture.capture().input.live(),
        "duplicate pending registration or staged resolution revokes diagnostics without replay or raw loss"
    );
  }
  for (const bool readyTail : {false, true}) {
    Fixture fixture;
    check(fixture.routes->defer(fixture.decision(1).stamp), "staging overflow fixture registers its pending head");
    for (std::uint64_t sequence = 2; sequence <= OriginalInputRoutes::capacity; ++sequence) {
      const auto decision = fixture.decision(sequence);
      check(
          readyTail ? fixture.routes->publish(decision) : fixture.routes->defer(decision.stamp),
          "all bounded staging slots are available for ready or pending actions"
      );
    }
    check(!fixture.routes->pop(), "a full staging buffer still cannot bypass an unresolved head");
    const auto extra = fixture.decision(OriginalInputRoutes::capacity + 1);
    check(
        !(readyTail ? fixture.routes->publish(extra) : fixture.routes->defer(extra.stamp)) && !fixture.routes->live() &&
            fixture.routes->evidence()->load() && !fixture.routes->pop() && fixture.capture().input.live(),
        "staging exhaustion revokes only diagnostics instead of overwriting or emitting unresolved actions"
    );
  }
  Fixture blockedOutput;
  for (std::uint64_t sequence = 1; sequence <= OriginalInputRoutes::capacity; ++sequence)
    check(blockedOutput.routes->publish(blockedOutput.decision(sequence)), "fill diagnostic output before deferral");
  const auto head = blockedOutput.decision(OriginalInputRoutes::capacity + 1);
  check(
      blockedOutput.routes->defer(head.stamp) &&
          blockedOutput.routes->publish(blockedOutput.decision(OriginalInputRoutes::capacity + 2)) &&
          !blockedOutput.routes->publish(head) && !blockedOutput.routes->live() && !blockedOutput.routes->pop() &&
          blockedOutput.capture().input.live(),
      "ready prefix output exhaustion remains terminal only for diagnostics"
  );
  Fixture revoked;
  const auto pending = revoked.decision(1);
  check(
      revoked.routes->defer(pending.stamp) && revoked.routes->publish(revoked.decision(2)),
      "revocation fixture has both pending and ready staged decisions"
  );
  const auto evidence = revoked.routes->evidence();
  revoked.routes->revoke();
  check(
      evidence->load() && !revoked.routes->publish(pending) && !revoked.routes->defer(revoked.decision(3).stamp) &&
          !revoked.routes->pop() && revoked.capture().input.live(),
      "revocation cannot flush or revive staged actions and leaves raw capture independent"
  );
  auto destroyed = std::make_unique<OriginalInputRoutes>();
  check(destroyed->bind(11) && destroyed->defer(pending.stamp), "destruction fixture retains a pending action");
  const auto retained = destroyed->evidence();
  destroyed.reset();
  check(retained->load() && revoked.source.epoch().live(), "pending conduit destruction retains diagnostic revocation");
}
void originalRouteHistory()
{
  Fixture fixture;
  auto decision = fixture.decision(1);
  decision.outcome = OriginalInputRouteOutcome::Original;
  check(
      fixture.join.capture(fixture.capture(), fixture.now) && fixture.join.decision(decision, fixture.now),
      "committed original result joins only its exact completed capture"
  );
  const auto joined = fixture.join.pop(fixture.now);
  check(
      joined && joined->live() && joined->route.outcome == OriginalInputRouteOutcome::Original &&
          joined->route.ticket.incarnation() == fixture.ticket.incarnation(),
      "authenticated original commit is retained as diagnostic history"
  );
  fixture.issuer.revoke();
  check(
      !joined->live() && fixture.source.epoch().live() && fixture.routes->live(),
      "original history follows exact ticket revocation even while capture and route conduits stay live"
  );
  for (unsigned missing = 0; missing < 4; ++missing) {
    Fixture unauthenticated;
    auto result = unauthenticated.decision(1);
    result.outcome = OriginalInputRouteOutcome::Original;
    if (missing == 0)
      result.server = 0;
    else if (missing == 1)
      result.recipient = 0;
    else if (missing == 2)
      result.ticket = {};
    else
      unauthenticated.issuer.revoke();
    check(
        unauthenticated.join.capture(unauthenticated.capture(), unauthenticated.now) &&
            unauthenticated.join.decision(result, unauthenticated.now),
        "unauthenticated original outcome still receives diagnostic accounting"
    );
    const auto denied = unauthenticated.join.pop(unauthenticated.now);
    check(
        denied && denied->route.outcome == OriginalInputRouteOutcome::Unauthenticated,
        "an original outcome without its live authenticated recipient cannot claim committed history"
    );
  }
}
void completedCaptureInbox()
{
  Fixture fixture;
  OriginalInputCapturedInbox inbox(fixture.source.epoch());
  const auto evidence = inbox.evidence();
  const auto beforeBind = fixture.capture(0);
  check(!inbox.bound() && inbox.live() && !inbox.pop(), "completed capture conduit starts unbound and empty");
  check(
      !inbox.publish(beforeBind) && inbox.live() && !evidence->load() && !inbox.pop(),
      "pre-bind capture is skipped without poisoning or filling the conduit"
  );
  check(inbox.bind(11) && inbox.bound(), "one Server binds the completed capture conduit");
  auto captured = fixture.capture(maximumOriginalInputParts, true);
  const auto first = captured.input.record.sequence;
  check(first > 1 && inbox.publish(captured), "the first post-bind capture may begin after previously drained input");
  captured.input.record.payload = OriginalInputMotion{999, 999};
  captured.translation.parts = 0;
  captured.translation.uncertain = false;
  const auto retained = inbox.pop();
  check(
      retained && retained->input.live() && retained->input.epoch.sameEpoch(fixture.source.epoch()) &&
          retained->input.record.sequence == first &&
          std::get<OriginalInputMotion>(retained->input.record.payload).dx == 3 &&
          retained->translation.stamp == OriginalInputStamp{fixture.source.epoch().id(), first, 0} &&
          retained->translation.parts == maximumOriginalInputParts && retained->translation.uncertain && !inbox.pop(),
      "inbox copies every completed field exactly once without borrowing mutable producer data"
  );
  const auto untranslated = fixture.capture(0);
  check(inbox.publish(untranslated), "zero-part completions are still published");
  const auto zero = inbox.pop();
  check(zero && zero->translation.parts == 0 && !zero->translation.uncertain, "zero-part completion is not fabricated");
  for (std::size_t count = 0; count < OriginalInputCapturedInbox::capacity; ++count)
    check(inbox.publish(fixture.capture()), "all completed capture slots are available");
  check(
      !inbox.publish(fixture.capture()) && !inbox.live() && evidence->load() && !inbox.pop(),
      "bounded completed capture overflow revokes without overwriting or returning stale slots"
  );
  check(
      fixture.source.epoch().live() && fixture.routes->live() && retained->input.live() &&
          fixture.capture().input.live(),
      "conduit overflow never revokes raw capture, independent route diagnostics or retained epoch evidence"
  );
  check(!inbox.bind(12), "a revoked completed capture conduit cannot be rebound");
}
void completedCaptureValidation()
{
  for (int failure = 0; failure != 11; ++failure) {
    Fixture fixture, other;
    OriginalInputCapturedInbox inbox(fixture.source.epoch());
    check(inbox.bind(11), "malformed completion fixture binds");
    auto captured = fixture.capture();
    switch (failure) {
    case 0:
      captured.input.record.sequence = 0;
      break;
    case 1:
      captured.input.record.sourceTimestampNs = 0;
      break;
    case 2:
      captured.input.record.payload = OriginalInputMotion{std::numeric_limits<std::int64_t>::max(), 0};
      break;
    case 3:
      ++captured.input.record.epoch;
      break;
    case 4:
      captured.input.epoch = {};
      break;
    case 5:
      captured = other.capture();
      break;
    case 6:
      ++captured.translation.stamp.epoch;
      break;
    case 7:
      ++captured.translation.stamp.sequence;
      break;
    case 8:
      captured.translation.stamp.part = 1;
      break;
    case 9:
      captured.translation.parts = maximumOriginalInputParts + 1;
      break;
    case 10:
      captured.translation.stamp = {};
      break;
    }
    check(
        !inbox.publish(captured) && !inbox.live() && inbox.evidence()->load() && !inbox.pop() &&
            fixture.source.epoch().live() && fixture.routes->live(),
        "malformed completion or wrong epoch revokes only the completed capture conduit"
    );
  }
  for (int order = 0; order != 4; ++order) {
    Fixture fixture;
    OriginalInputCapturedInbox inbox(fixture.source.epoch());
    check(inbox.bind(11), "capture order fixture binds");
    auto first = fixture.capture();
    if (order == 2)
      first.input.record.sourceTimestampNs = 100;
    if (order == 3)
      first.translation.stamp.sequence = first.input.record.sequence = std::numeric_limits<std::uint64_t>::max();
    check(inbox.publish(first) && inbox.pop().has_value(), "capture order fixture publishes first complete record");
    auto next = fixture.capture();
    if (order == 0)
      next = first;
    if (order == 1)
      next = fixture.capture();
    check(
        !inbox.publish(next) && !inbox.live() && fixture.source.epoch().live(),
        "duplicate, skipped, regressed or exhausted capture order is terminal only for the conduit"
    );
  }
  for (const auto rebound : {0u, 11u, 12u}) {
    Fixture fixture;
    OriginalInputCapturedInbox inbox(fixture.source.epoch());
    check(inbox.bind(11), "rebind fixture claims its single Server");
    check(!inbox.bind(rebound) && !inbox.live(), "invalid, duplicate and replacement Server bindings fail closed");
  }
  Fixture fixture;
  OriginalInputCapturedInbox invalidBind(fixture.source.epoch());
  check(
      !invalidBind.bind(0) && !invalidBind.bound() && !invalidBind.live() && !invalidBind.bind(11),
      "initial invalid binding cannot be repaired into another Server lifetime"
  );
  OriginalInputCapturedInbox noEpoch({});
  check(!noEpoch.bind(11) && !noEpoch.live(), "no completed capture conduit can bind without a live epoch");
}
void completedCaptureLifetime()
{
  Fixture fixture;
  auto inbox = std::make_unique<OriginalInputCapturedInbox>(fixture.source.epoch());
  check(inbox->bind(11) && inbox->publish(fixture.capture()), "conduit lifetime fixture publishes");
  const auto evidence = inbox->evidence();
  const auto retained = inbox->pop();
  inbox.reset();
  check(
      evidence->load() && retained && retained->input.live() && fixture.source.epoch().live(),
      "destruction revokes retained conduit evidence without revoking the independent capture epoch"
  );
  OriginalInputCapturedInbox epochLoss(fixture.source.epoch());
  check(epochLoss.bind(12) && epochLoss.publish(fixture.capture()), "epoch loss fixture publishes");
  const auto epochEvidence = epochLoss.evidence();
  fixture.source.revoke(OriginalInputRevocation::Stopped);
  check(
      !epochLoss.pop() && !epochLoss.live() && epochEvidence->load() && !retained->input.live(),
      "revoked original epoch cannot escape through queued completed copies or retained evidence"
  );
}
void completedCaptureConcurrency()
{
  Fixture fixture;
  OriginalInputCapturedInbox inbox(fixture.source.epoch());
  check(inbox.bind(11), "completed capture stress binds");
  constexpr std::uint64_t total = 20000;
  std::atomic<std::uint64_t> consumed{0};
  std::atomic_bool failed{false};
  std::thread producer([&] {
    try {
      for (std::uint64_t sequence = 1; sequence <= total && !failed.load(std::memory_order_acquire); ++sequence) {
        while (sequence - consumed.load(std::memory_order_acquire) > 32 && !failed.load(std::memory_order_acquire))
          std::this_thread::yield();
        if (failed.load(std::memory_order_acquire))
          break;
        const auto captured = fixture.capture(static_cast<std::uint16_t>(sequence % 17), sequence % 2 == 0);
        if (!inbox.publish(captured))
          failed.store(true, std::memory_order_release);
      }
    } catch (...) {
      failed.store(true, std::memory_order_release);
    }
  });
  for (std::uint64_t sequence = 1; sequence <= total && !failed.load(std::memory_order_acquire);) {
    if (const auto captured = inbox.pop()) {
      if (!captured->input.live() || captured->input.record.sequence != sequence ||
          captured->translation.stamp != OriginalInputStamp{fixture.source.epoch().id(), sequence, 0} ||
          captured->translation.parts != sequence % 17 || captured->translation.uncertain != (sequence % 2 == 0))
        failed.store(true, std::memory_order_release);
      consumed.store(sequence++, std::memory_order_release);
    } else
      std::this_thread::yield();
  }
  producer.join();
  check(
      !failed && consumed == total && inbox.live() && !inbox.pop(),
      "SPSC completed captures retain exact sequence, translation count and flags across repeated slot reuse"
  );
}
} // namespace
int main()
{
  try {
    eventStamping();
    joinsAndDenials();
    expiryDiscontinuityAndRevocation();
    concurrency();
    deferredRouteOrder();
    deferredRouteFailures();
    originalRouteHistory();
    completedCaptureInbox();
    completedCaptureValidation();
    completedCaptureLifetime();
    completedCaptureConcurrency();
    std::cout << "Original input route tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
