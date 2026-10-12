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
  result.moduleIntegrity = result.deliveryReady = result.neutralBoundary = [](const auto &) { return true; };
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
  bool closed = false, quiescent = false, reserved = false;
  int releases = 0, reads = 0, originals = 0;
  channel->onClosed = [&](QString) { closed = true; };
  channel->onOriginal = [&](const auto &, const auto &) { ++originals; };
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
  host.reserve = [&]() -> std::optional<KeyboardRouteBinding> {
    reserved = true;
    if (mode == "destroy-reserve")
      coordinator.reset();
    if (mode == "throw-reserve")
      throw std::runtime_error("Fixture reserve failed");
    return binding();
  };
  host.startQuiescence = [&](const auto &candidate) {
    check(candidate == binding(), "Host started a different reservation");
    check(channel->phase() == LocalInputPhase::Idle, "Preflight acquired channel authority");
    if (mode == "no-quiescence")
      return true;
    quiescent = true;
    coordinator->quiesced(candidate);
    return true;
  };
  host.isQuiesced = [&](const auto &candidate) { return quiescent && candidate == binding() && mode != "stale-focus"; };
  auto checks = allow();
  checks.release = [&](const auto &lease, auto) {
    check(
        lease.session == binding().owner.session && lease.keyboardRouteGeneration == binding().generation,
        "Release changed host owner"
    );
    ++releases;
    reserved = false;
    quiescent = false;
    return mode != "release-failure";
  };
  if (mode == "missing-host-neutral")
    checks.neutralBoundary = {};
  coordinator = LocalInputPreflight::host(*channel, policy(), std::move(host), std::move(checks));
  check(until([&] { return closed; }, 12000), "Fixture child did not close");
  if (mode == "destroy-reserve")
    check(!reserved && releases == 1, "Destroyed reservation was not released exactly once");
  if (mode != "normal")
    check(originals == 0, "Original input escaped failed preflight");
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
  std::shared_ptr<std::atomic_bool> revoked = std::make_shared<std::atomic_bool>(false);
  SelectedPeer selected;
  QTimer publisher;
  bool closed = false;
  int releases = 0, preparedChecks = 0, revokeNotifications = 0;
  QString closeReason;

  explicit Fixture(const QString &mode = "normal", LocalInputPreflight::Checks checks = allow())
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
    coordinator = LocalInputPreflight::core(*channel, mailbox, policy(), std::move(checks));
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
  void begin()
  {
    check(coordinator->begin(), "Preflight did not offer selected peer");
    check(channel->phase() == LocalInputPhase::Idle, "Offer acquired channel authority");
  }
  void active()
  {
    begin();
    check(
        until([&] { return coordinator->phase() == Phase::Active || closed; }) && !closed &&
            channel->phase() == LocalInputPhase::Active,
        "Preflight did not activate"
    );
  }
  void finish()
  {
    coordinator->stop();
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
  const std::array zero{LocalInputPeerPair{{}, pin(2)}};
  check(!LocalInputPeerPolicy::create(zero), "Empty fingerprint accepted");
  std::array<LocalInputPeerPair, 9> excess;
  for (std::size_t i = 0; i < excess.size(); ++i)
    excess[i] = {pin(static_cast<std::uint8_t>(i + 1)), pin(2)};
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
          lease.focusRequest == 13 && lease.keyboardRouteGeneration == 17 && f.preparedChecks >= 2,
      "Preflight lost selection, focus request, or neutral checks"
  );
  OriginalKeyEvent event;
  event.sample.sequence = 1;
  event.sample.timestampNs = 100;
  event.sample.keyboardType = 40;
  event.sample.kind = OriginalKeyKind::Down;
  event.transition = OriginalKeyTransition::Down;
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
void consecutiveControlAndOriginal()
{
  {
    Fixture f("same-batch-cancel");
    f.active();
    const auto lease = *f.coordinator->lease();
    OriginalKeyEvent event;
    event.sample.sequence = 1;
    event.sample.timestampNs = 100;
    event.sample.keyboardType = 40;
    event.sample.kind = OriginalKeyKind::Down;
    event.transition = OriginalKeyTransition::Down;
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
      OriginalKeyEvent event;
      event.sample.sequence = 1;
      event.sample.timestampNs = 100;
      event.sample.keyboardType = 40;
      event.sample.kind = OriginalKeyKind::Down;
      event.transition = OriginalKeyTransition::Down;
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
      checks.moduleIntegrity = {};
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
void selectionAndEvidence()
{
  {
    Fixture f;
    f.coordinator->replacePolicy(policy(3, 2));
    check(!f.coordinator->begin(), "Unapproved selected Deskflow fingerprint offered");
    f.finish();
  }
  for (int change = 0; change < 4; ++change) {
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
    f.begin();
    check(
        until([&] { return f.coordinator->phase() == Phase::Idle || f.closed; }, 3500),
        "Preflight deadline was extended by refresh"
    );
    check(f.preparedChecks == 0, "Unquiesced attempt reached prepare");
    f.finish();
  }
  {
    Fixture *fixture = nullptr;
    auto checks = allow();
    checks.moduleIntegrity = [&](const auto &lease) {
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
}
void changedAndFutureBindings()
{
  for (int change = 0; change < 4; ++change) {
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
    checks.moduleIntegrity = [&](const auto &lease) {
      if (!attempted) {
        attempted = true;
        fixture->coordinator->cancel();
        auto future = lease;
        ++future.routeGeneration;
        fixture->channel->onCandidate(future);
        restarted = fixture->coordinator->begin();
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
    checks.moduleIntegrity = [&](const auto &) {
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
        f.coordinator->phase() == Phase::Idle && f.releases == 1 && !f.coordinator->begin(),
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
    consecutiveControlAndOriginal();
    defaultDeny();
    hostFailureCases();
    selectionAndEvidence();
    deadlineAndQueue();
    changedAndFutureBindings();
    reentrantChecksAndPolicy();
    destructiveReleaseCallbacks();
    QTextStream(stdout) << "Local input preflight checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << error.what() << '\n';
    return 1;
  }
}
