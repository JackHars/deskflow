/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalWindowDragCoordinator.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QScopeGuard>
#include <QTextStream>
#include <QThread>
#include <stdexcept>
#include <unistd.h>

namespace deskflow::windowshare {
struct WindowDragEdgeMailboxTestAccess
{
  static void bind(WindowDragEdgeMailbox &mailbox)
  {
    mailbox.bind(7);
  }
  static bool publish(WindowDragEdgeMailbox &mailbox, WindowDragEdge edge)
  {
    return mailbox.publish(std::move(edge));
  }
  static void clear(WindowDragEdgeMailbox &mailbox)
  {
    mailbox.clear();
  }
};
struct AuthenticatedPeerObservationTestAccess
{
  static AuthenticatedPeerObservation create(AuthenticatedPeerTicket ticket)
  {
    return {std::move(ticket), std::chrono::steady_clock::now()};
  }
};
} // namespace deskflow::windowshare
using namespace deskflow;
using namespace deskflow::windowshare;
namespace {
void check(bool result, const char *message)
{
  if (!result)
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
LocalInputPin pin(int value)
{
  LocalInputPin result{};
  result.fill(static_cast<std::uint8_t>(value));
  return result;
}
LocalInputPeerPolicy policy(int core = 1, int viewer = 2)
{
  const std::array pairs{LocalInputPeerPair{pin(core), pin(viewer)}};
  return *LocalInputPeerPolicy::create(pairs);
}
LocalWindowPointer down()
{
  return {LocalWindowPointer::Kind::Down, 1, 1, 100, 0, 812, 1, 52, -1100, 110};
}
int child(QCoreApplication &app, int descriptor, const QString &mode)
{
  check(protectLocalInputDescriptor(descriptor), "Child capability is private");
  const auto parent = localInputProcess(getppid());
  check(parent.has_value(), "Parent process birth exists");
  auto channel = LocalInputChannel::adoptHost(descriptor, *parent);
  AuthenticatedPeerIssuer issuer, replacement;
  auto ticket = *issuer.issue(pin(2));
  int prepares = 0, motions = 0, cancels = 0;
  std::optional<WindowDragBinding> binding;
  std::unique_ptr<LocalWindowDragHost> coordinator;
  LocalWindowDragHost::Providers providers;
  providers.admission = [&] { return mode != "host-admission"; };
  providers.connection = [&]() -> std::optional<AuthenticatedPeerObservation> {
    if (mode == "no-connection")
      return {};
    return AuthenticatedPeerObservationTestAccess::create(ticket);
  };
  providers.prepare = [&](const WindowDragBinding &value, const LocalWindowDrag &edge) {
    check(
        value.captureEpoch == edge.captureEpoch && value.pressSequence == 1 && value.window == 812 &&
            value.eventNumber == 52 && value.topology == edge.boundary && value.connection == ticket.incarnation(),
        "Host binding joins its own TLS connection and original press"
    );
    check(
        edge.source.x == -1200 && edge.destination.y == -50 && edge.sourceX == -1 && edge.destinationX == 100,
        "Signed configured boundary survives the private hop"
    );
    ++prepares;
    binding = value;
    QTextStream(stdout) << "prepared\n" << Qt::flush;
    if (mode == "host-tls-loss")
      issuer.revoke();
    if (mode == "host-reconnect")
      ticket = *replacement.issue(pin(2));
    if (mode == "host-destroy")
      coordinator.reset();
    if (mode == "host-throw")
      throw std::runtime_error("Native callback failure");
    if (mode == "host-slow")
      QThread::msleep(400);
    return mode != "native-denied";
  };
  providers.motion = [&](const WindowDragBinding &value, const LocalWindowDrag &edge) {
    check(
        binding == value && edge.sequence > edge.pressSequence && edge.destinationY == 288,
        "Motion stays with exact prepared owner"
    );
    ++motions;
    QTextStream(stdout) << "motion\n" << Qt::flush;
  };
  providers.cancel = [&](const WindowDragBinding &value) {
    check(binding == value, "Cleanup targets exact prepared owner");
    ++cancels;
    QTextStream(stdout) << "cancelled\n" << Qt::flush;
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
  };
  coordinator = std::make_unique<LocalWindowDragHost>(
      *channel, mode == "wrong-policy" ? policy(1, 3) : policy(), std::move(providers)
  );
  channel->onWindowPointer = [&](const LocalWindowPointer &pointer) {
    if (coordinator && mode != "missing-press")
      coordinator->pointer(pointer);
  };
  channel->onClosed = [&](QString) { app.quit(); };
  QTimer::singleShot(1500, &app, &QCoreApplication::quit);
  const auto result = app.exec();
  if (coordinator)
    coordinator->stop();
  check(!channel->lease() && channel->phase() == LocalInputPhase::Idle, "Preparation never acquires input");
  const bool denied = mode == "wrong-policy" || mode == "missing-press" || mode == "host-admission" ||
                      mode == "no-connection" || mode == "core-admission" || mode == "core-policy";
  check(
      denied ? prepares == 0 && cancels == 0 : prepares == 1 && cancels >= 1,
      "Native prepare/cleanup count disagrees with admission"
  );
  if (mode == "normal")
    check(motions == 1, "Prepared preview receives one new motion");
  QTextStream(stdout) << "PASS\n" << Qt::flush;
  return result;
}
struct Source
{
  OriginalInputStream stream;
  OriginalPointerJoin pointers{stream.epoch()};
  WindowDragEdgeJoin join;
  WindowDragEdgeMailbox mailbox;
  AuthenticatedPeerIssuer issuer;
  WindowDragEdge edge;
  Source()
  {
    WindowDragEdgeMailboxTestAccess::bind(mailbox);
    const auto press = down();
    const OriginalInputRecord record{stream.epoch().id(), 1, press.sourceTime, 0, OriginalInputButton{0, true, 1}};
    check(stream.push(record), "Physical source down");
    const OriginalInputCaptured captured{
        *stream.pop(),
        {{record.epoch, 1, 0}, 1, false},
        {},
        OriginalPointerObservation{record.epoch, 1, 100, press.x, press.y, 0, 0, 52, 0, 1, 3}
    };
    check(pointers.capture(captured), "Capture copies use existing consumer");
    check(pointers.annotate({100, 0, {0, true, 1}, press.x, press.y, 812, 812, 52, 0, 1, 3}), "Exact press annotation");
    join.capture(captured);
    join.pointer(*pointers.pop());
    edge = {
        7,
        11,
        {record.epoch, 1, 1},
        {record.epoch, 2, 1},
        WindowDragEdgeSide::Right,
        {-1200, 0, 1200, 800},
        {100, -50, 1600, 900},
        -1,
        300,
        100,
        287,
        *issuer.issue(pin(1))
    };
  }
  WindowDragEdgeCandidate candidate()
  {
    check(WindowDragEdgeMailboxTestAccess::publish(mailbox, edge), "Publish authoritative configured boundary");
    auto result = join.join(mailbox.read());
    check(result.has_value(), "Original press joins Core boundary");
    return *result;
  }
};
void scenario(const QString &mode)
{
  QProcess process;
  auto pair = LocalInputSocketPair::create();
  pair.configureChild(process);
  process.start(QCoreApplication::applicationFilePath(), {"--child", QString::number(pair.childDescriptor()), mode});
  check(process.waitForStarted(3000), "Launch owned coordinator child");
  auto channel = pair.adoptCore(*localInputProcess(process.processId()));
  const auto cleanup = qScopeGuard([&] {
    channel->close();
    if (process.state() != QProcess::NotRunning) {
      process.kill();
      process.waitForFinished(2000);
    }
  });
  check(until([&] { return channel->authenticated(); }), "Real private handshake completes");
  bool admitted = mode != "core-admission";
  LocalWindowDragCore coordinator(*channel, mode == "core-policy" ? policy(3, 2) : policy(), [&] { return admitted; });
  Source source;
  auto pointer = down();
  pointer.captureEpoch = source.stream.epoch().id();
  check(channel->sendWindowPointer(pointer), "Joined press precedes edge on same FIFO");
  coordinator.observe(source.candidate());
  QByteArray output;
  const auto read = [&] {
    output += process.readAllStandardOutput();
    return output;
  };
  const bool denied = mode == "wrong-policy" || mode == "missing-press" || mode == "host-admission" ||
                      mode == "no-connection" || mode == "core-admission" || mode == "core-policy";
  if (!denied) {
    check(until([&] { return read().contains("prepared\n"); }), "Host prepared authenticated exact press");
    if (mode == "normal") {
      ++source.edge.motion.sequence;
      ++source.edge.destinationY;
      coordinator.observe(source.candidate());
      check(until([&] { return read().contains("motion\n"); }), "Motion reaches prepared native owner");
      // Stopping physical movement does not renew a sample, start another
      // preparation, or cancel an otherwise current held drag.
      QElapsedTimer elapsed;
      elapsed.start();
      check(until([&] { return elapsed.elapsed() >= 450; }), "Hold observation interval");
      check(!read().contains("cancelled"), "Stationary held preview survives short sample freshness");
      coordinator.observe({});
      pointer.kind = LocalWindowPointer::Kind::Up;
      pointer.sequence = 4;
      pointer.sourceTime = 400;
      pointer.clicks = 0;
      check(channel->sendWindowPointer(pointer), "Only the real source release cancels host preparation");
      source.join.revoke();
      coordinator.observe({});
    } else if (mode == "core-tls-loss")
      source.issuer.revoke();
    else if (mode == "topology-loss")
      WindowDragEdgeMailboxTestAccess::clear(source.mailbox);
    else if (mode == "capture-loss")
      source.stream.revoke(OriginalInputRevocation::Stopped);
    else if (mode == "held-loss")
      source.join.revoke();
    else if (mode == "admission-loss")
      admitted = false;
    else if (mode == "private-loss")
      channel->close();
    else if (mode == "core-stop")
      coordinator.stop();
    else if (mode == "host-silence") {
      // Block just the parent event loop. The child must cancel independently
      // even though its ordinary private heartbeat has not timed out yet.
      QThread::msleep(450);
    }
  }
  check(until([&] { return process.state() == QProcess::NotRunning; }), "Owned coordinator child exits boundedly");
  read();
  const auto errors = process.readAllStandardError();
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !output.contains("PASS"))
    throw std::runtime_error((mode + ": " + QString::fromUtf8(output + errors)).toStdString());
  coordinator.stop();
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  try {
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == "--child")
      return child(app, QString::fromLocal8Bit(argv[2]).toInt(), QString::fromLocal8Bit(argv[3]));
    for (const auto *mode :
         {"normal",       "wrong-policy",  "missing-press", "host-admission", "no-connection", "core-admission",
          "core-policy",  "native-denied", "host-tls-loss", "host-reconnect", "host-destroy",  "host-throw",
          "host-slow",    "core-tls-loss", "topology-loss", "capture-loss",   "held-loss",     "admission-loss",
          "private-loss", "core-stop",     "host-silence"}) {
      QTextStream(stdout) << "RUN: " << mode << '\n' << Qt::flush;
      scenario(QString::fromLatin1(mode));
    }
    QTextStream(stdout) << "PASS: private drag preparation, exact press/dual pin/connection, held refresh, loss, "
                           "reentrancy and cleanup\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
