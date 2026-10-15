/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputCompanion.h"
#include "LocalInputPeerTrust.h"
#include "LocalInputSession.h"
#ifdef __APPLE__
#include "platform/OriginalInputCaptureSource_mac.h"
#endif

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>
#include <vector>

// This executable links this stub instead of the packaged-code implementation.
// Production has no verifier injection API or build-time trust bypass.
#ifndef LOCAL_INPUT_COMPANION_TEST_VERIFIER
#error "Build this dedicated test with its test verifier, without the production verifier object"
#endif

using namespace deskflow::windowshare;
namespace {
struct TestTrust
{
  bool factory = true, verify = true;
  int verifications = 0, delayMs = 0;
  QString executable;
  std::optional<LocalInputProcess> observed;
} trust;
} // namespace
namespace deskflow::windowshare {
struct LocalInputPeerTrust::Impl
{
  QString executable;
};
LocalInputPeerTrust::LocalInputPeerTrust(std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
{
}
LocalInputPeerTrust::~LocalInputPeerTrust() = default;
std::unique_ptr<LocalInputPeerTrust> LocalInputPeerTrust::forPackagedPeer(LocalInputRole expectedPeer, QString *error)
{
  if (!trust.factory || expectedPeer != LocalInputRole::Host) {
    if (error)
      *error = QStringLiteral("Fixture has no trusted Host");
    return {};
  }
  return std::unique_ptr<LocalInputPeerTrust>(new LocalInputPeerTrust(std::make_unique<Impl>(Impl{trust.executable})));
}
QString LocalInputPeerTrust::executable() const
{
  return m_impl->executable;
}
bool LocalInputPeerTrust::verify(LocalInputProcess expected) const
{
  ++trust.verifications;
  trust.observed = expected;
  if (trust.delayMs)
    QThread::msleep(trust.delayMs);
  return trust.verify && localInputProcess(expected.pid) == expected;
}
#ifdef __APPLE__
struct WindowDragEdgeMailboxTestAccess
{
  static bool bind(WindowDragEdgeMailbox &mailbox)
  {
    return mailbox.bind(91);
  }
  static bool publish(WindowDragEdgeMailbox &mailbox, WindowDragEdge edge)
  {
    return mailbox.publish(std::move(edge));
  }
};
struct LocalInputCoreSessionTestAccess
{
  static std::unique_ptr<LocalInputCoreSession> captureOnly()
  {
    auto session = std::unique_ptr<LocalInputCoreSession>(new LocalInputCoreSession);
    session->m_captureSource = std::make_shared<OriginalInputCaptureSource>();
    return session;
  }
  static void drain(LocalInputCoreSession &session)
  {
    session.drainCapturedInput();
  }
  static bool barrier(const LocalInputCoreSession &session)
  {
    return bool(session.m_captureBarrier);
  }
  static bool emergencyStopped(const LocalInputCoreSession &session)
  {
    return session.m_captureEmergencyHandled && !session.m_captureTimer.isActive() && !session.m_captureBarrier;
  }
  static QPointer<LocalInputCompanion> companion(const LocalInputCoreSession &session)
  {
    return session.m_companion.get();
  }
};
#endif
} // namespace deskflow::windowshare

namespace {
constexpr LocalInputCompanionLimits limits{1000, 40, 80, {20, 400, 400}};
std::vector<LocalInputSocketPair> pairs;

void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
bool until(const std::function<bool()> &condition, int timeoutMs = 3000)
{
  if (condition())
    return true;
  bool observed = false;
  QEventLoop loop;
  QTimer poll, timeout;
  poll.setInterval(2);
  timeout.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (!observed && condition()) {
      observed = true;
      loop.quit();
    }
  });
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start();
  timeout.start(timeoutMs);
  loop.exec();
  return observed;
}
LocalInputSocketPair pair()
{
  check(!pairs.empty(), "Fixture ran out of socketpairs created before Qt");
  auto value = std::move(pairs.back());
  pairs.pop_back();
  return value;
}
void resetTrust()
{
  trust = {};
  trust.executable = QCoreApplication::applicationFilePath();
}
std::unique_ptr<LocalInputCompanion> companion(LocalInputCompanionLimits deadlines = limits)
{
  QString error;
  auto result = LocalInputCompanion::create(pair(), &error, deadlines);
  check(bool(result), qPrintable(error));
  return result;
}
void stopped(LocalInputCompanion &launcher)
{
  check(until([&] { return launcher.state() == LocalInputCompanion::State::Stopped; }), "Companion failed to stop");
  const auto child = launcher.childProcess();
  check(!child || localInputProcess(child->pid) != child, "Stopped companion still has a live child");
}

int hostFixture(QCoreApplication &application, int descriptor, const QString &mode)
{
  if (mode == "exit")
    return 12;
  if (mode == "ignore-term")
    std::signal(SIGTERM, SIG_IGN);
  QTimer::singleShot(5000, &application, &QCoreApplication::quit);
  if (mode == "idle")
    return application.exec();
  if (mode == "environment") {
    for (const auto &name :
         {"DYLD_INSERT_LIBRARIES", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "DYLD_FIXTURE", "QT_PLUGIN_PATH",
          "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QML_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"})
      check(!qEnvironmentVariableIsSet(name), "Launcher leaked a loader/plugin override");
    check(qgetenv("QT_QPA_PLATFORM") == "offscreen", "Launcher removed the permitted platform selection");
    check(qgetenv("WINDOWSHARE_FIXTURE_NORMAL") == "kept", "Launcher removed ordinary host environment");
  }
  const auto parent = localInputProcess(getppid());
  check(parent.has_value(), "Fixture parent has no kernel birth identity");
  auto channelLimits = limits.channel;
  if (mode == "slow-verification" || mode == "slow-adoption")
    channelLimits.handshakeMs = 1600;
  if (mode == "slow-adoption")
    QThread::msleep(600);
  auto channel = LocalInputChannel::adoptHost(descriptor, *parent, channelLimits);
  check((fcntl(descriptor, F_GETFD) & FD_CLOEXEC) != 0, "Adopted descriptor is inheritable");
  bool authenticated = false;
  channel->onClosed = [&](QString) {
    if (mode == "slow-close")
      QTimer::singleShot(2200, &application, &QCoreApplication::quit);
    else if (mode != "ignore-term")
      application.quit();
  };
  channel->onAuthenticated = [&] {
    check(channel->authenticated(), "Host authenticated callback inherited bootstrap age");
    authenticated = true;
    if (mode == "auth-exit")
      QTimer::singleShot(100, &application, &QCoreApplication::quit);
    if (mode == "auth-fail")
      QTimer::singleShot(100, &application, [&] { application.exit(12); });
    if (mode == "auth-crash")
      QTimer::singleShot(100, &application, [] { std::raise(SIGKILL); });
    if (mode == "diagnostic-output") {
      QTextStream(stderr) << "companion fixture stderr\n" << Qt::flush;
      QTextStream(stdout) << "companion fixture stdout\n" << Qt::flush;
      QTimer::singleShot(100, &application, &QCoreApplication::quit);
    }
  };
  const int result = application.exec();
  return result ? result : authenticated ? 0 : 13;
}

