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
  const bool preparing = mode.startsWith("drop-preparing");
  const bool top = mode.endsWith("-top");
  const bool queued = mode.contains("-queued");
  const std::uint64_t moveSequence = queued ? 14 : preparing ? 13 : 3;
  const auto targetX = (top ? 940 : 130) + (queued ? 5 : 0);
  const auto targetY = top ? -66 : preparing ? 312 : 302;
  bool initialReady = false, preparationSuperseded = false;
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
    if (!preparing)
      QTimer::singleShot(0, &app, [&, binding, sequence = command.sequence] { coordinator->ready(binding, sequence); });
    return true;
  };
  providers.motion = [&](const WindowDragBinding &binding, const LocalWindowDrag &command) {
    if (preparing && !initialReady) {
      preparationSuperseded = command.kind != LocalWindowDrag::Kind::Hold || command.sequence != 2;
      check(!preparationSuperseded, "Continuous movement must not supersede the pending initial preview");
      // Complete placement on the protocol's first hold, after the parent
      // has submitted all pre-ready motion. No sleep or timing race is needed.
      initialReady = true;
      coordinator->ready(binding, 2);
      return;
    }
    if (preparing && command.sequence == 12) {
      check(
          command.destinationX == (top ? 910 : 100) && command.destinationY == (top ? -51 : 297),
          "Readiness forwards the latest retained boundary position"
      );
      coordinator->ready(binding, command.sequence);
      return;
    }
    if (queued && command.sequence == 13) {
      check(
          command.destinationX == (top ? 915 : 105) && command.destinationY == (top ? -51 : 297),
          "Queued native motion contributes its original delta exactly once"
      );
      coordinator->ready(binding, command.sequence);
      return;
    }
    check(command.destinationX == targetX && command.destinationY == targetY, "Held raw deltas move only preview");
    coordinator->ready(binding, command.sequence);
  };
  providers.cancel = [&](const WindowDragBinding &) {
    ++cancelled;
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
  };
  providers.drop = [&](const WindowDragBinding &binding, const LocalWindowDrag &command, const WindowDragRelease &up) {
    ++dropped;
    check(
        command.kind == LocalWindowDrag::Kind::Drop && command.sequence == moveSequence &&
            command.releaseSequence == moveSequence + 1 && command.releaseTime == 400 &&
            up.sequence == moveSequence + 1 && up.window == 812 && up.eventNumber == 52,
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
  check(!preparationSuperseded, "Continuous movement superseded the pending initial preview");
  const bool success = mode == "drop-normal" || mode == "drop-server-first" || mode == "drop-return" ||
                       mode == "drop-return-native-first" || mode == "drop-preparing" || top ||
                       mode == "drop-preparing-queued" || mode == "drop-preparing-queued-untranslated";
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
  const bool preparing = mode.startsWith("drop-preparing");
  const bool top = mode.endsWith("-top");
  const bool queued = mode.contains("-queued");
  const bool postArmLocal = mode == "drop-preparing-post-arm-local";
  const bool abortPreparation = preparing && mode != "drop-preparing" && !top && !queued && !postArmLocal;
  const std::uint64_t moveSequence = queued ? 14 : preparing ? 13 : 3;
  const auto targetX = (top ? 940 : 130) + (queued ? 5 : 0);
  const auto targetY = top ? -66 : preparing ? 312 : 302;
  source.edge.returning = returning;
  if (top) {
    source.edge.side = WindowDragEdgeSide::Top;
    source.edge.destination.y = -950;
    source.edge.sourceX = -600;
    source.edge.sourceY = 0;
    source.edge.destinationX = 900;
    source.edge.destinationY = -51;
  }
  auto box = std::shared_ptr<WindowDragEdgeMailbox>(&source.mailbox, [](auto *) {});
  auto position = std::make_shared<std::atomic<std::uint64_t>>(2);
  check(box->bindCapture(source.stream.epoch(), position), "One producer fence binds before transfer");
  WindowDragMotionGate gate(source.stream.epoch().id());
  gate.observe({source.stream.epoch().id(), 1, 100, 0, OriginalInputButton{0, true, 1}});
  check(
      source.stream.push({source.stream.epoch().id(), 2, 200, 0, OriginalInputMotion{1, 0, 0}}), "Boundary raw motion"
  );
  gate.observe(source.stream.pop()->record);
  bool armed = false, admitted = true;
  int armAttempts = 0;
  std::optional<OriginalInputCaptured> queuedNative;
  LocalWindowDragCore *owner = nullptr;
  LocalWindowDragCore core(
      *channel, policy(),
      LocalWindowDragCore::Providers{
          [&] { return admitted; },
          [&](OriginalInputStamp press) -> std::optional<OriginalInputStamp> {
            ++armAttempts;
            if (mode == "drop-preparing-arm-denied")
              return {};
            if (queued) {
              OriginalInputRecord record{press.epoch, 13, 250, 0, OriginalInputMotion{5, 0, 0}};
              check(source.stream.push(record) && !gate.observe(record), "Queued pre-arm motion remains native");
              position->store(record.sequence);
              // Normal Mac primary movement deliberately marks its legacy
              // live-cursor translation uncertain. At a clamped edge it can
              // also produce no legacy motion event at all.
              queuedNative = OriginalInputCaptured{*source.stream.pop(), {{record.epoch, 13, 0}, 1, true}};
            }
            auto prefix = gate.armWithPrefix(press);
            armed = prefix.has_value();
            if (mode == "drop-preparing-arm-stop")
              owner->stop();
            if (mode == "drop-preparing-arm-revoked")
              admitted = false;
            if (mode == "drop-preparing-arm-throw")
              throw std::runtime_error("Arming provider failed after accepting the press");
            if (prefix && mode == "drop-preparing-arm-wrong-epoch")
              ++prefix->epoch;
            if (prefix && mode == "drop-preparing-arm-short-prefix")
              prefix->sequence = press.sequence;
            return prefix;
          },
          [&](OriginalInputStamp press) { return gate.refresh(press); }, [&] { gate.cancel(); }, box
      }
  );
  owner = &core;
  const auto expectCancelled = [&] {
    check(until([&] { return process.state() == QProcess::NotRunning; }), "Interrupted preparation cancels boundedly");
    const auto output = process.readAllStandardOutput() + process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() || !output.contains("PASS"))
      throw std::runtime_error((mode + ": " + QString::fromUtf8(output)).toStdString());
    check(!gate.active(source.edge.press), "Interrupted preparation cannot retain held motion");
    check(!WindowDragEdgeMailboxTestAccess::take(*box), "Interrupted preparation cannot switch the cursor");
    core.stop();
  };
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
  if (preparing) {
    // Do not pump the parent's event loop until these positions have all
    // arrived: readiness must make progress while the user keeps dragging.
    for (std::uint64_t sequence = 3; sequence <= 12; ++sequence) {
      OriginalInputRecord move{
          source.stream.epoch().id(), sequence, 200 + sequence, 0, OriginalInputMotion{top ? 1 : 0, top ? 0 : 1, 0}
      };
      check(source.stream.push(move) && !gate.observe(move), "Pre-ready movement remains with the native owner");
      position->store(sequence);
      core.capture({*source.stream.pop(), {{move.epoch, sequence, 0}, 1, false}});
      source.edge.motion.sequence = sequence;
      if (top)
        ++source.edge.destinationX;
      else
        ++source.edge.destinationY;
      core.observe(source.candidate());
    }
  }
  if (abortPreparation) {
    if (mode == "drop-preparing-boundary-loss")
      WindowDragEdgeMailboxTestAccess::clear(source.mailbox);
    if (mode == "drop-preparing-release") {
      OriginalInputRecord up{source.stream.epoch().id(), 13, 400, 0, OriginalInputButton{0, false, 0}};
      check(source.stream.push(up) && !gate.observe(up), "Release during preparation remains native");
      position->store(up.sequence);
      OriginalInputCaptured captured{*source.stream.pop(), {{up.epoch, up.sequence, 0}, 1, false}};
      source.join.capture(captured);
      core.capture(captured);
      core.observe({});
    }
    expectCancelled();
    check(armAttempts == (mode.contains("-arm-") ? 1 : 0), "Only current preparation may attempt one arm");
    return;
  }
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
  check(until([&] { return armed || process.state() == QProcess::NotRunning; }), "Preview readiness terminates");
  if (!armed)
    throw std::runtime_error(
        (mode + ": preview never armed: " + QString::fromUtf8(process.readAllStandardError())).toStdString()
    );
  if (queuedNative) {
    if (mode.endsWith("-untranslated"))
      queuedNative->translation.parts = 0;
    if (mode.endsWith("-multipart"))
      queuedNative->translation.parts = 2;
    if (mode.endsWith("-wrong-stamp"))
      ++queuedNative->translation.stamp.sequence;
    if (mode.endsWith("-modified"))
      queuedNative->input.record.flags = originalKey::shiftFlag;
    core.capture(*queuedNative);
    if (mode.endsWith("-multipart") || mode.endsWith("-wrong-stamp") || mode.endsWith("-modified")) {
      expectCancelled();
      return;
    }
  }
  if (postArmLocal) {
    gate.cancel();
    OriginalInputRecord record{source.stream.epoch().id(), moveSequence, 300, 0, OriginalInputMotion{5, 0, 0}};
    check(source.stream.push(record) && !gate.observe(record), "Lost gate leaves later movement native");
    position->store(record.sequence);
    core.capture({*source.stream.pop(), {{record.epoch, record.sequence, 0}, 1, false}});
    expectCancelled();
    return;
  }
  OriginalInputRecord move{
      source.stream.epoch().id(), moveSequence, 300, 0, OriginalInputMotion{30, top ? -15 : 15, 0}
  };
  check(source.stream.push(move), "Held raw movement enters sole consumer");
  const bool suppressed = gate.observe(move);
  check(suppressed, "Admitted held motion is withheld");
  position->store(moveSequence);
  OriginalInputCaptured captured{*source.stream.pop(), {{move.epoch, moveSequence, 0}, 0, false}, {}, {}, suppressed};
  core.capture(captured);
  OriginalInputRecord up{move.epoch, moveSequence + 1, 400, 0, OriginalInputButton{0, false, 0}};
  check(source.stream.push(up) && !gate.observe(up), "Real zero-click up always dispatches normally");
  position->store(up.sequence);
  OriginalInputCaptured released{
      *source.stream.pop(),
      {{up.epoch, up.sequence, 0}, 1, false},
      {},
      OriginalPointerObservation{up.epoch, up.sequence, 400, -1, 300, 0, 0, 52, 0, 1, 3}
  };
  if (mode == "drop-server-first")
    check(
        WindowDragEdgeMailboxTestAccess::release(*box, source.edge.press, {up.epoch, up.sequence, 1}),
        "Server wins release race"
    );
  source.join.capture(released);
  core.capture(released);
  core.observe({}); // Up withdraws held evidence without cancelling a pending joined drop.
  if (mode != "drop-server-first")
    check(
        WindowDragEdgeMailboxTestAccess::release(*box, source.edge.press, {up.epoch, up.sequence, 1}),
        "Server resolves local up"
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
  pointer.sequence = up.sequence;
  pointer.sourceTime = 400;
  pointer.clicks = 0;
  pointer.window = window;
  check(channel->sendWindowPointer(pointer), "Host sees real up before explicit Drop on same FIFO");
  OriginalInputRouteDecision route;
  route.stamp = {up.epoch, up.sequence, 1};
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
      mode == "drop-return-native-first" || mode == "drop-return-transfer-failed" || preparing) {
    std::optional<WindowDragEdgeMailbox::Transfer> transfer;
    if (!until([&] {
          if (!transfer)
            transfer = WindowDragEdgeMailboxTestAccess::take(*box);
          return transfer.has_value();
        }))
      throw std::runtime_error((QString("Cursor transfer missing: ") + channelError + " child: " +
                                QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError()))
                                   .toStdString());
    check(
        transfer->release.live() && transfer->x == targetX && transfer->y == targetY,
        "Final preview coordinates retained"
    );
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
         {"drop-preparing",
          "drop-preparing-top",
          "drop-preparing-queued",
          "drop-preparing-queued-top",
          "drop-preparing-queued-untranslated",
          "drop-preparing-queued-multipart",
          "drop-preparing-queued-wrong-stamp",
          "drop-preparing-queued-modified",
          "drop-preparing-post-arm-local",
          "drop-preparing-release",
          "drop-preparing-boundary-loss",
          "drop-preparing-arm-denied",
          "drop-preparing-arm-stop",
          "drop-preparing-arm-revoked",
          "drop-preparing-arm-throw",
          "drop-preparing-arm-wrong-epoch",
          "drop-preparing-arm-short-prefix",
          "drop-normal",
          "drop-server-first",
          "drop-wrong-window",
          "drop-wrong-route",
          "drop-new-input",
          "drop-transfer-failed",
          "drop-native-denied",
          "drop-no-commit",
          "drop-return",
          "drop-return-native-first",
          "drop-return-no-native-up",
          "drop-return-native-cancel",
          "drop-return-wrong-route",
          "drop-return-new-input",
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
