/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputChannel.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTextStream>
#include <QThread>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace deskflow::windowshare;
namespace deskflow::windowshare {
struct LocalInputChannelTestAccess
{
  static void freeze(LocalInputChannel &channel)
  {
    channel.m_timer.stop();
    QObject::disconnect(&channel.m_socket, nullptr, &channel, nullptr);
  }
  static qint64 queued(const LocalInputChannel &channel)
  {
    return channel.m_socket.bytesToWrite();
  }
  static void exhaustOutput(LocalInputChannel &channel)
  {
    channel.m_sent = std::numeric_limits<std::uint64_t>::max();
  }
  static void exhaustInput(LocalInputChannel &channel)
  {
    channel.m_received = std::numeric_limits<std::uint64_t>::max();
  }
  static bool rawOriginal(LocalInputChannel &channel, const QByteArray &payload)
  {
    return channel.send(static_cast<LocalInputChannel::Type>(11), payload);
  }
  static void stopTimer(LocalInputChannel &channel)
  {
    channel.m_timer.stop();
  }
  static void flush(LocalInputChannel &channel)
  {
    channel.m_socket.flush();
  }
  static void disconnectTransport(LocalInputChannel &channel)
  {
    channel.m_socket.abort();
  }
  static bool captureAt(const LocalInputChannel &channel, std::uint64_t sequence, std::uint64_t timestamp)
  {
    return channel.m_originalSequence == sequence && channel.m_originalTimestamp == timestamp;
  }
  static void incoming(LocalInputChannel &channel, unsigned type, const QByteArray &payload = {})
  {
    // Model a complete frame already queued when a delayed readyRead runs.
    QByteArray frame;
    const auto number = [&](std::uint64_t value, int bytes) {
      for (int index = bytes - 1; index >= 0; --index)
        frame.append(static_cast<char>(value >> (index * 8)));
    };
    number(0x444c4931, 4);
    number(LocalInputChannel::version, 2);
    number(type, 1);
    number(channel.m_role == LocalInputRole::Core ? 2 : 1, 1);
    number(payload.size(), 4);
    number(channel.m_received + 1, 8);
    for (const auto &value : {channel.m_coreIncarnation, channel.m_hostIncarnation})
      frame.append(reinterpret_cast<const char *>(value.data()), value.size());
    frame += payload;
    channel.m_incoming = frame;
    channel.m_expected = frame.size();
    channel.read();
  }
};
} // namespace deskflow::windowshare

namespace {
constexpr LocalInputLimits limits{20, 400};
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
LocalInputLease fixtureLease()
{
  LocalInputLease lease;
  lease.deskflowClientIncarnation[0] = 1;
  lease.windowShareClientIncarnation[0] = 2;
  lease.deskflowPeerPin[0] = 3;
  lease.windowSharePeerPin[0] = 4;
  lease.session = 11;
  lease.epoch = 12;
  lease.focusRevision = 13;
  lease.geometryRevision = 14;
  lease.routeGeneration = 15;
  return lease;
}
OriginalKeyEvent fixtureEvent(std::uint64_t sequence, OriginalKeyTransition transition = OriginalKeyTransition::Down)
{
  OriginalKeyEvent event;
  event.sample.sequence = sequence;
  event.sample.timestampNs = sequence * 100;
  event.sample.keyboardType = 40;
  event.sample.kind = transition == OriginalKeyTransition::Up ? OriginalKeyKind::Up : OriginalKeyKind::Down;
  event.sample.repeat = transition == OriginalKeyTransition::Repeat;
  event.transition = transition;
  return event;
}
void number(QByteArray &bytes, std::uint64_t value, int size)
{
  for (int i = size - 1; i >= 0; --i)
    bytes.append(static_cast<char>(value >> (i * 8)));
}
QByteArray originalPayload(const LocalInputLease &lease, const OriginalKeyEvent &event)
{
  QByteArray bytes;
  auto append = [&](const auto &value) { bytes.append(reinterpret_cast<const char *>(value.data()), value.size()); };
  append(lease.deskflowClientIncarnation);
  append(lease.windowShareClientIncarnation);
  append(lease.deskflowPeerPin);
  append(lease.windowSharePeerPin);
  for (auto value :
       {lease.session, lease.epoch, lease.focusRevision, lease.geometryRevision, lease.routeGeneration,
        event.sample.sequence, event.sample.timestampNs, event.sample.flags})
    number(bytes, value, 8);
  number(bytes, event.sample.keyboardType, 4);
  number(bytes, event.sample.keyCode, 2);
  number(bytes, static_cast<unsigned>(event.sample.kind), 1);
  number(bytes, event.sample.repeat, 1);
  number(bytes, static_cast<unsigned>(event.transition), 1);
  return bytes;
}

struct Fixture
{
  QProcess child;
  std::unique_ptr<LocalInputChannel> core;
  bool closed = false, ready = false, prepared = false, active = false;
  int revoked = 0;
  QString reason;

