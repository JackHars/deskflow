/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputCompanion.h"
#include "LocalInputPeerTrust.h"
#include "LocalInputSession.h"

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
} // namespace deskflow::windowshare

namespace {
constexpr LocalInputCompanionLimits limits{1000, 40, 80, {20, 400}};
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
  auto channel = LocalInputChannel::adoptHost(descriptor, *parent, limits.channel);
  check((fcntl(descriptor, F_GETFD) & FD_CLOEXEC) != 0, "Adopted descriptor is inheritable");
  bool authenticated = false;
  channel->onClosed = [&](QString) {
    if (mode == "slow-close")
      QTimer::singleShot(2200, &application, &QCoreApplication::quit);
    else if (mode != "ignore-term")
      application.quit();
  };
  channel->onAuthenticated = [&] {
    authenticated = true;
    if (mode == "auth-exit")
      QTimer::singleShot(100, &application, &QCoreApplication::quit);
    if (mode == "auth-fail")
      QTimer::singleShot(100, &application, [&] { application.exit(12); });
    if (mode == "auth-crash")
      QTimer::singleShot(100, &application, [] { std::raise(SIGKILL); });
  };
  const int result = application.exec();
  return result ? result : authenticated ? 0 : 13;
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
  auto launcher = companion(deadlines);
  std::unique_ptr<LocalInputChannel> channel;
  QString reason;
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel> value) { channel = std::move(value); };
  launcher->onStopped = [&](QString value) { reason = value; };
  check(launcher->start({"--fixture=idle"}), "Handshake timeout fixture did not launch");
  stopped(*launcher);
  check(reason.contains("handshake") && channel && !channel->authenticated(), "Startup did not bound the handshake");

  resetTrust();
  trust.delayMs = 200;
  launcher = companion(deadlines);
  bool delivered = false;
  launcher->onChannel = [&](std::unique_ptr<LocalInputChannel>) { delivered = true; };
  check(launcher->start({"--fixture=normal"}), "Slow verification fixture did not launch");
  stopped(*launcher);
  check(!delivered, "Verification delivered a capability after the startup deadline expired");

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
  LocalInputLease lease;
  lease.deskflowClientIncarnation[0] = 1;
  lease.windowShareClientIncarnation[0] = 2;
  lease.deskflowPeerPin[0] = 3;
  lease.windowSharePeerPin[0] = 4;
  lease.session = lease.epoch = lease.focusRevision = lease.geometryRevision = lease.routeGeneration = 1;
  check(
      validLocalInputLease(lease) && !session->channel()->prepare(lease),
      "Starting the companion implicitly authorized a native input lease"
  );
  const auto firstChild = trust.observed;
  int completions = 0;
  session->onStopped = [&](QString) { ++completions; };
  check(session->stop() && session->stop(), "Core session could not drain its stopped child");
  check(
      completions == 1 && firstChild && localInputProcess(firstChild->pid) != firstChild &&
          !session->channel()->authenticated(),
      "Core session stop did not revoke and reap its exact child once"
  );
  check(
      session->exitResult() && session->exitResult()->normalExit && session->exitResult()->exitCode == 0,
      "Core session lost graceful authenticated child exit evidence"
  );
  session.reset();

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
    for (int i = 0; i < 32; ++i)
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
    for (const auto &argument : application.arguments())
      if (argument.startsWith("--exit-loop="))
        return exitLoopFixture(application, argument.mid(12));
    normalLaunchAndStop();
    refusedLaunches();
    startupAndDeath();
    reentrantOwnership();
    environmentIsolation();
    coreSessionOwnership();
    coreSessionCustomDrain();
    childExitEvidence();
    eventLoopExit();
    QTextStream(stdout) << "PASS: fixed verified companion, exclusive inheritance, handshake deadline, child death, "
                           "bounded stop, callback deletion/stop/drop/throw, no restart, loader environment isolation, "
                           "startup expiry at use, detached reaping, event-loop exit, core session default-deny, "
                           "failure drain, deletion during stop, configured shutdown budget, graceful EOF drain "
                           "and child exit evidence\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