int diagnosticForwardingFixture(bool forward)
{
  resetTrust();
  auto diagnosticLimits = limits;
  diagnosticLimits.forwardStandardError = forward;
  auto launcher = companion(diagnosticLimits);
  std::unique_ptr<LocalInputChannel> channel;
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) {
    check(!value->authorize, "Diagnostic forwarding granted input authority");
    channel = std::move(value);
  };
  check(launcher->start({"--fixture=diagnostic-output"}), "Diagnostic companion launch failed");
  stopped(*launcher);
  check(
      launcher->exitResult() && launcher->exitResult()->normalExit && launcher->exitResult()->exitCode == 0,
      "Diagnostic companion failed its authenticated normal exit"
  );
  return 0;
}

void diagnosticForwarding()
{
  for (const bool forward : {false, true}) {
    QProcess fixture;
    auto environment = QProcessEnvironment::systemEnvironment();
    // The wrapper may emit its own Qt locale warning before launching Host.
    // Test the child's unique marker, not unrelated wrapper diagnostics.
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    fixture.setProcessEnvironment(environment);
    fixture.start(
        QCoreApplication::applicationFilePath(), {QStringLiteral("--diagnostic-forwarding=%1").arg(int(forward))}
    );
    check(until([&] { return fixture.state() == QProcess::NotRunning; }), "Diagnostic forwarding fixture hung");
    check(
        fixture.exitStatus() == QProcess::NormalExit && fixture.exitCode() == 0, "Diagnostic forwarding fixture failed"
    );
    const auto error = fixture.readAllStandardError();
    check(
        error.count("companion fixture stderr\n") == (forward ? 1 : 0),
        "Companion stderr forwarding did not respect explicit opt-in"
    );
    check(fixture.readAllStandardOutput().isEmpty(), "Diagnostic forwarding leaked companion stdout");
  }
}

void normalLaunchAndStop()
{
  resetTrust();
  auto socketpair = pair();
  const int descriptor = socketpair.childDescriptor();
  // An unrelated exec before the designated launch must not inherit the pair.
  QProcess unrelated;
  unrelated.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--expect-closed=%1").arg(descriptor)});
  check(until([&] { return unrelated.state() == QProcess::NotRunning; }), "Inheritance probe did not finish");
  check(unrelated.exitStatus() == QProcess::NormalExit && unrelated.exitCode() == 0, "Unrelated exec inherited FD");
  auto launcher = LocalInputCompanion::create(std::move(socketpair), nullptr, limits);
  check(bool(launcher), "Cannot create launcher");
  std::unique_ptr<LocalInputChannel> channel;
  int delivered = 0, completions = 0;
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) {
    ++delivered;
    check(!value->authorize, "Launcher granted native input authority");
    check(trust.verifications == 1 && launcher->childProcess() == trust.observed, "Unverified child channel delivered");
    check(fcntl(descriptor, F_GETFD) == -1 && errno == EBADF, "Parent retained the child capability after adoption");
    channel = std::move(value);
  };
  launcher->onStopped = [&](QString) {
    ++completions;
    check(!channel || !channel->authenticated(), "Process completion left channel authority live");
    launcher->stop();
  };
  check(launcher->start({"--fixture=ignore-term"}), "Fixed companion launch was rejected");
  check(!launcher->start({"--fixture=normal"}), "Launcher started a second process");
  check(until([&] { return launcher->state() == LocalInputCompanion::State::Running; }), "Private handshake failed");
  check(delivered == 1 && channel && channel->authenticated(), "Channel was not delivered exactly once");
  QElapsedTimer elapsed;
  elapsed.start();
  launcher->stop();
  launcher->stop();
  check(!channel->authenticated(), "Stop did not immediately revoke private-channel authority");
  stopped(*launcher);
  check(elapsed.elapsed() < 1500 && completions == 1, "Ignoring SIGTERM broke bounded asynchronous stop");
  check(
      launcher->exitResult() && !launcher->exitResult()->normalExit,
      "Forced termination was recorded as a normal child exit"
  );
  check(!launcher->start({"--fixture=normal"}), "Stopped launcher automatically reused its capability");
}

void refusedLaunches()
{
  resetTrust();
  trust.factory = false;
  QString error;
  check(!LocalInputCompanion::create(pair(), &error, limits) && !error.isEmpty(), "Missing trust did not fail closed");
  resetTrust();
  trust.executable = "relative-companion";
  check(!LocalInputCompanion::create(pair(), &error, limits), "Relative executable was accepted");
  resetTrust();
  for (const int handshakeMs : {-1, 0, 99, 30001}) {
    auto invalid = limits;
    invalid.channel.handshakeMs = handshakeMs;
    check(!LocalInputCompanion::create(pair(), &error, invalid), "Companion accepted invalid handshake budget");
  }
  for (const auto &arguments :
       {QStringList{"--core-input-fd=9"}, QStringList{"--core-input-fd", "9"}, QStringList{"-core-input-fd=9"},
        QStringList{QString("name") + QChar('\0') + "tail"}}) {
    resetTrust();
    auto launcher = companion();
    int completions = 0;
    launcher->onChannel = [](std::unique_ptr<LocalInputChannel>) { throw std::runtime_error("Unexpected channel"); };
    launcher->onStopped = [&](QString) { ++completions; };
    check(!launcher->start(arguments), "Caller private descriptor override was accepted");
    check(
        launcher->state() == LocalInputCompanion::State::Stopped && completions == 1 && !trust.verifications,
        "Rejected arguments launched or repeatedly completed a process"
    );
  }
  resetTrust();
  trust.executable = "/definitely-missing-windowshare-fixture";
  auto launcher = companion();
  int delivered = 0;
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel>) { ++delivered; };
  launcher->start({"--fixture=normal"});
  stopped(*launcher);
  check(!delivered && !trust.verifications, "Failed exec delivered a channel");

  resetTrust();
  trust.verify = false;
  launcher = companion();
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel>) { ++delivered; };
  check(launcher->start({"--fixture=normal"}), "Rejected-code fixture did not launch");
  stopped(*launcher);
  check(
      !delivered && trust.verifications == 1 && trust.observed &&
          localInputProcess(trust.observed->pid) != trust.observed,
      "Unverified code received authority or escaped owned-child cleanup"
  );
}

