/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeAssociatedWindow.h"
#include "NativeHostLifecycle.h"
#include "NativeOriginalInputState.h"

#include <iostream>
#include <stdexcept>
#include <thread>

using namespace deskflow::windowshare;

namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
void associatedChildReturn()
{
  const NativeAssociatedParent parent{{11, 12}, 71, 81, 91};
  for (const auto kind : {NativeAssociatedRole::Sheet, NativeAssociatedRole::Drawer}) {
    const NativeAssociatedEvidence exact{parent, kind, true, true};
    for (int variation = 0; variation < 10; ++variation) {
      NativeAssociatedReturn state;
      auto wrong = exact;
      if (variation == 0)
        ++wrong.observedParent.owner.session;
      if (variation == 1)
        ++wrong.observedParent.owner.epoch;
      if (variation == 2)
        ++wrong.observedParent.window;
      if (variation == 3)
        ++wrong.observedParent.process;
      if (variation == 4)
        ++wrong.observedParent.birth;
      if (variation == 5)
        wrong.role = NativeAssociatedRole::Other;
      if (variation == 6)
        wrong.role = static_cast<NativeAssociatedRole>(99);
      if (variation == 7)
        wrong.exactFocusedTopLevel = false;
      if (variation == 8)
        wrong.exactAttachedParent = false;
      if (variation == 9)
        wrong.observedParent.window = 0;
      check(
          !state.observe(parent, wrong) && !state.blocked(),
          "same-PID windows, stale exports, process replacements, unknown roles and incomplete AX links never return a "
          "parent"
      );
      check(
          state.observe(parent, exact) && state.blocked() && state.returning(parent),
          "exact attached sheet/drawer fences the original export before restoration callbacks"
      );
      check(
          !state.observe(parent, exact) && state.blocked(), "nested callbacks cannot schedule a second parent return"
      );
      check(
          !state.observe(parent, wrong) && state.returning(parent),
          "later focus or AX changes cannot reopen or relabel the pending return"
      );
      auto replacement = parent;
      ++replacement.owner.epoch;
      check(
          !state.returning(replacement),
          "delayed return cannot restore a replacement export with the same native window"
      );
      NativeAssociatedReturn fresh;
      check(!fresh.blocked(), "new export owns an independent explicit admission lifetime");
    }
  }
  for (int field = 0; field < 5; ++field) {
    auto invalid = parent;
    if (field == 0)
      invalid.owner.session = 0;
    if (field == 1)
      invalid.owner.epoch = 0;
    if (field == 2)
      invalid.window = 0;
    if (field == 3)
      invalid.process = 0;
    if (field == 4)
      invalid.birth = 0;
    NativeAssociatedReturn state;
    check(
        !state.observe(invalid, {invalid, NativeAssociatedRole::Sheet, true, true}),
        "unknown native identity never authorizes a return"
    );
  }
}

KeyboardRouteBinding binding()
{
  return {{11, 12}, 31, 14, 15, 16, ForwardingScope::All};
}
LocalInputMappedPoint point()
{
  return {120.25, 230.5, 640, 480, localInputMonotonicNs() + 200000000};
}
OriginalInputKey key(std::uint16_t code, OriginalKeyTransition transition)
{
  return {
      42, code,
      originalKeyIsModifier(code) || code == originalKey::capsLock ? OriginalKeyKind::FlagsChanged
      : transition == OriginalKeyTransition::Up                    ? OriginalKeyKind::Up
                                                                   : OriginalKeyKind::Down,
      transition, transition == OriginalKeyTransition::Repeat
  };
}
struct Fixture
{
  NativeOriginalInputState state;
  KeyboardRouteBinding owner = binding();
  std::uint64_t sequence = 0;
  Fixture()
  {
    check(state.establish(owner, 71), "establish exact fixture owner");
  }
  template <class T> OriginalInputRecord record(T payload, std::uint64_t flags = 0)
  {
    ++sequence;
    return {71, sequence, sequence * 100, flags, payload};
  }
  std::optional<NativeOriginalInputState::Prepared> prepare(const OriginalInputRecord &input)
  {
    return state.prepare(owner, input, point(), 640, 480);
  }
  template <class T> OriginalInputRecord send(T payload, std::uint64_t flags = 0)
  {
    const auto input = record(payload, flags);
    const auto pending = prepare(input);
    check(pending && state.pending(pending->token), "stage a valid original record");
    check(state.submitted(pending->token), "record synchronous native submission");
    check(!state.pending(pending->token), "submission is one-shot");
    return input;
  }
};
constexpr auto leftShift = originalKey::shiftFlag | originalKey::leftShiftFlag;
constexpr auto rightShift = originalKey::shiftFlag | originalKey::rightShiftFlag;

