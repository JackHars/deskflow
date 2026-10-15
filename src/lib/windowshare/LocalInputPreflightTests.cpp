/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputPreflight.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTextStream>
#include <QThread>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace deskflow::windowshare {
class SelectedPeerTestAccess
{
public:
  static SelectedPeer
  create(AuthenticatedPeerTicket ticket, std::shared_ptr<const std::atomic_bool> revoked, std::uint64_t generation = 3)
  {
    return {1, 2, generation, std::move(ticket), std::move(revoked)};
  }
};
struct AuthenticatedPeerObservationTestAccess
{
  static AuthenticatedPeerObservation create(AuthenticatedPeerTicket ticket, int ageMs = 0)
  {
    return {std::move(ticket), std::chrono::steady_clock::now() - std::chrono::milliseconds(ageMs)};
  }
};
struct SelectedPeerMailboxTestAccess
{
  static void stallAndRefresh(SelectedPeerMailbox &mailbox, std::atomic_bool &locked)
  {
    std::scoped_lock lock(mailbox.m_mutex);
    locked.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto old = *mailbox.m_selected;
    mailbox.m_selected = SelectedPeerMailbox::Observation(
        old.m_peer, ++mailbox.m_revision, old.m_selection, old.m_token, std::chrono::steady_clock::now()
    );
  }
};
} // namespace deskflow::windowshare

using namespace deskflow;
using namespace deskflow::windowshare;
namespace {
using Phase = LocalInputPreflight::Phase;
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
bool until(const std::function<bool()> &condition, int timeoutMs = 4000)
{
  if (condition())
    return true;
  QEventLoop loop;
  QTimer poll, timeout;
  poll.setInterval(2);
  timeout.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (condition())
      loop.quit();
  });
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start();
  timeout.start(timeoutMs);
  loop.exec();
  return condition();
}
LocalInputPin pin(std::uint8_t value)
{
  LocalInputPin result{};
  result.fill(value);
  return result;
}
LocalInputPeerPolicy policy(std::uint8_t core = 1, std::uint8_t viewer = 2)
{
  const std::array pairs{LocalInputPeerPair{pin(core), pin(viewer)}};
  const auto result = LocalInputPeerPolicy::create(pairs);
  check(result.has_value(), "Fixture policy rejected");
  return *result;
}
LocalInputPreflight::Checks allow()
{
  LocalInputPreflight::Checks result;
  result.codeAdmission = result.deliveryReady = result.neutralBoundary = [](const auto &) { return true; };
  result.release = [](const auto &, auto) { return true; };
  return result;
}
KeyboardRouteBinding binding()
{
  return {{11, 12}, 17, 13, 14, 15};
}
int child(int descriptor, const QString &mode)
{
  check(protectLocalInputDescriptor(descriptor), "Fixture endpoint rejected");
  const auto process = localInputProcess(getppid());
  check(process.has_value(), "Fixture parent unavailable");
  auto channel = LocalInputChannel::adoptHost(descriptor, *process);
  const bool unsupportedScope = mode.startsWith("full-scope-") || mode == "invalid-scope-reserve";
  auto expectedBinding = binding();
  if (mode == "all-input" || mode == "deferred-all" || mode == "target-all")
    expectedBinding.scope = ForwardingScope::All;
  bool closed = false, quiescent = false, reserved = false, active = false, held = false, providerLost = false;
  int releases = 0, reads = 0, originals = 0, prepares = 0, activations = 0, neutralAfterActivation = 0,
      quiescenceAfterActivation = 0, starts = 0, reservationShutdowns = 0;
  channel->onClosed = [&](QString) {
    closed = true;
    if (unsupportedScope && reserved) {
      // Simulate the owning connection shutting down its unstarted reservation.
      // A scope-mismatched lease cannot perform this exact-scope cleanup.
      ++reservationShutdowns;
      reserved = false;
    }
  };
  channel->onOriginal = [&](const auto &lease, const auto &record) {
    ++originals;
    held = true;
    if (mode == "all-input") {
      constexpr std::array<std::size_t, 8> families{0, 2, 1, 3, 2, 1, 0, 4};
      check(
          lease.scope == ForwardingScope::All && originals <= 8 &&
              record.sequence == static_cast<std::uint64_t>(originals) &&
              record.payload.index() == families[originals - 1],
          "Full-input receiver lost scope, family, or source order"
      );
      if (originals == 8)
        channel->revoke();
    }
    if (mode.startsWith("host-watchdog-"))
      QTimer::singleShot(25, QCoreApplication::instance(), [&] { providerLost = true; });
  };
  AuthenticatedPeerIssuer issuer;
  const auto ticket = issuer.issue(pin(mode == "wrong-host-pin" ? 3 : 2));
  check(ticket.has_value(), "Fixture host ticket unavailable");
  std::unique_ptr<LocalInputPreflight> coordinator;
  LocalInputPreflight::Host host;
  host.connection = [&]() -> std::optional<AuthenticatedPeerObservation> {
    ++reads;
    if (mode == "stale-host-after-read" && reads == 2)
      QThread::msleep(150);
    return AuthenticatedPeerObservationTestAccess::create(
        *ticket, mode == "stale-host-after-read" && reads == 1 ? 5900 : 0
    );
  };
  host.reserve = [&](ForwardingScope scope) -> std::optional<KeyboardRouteBinding> {
    reserved = true;
    if (mode == "destroy-reserve" || mode == "full-scope-destroy-reserve")
      coordinator.reset();
    if (mode == "full-scope-cancel-reserve")
      coordinator->cancel();
    if (mode == "throw-reserve")
      throw std::runtime_error("Fixture reserve failed");
    auto candidate = binding();
    candidate.scope = scope;
    if (unsupportedScope)
      candidate.scope = mode == "invalid-scope-reserve" ? static_cast<ForwardingScope>(123) : ForwardingScope::All;
    return candidate;
  };
  host.startQuiescence = [&](const auto &candidate) {
    ++starts;
    check(candidate == expectedBinding, "Host started a different reservation");
    check(channel->phase() == LocalInputPhase::Idle, "Preflight acquired channel authority");
    if (mode == "no-quiescence" || mode == "target-all")
      return true;
    quiescent = true;
    coordinator->quiesced(candidate);
    return true;
  };
  host.isQuiesced = [&](const auto &candidate) {
    quiescenceAfterActivation += active;
    return !active && quiescent && candidate == expectedBinding && mode != "stale-focus";
  };
  host.activate = [&](const auto &candidate) {
    check(
        candidate == expectedBinding && quiescent && !active && channel->phase() == LocalInputPhase::Prepared &&
            coordinator->phase() == Phase::Quiesced,
        "Host activation preceded exact neutral Commit or followed its acknowledgment"
    );
    ++activations;
    active = true;
    if (mode == "reject-host-activation")
      return false;
    if (mode == "throw-host-activation")
      throw std::runtime_error("Fixture activation failed after a partial transition");
    if (mode.startsWith("activation-loses-"))
      providerLost = true;
    if (mode == "cancel-host-activation")
      coordinator->cancel();
    else if (mode == "stop-host-activation")
      coordinator->stop();
    else if (mode == "destroy-host-activation")
      coordinator.reset();
    else if (mode == "close-host-activation")
      channel->close();
    else if (mode == "destroy-channel-activation")
      channel.reset();
    else if (mode == "reentrant-host-activation") {
      const auto lease = *coordinator->lease();
      check(!channel->authorize(lease, LocalInputCheck::Commit), "Recursive activation acquired authority");
    }
    return true;
  };
  host.isActive = [&](const auto &candidate) {
    check(candidate == expectedBinding, "Active host query changed exact owner");
    if (mode == "reject-host-active" || (providerLost && mode == "host-watchdog-active"))
      return false;
    if (mode == "throw-host-active")
      throw std::runtime_error("Fixture active query failed");
    if (mode == "cancel-host-active")
      coordinator->cancel();
    if (mode == "destroy-host-active")
      coordinator.reset();
    return active;
  };
  if (mode == "missing-host-activation")
    host.activate = {};
  if (mode == "missing-host-active")
    host.isActive = {};
  auto checks = allow();
  checks.codeAdmission = [&](const auto &) {
    return !providerLost || (mode != "host-watchdog-integrity" && mode != "activation-loses-integrity");
  };
  checks.deliveryReady = [&](const auto &) {
    return !providerLost || (mode != "host-watchdog-delivery" && mode != "activation-loses-delivery");
  };
  checks.neutralBoundary = [&](const auto &) {
    neutralAfterActivation += active;
    return !held;
  };
  checks.release = [&](const auto &lease, auto) {
    check(
        lease.session == expectedBinding.owner.session && lease.keyboardRouteGeneration == expectedBinding.generation &&
            lease.scope == expectedBinding.scope,
        "Release changed host owner"
    );
    ++releases;
    reserved = false;
    quiescent = false;
    active = false;
    held = false;
    return mode != "release-failure";
  };
  if (mode == "missing-host-neutral")
    checks.neutralBoundary = {};
  coordinator = LocalInputPreflight::host(*channel, policy(), std::move(host), std::move(checks));
  const auto prepare = channel->onPrepare;
  channel->onPrepare = [&, prepare](const auto &lease) {
    ++prepares;
    prepare(lease);
  };
  check(until([&] { return closed; }, 12000), "Fixture child did not close");
  if (unsupportedScope)
    check(
        reservationShutdowns == 1 && releases == 0 && starts == 0 && activations == 0 && !reserved && !quiescent &&
            !active && !channel->lease() &&
            (!coordinator || (coordinator->phase() == Phase::Stopped && !coordinator->lease())),
        "Unsupported scope started a fence or used a downgraded private lease for reservation cleanup"
    );
  if (mode == "all-input")
    check(
        originals == 8 && releases == 1 && activations == 1 && !held && !active,
        "Full-input preflight lost input families or exact release ownership"
    );
  if (mode == "deferred-all")
    check(
        prepares == activations && originals == 0 && !reserved && !quiescent && !active,
        "Deferred continuation sent an extra Prepare, delivered input, or retained its reservation"
    );
  if (mode == "destroy-reserve")
    check(!reserved && releases == 1, "Destroyed reservation was not released exactly once");
  if (mode.startsWith("host-watchdog-"))
    check(
        originals == 1 && activations == 1 && releases == 1 && !held && !active && providerLost,
        "Idle host provider loss failed to release its held owner exactly once"
    );
  else if (mode != "normal" && mode != "all-input")
    check(originals == 0, "Original input escaped failed preflight");
  if (mode.endsWith("-activation") || mode.endsWith("-active") || mode.startsWith("activation-loses-")) {
    const bool missing = mode.startsWith("missing-");
    check(
        activations == (missing ? 0 : 1) && releases == (missing ? 0 : 1) && !active,
        "Host activation failure or stop did not release its exact partial owner once"
    );
  }
  check(
      neutralAfterActivation == 0 && quiescenceAfterActivation == 0,
      "Active route rechecked one-time physical or quiesced admission"
  );
  coordinator.reset();
  return 0;
}

