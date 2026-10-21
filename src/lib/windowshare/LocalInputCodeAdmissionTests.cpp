/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputCodeAdmission.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTextStream>
#include <QThread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <unistd.h>

// This dedicated executable substitutes the verifier at link time. Production
// has no injected verifier, test-result constructor, or conditional trust path.
#ifndef LOCAL_INPUT_CODE_ADMISSION_TEST_VERIFIER
#error "Build only this test with its fixture verifier, without the production verifier object"
#endif

namespace {
thread_local bool failNextAllocation = false;
}
// Keep the replacement allocation pair opaque at call sites. GCC's optimized
// mismatched-new-delete check otherwise folds the replacement delete's free()
// into callers of new while treating new as the built-in allocator. The pair
// deliberately uses malloc/free so the one-shot failure remains deterministic.
Q_NEVER_INLINE void *operator new(std::size_t size)
{
  if (failNextAllocation) {
    failNextAllocation = false;
    throw std::bad_alloc();
  }
  if (void *memory = std::malloc(size ? size : 1))
    return memory;
  throw std::bad_alloc();
}
Q_NEVER_INLINE void operator delete(void *memory) noexcept
{
  std::free(memory);
}
Q_NEVER_INLINE void operator delete(void *memory, std::size_t) noexcept
{
  std::free(memory);
}

using namespace deskflow::windowshare;
namespace {
using Clock = std::chrono::steady_clock;
using State = LocalInputCodeAdmission::State;
enum class Outcome
{
  Pass,
  FactoryDenied,
  Denied,
  Throws,
  WrongSelf,
  WrongPeer,
  WrongRole,
  OldInspection,
  FutureInspection
};
struct Plan
{
  Outcome outcome = Outcome::Pass;
  std::optional<LocalInputProcess> anchor;
  std::thread::id owner = std::this_thread::get_id();
  std::atomic<int> factoryCalls = 0, inspections = 0;
  std::atomic<bool> entered = false, exited = false, onOwner = false;
  std::mutex mutex;
  std::condition_variable condition;
  bool blocked = false;

  void unblock()
  {
    const std::lock_guard lock(mutex);
    blocked = false;
    condition.notify_all();
  }
};
std::mutex planMutex;
std::shared_ptr<Plan> nextPlan;

void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
bool until(const std::function<bool()> &condition, int timeoutMs = 3000)
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
std::shared_ptr<Plan> plan(Outcome outcome = Outcome::Pass, bool blocked = false)
{
  auto value = std::make_shared<Plan>();
  value->outcome = outcome;
  value->blocked = blocked;
  const std::lock_guard lock(planMutex);
  nextPlan = value;
  return value;
}
} // namespace

namespace deskflow::windowshare {
struct LocalInputPeerTrust::Impl
{
  std::shared_ptr<Plan> plan;
  LocalInputRole role;
};
LocalInputPeerTrust::LocalInputPeerTrust(std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
{
}
LocalInputPeerTrust::~LocalInputPeerTrust()
{
  m_impl->plan->exited.store(true);
}
std::unique_ptr<LocalInputPeerTrust> LocalInputPeerTrust::forPackagedPeer(LocalInputRole peer, QString *error)
{
  std::shared_ptr<Plan> value;
  {
    const std::lock_guard lock(planMutex);
    value = nextPlan;
  }
  if (!value)
    throw std::runtime_error("No fixture inspection plan");
  ++value->factoryCalls;
  value->onOwner.store(value->owner == std::this_thread::get_id());
  if (value->outcome == Outcome::FactoryDenied) {
    if (error)
      *error = QStringLiteral("Fixture packaged-peer construction denied");
    value->exited.store(true);
    return nullptr;
  }
  return std::unique_ptr<LocalInputPeerTrust>(new LocalInputPeerTrust(std::make_unique<Impl>(Impl{value, peer})));
}
std::optional<LocalInputExactPermittedLibrariesSnapshot>
LocalInputPeerTrust::inspectExactPermittedLibraries(LocalInputProcess expected, QString *error) const
{
  const auto &value = m_impl->plan;
  auto started = Clock::now();
  ++value->inspections;
  value->entered.store(true);
  {
    std::unique_lock lock(value->mutex);
    value->condition.wait(lock, [&] { return !value->blocked; });
  }
  if (value->outcome == Outcome::Denied) {
    if (error)
      *error = QString(800, 'x');
    return std::nullopt;
  }
  if (value->outcome == Outcome::Throws)
    throw std::runtime_error("Fixture native inspector failed");
  auto self = currentLocalInputProcess().value();
  const auto anchor = value->anchor.value_or(m_impl->role == LocalInputRole::Host ? self : expected);
  auto role = m_impl->role;
  if (value->outcome == Outcome::WrongSelf)
    ++self.startStamp;
  if (value->outcome == Outcome::WrongPeer)
    ++expected.startStamp;
  if (value->outcome == Outcome::WrongRole)
    role = role == LocalInputRole::Host ? LocalInputRole::Core : LocalInputRole::Host;
  if (value->outcome == Outcome::OldInspection)
    started -= std::chrono::hours(1);
  auto finished = Clock::now();
  if (value->outcome == Outcome::FutureInspection)
    finished += std::chrono::hours(1);
  auto restricted = LocalInputRestrictedLibraryAdmissionSnapshot(
      self, expected, anchor, role, QByteArray(20, 's'), QByteArray(20, 'p'), QByteArray(20, 'a'), "TESTTEAM01",
      started, finished
  );
  return LocalInputExactPermittedLibrariesSnapshot(
      std::move(restricted), QByteArray(32, 'm'), {QByteArray(20, 'l')}, 1, 1, started, finished
  );
}
struct LocalInputChannelTestAccess
{
  static void freeze(LocalInputChannel &channel)
  {
    channel.m_timer.stop();
    QObject::disconnect(&channel.m_socket, nullptr, &channel, nullptr);
  }
};
} // namespace deskflow::windowshare

namespace {
constexpr LocalInputLimits limits{30, 1000, 1500};
struct Fixture
{
  QProcess child;
  std::unique_ptr<LocalInputChannel> channel;