void lifecycleExclusion()
{
  using Event = NativeHostLifecycle::Event;
  NativeHostLifecycle life;
  const auto oldOperation = life.admission();
  check(oldOperation && life.current(oldOperation) && !life.cleanup(), "fresh native lifecycle admits work");
  // This represents the notification thread while a native AX/input operation
  // still owns an old token. No owner-thread cleanup has run yet.
  std::thread notification([&] { life.signal(Event::Sleep); });
  notification.join();
  check(!life.current(oldOperation) && !life.admission(), "sleep immediately retires in-flight input and relocation");
  const auto asleep = life.cleanup();
  check(asleep && life.claimQueue(), "first notification schedules restoration");
  check(!life.claimQueue(), "a pending owner task coalesces repeated notifications");
  life.signal(Event::Deactivate);
  const auto inactive = life.cleanup();
  check(inactive != asleep && !life.complete(asleep), "session loss makes old sleep cleanup stale");
  life.signal(Event::Wake);
  const auto awake = life.cleanup();
  check(awake != inactive && !life.complete(inactive), "wake cannot accept earlier restoration completion");
  check(
      life.complete(awake) && !life.cleanup() && !life.admission(),
      "sleep then session loss then wake stays denied after cleanup while session is inactive"
  );
  life.releaseQueue();
  life.signal(Event::Activate);
  const auto active = life.cleanup();
  check(!life.admission() && life.claimQueue(), "activation still requires its own restored generation");
  check(!life.complete(awake) && life.complete(active), "only exact post-activation restoration can reopen");
  life.releaseQueue();
  const auto newOperation = life.admission();
  check(
      newOperation && life.current(newOperation) && !life.current(oldOperation),
      "a restored desktop admits fresh work without reviving an older native operation"
  );
  check(!life.complete(active), "cleanup completion is one-shot");

  // Native restoration may trigger another workspace notification reentrantly.
  // Completing the copied ticket must leave the newer cleanup pending, and
  // releasing the single queue claim must permit exactly one follow-up task.
  life.signal(Event::Sleep);
  const auto restoring = life.cleanup();
  check(life.claimQueue(), "restoration owns one queue slot");
  life.signal(Event::Wake);
  life.signal(Event::Deactivate);
  life.signal(Event::Activate);
  const auto reentered = life.cleanup();
  check(
      !life.claimQueue() && !life.complete(restoring) && !life.admission(),
      "reentrant wake and activation do not reopen an incomplete restore"
  );
  life.releaseQueue();
  check(life.claimQueue() && !life.claimQueue(), "reentrant lifecycle changes schedule one fresh task");
  check(life.complete(reentered) && life.admission(), "latest restored lifecycle can reopen exactly once");
  life.releaseQueue();

  life.signal(Event::Sleep);
  const auto sleeping = life.cleanup();
  check(life.complete(sleeping) && !life.admission(), "successful restoration alone does not clear sleep");
  life.signal(Event::Wake);
  check(!life.admission(), "wake cannot bypass restoration even when there are no exported windows");
  // Failure deliberately leaves this ticket unfinished; a same-state retry
  // can finish it, but no quiet-time or queue release is a cleanup receipt.
  life.releaseQueue();
  check(!life.admission(), "unqueuing failed restoration cannot reopen admission");
  check(life.complete(life.cleanup()) && life.admission(), "explicit completed retry clears only pending cleanup");

  for (const auto terminal : {Event::Emergency, Event::Shutdown, static_cast<Event>(255)}) {
    NativeHostLifecycle stopped;
    const auto operation = stopped.admission();
    stopped.signal(terminal);
    check(
        !stopped.current(operation) && !stopped.admission(), "terminal lifecycle event revokes admission immediately"
    );
    const auto stop = stopped.cleanup();
    stopped.signal(Event::Wake);
    stopped.signal(Event::Activate);
    check(!stopped.complete(stop), "wake/activation cannot accept a stale terminal cleanup");
    check(
        stopped.complete(stopped.cleanup()) && !stopped.admission(),
        "emergency, shutdown and invalid events stay latched after successful cleanup"
    );
  }
  NativeHostLifecycle explicitNewHost;
  check(bool(explicitNewHost.admission()), "explicit new host startup is separate from a terminal instance");
}