struct Fixture
{
  QProcess childProcess;
  std::unique_ptr<LocalInputChannel> channel;
  std::unique_ptr<LocalInputPreflight> coordinator;
  std::shared_ptr<SelectedPeerMailbox> mailbox = std::make_shared<SelectedPeerMailbox>();
  AuthenticatedPeerIssuer issuer;
  OriginalInputStream capture;
  std::shared_ptr<std::atomic_bool> revoked = std::make_shared<std::atomic_bool>(false);
  SelectedPeer selected;
  QTimer publisher;
  bool closed = false;
  int releases = 0, preparedChecks = 0, revokeNotifications = 0;
  QString closeReason;

  explicit Fixture(
      const QString &mode = "normal", LocalInputPreflight::Checks checks = allow(),
      LocalInputPreflight::PreparationNotification preparation = {}, LocalInputPreflight::TargetNotification target = {}
  )
  {
    auto pair = LocalInputSocketPair::create();
    pair.configureChild(childProcess);
    childProcess.start(
        QCoreApplication::applicationFilePath(), {"--child", QString::number(pair.childDescriptor()), mode}
    );
    check(childProcess.waitForStarted(2000), "Fixture child did not start");
    const auto process = localInputProcess(childProcess.processId());
    check(process.has_value(), "Fixture child identity unavailable");
    channel = pair.adoptCore(*process);
    channel->onClosed = [&](QString reason) {
      closed = true;
      closeReason = reason;
      mailbox->close();
    };
    check(until([&] { return channel->authenticated() || closed; }) && !closed, "Fixture handshake failed");
    const auto ticket = issuer.issue(pin(1));
    check(ticket.has_value(), "Fixture core ticket unavailable");
    selected = SelectedPeerTestAccess::create(*ticket, revoked);
    mailbox->replace(selected);
    publisher.setInterval(20);
    QObject::connect(&publisher, &QTimer::timeout, &publisher, [&] { mailbox->replace(selected); });
    publisher.start();
    const auto neutral = checks.neutralBoundary;
    if (neutral)
      checks.neutralBoundary = [&, neutral](const auto &lease) {
        ++preparedChecks;
        return neutral(lease);
      };
    const auto release = checks.release;
    if (release)
      checks.release = [&, release](const auto &lease, auto reason) {
        check(
            lease.coreRecipient == selected.recipient() && lease.session == binding().owner.session,
            "Release changed core owner"
        );
        ++releases;
        return release(lease, reason);
      };
    channel->onRevoked = [&](const auto &, auto) { ++revokeNotifications; };
    coordinator = LocalInputPreflight::core(
        *channel, mailbox, policy(), std::move(checks), std::move(preparation), std::move(target)
    );
  }
  ~Fixture()
  {
    publisher.stop();
    coordinator.reset();
    if (channel) {
      channel->onClosed = {};
      channel->close();
    }
    if (childProcess.state() != QProcess::NotRunning) {
      childProcess.kill();
      childProcess.waitForFinished(2000);
    }
  }
  void begin(ForwardingScope scope = ForwardingScope::Keyboard)
  {
    check(coordinator->begin(capture.epoch(), scope), "Preflight did not offer selected peer");
    check(channel->phase() == LocalInputPhase::Idle, "Offer acquired channel authority");
  }
  void active(ForwardingScope scope = ForwardingScope::Keyboard)
  {
    begin(scope);
    check(
        until([&] { return coordinator->phase() == Phase::Active || closed; }) && !closed &&
            channel->phase() == LocalInputPhase::Active,
        "Preflight did not activate"
    );
  }
  void finish()
  {
    if (coordinator)
      coordinator->stop();
    if (channel)
      channel->close();
    check(until([&] { return childProcess.state() == QProcess::NotRunning; }), "Fixture child failed to stop");
    if (childProcess.exitStatus() != QProcess::NormalExit || childProcess.exitCode() != 0)
      throw std::runtime_error("Fixture child failed: " + childProcess.readAllStandardError().toStdString());
  }
};