void startupAndDeath()
{
  resetTrust();
  auto deadlines = limits;
  deadlines.startupMs = 160;
  deadlines.channel.timeoutMs = 600;
  deadlines.channel.handshakeMs = 600;
  auto launcher = companion(deadlines);
  std::unique_ptr<LocalInputChannel> channel;
  QString reason;
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) { channel = std::move(value); };
  launcher->onStopped = [&](QString value) { reason = value; };
  check(launcher->start({"--fixture=idle"}), "Handshake timeout fixture did not launch");
  stopped(*launcher);
  check(
      reason.contains("stage=private-handshake") && reason.contains("elapsed=") && channel && !channel->authenticated(),
      "Startup did not bound or diagnose the handshake"
  );

  resetTrust();
  trust.delayMs = 200;
  launcher = companion(deadlines);
  bool delivered = false;
  reason.clear();
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel>) { delivered = true; };
  launcher->onStopped = [&](QString value) { reason = value; };
  check(launcher->start({"--fixture=normal"}), "Slow verification fixture did not launch");
  stopped(*launcher);
  check(!delivered, "Verification delivered a capability after the startup deadline expired");
  check(
      reason.contains("stage=peer-verification") && reason.contains("elapsed="),
      "Slow verification timeout omitted its stage and elapsed age"
  );

  resetTrust();
  launcher = companion();
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) { channel = std::move(value); };
  check(launcher->start({"--fixture=auth-exit"}), "Exit fixture did not launch");
  check(
      until([&] { return launcher->state() == LocalInputCompanion::State::Running; }), "Exit fixture never connected"
  );
  stopped(*launcher);
  check(channel && !channel->authenticated(), "Child death retained channel authority");

  resetTrust();
  launcher = companion();
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) { channel = std::move(value); };
  check(launcher->start({"--fixture=ignore-term"}), "Channel-close fixture did not launch");
  check(
      until([&] { return launcher->state() == LocalInputCompanion::State::Running; }), "Close fixture never connected"
  );
  channel->close("Fixture lost local authority");
  stopped(*launcher);
}

void coldStartupAllowance()
{
  static_assert(LocalInputCompanionLimits{}.startupMs == 15000);
  static_assert(LocalInputCompanionLimits{}.channel.handshakeMs == LocalInputLimits{}.handshakeMs);
  for (const QString mode : {"slow-verification", "slow-adoption"}) {
    resetTrust();
    if (mode == "slow-verification")
      trust.delayMs = 600;
    auto deadlines = limits;
    deadlines.startupMs = 1600;
    deadlines.channel.handshakeMs = 1600;
    auto launcher = companion(deadlines);
    std::unique_ptr<LocalInputChannel> channel;
    bool freshInCallback = false;
    launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) {
      channel = std::move(value);
      channel->onAuthenticated = [&] { freshInCallback = channel->authenticated(); };
    };
    QElapsedTimer age;
    age.start();
    check(launcher->start({QStringLiteral("--fixture=") + mode}), "Delayed bootstrap fixture did not launch");
    check(
        until([&] { return launcher->state() == LocalInputCompanion::State::Running; }) && age.elapsed() >= 600 &&
            freshInCallback && channel->authenticated(),
        "Valid cold startup was limited by authenticated heartbeat age"
    );
    launcher->stop();
    stopped(*launcher);
    check(!channel->authenticated(), "Cold-start allowance changed terminal authority cleanup");
  }
}

void reentrantOwnership()
{
  for (int action = 0; action < 5; ++action) {
    resetTrust();
    auto launcher = companion();
    std::unique_ptr<LocalInputChannel> channel;
    std::optional<LocalInputProcess> child;
    int completions = 0;
    launcher->onStopped = [&](QString) {
      ++completions;
      if (action == 4)
        launcher.reset();
    };
    launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) {
      child = launcher->childProcess();
      if (action == 0)
        return; // No owner retains the channel.
      channel = std::move(value);
      if (action == 1)
        launcher->stop();
      else if (action == 2)
        launcher.reset();
      else if (action == 3)
        throw std::runtime_error("Fixture callback failed");
      else
        launcher->stop();
    };
    check(launcher->start({"--fixture=normal"}), "Callback fixture did not launch");
    check(until([&] { return child && localInputProcess(child->pid) != child; }), "Callback left its child alive");
    check(!channel || !channel->authenticated(), "Callback destruction retained channel authority");
    if (action == 4)
      check(until([&] { return !launcher; }), "Completion did not delete its launcher");
    else if (launcher)
      stopped(*launcher);
    check(completions == (action == 2 ? 0 : 1), "Callback deletion duplicated or leaked completion");
  }

  resetTrust();
  auto launcher = companion();
  int channels = 0;
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel>) { ++channels; };
  check(launcher->start({"--fixture=normal"}), "Early-stop fixture did not launch");
  launcher->stop();
  stopped(*launcher);
  check(!channels, "Stop before started still transferred channel authority");

  resetTrust();
  launcher = companion();
  std::unique_ptr<LocalInputChannel> retained;
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) { retained = std::move(value); };
  check(launcher->start({"--fixture=ignore-term"}), "Detached cleanup fixture did not launch");
  check(until([&] { return launcher->state() == LocalInputCompanion::State::Running; }), "Detached fixture failed");
  const auto child = launcher->childProcess();
  retained->onClosed = [&](QString) { retained.reset(); };
  launcher.reset();
  check(!retained, "Destruction did not close the externally owned channel");
  check(until([&] { return localInputProcess(child->pid) != child; }), "Detached reaper failed to kill/reap its child");

  for (const bool destroy : {false, true}) {
    resetTrust();
    launcher = companion();
    launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) { retained = std::move(value); };
    check(launcher->start({"--fixture=ignore-term"}), "Nested-close fixture did not launch");
    check(
        until([&] { return launcher->state() == LocalInputCompanion::State::Running; }), "Nested-close fixture failed"
    );
    const auto nestedChild = launcher->childProcess();
    bool drained = false;
    retained->onClosed = [&](QString) {
      drained = until([&] {
        return localInputProcess(nestedChild->pid) != nestedChild &&
               (destroy || launcher->state() == LocalInputCompanion::State::Stopped);
      });
      // Permit a pending deleteLater to run before the caller of close resumes.
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    if (destroy)
      launcher.reset();
    else
      launcher->stop();
    check(drained, "Reentrant close delayed escalation or accessed an already-deleted reaper");
    retained.reset();
  }
}