void exactAdmission()
{
  NativeOriginalInputState state;
  const auto exact = binding();
  auto other = exact;
  other.scope = ForwardingScope::Keyboard;
  check(!state.establish(other, 71), "keyboard-only fence cannot admit original pointer input");
  check(!state.establish(exact, 0), "missing capture epoch denied");
  check(state.establish(exact, 71), "exact All fence can bind independently authorized endpoint");
  check(state.establish(exact, 71), "idempotent establishment preserves delivered owner");
  check(!state.establish(exact, 72), "same route cannot replace its capture epoch");
  for (int field = 0; field != 6; ++field) {
    auto stale = exact;
    if (field == 0)
      ++stale.owner.session;
    else if (field == 1)
      ++stale.owner.epoch;
    else if (field == 2)
      ++stale.generation;
    else if (field == 3)
      ++stale.focusRequest;
    else if (field == 4)
      ++stale.focusRevision;
    else
      ++stale.geometry;
    check(
        !state.establish(stale, 71) && !state.revoke(stale) && state.current(exact, 71),
        "stale scope component cannot replace or revoke an active exact owner"
    );
  }
  check(state.revoke(exact).has_value(), "exact revoke invalidates owner");
  check(!state.current(exact, 71) && !state.establish(exact, 71), "retired exact binding cannot revive");
  auto newer = exact;
  ++newer.generation;
  check(state.establish(newer, 71), "fresh fence admits a new owner incarnation");
  check(!state.revoke(exact) && state.current(newer, 71), "late revoke cannot drain the replacement");
}

void pressesAndCleanup()
{
  Fixture f;
  const auto down = f.send(key(0, OriginalKeyTransition::Down));
  const auto repeated = f.record(key(0, OriginalKeyTransition::Repeat));
  const auto repeat = f.prepare(repeated);
  check(
      repeat && repeat->press && repeat->press->sequence == down.sequence,
      "repeat retains the first delivered press identity"
  );
  check(f.state.submitted(repeat->token), "repeat submitted once");
  check(!f.state.submitted(repeat->token), "replayed submission token denied");
  f.send(key(0, OriginalKeyTransition::Up));
  check(f.state.empty(), "normal up retires its delivered press");
  const auto empty = f.state.revoke(f.owner);
  check(empty && empty->releases.empty(), "retired physical up is not released twice");

  Fixture modifiers;
  modifiers.send(key(originalKey::leftShift, OriginalKeyTransition::Down), leftShift);
  modifiers.send(key(originalKey::rightShift, OriginalKeyTransition::Down), leftShift | rightShift);
  modifiers.send(key(0, OriginalKeyTransition::Down), leftShift | rightShift);
  modifiers.send(OriginalInputButton{31, true, 7}, leftShift | rightShift);
  const auto cleanup = modifiers.state.revoke(modifiers.owner);
  check(cleanup && cleanup->releases.size() == 4, "cleanup contains exactly the four delivered presses");
  check(
      std::get<OriginalInputKey>(cleanup->releases[0].press.payload).code == 0 &&
          std::get<OriginalInputButton>(cleanup->releases[1].press.payload).button == 31,
      "ordinary key and high mouse button release before modifier sides"
  );
  check(
      cleanup->releases[0].flags == (leftShift | rightShift) &&
          cleanup->releases[1].flags == (leftShift | rightShift) && cleanup->releases[2].flags == rightShift &&
          cleanup->releases[3].flags == 0,
      "cleanup flags preserve the opposite physical modifier until its owned release"
  );
  for (const auto &release : cleanup->releases)
    check(release.owner == modifiers.owner.generation, "all cleanup belongs to one immutable route incarnation");
  check(modifiers.state.empty(), "revocation removes native delivered state without claiming physical neutrality");

  Fixture staged;
  staged.send(key(0, OriginalKeyTransition::Down));
  const auto up = staged.prepare(staged.record(key(0, OriginalKeyTransition::Up)));
  check(up.has_value(), "prepare an up before native submission");
  const auto cancelled = staged.state.revoke(staged.owner);
  check(
      cancelled && cancelled->releases.size() == 1 && !staged.state.submitted(up->token),
      "cancelled unsubmitted up still releases the previously delivered down"
  );

  Fixture notSent;
  const auto preparation = notSent.prepare(notSent.record(key(0, OriginalKeyTransition::Down)));
  check(preparation.has_value(), "prepare a down before native identity callback");
  const auto revoked = notSent.state.revoke(notSent.owner);
  check(
      revoked && revoked->releases.empty() && !notSent.state.pending(preparation->token),
      "revocation before post cannot fabricate cleanup for an undelivered down"
  );
  auto next = notSent.owner;
  ++next.generation;
  check(
      notSent.state.establish(next, 71) && !notSent.state.submitted(preparation->token),
      "older native continuation cannot commit into a replacement owner"
  );
}

