/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalWindowDragCoordinator.h"
#include "LifecycleDiagnostics.h"
#include <QPointer>
#include <QThread>
#include <utility>

namespace deskflow::windowshare {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto holdInterval = std::chrono::milliseconds(100);
constexpr auto hostSilence = std::chrono::milliseconds(350);
constexpr auto maximumDrag = (std::uint64_t{1} << 53) - 1;

LocalWindowDrag command(const WindowDragEdgeCandidate &candidate, std::uint64_t id, LocalInputPin peer)
{
  const auto &edge = candidate.boundary.edge();
  const auto &record = candidate.press.captured.input.record;
  return {
      LocalWindowDrag::Kind::Begin,
      edge.side,
      id,
      edge.server,
      edge.recipient,
      candidate.boundary.generation(),
      record.epoch,
      record.sequence,
      record.sourceTimestampNs,
      edge.motion.sequence,
      edge.peer.incarnation(),
      candidate.press.window(),
      candidate.press.captured.pointer->eventNumber,
      edge.peer.fingerprint(),
      peer,
      edge.source,
      edge.destination,
      edge.sourceX,
      edge.sourceY,
      edge.destinationX,
      edge.destinationY
  };
}
bool exactPress(const LocalWindowPointer &press, const LocalWindowDrag &drag)
{
  return press.kind == LocalWindowPointer::Kind::Down && press.clicks == 1 && press.captureEpoch == drag.captureEpoch &&
         press.sequence == drag.pressSequence && press.sourceTime == drag.pressTime &&
         (drag.returnOffer || press.window == drag.window) && press.eventNumber == drag.eventNumber;
}
} // namespace

struct LocalWindowDragCore::State
{
  QPointer<LocalInputChannel> channel;
  LocalInputPeerPolicy policy;
  Providers providers;
  QTimer timer;
  bool stopped = false, checking = false, holding = false, upMatched = false, upRouted = false, transferring = false;
  std::uint64_t next = 0, lastBoundary = 0, nativeThrough = 0;
  std::optional<WindowDragEdgeCandidate> candidate;
  std::optional<WindowDragEdgeMailbox::Observation> returnBoundary;
  std::optional<OriginalInputCaptured> returnDown;
  bool returnDownObserved = false;
  std::optional<OriginalInputRouted> returnRoute;
  std::optional<LocalWindowMove> returnMove, returnOrigin;
  Clock::time_point returnDeadline{};
  std::uint64_t lastReturnOffer = 0;
  std::optional<LocalWindowDrag> sent;
  std::optional<OriginalInputCaptured> released;
  Clock::time_point deadline{}, lastSent{}, gateLost{};