int exitLoopFixture(QCoreApplication &application, const QString &marker)
{
  resetTrust();
  auto launcher = companion();
  std::unique_ptr<LocalInputChannel> channel;
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) { channel = std::move(value); };
  check(launcher->start({"--fixture=ignore-term"}), "App-exit fixture did not launch");
  QTimer poll;
  poll.setInterval(2);
  QObject::connect(&poll, &QTimer::timeout, &application, [&] {
    if (launcher->state() != LocalInputCompanion::State::Running)
      return;
    const auto child = launcher->childProcess();
    QFile file(marker);
    if (!child || !file.open(QIODevice::WriteOnly))
      std::exit(2);
    QTextStream(&file) << child->pid << ' ' << child->startStamp;
    file.close();
    application.quit();
  });
  QTimer::singleShot(2000, &application, &QCoreApplication::quit);
  poll.start();
  return application.exec();
}

void eventLoopExit()
{
  QTemporaryDir directory;
  check(directory.isValid(), "Cannot create app-exit fixture directory");
  const auto marker = directory.filePath("child.txt");
  QProcess probe;
  probe.start(QCoreApplication::applicationFilePath(), {"--exit-loop=" + marker});
  check(
      until([&] { return probe.state() == QProcess::NotRunning; }), "App exit blocked on a running QProcess destructor"
  );
  check(probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0, "App-exit fixture failed");
  QFile file(marker);
  check(file.open(QIODevice::ReadOnly), "App-exit fixture did not authenticate its child");
  qint64 pid = 0;
  quint64 stamp = 0;
  QTextStream input(&file);
  input >> pid >> stamp;
  const LocalInputProcess child{pid, stamp};
  check(
      pid > 0 && stamp > 0 && until([&] { return localInputProcess(pid) != child; }),
      "Main-loop shutdown left the owned companion alive"
  );
}

void coreSessionOwnership()
{
  resetTrust();
  QString error;
  auto session = LocalInputCoreSession::start(pair(), {"--fixture=normal"}, &error);
  check(bool(session), qPrintable(error));
  check(
      session->channel() && session->channel()->authenticated() && !session->channel()->authorize,
      "Core session did not retain a default-deny authenticated channel"
  );
  const auto mailbox = session->selectedPeerMailbox();
  check(
      mailbox && !mailbox->read().closed && !mailbox->read().selected,
      "Core session began with a selected-peer identity before core publication"
  );
#ifdef __APPLE__
  const auto capture = session->captureSource();
  check(capture && !capture->epoch().live(), "Session exposed capture continuity before producer start");
  const auto completed = capture->completedInput();
  check(completed && !completed->bound() && completed->live(), "Session preallocates an unbound completion conduit");
  const auto completedEvidence = completed->evidence();
  check(completed->bind(91), "Server may bind the conduit before native producer startup");
  LocalInputCoreSessionTestAccess::drain(*session);
  check(completed->live(), "a pre-start timer tick cannot revoke the underlying unstarted capture epoch");
  // Simulate producer lifecycle only. No tap registration or native input.
  check(capture->start(), "Preallocated capture source could not start");
  const auto captureToken = capture->epoch();
#endif
  LocalInputLease lease;
  lease.deskflowClientIncarnation[0] = 1;
  lease.windowShareClientIncarnation[0] = 2;
  lease.deskflowPeerPin[0] = 3;
  lease.windowSharePeerPin[0] = 4;
  lease.session = lease.epoch = lease.focusRequest = lease.focusRevision = lease.geometryRevision =
      lease.keyboardRouteGeneration = lease.routeGeneration = lease.coreServerIncarnation = lease.coreRecipient =
          lease.coreSelectionGeneration = lease.captureEpoch = 1;
  check(
      validLocalInputLease(lease) && !session->channel()->prepare(lease),
      "Starting the companion implicitly authorized a native input lease"
  );
  const auto firstChild = trust.observed;
  int completions = 0;
  bool mailboxClosedBeforeCallback = false;
#ifdef __APPLE__
  bool captureStoppedBeforeCallback = false;
#endif
  session->onStopped = [&](QString) {
    ++completions;
    mailboxClosedBeforeCallback = mailbox->read().closed;
#ifdef __APPLE__
    captureStoppedBeforeCallback = !captureToken.live() && !completed->live() && completedEvidence->load();
#endif
  };
  check(session->stop() && session->stop(), "Core session could not drain its stopped child");
  check(
      completions == 1 && mailboxClosedBeforeCallback && firstChild &&
          localInputProcess(firstChild->pid) != firstChild && !session->channel()->authenticated(),
      "Core session stop did not revoke and reap its exact child once"
  );
  check(
      session->exitResult() && session->exitResult()->normalExit && session->exitResult()->exitCode == 0,
      "Core session lost graceful authenticated child exit evidence"
  );
  session.reset();
#ifdef __APPLE__
  check(captureStoppedBeforeCallback, "Session notified stop before revoking capture continuity");
  check(!captureToken.live() && !capture->start(), "Retained capture source revived after session destruction");
  check(!completed->pop() && !completed->bind(92), "retained completion conduit cannot outlive its stopped Session");
#endif
  check(mailbox->read().closed && !mailbox->clear(), "Retained mailbox reopened after session stop/destruction");

  resetTrust();
  trust.verify = false;
  session = LocalInputCoreSession::start(pair(), {"--fixture=normal"}, &error);
  check(
      !session && !error.isEmpty() && trust.observed && localInputProcess(trust.observed->pid) != trust.observed,
      "Failed core session returned before draining its rejected child"
  );

  resetTrust();
  session = LocalInputCoreSession::start(pair(), {"--fixture=normal"}, &error);
  check(bool(session), qPrintable(error));
  const auto lastChild = trust.observed;
  bool notified = false;
  session->onStopped = [&](QString) {
    notified = true;
    session.reset();
  };
  const bool drained = session->stop();
  check(
      notified && !session && !drained && lastChild && localInputProcess(lastChild->pid) != lastChild,
      "Deleting a session during its stop callback accessed a dead owner or claimed a retained drain"
  );
}

void coreSessionMailboxClosure()
{
  for (const bool destroy : {false, true}) {
    resetTrust();
    QString error;
    auto session = LocalInputCoreSession::start(pair(), {"--fixture=normal"}, &error);
    check(bool(session), qPrintable(error));
    const auto mailbox = session->selectedPeerMailbox();
    const auto child = trust.observed;
    check(mailbox && !mailbox->read().closed, "Connected session mailbox was unexpectedly closed");
    if (destroy)
      session.reset();
    else
      session->channel()->close("Fixture closed private authority");
    check(
        mailbox->read().closed && !mailbox->read().selected && !mailbox->clear(),
        "Channel close or session destruction failed to close the retained mailbox synchronously"
    );
    if (session)
      check(session->stop(), "Closed-channel session did not reap its companion");
    check(child && until([&] { return localInputProcess(child->pid) != child; }), "Session closure left child alive");
  }
}