  explicit Fixture(const QString &mode = "passive", LocalInputLimits coreLimits = limits)
  {
    auto pair = LocalInputSocketPair::create();
#ifdef Q_OS_UNIX
    check((fcntl(pair.childDescriptor(), F_GETFD) & FD_CLOEXEC) != 0, "Parent capability is inheritable");
#endif
    pair.configureChild(child);
    child.start(QCoreApplication::applicationFilePath(), {"--child", QString::number(pair.childDescriptor()), mode});
    check(child.waitForStarted(2000), "Cannot start local input fixture child");
    const auto identity = localInputProcess(child.processId());
    check(identity.has_value(), "Cannot inspect launched fixture process");
    core = pair.adoptCore(*identity, coreLimits);
    core->authorize = [](const LocalInputLease &lease, LocalInputCheck) { return lease == fixtureLease(); };
    core->onAuthenticated = [&] { ready = true; };
    core->onPrepared = [&](const LocalInputLease &lease) {
      check(lease == fixtureLease(), "Prepared acknowledgement changed lease");
      prepared = true;
    };
    core->onActivated = [&](const LocalInputLease &lease) {
      check(lease == fixtureLease(), "Commit acknowledgement changed lease");
      active = true;
    };
    core->onRevoked = [&](const LocalInputLease &lease, LocalInputRevocation) {
      check(lease == fixtureLease(), "Revocation changed the release owner");
      ++revoked;
    };
    core->onClosed = [&](QString why) {
      reason = why;
      closed = true;
    };
  }
  ~Fixture()
  {
    if (core) {
      core->onRevoked = {};
      core->onClosed = {};
      core->close();
      core.reset();
    }
    if (child.state() != QProcess::NotRunning) {
      child.kill();
      child.waitForFinished(2000);
    }
  }
  void establish()
  {
    check(until([&] { return ready || closed; }) && ready && !closed, "Private bootstrap did not authenticate");
  }
  void prepare()
  {
    establish();
    check(!core->sendOriginal(fixtureLease(), fixtureEvent(1)), "Original event escaped before preparation");
    check(core->prepare(fixtureLease()), "Core could not prepare lease");
    check(until([&] { return prepared || closed; }) && prepared && !closed, "Host did not acknowledge prepare");
    check(!core->sendOriginal(fixtureLease(), fixtureEvent(1)), "Original event escaped before commit");
  }
  void activate()
  {
    prepare();
    check(core->commit(fixtureLease()), "Core could not commit lease");
    check(until([&] { return active || closed; }) && active && !closed, "Lease did not become active");
  }
  void childExitedCleanly()
  {
    check(until([&] { return child.state() == QProcess::NotRunning; }), "Fixture child failed to exit");
    check(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, "Fixture child rejected invariant");
  }
};

#ifdef Q_OS_UNIX
QByteArray readHello(int descriptor)
{
  QByteArray result;
  QElapsedTimer timer;
  timer.start();
  while (result.size() < 76 && timer.elapsed() < 2000) {
    pollfd poller{descriptor, POLLIN, 0};
    if (poll(&poller, 1, 50) <= 0)
      continue;
    char buffer[76];
    const auto count = ::read(descriptor, buffer, 76 - result.size());
    if (count <= 0)
      break;
    result.append(buffer, count);
  }
  check(result.size() == 76, "Raw fixture did not receive bootstrap");
  return result;
}
int rawChild(QCoreApplication &application, int descriptor, const QString &mode)
{
  auto frame = readHello(descriptor);
  frame[6] = 2;
  frame[7] = 2;
  if (mode == "raw-magic")
    frame[0] = 'X';
  else if (mode == "raw-version")
    frame[5] = 2;
  else if (mode == "raw-role")
    frame[7] = 1;
  else if (mode == "raw-incarnation")
    frame[20] = static_cast<char>(frame[20] ^ 1);
  else if (mode == "raw-zero-incarnation")
    frame.replace(20, 16, QByteArray(16, '\0'));
  else if (mode == "raw-process")
    frame[75] = static_cast<char>(frame[75] ^ 1);
  else if (mode == "raw-sequence")
    frame.replace(12, 8, QByteArray(8, '\0'));
  else if (mode == "raw-oversize") {
    frame[10] = 1;
    frame[11] = 1;
    frame.resize(52);
  } else if (mode == "raw-partial-header")
    frame.resize(17);
  else if (mode == "raw-partial-payload")
    frame.resize(62);
  else if (mode == "raw-trailing") {
    frame[11] = 25;
    frame.append('x');
  } else if (mode == "raw-unknown")
    frame[6] = static_cast<char>(255);
  else if (mode == "raw-replay") {
    auto ping = frame.left(52);
    ping[6] = 4;
    ping.replace(8, 4, QByteArray(4, '\0'));
    frame += ping; // Reuses sequence 1 after the valid HostHello.
  } else if (mode != "raw-silent")
    check(false, "Unknown raw fixture mode");
  if (mode != "raw-silent")
    check(::write(descriptor, frame.constData(), frame.size()) == frame.size(), "Raw fixture write failed");
  QTimer::singleShot(2500, &application, &QCoreApplication::quit);
  return application.exec();
}
#endif

int childMain(QCoreApplication &application, int descriptor, const QString &mode)
{
#ifdef Q_OS_UNIX
  if (mode.startsWith("raw-"))
    return rawChild(application, descriptor, mode);
  const auto expectedCore = localInputProcess(getppid());
  check(expectedCore.has_value(), "Controlled fixture launcher is unavailable");
  auto host =
      LocalInputChannel::adoptHost(descriptor, *expectedCore, mode == "patient" ? LocalInputLimits{20, 2000} : limits);
  check((fcntl(descriptor, F_GETFD) & FD_CLOEXEC) != 0, "Adopted child capability is inheritable");
  std::uint64_t originalEvents = 0;
  int callbackEvents = 0;
  bool retiredDelivered = false;
  host->authorize = [&, marker = QString(256, 'a')](const LocalInputLease &lease, LocalInputCheck phase) {
    if (phase == LocalInputCheck::Deliver && mode == "nested-capture") {
      auto retired = fixtureLease();
      --retired.routeGeneration;
      LocalInputChannelTestAccess::incoming(*host, 11, originalPayload(retired, fixtureEvent(2)));
      return true;
    }
    if (phase == LocalInputCheck::Deliver && mode.startsWith("host-authority-")) {
      if (mode.endsWith("revoke"))
        host->revoke();
      else if (mode.endsWith("close"))
        host->close();
      else
        host.reset();
      ++callbackEvents;
      check(marker.at(callbackEvents % marker.size()) == 'a', "Authorization callback capture did not survive");
      return true;
    }
    auto binding = lease;
    if (mode == "reuse" || mode == "revoke-prepared" || mode.startsWith("retired-"))
      binding.routeGeneration = fixtureLease().routeGeneration;
    return binding == fixtureLease() && !(mode == "deny-commit" && phase == LocalInputCheck::Commit);
  };
  host->onPrepare = [&, marker = QString(256, 'p')](const LocalInputLease &lease) {
    if (mode == "delete-prepare") {
      host.reset();
      ++callbackEvents;
      check(marker.at(callbackEvents % marker.size()) == 'p', "Prepare callback capture did not survive");
      return;
    }
    check(host->acknowledgePrepare(lease), "Child prepare failed");
    if (mode == "revoke-prepared" && lease == fixtureLease())
      host->revoke();
  };
  host->onActivated = [&](const LocalInputLease &lease) {
    if (mode == "freeze")
      LocalInputChannelTestAccess::freeze(*host);
    if (mode == "wrong-direction")
      LocalInputChannelTestAccess::rawOriginal(*host, originalPayload(fixtureLease(), fixtureEvent(1)));
    if (mode.startsWith("host-authority-") || mode == "nested-capture")
      LocalInputChannelTestAccess::stopTimer(*host);
    if (mode == "nested-capture")
      LocalInputChannelTestAccess::incoming(*host, 11, originalPayload(lease, fixtureEvent(1)));
    if (mode == "revoke-prepared") {
      check(
          lease.routeGeneration == fixtureLease().routeGeneration + 1,
          "Cancelled prepared lease was activated by late Commit"
      );
      retiredDelivered = true;
    }
    if (mode == "expired-original") {
      LocalInputChannelTestAccess::stopTimer(*host);
      LocalInputChannelTestAccess::flush(*host);
      QThread::msleep(limits.timeoutMs + 50);
      LocalInputChannelTestAccess::incoming(*host, 11, originalPayload(lease, fixtureEvent(1)));
    }
    if (mode == "disconnected-original") {
      LocalInputChannelTestAccess::disconnectTransport(*host);
      LocalInputChannelTestAccess::incoming(*host, 11, originalPayload(lease, fixtureEvent(1)));
    }
    if ((mode == "reuse" || mode.startsWith("retired-")) && lease.routeGeneration == fixtureLease().routeGeneration)
      host->revoke();
  };
  host->onOriginal = [&, marker = QString(256, 'o')](const LocalInputLease &lease, const OriginalKeyEvent &event) {
    ++originalEvents;
    if (mode == "delete-original") {
      host.reset();
      ++callbackEvents;
      check(marker.at(callbackEvents % marker.size()) == 'o', "Original callback capture did not survive");
      return;
    }
    if (mode == "retired-original") {
      auto next = fixtureLease();
      ++next.routeGeneration;
      check(
          lease == next && originalEvents == 1 && event.sample.sequence == 3,
          "Retired input was delivered or new input lost its capture ordering"
      );
      retiredDelivered = true;
      host->revoke();
      return;
    }
    if (mode != "roundtrip") {
      application.exit(3);
      return;
    }
    check(
        lease == fixtureLease() && event.sample.keyboardType == 40 && event.sample.sequence == originalEvents,
        "Original event envelope changed"
    );
    check(
        event.transition == (originalEvents == 1 ? OriginalKeyTransition::Down : OriginalKeyTransition::Up),
        "Original transition changed"
    );
    if (originalEvents == 2)
      host->revoke();
  };
  host->onClosed = [&](QString) { application.quit(); };
  const auto status = application.exec();
  if (mode == "nested-capture")
    check(
        host && LocalInputChannelTestAccess::captureAt(*host, 2, 200) && originalEvents == 0,
        "Nested retired capture was rolled back or older original was delivered"
    );
  if (host) {
    host->onClosed = {};
    host->onRevoked = {};
    host->close();
  }
  if (mode.startsWith("delete-") || mode.startsWith("host-authority-"))
    check(callbackEvents == 1, "Destructive callback fixture did not execute exactly once");
  if (mode == "expired-original" || mode == "disconnected-original" || mode.startsWith("host-authority-"))
    check(originalEvents == 0, "Revoked or expired original event reached the receiver");
  if (mode == "retired-original")
    check(retiredDelivered && originalEvents == 1, "Retired input prevented the next owner's valid delivery");
  else if (mode.startsWith("retired-"))
    check(originalEvents == 0, "Invalid retired input was delivered");
  if (mode == "revoke-prepared")
    check(retiredDelivered, "Host cancellation prevented the next route from activating");
  return status;
#else
  Q_UNUSED(application);
  Q_UNUSED(descriptor);
  Q_UNUSED(mode);
  return 1;
#endif
}

void roundtrip()
{
  Fixture fixture("roundtrip");
  fixture.activate();
  check(fixture.core->sendOriginal(fixtureLease(), fixtureEvent(1)), "Original down not queued");
  check(
      fixture.core->sendOriginal(fixtureLease(), fixtureEvent(2, OriginalKeyTransition::Up)), "Original up not queued"
  );
  check(until([&] { return fixture.revoked == 1; }), "Child did not receive original event pair");
  check(fixture.core->phase() == LocalInputPhase::Idle && !fixture.core->lease(), "Revoked lease remained active");
  check(!fixture.core->prepare(fixtureLease()), "Reused route generation was accepted");
  check(!fixture.core->sendOriginal(fixtureLease(), fixtureEvent(3)), "Original event escaped after revoke");
  fixture.core->close();
  fixture.childExitedCleanly();
}
void authoritativeCommit()
{
  Fixture fixture;
  fixture.prepare();
  int queried = 0;
  fixture.core->authorize = [&](const LocalInputLease &lease, LocalInputCheck phase) {
    if (phase == LocalInputCheck::Commit) {
      ++queried;
      return false; // Local physical/owner/viewer/focus gate is not ready.
    }
    return lease == fixtureLease();
  };
  check(
      !fixture.core->commit(fixtureLease()) && queried == 1 && !fixture.active,
      "Peer preparation bypassed authoritative commit state"
  );
  check(!fixture.core->sendOriginal(fixtureLease(), fixtureEvent(1)), "Input escaped denied commit");
  fixture.core->revoke();
  check(fixture.revoked == 1, "Prepared owner's release was not requested");
  fixture.core->close();
  fixture.childExitedCleanly();
  Fixture denied("deny-commit");
  denied.prepare();
  check(denied.core->commit(fixtureLease()), "Could not exercise host commit authority");
  check(
      until([&] { return denied.closed; }) && !denied.active && denied.revoked == 1,
      "Core commit bypassed host's local authority"
  );
}
void malformedBootstrap()
{
  for (const auto *mode :
       {"magic", "version", "role", "incarnation", "zero-incarnation", "process", "sequence", "oversize",
        "partial-header", "partial-payload", "trailing", "unknown", "replay", "silent"}) {
    Fixture fixture(QStringLiteral("raw-") + mode);
    check(until([&] { return fixture.closed; }), "Malformed bootstrap did not fail closed");
    check(
        !fixture.core->authenticated() && !fixture.active && fixture.revoked == 0,
        "Malformed bootstrap gained input authority"
    );
  }
}
void changedBindings()
{
  for (int field = 0; field < 9; ++field) {
    Fixture fixture;
    fixture.activate();
    auto lease = fixtureLease();
    switch (field) {
    case 0:
      ++lease.deskflowClientIncarnation[0];
      break;
    case 1:
      ++lease.windowShareClientIncarnation[0];
      break;
    case 2:
      ++lease.deskflowPeerPin[0];
      break;
    case 3:
      ++lease.windowSharePeerPin[0];
      break;
    case 4:
      ++lease.session;
      break;
    case 5:
      ++lease.epoch;
      break;
    case 6:
      ++lease.focusRevision;
      break;
    case 7:
      ++lease.geometryRevision;
      break;
    case 8:
      ++lease.routeGeneration;
      break;
    }
    check(!fixture.core->sendOriginal(lease, fixtureEvent(1)), "Local API accepted a different lease");
    check(
        LocalInputChannelTestAccess::rawOriginal(*fixture.core, originalPayload(lease, fixtureEvent(1))),
        "Cannot inject mismatched binding fixture"
    );
    check(until([&] { return fixture.closed; }) && fixture.revoked == 1, "Changed binding did not revoke owner");
    fixture.childExitedCleanly();
  }
  Fixture role("wrong-direction");
  role.prepare();
  check(role.core->commit(fixtureLease()), "Cannot commit role test");
  check(until([&] { return role.closed; }) && role.revoked == 1, "Host acquired original-key send authority");
}
void expiryAtUse()
{
  for (const auto stage : {LocalInputCheck::Prepare, LocalInputCheck::Commit, LocalInputCheck::Deliver}) {
    Fixture fixture("patient");
    if (stage == LocalInputCheck::Prepare)
      fixture.establish();
    else if (stage == LocalInputCheck::Commit)
      fixture.prepare();
    else
      fixture.activate();
    LocalInputChannelTestAccess::stopTimer(*fixture.core);
    QThread::msleep(limits.timeoutMs + 50); // No Qt events: the timeout slot cannot run first.
    bool accepted = false;
    if (stage == LocalInputCheck::Prepare)
      accepted = fixture.core->prepare(fixtureLease());
    else if (stage == LocalInputCheck::Commit)
      accepted = fixture.core->commit(fixtureLease());
    else
      accepted = fixture.core->sendOriginal(fixtureLease(), fixtureEvent(1));
    check(!accepted && fixture.closed && !fixture.core->authenticated(), "Expired authority survived an API call");
    check(fixture.revoked == (stage == LocalInputCheck::Prepare ? 0 : 1), "Expiry lost the exact release owner");
    fixture.childExitedCleanly();
  }
  {
    Fixture fixture("patient");
    fixture.activate();
    LocalInputChannelTestAccess::stopTimer(*fixture.core);
    QThread::msleep(limits.timeoutMs + 50);
    LocalInputChannelTestAccess::incoming(*fixture.core, 4); // A queued valid Ping precedes the timer slot.
    check(fixture.closed && fixture.revoked == 1, "Queued heartbeat revived an expired lease");
    fixture.childExitedCleanly();
  }
  {
    Fixture fixture("patient");
    fixture.activate();
    fixture.core->authorize = [](const LocalInputLease &, LocalInputCheck) {
      QThread::msleep(limits.timeoutMs + 50);
      return true;
    };
    check(
        !fixture.core->sendOriginal(fixtureLease(), fixtureEvent(1)) && fixture.closed && fixture.revoked == 1,
        "Slow authorization granted expired authority"
    );
    fixture.childExitedCleanly();
  }
  Fixture original("expired-original", {20, 2000});
  original.activate();
  check(until([&] { return original.closed; }) && original.revoked == 1, "Expired receiver retained its lease");
  original.childExitedCleanly(); // Child asserts zero original delivery after its loop stall.
  Fixture disconnected("disconnected-original");
  disconnected.prepare();
  check(disconnected.core->commit(fixtureLease()), "Cannot start queued disconnect fixture");
  check(until([&] { return disconnected.closed; }), "Known disconnected transport retained authority");
  disconnected.childExitedCleanly(); // Child forces EOF before a previously queued Original/read notification.
}
void reentrantAuthorization()
{
  for (const QString stage : {"prepare", "commit", "deliver", "prepared", "committed"}) {
    for (const QString action : {"revoke", "close", "delete", "throw"}) {
      Fixture fixture;
      if (stage == "commit" || stage == "committed")
        fixture.prepare();
      else if (stage == "deliver")
        fixture.activate();
      else
        fixture.establish();
      int queries = 0, survived = 0;
      const bool incoming = stage == "prepared" || stage == "committed";
      const auto requested = stage == "commit" || stage == "committed" ? LocalInputCheck::Commit
                             : stage == "deliver"                      ? LocalInputCheck::Deliver
                                                                       : LocalInputCheck::Prepare;
      fixture.core->authorize = [&, marker = QString(256, 'r')](const LocalInputLease &, LocalInputCheck checkKind) {
        if (checkKind != requested || ++queries != (incoming ? 2 : 1))
          return true;
        if (action == "revoke")
          fixture.core->revoke();
        else if (action == "close")
          fixture.core->close();
        else if (action == "delete")
          fixture.core.reset();
        else
          throw std::runtime_error("Deliberate authority fixture failure");
        ++survived;
        check(marker.at(survived % marker.size()) == 'r', "Authorization capture was destroyed while executing");
        return true; // Must not grant through its own revoke/close/destruction.
      };
      bool accepted = false;
      if (requested == LocalInputCheck::Prepare)
        accepted = fixture.core->prepare(fixtureLease());
      else if (requested == LocalInputCheck::Commit)
        accepted = fixture.core->commit(fixtureLease());
      else
        accepted = fixture.core->sendOriginal(fixtureLease(), fixtureEvent(1));
      if (incoming)
        check(accepted && until([&] { return fixture.closed || !fixture.core; }), "Reentrant acknowledgement survived");
      else
        check(!accepted, "Reentrant authority granted an operation");
      check(survived == (action == "throw" ? 0 : 1), "Authority callback did not finish after mutation");
      check(
          !fixture.core || fixture.core->phase() == LocalInputPhase::Idle,
          "Reentrant authority retained or restored a lease"
      );
      if (fixture.core)
        fixture.core->close();
      fixture.childExitedCleanly();
    }
  }
  for (const QString action : {"revoke", "close", "delete"}) {
    Fixture fixture("host-authority-" + action);
    fixture.activate();
    check(fixture.core->sendOriginal(fixtureLease(), fixtureEvent(1)), "Cannot queue host authority fixture event");
    check(until([&] { return fixture.closed; }) && fixture.revoked == 1, "Host mutation retained input authority");
    fixture.childExitedCleanly(); // Child asserts no onOriginal after authorizer mutation.
  }
  Fixture nested("nested-capture");
  nested.prepare();
  check(nested.core->commit(fixtureLease()), "Cannot start nested capture fixture");
  check(until([&] { return nested.closed; }) && nested.revoked == 1, "Superseded original retained authority");
  nested.childExitedCleanly(); // Child asserts the newer capture survives and no older original was delivered.
}
void destructiveNotifications()
{
  for (const QString stage : {"authenticated", "prepared", "activated", "revoked", "closed"}) {
    Fixture fixture;
    if (stage == "prepared")
      fixture.establish();
    else if (stage == "activated")
      fixture.prepare();
    else if (stage == "revoked" || stage == "closed")
      fixture.activate();
    int survived = 0;
    const auto destroy = [&, marker = QString(256, 'n')] {
      fixture.core.reset();
      ++survived;
      check(marker.at(survived % marker.size()) == 'n', "Notification capture was destroyed while executing");
    };
    if (stage == "authenticated")
      fixture.core->onAuthenticated = destroy;
    else if (stage == "prepared") {
      fixture.core->onPrepared = [destroy](const LocalInputLease &) { destroy(); };
      check(fixture.core->prepare(fixtureLease()), "Cannot queue destructive prepared notification");
    } else if (stage == "activated") {
      fixture.core->onActivated = [destroy](const LocalInputLease &) { destroy(); };
      check(fixture.core->commit(fixtureLease()), "Cannot queue destructive activated notification");
    } else if (stage == "revoked") {
      fixture.core->onRevoked = [destroy](const LocalInputLease &, LocalInputRevocation) { destroy(); };
      fixture.core->revoke();
    } else {
      fixture.core->onClosed = [destroy](QString) { destroy(); };
      fixture.core->close();
    }
    check(until([&] { return !fixture.core; }) && survived == 1, "Destructive notification did not finish safely");
    fixture.childExitedCleanly();
  }
  for (const QString stage : {"prepare", "original"}) {
    Fixture fixture("delete-" + stage);
    if (stage == "prepare") {
      fixture.establish();
      check(fixture.core->prepare(fixtureLease()), "Cannot queue destructive host prepare");
    } else {
      fixture.activate();
      check(fixture.core->sendOriginal(fixtureLease(), fixtureEvent(1)), "Cannot queue destructive host original");
    }
    check(until([&] { return fixture.closed; }), "Destroyed host did not close its transport");
    fixture.childExitedCleanly();
  }
  for (bool duringClose : {false, true}) {
    Fixture fixture;
    fixture.activate();
    fixture.core->onRevoked = [](const LocalInputLease &, LocalInputRevocation) {
      throw std::runtime_error("Deliberate release callback failure");
    };
    if (duringClose)
      fixture.core->close();
    else
      fixture.core->revoke();
    check(fixture.closed && !fixture.core->authenticated(), "Throwing release callback left transport alive");
    fixture.childExitedCleanly();
  }
}
void cancelledLeaseReuse()
{
  Fixture fixture("reuse");
  fixture.prepare();
  auto middle = fixtureLease(), latest = fixtureLease();
  ++middle.routeGeneration;
  latest.routeGeneration += 2;
  fixture.core->authorize = [](LocalInputLease lease, LocalInputCheck) {
    lease.routeGeneration = fixtureLease().routeGeneration;
    return lease == fixtureLease();
  };
  bool reused = false;
  int releases = 0;
  fixture.core->onPrepared = [&](const LocalInputLease &lease) {
    check(lease == latest, "Cancelled preparation acknowledgement reached the current route");
    check(fixture.core->commit(lease), "Cannot commit reused route");
  };
  fixture.core->onRevoked = [&](const LocalInputLease &lease, LocalInputRevocation) {
    ++releases;
    if (lease == fixtureLease()) {
      check(fixture.core->prepare(middle), "Cannot prepare from release callback");
      fixture.core->revoke(); // Cancel before its acknowledgement can arrive.
      check(fixture.core->prepare(latest), "Cannot prepare after cancelling intermediate route");
    }
  };
  fixture.core->onActivated = [&](const LocalInputLease &lease) {
    if (lease == fixtureLease())
      fixture.core->revoke(); // Host has also queued its old Revoke in the other direction.
    else {
      check(lease == latest, "Wrong generation activated during reuse");
      reused = true;
    }
  };
  check(fixture.core->commit(fixtureLease()), "Cannot start crossed revoke fixture");
  check(
      until([&] { return reused || fixture.closed; }) && reused && !fixture.closed && releases == 2 &&
          fixture.core->lease() == latest,
      "Crossed revoke or cancelled acknowledgement corrupted the newer lease"
  );
  fixture.core->close();
  fixture.childExitedCleanly();

  Fixture committing("reuse");
  committing.prepare();
  committing.core->authorize = [](LocalInputLease lease, LocalInputCheck) {
    lease.routeGeneration = fixtureLease().routeGeneration;
    return lease == fixtureLease();
  };
  bool committedNew = false;
  int cancelledReleases = 0;
  committing.core->onRevoked = [&](const LocalInputLease &, LocalInputRevocation) { ++cancelledReleases; };
  committing.core->onPrepared = [&](const LocalInputLease &lease) {
    check(lease == latest && committing.core->commit(lease), "Cancelled commit corrupted the new preparation");
  };
  committing.core->onActivated = [&](const LocalInputLease &lease) {
    check(lease == latest, "Cancelled commit acknowledgement activated the old lease");
    committedNew = true;
  };
  check(committing.core->commit(fixtureLease()), "Cannot start cancelled commit");
  committing.core->revoke(); // Cancel while COMMITTING, before any acknowledgement is dispatched.
  check(committing.core->prepare(latest), "Cannot prepare after cancelled commit");
  check(
      until([&] { return committedNew || committing.closed; }) && committedNew && !committing.closed &&
          cancelledReleases == 1,
      "Obsolete commit acknowledgement corrupted the newer route"
  );
  committing.core->close();
  committing.childExitedCleanly();

  Fixture hostCancelled("revoke-prepared");
  hostCancelled.establish();
  auto next = fixtureLease();
  ++next.routeGeneration;
  hostCancelled.core->authorize = [](LocalInputLease lease, LocalInputCheck) {
    lease.routeGeneration = fixtureLease().routeGeneration;
    return lease == fixtureLease();
  };
  bool activeAfterHostCancel = false;
  int hostCancelledReleases = 0;
  hostCancelled.core->onPrepared = [&](const LocalInputLease &lease) {
    check(hostCancelled.core->commit(lease), "Cannot commit before observing host cancellation");
  };
  hostCancelled.core->onRevoked = [&](const LocalInputLease &lease, LocalInputRevocation) {
    ++hostCancelledReleases;
    if (lease == fixtureLease())
      check(hostCancelled.core->prepare(next), "Cannot reprepare after host cancellation");
  };
  hostCancelled.core->onActivated = [&](const LocalInputLease &lease) {
    check(lease == next, "Late commit activated the host-cancelled lease");
    activeAfterHostCancel = true;
  };
  check(hostCancelled.core->prepare(fixtureLease()), "Cannot start host-cancelled preparation");
  check(
      until([&] { return activeAfterHostCancel || hostCancelled.closed; }) && activeAfterHostCancel &&
          !hostCancelled.closed && hostCancelledReleases == 1,
      "Late Commit for host-cancelled preparation closed or changed the reusable channel"
  );
  hostCancelled.core->close();
  hostCancelled.childExitedCleanly();
}
void retiredOriginals()
{
  Fixture fixture("retired-original");
  fixture.prepare();
  auto next = fixtureLease();
  ++next.routeGeneration;
  fixture.core->authorize = [](LocalInputLease lease, LocalInputCheck) {
    lease.routeGeneration = fixtureLease().routeGeneration;
    return lease == fixtureLease();
  };
  bool deliveredNew = false;
  int releases = 0;
  fixture.core->onPrepared = [&](const LocalInputLease &lease) {
    check(lease == next && fixture.core->commit(lease), "Cannot reuse after retired input");
  };
  fixture.core->onRevoked = [&](const LocalInputLease &lease, LocalInputRevocation) {
    ++releases;
    if (lease == fixtureLease())
      check(fixture.core->prepare(next), "Retired input prevented reprepare");
    else {
      check(lease == next, "Wrong owner released during retired input test");
      deliveredNew = true;
    }
  };
  fixture.core->onActivated = [&](const LocalInputLease &lease) {
    if (lease == fixtureLease())
      check(fixture.core->sendOriginal(lease, fixtureEvent(1)), "Cannot queue input crossing host revoke");
    else {
      // Also exercise the strictly-older-generation branch while a new owner
      // is active. Both discarded frames must advance capture replay state.
      check(
          LocalInputChannelTestAccess::rawOriginal(*fixture.core, originalPayload(fixtureLease(), fixtureEvent(2))),
          "Cannot queue older-generation original fixture"
      );
      check(fixture.core->sendOriginal(lease, fixtureEvent(3)), "Cannot deliver new owner's original event");
    }
  };
  check(fixture.core->commit(fixtureLease()), "Cannot start retired input fixture");
  check(
      until([&] { return deliveredNew || fixture.closed; }) && deliveredNew && !fixture.closed && releases == 2,
      "Retired original input closed the reusable channel or reached the wrong owner"
  );
  fixture.core->close();
  fixture.childExitedCleanly();

  for (const QString invalid : {"replay", "timestamp", "malformed", "binding", "future"}) {
    Fixture rejected("retired-" + invalid);
    rejected.prepare();
    rejected.core->onActivated = [&](const LocalInputLease &) {
      check(rejected.core->sendOriginal(fixtureLease(), fixtureEvent(1)), "Cannot queue retired validation fixture");
      auto lease = fixtureLease();
      auto event = fixtureEvent(2);
      if (invalid == "replay")
        event.sample.sequence = 1;
      else if (invalid == "timestamp")
        event.sample.timestampNs = 99;
      else if (invalid == "malformed")
        event.transition = static_cast<OriginalKeyTransition>(255);
      else if (invalid == "binding")
        ++lease.deskflowPeerPin[0];
      else
        ++lease.routeGeneration;
      check(
          LocalInputChannelTestAccess::rawOriginal(*rejected.core, originalPayload(lease, event)),
          "Cannot queue invalid retired original fixture"
      );
    };
    check(rejected.core->commit(fixtureLease()), "Cannot start retired original validation");
    check(
        until([&] { return rejected.closed; }) && rejected.revoked == 1,
        "Malformed, replayed or unretired original escaped validation"
    );
    rejected.childExitedCleanly();
  }
}
void lossAndLimits()
{
  {
    Fixture fixture;
    fixture.activate();
    fixture.child.kill();
    check(until([&] { return fixture.closed; }) && fixture.revoked == 1, "Child death did not release lease owner");
    fixture.core->close();
    check(fixture.revoked == 1, "Owner release repeated after disconnect");
  }
  {
    Fixture fixture("freeze");
    fixture.activate();
    check(until([&] { return fixture.closed; }) && fixture.revoked == 1, "Heartbeat loss did not release lease owner");
  }
  for (bool output : {false, true}) {
    Fixture fixture;
    fixture.activate();
    if (output) {
      LocalInputChannelTestAccess::exhaustOutput(*fixture.core);
      check(!fixture.core->sendOriginal(fixtureLease(), fixtureEvent(1)), "Output sequence wrapped");
    } else
      LocalInputChannelTestAccess::exhaustInput(*fixture.core);
    check(until([&] { return fixture.closed; }) && fixture.revoked == 1, "Sequence exhaustion did not close lease");
  }
  {
    Fixture fixture("freeze");
    fixture.activate();
    qint64 peak = 0;
    std::uint64_t sequence = 0;
    QTimer flood;
    flood.setInterval(1);
    QObject::connect(&flood, &QTimer::timeout, [&] {
      for (int count = 0; count < 40 && !fixture.closed; ++count) {
        ++sequence;
        peak = std::max(peak, LocalInputChannelTestAccess::queued(*fixture.core));
        fixture.core->sendOriginal(
            fixtureLease(),
            fixtureEvent(sequence, sequence == 1 ? OriginalKeyTransition::Down : OriginalKeyTransition::Repeat)
        );
      }
    });
    flood.start();
    check(until([&] { return fixture.closed; }), "Stalled reader did not close");
    flood.stop();
    check(
        peak > LocalInputChannel::maxQueueBytes / 2 && peak <= LocalInputChannel::maxQueueBytes &&
            fixture.reason == "Local input output limit exceeded" && fixture.revoked == 1,
        "Stalled reader queue was not bounded before owner release"
    );
  }
}
void descriptorAndIdentityGuards()
{
  check(
      currentLocalInputProcess().has_value() && !localInputProcess(0) && !localInputProcess(-1),
      "Invalid process birth identity accepted"
  );
#ifdef Q_OS_UNIX
  int pipeDescriptors[2];
  check(::pipe(pipeDescriptors) == 0, "Cannot create non-socket descriptor fixture");
  QProcess child;
  child.start(QCoreApplication::applicationFilePath(), {"--idle"});
  check(child.waitForStarted(2000), "Cannot start descriptor fixture peer");
  const auto identity = localInputProcess(child.processId());
  check(identity.has_value(), "Cannot read descriptor fixture identity");
  bool rejected = false;
  try {
    LocalInputChannel::adoptHost(pipeDescriptors[0], *identity, limits);
  } catch (...) {
    rejected = true;
  }
  ::close(pipeDescriptors[1]);
  check(rejected, "Non-socket descriptor was accepted");
  auto pair = LocalInputSocketPair::create();
  auto wrong = *identity;
  ++wrong.startStamp;
  rejected = false;
  try {
    pair.adoptCore(wrong, limits);
  } catch (...) {
    rejected = true;
  }
  check(rejected, "Wrong process birth stamp was accepted");
  child.kill();
  child.waitForFinished(2000);
#endif
}
void expectDescriptorClosedAfterExec(int descriptor)
{
  QProcess probe;
  probe.start(QCoreApplication::applicationFilePath(), {"--expect-closed", QString::number(descriptor)});
  check(probe.waitForStarted(2000) && probe.waitForFinished(2000), "Cannot run descriptor inheritance probe");
  check(
      probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0,
      "Unrelated executable inherited the private descriptor"
  );
}
void exclusiveInheritance()
{
  auto pair = LocalInputSocketPair::create();
  expectDescriptorClosedAfterExec(pair.childDescriptor());
  QProcess child;
  pair.configureChild(child);
  child.start(QCoreApplication::applicationFilePath(), {"--inheritance", QString::number(pair.childDescriptor())});
  check(child.waitForStarted(2000) && child.waitForFinished(3000), "Cannot run designated-child inheritance probe");
  check(
      child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
      "Adopted capability leaked to a descendant executable"
  );
}
} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_UNIX
  // Inspect before Qt can open another file at the just-closed descriptor number.
  if (argc == 3 && std::strcmp(argv[1], "--expect-closed") == 0) {
    char *end = nullptr;
    const auto descriptor = std::strtol(argv[2], &end, 10);
    if (!end || *end || descriptor < 3 || descriptor > std::numeric_limits<int>::max())
      return 1;
    return fcntl(static_cast<int>(descriptor), F_GETFD) == -1 && errno == EBADF ? 0 : 1;
  }