  bool canHoldMotion() const
  {
    return providers.armMotion && providers.refreshMotion && providers.cancelMotion && providers.mailbox;
  }
  bool admitted()
  {
    if (stopped || checking || !channel || QThread::currentThread() != channel->thread() ||
        !channel->supportsWindowDrag() || !providers.admission)
      return false;
    checking = true;
    bool result = false;
    try {
      const auto check = providers.admission;
      result = check();
    } catch (...) {
    }
    checking = false;
    return result && !stopped && channel && channel->supportsWindowDrag();
  }
  void motionOff()
  {
    holding = false;
    const auto callback = providers.cancelMotion;
    try {
      if (callback)
        callback();
    } catch (...) {
    }
  }
  void reset()
  {
    candidate.reset();
    returnBoundary.reset();
    released.reset();
    sent.reset();
    upMatched = upRouted = transferring = false;
    gateLost = {};
    nativeThrough = 0;
    motionOff();
  }
  void cancel()
  {
    const auto old = sent;
    reset(); // Revoke before calling a borrowed provider or channel callback.
    if (old && providers.mailbox)
      providers.mailbox->cancelRelease(old->boundary);
    if (old && channel && channel->supportsWindowDrag()) {
      auto cancelled = *old;
      cancelled.kind = LocalWindowDrag::Kind::Cancel;
      if (!channel->sendWindowDrag(cancelled) && channel)
        channel->close(QStringLiteral("Window drag cancellation failed"));
    }
  }
  bool send(LocalWindowDrag value)
  {
    sent = value;
    lastSent = Clock::now();
    if (channel && channel->sendWindowDrag(value))
      return !stopped && sent.has_value();
    cancel();
    return false;
  }
  std::optional<WindowDragEdgeMailbox::Release> releaseEvidence() const
  {
    const auto value = providers.mailbox ? providers.mailbox->readRelease() : std::nullopt;
    if (!value || !sent || (!candidate && !returnBoundary) || value->generation() != sent->boundary ||
        !value->edge().sameBoundary(returnBoundary ? returnBoundary->edge() : candidate->boundary.edge()) ||
        (released &&
         value->up() != OriginalInputStamp{released->input.record.epoch, released->input.record.sequence, 1}))
      return {};
    return value;
  }
  bool contextCurrent() const
  {
    if (sent && sent->returnOffer)
      return returnBoundary && returnDown && returnDown->input.live() && returnRoute && returnRoute->live() &&
             returnMove && returnMove->offer == sent->returnOffer && returnMove->window == sent->window &&
             returnMove->phase != LocalWindowMove::Phase::Cancel && Clock::now() < returnDeadline &&
             (returnBoundary->current() || releaseEvidence().has_value());
    return candidate && candidate->press.live() &&
           (candidate->current() || (released && candidate->boundary.current()) || releaseEvidence().has_value());
  }
  void moved(const LocalWindowMove &value)
  {
    if (stopped || !validLocalWindowMove(value))
      return;
    if (value.phase == LocalWindowMove::Phase::Begin) {
      returnDeadline = Clock::now() + WindowDragTransaction::dragBudget;
      returnOrigin = value;
    }
    returnMove = value;
    if (sent && sent->returnOffer &&
        (sent->returnOffer != value.offer || value.phase == LocalWindowMove::Phase::Cancel))
      cancel();
  }
  void beginReturn()
  {
    if (sent || !channel || !channel->supportsWindowMove() || !providers.mailbox || !returnDown ||
        !returnDownObserved || !returnRoute || !returnDown->input.live() || !returnRoute->live() || !returnMove ||
        Clock::now() >= returnDeadline || returnMove->offer <= lastReturnOffer || next == maximumDrag ||
        (returnMove->phase != LocalWindowMove::Phase::Begin && returnMove->phase != LocalWindowMove::Phase::Motion))
      return;
    const auto edge = providers.mailbox->read();
    const auto &record = returnDown->input.record;
    const auto &route = returnRoute->route;
    if (!edge || !edge->edge().returning ||
        edge->edge().press != OriginalInputStamp{record.epoch, record.sequence, 1} ||
        route.outcome != OriginalInputRouteOutcome::Dispatched || !route.pointKnown ||
        route.server != edge->edge().server || route.recipient != edge->edge().recipient ||
        route.ticket.incarnation() != edge->edge().peer.incarnation() ||
        route.ticket.fingerprint() != edge->edge().peer.fingerprint() ||
        policy.windowSharePeer(route.ticket.fingerprint()) != std::optional(returnMove->peer) ||
        (!returnOrigin || returnOrigin->offer != returnMove->offer ||
         !returnOrigin->frame.contains(route.x, route.y)) ||
        !returnDown->pointer || !admitted())
      return;
    lastReturnOffer = returnMove->offer;
    returnBoundary = edge;
    const auto &boundary = edge->edge();
    LocalWindowDrag value{
        LocalWindowDrag::Kind::Begin,
        boundary.side,
        ++next,
        boundary.server,
        boundary.recipient,
        edge->generation(),
        record.epoch,
        record.sequence,
        record.sourceTimestampNs,
        boundary.motion.sequence,
        boundary.peer.incarnation(),
        returnMove->window,
        returnDown->pointer->eventNumber,
        boundary.peer.fingerprint(),
        returnMove->peer,
        boundary.source,
        boundary.destination,
        boundary.sourceX,
        boundary.sourceY,
        boundary.destinationX,
        boundary.destinationY
    };
    value.returnOffer = returnMove->offer;
    deadline = returnDeadline;
    send(value);
  }
  void observe(const std::optional<WindowDragEdgeCandidate> &value)
  {
    if (stopped)
      return;
    if (sent && (Clock::now() >= deadline || (!transferring && !contextCurrent())))
      cancel();
    if (released || transferring || (sent && sent->returnOffer) || !value || !value->live() ||
        value->boundary.edge().returning)
      return;
    if (sent && candidate && candidate->boundary.generation() != value->boundary.generation())
      cancel();
    if (!admitted()) {
      cancel();
      return;
    }
    const auto peer = policy.windowSharePeer(value->boundary.edge().peer.fingerprint());
    if (!peer || !value->live()) {
      cancel();
      return;
    }
    if (!sent) {
      if (value->boundary.generation() <= lastBoundary || next == maximumDrag)
        return;
      lastBoundary = value->boundary.generation();
      candidate = value;
      deadline = Clock::now() + WindowDragTransaction::dragBudget;
      send(command(*value, ++next, *peer));
      return;
    }
    auto moved = command(*value, sent->drag, *peer);
    if (!sent->sameDrag(moved)) {
      cancel();
      return;
    }
    candidate = value;
    // Keep the first painted preview stable until its exact Ready arrives.
    // Otherwise continuous movement supersedes every in-flight acknowledgement
    // and the native-to-preview handoff cannot arm until the user stops moving.
    // One retained candidate coalesces that movement without adding a queue.
    if (!holding && !canHoldMotion() && moved.sequence > sent->sequence) {
      moved.kind = LocalWindowDrag::Kind::Motion;
      send(moved);
    }
  }
  bool finishTransfer()
  {
    if (!transferring || !sent || !providers.mailbox)
      return false;
    const auto result = providers.mailbox->transferResult();
    if (!result || result->generation != sent->boundary ||
        result->up != OriginalInputStamp{sent->captureEpoch, sent->releaseSequence, 1})
      return false;
    if (!result->switched) {
      cancel();
      return true;
    }
    auto finished = *sent;
    finished.kind = LocalWindowDrag::Kind::Finish;
    // Ordinary Core selection has completed. Finish retires only drag state;
    // the exported window and its durable native recovery record stay live.
    reset();
    if (channel && !channel->sendWindowDrag(finished))
      channel->close(QStringLiteral("Window drag completion failed"));
    return true;
  }
  void capture(const OriginalInputCaptured &value)
  {
    finishTransfer();
    const auto &record = value.input.record;
    if (!sent) {
      if (const auto *button = std::get_if<OriginalInputButton>(&record.payload)) {
        returnDown.reset();
        returnDownObserved = false;
        returnRoute.reset();
        if (value.input.live() && button->button == 0 && button->down && button->clickCount == 1 &&
            value.translation.parts == 1 && !value.translation.uncertain && value.pointer &&
            !(record.flags & (originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag |
                              originalKey::commandFlag | originalKey::functionFlag)))
          returnDown = value;
      } else if (!value.input.live() || !std::holds_alternative<OriginalInputMotion>(record.payload)) {
        returnDown.reset();
        returnRoute.reset();
      }
      return;
    }
    if (!value.input.live() || record.epoch != sent->captureEpoch || !admitted())
      return cancel();
    if (record.sequence <= sent->sequence)
      return; // Captures already used by the boundary join.
    if (released)
      return cancel(); // A newer interaction cannot inherit a pending drop.
    if (const auto *motion = std::get_if<OriginalInputMotion>(&record.payload)) {
      const bool queuedNative = !value.windowDragMotionHeld && record.sequence <= nativeThrough;
      if (!value.windowDragMotionHeld && !queuedNative) {
        if (holding) {
          lifecycleDiagnostic(
              "Core window drag", "drag.motion.capture", "failed", "Native movement followed the arming prefix",
              {{"drag", QString::number(sent->drag)},
               {"sequence", QString::number(record.sequence)},
               {"nativeThrough", QString::number(nativeThrough)}}
          );
          cancel();
        }
        return;
      }
      // Primary-screen motion samples the live cursor for legacy translation,
      // so that translation is explicitly uncertain and may emit no part at a
      // clamped edge. The exact pre-arm raw record still describes movement;
      // it updates only the preview, never input routing or source ownership.
      const bool translationValid =
          value.translation.stamp == OriginalInputStamp{record.epoch, record.sequence, 0} &&
          (queuedNative ? value.translation.parts <= 1 : !value.translation.parts && !value.translation.uncertain);
      if (!holding || !contextCurrent() || motion->dragButton != 0 || !translationValid ||
          (record.flags & (originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag |
                           originalKey::commandFlag | originalKey::functionFlag)))
        return cancel();
      // Account for each queued pre-arm delta once, just like later held
      // movement. A newer boundary observation cannot replay it because sent
      // retains its original sequence. Every real up still follows its joins.
      auto moved = *sent;
      moved.kind = LocalWindowDrag::Kind::Motion;
      moved.sequence = record.sequence;
      const auto &screen = moved.destination;
      const auto delta = [](std::int64_t n) { return std::clamp(n, std::int64_t(INT32_MIN), std::int64_t(INT32_MAX)); };
      const auto x = std::int64_t(moved.destinationX) + delta(motion->dx);
      const auto y = std::int64_t(moved.destinationY) + delta(motion->dy);
      const auto right = std::int64_t(screen.x) + screen.width - 1;
      const auto bottom = std::int64_t(screen.y) + screen.height - 1;
      if ((moved.side == WindowDragEdgeSide::Right && x < screen.x) ||
          (moved.side == WindowDragEdgeSide::Left && x > right) ||
          (moved.side == WindowDragEdgeSide::Bottom && y < screen.y) ||
          (moved.side == WindowDragEdgeSide::Top && y > bottom))
        return cancel(); // Crossing back resumes the original native drag.
      moved.destinationX = static_cast<std::int32_t>(std::clamp(x, std::int64_t(screen.x), right));
      moved.destinationY = static_cast<std::int32_t>(std::clamp(y, std::int64_t(screen.y), bottom));
      send(moved);
      return;
    }
    const auto *button = std::get_if<OriginalInputButton>(&record.payload);
    if (!holding || !button || button->button != 0 || button->down || value.translation.parts != 1 ||
        value.translation.uncertain || !value.pointer ||
        (record.flags & (originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag |
                         originalKey::commandFlag | originalKey::functionFlag)))
      return cancel();
    released = value;
    deadline = std::min(deadline, Clock::now() + WindowDragTransaction::dropBudget);
    motionOff(); // Real up was never suppressed; await its two independent joins.
  }
  void pointer(const OriginalPointerJoin::Match &value)
  {
    // Session enqueues this metadata to Host before invoking us. Remote input
    // is consumed at HID and therefore may have no application annotation;
    // the bounded join still publishes its exact raw stamp with window=0.
    if (!sent && returnDown && value.live() && value.captured.input.record.epoch == returnDown->input.record.epoch &&
        value.captured.input.record.sequence == returnDown->input.record.sequence)
      returnDownObserved = true;
    if (!sent || !released || !value.live())
      return;
    const auto &record = value.captured.input.record;
    if (record.epoch != sent->captureEpoch || record.sequence != released->input.record.sequence)
      return;
    if ((!sent->returnOffer && value.window() != sent->window) || !value.captured.pointer ||
        value.captured.pointer->eventNumber != sent->eventNumber)
      return cancel();
    upMatched = true;
    tryDrop();
  }
  void routed(const OriginalInputRouted &value)
  {
    if (!sent && returnDown && value.input.record.epoch == returnDown->input.record.epoch &&
        value.input.record.sequence == returnDown->input.record.sequence) {
      if (value.live() && value.route.kind == OriginalInputRouteKind::Button &&
          value.route.stamp == OriginalInputStamp{value.input.record.epoch, value.input.record.sequence, 1} &&
          value.route.outcome == OriginalInputRouteOutcome::Dispatched)
        returnRoute = value;
      else {
        returnDown.reset();
        returnRoute.reset();
      }
    }
    if (!sent || !released || value.input.record.epoch != sent->captureEpoch ||
        value.input.record.sequence != released->input.record.sequence)
      return;
    if (!value.live() || value.route.stamp != OriginalInputStamp{sent->captureEpoch, value.input.record.sequence, 1} ||
        value.route.server != sent->server || value.route.kind != OriginalInputRouteKind::Button ||
        value.route.outcome !=
            (sent->returnOffer ? OriginalInputRouteOutcome::Dispatched : OriginalInputRouteOutcome::Local) ||
        (sent->returnOffer &&
         (value.route.recipient != sent->recipient || value.route.ticket.incarnation() != sent->coreConnection ||
          value.route.ticket.fingerprint() != sent->corePin)))
      return cancel();
    upRouted = true;
    tryDrop();
  }
  void tryDrop()
  {
    if (!sent || !released || !upMatched || !upRouted || sent->kind == LocalWindowDrag::Kind::Drop)
      return;
    if (!releaseEvidence() || !admitted() || Clock::now() >= deadline)
      return cancel();
    auto drop = *sent;
    drop.kind = LocalWindowDrag::Kind::Drop;
    drop.releaseSequence = released->input.record.sequence;
    drop.releaseTime = released->input.record.sourceTimestampNs;
    send(drop);
  }
  void status(const LocalWindowDragStatus &value)
  {
    if (!sent || !sent->sameDrag(value.command))
      return;
    if (value.kind == LocalWindowDragStatus::Kind::Cancelled)
      return cancel();
    if (value.kind == LocalWindowDragStatus::Kind::Ready && released)
      return; // A pre-up paint acknowledgement can already be in the FIFO.
    if (value.command.sequence != sent->sequence)
      return;
    auto expected = *sent;
    expected.kind = value.command.kind;
    if (expected != value.command || !admitted() || Clock::now() >= deadline)
      return cancel();
    if (value.kind == LocalWindowDragStatus::Kind::Ready) {
      if (holding || released || !canHoldMotion())
        return;
      if (!contextCurrent())
        return cancel();
      holding = true; // Reserve before an injected provider can destroy us.
      const auto requested = *sent;
      const auto arm = providers.armMotion;
      std::optional<OriginalInputStamp> armed;
      try {
        armed = arm({sent->captureEpoch, sent->pressSequence, 1});
      } catch (...) {
      }
      if (!armed || armed->epoch != requested.captureEpoch || armed->part || armed->sequence < requested.sequence ||
          !admitted() || stopped || !holding || !sent || *sent != requested || !contextCurrent() ||
          Clock::now() >= deadline) {
        lifecycleDiagnostic(
            "Core window drag", "drag.motion.arm", "failed", "Preview lost its exact capture/owner admission",
            {{"drag", QString::number(requested.drag)},
             {"previewSequence", QString::number(requested.sequence)},
             {"prefixReturned", armed.has_value()},
             {"nativeThrough", QString::number(armed ? armed->sequence : 0)}}
        );
        return cancel();
      }
      nativeThrough = armed->sequence;
      if (candidate) {
        auto latest = command(*candidate, sent->drag, sent->windowSharePin);
        if (!sent->sameDrag(latest) || latest.sequence > nativeThrough)
          return cancel();
        if (latest.sequence > sent->sequence) {
          latest.kind = LocalWindowDrag::Kind::Motion;
          send(latest);
        }
      }
      if (sent)
        lifecycleDiagnostic(
            "Core window drag", "drag.motion.arm", "ready", "Exact preview accepted; captured motion may continue",
            {{"drag", QString::number(sent->drag)},
             {"previewSequence", QString::number(requested.sequence)},
             {"latestSequence", QString::number(sent->sequence)},
             {"nativeThrough", QString::number(nativeThrough)}}
        );
    } else if (value.kind == LocalWindowDragStatus::Kind::Committed) {
      if (transferring)
        return;
      const auto release = releaseEvidence();
      if (sent->kind != LocalWindowDrag::Kind::Drop || !release || !released || !upMatched || !upRouted)
        return cancel();
      transferring = true;
      if (!providers.mailbox->requestTransfer(*release, sent->destinationX, sent->destinationY))
        cancel();
    }
  }
  void poll()
  {
    if (finishTransfer())
      return;
    if (!sent) {
      beginReturn();
      return;
    }
    if (Clock::now() >= deadline || !admitted() ||
        (sent->returnOffer ? (!returnDown || !returnDown->input.live()) : (!candidate || !candidate->press.live())))
      return cancel();
    if (transferring)
      return; // Server may be inside the synchronous selection transition.
    if (!contextCurrent())
      return cancel();
    if (released) {
      tryDrop();
      return;
    }
    if (holding) {
      const auto refresh = providers.refreshMotion;
      bool refreshed = false;
      try {
        refreshed = refresh && refresh({sent->captureEpoch, sent->pressSequence, 1});
      } catch (...) {
      }
      if (!sent || stopped)
        return;
      if (!refreshed) {
        // Up can reach the producer before the sole consumer drains it. Never
        // rearm; allow that bounded drain to identify up or cancel on new input.
        if (gateLost == Clock::time_point{})
          gateLost = Clock::now() + OriginalInputRouteJoin::timeout;
        if (Clock::now() >= gateLost)
          return cancel();
      }
    }
    if (sent && Clock::now() - lastSent >= holdInterval) {
      auto held = *sent;
      held.kind = LocalWindowDrag::Kind::Hold;
      send(held);
    }
  }
  void stop()
  {
    stopped = true;
    timer.stop();
    cancel();
    providers = {};
  }
};
LocalWindowDragCore::LocalWindowDragCore(
    LocalInputChannel &channel, LocalInputPeerPolicy policy, std::function<bool()> admission
)
    : LocalWindowDragCore(channel, std::move(policy), Providers{std::move(admission), {}, {}, {}, {}})
{
}
LocalWindowDragCore::LocalWindowDragCore(LocalInputChannel &channel, LocalInputPeerPolicy policy, Providers providers)
    : m_state(std::make_shared<State>())
{
  m_state->channel = &channel;
  m_state->policy = std::move(policy);
  m_state->providers = std::move(providers);
  const std::weak_ptr<State> weak = m_state;
  channel.onWindowDragStatus = [weak](const LocalWindowDragStatus &status) {
    if (const auto state = weak.lock())
      state->status(status);
  };
  channel.onWindowMove = [weak](const LocalWindowMove &move) {
    if (const auto state = weak.lock())
      state->moved(move);
  };
  QObject::connect(
      &m_state->timer, &QTimer::timeout, &m_state->timer,
      [weak] {
        if (const auto state = weak.lock())
          state->poll();
      },
      Qt::QueuedConnection
  );
  m_state->timer.start(25);
}
LocalWindowDragCore::~LocalWindowDragCore()
{
  stop();
}
void LocalWindowDragCore::observe(const std::optional<WindowDragEdgeCandidate> &candidate)
{
  const auto state = m_state;
  state->observe(candidate);
}
void LocalWindowDragCore::capture(const OriginalInputCaptured &captured)
{
  const auto state = m_state;
  state->capture(captured);
}
void LocalWindowDragCore::pointer(const OriginalPointerJoin::Match &match)
{
  const auto state = m_state;
  state->pointer(match);
}
void LocalWindowDragCore::routed(const OriginalInputRouted &routed)
{
  const auto state = m_state;
  state->routed(routed);
}
void LocalWindowDragCore::moved(const LocalWindowMove &move)
{
  const auto state = m_state;
  state->moved(move);
}
void LocalWindowDragCore::stop()
{
  const auto state = m_state;
  state->stop();
}