void childExitEvidence()
{
  for (const auto &mode : {QString("normal"), QString("auth-fail"), QString("auth-crash")}) {
    resetTrust();
    auto launcher = companion();
    std::unique_ptr<LocalInputChannel> channel;
    std::optional<LocalInputCompanion::ExitResult> notified;
    launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) { channel = std::move(value); };
    launcher->onStopped = [&](QString) { notified = launcher->exitResult(); };
    check(launcher->start({"--fixture=" + mode}), "Exit-status fixture did not launch");
    check(
        until([&] { return launcher->state() == LocalInputCompanion::State::Running; }), "Exit-status fixture failed"
    );
    if (mode == "normal")
      launcher->stop();
    stopped(*launcher);
    check(notified.has_value(), "Completion callback ran before child exit evidence was recorded");
    if (mode == "normal")
      check(
          notified->normalExit && notified->exitCode == 0,
          "Graceful EOF drain signaled the child before it returned success"
      );
    else if (mode == "auth-fail")
      check(notified->normalExit && notified->exitCode == 12, "Nonzero diagnostic exit was lost");
    else
      check(!notified->normalExit, "Crashed diagnostic child appeared to exit normally");
  }
}

void coreSessionCustomDrain()
{
  resetTrust();
  QString error;
  auto deadlines = limits;
  deadlines.gracefulMs = 3000;
  auto session = LocalInputCoreSession::start(pair(), {"--fixture=slow-close"}, &error, deadlines);
  check(bool(session), qPrintable(error));
  const auto child = trust.observed;
  QElapsedTimer elapsed;
  elapsed.start();
  check(session->stop(), "Core session ignored the configured shutdown drain budget");
  const auto exit = session->exitResult();
  check(
      elapsed.elapsed() >= 2000 && elapsed.elapsed() < 3700 && exit && exit->normalExit && exit->exitCode == 0 &&
          child && localInputProcess(child->pid) != child,
      "Custom shutdown budget did not permit delayed authenticated EOF cleanup"
  );
}

#ifdef __APPLE__
// Private constructed events only: no event tap, host, posting or OS input.
struct CaptureFixture
{
  CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStatePrivate);
  CGEventRef event = source ? CGEventCreate(source) : nullptr;
  CaptureFixture()
  {
    check(event, "Cannot construct private capture fixture");
    CGEventSetType(event, kCGEventMouseMoved);
    CGEventSetFlags(event, 0);
    CGEventSetTimestamp(event, 100);
    CGEventSetIntegerValueField(event, kCGMouseEventSubtype, kCGEventMouseSubtypeDefault);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, 1);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, -1);
  }
  ~CaptureFixture()
  {
    if (event)
      CFRelease(event);
    if (source)
      CFRelease(source);
  }
  void key(std::uint16_t code, std::uint64_t flags = 0)
  {
    CGEventSetType(event, kCGEventKeyDown);
    CGEventSetIntegerValueField(event, kCGKeyboardEventKeycode, code);
    CGEventSetIntegerValueField(event, kCGKeyboardEventKeyboardType, 41);
    CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, 0);
    CGEventSetFlags(event, flags);
  }
  void emergency(OriginalInputCaptureSource &capture)
  {
    key(originalKey::escape, originalKey::controlFlag | originalKey::optionFlag | originalKey::shiftFlag);
    capture.capture(kCGEventKeyDown, event);
    check(capture.emergencyRequested(), "Constructed emergency was not retained");
  }
};

void coreSessionWindowDragEdge()
{
  for (bool destroy : {false, true}) {
    auto session = LocalInputCoreSessionTestAccess::captureOnly();
    const auto source = session->captureSource();
    const auto mailbox = session->windowDragEdgeMailbox();
    check(
        source->start() && source->startAnnotations() && source->routes()->bind(91) &&
            WindowDragEdgeMailboxTestAccess::bind(*mailbox),
        "edge session fixture starts without native taps"
    );
    deskflow::AuthenticatedPeerIssuer issuer;
    deskflow::AuthenticatedPeerTicket::Fingerprint pin{};
    pin.fill(0x45);
    const auto ticket = *issuer.issue(pin);
    CaptureFixture fixture;
    CGEventSetType(fixture.event, kCGEventLeftMouseDown);
    CGEventSetIntegerValueField(fixture.event, kCGMouseEventButtonNumber, 0);
    CGEventSetIntegerValueField(fixture.event, kCGMouseEventClickState, 1);
    CGEventSetIntegerValueField(fixture.event, kCGMouseEventNumber, 92);
    CGEventSetLocation(fixture.event, {200, 200});
    const auto down = source->beginCapture(kCGEventLeftMouseDown, fixture.event);
    source->completeCapture({down, 1, false});
    CGEventSetIntegerValueField(fixture.event, kCGMouseEventWindowUnderMousePointer, 812);
    CGEventSetIntegerValueField(fixture.event, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, 812);
    source->captureAnnotation(kCGEventLeftMouseDown, fixture.event);
    LocalInputCoreSessionTestAccess::drain(*session);
    CGEventSetType(fixture.event, kCGEventLeftMouseDragged);
    CGEventSetTimestamp(fixture.event, 200);
    const auto motion = source->beginCapture(kCGEventLeftMouseDragged, fixture.event);
    source->completeCapture({motion, 1, false});
    WindowDragEdge edge{
        91,
        19,
        {down.epoch, down.sequence, 1},
        {motion.epoch, motion.sequence, 1},
        WindowDragEdgeSide::Right,
        {0, 0, 1000, 1000},
        {0, 0, 1000, 1000},
        999,
        200,
        0,
        200,
        ticket
    };
    check(WindowDragEdgeMailboxTestAccess::publish(*mailbox, edge), "Core publishes configured edge");
    std::optional<WindowDragEdgeCandidate> retained;
    int offers = 0, withdrawals = 0;
    session->onWindowDragEdge = [&](const auto &candidate) {
      if (!candidate) {
        ++withdrawals;
        return;
      }
      ++offers;
      retained = candidate;
      check(candidate->live() && candidate->press.window() == 812, "consumer joins the original down and exact window");
      if (destroy)
        session.reset();
    };
    LocalInputCoreSessionTestAccess::drain(*session);
    check(offers == 1 && retained && !source->popCaptured(), "one raw consumer notifies once after draining");
    if (destroy) {
      check(!session && !retained->live() && !mailbox->read(), "callback destruction revokes all retained values");
      continue;
    }
    LocalInputCoreSessionTestAccess::drain(*session);
    check(offers == 1, "poll does not repeat unchanged motion");
    CGEventSetType(fixture.event, kCGEventLeftMouseUp);
    CGEventSetTimestamp(fixture.event, 300);
    CGEventSetIntegerValueField(fixture.event, kCGMouseEventClickState, 0);
    const auto up = source->beginCapture(kCGEventLeftMouseUp, fixture.event);
    source->completeCapture({up, 1, false});
    // Hold back its annotation and the Core's corresponding release. The sole
    // raw consumer must revoke now, without waiting for either delayed queue.
    LocalInputCoreSessionTestAccess::drain(*session);
    check(withdrawals == 1 && !retained->live(), "real raw up immediately withdraws the candidate");
    source->captureAnnotation(kCGEventLeftMouseUp, fixture.event);
    LocalInputCoreSessionTestAccess::drain(*session);
    check(offers == 1 && withdrawals == 1, "late annotation cannot revive the ended drag");
  }
}

