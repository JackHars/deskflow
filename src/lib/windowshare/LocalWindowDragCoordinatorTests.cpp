/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalWindowDragCoordinator.h"
#include "WindowDragMotionGate.h"
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
  static bool release(WindowDragEdgeMailbox &mailbox, OriginalInputStamp press, OriginalInputStamp up)
  {
    return mailbox.release(press, up);
  }
  static std::optional<WindowDragEdgeMailbox::Transfer> take(WindowDragEdgeMailbox &mailbox)
  {
    return mailbox.takeTransfer();
  }
  static void complete(WindowDragEdgeMailbox &mailbox, const WindowDragEdgeMailbox::Transfer &request, bool ok)
  {
    mailbox.clear();
    mailbox.completeTransfer(request, ok);
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
    if (edge.kind == LocalWindowDrag::Kind::Hold) {
      check(binding == value && edge.sequence > edge.pressSequence, "Stationary hold retains exact prepared owner");
      return;
    }
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
  std::optional<OriginalInputCaptured> capturedDown;
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
    capturedDown = captured;
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
int dropChild(QCoreApplication &app, int descriptor, const QString &mode)
{
  check(protectLocalInputDescriptor(descriptor), "Drop fixture descriptor is private");
  auto channel = LocalInputChannel::adoptHost(descriptor, *localInputProcess(getppid()));
  AuthenticatedPeerIssuer issuer;
  const auto ticket = *issuer.issue(pin(2));
  std::unique_ptr<LocalWindowDragHost> coordinator;
  const bool returning = mode.startsWith("drop-return");
  int prepared = 0, dropped = 0, finished = 0, cancelled = 0;
  LocalWindowDragHost::Providers providers;
  providers.admission = [] { return true; };
  providers.connection = [&] { return AuthenticatedPeerObservationTestAccess::create(ticket); };
  providers.prepare = [&](const WindowDragBinding &binding, const LocalWindowDrag &command) {
    ++prepared;
    check(
        (binding.direction == WindowDragDirection::Return) == returning && bool(command.returnOffer) == returning,
        "Drag direction must match independent native move offer"
    );
    // This fixture models the separately tested native placement/paint result.
    QTimer::singleShot(0, &app, [&, binding, sequence = command.sequence] { coordinator->ready(binding, sequence); });
    return true;
  };
  providers.motion = [&](const WindowDragBinding &binding, const LocalWindowDrag &command) {
    check(command.destinationX == 130 && command.destinationY == 302, "Held raw deltas move only preview");
    coordinator->ready(binding, command.sequence);
  };
  providers.cancel = [&](const WindowDragBinding &) {
    ++cancelled;
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
  };
  providers.drop = [&](const WindowDragBinding &binding, const LocalWindowDrag &command, const WindowDragRelease &up) {
    ++dropped;
    check(
        command.kind == LocalWindowDrag::Kind::Drop && command.sequence == 3 && command.releaseSequence == 4 &&
            command.releaseTime == 400 && up.sequence == 4 && up.window == 812 && up.eventNumber == 52,
        "Drop joins final preview with the one original up"
    );
    if (mode == "drop-native-denied")
      return false;
    if (mode == "drop-no-commit")
      return true;
    coordinator->committed(binding);
    return true;
  };
  providers.finish = [&](const WindowDragBinding &) {
    ++finished;
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
  };
  coordinator = std::make_unique<LocalWindowDragHost>(*channel, policy(), std::move(providers));
  LocalWindowMove native{LocalWindowMove::Phase::Begin, 812, 0, 9, 10, 11, 12, 1, {}, -100, 20, {-500, 0, 640, 480}};
  channel->onWindowPointer = [&](const auto &pointer) {
    const bool up = pointer.kind == LocalWindowPointer::Kind::Up;
    const auto end = [&] {
      native.phase = mode == "drop-return-native-cancel" ? LocalWindowMove::Phase::Cancel : LocalWindowMove::Phase::End;
      native.sequence = 2;
      coordinator->moved(native);
    };
    if (returning && up && mode == "drop-return-native-first")
      end();
    coordinator->pointer(pointer);
    if (returning && pointer.kind == LocalWindowPointer::Kind::Down)
      coordinator->moved(native);
    if (returning && up && mode != "drop-return-native-first" && mode != "drop-return-no-native-up")
      end();
  };
  channel->onClosed = [&](QString) { app.quit(); };
  QTimer::singleShot(3500, &app, &QCoreApplication::quit);
  app.exec();
  coordinator->stop();
  const bool success = mode == "drop-normal" || mode == "drop-server-first" || mode == "drop-return" ||
                       mode == "drop-return-native-first";
  check(
      prepared == 1 && (success ? finished == 1 && dropped == 1 && cancelled == 0 : finished == 0 && cancelled == 1),
      "Native completion or exact rollback count"
  );
  check(!channel->lease(), "Drag never acquired an original-input lease");
  QTextStream(stdout) << "PASS\n" << Qt::flush;
  return 0;
}
void dropScenario(const QString &mode)
{
  QProcess process;
  auto pair = LocalInputSocketPair::create();
  pair.configureChild(process);
  process.start(QCoreApplication::applicationFilePath(), {"--child", QString::number(pair.childDescriptor()), mode});
  check(process.waitForStarted(3000), "Launch drop coordinator child");
  auto channel = pair.adoptCore(*localInputProcess(process.processId()));
  QString channelError;
  channel->onClosed = [&](const QString &reason) { channelError = reason; };
  const auto cleanup = qScopeGuard([&] {
    channel->close();
    if (process.state() != QProcess::NotRunning) {
      process.kill();
      process.waitForFinished(2000);
    }
  });
  check(until([&] { return channel->authenticated(); }), "Drop handshake authenticates both processes");
  Source source;
  const bool returning = mode.startsWith("drop-return");
  source.edge.returning = returning;
  auto box = std::shared_ptr<WindowDragEdgeMailbox>(&source.mailbox, [](auto *) {});
  auto position = std::make_shared<std::atomic<std::uint64_t>>(2);
  check(box->bindCapture(source.stream.epoch(), position), "One producer fence binds before transfer");
  WindowDragMotionGate gate(source.stream.epoch().id());
  gate.observe({source.stream.epoch().id(), 1, 100, 0, OriginalInputButton{0, true, 1}});
  check(
      source.stream.push({source.stream.epoch().id(), 2, 200, 0, OriginalInputMotion{1, 0, 0}}), "Boundary raw motion"
  );
  gate.observe(source.stream.pop()->record);
  bool armed = false;
  LocalWindowDragCore core(
      *channel, policy(),
      LocalWindowDragCore::Providers{
          [] { return true; },
          [&](OriginalInputStamp press) {
            armed = gate.arm(press);
            return armed;
          },
          [&](OriginalInputStamp press) { return gate.refresh(press); }, [&] { gate.cancel(); }, box
      }
  );
  auto pointer = down();
  pointer.captureEpoch = source.stream.epoch().id();
  if (returning) {
    pointer.window = 0; // No Ubuntu window identity is fabricated in the Mac annotation.
    core.capture(*source.capturedDown);
    OriginalInputRouteDecision downRoute;
    downRoute.stamp = source.edge.press;
    downRoute.kind = OriginalInputRouteKind::Button;
    downRoute.outcome = OriginalInputRouteOutcome::Dispatched;
    downRoute.server = 7;
    downRoute.recipient = 11;
    downRoute.ticket = source.edge.peer;
    downRoute.pointKnown = true;
    downRoute.x = -100;
    downRoute.y = 20;
    core.routed({source.capturedDown->input, downRoute, std::make_shared<std::atomic_bool>(false)});
  }
  check(channel->sendWindowPointer(pointer), "Original down is observed before edge");
  core.observe(source.candidate());
  if (returning) {
    QElapsedTimer waiting;
    waiting.start();
    check(until([&] { return waiting.elapsed() >= 60; }), "Wait for the ordered pointer observation");
    check(!armed, "Return began before Session published its original press metadata");
    OriginalPointerJoin remotePointers(source.stream.epoch());
    check(remotePointers.capture(*source.capturedDown), "Remote press enters the existing join");
    const auto observed = remotePointers.pop(OriginalPointerJoin::Clock::now() + OriginalPointerJoin::budget);
    check(observed && !observed->annotation && observed->window() == 0, "Remote press fabricated a Mac annotation");
    core.pointer(*observed);
  }
  check(until([&] { return armed; }), "Native preview readiness arms only the matching motion gate");
  OriginalInputRecord move{source.stream.epoch().id(), 3, 300, 0, OriginalInputMotion{30, 15, 0}};
  check(source.stream.push(move), "Held raw movement enters sole consumer");
  const bool suppressed = gate.observe(move);
  check(suppressed, "Admitted held motion is withheld");
  position->store(3);
  OriginalInputCaptured captured{*source.stream.pop(), {{move.epoch, 3, 0}, 0, false}, {}, {}, suppressed};
  core.capture(captured);
  OriginalInputRecord up{move.epoch, 4, 400, 0, OriginalInputButton{0, false, 0}};
  check(source.stream.push(up) && !gate.observe(up), "Real zero-click up always dispatches normally");
  position->store(4);
  OriginalInputCaptured released{
      *source.stream.pop(),
      {{up.epoch, 4, 0}, 1, false},
      {},
      OriginalPointerObservation{up.epoch, 4, 400, -1, 300, 0, 0, 52, 0, 1, 3}
  };
  if (mode == "drop-server-first")
    check(
        WindowDragEdgeMailboxTestAccess::release(*box, source.edge.press, {up.epoch, 4, 1}), "Server wins release race"
    );
  source.join.capture(released);
  core.capture(released);
  core.observe({}); // Up withdraws held evidence without cancelling a pending joined drop.
  if (mode != "drop-server-first")
    check(
        WindowDragEdgeMailboxTestAccess::release(*box, source.edge.press, {up.epoch, 4, 1}), "Server resolves local up"
    );
  const auto release = box->readRelease();
  check(release.has_value(), "Separate bounded release evidence survives held-edge withdrawal");
  check(source.pointers.capture(released), "Original release metadata remains a single capture");
  const auto window = returning ? 0u : mode == "drop-wrong-window" ? 999u : 812u;
  if (!returning)
    check(
        source.pointers.annotate({400, 0, {0, false, 0}, -1, 300, window, window, 52, 0, 1, 3}),
        "Native release annotation"
    );
  const auto matched = source.pointers.pop(
      OriginalPointerJoin::Clock::now() + (returning ? OriginalPointerJoin::budget : std::chrono::milliseconds(0))
  );
  check(matched.has_value(), "Release joins exact raw observation");
  pointer.kind = LocalWindowPointer::Kind::Up;
  pointer.sequence = 4;
  pointer.sourceTime = 400;
  pointer.clicks = 0;
  pointer.window = window;
  check(channel->sendWindowPointer(pointer), "Host sees real up before explicit Drop on same FIFO");
  OriginalInputRouteDecision route;
  route.stamp = {up.epoch, 4, 1};
  route.kind = OriginalInputRouteKind::Button;
  route.outcome = mode.endsWith("wrong-route") ? OriginalInputRouteOutcome::Filtered
                  : returning                  ? OriginalInputRouteOutcome::Dispatched
                                               : OriginalInputRouteOutcome::Local;
  route.server = 7;
  if (returning) {
    route.recipient = 11;
    route.ticket = source.edge.peer;
    route.pointKnown = true;
  }
  core.routed({released.input, route, std::make_shared<std::atomic_bool>(false)});
  core.pointer(*matched);
  if (mode.endsWith("new-input")) {
    OriginalInputRecord next{up.epoch, 5, 500, 0, OriginalInputMotion{1, 0, originalInputButtonCount}};
    check(source.stream.push(next), "Next interaction cannot inherit pending drop");
    position->store(5);
    core.capture({*source.stream.pop(), {{up.epoch, 5, 0}, 1, false}});
  }
  if (mode == "drop-normal" || mode == "drop-server-first" || mode == "drop-transfer-failed" || mode == "drop-return" ||
      mode == "drop-return-native-first" || mode == "drop-return-transfer-failed") {
    std::optional<WindowDragEdgeMailbox::Transfer> transfer;
    if (!until([&] {
          if (!transfer)
            transfer = WindowDragEdgeMailboxTestAccess::take(*box);
          return transfer.has_value();
        }))
      throw std::runtime_error((QString("Cursor transfer missing: ") + channelError + " child: " +
                                QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError()))
                                   .toStdString());
    check(transfer->release.live() && transfer->x == 130 && transfer->y == 302, "Final preview coordinates retained");
    WindowDragEdgeMailboxTestAccess::complete(*box, *transfer, !mode.endsWith("transfer-failed"));
  }
  check(until([&] { return process.state() == QProcess::NotRunning; }), "Drop or cancellation finishes boundedly");
  const auto output = process.readAllStandardOutput() + process.readAllStandardError();
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() || !output.contains("PASS"))
    throw std::runtime_error((mode + ": " + QString::fromUtf8(output)).toStdString());
  check(!gate.active(source.edge.press), "Every exit reopens movement without posting any button");
  core.stop();
}
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
      return QString::fromLocal8Bit(argv[3]).startsWith("drop-")
                 ? dropChild(app, QString::fromLocal8Bit(argv[2]).toInt(), QString::fromLocal8Bit(argv[3]))
                 : child(app, QString::fromLocal8Bit(argv[2]).toInt(), QString::fromLocal8Bit(argv[3]));
    for (const auto *mode :
         {"normal",       "wrong-policy",  "missing-press", "host-admission", "no-connection", "core-admission",
          "core-policy",  "native-denied", "host-tls-loss", "host-reconnect", "host-destroy",  "host-throw",
          "host-slow",    "core-tls-loss", "topology-loss", "capture-loss",   "held-loss",     "admission-loss",
          "private-loss", "core-stop",     "host-silence"}) {
      QTextStream(stdout) << "RUN: " << mode << '\n' << Qt::flush;
      scenario(QString::fromLatin1(mode));
    }
    for (const auto *mode :
         {"drop-normal", "drop-server-first", "drop-wrong-window", "drop-wrong-route", "drop-new-input",
          "drop-transfer-failed", "drop-native-denied", "drop-no-commit", "drop-return", "drop-return-native-first",
          "drop-return-no-native-up", "drop-return-native-cancel", "drop-return-wrong-route", "drop-return-new-input",
          "drop-return-transfer-failed"}) {
      QTextStream(stdout) << "RUN: " << mode << '\n' << Qt::flush;
      dropScenario(QString::fromLatin1(mode));
    }
    QTextStream(stdout) << "PASS: private drag preparation, exact press/dual pin/connection, held refresh, loss, "
                           "reentrancy and cleanup\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