struct LocalWindowDragHost::State
{
  QPointer<LocalInputChannel> channel;
  LocalInputPeerPolicy policy;
  Providers providers;
  QTimer timer;
  bool stopped = false, checking = false, committed = false, dropRequested = false;
  std::uint64_t nextMove = 0;
  std::optional<LocalWindowMove> nativeMove;
  std::optional<LocalWindowPointer> press;
  std::optional<LocalWindowPointer> release;
  std::optional<LocalWindowDrag> current;
  std::optional<AuthenticatedPeerObservation> connection;
  std::optional<WindowDragBinding> binding;
  Clock::time_point deadline{}, lastRead{};

  bool admitted()
  {
    if (stopped || checking || !channel || QThread::currentThread() != channel->thread() ||
        !channel->supportsWindowDrag() || !providers.admission || !providers.connection)
      return false;
    checking = true;
    bool result = false;
    try {
      const auto check = providers.admission;
      const auto read = providers.connection;
      result = check();
      const auto now = !stopped && result ? read() : std::nullopt;
      result = now && now->live() && current && current->windowSharePin == now->ticket().fingerprint() &&
               (!connection || connection->sameConnection(*now));
      if (result)
        connection = now;
    } catch (...) {
      result = false;
    }
    checking = false;
    return result && !stopped && channel && channel->supportsWindowDrag() && current && press &&
           exactPress(*press, *current) &&
           (!current->returnOffer ||
            (channel->supportsWindowMove() && nativeMove && nativeMove->offer == current->returnOffer &&
             nativeMove->window == current->window && nativeMove->peer == current->windowSharePin &&
             nativeMove->phase != LocalWindowMove::Phase::Cancel)) &&
           Clock::now() < deadline && (release || Clock::now() - lastRead < hostSilence);
  }
  void cancel()
  {
    const auto old = std::exchange(binding, {});
    const auto previous = std::exchange(current, {});
    connection.reset();
    release.reset();
    committed = dropRequested = false;
    if (old) {
      const auto callback = providers.cancel;
      try {
        if (callback)
          callback(*old);
      } catch (...) {
        if (channel)
          channel->close(QStringLiteral("Window drag cleanup failed"));
      }
    }
    if (previous && channel && channel->supportsWindowDrag())
      channel->sendWindowDragStatus({LocalWindowDragStatus::Kind::Cancelled, *previous});
  }
  void receive(const LocalWindowDrag &value)
  {
    if (stopped)
      return;
    if (value.kind == LocalWindowDrag::Kind::Cancel) {
      cancel();
      return;
    }
    if (value.kind == LocalWindowDrag::Kind::Begin) {
      cancel();
      if (stopped || !press || !exactPress(*press, value) ||
          policy.windowSharePeer(value.corePin) != std::optional(value.windowSharePin) || !providers.prepare ||
          !providers.cancel ||
          (value.returnOffer && (!nativeMove || nativeMove->offer != value.returnOffer ||
                                 (nativeMove->phase != LocalWindowMove::Phase::Begin &&
                                  nativeMove->phase != LocalWindowMove::Phase::Motion))))
        return;
      current = value;
      lastRead = Clock::now();
      deadline = lastRead + WindowDragTransaction::dragBudget;
      if (!admitted()) {
        cancel();
        return;
      }
      binding = WindowDragBinding{
          value.drag,
          connection->ticket().incarnation(),
          value.boundary,
          value.captureEpoch,
          value.pressSequence,
          value.pressTime,
          value.window,
          value.eventNumber,
          value.returnOffer ? WindowDragDirection::Return : WindowDragDirection::Export
      };
      const auto requested = *binding;
      const auto prepare = providers.prepare;
      const auto cleanup = providers.cancel;
      bool prepared = false;
      try {
        prepared = prepare(requested, value);
      } catch (...) {
      }
      if (!prepared || binding != requested || !admitted()) {
        if (binding == requested)
          cancel();
        else {
          // A callback can stop this owner before returning a newly-created
          // preview. The exact late result still belongs to this request.
          try {
            cleanup(requested);
          } catch (...) {
            if (channel)
              channel->close(QStringLiteral("Late window drag cleanup failed"));
          }
        }
      }
      return;
    }
    if (!current || !binding || !current->sameDrag(value))
      return; // A denied/expired preparation is never revived by held traffic.
    // Test the old deadline BEFORE refreshing it; late traffic cannot revive.
    if (!admitted()) {
      cancel();
      return;
    }
    current = value;
    lastRead = Clock::now();
    if (value.kind == LocalWindowDrag::Kind::Finish) {
      if (!committed || !release || !providers.finish)
        return cancel();
      const auto expected = *binding;
      const auto callback = providers.finish;
      // Finish cannot roll back a cursor transfer that already completed.
      binding.reset();
      current.reset();
      connection.reset();
      release.reset();
      press.reset();
      committed = false;
      try {
        callback(expected);
      } catch (...) {
        if (channel)
          channel->close(QStringLiteral("Window drag completion cleanup failed"));
        return;
      }
      if (channel)
        channel->sendWindowDragStatus({LocalWindowDragStatus::Kind::Finished, value});
      return;
    }
    if (value.kind == LocalWindowDrag::Kind::Drop) {
      if (!release || release->sequence != value.releaseSequence || release->sourceTime != value.releaseTime ||
          !providers.drop)
        return cancel();
      tryDrop();
      return;
    }
    if (release)
      return cancel(); // Held traffic cannot revive a released native owner.
    if ((value.kind == LocalWindowDrag::Kind::Motion || value.kind == LocalWindowDrag::Kind::Hold) &&
        providers.motion) {
      const auto callback = providers.motion;
      const auto expected = *binding;
      try {
        callback(expected, value);
      } catch (...) {
        cancel();
        return;
      }
      if (binding == expected && !admitted())
        cancel();
    }
  }
  void tryDrop()
  {
    if (dropRequested || !current || current->kind != LocalWindowDrag::Kind::Drop || !binding || !release ||
        !providers.drop || !admitted())
      return;
    if (current->returnOffer && (!nativeMove || nativeMove->phase != LocalWindowMove::Phase::End))
      return;
    dropRequested = true;
    const auto expected = *binding;
    const auto command = *current;
    const auto callback = providers.drop;
    const WindowDragRelease up{
        release->captureEpoch, release->sequence, release->sourceTime, command.window, release->eventNumber
    };
    bool accepted = false;
    try {
      accepted = callback(expected, command, up);
    } catch (...) {
    }
    if (!accepted || (binding == expected && !admitted()))
      cancel();
  }
  void moved(LocalWindowMove value)
  {
    if (stopped || !channel || !channel->supportsWindowMove() || !providers.connection)
      return;
    const auto read = providers.connection;
    std::optional<AuthenticatedPeerObservation> peer;
    try {
      peer = read();
    } catch (...) {
      cancel();
    }
    if (stopped || !channel || !peer || !peer->live())
      return;
    value.peer = peer->ticket().fingerprint();
    if (value.phase == LocalWindowMove::Phase::Begin) {
      if (nativeMove &&
          (nativeMove->phase == LocalWindowMove::Phase::Begin || nativeMove->phase == LocalWindowMove::Phase::Motion)) {
        auto cancelled = *nativeMove;
        cancelled.phase = LocalWindowMove::Phase::Cancel;
        channel->sendWindowMove(cancelled);
        if (stopped || !channel)
          return;
        if (current && current->returnOffer)
          cancel();
        if (stopped || !channel)
          return;
      }
      if (nextMove == maximumDrag)
        return;
      value.offer = ++nextMove;
    } else {
      if (!nativeMove)
        return;
      value.offer = nativeMove->offer;
      if (!value.sameMove(*nativeMove))
        return;
    }
    if (!validLocalWindowMove(value))
      return;
    nativeMove = value;
    if (!channel->sendWindowMove(value)) {
      cancel();
      if (channel)
        channel->close(QStringLiteral("Native move observation failed"));
      return;
    }
    if (stopped)
      return;
    if (current && current->returnOffer && value.phase == LocalWindowMove::Phase::Cancel)
      cancel();
    else
      tryDrop();
  }
  void pointer(const LocalWindowPointer &value)
  {
    if (!stopped && current && binding && press && providers.drop && validLocalWindowPointer(value) &&
        value.kind == LocalWindowPointer::Kind::Up && value.captureEpoch == current->captureEpoch &&
        value.sequence > current->sequence && value.sourceTime > current->pressTime &&
        (current->returnOffer || value.window == current->window) && value.eventNumber == current->eventNumber &&
        !(value.flags & (originalKey::shiftFlag | originalKey::controlFlag | originalKey::optionFlag |
                         originalKey::commandFlag | originalKey::functionFlag)) &&
        !release && admitted()) {
      release = value;
      deadline = std::min(deadline, Clock::now() + WindowDragTransaction::dropBudget);
      return;
    }
    cancel();
    press.reset();
    if (!stopped && validLocalWindowPointer(value) && value.kind == LocalWindowPointer::Kind::Down &&
        (value.window || (channel && channel->supportsWindowMove())) && value.clicks == 1)
      press = value;
  }
  void ready(const WindowDragBinding &value, std::uint64_t sequence)
  {
    if (binding != value || !current || release || sequence != current->sequence || !admitted())
      return;
    if (channel && !channel->sendWindowDragStatus({LocalWindowDragStatus::Kind::Ready, *current}))
      cancel();
  }
  void commit(const WindowDragBinding &value)
  {
    if (binding != value || !current || current->kind != LocalWindowDrag::Kind::Drop || !release || committed ||
        !admitted())
      return;
    committed = true;
    if (channel && !channel->sendWindowDragStatus({LocalWindowDragStatus::Kind::Committed, *current}))
      cancel();
  }
  void stop()
  {
    stopped = true;
    timer.stop();
    press.reset();
    cancel();
  }
};
LocalWindowDragHost::LocalWindowDragHost(LocalInputChannel &channel, LocalInputPeerPolicy policy, Providers providers)
    : m_state(std::make_shared<State>())
{
  m_state->channel = &channel;
  m_state->policy = std::move(policy);
  m_state->providers = std::move(providers);
  const std::weak_ptr<State> weak = m_state;
  channel.onWindowDrag = [weak](const LocalWindowDrag &drag) {
    if (const auto state = weak.lock())
      state->receive(drag);
  };
  QObject::connect(
      &m_state->timer, &QTimer::timeout, &m_state->timer,
      [weak] {
        if (const auto state = weak.lock(); state && state->current && !state->admitted())
          state->cancel();
      },
      Qt::QueuedConnection
  );
  m_state->timer.start(25);
}
LocalWindowDragHost::~LocalWindowDragHost()
{
  stop();
}
void LocalWindowDragHost::pointer(const LocalWindowPointer &pointer)
{
  const auto state = m_state;
  state->pointer(pointer);
}
void LocalWindowDragHost::stop()
{
  const auto state = m_state;
  state->stop();
}
void LocalWindowDragHost::ready(const WindowDragBinding &binding, std::uint64_t sequence)
{
  const auto state = m_state;
  state->ready(binding, sequence);
}
void LocalWindowDragHost::committed(const WindowDragBinding &binding)
{
  const auto state = m_state;
  state->commit(binding);
}
void LocalWindowDragHost::moved(LocalWindowMove value)
{
  const auto state = m_state;
  state->moved(std::move(value));
}
void LocalWindowDragHost::cancelled(const WindowDragBinding &binding)
{
  const auto state = m_state;
  if (state->binding == binding)
    state->cancel();
}
} // namespace deskflow::windowshare