#endif
  QCoreApplication application(argc, argv);
  try {
    const auto arguments = application.arguments();
#ifdef Q_OS_UNIX
    if (arguments.size() == 3 && arguments[1] == "--inheritance") {
      bool valid = false;
      const auto descriptor = arguments[2].toInt(&valid);
      const auto expectedCore = localInputProcess(getppid());
      check(valid && descriptor >= 3 && expectedCore.has_value(), "Invalid designated-child inheritance probe");
      auto host = LocalInputChannel::adoptHost(descriptor, *expectedCore, limits);
      expectDescriptorClosedAfterExec(descriptor);
      host->close();
      return 0;
    }
#endif
    if (arguments.size() == 4 && arguments[1] == "--child") {
      bool valid = false;
      const auto descriptor = arguments[2].toInt(&valid);
      check(valid && descriptor >= 3, "Invalid fixture descriptor");
      return childMain(application, descriptor, arguments[3]);
    }
    if (arguments.size() == 2 && arguments[1] == "--idle") {
      QTimer::singleShot(5000, &application, &QCoreApplication::quit);
      return application.exec();
    }
    check(arguments.size() == 1, "Unexpected local input test argument");
    descriptorAndIdentityGuards();
    exclusiveInheritance();
    roundtrip();
    authoritativeCommit();
    malformedBootstrap();
    changedBindings();
    expiryAtUse();
    reentrantAuthorization();
    destructiveNotifications();
    cancelledLeaseReuse();
    retiredOriginals();
    lossAndLimits();
    QTextStream(
        stdout
    ) << "PASS: inherited IPC, process/role/incarnation binding, authoritative leases, "
         "original-event framing, stale/replayed/malformed rejection, bounded queues and owner revocation\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