void policyValidation()
{
  check(!LocalInputPeerPolicy{}.windowSharePeer(pin(1)), "Default policy approved a peer");
  check(LocalInputPeerPolicy::create({}).has_value(), "Empty deny policy rejected");
  const std::array duplicate{LocalInputPeerPair{pin(1), pin(2)}, LocalInputPeerPair{pin(1), pin(3)}};
  check(!LocalInputPeerPolicy::create(duplicate), "Ambiguous pair mapping accepted");
  const std::array duplicateViewer{LocalInputPeerPair{pin(1), pin(2)}, LocalInputPeerPair{pin(3), pin(2)}};
  check(!LocalInputPeerPolicy::create(duplicateViewer), "Ambiguous reverse pair mapping accepted");
  const std::array zero{LocalInputPeerPair{{}, pin(2)}};
  check(!LocalInputPeerPolicy::create(zero), "Empty fingerprint accepted");
  std::array<LocalInputPeerPair, 9> excess;
  for (std::size_t i = 0; i < excess.size(); ++i)
    excess[i] = {pin(static_cast<std::uint8_t>(i + 1)), pin(static_cast<std::uint8_t>(i + 20))};
  check(LocalInputPeerPolicy::create(std::span(excess).first<8>()).has_value(), "Eight distinct pairs rejected");
  check(!LocalInputPeerPolicy::create(excess), "Unbounded mapping accepted");
  const auto encoded = localInputConnectionIncarnation(0x1020304050607080ULL);
  check(
      encoded[0] == 0 && encoded[7] == 0 && encoded[8] == 0x10 && encoded[15] == 0x80,
      "TLS incarnation lost canonical byte order"
  );
}
void successAndRetirement()
{
  Fixture f;
  f.active();
  const auto lease = *f.coordinator->lease();
  check(
      lease.coreServerIncarnation == 1 && lease.coreRecipient == 2 && lease.coreSelectionGeneration == 3 &&
          lease.focusRequest == 13 && lease.keyboardRouteGeneration == 17 &&
          lease.captureEpoch == f.capture.epoch().id() && f.preparedChecks >= 2,
      "Preflight lost selection, focus request, or neutral checks"
  );
  OriginalInputRecord event;
  event.epoch = lease.captureEpoch;
  event.sequence = 1;
  event.sourceTimestampNs = 100;
  std::get<OriginalInputKey>(event.payload).keyboardType = 40;
  std::get<OriginalInputKey>(event.payload).kind = OriginalKeyKind::Down;
  std::get<OriginalInputKey>(event.payload).transition = OriginalKeyTransition::Down;
  check(f.channel->sendOriginal(lease, event), "Test-authorized original input did not send");
  f.coordinator->cancel();
  check(
      f.coordinator->phase() == Phase::Idle && f.channel->phase() == LocalInputPhase::Idle && f.releases == 1,
      "Cancellation did not release exact active owner"
  );
  check(!f.channel->sendOriginal(lease, event), "Retired original input escaped");
  f.channel->onCandidate(lease);
  f.channel->onQuiesced(lease);
  QCoreApplication::processEvents();
  check(!f.closed && f.coordinator->phase() == Phase::Idle, "Retired replies revived or broke authority");
  f.finish();
}
void fullInputScope()
{
  Fixture f("all-input");
  f.active(ForwardingScope::All);
  const auto lease = *f.coordinator->lease();
  check(
      lease.scope == ForwardingScope::All && f.channel->supports(ForwardingScope::All),
      "Full scope was erased between offer and activation"
  );
  const std::array<decltype(OriginalInputRecord::payload), 8> payloads{
      OriginalInputKey{40, 0},
      OriginalInputButton{0, true, 1},
      OriginalInputMotion{3, -4, 0},
      OriginalInputScroll{},
      OriginalInputButton{0, false, 1},
      OriginalInputMotion{-2, 1, 32},
      OriginalInputKey{40, 0, OriginalKeyKind::Up, OriginalKeyTransition::Up, false},
      OriginalInputBoundary::Checkpoint
  };
  std::uint64_t sequence = 0;
  for (const auto &payload : payloads) {
    ++sequence;
    const OriginalInputRecord record{lease.captureEpoch, sequence, 100 + sequence, 0, payload};
    check(f.channel->sendOriginal(lease, record), "Full lease rejected a valid original input family");
  }
  // The receiver revokes only after consuming all eight records. This is a
  // private receiver receipt, not a native compositor/application receipt.
  check(
      until([&] { return f.coordinator->phase() == Phase::Idle || f.closed; }) && !f.closed,
      "Full input failed at its private receiver"
  );
  check(
      f.releases == 1 && f.coordinator->phase() == Phase::Idle,
      "Full-input cancellation failed exact-owner synchronous release"
  );
  f.finish();
}