void coreSessionRouteJoin()
{
  auto session = LocalInputCoreSessionTestAccess::captureOnly();
  const auto source = session->captureSource();
  check(source->start() && source->routes()->bind(91), "session route fixture startup");
  const auto completed = source->completedInput();
  const auto completedEvidence = completed->evidence();
  check(completed->bind(91), "session route fixture completion conduit binds");
  CaptureFixture fixture;
  deskflow::AuthenticatedPeerIssuer issuer;
  deskflow::AuthenticatedPeerTicket::Fingerprint fingerprint{};
  fingerprint.fill(0x76);
  const auto ticket = *issuer.issue(fingerprint);
  std::vector<OriginalInputRouted> joined;
  session->onRoutedInput = [&](const auto &value) {
    const auto copied = completed->pop();
    check(
        copied && copied->input.record.sequence == value.input.record.sequence && copied->input.live(),
        "completed capture publication precedes the reentrant diagnostic callback"
    );
    joined.push_back(value);
  };
  const auto stamp = source->beginCapture(kCGEventMouseMoved, fixture.event);
  source->completeCapture({stamp, 1, false});
  LocalInputCoreSessionTestAccess::drain(*session);
  check(joined.empty(), "session must wait for actual core route");
  OriginalInputRouteDecision decision{
      {stamp.epoch, stamp.sequence, 1},
      OriginalInputRouteKind::Motion,
      OriginalInputRouteOutcome::Dispatched,
      91,
      19,
      ticket,
      812,
      227,
      true
  };
  check(source->routes()->publish(decision), "core route publication");
  LocalInputCoreSessionTestAccess::drain(*session);
  check(
      joined.size() == 1 && joined.back().live() && joined.back().route.x == 812 &&
          joined.back().input.record.sequence == stamp.sequence &&
          joined.back().route.ticket.incarnation() == ticket.incarnation(),
      "session joins exact route instead of querying current peer/cursor"
  );
  source->capture(kCGEventMouseMoved, fixture.event);
  LocalInputCoreSessionTestAccess::drain(*session);
  check(
      joined.size() == 2 && joined.back().route.outcome == OriginalInputRouteOutcome::NoTranslation,
      "session accounts for zero legacy translations"
  );
  const auto expanded = source->beginCapture(kCGEventMouseMoved, fixture.event);
  source->completeCapture({expanded, 2, false});
  LocalInputCoreSessionTestAccess::drain(*session);
  check(
      joined.size() == 3 && joined.back().route.outcome == OriginalInputRouteOutcome::Expanded,
      "session never treats several translated parts as one input"
  );
  source->capture(kCGEventMouseMoved, fixture.event);
  session->onRoutedInput = [&](const auto &value) {
    check(value.live(), "callback entered with retained evidence");
    const auto copied = completed->pop();
    check(copied && copied->input.live(), "callback retains its previously published capture copy");
    session.reset();
    check(
        !value.live() && !copied->input.live() && completedEvidence->load() && !completed->live(),
        "destroying session revokes callback-owned route, epoch and completed-conduit evidence"
    );
  };
  LocalInputCoreSessionTestAccess::drain(*session);
  check(
      !session && !joined.front().live() && !source->epoch().live(),
      "callback destruction returns without accessing session and revokes retained joins"
  );
}

void coreSessionCompletedCaptures()
{
  auto session = LocalInputCoreSessionTestAccess::captureOnly();
  const auto source = session->captureSource();
  const auto completed = source->completedInput();
  check(source->start() && source->routes()->bind(91), "completed Session capture fixture starts");
  CaptureFixture fixture;
  std::vector<OriginalInputRouted> joined;
  session->onRoutedInput = [&](const auto &value) { joined.push_back(value); };
  source->capture(kCGEventMouseMoved, fixture.event);
  LocalInputCoreSessionTestAccess::drain(*session);
  check(
      joined.size() == 1 && !completed->bound() && completed->live() && !completed->pop(),
      "sole Session consumer drains diagnostics normally before a Server binds the copy conduit"
  );
  check(completed->bind(91), "late Server binding remains available after earlier raw captures");
  const auto first = source->beginCapture(kCGEventMouseMoved, fixture.event);
  LocalInputCoreSessionTestAccess::drain(*session);
  check(!completed->pop(), "a begun but incomplete translation is never published to Server");
  source->completeCapture({first, 0, true});
  LocalInputCoreSessionTestAccess::drain(*session);
  const auto zero = completed->pop();
  check(
      zero && zero->input.live() && zero->input.record.sequence == first.sequence && first.sequence > 1 &&
          zero->translation.stamp == first && zero->translation.parts == 0 && zero->translation.uncertain &&
          !completed->pop() && joined.size() == 2,
      "Session publishes one exact zero-part completion after observing its barrier and diagnostic state"
  );
  const auto pending = source->beginCapture(kCGEventMouseMoved, fixture.event);
  source->completeCapture({pending, 1, false});
  LocalInputCoreSessionTestAccess::drain(*session);
  const auto early = completed->pop();
  check(
      early && early->translation.stamp == pending && early->translation.parts == 1 && joined.size() == 2,
      "completed capture reaches Server without waiting for the post-dispatch diagnostic join"
  );
  OriginalInputRouteDecision decision;
  decision.stamp = {pending.epoch, pending.sequence, 1};
  decision.server = 91;
  decision.kind = OriginalInputRouteKind::Motion;
  decision.outcome = OriginalInputRouteOutcome::NoSend;
  check(source->routes()->publish(decision), "delayed diagnostic decision is published separately");
  LocalInputCoreSessionTestAccess::drain(*session);
  check(joined.size() == 3 && !completed->pop(), "late diagnostic join cannot republish an already completed capture");
  source->routes()->revoke();
  const auto afterDiagnosticFailure = source->beginCapture(kCGEventMouseMoved, fixture.event);
  source->completeCapture({afterDiagnosticFailure, 2, true});
  LocalInputCoreSessionTestAccess::drain(*session);
  const auto independent = completed->pop();
  check(
      independent && independent->input.live() && independent->translation.stamp == afterDiagnosticFailure &&
          independent->translation.parts == 2 && independent->translation.uncertain && completed->live() &&
          LocalInputCoreSessionTestAccess::barrier(*session),
      "failed route diagnostics cannot prevent the sole consumer from publishing completed captures"
  );
}