void deniedPressTransitions()
{
  for (const auto transition : {OriginalKeyTransition::Up, OriginalKeyTransition::Repeat}) {
    Fixture f;
    check(!f.prepare(f.record(key(0, transition))), "orphan up and repeat never acquire a new owner");
  }
  {
    Fixture f;
    f.send(key(0, OriginalKeyTransition::Down));
    check(!f.prepare(f.record(key(0, OriginalKeyTransition::Down))), "duplicate down denied");
    const auto cleanup = f.state.revoke(f.owner);
    check(cleanup && cleanup->releases.size() == 1, "duplicate cannot overwrite original down");
  }
  {
    Fixture f;
    check(!f.prepare(f.record(OriginalInputMotion{}, leftShift)), "foreign momentary modifier cannot cross owners");
    check(
        !f.prepare(f.record(key(0, OriginalKeyTransition::Down), leftShift)),
        "zero native ledger is not fabricated physical neutrality"
    );
    const auto cleanup = f.state.revoke(f.owner);
    check(cleanup && cleanup->releases.empty(), "denied modifier-bearing down creates no release");
  }
  {
    Fixture f;
    f.send(key(0, OriginalKeyTransition::Down));
    auto up = key(0, OriginalKeyTransition::Up);
    ++up.keyboardType;
    check(!f.prepare(f.record(up)), "keyboard-type substitution cannot release another physical press");
  }
  {
    Fixture f;
    check(!f.prepare(f.record(OriginalInputMotion{12, -19, 2})), "drag needs its exact delivered button down");
    const auto down = f.send(OriginalInputButton{2, true, 4});
    const auto drag = f.prepare(f.record(OriginalInputMotion{12, -19, 2}));
    check(
        drag && drag->press && drag->press->sequence == down.sequence &&
            std::get<OriginalInputMotion>(drag->record.payload).dy == -19,
        "drag retains original button owner and signed deltas without scaling"
    );
    check(f.state.submitted(drag->token), "drag submits to its owned press");
    check(!f.prepare(f.record(OriginalInputButton{2, false, 1})), "button up cannot substitute click identity");
    f.send(OriginalInputButton{2, false, 4});
    check(f.state.empty(), "matching button up retires ownership");
  }
  {
    Fixture f;
    f.send(key(originalKey::capsLock, OriginalKeyTransition::Toggle), originalKey::capsLockFlag);
    check(f.state.empty(), "Caps toggle is preserved without inventing a held momentary key");
    check(!f.prepare(f.record(OriginalInputBoundary::Emergency)), "emergency is not a deliverable event");
    check(!f.prepare(f.record(OriginalInputBoundary::Discontinuity)), "discontinuity is not a deliverable event");
  }
}

