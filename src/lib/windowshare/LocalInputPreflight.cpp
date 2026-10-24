/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputPreflight.h"
#include "LifecycleDiagnostics.h"
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <deque>
#include <limits>
#include <stdexcept>
#include <utility>

namespace deskflow::windowshare {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto preflightTimeout = std::chrono::seconds(2);
bool nonzero(const auto &bytes)
{
  return std::any_of(bytes.begin(), bytes.end(), [](auto value) { return value != 0; });
}
std::uint64_t nextGeneration()
{
  static std::atomic<std::uint64_t> next{1};
  auto value = next.load(std::memory_order_relaxed);
  while (value != 0) {
    const auto successor = value == std::numeric_limits<std::uint64_t>::max() ? 0 : value + 1;
    if (next.compare_exchange_weak(value, successor, std::memory_order_relaxed))
      return value;
  }
  return 0;
}
KeyboardRouteBinding keyboardBinding(const LocalInputLease &lease)
{
  return {{lease.session, lease.epoch}, lease.keyboardRouteGeneration, lease.focusRequest,
          lease.focusRevision,          lease.geometryRevision,        lease.scope};
}
bool canonical(const LocalInputIncarnation &incarnation)
{
  return std::all_of(incarnation.begin(), incarnation.begin() + 8, [](auto value) { return value == 0; }) &&
         nonzero(incarnation);
}
} // namespace

std::optional<LocalInputPeerPolicy> LocalInputPeerPolicy::create(std::span<const LocalInputPeerPair> pairs)
{
  if (pairs.size() > maximumPairs)
    return std::nullopt;
  LocalInputPeerPolicy result;
  for (const auto &pair : pairs) {
    if (!nonzero(pair.deskflow) || !nonzero(pair.windowShare) ||
        std::any_of(result.m_pairs.begin(), result.m_pairs.begin() + result.m_size, [&](const auto &old) {
          return old.deskflow == pair.deskflow || old.windowShare == pair.windowShare;
        }))
      return std::nullopt;
    result.m_pairs[result.m_size++] = pair;
  }
  return result;
}
std::optional<LocalInputPin> LocalInputPeerPolicy::windowSharePeer(const LocalInputPin &deskflow) const
{
  for (std::size_t i = 0; i < m_size; ++i)
    if (m_pairs[i].deskflow == deskflow)
      return m_pairs[i].windowShare;
  return std::nullopt;
}
bool LocalInputPeerPolicy::permits(const LocalInputOffer &offer) const
{
  const auto pin = windowSharePeer(offer.deskflowPeerPin);
  return validLocalInputOffer(offer) && pin && *pin == offer.windowSharePeerPin;
}
LocalInputIncarnation localInputConnectionIncarnation(std::uint64_t counter)
{
  LocalInputIncarnation result{};
  for (int i = 15; i >= 8; --i) {
    result[i] = static_cast<std::uint8_t>(counter);
    counter >>= 8;
  }
  return result;
}

struct LocalInputPreflight::State final : QObject, std::enable_shared_from_this<State>
{
  friend struct LocalInputPreflightTestAccess;
  enum class Kind
  {
    Offer,
    Candidate,
    InputTarget,
    Quiesced,
    Cancel,
    Prepare,
    Prepared,
    Activated,
    LocalQuiesced,
    ContinuePreparation
  };
  struct Control
  {
    Kind kind;
    LocalInputOffer offer{};
    LocalInputLease lease{};
    LocalInputRevocation reason = LocalInputRevocation::Requested;
    LocalInputTarget target{};
  };
  struct Attempt
  {
    // Local scheduling only; reaching Started still requires every Check.
    enum class Continuation
    {
      Waiting,
      Queued,
      Started
    };
    LocalInputOffer offer;
    std::optional<LocalInputLease> lease;
    std::optional<SelectedPeerMailbox::Observation> core;
    std::optional<AuthenticatedPeerObservation> host;
    OriginalInputEpoch capture; // Core-local only; host retains its numeric attestation in offer.
    Clock::time_point deadline = Clock::now() + preflightTimeout;
    Phase phase = Phase::Offered;
    bool hostActivationAttempted = false, hostActivated = false;
    bool targetReceived = false;
    Continuation continuation = Continuation::Waiting;
  };
  QPointer<LocalInputChannel> channel;
  LocalInputRole role;
  LocalInputPeerPolicy policy;
  Checks checks;
  PreparationNotification preparation;
  TargetNotification target;
  ProgressNotification progress;
  Host host;
  std::shared_ptr<SelectedPeerMailbox> mailbox;
  std::optional<Attempt> attempt;
  std::optional<LocalInputOffer> retired;
  std::optional<LocalInputLease> retiredLease;
  std::uint64_t highWater = 0, captureHighWater = 0;
  std::deque<Control> controls;
  QTimer timer;
  bool running = true, scheduled = false, authorizing = false, processing = false, cancelling = false,
       reserving = false;