void coreSessionCompletedCaptureOverflow()
{
  auto session = LocalInputCoreSessionTestAccess::captureOnly();
  const auto source = session->captureSource();
  const auto completed = source->completedInput();
  const auto evidence = completed->evidence();
  check(
      source->start() && source->routes()->bind(91) && completed->bind(91),
      "stalled Server fixture starts both independent conduits"
  );
  CaptureFixture fixture;
  std::size_t observed = 0;
  session->onRoutedInput = [&](const auto &value) {
    check(value.live(), "stalled Server must not revoke the independent diagnostic join");
    ++observed;
  };
  constexpr auto count = OriginalInputCapturedInbox::capacity + 3;
  for (std::size_t index = 0; index < count; ++index) {
    source->capture(kCGEventMouseMoved, fixture.event);
    LocalInputCoreSessionTestAccess::drain(*session);
  }
  check(
      observed == count && !completed->live() && evidence->load() && !completed->pop() && source->epoch().live() &&
          source->routes()->live() && LocalInputCoreSessionTestAccess::barrier(*session),
      "completed-copy overflow preserves ordinary raw draining, the barrier and diagnostic observations"
  );
  fixture.emergency(*source);
  LocalInputCoreSessionTestAccess::drain(*session);
  check(
      LocalInputCoreSessionTestAccess::emergencyStopped(*session) && source->emergencyRequested() &&
          !source->epoch().live() && session->selectedPeerMailbox()->read().closed,
      "a revoked completed-copy conduit cannot hide or stop a later raw emergency"
  );
}

void coreSessionCompletedCaptureLifetime()
{
  for (const bool started : {false, true}) {
    auto source = std::make_unique<OriginalInputCaptureSource>();
    const auto completed = source->completedInput();
    const auto evidence = completed->evidence();
    check(completed->bind(91), "completion lifetime fixture binds its preallocated conduit");
    if (started)
      check(source->start(), "completion lifetime fixture starts only the constructed producer");
    source.reset();
    check(
        evidence->load() && !completed->live() && !completed->pop() && !completed->bind(92),
        "source destruction before or after producer startup revokes every retained completion conduit"
    );
  }
}

void coreSessionEmergency()
{
  enum class Before
  {
    Live,
    Overflow,
    BarrierFailure
  };
  for (const auto before : {Before::Live, Before::Overflow, Before::BarrierFailure}) {
    resetTrust();
    auto deadlines = limits;
    deadlines.gracefulMs = 1200;
    QString error;
    auto session = LocalInputCoreSession::start(pair(), {"--fixture=ignore-term"}, &error, deadlines);
    check(bool(session), qPrintable(error));
    const auto capture = session->captureSource();
    const auto mailbox = session->selectedPeerMailbox();
    const auto companion = LocalInputCoreSessionTestAccess::companion(*session);
    const auto child = trust.observed;
    check(child && capture->start(), "Emergency fixture could not start its producer");
    const auto epoch = capture->epoch();
    CaptureFixture fixture;
    capture->capture(kCGEventMouseMoved, fixture.event);
    LocalInputCoreSessionTestAccess::drain(*session);
    check(LocalInputCoreSessionTestAccess::barrier(*session), "Initial drain did not retain a barrier");
    if (before == Before::Overflow) {
      for (std::size_t i = 0; i <= OriginalInputStream::capacity; ++i)
        capture->capture(kCGEventMouseMoved, fixture.event);
      check(!epoch.live() && epoch.reason() == OriginalInputRevocation::Overflow, "Fixture did not overflow");
    } else if (before == Before::BarrierFailure) {
      fixture.key(0);
      capture->capture(kCGEventKeyDown, fixture.event);
      capture->capture(kCGEventKeyDown, fixture.event);
      check(epoch.live(), "Duplicate downs must reach the consumer barrier before continuity revocation");
      LocalInputCoreSessionTestAccess::drain(*session);
      check(
          !epoch.live() && epoch.reason() == OriginalInputRevocation::Discontinuity,
          "Contradictory downs did not invalidate the consumer barrier"
      );
    }
    if (before != Before::Live) {
      LocalInputCoreSessionTestAccess::drain(*session);
      check(
          !capture->epoch().live() && session->channel()->authenticated() && !mailbox->read().closed,
          "Continuity loss alone stopped the companion or invented a new epoch"
      );
    }
    int closures = 0, completions = 0;
    const auto previousClosed = session->channel()->onClosed;
    session->channel()->onClosed = [&, previousClosed](QString reason) {
      ++closures;
      if (previousClosed)
        previousClosed(std::move(reason));
    };
    session->onStopped = [&](QString) { ++completions; };
    fixture.emergency(*capture);
    QElapsedTimer elapsed;
    elapsed.start();
    LocalInputCoreSessionTestAccess::drain(*session);
    check(
        elapsed.elapsed() < 600 && companion && companion->state() == LocalInputCompanion::State::Stopping &&
            !companion->exitResult() && localInputProcess(child->pid) == child,
        "Capture drain synchronously waited for companion shutdown"
    );
    check(
        LocalInputCoreSessionTestAccess::emergencyStopped(*session) && mailbox->read().closed &&
            !session->channel()->authenticated() && !epoch.live() && !capture->start() && closures == 1,
        "Emergency did not synchronously revoke all session authority before asynchronous child cleanup"
    );
    for (int i = 0; i < 3; ++i)
      LocalInputCoreSessionTestAccess::drain(*session);
    bool coreContinued = false;
    QTimer::singleShot(0, QCoreApplication::instance(), [&] {
      coreContinued = companion && companion->state() == LocalInputCompanion::State::Stopping;
    });
    check(until([&] { return coreContinued; }, 300), "Core event loop did not continue during companion cleanup");
    check(
        until([&] { return completions == 1; }) && closures == 1 &&
            companion->state() == LocalInputCompanion::State::Stopped && localInputProcess(child->pid) != child,
        "Emergency did not complete its terminal companion stop exactly once"
    );
    LocalInputCoreSessionTestAccess::drain(*session);
    check(completions == 1 && closures == 1 && !mailbox->clear(), "Emergency shutdown was not terminal");
  }
}