void mappingAndOrder()
{
  Fixture f;
  const auto input = f.record(OriginalInputMotion{-7, 13});
  for (int variation = 0; variation != 7; ++variation) {
    auto mapped = point();
    if (variation == 0)
      mapped.deadlineNs = localInputMonotonicNs() - 1;
    else if (variation == 1)
      mapped.deadlineNs += 1000000000;
    else if (variation == 2)
      mapped.x = mapped.width;
    else if (variation == 3)
      mapped.y = -0.25;
    else if (variation == 4)
      mapped.x = std::numeric_limits<double>::quiet_NaN();
    else if (variation == 5)
      ++mapped.width;
    else
      ++mapped.height;
    check(
        !f.state.prepare(f.owner, input, mapped, 640, 480),
        "expired, renewed, outside, nonfinite and stale-size native mappings denied"
    );
  }
  const auto pending = f.prepare(input);
  check(pending && f.state.submitted(pending->token), "denials did not submit or consume the native record");
  check(!f.prepare(input), "duplicate sequence cannot be delivered twice");
  auto backwards = f.record(OriginalInputMotion{});
  backwards.sourceTimestampNs = input.sourceTimestampNs - 1;
  check(!f.prepare(backwards), "backwards source timestamp denied");
  auto wrongEpoch = f.record(OriginalInputMotion{});
  ++wrongEpoch.epoch;
  check(!f.prepare(wrongEpoch), "record from a different capture epoch denied");
  const auto checkpoint = f.prepare(f.record(OriginalInputBoundary::Checkpoint));
  check(
      checkpoint && !checkpoint->posts && f.state.submitted(checkpoint->token),
      "checkpoint advances ordering without constructing or posting OS input"
  );
  f.state.revoke(f.owner);
  ++f.owner.generation;
  check(f.state.establish(f.owner, 71) && !f.prepare(input), "capture replay protection survives route replacement");
  f.state.revoke(f.owner);
  ++f.owner.generation;
  check(!f.state.establish(f.owner, 70), "retired capture epoch cannot return under a new route");
  check(f.state.establish(f.owner, 72), "new capture epoch resets its own sequence domain");
}

OriginalInputScroll scroll(OriginalScrollPhase phase, OriginalMomentumPhase momentum = OriginalMomentumPhase::None)
{
  return {{-3, 7}, {-21, 49}, {-196609, 458751}, phase, momentum, true};
}
void scrollOwnership()
{
  for (const auto orphan :
       {scroll(OriginalScrollPhase::Changed), scroll(OriginalScrollPhase::Ended),
        scroll(OriginalScrollPhase::Cancelled), scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Began),
        scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Continued)}) {
    Fixture f;
    check(!f.prepare(f.record(orphan)), "orphan gesture/momentum phase cannot acquire the endpoint");
  }
  {
    Fixture f;
    f.send(scroll(OriginalScrollPhase::MayBegin));
    const auto began = f.send(scroll(OriginalScrollPhase::Began));
    const auto cleanup = f.state.revoke(f.owner);
    check(
        cleanup && cleanup->scroll && cleanup->scroll->sequence == began.sequence,
        "gesture cleanup retains its delivered record instead of inventing a capture sequence"
    );
    const auto &ending = std::get<OriginalInputScroll>(cleanup->scroll->payload);
    check(
        ending.phase == OriginalScrollPhase::Cancelled && ending.momentum == OriginalMomentumPhase::None &&
            ending.lines == std::array<std::int64_t, 2>{} && ending.points == std::array<std::int64_t, 2>{} &&
            ending.fixed16_16 == std::array<std::int64_t, 2>{},
        "revocation terminates an owned gesture without adding scroll movement"
    );
  }
  {
    Fixture f;
    f.send(scroll(OriginalScrollPhase::Began));
    f.send(scroll(OriginalScrollPhase::Changed));
    f.send(scroll(OriginalScrollPhase::Ended));
    const auto momentum = f.send(scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Began));
    const auto cleanup = f.state.revoke(f.owner);
    check(
        cleanup && cleanup->scroll && cleanup->scroll->sequence == momentum.sequence &&
            std::get<OriginalInputScroll>(cleanup->scroll->payload).momentum == OriginalMomentumPhase::Ended,
        "momentum cleanup ends only the original delivered owner"
    );
  }
  {
    Fixture f;
    f.send(scroll(OriginalScrollPhase::Began));
    f.send(scroll(OriginalScrollPhase::Ended));
    f.send(scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Began));
    f.send(scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Continued));
    f.send(scroll(OriginalScrollPhase::None, OriginalMomentumPhase::Ended));
    const auto cleanup = f.state.revoke(f.owner);
    check(
        cleanup && !cleanup->scroll && cleanup->releases.empty(),
        "completed scrolling needs no duplicate terminal event"
    );
  }
}
} // namespace

int main()
{
  try {
    lifecycleExclusion();
    associatedChildReturn();
    exactAdmission();
    pressesAndCleanup();
    deniedPressTransitions();
    mappingAndOrder();
    scrollOwnership();
    std::cout << "PASS: native original-input owner, mapping, press/drag/modifier cleanup and lifecycle fences\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