  State(LocalInputChannel &input, LocalInputRole endpoint, LocalInputPeerPolicy approved, Checks gate)
      : channel(&input),
        role(endpoint),
        policy(std::move(approved)),
        checks(std::move(gate))
  {
    if (input.thread() != QThread::currentThread() || input.authorize || input.onOffer || input.onCandidate ||
        input.onQuiesced || input.onOfferCancelled || input.onPrepare || input.onPrepared || input.onActivated ||
        input.onInputTarget)
      throw std::invalid_argument("Preflight requires an unowned channel on its Qt thread");
  }
  bool current(const LocalInputOffer &offer) const
  {
    return running && attempt && attempt->offer == offer;
  }
  void fail(const QString &reason)
  {
    cancel(LocalInputRevocation::AuthorityLost, false);
    if (channel)
      channel->close(reason);
  }
  // No callback runs without retaining State. Owner destruction sets running
  // false; no callback can subsequently grant authority or revive an attempt.
  void attach()
  {
    const auto weak = weak_from_this();
    channel->onOffer = [weak](const auto &offer) {
      if (auto self = weak.lock())
        self->dispatch({Kind::Offer, offer});
    };
    channel->onCandidate = [weak](const auto &lease) {
      if (auto self = weak.lock())
        self->dispatch({Kind::Candidate, leaseOffer(lease), lease});
    };
    channel->onInputTarget = [weak](const auto &lease, const auto &mappedTarget) {
      if (auto self = weak.lock())
        self->dispatch({Kind::InputTarget, leaseOffer(lease), lease, LocalInputRevocation::Requested, mappedTarget});
    };
    channel->onQuiesced = [weak](const auto &lease) {
      if (auto self = weak.lock())
        self->dispatch({Kind::Quiesced, leaseOffer(lease), lease});
    };
    channel->onOfferCancelled = [weak](const auto &offer, auto reason) {
      if (auto self = weak.lock())
        self->dispatch({Kind::Cancel, offer, {}, reason});
    };
    channel->onPrepare = [weak](const auto &lease) {
      if (auto self = weak.lock())
        self->dispatch({Kind::Prepare, leaseOffer(lease), lease});
    };
    channel->onPrepared = [weak](const auto &lease) {
      if (auto self = weak.lock())
        self->dispatch({Kind::Prepared, leaseOffer(lease), lease});
    };
    channel->onActivated = [weak](const auto &lease) {
      if (auto self = weak.lock())
        self->dispatch({Kind::Activated, leaseOffer(lease), lease});
    };
    const auto previousRevoke = channel->onRevoked;
    const QPointer<LocalInputChannel> alive = channel;
    channel->onRevoked = [weak, previousRevoke, alive](const auto &lease, auto reason) {
      if (auto self = weak.lock())
        self->cancel(reason, true);
      if (alive && previousRevoke)
        previousRevoke(lease, reason);
    };
    const auto previousClose = channel->onClosed;
    channel->onClosed = [weak, previousClose, alive](QString reason) {
      if (auto self = weak.lock())
        self->stop();
      if (alive && previousClose)
        previousClose(std::move(reason));
    };
    channel->authorize = [weak](const auto &lease, auto check) {
      if (auto self = weak.lock())
        return self->permits(lease, check);
      return false;
    };
    timer.setInterval(20);
    QObject::connect(
        &timer, &QTimer::timeout, this,
        [weak] {
          if (auto self = weak.lock())
            self->tick();
        },
        Qt::QueuedConnection
    );
    timer.start();
  }
  void stop()
  {
    if (!running)
      return;
    running = false;
    timer.stop();
    progress = {};
    controls.clear();
    cancel(LocalInputRevocation::Requested, true);
  }
  void cancel(LocalInputRevocation reason, bool notify)
  {
    if (!attempt)
      return;
    const auto old = std::exchange(attempt, std::nullopt);
    lifecycleDiagnostic(
        role == LocalInputRole::Host ? "Host preflight" : "Core preflight", "input.preflight", "cancelled",
        "Input preflight released its attempt",
        {{"routeGeneration", QString::number(old->offer.routeGeneration)},
         {"phase", int(old->phase)},
         {"reason", int(reason)},
         {"notifyPeer", notify},
         {"hostActivated", old->hostActivated},
         {"preparationBudgetRemainingMs",
          qint64(std::chrono::duration_cast<std::chrono::milliseconds>(old->deadline - Clock::now()).count())}}
    );
    retired = old->offer;
    retiredLease = old->lease;
    // Reset before any callback; all synchronous/nested authorizers now deny.
    const bool wasCancelling = std::exchange(cancelling, true);
    if (channel)
      channel->revoke(reason);
    bool released = !old->lease;
    if (old->lease) {
      try {
        const auto release = checks.release;
        released = release && release(*old->lease, reason);
      } catch (...) {
        released = false;
      }
    }
    if (channel && !released)
      channel->close("Preflight owner release failed");
    else if (channel && notify && channel->authenticated() && !channel->cancelOffer(old->offer, reason) && channel)
      channel->close("Preflight cancellation could not be delivered");
    // A declined offer never acquired native or Viewer ownership. A reserve
    // callback still on the stack may return an owned binding; its exact
    // asynchronous release, rather than this cancellation, must acknowledge it.
    if (channel && role == LocalInputRole::Host && !old->lease && !reserving &&
        old->offer.scope == ForwardingScope::All && channel->supportsMappedInput())
      (void)channel->sendReleased(old->offer);
    cancelling = wasCancelling;
    schedule();
  }
  bool live(bool refresh = true)
  {
    if (!running || !attempt || !channel || !channel->authenticated())
      return false;
    const auto offer = attempt->offer;
    const auto withinDeadline = [&] {
      return current(offer) && (attempt->phase == Phase::Active || Clock::now() < attempt->deadline);
    };
    if (!withinDeadline() || !policy.permits(offer))
      return false;
    if (role == LocalInputRole::Core) {
      if (!attempt->capture.live() || attempt->capture.id() != offer.captureEpoch || !attempt->core ||
          !attempt->core->live() || !mailbox)
        return false;
      const auto latest = mailbox->read();
      if (!current(offer) || !attempt->capture.live() || !attempt->core->live() || latest.closed || !latest.selected ||
          !latest.selected->live() || !latest.selected->peer().sameSelection(attempt->core->peer()))
        return false;
      if (refresh)
        attempt->core = latest.selected;
      const auto &peer = attempt->core->peer();
      return withinDeadline() && attempt->capture.live() && attempt->capture.id() == offer.captureEpoch &&
             peer.serverIncarnation() == offer.coreServerIncarnation && peer.recipient() == offer.coreRecipient &&
             peer.generation() == offer.coreSelectionGeneration &&
             peer.ticket().fingerprint() == offer.deskflowPeerPin &&
             localInputConnectionIncarnation(peer.ticket().incarnation()) == offer.deskflowClientIncarnation;
    }
    if (!attempt->host || !attempt->host->live() || !host.connection)
      return false;
    std::optional<AuthenticatedPeerObservation> latest;
    try {
      const auto read = host.connection;
      latest = read();
    } catch (...) {
      return false;
    }
    if (!withinDeadline() || !attempt->host->live() || !latest || !latest->live() ||
        !latest->sameConnection(*attempt->host) || latest->ticket().fingerprint() != offer.windowSharePeerPin)
      return false;
    if (refresh)
      attempt->host = latest;
    return !attempt->lease || attempt->lease->windowShareClientIncarnation ==
                                  localInputConnectionIncarnation(latest->ticket().incarnation());
  }
  void tick()
  {
    // The channel already invokes permits(Deliver) on its bounded heartbeat,
    // including with no input traffic. That rechecks active providers and
    // revokes/releases on failure; this faster timer only checks retained life.
    if (attempt && !live())
      cancel(LocalInputRevocation::AuthorityLost, true);
  }
  bool begin(OriginalInputEpoch capture, ForwardingScope scope)
  {
    if (!running || cancelling || authorizing || processing || !controls.empty() || role != LocalInputRole::Core ||
        attempt || !channel || !channel->supports(scope) || !mailbox || !capture.live() ||
        capture.id() < captureHighWater)
      return false;
    const auto value = mailbox->read();
    if (!capture.live() || value.closed || !value.selected || !value.selected->live())
      return false;
    const auto &peer = value.selected->peer();
    const auto pin = policy.windowSharePeer(peer.ticket().fingerprint());
    if (!pin)
      return false;
    LocalInputOffer offer;
    offer.routeGeneration = nextGeneration();
    offer.coreServerIncarnation = peer.serverIncarnation();
    offer.coreRecipient = peer.recipient();
    offer.coreSelectionGeneration = peer.generation();
    offer.captureEpoch = capture.id();
    offer.deskflowClientIncarnation = localInputConnectionIncarnation(peer.ticket().incarnation());
    offer.deskflowPeerPin = peer.ticket().fingerprint();
    offer.windowSharePeerPin = *pin;
    offer.scope = scope;
    if (!validLocalInputOffer(offer)) {
      fail("Preflight generation exhausted");
      return false;
    }
    highWater = offer.routeGeneration;
    captureHighWater = offer.captureEpoch;
    attempt = Attempt{offer, {}, value.selected, {}, std::move(capture)};
    if (!live() || !channel->offer(offer) || !current(offer) || !live()) {
      cancel(LocalInputRevocation::AuthorityLost, true);
      return false;
    }
    return current(offer);
  }
  bool continuePreparation(LocalInputLease lease)
  {
    if (QThread::currentThread() != thread() || !running || role != LocalInputRole::Core || !preparation ||
        cancelling || authorizing || !attempt || attempt->phase != Phase::Quiesced || attempt->lease != lease ||
        attempt->continuation != Attempt::Continuation::Waiting)
      return false;
    if (!live()) {
      cancel(LocalInputRevocation::AuthorityLost, true);
      return false;
    }
    // This is a queue-deduplication marker, never readiness evidence. Set it
    // before scheduling so a reentrant or duplicate completion cannot consume
    // the bounded control queue or send Prepare twice.
    attempt->continuation = Attempt::Continuation::Queued;
    enqueue({Kind::ContinuePreparation, leaseOffer(lease), lease});
    return current(leaseOffer(lease)) && attempt->continuation == Attempt::Continuation::Queued;
  }
  bool wellFormed(const Control &control) const
  {
    return validLocalInputOffer(control.offer) && canonical(control.offer.deskflowClientIncarnation) &&
           (control.kind == Kind::Offer || control.kind == Kind::Cancel ||
            (validLocalInputLease(control.lease) && leaseOffer(control.lease) == control.offer &&
             canonical(control.lease.windowShareClientIncarnation))) &&
           (control.kind != Kind::InputTarget ||
            (control.lease.scope == ForwardingScope::All && validLocalInputTarget(control.target)));
  }
  bool pendingAuthorityControl() const
  {
    return std::any_of(controls.begin(), controls.end(), [&](const auto &control) {
      return control.offer.routeGeneration >= highWater;
    });
  }
  void dispatch(Control control)
  {
    enqueue(std::move(control), false);
    drain();
  }
  void enqueue(Control control, bool deferred = true)
  {
    if (!running)
      return;
    // Validate even obsolete traffic before dropping it. Retired controls must
    // neither block newer authorization nor consume its bounded control queue.
    if (!wellFormed(control)) {
      fail("Invalid preflight binding");
      return;
    }
    if (control.offer.routeGeneration < highWater)
      return;
    // Seven data controls plus a reserved cancellation slot. A flood cannot
    // silently discard an authority transition or defer local revocation.
    const auto limit = control.kind == Kind::Cancel ? 8U : 7U;
    if (controls.size() >= limit) {
      controls.clear();
      fail("Preflight control queue overflow");
      return;
    }
    controls.push_back(std::move(control));
    if (deferred)
      schedule();
  }
  void schedule()
  {
    if (!running || controls.empty() || processing || cancelling || authorizing || scheduled)
      return;
    scheduled = true;
    const auto weak = weak_from_this();
    QTimer::singleShot(0, this, [weak] {
      if (auto self = weak.lock()) {
        self->scheduled = false;
        self->drain();
      }
    });
  }
  void drain()
  {
    if (processing || cancelling || authorizing)
      return;
    processing = true;
    bool processed = false;
    while (running && !controls.empty()) {
      processed = true;
      const auto control = controls.front();
      controls.pop_front();
      try {
        receive(control);
      } catch (...) {
        fail("Preflight callback failed");
      }
    }
    processing = false;
    schedule();
    if (processed && running && progress) {
      // In particular, Committed/Activated can complete after the coordinator's
      // last tick. Notify only after the bounded control drain has unwound, so
      // its queued service need not wait for a maintenance timer to see Active.
      const auto notify = progress;
      try {
        notify();
      } catch (...) {
        // Notifications are scheduling hints; normal deadline maintenance and
        // all independent authorization checks remain responsible for safety.
      }
    }
  }
  // Returns true only for current traffic. Shape validation happens even for
  // retired traffic. Only the last retired binding needs an equality check;
  // lower generations can never affect the current authority.
  bool matches(const Control &control)
  {
    if (!wellFormed(control)) {
      fail("Invalid preflight binding");
      return false;
    }
    if (control.offer.routeGeneration < highWater)
      return false;
    if (control.offer.routeGeneration > highWater) {
      fail("Unsolicited future preflight reply");
      return false;
    }
    if (!attempt) {
      if (retired != control.offer || (control.kind != Kind::Cancel && retiredLease && retiredLease != control.lease))
        fail("Retired preflight binding changed");
      return false;
    }
    if (attempt->offer != control.offer ||
        (control.kind != Kind::Cancel && attempt->lease && attempt->lease != control.lease)) {
      fail("Current preflight binding changed");
      return false;
    }
    if (!live()) {
      cancel(LocalInputRevocation::AuthorityLost, true);
      return false;
    }
    return true;
  }
  void prepare(const LocalInputLease &lease)
  {
    if (!permits(lease, LocalInputCheck::Prepare)) {
      cancel(LocalInputRevocation::AuthorityLost, true);
      return;
    }
    attempt->phase = Phase::Preparing;
    if (!channel || !channel->prepare(lease))
      cancel(LocalInputRevocation::AuthorityLost, true);
  }
  void receive(const Control &control)
  {
    if (!running)
      return;
    if (control.kind == Kind::Offer) {
      receiveOffer(control.offer);
      return;
    }
    if (!matches(control))
      return;
    if (control.kind == Kind::Cancel) {
      cancel(control.reason, false);
      return;
    }
    const auto offer = control.offer;
    switch (control.kind) {
    case Kind::Candidate:
      if (role != LocalInputRole::Core || attempt->phase != Phase::Offered) {
        fail("Unexpected preflight candidate");
        return;
      }
      attempt->lease = control.lease;
      attempt->phase = Phase::Candidate;
      break;
    case Kind::InputTarget:
      if (role != LocalInputRole::Core || attempt->phase != Phase::Candidate || !attempt->lease || !target ||
          attempt->targetReceived) {
        fail("Unexpected mapped preflight target");
        return;
      }
      attempt->targetReceived = true;
      {
        const auto notify = target;
        notify(control.lease, control.target);
        if (current(offer) && !live())
          cancel(LocalInputRevocation::AuthorityLost, true);
      }
      break;
    case Kind::Quiesced:
      if (role != LocalInputRole::Core || attempt->phase != Phase::Candidate || !attempt->lease ||
          (target && control.lease.scope == ForwardingScope::All && !attempt->targetReceived)) {
        fail("Unexpected quiescence");
        return;
      }
      attempt->phase = Phase::Quiesced;
      if (preparation) {
        const auto notify = preparation;
        const auto deadline = attempt->deadline;
        notify(control.lease, deadline);
        if (current(offer) && !live())
          cancel(LocalInputRevocation::AuthorityLost, true);
      } else {
        prepare(control.lease);
      }
      break;
    case Kind::ContinuePreparation:
      if (role != LocalInputRole::Core || !preparation || attempt->phase != Phase::Quiesced ||
          attempt->continuation != Attempt::Continuation::Queued) {
        fail("Unexpected preflight preparation continuation");
        return;
      }
      attempt->continuation = Attempt::Continuation::Started;
      prepare(control.lease);
      break;
    case Kind::LocalQuiesced:
      if (role != LocalInputRole::Host || attempt->phase != Phase::Candidate || !host.isQuiesced) {
        fail("Unexpected local quiescence");
        return;
      }
      {
        const auto query = host.isQuiesced;
        const bool quiescent = query(keyboardBinding(control.lease));
        if (!current(offer))
          return;
        if (!quiescent || !live()) {
          cancel(LocalInputRevocation::AuthorityLost, true);
          return;
        }
        attempt->phase = Phase::Quiesced;
        if (!channel || !channel->quiesced(control.lease))
          cancel(LocalInputRevocation::AuthorityLost, true);
      }
      break;
    case Kind::Prepare:
      if (role != LocalInputRole::Host || attempt->phase != Phase::Quiesced) {
        fail("Unexpected preflight preparation");
        return;
      }
      if (!channel || !channel->acknowledgePrepare(control.lease))
        cancel(LocalInputRevocation::AuthorityLost, true);
      break;
    case Kind::Prepared:
      if (role != LocalInputRole::Core || attempt->phase != Phase::Preparing) {
        fail("Unexpected preflight acknowledgement");
        return;
      }
      attempt->phase = Phase::Committing;
      if (!channel || !channel->commit(control.lease))
        cancel(LocalInputRevocation::AuthorityLost, true);
      break;
    case Kind::Activated:
      if ((role == LocalInputRole::Core && attempt->phase != Phase::Committing) ||
          (role == LocalInputRole::Host && attempt->phase != Phase::Quiesced)) {
        fail("Unexpected preflight activation");
        return;
      }
      attempt->phase = Phase::Active;
      if (!permits(control.lease, LocalInputCheck::Deliver))
        cancel(LocalInputRevocation::AuthorityLost, true);
      break;
    case Kind::Offer:
    case Kind::Cancel:
      break;
    }
  }
  void receiveOffer(const LocalInputOffer &offer)
  {
    if (role != LocalInputRole::Host || !validLocalInputOffer(offer) || !canonical(offer.deskflowClientIncarnation)) {
      fail("Invalid preflight offer");
      return;
    }
    if (offer.routeGeneration < highWater)
      return;
    if (offer.routeGeneration == highWater) {
      if ((attempt && attempt->offer != offer) || (!attempt && retired != offer))
        fail("Preflight offer changed");
      return;
    }
    if (attempt) {
      fail("Replacement offer preceded cancellation");
      return;
    }
    if (offer.captureEpoch < captureHighWater) {
      fail("Preflight capture epoch regressed");
      return;
    }
    highWater = offer.routeGeneration;
    captureHighWater = offer.captureEpoch;
    attempt = Attempt{offer, {}, {}, {}, {}};
    if (!policy.permits(offer) || !host.connection || !host.reserve || !host.startQuiescence || !host.activate ||
        !host.isActive) {
      cancel(LocalInputRevocation::AuthorityLost, true);
      return;
    }
    const auto connection = host.connection;
    const auto observation = connection();
    if (!current(offer))
      return;
    attempt->host = observation;
    if (!live()) {
      cancel(LocalInputRevocation::AuthorityLost, true);
      return;
    }
    const auto reserve = host.reserve;
    std::optional<KeyboardRouteBinding> binding;
    reserving = true;
    try {
      binding = reserve(offer.scope);
    } catch (...) {
      // The callback may have partially reserved native ownership. The
      // ensuing terminal close must not claim a completed empty release.
      throw;
    }
    reserving = false;
    if (!binding) {
      if (current(offer))
        cancel(LocalInputRevocation::AuthorityLost, true);
      else if (channel && offer.scope == ForwardingScope::All && channel->supportsMappedInput())
        (void)channel->sendReleased(offer);
      return;
    }
    if (binding->scope != offer.scope) {
      // Never relabel an unexpected reservation with the requested scope,
      // including when reserve cancelled or destroyed this coordinator.
      // Connection teardown must release the unstarted exact reservation.
      fail("Private input reservation scope differs from the authenticated offer");
      return;
    }
    LocalInputLease lease;
    static_cast<LocalInputOffer &>(lease) = offer;
    lease.windowShareClientIncarnation = localInputConnectionIncarnation(observation->ticket().incarnation());
    lease.session = binding->owner.session;
    lease.epoch = binding->owner.epoch;
    lease.focusRequest = binding->focusRequest;
    lease.focusRevision = binding->focusRevision;
    lease.geometryRevision = binding->geometry;
    lease.keyboardRouteGeneration = binding->generation;
    if (!current(offer)) {
      bool released = false;
      try {
        const auto release = checks.release;
        released = release && release(lease, LocalInputRevocation::AuthorityLost);
      } catch (...) {
      }
      if (!released && channel)
        channel->close("Abandoned preflight reservation could not be released");
      return;
    }
    attempt->lease = lease;
    if (!validKeyboardRouteBinding(*binding) || !validLocalInputLease(lease) || !live()) {
      cancel(LocalInputRevocation::AuthorityLost, true);
      return;
    }
    attempt->phase = Phase::Candidate;
    if (!channel || !channel->candidate(lease)) {
      cancel(LocalInputRevocation::AuthorityLost, true);
      return;
    }
    if (!current(offer) || !live()) {
      cancel(LocalInputRevocation::AuthorityLost, true);
      return;
    }
    const auto start = host.startQuiescence;
    const bool started = start(*binding);
    if (current(offer) && (!started || !live()))
      cancel(LocalInputRevocation::AuthorityLost, true);
  }
  bool permits(const LocalInputLease &lease, LocalInputCheck check)
  {
    if (authorizing || cancelling || pendingAuthorityControl() || !checks.release || !attempt ||
        attempt->lease != lease ||
        (role == LocalInputRole::Core && preparation && attempt->continuation != Attempt::Continuation::Started) ||
        (attempt->phase != Phase::Quiesced && attempt->phase != Phase::Preparing &&
         attempt->phase != Phase::Committing && attempt->phase != Phase::Active))
      return false;
    if ((check == LocalInputCheck::Deliver && attempt->phase != Phase::Active) ||
        (check != LocalInputCheck::Deliver && attempt->phase == Phase::Active))
      return false;
    if (checks.neutralBoundary && checks.acquisitionBoundary)
      return false;
    authorizing = true;
    const auto offer = attempt->offer;
    const auto phase = attempt->phase;
    const auto stillCurrent = [&] {
      return !cancelling && !pendingAuthorityControl() && current(offer) && attempt->lease == lease &&
             attempt->phase == phase && live() && !cancelling && !pendingAuthorityControl();
    };
    const auto timed = [&](const char *step, const auto &call) {
      if (check == LocalInputCheck::Deliver) {
        const bool accepted = call();
        if (!accepted)
          lifecycleDiagnostic(
              role == LocalInputRole::Host ? "Host preflight" : "Core preflight", step, "denied",
              "Active input admission callback denied delivery",
              {{"routeGeneration", QString::number(lease.routeGeneration)}}
          );
        return accepted;
      }
      const auto started = Clock::now();
      const QString component = role == LocalInputRole::Host ? "Host preflight" : "Core preflight";
      const QJsonObject identity{
          {"routeGeneration", QString::number(lease.routeGeneration)},
          {"check", check == LocalInputCheck::Prepare ? "prepare" : "commit"}
      };
      lifecycleDiagnostic(component, step, "started", "Input admission callback", identity);
      const bool accepted = call();
      auto details = identity;
      details.insert(
          "elapsedUs", qint64(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count())
      );
      details.insert("accepted", accepted);
      lifecycleDiagnostic(component, step, accepted ? "completed" : "denied", "Input admission callback", details);
      return accepted;
    };
    bool result = false;
    try {
      const auto integrity = checks.codeAdmission, route = checks.deliveryReady,
                 neutral = checks.neutralBoundary ? checks.neutralBoundary : checks.acquisitionBoundary;
      result = stillCurrent() && integrity && route &&
               timed("input.code-admission", [&] { return integrity(lease); }) && stillCurrent() &&
               timed("input.delivery-ready", [&] { return route(lease); }) && stillCurrent();
      if (result && check != LocalInputCheck::Deliver)
        result = neutral && timed("input.acquisition-boundary", [&] { return neutral(lease); }) && stillCurrent();
      if (result && role == LocalInputRole::Host) {
        const auto binding = keyboardBinding(lease);
        if (check == LocalInputCheck::Deliver) {
          const auto query = host.isActive;
          result =
              attempt->hostActivated && query &&
              timed(
                  check == LocalInputCheck::Deliver ? "input.active" : "input.quiesced", [&] { return query(binding); }
              ) &&
              stillCurrent();
        } else {
          const auto query = host.isQuiesced;
          result =
              query &&
              timed(
                  check == LocalInputCheck::Deliver ? "input.active" : "input.quiesced", [&] { return query(binding); }
              ) &&
              stillCurrent();
          if (result && check == LocalInputCheck::Commit) {
            const auto activate = host.activate, active = host.isActive;
            result = !attempt->hostActivationAttempted && channel && channel->phase() == LocalInputPhase::Prepared &&
                     activate && active;
            if (result) {
              // Mark before callbacks: a reentrant check must never install
              // the same host route twice, even if the first callback fails.
              attempt->hostActivationAttempted = true;
              // Activation can synchronously change provider state without
              // changing the private lease. Recheck ongoing admission before
              // allowing its acknowledgment; neutrality belongs before commit.
              result = timed("input.activate", [&] { return activate(binding); }) && stillCurrent() &&
                       timed("input.code-admission", [&] { return integrity(lease); }) && stillCurrent() &&
                       timed("input.delivery-ready", [&] { return route(lease); }) && stillCurrent() &&
                       timed("input.active", [&] { return active(binding); }) && stillCurrent();
              if (result)
                attempt->hostActivated = true;
            }
          }
        }
      }
    } catch (...) {
      result = false;
    }
    authorizing = false;
    schedule();
    return result;
  }
};