void coreSessionEmergencyCallbacks()
{
  enum class Action
  {
    Reenter,
    DestroyOnClose,
    DestroyOnCompletion
  };
  for (const auto action : {Action::Reenter, Action::DestroyOnClose, Action::DestroyOnCompletion}) {
    resetTrust();
    QString error;
    auto session = LocalInputCoreSession::start(pair(), {"--fixture=ignore-term"}, &error, limits);
    check(bool(session), qPrintable(error));
    const auto capture = session->captureSource();
    const auto mailbox = session->selectedPeerMailbox();
    const auto child = trust.observed;
    check(child && capture->start(), "Emergency callback fixture did not start");
    int closures = 0, completions = 0, afterDeletion = 0;
    bool closedBeforeNotification = false, reentrantStopDrained = false;
    session->onStopped = [&, keepCapture = capture](QString) {
      ++completions;
      if (action == Action::DestroyOnCompletion) {
        session.reset();
        // Access the callback's own captures after its owner has gone.
        afterDeletion += keepCapture->emergencyRequested() && mailbox->read().closed;
      } else {
        LocalInputCoreSessionTestAccess::drain(*session);
      }
    };
    const auto previousClosed = session->channel()->onClosed;
    session->channel()->onClosed = [&, previousClosed, keepCapture = capture](QString reason) {
      ++closures;
      closedBeforeNotification = LocalInputCoreSessionTestAccess::emergencyStopped(*session) &&
                                 mailbox->read().closed && !keepCapture->epoch().live();
      if (previousClosed)
        previousClosed(std::move(reason));
      if (action == Action::DestroyOnClose) {
        session.reset();
        afterDeletion += keepCapture->emergencyRequested() && mailbox->read().closed;
      } else if (action == Action::Reenter) {
        LocalInputCoreSessionTestAccess::drain(*session);
        LocalInputCoreSessionTestAccess::companion(*session)->stop();
        reentrantStopDrained = until([&] { return completions == 1; });
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      }
    };
    CaptureFixture fixture;
    fixture.emergency(*capture);
    // Exercise the actual queued capture timer, including deletion of the
    // session that owns its receiver and timer while the drain is active.
    check(
        until([&] { return closures == 1 && (action == Action::DestroyOnClose || completions == 1); }),
        "Queued emergency callback did not close/reap its companion"
    );
    check(
        closedBeforeNotification && closures == 1 && completions == (action == Action::DestroyOnClose ? 0 : 1) &&
            (action == Action::Reenter ? reentrantStopDrained : !session && afterDeletion == 1),
        "Emergency callback reentrancy or destruction violated terminal session ownership"
    );
    check(
        until([&] { return localInputProcess(child->pid) != child; }) && mailbox->read().closed && !capture->start(),
        "Destructive emergency callback lost child cleanup or revived capture authority"
    );
  }
}
#endif

void environmentIsolation()
{
  resetTrust();
  auto environment = QProcessEnvironment::systemEnvironment();
  const QStringList names{
      "DYLD_INSERT_LIBRARIES",       "DYLD_LIBRARY_PATH",  "DYLD_FRAMEWORK_PATH", "DYLD_FIXTURE",     "QT_PLUGIN_PATH",
      "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QML_PLUGIN_PATH", "QML_IMPORT_PATH",     "QML2_IMPORT_PATH", "QT_QPA_PLATFORM",
      "WINDOWSHARE_FIXTURE_NORMAL"
  };
  for (const auto &name : names)
    qputenv(
        name.toLocal8Bit(), name == "QT_QPA_PLATFORM"              ? "offscreen"
                            : name == "WINDOWSHARE_FIXTURE_NORMAL" ? "kept"
                                                                   : "/fixture/untrusted"
    );
  auto launcher = companion();
  std::unique_ptr<LocalInputChannel> channel;
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) { channel = std::move(value); };
  const bool launched = launcher->start({"--fixture=environment"});
  for (const auto &name : names)
    if (environment.contains(name))
      qputenv(name.toLocal8Bit(), environment.value(name).toLocal8Bit());
    else
      qunsetenv(name.toLocal8Bit());
  check(
      launched && until([&] { return launcher->state() == LocalInputCompanion::State::Running; }),
      "Child observed an injected loader/plugin override or lost ordinary settings"
  );
  launcher->stop();
  stopped(*launcher);
}
} // namespace

int main(int argc, char **argv)
{
  int descriptor = -1;
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], "--expect-closed=", 16) == 0) {
      const int fd = std::atoi(argv[i] + 16);
      return fd >= 3 && fcntl(fd, F_GETFD) == -1 && errno == EBADF ? 0 : 1;
    }
    if (std::strncmp(argv[i], "--core-input-fd=", 16) == 0) {
      if (descriptor != -1)
        return 1;
      descriptor = std::atoi(argv[i] + 16);
      if (descriptor < 3 || fcntl(descriptor, F_SETFD, FD_CLOEXEC) != 0)
        return 1;
    }
  }
  // Every pair used by this suite exists before Qt starts any helper thread.
  if (descriptor < 0)
    for (int i = 0; i < 48; ++i)
      pairs.push_back(LocalInputSocketPair::create());
  QCoreApplication application(argc, argv);
  try {
    if (descriptor >= 0) {
      QString mode;
      for (const auto &argument : application.arguments())
        if (argument.startsWith("--fixture="))
          mode = argument.mid(10);
      return hostFixture(application, descriptor, mode);
    }
    for (const auto &argument : application.arguments()) {
      if (argument.startsWith("--exit-loop="))
        return exitLoopFixture(application, argument.mid(12));
      if (argument == "--diagnostic-forwarding=0" || argument == "--diagnostic-forwarding=1")
        return diagnosticForwardingFixture(argument.endsWith('1'));
    }
    normalLaunchAndStop();
    diagnosticForwarding();
    refusedLaunches();
    startupAndDeath();
    coldStartupAllowance();
    reentrantOwnership();
    environmentIsolation();
    coreSessionOwnership();
    coreSessionMailboxClosure();
    coreSessionCustomDrain();
#ifdef __APPLE__
    coreSessionRouteJoin();
    coreSessionWindowDragEdge();
    coreSessionCompletedCaptures();
    coreSessionCompletedCaptureOverflow();
    coreSessionCompletedCaptureLifetime();
    coreSessionEmergency();
    coreSessionEmergencyCallbacks();
    QTextStream(stdout) << "PASS: asynchronous capture emergency after overflow/barrier revocation, core continuation, "
                           "terminal and destructive emergency callbacks\n";
#endif
    childExitEvidence();
    eventLoopExit();
    QTextStream(
        stdout
    ) << "PASS: fixed verified companion, exclusive inheritance, cold-start and handshake deadlines, child death, "
         "bounded stop, callback deletion/stop/drop/throw, no restart, loader environment isolation, "
         "explicit stderr forwarding with stdout discarded, "
         "startup expiry at use, detached reaping, event-loop exit, core session default-deny, "
         "failure drain, deletion during stop, selected-peer mailbox closure, configured shutdown "
         "budget, graceful EOF drain "
         "and child exit evidence\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