void consecutiveControlAndOriginal()
{
  {
    Fixture f("same-batch-cancel");
    f.active();
    const auto lease = *f.coordinator->lease();
    OriginalInputRecord event;
    event.epoch = lease.captureEpoch;
    event.sequence = 1;
    event.sourceTimestampNs = 100;
    std::get<OriginalInputKey>(event.payload).keyboardType = 40;
    std::get<OriginalInputKey>(event.payload).kind = OriginalKeyKind::Down;
    std::get<OriginalInputKey>(event.payload).transition = OriginalKeyTransition::Down;
    // Both frames are queued without pumping Qt between them. The host must
    // retire this owner before reading the following in-flight Original; its
    // fixture asserts that no onOriginal callback ran for this mode.
    check(f.channel->cancelOffer(leaseOffer(lease)), "Could not queue cancellation");
    check(f.channel->sendOriginal(lease, event), "Could not queue trailing Original");
    check(
        until([&] { return f.coordinator->phase() == Phase::Idle || f.closed; }) && !f.closed && f.releases == 1,
        "Consecutive cancellation and Original did not retire the owner cleanly"
    );
    f.finish();
  }
  {
    Fixture f;
    const auto activated = f.channel->onActivated;
    bool called = false, coordinatorActive = false, delivered = false;
    f.channel->onActivated = [&](const LocalInputLease &lease) {
      activated(lease);
      called = true;
      coordinatorActive = f.coordinator->phase() == Phase::Active;
      OriginalInputRecord event;
      event.epoch = lease.captureEpoch;
      event.sequence = 1;
      event.sourceTimestampNs = 100;
      std::get<OriginalInputKey>(event.payload).keyboardType = 40;
      std::get<OriginalInputKey>(event.payload).kind = OriginalKeyKind::Down;
      std::get<OriginalInputKey>(event.payload).transition = OriginalKeyTransition::Down;
      // Delivery and an already queued channel heartbeat can run immediately
      // after activation, before a zero-duration coordinator timer dispatches.
      delivered = f.channel->sendOriginal(lease, event);
    };
    f.begin();
    check(until([&] { return called || f.closed; }) && called && !f.closed, "Activation callback did not run");
    check(coordinatorActive && delivered, "Activated channel temporarily denied valid Original");
    f.finish();
  }
}
void defaultDeny()
{
  for (int missing = 0; missing < 4; ++missing) {
    auto checks = allow();
    if (missing == 0)
      checks.codeAdmission = {};
    if (missing == 1)
      checks.neutralBoundary = {};
    if (missing == 2)
      checks.deliveryReady = {};
    if (missing == 3)
      checks.release = {};
    Fixture f("normal", std::move(checks));
    f.begin();
    check(until([&] { return f.closed || f.coordinator->phase() == Phase::Idle; }), "Missing provider did not deny");
    check(
        f.channel->phase() == LocalInputPhase::Idle && f.coordinator->phase() != Phase::Active,
        "Missing integrity/neutral/ordering/release provider granted authority"
    );
    f.finish();
  }
}
void deferredPreparation()
{
  using Clock = std::chrono::steady_clock;
  std::optional<LocalInputLease> notified;
  Clock::time_point deadline;
  int notifications = 0;
  bool ready = false;
  auto checks = allow();
  checks.deliveryReady = [&](const auto &) { return ready; };
  Fixture f("deferred-all", std::move(checks), [&](const auto &lease, auto expires) {
    ++notifications;
    notified = lease;
    deadline = expires;
  });
  const auto before = Clock::now();
  f.begin(ForwardingScope::All);
  const auto after = Clock::now();
  check(until([&] { return notified || f.closed; }) && !f.closed, "Deferred quiescence notification did not arrive");
  check(
      notifications == 1 && notified == f.coordinator->lease() && notified->scope == ForwardingScope::All &&
          deadline >= before + std::chrono::seconds(2) && deadline <= after + std::chrono::seconds(2) &&
          deadline > Clock::now() && f.coordinator->phase() == Phase::Quiesced &&
          f.channel->phase() == LocalInputPhase::Idle && !f.channel->lease() && f.preparedChecks == 0,
      "Deferred notification lost its exact lease/deadline or prepared before continuation"
  );
  // Neither the notification nor a direct channel call can skip the queued
  // continuation, even if the independent providers already permit preparation.
  ready = true;
  check(!f.channel->prepare(*notified), "Direct channel preparation bypassed deferred continuation");
  for (int change = 0; change < 5; ++change) {
    auto wrong = *notified;
    if (change == 0)
      wrong.scope = ForwardingScope::Keyboard;
    if (change == 1)
      ++wrong.focusRequest;
    if (change == 2)
      ++wrong.captureEpoch;
    if (change == 3)
      ++wrong.routeGeneration;
    if (change == 4)
      ++wrong.coreRecipient;
    check(!f.coordinator->continuePreparation(wrong), "Mismatched continuation changed a live attempt");
  }
  QCoreApplication::processEvents();
  check(
      !f.closed && notifications == 1 && f.coordinator->phase() == Phase::Quiesced && f.preparedChecks == 0 &&
          !f.channel->lease(),
      "A mismatched continuation or event-loop turn prepared the pending route"
  );
  check(f.coordinator->continuePreparation(*notified), "Exact continuation did not queue");
  check(!f.coordinator->continuePreparation(*notified), "Duplicate continuation entered the control queue");
  check(
      f.coordinator->phase() == Phase::Quiesced && f.channel->phase() == LocalInputPhase::Idle &&
          !f.channel->prepare(*notified) && f.preparedChecks == 0,
      "Continuation prepared inline or before its control was dequeued"
  );
  check(
      until([&] { return f.closed || f.coordinator->phase() == Phase::Active; }) && !f.closed &&
          f.channel->phase() == LocalInputPhase::Active && f.preparedChecks >= 2 && notifications == 1 &&
          !f.coordinator->continuePreparation(*notified),
      "Later continuation failed to recheck and activate once"
  );
  f.finish();
}
void deferredCancellationAndStale()
{
  std::optional<LocalInputLease> notified;
  int notifications = 0;
  Fixture f("deferred-all", allow(), [&](const auto &lease, auto) {
    ++notifications;
    notified = lease;
  });
  f.begin(ForwardingScope::All);
  check(until([&] { return notified || f.closed; }) && !f.closed, "No lease for canceled continuation");
  const auto old = *notified;
  check(f.coordinator->continuePreparation(old), "Canceled fixture did not queue continuation");
  f.coordinator->cancel();
  check(!f.coordinator->continuePreparation(old), "Canceled continuation was accepted");
  QCoreApplication::processEvents();
  check(
      !f.closed && f.coordinator->phase() == Phase::Idle && f.channel->phase() == LocalInputPhase::Idle &&
          f.releases == 1 && f.preparedChecks == 0,
      "Queued continuation revived its canceled owner"
  );
  notified.reset();
  f.begin(ForwardingScope::All);
  check(until([&] { return notified || f.closed; }) && !f.closed, "Replacement deferred lease did not arrive");
  check(
      notifications == 2 && notified->routeGeneration > old.routeGeneration &&
          !f.coordinator->continuePreparation(old) && f.coordinator->phase() == Phase::Quiesced &&
          f.coordinator->continuePreparation(*notified),
      "Stale continuation affected a newer exact lease"
  );
  check(
      until([&] { return f.closed || f.coordinator->phase() == Phase::Active; }) && !f.closed,
      "Current continuation did not survive stale completion"
  );
  f.finish();
  check(f.releases == 2, "Deferred cancellation or shutdown repeated its exact release");
}
void deferredChecks()
{
  for (int denied = 0; denied < 4; ++denied) {
    bool allowed = true;
    std::optional<LocalInputLease> notified;
    auto checks = allow();
    const auto query = [&](const auto &) { return allowed; };
    if (denied == 0)
      checks.codeAdmission = query;
    if (denied == 1)
      checks.deliveryReady = query;
    if (denied == 2)
      checks.neutralBoundary = query;
    Fixture f("deferred-all", std::move(checks), [&](const auto &lease, auto) { notified = lease; });
    f.begin(ForwardingScope::All);
    check(until([&] { return notified || f.closed; }) && !f.closed, "No lease for deferred revalidation");
    allowed = false;
    if (denied == 3)
      f.capture.revoke(OriginalInputRevocation::Discontinuity);
    check(
        f.coordinator->continuePreparation(*notified) == (denied != 3),
        "Continuation skipped retained capture liveness or ran providers inline"
    );
    check(
        until([&] { return f.closed || f.coordinator->phase() == Phase::Idle; }) && !f.closed &&
            f.channel->phase() == LocalInputPhase::Idle && !f.channel->lease() && f.releases == 1,
        "Deferred continuation bypassed independent admission, route, neutrality, or capture checks"
    );
    f.finish();
  }
}
void deferredNotificationReentry()
{
  {
    Fixture *fixture = nullptr;
    bool returnedWithoutPrepare = false;
    Fixture f("deferred-all", allow(), [&](const auto &lease, auto) {
      check(fixture->coordinator->continuePreparation(lease), "Reentrant continuation was rejected");
      check(!fixture->coordinator->continuePreparation(lease), "Reentrant duplicate continuation queued");
      QCoreApplication::processEvents();
      returnedWithoutPrepare = fixture->coordinator->phase() == Phase::Quiesced &&
                               fixture->channel->phase() == LocalInputPhase::Idle && fixture->preparedChecks == 0;
    });
    fixture = &f;
    f.active(ForwardingScope::All);
    check(returnedWithoutPrepare, "Nested event processing sent Prepare before the notification returned");
    f.finish();
  }
  enum class Action
  {
    Cancel,
    Stop,
    DestroyCoordinator,
    DestroyChannel,
    Throw
  };
  for (const auto action :
       {Action::Cancel, Action::Stop, Action::DestroyCoordinator, Action::DestroyChannel, Action::Throw}) {
    Fixture *fixture = nullptr;
    std::optional<LocalInputLease> notified;
    bool queued = false;
    Fixture f("deferred-all", allow(), [&](const auto &lease, auto) {
      notified = lease;
      queued = fixture->coordinator->continuePreparation(lease);
      if (action == Action::Cancel)
        fixture->coordinator->cancel();
      else if (action == Action::Stop)
        fixture->coordinator->stop();
      else if (action == Action::DestroyCoordinator)
        fixture->coordinator.reset();
      else if (action == Action::DestroyChannel)
        fixture->channel.reset();
      else
        throw std::runtime_error("Fixture preparation notification failed");
    });
    fixture = &f;
    f.begin(ForwardingScope::All);
    check(until([&] { return notified.has_value(); }), "Reentrant preparation notification did not run");
    QCoreApplication::processEvents();
    check(
        queued && f.releases == 1 && f.preparedChecks == 0 &&
            (!f.channel || (f.channel->phase() == LocalInputPhase::Idle && !f.channel->lease())) &&
            (!f.coordinator || (!f.coordinator->continuePreparation(*notified) &&
                                (f.coordinator->phase() == Phase::Idle || f.coordinator->phase() == Phase::Stopped))),
        "Reentrant notification cancellation/destruction granted authority or repeated release"
    );
    f.finish();
  }
}
void hostFailureCases()
{
  for (const auto mode :
       {"wrong-host-pin", "stale-host-after-read", "stale-focus", "missing-host-neutral", "destroy-reserve",
        "throw-reserve"}) {
    Fixture f(mode);
    f.begin();
    check(until([&] { return f.closed || f.coordinator->phase() == Phase::Idle; }), "Host failure did not cancel");
    check(
        f.coordinator->phase() != Phase::Active && f.channel->phase() != LocalInputPhase::Active,
        "Failed host authority activated route"
    );
    f.finish();
  }
}
void unsupportedReservationScope()
{
  for (const auto mode :
       {"full-scope-reserve", "full-scope-cancel-reserve", "full-scope-destroy-reserve", "invalid-scope-reserve"}) {
    Fixture f(mode);
    int candidates = 0, activations = 0;
    const auto candidate = f.channel->onCandidate;
    f.channel->onCandidate = [&](const auto &lease) {
      ++candidates;
      candidate(lease);
    };
    const auto activated = f.channel->onActivated;
    f.channel->onActivated = [&](const auto &lease) {
      ++activations;
      activated(lease);
    };
    f.begin();
    check(until([&] { return f.closed; }), "Unsupported host reservation did not close the private connection");
    check(
        candidates == 0 && activations == 0 && f.preparedChecks == 0 && f.releases == 0 && f.revokeNotifications == 0 &&
            f.coordinator->phase() == Phase::Stopped && !f.coordinator->lease() && !f.channel->lease() &&
            f.channel->phase() == LocalInputPhase::Idle,
        "Mismatched host scope crossed the authenticated offer boundary"
    );
    f.finish();
  }
}
void hostActivation()
{
  for (const auto mode :
       {"missing-host-activation", "missing-host-active", "reject-host-activation", "throw-host-activation",
        "cancel-host-activation", "stop-host-activation", "destroy-host-activation", "close-host-activation",
        "destroy-channel-activation", "reject-host-active", "throw-host-active", "cancel-host-active",
        "destroy-host-active", "activation-loses-integrity", "activation-loses-delivery"}) {
    Fixture f(mode);
    int acknowledged = 0;
    const auto activated = f.channel->onActivated;
    f.channel->onActivated = [&](const auto &lease) {
      ++acknowledged;
      activated(lease);
    };
    f.begin();
    check(
        until([&] { return f.closed || f.coordinator->phase() == Phase::Idle; }) && acknowledged == 0 &&
            f.channel->phase() == LocalInputPhase::Idle,
        "Missing, failed or reentrant host activation emitted a Committed acknowledgment"
    );
    f.finish();
  }
  Fixture f("reentrant-host-activation");
  f.active();
  f.finish();
}
void activeProviderWatchdog()
{
  for (const auto mode : {"host-watchdog-integrity", "host-watchdog-delivery", "host-watchdog-active"}) {
    Fixture f(mode);
    f.active();
    const auto lease = *f.coordinator->lease();
    const int neutralChecks = f.preparedChecks;
    const OriginalInputRecord down{lease.captureEpoch, 1, 100, 0, OriginalInputKey{40, 0}};
    check(f.channel->sendOriginal(lease, down), "Host watchdog fixture could not acquire a held press");
    // Host invalidates a provider after receiving this Down. No Up or further
    // Original traffic is sent; the existing channel heartbeat must revoke.
    check(
        until([&] { return f.closed || f.coordinator->phase() == Phase::Idle; }, 1800) && !f.closed &&
            f.channel->phase() == LocalInputPhase::Idle && f.releases == 1 && f.preparedChecks == neutralChecks,
        "Idle host provider loss retained authority or rechecked physical neutrality"
    );
    f.finish();
  }

  enum class Failure
  {
    Integrity,
    Delivery,
    Throw,
    Cancel,
    DestroyCoordinator,
    DestroyChannel
  };
  for (const auto failure :
       {Failure::Integrity, Failure::Delivery, Failure::Throw, Failure::Cancel, Failure::DestroyCoordinator,
        Failure::DestroyChannel}) {
    Fixture *fixture = nullptr;
    bool held = false, lost = false;
    int healthyHeldChecks = 0, failedChecks = 0, released = 0;
    auto checks = allow();
    checks.neutralBoundary = [&](const auto &) { return !held; };
    const auto query = [&](const LocalInputLease &) {
      if (!held)
        return true;
      if (!lost) {
        ++healthyHeldChecks;
        return true;
      }
      ++failedChecks;
      if (failure == Failure::Throw)
        throw std::runtime_error("Fixture active provider failed");
      if (failure == Failure::Cancel)
        fixture->coordinator->cancel();
      if (failure == Failure::DestroyCoordinator)
        fixture->coordinator.reset();
      if (failure == Failure::DestroyChannel)
        fixture->channel.reset();
      // A true result after reentrant cancellation/destruction must not undo
      // revocation or allow the heartbeat to access its former owner.
      return failure == Failure::Cancel || failure == Failure::DestroyCoordinator || failure == Failure::DestroyChannel;
    };
    if (failure == Failure::Delivery)
      checks.deliveryReady = query;
    else
      checks.codeAdmission = query;
    checks.release = [&](const auto &, auto) {
      ++released;
      held = false;
      return true;
    };
    Fixture f("normal", std::move(checks));
    fixture = &f;
    f.active();
    const auto lease = *f.coordinator->lease();
    const int neutralChecks = f.preparedChecks;
    const OriginalInputRecord down{lease.captureEpoch, 1, 100, 0, OriginalInputKey{40, 0}};
    check(f.channel->sendOriginal(lease, down), "Core watchdog fixture could not acquire a held press");
    held = true;
    check(
        until([&] { return f.closed || healthyHeldChecks >= 2; }, 1800) && !f.closed &&
            f.coordinator->phase() == Phase::Active && f.preparedChecks == neutralChecks && released == 0,
        "Healthy active heartbeat repeated neutral admission or rejected its held owner"
    );
    lost = true;
    check(
        until([&] { return f.closed || !f.coordinator || f.coordinator->phase() != Phase::Active; }, 1800) &&
            failedChecks == 1 && f.releases == 1 && released == 1 && !held &&
            (!f.channel || f.channel->phase() == LocalInputPhase::Idle) && f.preparedChecks == neutralChecks,
        "Idle core provider loss failed to revoke/release exactly once after a reentrant callback"
    );
    f.finish();
    check(released == 1, "Subsequent teardown repeated the retired owner's release");
  }
}
void selectionAndEvidence()
{
  {
    Fixture f;
    f.coordinator->replacePolicy(policy(3, 2));
    check(!f.coordinator->begin(f.capture.epoch()), "Unapproved selected Deskflow fingerprint offered");
    f.finish();
  }
  for (int change = 0; change < 5; ++change) {
    Fixture f("no-quiescence");
    f.begin();
    check(
        until([&] { return f.coordinator->phase() == Phase::Candidate || f.closed; }) && !f.closed,
        "Host candidate did not arrive"
    );
    const auto lease = *f.coordinator->lease();
    if (change == 0)
      f.revoked->store(true, std::memory_order_release);
    if (change == 1)
      f.issuer.revoke();
    if (change == 2)
      f.mailbox->clear();
    if (change == 3) {
      f.publisher.stop();
      QThread::msleep(270); // Expire the retained read, then republish the exact same selection.
      f.mailbox->replace(f.selected);
    }
    if (change == 4)
      f.capture.revoke(OriginalInputRevocation::Discontinuity);
    f.channel->onQuiesced(lease);
    check(until([&] { return f.coordinator->phase() == Phase::Idle || f.closed; }), "Revoked observation survived use");
    check(f.preparedChecks == 0, "Expired evidence reached neutral/prepare checks");
    f.finish();
  }
  {
    Fixture f("no-quiescence");
    f.begin();
    check(until([&] { return f.coordinator->phase() == Phase::Candidate; }), "No candidate for blocked read");
    f.publisher.stop();
    const auto lease = *f.coordinator->lease();
    std::atomic_bool locked{false};
    std::thread publisher([&] { SelectedPeerMailboxTestAccess::stallAndRefresh(*f.mailbox, locked); });
    while (!locked.load(std::memory_order_acquire))
      std::this_thread::yield();
    f.channel->onQuiesced(lease); // Blocks in read() across the old observation's deadline.
    publisher.join();
    check(
        f.coordinator->phase() == Phase::Idle && f.preparedChecks == 0,
        "Mailbox refresh revived evidence that expired during read"
    );
    f.finish();
  }
}
void deadlineAndQueue()
{
  {
    Fixture f("no-quiescence");
    std::optional<LocalInputLease> notified;
    std::chrono::steady_clock::time_point deadline;
    Fixture deferred("deferred-all", allow(), [&](const auto &lease, auto expires) {
      notified = lease;
      deadline = expires;
    });
    f.mailbox->replace(f.selected);
    f.begin();
    deferred.begin(ForwardingScope::All);
    check(
        until([&] { return notified || deferred.closed; }) && !deferred.closed &&
            deferred.coordinator->phase() == Phase::Quiesced,
        "Deadline fixture did not hold deferred quiescence"
    );
    check(
        until(
            [&] {
              return (f.coordinator->phase() == Phase::Idle || f.closed) &&
                     (deferred.coordinator->phase() == Phase::Idle || deferred.closed);
            },
            3500
        ),
        "Preflight deadline was extended by refresh"
    );
    check(f.preparedChecks == 0, "Unquiesced attempt reached prepare");
    check(
        !deferred.closed && std::chrono::steady_clock::now() >= deadline &&
            !deferred.coordinator->continuePreparation(*notified) && deferred.preparedChecks == 0 &&
            deferred.channel->phase() == LocalInputPhase::Idle && !deferred.channel->lease() && deferred.releases == 1,
        "Deferred continuation renewed an expired deadline or prepared its retired lease"
    );
    f.finish();
    deferred.finish();
  }
  {
    Fixture *fixture = nullptr;
    auto checks = allow();
    checks.codeAdmission = [&](const auto &lease) {
      for (int i = 0; i < 7; ++i)
        fixture->channel->onCandidate(lease);
      fixture->channel->onOfferCancelled(leaseOffer(lease), LocalInputRevocation::Requested);
      check(!fixture->closed, "Reserved eighth cancellation slot was lost");
      fixture->channel->onCandidate(lease);
      return true;
    };
    Fixture f("normal", checks);
    fixture = &f;
    f.begin();
    check(
        until([&] { return f.closed; }) && f.releases == 1 && f.preparedChecks == 0,
        "Ninth reentrant control did not fail closed and release"
    );
    f.finish();
  }
  {
    Fixture *fixture = nullptr;
    bool continued = true;
    Fixture f("deferred-all", allow(), [&](const auto &lease, auto) {
      for (int i = 0; i < 7; ++i)
        fixture->channel->onCandidate(lease);
      continued = fixture->coordinator->continuePreparation(lease);
    });
    fixture = &f;
    f.begin(ForwardingScope::All);
    check(
        until([&] { return f.closed; }) && !continued && f.releases == 1 && f.preparedChecks == 0,
        "Deferred continuation bypassed the bounded control queue"
    );
    f.finish();
  }
}
void mappedTargetRetirementAndOrdering()
{
  Fixture *fixture = nullptr;
  int targets = 0, notifications = 0;
  const LocalInputTarget target{27, pin(3)};
  Fixture f(
      "target-all", allow(), [&](const auto &, auto) { ++notifications; },
      [&](const auto &lease, const auto &value) {
        check(value == target, "Mapped target changed before notification");
        ++targets;
        // Nested callbacks must preserve target-before-quiescence ordering.
        fixture->channel->onQuiesced(lease);
        check(fixture->coordinator->phase() == Phase::Candidate, "Quiescence bypassed the control queue");
      }
  );
  fixture = &f;
  f.begin(ForwardingScope::All);
  check(until([&] { return f.coordinator->phase() == Phase::Candidate; }), "Missing mapped candidate");
  const auto retired = *f.coordinator->lease();
  f.coordinator->cancel();
  f.channel->onInputTarget(retired, target);
  f.channel->onQuiesced(retired);
  check(!f.closed && targets == 0 && notifications == 0 && f.releases == 1, "Retired target changed authority");

  f.begin(ForwardingScope::All);
  check(until([&] { return f.coordinator->phase() == Phase::Candidate; }), "Missing replacement candidate");
  const auto current = *f.coordinator->lease();
  check(current.routeGeneration > retired.routeGeneration, "Replacement did not advance generation");
  f.channel->onInputTarget(retired, target);
  check(!f.closed && targets == 0 && f.coordinator->lease() == current, "Old target affected its replacement");
  f.channel->onInputTarget(current, target);
  check(
      !f.closed && targets == 1 && notifications == 1 && f.coordinator->phase() == Phase::Quiesced &&
          f.channel->phase() == LocalInputPhase::Idle && f.preparedChecks == 0,
      "Mapped attestation bypassed deferred authority checks or lost ordering"
  );
  f.finish();
}
void mappedTargetRejectsInvalidReplies()
{
  const LocalInputTarget validTarget{27, pin(3)};
  for (const bool retired : {false, true}) {
    for (int change = 0; change < 7; ++change) {
      int targets = 0;
      Fixture f("target-all", allow(), {}, [&](const auto &, const auto &) { ++targets; });
      f.begin(ForwardingScope::All);
      check(until([&] { return f.coordinator->phase() == Phase::Candidate; }), "Missing invalid-target candidate");
      auto lease = *f.coordinator->lease();
      auto target = validTarget;
      if (retired)
        f.coordinator->cancel();
      if (change == 0)
        ++lease.focusRequest;
      if (change == 1)
        ++lease.routeGeneration;
      if (change == 2)
        ++lease.windowShareClientIncarnation[15];
      if (change == 3)
        lease.windowShareClientIncarnation[0] = 1;
      if (change == 4)
        lease.scope = ForwardingScope::Keyboard;
      if (change == 5)
        target.viewerConnection = 0;
      if (change == 6)
        target.viewerPeerFingerprint = {};
      f.channel->onInputTarget(lease, target);
      check(f.closed && targets == 0 && f.releases == 1, "Changed/future/invalid target did not fail closed");
      f.finish();
    }
  }
  for (int change = 0; change < 4; ++change) {
    int targets = 0;
    Fixture f("target-all", allow(), [](const auto &, auto) {}, [&](const auto &, const auto &) { ++targets; });
    f.begin(ForwardingScope::All);
    check(until([&] { return f.coordinator->phase() == Phase::Candidate; }), "Missing target-order candidate");
    auto lease = *f.coordinator->lease();
    if (change < 2) {
      f.channel->onInputTarget(lease, validTarget);
      if (change == 1)
        f.channel->onQuiesced(lease);
      f.channel->onInputTarget(lease, validTarget);
    } else if (change == 2) {
      f.channel->onQuiesced(lease);
    } else {
      f.coordinator->cancel();
      f.begin(ForwardingScope::All);
      // Even an obsolete generation cannot hide a malformed target.
      f.channel->onInputTarget(lease, {});
    }
    check(f.closed && targets == (change < 2 ? 1 : 0), "Missing/duplicate/out-of-order target was accepted");
    f.finish();
  }
}
void mappedTargetCallbackCancellation()
{
  for (const bool destroy : {false, true}) {
    Fixture *fixture = nullptr;
    int targets = 0;
    Fixture f("target-all", allow(), {}, [&](const auto &lease, const auto &target) {
      ++targets;
      if (destroy) {
        fixture->coordinator.reset();
      } else {
        fixture->coordinator->cancel();
        fixture->channel->onInputTarget(lease, target);
        fixture->channel->onQuiesced(lease);
      }
    });
    fixture = &f;
    f.begin(ForwardingScope::All);
    check(until([&] { return f.coordinator->phase() == Phase::Candidate; }), "Missing reentrant-target candidate");
    const auto lease = *f.coordinator->lease();
    f.channel->onInputTarget(lease, {27, pin(3)});
    check(
        !f.closed && targets == 1 && f.releases == 1 && f.preparedChecks == 0 &&
            (!f.coordinator || f.coordinator->phase() == Phase::Idle),
        "Reentrant target callback revived a canceled owner"
    );
    f.finish();
  }
}
void changedAndFutureBindings()
{
  for (int change = 0; change < 5; ++change) {
    Fixture f("no-quiescence");
    f.begin();
    check(until([&] { return f.coordinator->phase() == Phase::Candidate; }), "No candidate for mismatch");
    auto lease = *f.coordinator->lease();
    if (change == 0)
      ++lease.focusRequest;
    if (change == 1)
      lease.windowSharePeerPin = pin(9);
    if (change == 2)
      ++lease.windowShareClientIncarnation[15];
    if (change == 3)
      ++lease.routeGeneration;
    if (change == 4)
      ++lease.captureEpoch;
    f.channel->onQuiesced(lease);
    check(until([&] { return f.closed; }), "Changed/future binding was not terminal");
    f.finish();
  }
}
void reentrantChecksAndPolicy()
{
  {
    Fixture *fixture = nullptr;
    bool attempted = false, restarted = false;
    auto checks = allow();
    checks.codeAdmission = [&](const auto &lease) {
      if (!attempted) {
        attempted = true;
        fixture->coordinator->cancel();
        auto future = lease;
        ++future.routeGeneration;
        fixture->channel->onCandidate(future);
        restarted = fixture->coordinator->begin(fixture->capture.epoch());
      }
      return true;
    };
    Fixture f("normal", checks);
    fixture = &f;
    f.begin();
    check(
        until([&] { return f.closed; }) && attempted && !restarted,
        "Nested begin made an unsolicited future candidate current"
    );
    f.finish();
  }
  {
    Fixture *fixture = nullptr;
    auto checks = allow();
    checks.codeAdmission = [&](const auto &) {
      fixture->coordinator.reset();
      return true;
    };
    Fixture f("normal", checks);
    fixture = &f;
    f.begin();
    check(until([&] { return !f.coordinator; }), "Reentrant destruction callback did not execute");
    check(f.channel->phase() == LocalInputPhase::Idle && f.releases == 1, "Destruction callback granted authority");
    f.channel->close();
  }
  {
    Fixture f;
    f.active();
    f.coordinator->replacePolicy(policy(3, 4));
    check(
        f.coordinator->phase() == Phase::Idle && f.releases == 1 && !f.coordinator->begin(f.capture.epoch()),
        "Policy replacement failed to revoke before approval changed"
    );
    f.finish();
  }
  {
    Fixture f("release-failure");
    f.active();
    f.coordinator->cancel();
    check(until([&] { return f.closed; }), "Failed host release resumed an unsafe route");
    f.finish();
  }
}
void destructiveReleaseCallbacks()
{
  for (const bool active : {false, true}) {
    Fixture *fixture = nullptr;
    auto checks = allow();
    checks.release = [&](const auto &, auto) {
      fixture->channel.reset();
      return true;
    };
    Fixture f(active ? "normal" : "no-quiescence", std::move(checks));
    fixture = &f;
    if (active)
      f.active();
    else {
      f.begin();
      check(until([&] { return f.coordinator->phase() == Phase::Candidate; }), "No candidate for destructive close");
    }
    // Candidate closure invokes the coordinator through onClosed, while active
    // closure first invokes onRevoked. Either path can lose its channel during
    // the owner-release callback, so neither copied prior callback may follow.
    f.channel->close();
    check(
        !f.channel && !f.closed && f.revokeNotifications == 0 && f.releases == 1 &&
            f.coordinator->phase() != Phase::Active,
        "Copied channel callback ran after destructive owner release"
    );
    check(
        until([&] { return f.childProcess.state() == QProcess::NotRunning; }) &&
            f.childProcess.exitStatus() == QProcess::NormalExit && f.childProcess.exitCode() == 0,
        "Child did not stop after destructive owner release"
    );
  }
}
void captureEpochAuthority()
{
  {
    Fixture f;
    check(!f.coordinator->begin({}), "Missing capture continuity began preflight");
    OriginalInputEpoch destroyed;
    {
      OriginalInputStream temporary;
      destroyed = temporary.epoch();
    }
    check(!f.coordinator->begin(destroyed), "Destroyed capture stream began preflight");
    for (const auto reason :
         {OriginalInputRevocation::Overflow, OriginalInputRevocation::Emergency, OriginalInputRevocation::Discontinuity,
          OriginalInputRevocation::Stopped}) {
      OriginalInputStream ended;
      ended.revoke(reason);
      check(!f.coordinator->begin(ended.epoch()), "Revoked capture epoch began preflight");
    }
    check(
        f.coordinator->phase() == Phase::Idle && f.preparedChecks == 0, "Missing continuity reached preparation checks"
    );
    f.finish();
  }
  {
    Fixture f("capture-offered");
    f.begin();
    f.capture.revoke(OriginalInputRevocation::Discontinuity);
    check(
        until([&] { return f.coordinator->phase() == Phase::Idle || f.closed; }) && !f.closed && f.preparedChecks == 0,
        "Capture revoked during offered phase reached Prepare"
    );
    f.finish();
  }
  for (const bool atUse : {false, true}) {
    Fixture f("capture-active");
    f.active();
    const auto lease = *f.coordinator->lease();
    f.capture.revoke(OriginalInputRevocation::Overflow);
    if (atUse) {
      OriginalInputRecord record{lease.captureEpoch, 900, 1000, 0, OriginalInputKey{40, 0}};
      check(!f.channel->sendOriginal(lease, record), "Revoked capture delivered before a timer could run");
    } else {
      check(
          until([&] { return f.coordinator->phase() == Phase::Idle || f.closed; }),
          "Preflight heartbeat retained revoked capture authority"
      );
    }
    check(
        !f.closed && f.coordinator->phase() == Phase::Idle && f.channel->phase() == LocalInputPhase::Idle &&
            f.releases == 1,
        "Capture revocation failed to synchronously release its exact owner"
    );
    check(!f.coordinator->begin(f.capture.epoch()), "Revoked retained capture epoch restarted");
    f.finish();
  }
  for (int callback = 0; callback < 3; ++callback) {
    Fixture *fixture = nullptr;
    auto checks = allow();
    const auto invalidate = [&](const LocalInputLease &) {
      fixture->capture.revoke(OriginalInputRevocation::Emergency);
      return true;
    };
    if (callback == 0)
      checks.codeAdmission = invalidate;
    else if (callback == 1)
      checks.deliveryReady = invalidate;
    else
      checks.neutralBoundary = invalidate;
    Fixture f("capture-callback", std::move(checks));
    fixture = &f;
    f.begin();
    check(
        until([&] { return f.coordinator->phase() == Phase::Idle || f.closed; }) && !f.closed &&
            f.channel->phase() == LocalInputPhase::Idle && f.releases == 1,
        "Reentrant capture revocation granted preparation authority"
    );
    f.finish();
  }
  {
    Fixture f("no-quiescence");
    const auto old = f.capture.epoch();
    f.begin();
    check(until([&] { return f.coordinator->phase() == Phase::Candidate; }), "No candidate for epoch reuse");
    f.coordinator->cancel();
    check(f.coordinator->begin(old), "Live same-epoch selection could not be reused");
    check(until([&] { return f.coordinator->phase() == Phase::Candidate; }), "Same-epoch candidate missing");
    f.coordinator->cancel();
    OriginalInputStream newer;
    check(newer.epoch().id() > old.id() && f.coordinator->begin(newer.epoch()), "Fresh capture epoch did not begin");
    check(until([&] { return f.coordinator->phase() == Phase::Candidate; }), "Fresh-epoch candidate missing");
    check(f.coordinator->offer()->captureEpoch == newer.epoch().id(), "Offer lost exact capture identity");
    f.coordinator->cancel();
    check(!f.coordinator->begin(old), "Capture epoch regressed after newer attempt without any Original traffic");
    f.finish();
  }
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  try {
    const auto args = app.arguments();
    if (args.size() == 4 && args[1] == "--child")
      return child(args[2].toInt(), args[3]);
    policyValidation();
    successAndRetirement();
    fullInputScope();
    consecutiveControlAndOriginal();
    defaultDeny();
    deferredPreparation();
    deferredCancellationAndStale();
    deferredChecks();
    deferredNotificationReentry();
    hostFailureCases();
    unsupportedReservationScope();
    hostActivation();
    activeProviderWatchdog();
    selectionAndEvidence();
    deadlineAndQueue();
    mappedTargetRetirementAndOrdering();
    mappedTargetRejectsInvalidReplies();
    mappedTargetCallbackCancellation();
    changedAndFutureBindings();
    reentrantChecksAndPolicy();
    destructiveReleaseCallbacks();
    captureEpochAuthority();
    QTextStream(stdout) << "Local input preflight checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << error.what() << '\n';
    return 1;
  }
}