LocalInputPreflight::LocalInputPreflight(std::shared_ptr<State> state) : m_state(std::move(state))
{
}
std::unique_ptr<LocalInputPreflight> LocalInputPreflight::core(
    LocalInputChannel &channel, std::shared_ptr<SelectedPeerMailbox> mailbox, LocalInputPeerPolicy policy,
    Checks checks, PreparationNotification preparation, TargetNotification target, ProgressNotification progress
)
{
  if (!mailbox)
    throw std::invalid_argument("Preflight requires a selected-peer mailbox");
  auto state = std::make_shared<State>(channel, LocalInputRole::Core, std::move(policy), std::move(checks));
  state->mailbox = std::move(mailbox);
  state->preparation = std::move(preparation);
  state->target = std::move(target);
  state->progress = std::move(progress);
  state->attach();
  return std::unique_ptr<LocalInputPreflight>(new LocalInputPreflight(std::move(state)));
}
std::unique_ptr<LocalInputPreflight>
LocalInputPreflight::host(LocalInputChannel &channel, LocalInputPeerPolicy policy, Host host, Checks checks)
{
  auto state = std::make_shared<State>(channel, LocalInputRole::Host, std::move(policy), std::move(checks));
  state->host = std::move(host);
  state->attach();
  return std::unique_ptr<LocalInputPreflight>(new LocalInputPreflight(std::move(state)));
}
LocalInputPreflight::~LocalInputPreflight()
{
  stop();
}
bool LocalInputPreflight::begin(OriginalInputEpoch capture, ForwardingScope scope)
{
  const auto state = m_state;
  return state->begin(std::move(capture), scope);
}
bool LocalInputPreflight::continuePreparation(const LocalInputLease &lease)
{
  const auto state = m_state;
  return state->continuePreparation(lease);
}
void LocalInputPreflight::quiesced(const KeyboardRouteBinding &binding)
{
  const auto state = m_state;
  if (state->running && state->role == LocalInputRole::Host && state->attempt && state->attempt->lease &&
      keyboardBinding(*state->attempt->lease) == binding)
    state->enqueue({State::Kind::LocalQuiesced, state->attempt->offer, *state->attempt->lease});
}
void LocalInputPreflight::cancel(LocalInputRevocation reason)
{
  const auto state = m_state;
  state->cancel(reason, true);
}
void LocalInputPreflight::replacePolicy(LocalInputPeerPolicy policy)
{
  const auto state = m_state;
  state->cancel(LocalInputRevocation::AuthorityLost, true);
  if (state->running)
    state->policy = std::move(policy);
}
void LocalInputPreflight::stop()
{
  const auto state = m_state;
  state->stop();
}
LocalInputPreflight::Phase LocalInputPreflight::phase() const
{
  return !m_state->running ? Phase::Stopped : m_state->attempt ? m_state->attempt->phase : Phase::Idle;
}
std::optional<LocalInputOffer> LocalInputPreflight::offer() const
{
  return m_state->attempt ? std::optional(m_state->attempt->offer) : std::nullopt;
}
std::optional<LocalInputLease> LocalInputPreflight::lease() const
{
  return m_state->attempt ? m_state->attempt->lease : std::nullopt;
}
} // namespace deskflow::windowshare