  Fixture()
  {
    auto pair = LocalInputSocketPair::create();
    check(pair.childDescriptor() >= 3, "Cannot create fixture socketpair");
    pair.configureChild(child);
    child.start(QCoreApplication::applicationFilePath(), {"--child", QString::number(pair.childDescriptor())});
    check(child.waitForStarted(3000), "Cannot start fixture child");
    const auto expected = localInputProcess(child.processId());
    check(bool(expected), "Cannot read fixture process birth");
    channel = pair.adoptCore(*expected, limits);
    check(until([&] { return channel->authenticated(); }), "Fixture private channel did not authenticate");
  }
  ~Fixture()
  {
    if (channel) {
      channel->onClosed = {};
      channel->close();
      channel.reset();
    }
    if (child.state() != QProcess::NotRunning) {
      child.kill();
      child.waitForFinished(3000);
    }
  }
};

std::unique_ptr<LocalInputCodeAdmission> request(Fixture &fixture, int timeoutMs = 1000)
{
  std::unique_ptr<LocalInputCodeAdmission> result;
  QString error;
  // A preceding worker may be completing its final slot-release instruction.
  // Retry ONLY that explicit busy result, never an authority/expiry failure.
  check(
      until([&] {
        result = LocalInputCodeAdmission::request(*fixture.channel, &error, timeoutMs);
        return bool(result) || !error.contains("already in flight");
      }),
      "Inspection worker slot did not become free"
  );
  check(bool(result), qPrintable(error));
  return result;
}
LocalInputCodeAdmissionProof ready(LocalInputCodeAdmission &guard)
{
  check(until([&] { return guard.state() != State::Pending; }), "Code-admission request did not finish");
  check(guard.state() == State::Ready, qPrintable(guard.error()));
  const auto proof = guard.proof();
  check(proof && proof->live(), "Ready guard has no live proof");
  return *proof;
}

void resultAndThreadBoundary()
{
  Fixture fixture;
  auto value = plan(Outcome::Pass, true);
  const auto started = Clock::now();
  auto guard = request(fixture);
  check(Clock::now() - started < std::chrono::milliseconds(100), "Request waited for native inspection");
  check(guard->state() == State::Pending && !guard->proof(), "Blocked inspection minted early authority");
  check(until([&] { return value->entered.load(); }), "Inspection worker did not run");
  QString error;
  check(!LocalInputCodeAdmission::request(*fixture.channel, &error), "Concurrent inspection bypassed worker bound");
  check(error.contains("already in flight"), "Busy inspection did not explain denial");
  check(value->factoryCalls == 1 && value->inspections == 1 && !value->onOwner, "Inspector ran on the owning thread");
  value->unblock();
  auto proof = ready(*guard);
  const auto copy = proof;
  check(proof.connection() == fixture.channel->authenticatedIdentity(), "Proof changed private-channel identity");
  check(proof.inspection().manifestHash() == QByteArray(32, 'm'), "Proof changed exact policy metadata");
  check(proof.inspection().restrictedAdmission().peerProcess() == proof.connection().peer, "Wrong peer was admitted");
  std::thread foreign([&] {
    check(!proof.live(), "Foreign thread used retained authority");
    check(
        guard->state() == State::Denied && !guard->proof() && !guard->error().isEmpty(), "Foreign guard use succeeded"
    );
    check(!LocalInputCodeAdmission::request(*fixture.channel, &error), "Foreign request launched an inspection");
    check(!fixture.channel->authenticatedIdentity(), "Foreign channel query returned authority");
  });
  foreign.join();
  for (int i = 0; i < 10; ++i)
    check(proof.live() && guard->state() == State::Ready, "Owner lost valid proof after foreign denial");
  check(value->factoryCalls == 1 && value->inspections == 1, "Fast proof checks repeated signature inspection");
  guard->revoke();
  check(!proof.live() && !copy.live() && guard->state() == State::Revoked, "Revoke did not invalidate retained copies");
  check(fixture.channel->authenticated(), "Admission revocation closed ordinary private channel");

  value = plan();
  guard = request(fixture);
  auto replacement = ready(*guard);
  check(!proof.live() && replacement.live(), "New admission revived a revoked proof");
  guard.reset();
  check(!replacement.live(), "Destroyed guard left live retained proof");
}

void failuresAndBounds()
{
  Fixture fixture;
  for (const auto outcome :
       {Outcome::FactoryDenied, Outcome::Denied, Outcome::Throws, Outcome::WrongSelf, Outcome::WrongPeer,
        Outcome::WrongRole, Outcome::OldInspection, Outcome::FutureInspection}) {
    auto value = plan(outcome);
    auto guard = request(fixture);
    check(until([&] { return guard->state() != State::Pending; }), "Denied inspection stayed pending");
    check(
        guard->state() == State::Denied && !guard->proof() && !guard->error().isEmpty(),
        "Invalid inspection was admitted"
    );
    check(guard->error().size() <= 512, "Unbounded inspection diagnostic");
    check(fixture.channel->authenticated(), "Denied inspection changed channel authority");
    check(value->factoryCalls == 1, "Unexpected verifier retry");
  }
  auto value = plan();
  QString error;
  for (const int budget : {-1, 0, 99, 30001})
    check(!LocalInputCodeAdmission::request(*fixture.channel, &error, budget), "Invalid admission budget was accepted");
  check(value->factoryCalls == 0, "Rejected budget started a verifier");
  fixture.channel->close();
  check(!LocalInputCodeAdmission::request(*fixture.channel, &error), "Closed channel began admission");
}

void deadlineAndDetachedLifetime()
{
  Fixture fixture;
  auto value = plan(Outcome::Pass, true);
  auto guard = request(fixture, 100);
  check(until([&] { return value->entered.load(); }), "Blocked worker did not enter");
  QThread::msleep(125);
  check(
      guard->state() == State::Denied && guard->error().contains("expired"), "Absolute caller deadline did not expire"
  );
  QString error;
  check(!LocalInputCodeAdmission::request(*fixture.channel, &error), "Expired work was hidden from in-flight bound");
  const auto destroyed = Clock::now();
  guard.reset();
  check(Clock::now() - destroyed < std::chrono::milliseconds(100), "Guard destruction waited for blocked inspector");
  value->unblock();
  check(until([&] { return value->exited.load(); }), "Detached inspection did not finish after owner destruction");

  value = plan();
  guard = request(fixture, 100);
  check(until([&] { return value->exited.load(); }), "Fast inspection did not finish");
  QThread::msleep(125);
  check(guard->state() == State::Denied && !guard->proof(), "Late owner poll revived an expired completed inspection");

  value = plan(Outcome::Pass, true);
  guard = request(fixture);
  check(until([&] { return value->entered.load(); }), "Revocation fixture did not enter");
  guard->revoke();
  value->unblock();
  check(until([&] { return value->exited.load(); }), "Revoked worker did not finish");
  check(guard->state() == State::Revoked && !guard->proof(), "Late success revived an explicitly revoked request");
}

void channelLossAndReplacement()
{
  Fixture fixture;
  auto value = plan();
  auto guard = request(fixture);
  auto proof = ready(*guard);
  bool closedSawRevocation = false;
  fixture.channel->onClosed = [&](QString) { closedSawRevocation = !proof.live(); };
  fixture.channel->close();
  check(closedSawRevocation && !proof.live(), "Channel close callback retained code authority");
  check(guard->state() == State::Revoked && !guard->proof(), "Closed channel guard stayed ready");

  Fixture replacement;
  value = plan();
  auto nextGuard = request(replacement);
  auto next = ready(*nextGuard);
  check(next.connection().coreIncarnation != proof.connection().coreIncarnation, "Reconnect reused core incarnation");
  check(next.connection().hostIncarnation != proof.connection().hostIncarnation, "Reconnect reused host incarnation");
  check(next.live() && !proof.live(), "Reconnect revived an old channel proof");
  replacement.channel.reset();
  check(!next.live() && nextGuard->state() == State::Revoked, "Channel destruction left live proof");

  Fixture pending;
  value = plan(Outcome::Pass, true);
  guard = request(pending);
  check(until([&] { return value->entered.load(); }), "Pending loss fixture did not enter");
  pending.channel.reset();
  value->unblock();
  check(until([&] { return value->exited.load(); }), "Lost-channel worker did not finish");
  check(guard->state() == State::Revoked && !guard->proof(), "Worker completion admitted a destroyed channel");
}

void processAndFreshnessLoss()
{
  Fixture fixture;
  QProcess anchor;
  anchor.start(QCoreApplication::applicationFilePath(), {"--idle"});
  check(anchor.waitForStarted(3000), "Cannot start separate anchor fixture");
  auto value = plan();
  value->anchor = localInputProcess(anchor.processId());
  check(bool(value->anchor), "Cannot inspect anchor fixture identity");
  auto guard = request(fixture);
  check(until([&] { return guard->state() != State::Pending; }), "Distinct-anchor inspection did not finish");
  check(
      guard->state() == State::Denied && guard->error().contains("Core endpoint"),
      "Distinct ancestor minted retained proof"
  );
  anchor.kill();
  check(anchor.waitForFinished(3000), "Anchor fixture did not exit");
  check(fixture.channel->authenticated(), "Denied external anchor fixture closed unrelated channel");

  value = plan();
  guard = request(fixture);
  auto proof = ready(*guard);
  fixture.child.kill();
  check(fixture.child.waitForFinished(3000), "Peer fixture did not exit");
  check(!proof.live(), "Dead peer retained authority before queued disconnect callback");

  Fixture expired;
  value = plan();
  guard = request(expired);
  proof = ready(*guard);
  LocalInputChannelTestAccess::freeze(*expired.channel);
  QThread::msleep(limits.timeoutMs + 25);
  check(!proof.live() && !expired.channel->authenticatedIdentity(), "Unserviced heartbeat expiry retained proof");
  check(guard->state() == State::Revoked && !guard->proof(), "Expired proof remained published");
}

void publicationAllocationFailure()
{
  Fixture fixture;
  auto value = plan();
  auto guard = request(fixture);
  check(until([&] { return value->exited.load(); }), "Allocation fixture inspection did not complete");
  State state = State::Pending;
  check(
      until([&] {
        // If the worker has not published yet, retry without assuming a scheduling
        // delay establishes completion. Only this thread receives the fault.
        failNextAllocation = true;
        state = guard->state();
        const bool consumed = !failNextAllocation;
        failNextAllocation = false;
        return consumed;
      }),
      "Publication fault did not reach an owner-thread allocation"
  );
  check(state == State::Denied || state == State::Revoked, "Allocation failure left a pending/ready guard");
  check(!guard->proof() && guard->state() == state, "Failed publication reused its cleared worker job");
  check(fixture.channel->authenticated(), "Publication failure closed ordinary channel");
}

int child(QCoreApplication &application, int descriptor)
{
  const auto parent = localInputProcess(getppid());
  check(bool(parent), "Cannot inspect test parent");
  auto channel = LocalInputChannel::adoptHost(descriptor, *parent, limits);
  channel->onAuthenticated = [&] {
    const auto identity = channel->authenticatedIdentity();
    check(identity && identity->role == LocalInputRole::Host && identity->peer == *parent, "Host identity mismatch");
  };
  channel->onClosed = [&](QString) { application.quit(); };
  QTimer::singleShot(10000, &application, &QCoreApplication::quit);
  return application.exec();
}
} // namespace

int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  try {
    const auto arguments = application.arguments();
    if (arguments.size() == 3 && arguments[1] == "--child") {
      bool ok = false;
      const auto descriptor = arguments[2].toInt(&ok);
      check(ok && descriptor >= 3, "Invalid fixture descriptor");
      return child(application, descriptor);
    }
    if (arguments.size() == 2 && arguments[1] == "--idle") {
      QTimer::singleShot(10000, &application, &QCoreApplication::quit);
      return application.exec();
    }
    check(arguments.size() == 1, "Unexpected code-admission fixture argument");
    resultAndThreadBoundary();
    failuresAndBounds();
    deadlineAndDetachedLifetime();
    channelLossAndReplacement();
    processAndFreshnessLoss();
    publicationAllocationFailure();
    QTextStream(stdout) << "PASS: optional code admission, bounded worker, immutable facts, owner-thread checks, "
                           "absolute deadline, late-result denial, channel/process/anchor revocation and reconnect\n";
    return 0;
  } catch (const std::exception &failure) {
    // Ensure a deliberately stalled fixture cannot outlive stack unwinding in
    // the failed test process; production has no controllable inspection seam.
    std::shared_ptr<Plan> current;
    {
      const std::lock_guard lock(planMutex);
      current = nextPlan;
    }
    if (current)
      current->unblock();
    QTextStream(stderr) << "FAIL: " << failure.what() << '\n';
    return 1;
  }
}
