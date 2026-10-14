/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "InputEvidence.h"
#include "LocalInputChannel.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTextStream>
#include <QThread>
#include <algorithm>
#include <bit>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>
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
  static std::uint64_t received(const LocalInputChannel &channel)
  {
    return channel.m_received;
  }
  static qint64 buffered(const LocalInputChannel &channel)
  {
    return channel.m_socket.bytesAvailable();
  }
  static bool incomingEmpty(const LocalInputChannel &channel)
  {
    return channel.m_incoming.isEmpty();
  }
  static void observeReadyRead(LocalInputChannel &channel, const std::function<void()> &callback)
  {
    QObject::connect(&channel.m_socket, &QLocalSocket::readyRead, &channel, callback, Qt::DirectConnection);
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
  static bool raw(LocalInputChannel &channel, unsigned type, const QByteArray &payload)
  {
    return channel.send(static_cast<LocalInputChannel::Type>(type), payload);
  }
  static void stopTimer(LocalInputChannel &channel)
  {
    channel.m_timer.stop();
  }
  static int handshake(const LocalInputChannel &channel)
  {
    return channel.m_handshake;
  }
  static QString handshakeDiagnostics(const LocalInputChannel &channel)
  {
    return QStringLiteral(
               "stage=%1 authenticated=%2 closed=%3 handshakeElapsedMs=%4 handshakeBudgetMs=%5 "
               "lastReadElapsedMs=%6 heartbeatBudgetMs=%7 socketState=%8 sent=%9 received=%10 "
               "socketBufferedBytes=%11 frameBufferedBytes=%12 outputBufferedBytes=%13"
    )
        .arg(channel.m_handshake)
        .arg(channel.m_authenticated)
        .arg(channel.m_closed)
        .arg(channel.m_handshakeAge.elapsed())
        .arg(channel.m_limits.handshakeMs)
        .arg(channel.m_lastRead.elapsed())
        .arg(channel.m_limits.timeoutMs)
        .arg(int(channel.m_socket.state()))
        .arg(channel.m_sent)
        .arg(channel.m_received)
        .arg(channel.m_socket.bytesAvailable())
        .arg(channel.m_incoming.size())
        .arg(channel.m_socket.bytesToWrite());
  }
  static void read(LocalInputChannel &channel)
  {
    channel.read();
  }
  static void ready(LocalInputChannel &channel)
  {
    QByteArray payload;
    for (int index = 7; index >= 0; --index)
      payload.append(static_cast<char>(channel.m_capabilities >> (index * 8)));
    channel.bootstrap(static_cast<LocalInputChannel::Type>(3), payload);
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
  static std::uint64_t captureEpoch(const LocalInputChannel &channel)
  {
    return channel.m_captureEpoch;
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
constexpr LocalInputLimits limits{20, 400, 400};
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
  lease.coreServerIncarnation = 16;
  lease.coreRecipient = 17;
  lease.coreSelectionGeneration = 18;
  lease.focusRequest = 19;
  lease.keyboardRouteGeneration = 20;
  lease.captureEpoch = 21;
  lease.scope = ForwardingScope::All;
  return lease;
}
OriginalInputRecord fixtureEvent(std::uint64_t sequence, OriginalKeyTransition transition = OriginalKeyTransition::Down)
{
  OriginalInputRecord event;
  event.epoch = fixtureLease().captureEpoch;
  event.sequence = sequence;
  event.sourceTimestampNs = sequence * 100;
  event.payload = OriginalInputKey{
      40, 0, transition == OriginalKeyTransition::Up ? OriginalKeyKind::Up : OriginalKeyKind::Down, transition,
      transition == OriginalKeyTransition::Repeat
  };
  return event;
}
std::vector<OriginalInputRecord> mixedRecords()
{
  constexpr auto extraFlag = std::uint64_t{1} << 63;
  constexpr auto shifted = extraFlag | originalKey::shiftFlag | originalKey::leftShiftFlag;
  const auto key =
      OriginalInputKey{40, originalKey::leftShift, OriginalKeyKind::FlagsChanged, OriginalKeyTransition::Down, false};
  const auto up =
      OriginalInputKey{40, originalKey::leftShift, OriginalKeyKind::FlagsChanged, OriginalKeyTransition::Up, false};
  constexpr auto minimum = std::numeric_limits<std::int32_t>::min();
  constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
  return {
      {21, 100, 1000, shifted, key},
      {21, 110, 1001, shifted, OriginalInputMotion{minimum, maximum, 32}},
      {21, 120, 1002, shifted, OriginalInputButton{31, true, maximum}},
      {21, 130, 1003, shifted, OriginalInputMotion{1, -2, 31}},
      {21, 140, 1004, shifted,
       OriginalInputScroll{
           {minimum, maximum}, {-1, 0}, {65536, -65536}, OriginalScrollPhase::Began, OriginalMomentumPhase::None, true
       }},
      {21, 150, 1005, shifted, OriginalInputButton{31, false, 0}},
      {21, 160, 1006, extraFlag, up},
      {21, 170, 1007, extraFlag, OriginalInputBoundary::Checkpoint}
  };
}

void number(QByteArray &bytes, std::uint64_t value, int size)
{
  for (int i = size - 1; i >= 0; --i)
    bytes.append(static_cast<char>(value >> (i * 8)));
}
QByteArray offerPayload(const LocalInputOffer &offer)
{
  QByteArray bytes;
  auto append = [&](const auto &value) { bytes.append(reinterpret_cast<const char *>(value.data()), value.size()); };
  for (auto value :
       {offer.routeGeneration, offer.coreServerIncarnation, offer.coreRecipient, offer.coreSelectionGeneration,
        offer.captureEpoch})
    number(bytes, value, 8);
  append(offer.deskflowClientIncarnation);
  append(offer.deskflowPeerPin);
  append(offer.windowSharePeerPin);
  number(bytes, offer.scope == ForwardingScope::Keyboard ? 1 : 2, 8);
  return bytes;
}
QByteArray leasePayload(const LocalInputLease &lease)
{
  auto bytes = offerPayload(lease);
  bytes.append(
      reinterpret_cast<const char *>(lease.windowShareClientIncarnation.data()),
      lease.windowShareClientIncarnation.size()
  );
  for (auto value :
       {lease.session, lease.epoch, lease.focusRequest, lease.focusRevision, lease.geometryRevision,
        lease.keyboardRouteGeneration})
    number(bytes, value, 8);
  return bytes;
}
QByteArray originalPayload(const LocalInputLease &lease, const OriginalInputRecord &event)
{
  auto bytes = leasePayload(lease);
  for (auto value : {event.sequence, event.sourceTimestampNs, event.flags})
    number(bytes, value, 8);
  if (const auto *key = std::get_if<OriginalInputKey>(&event.payload)) {
    number(bytes, 1, 1);
    number(bytes, key->keyboardType, 4);
    number(bytes, key->code, 2);
    number(bytes, static_cast<unsigned>(key->kind), 1);
    number(bytes, key->repeat, 1);
    number(bytes, static_cast<unsigned>(key->transition), 1);
  } else if (const auto *motion = std::get_if<OriginalInputMotion>(&event.payload)) {
    number(bytes, 2, 1);
    number(bytes, static_cast<std::uint32_t>(motion->dx), 4);
    number(bytes, static_cast<std::uint32_t>(motion->dy), 4);
    number(bytes, motion->dragButton, 1);
  } else if (const auto *button = std::get_if<OriginalInputButton>(&event.payload)) {
    number(bytes, 3, 1);
    number(bytes, button->button, 1);
    number(bytes, button->down, 1);
    number(bytes, button->clickCount, 4);
  } else if (const auto *scroll = std::get_if<OriginalInputScroll>(&event.payload)) {
    number(bytes, 4, 1);
    for (const auto &axes : {scroll->lines, scroll->points, scroll->fixed16_16})
      for (auto value : axes)
        number(bytes, static_cast<std::uint32_t>(value), 4);
    number(bytes, static_cast<unsigned>(scroll->phase), 1);
    number(bytes, static_cast<unsigned>(scroll->momentum), 1);
    number(bytes, scroll->continuous, 1);
  } else {
    number(bytes, 5, 1);
    number(bytes, static_cast<unsigned>(std::get<OriginalInputBoundary>(event.payload)), 1);
  }
  return bytes;
}

struct Fixture
{
  QProcess child;
  std::unique_ptr<LocalInputChannel> core;
  bool closed = false, ready = false, prepared = false, active = false;
  int revoked = 0;
  QString reason;
  LocalInputLease expectedLease = fixtureLease();
  bool identityFailureDiagnostics = false;
  qint64 identityWaitElapsedMs = 0;

  explicit Fixture(const QString &mode = "passive", LocalInputLimits coreLimits = limits)
  {
    if (mode.startsWith("keyboard-"))
      expectedLease.scope = ForwardingScope::Keyboard;
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
    // These codec/callback fixtures inject frames directly into read(). They
    // consume sequence slots the real peer never sent, so neither side may
    // interleave periodic heartbeats with those synthetic frames.
    if (mode == "synthetic" || mode.startsWith("buffered-evidence-"))
      LocalInputChannelTestAccess::stopTimer(*core);
    core->authorize = [this](const LocalInputLease &lease, LocalInputCheck) { return lease == expectedLease; };
    core->onAuthenticated = [&] { ready = true; };
    core->onPrepared = [&](const LocalInputLease &lease) {
      check(lease == expectedLease, "Prepared acknowledgement changed lease");
      prepared = true;
    };
    core->onActivated = [&](const LocalInputLease &lease) {
      check(lease == expectedLease, "Commit acknowledgement changed lease");
      active = true;
    };
    core->onRevoked = [&](const LocalInputLease &lease, LocalInputRevocation) {
      check(lease == expectedLease, "Revocation changed the release owner");
      ++revoked;
    };
    core->onClosed = [&](QString why) {
      reason = why;
      closed = true;
    };
  }
  ~Fixture()
  {
    const auto childStateBeforeCleanup = child.state();
    bool cleanupKillRequested = false;
    if (identityFailureDiagnostics) {
      QTextStream(stderr) << "Identity authentication failure: waitElapsedMs=" << identityWaitElapsedMs
                          << " waitBudgetMs=4000 ready=" << ready << " closed=" << closed
                          << " closeReason=" << reason.left(4096) << '\n';
      if (core)
        QTextStream(stderr) << "Core channel: " << LocalInputChannelTestAccess::handshakeDiagnostics(*core) << '\n';
      QTextStream(stderr) << "Child before cleanup: pid=" << child.processId()
                          << " state=" << int(childStateBeforeCleanup) << " error=" << int(child.error())
                          << " errorString=" << child.errorString().left(4096);
      if (childStateBeforeCleanup == QProcess::NotRunning)
        QTextStream(stderr) << " exitStatus=" << int(child.exitStatus()) << " exitCode=" << child.exitCode();
      QTextStream(stderr) << '\n' << Qt::flush;
    }
    if (core) {
      core->onRevoked = {};
      core->onClosed = {};
      core->close();
      core.reset();
    }
    if (child.state() != QProcess::NotRunning) {
      cleanupKillRequested = true;
      child.kill();
      child.waitForFinished(2000);
    }
    if (identityFailureDiagnostics) {
      // These reads never wait. Preserve output after the existing cleanup
      // wait too, without extending its deadline or confusing our kill with
      // an independently observed child crash.
      QTextStream(stderr) << "Child after existing cleanup: state=" << int(child.state())
                          << " cleanupKillRequested=" << cleanupKillRequested;
      if (child.state() == QProcess::NotRunning)
        QTextStream(stderr) << " exitStatus=" << int(child.exitStatus()) << " exitCode=" << child.exitCode();
      QTextStream(stderr) << '\n';
      for (const auto channel : {QProcess::StandardOutput, QProcess::StandardError}) {
        child.setReadChannel(channel);
        const auto available = child.bytesAvailable();
        const auto output = child.read(16 * 1024);
        QTextStream(stderr) << (channel == QProcess::StandardOutput ? "Child stdout" : "Child stderr")
                            << " (available=" << available << " retained=" << output.size() << "):\n"
                            << QString::fromUtf8(output) << '\n'
                            << Qt::flush;
      }
    }
  }
  void requireAuthenticatedIdentity(const char *message)
  {
    QElapsedTimer elapsed;
    elapsed.start();
    const bool authenticated = until([&] { return ready; });
    identityWaitElapsedMs = elapsed.elapsed();
    identityFailureDiagnostics = !authenticated;
    check(authenticated, message);
  }
  void establish()
  {
    check(until([&] { return ready || closed; }) && ready && !closed, "Private bootstrap did not authenticate");
  }
  void prepare()
  {
    establish();
    check(!core->sendOriginal(expectedLease, fixtureEvent(1)), "Original event escaped before preparation");
    check(core->prepare(expectedLease), "Core could not prepare lease");
    check(until([&] { return prepared || closed; }) && prepared && !closed, "Host did not acknowledge prepare");
    check(!core->sendOriginal(expectedLease, fixtureEvent(1)), "Original event escaped before commit");
  }
  void activate()
  {
    prepare();
    check(core->commit(expectedLease), "Core could not commit lease");
    check(until([&] { return active || closed; }) && active && !closed, "Lease did not become active");
  }
  void childExitedCleanly()
  {
    check(until([&] { return child.state() == QProcess::NotRunning; }), "Fixture child failed to exit");
    if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0)
      throw std::runtime_error("Fixture child rejected invariant: " + child.readAllStandardError().toStdString());
  }
};

#ifdef Q_OS_UNIX
QByteArray readBytes(int descriptor, qsizetype size)
{
  QByteArray result;
  QElapsedTimer timer;
  timer.start();
  while (result.size() < size && timer.elapsed() < 2000) {
    pollfd poller{descriptor, POLLIN, 0};
    if (poll(&poller, 1, 50) <= 0)
      continue;
    char buffer[512];
    const auto count = ::read(descriptor, buffer, std::min(size - result.size(), qsizetype{sizeof(buffer)}));
    if (count <= 0)
      break;
    result.append(buffer, count);
  }
  check(result.size() == size, "Raw fixture did not receive complete frame");
  return result;
}
QByteArray
wireFrame(const QByteArray &hello, unsigned type, unsigned role, std::uint64_t sequence, const QByteArray &payload = {})
{
  QByteArray header;
  number(header, 0x444c4931, 4);
  number(header, LocalInputChannel::version, 2);
  number(header, type, 1);
  number(header, role, 1);
  number(header, payload.size(), 4);
  number(header, sequence, 8);
  header += hello.mid(20, 32); // Authenticated process incarnations, kept opaque in diagnostics.
  return header + payload;
}
int wireChild(int descriptor)
{
  const auto hello = readBytes(descriptor, 84);
  check(hello == wireFrame(hello, 1, 1, 1, hello.mid(52)), "Unexpected protocol v4 bootstrap layout");
  const auto write = [&](unsigned type, std::uint64_t sequence, const QByteArray &payload = {}) {
    const auto frame = wireFrame(hello, type, 2, sequence, payload);
    check(::write(descriptor, frame.constData(), frame.size()) == frame.size(), "Raw fixture write failed");
  };
  const auto expect = [&](unsigned type, std::uint64_t sequence, const QByteArray &payload = {}) {
    const auto expected = wireFrame(hello, type, 1, sequence, payload);
    check(readBytes(descriptor, expected.size()) == expected, "Protocol v4 wire bytes changed");
  };
  const auto lease = fixtureLease();
  const auto offer = offerPayload(lease), binding = leasePayload(lease);
  write(2, 1, hello.mid(52));
  expect(3, 2, hello.last(8));
  expect(12, 3, offer);
  write(13, 2, binding);
  write(14, 3, binding);
  expect(15, 4, offer + char(LocalInputRevocation::Requested));
  write(15, 4, offer + char(LocalInputRevocation::AuthorityLost));
  expect(6, 5, binding);
  write(7, 5, binding);
  expect(8, 6, binding);
  write(9, 6, binding);
  expect(11, 7, originalPayload(lease, fixtureEvent(1)));
  std::uint64_t sequence = 8;
  for (const auto &record : mixedRecords())
    expect(11, sequence++, originalPayload(lease, record));
  write(10, 7, binding + char(LocalInputRevocation::Requested));
  pollfd poller{descriptor, POLLIN, 0};
  char byte;
  check(poll(&poller, 1, 2000) > 0 && ::read(descriptor, &byte, 1) == 0, "Wire fixture did not receive clean EOF");
  return 0;
}
int rawChild(QCoreApplication &application, int descriptor, const QString &mode)
{
  auto frame = readBytes(descriptor, 84);
  frame[6] = 2;
  frame[7] = 2;
  if (mode == "raw-magic")
    frame[0] = 'X';
  else if (mode == "raw-version")
    frame[5] = 3; // A v3 lease has no scope/capability evidence and must not negotiate.
  else if (mode == "raw-role")
    frame[7] = 1;
  else if (mode == "raw-incarnation")
    frame[20] = static_cast<char>(frame[20] ^ 1);
  else if (mode == "raw-zero-incarnation")
    frame.replace(20, 16, QByteArray(16, '\0'));
  else if (mode == "raw-process")
    frame[75] = static_cast<char>(frame[75] ^ 1);
  else if (mode.startsWith("raw-capabilities-")) {
    frame.replace(76, 8, QByteArray(8, '\0'));
    frame[83] = mode.endsWith("unknown") ? 7 : mode.endsWith("no-keyboard") ? 2 : 0;
  } else if (mode == "raw-sequence")
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
    frame[11] = 33;
    frame.append('x');
  } else if (mode == "raw-unknown")
    frame[6] = static_cast<char>(255);
  else if (mode == "raw-replay") {
    auto ping = frame.left(52);
    ping[6] = 4;
    ping.replace(8, 4, QByteArray(4, '\0'));
    frame += ping; // Reuses sequence 1 after the valid HostHello.
  } else if (mode == "raw-slow-hello")
    QThread::msleep(600);
  else if (mode != "raw-silent")
    check(false, "Unknown raw fixture mode");
  if (mode != "raw-silent")
    check(::write(descriptor, frame.constData(), frame.size()) == frame.size(), "Raw fixture write failed");
  QTimer::singleShot(2500, &application, &QCoreApplication::quit);
  return application.exec();
}

int delayedReadyChild(QCoreApplication &application, int descriptor, LocalInputProcess core, const QString &mode)
{
  // The core replies promptly; delay servicing its Ready while keeping the
  // endpoint/process alive. Cover both sides of the absolute bootstrap bound.
  const bool withinBudget = mode == "slow-ready";
  auto host = LocalInputChannel::adoptHost(
      descriptor, core, withinBudget ? LocalInputLimits{20, 200, 1600} : LocalInputLimits{20, 2000, 400}
  );
  LocalInputChannelTestAccess::freeze(*host);
  bool authenticated = false, closed = false;
  QString reason;
  host->onAuthenticated = [&] { authenticated = host->authenticated(); };
  host->onClosed = [&](QString value) {
    reason = value;
    closed = true;
  };
  QTimer::singleShot(100, &application, [&] {
    try {
      LocalInputChannelTestAccess::read(*host);
      check(LocalInputChannelTestAccess::handshake(*host) == 1, "Late Ready fixture did not accept CoreHello");
      LocalInputChannelTestAccess::flush(*host);
      // CoreHello restarted lastRead above, but must not restart handshakeAge.
      QThread::msleep(350);
      if (mode != "late-ready")
        LocalInputChannelTestAccess::ready(*host);
      else
        LocalInputChannelTestAccess::read(*host);
      if (withinBudget)
        check(
            !closed && authenticated && host->authenticated(),
            "Host transition callback inherited the older bootstrap read age"
        );
      else
        check(
            closed && !authenticated && !host->authenticated() && reason.contains("stage=ready") &&
                reason.contains("elapsed="),
            "Expired Ready revived bootstrap or intermediate traffic extended its absolute age"
        );
      application.quit();
    } catch (const std::exception &error) {
      QTextStream(stderr) << "FAIL: " << error.what() << '\n';
      application.exit(1);
    }
  });
  const auto result = application.exec();
  host->onClosed = {};
  host->close();
  return result;
}

constexpr std::uint64_t bufferedEvidenceFrames = 96;
int bufferedEvidenceChild(QCoreApplication &application, int descriptor, LocalInputProcess core, bool recording)
{
  auto evidence = std::make_unique<InputEvidenceRecorder>(InputEvidenceRole::Host);
  check(inputEvidenceInstalled.exchange(evidence.get()) == nullptr, "Fixture recorder was already installed");
  struct Uninstall
  {
    ~Uninstall()
    {
      inputEvidenceInstalled.store(nullptr);
    }
  } uninstall;
  auto host = LocalInputChannel::adoptHost(descriptor, core, {1000, 10000, 2000});
  LocalInputChannelTestAccess::stopTimer(*host);
  bool active = false, announced = false, observed = false, finished = false;
  std::uint64_t firstSequence = 0;
  const auto checked = [&](const std::function<void()> &action) {
    try {
      action();
    } catch (const std::exception &error) {
      QTextStream(stderr) << "FAIL: " << error.what() << '\n';
      application.exit(1);
    }
  };
  host->authorize = [](const LocalInputLease &lease, LocalInputCheck) { return lease == fixtureLease(); };
  host->onPrepare = [&](const LocalInputLease &lease) {
    check(host->acknowledgePrepare(lease), "Buffered evidence Host prepare failed");
  };
  host->onActivated = [&](const LocalInputLease &) { active = true; };
  host->onClosed = [&](QString) {
    if (!finished) {
      QTextStream(stderr) << "FAIL: Buffered evidence channel closed before its burst was consumed\n";
      application.exit(1);
    }
  };
  LocalInputChannelTestAccess::observeReadyRead(*host, [&] {
    if (!announced || observed)
      return;
    checked([&] {
      // This is the real QLocalSocket signal, after its direct byte observer
      // and before LocalInputChannel's queued read() dispatch. No frame has
      // entered m_incoming or receive(), so only the buffered queue can block
      // an otherwise ready recorder here.
      check(
          LocalInputChannelTestAccess::buffered(*host) > 0 && LocalInputChannelTestAccess::incomingEmpty(*host) &&
              LocalInputChannelTestAccess::received(*host) == firstSequence,
          "Buffered evidence fixture did not reach the pre-dispatch socket boundary"
      );
      if (!recording) {
        check(!evidence->readyOwner(), "Qt-buffered input was invisible to recorder readiness");
        check(
            !evidence->arm(5000000000) && evidence->state() == InputEvidenceState::Denied &&
                evidence->loss() == InputEvidenceLoss::Baseline,
            "Recorder armed with private socket input waiting for queued dispatch"
        );
      }
      observed = true;
    });
  });
  QTimer poll;
  poll.setInterval(1);
  QObject::connect(&poll, &QTimer::timeout, &application, [&] {
    checked([&] {
      if (!announced) {
        if (!active || !evidence->readyOwner())
          return;
        check(evidence->loss() == InputEvidenceLoss::None, "Private bootstrap lost recorder accounting");
        if (recording)
          check(evidence->arm(5000000000), "Empty authenticated Host recorder could not arm");
        firstSequence = LocalInputChannelTestAccess::received(*host);
        announced = true;
        QTextStream(stdout) << "BUFFERED_EVIDENCE_READY\n" << Qt::flush;
        return;
      }
      if (LocalInputChannelTestAccess::received(*host) != firstSequence + bufferedEvidenceFrames)
        return;
      check(observed, "Real socket burst skipped the direct readyRead observation");
      check(
          LocalInputChannelTestAccess::buffered(*host) == 0 && LocalInputChannelTestAccess::incomingEmpty(*host),
          "Private input remained buffered after the complete burst"
      );
      if (recording) {
        evidence->stop();
        evidence->finish();
        check(
            evidence->state() == InputEvidenceState::Stopped && evidence->loss() == InputEvidenceLoss::None,
            "Complete private burst did not reach an accounted recorder boundary"
        );
        std::uint64_t enqueued = 0, retired = 0, depth = 0, peak = 0;
        while (const auto row = evidence->pop()) {
          if (row->kind != InputEvidenceKind::Queue || row->queue != InputEvidenceQueue::PrivateBufferedInput)
            continue;
          check(
              row->capacity == 4 * (LocalInputChannel::headerBytes + LocalInputChannel::maxPayloadBytes) &&
                  row->depth <= row->capacity && row->enqueued >= enqueued && row->retired >= retired,
              "Private buffered byte counters or capacity changed"
          );
          enqueued = row->enqueued;
          retired = row->retired;
          depth = row->depth;
          peak = std::max(peak, depth);
        }
        check(
            peak > 0 && enqueued == bufferedEvidenceFrames * LocalInputChannel::headerBytes && retired == enqueued &&
                depth == 0,
            "Private buffered input was omitted, double counted, or left pending after a >64-frame burst"
        );
      }
      finished = true;
      application.quit();
    });
  });
  poll.start();
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &application, [&] {
    QTextStream(stderr) << "FAIL: Buffered evidence fixture exceeded its absolute timeout\n";
    application.exit(1);
  });
  timeout.start(3000);
  const auto result = application.exec();
  host->onClosed = {};
  host->close();
  return result;
}
#endif

int childMain(QCoreApplication &application, int descriptor, const QString &mode)
{
#ifdef Q_OS_UNIX
  if (mode.startsWith("raw-"))
    return rawChild(application, descriptor, mode);
  if (mode == "wire-v4")
    return wireChild(descriptor);
  const auto expectedCore = localInputProcess(getppid());
  check(expectedCore.has_value(), "Controlled fixture launcher is unavailable");
  if (mode.startsWith("buffered-evidence-"))
    return bufferedEvidenceChild(application, descriptor, *expectedCore, mode == "buffered-evidence-recording");
  if (mode == "late-ready" || mode == "late-ready-transition" || mode == "slow-ready")
    return delayedReadyChild(application, descriptor, *expectedCore, mode);
  auto hostLimits = mode == "patient" ? LocalInputLimits{20, 2000, 2000} : limits;
  if (mode == "keyboard-only")
    hostLimits.capabilities = localInputKeyboardCapability;
  auto host = LocalInputChannel::adoptHost(descriptor, *expectedCore, hostLimits);
  check((fcntl(descriptor, F_GETFD) & FD_CLOEXEC) != 0, "Adopted child capability is inheritable");
  if (mode == "synthetic")
    LocalInputChannelTestAccess::stopTimer(*host);
  std::uint64_t originalEvents = 0;
  int callbackEvents = 0;
  bool retiredDelivered = false;
  host->authorize = [&, marker = QString(256, 'a')](const LocalInputLease &lease, LocalInputCheck phase) {
    if (phase == LocalInputCheck::Deliver && mode == "mapped-expire")
      QThread::msleep(50);
    if (phase == LocalInputCheck::Deliver && mode == "nested-capture") {
      auto retired = fixtureLease();
      --retired.routeGeneration;
      auto newer = fixtureEvent(2);
      newer.payload = OriginalInputMotion{-9, 11, 32};
      LocalInputChannelTestAccess::incoming(*host, 11, originalPayload(retired, newer));
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
    if (mode.startsWith("keyboard-")) {
      if (lease.scope != ForwardingScope::Keyboard)
        return false;
      binding.scope = ForwardingScope::All;
    }
    if (mode == "reuse" || mode == "revoke-prepared" || mode.startsWith("retired-") || mode == "epochs")
      binding.routeGeneration = fixtureLease().routeGeneration;
    if (mode == "epochs")
      binding.captureEpoch = fixtureLease().captureEpoch;
    return binding == fixtureLease() && !(mode == "deny-commit" && phase == LocalInputCheck::Commit);
  };
  host->onPrepare = [&, marker = QString(256, 'p')](const LocalInputLease &lease) {
    if (mode == "epochs" && lease.captureEpoch == 21 && lease.routeGeneration == 16)
      check(
          LocalInputChannelTestAccess::captureAt(*host, 100, 10000),
          "Same-epoch host preparation reset capture replay state"
      );
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
  host->onMappedOriginal = [&](const LocalInputLease &lease, const OriginalInputRecord &record,
                               const LocalInputMappedPoint &point) {
    ++originalEvents;
    const auto records = mixedRecords();
    check(
        mode == "mapped" && lease == fixtureLease() && originalEvents <= records.size() &&
            originalPayload(lease, record) == originalPayload(lease, records[originalEvents - 1]) && point.x == 17.25 &&
            point.y == 33.5 && point.width == 640 && point.height == 480 && liveLocalInputMappedPoint(point),
        "Mapped private input changed its record, position, geometry or deadline"
    );
    if (originalEvents == records.size()) {
      host->revoke();
      check(host->sendReleased(lease), "Exact mapped owner release could not be acknowledged");
    }
  };
  host->onOriginal = [&, marker = QString(256, 'o')](const LocalInputLease &lease, const OriginalInputRecord &event) {
    ++originalEvents;
    if (mode.startsWith("keyboard-")) {
      check(
          lease.scope == ForwardingScope::Keyboard && originalEvents == 1 &&
              std::holds_alternative<OriginalInputKey>(event.payload),
          "Keyboard-only lease delivered pointer input or changed scope"
      );
      host->revoke();
      return;
    }
    if (mode == "mixed") {
      const auto records = mixedRecords();
      check(
          lease == fixtureLease() && originalEvents <= records.size() && event.epoch == lease.captureEpoch &&
              originalPayload(lease, event) == originalPayload(lease, records[originalEvents - 1]),
          "Mixed input type, signed value, flags, epoch or order changed"
      );
      if (originalEvents == records.size())
        host->revoke();
      return;
    }
    if (mode == "epochs") {
      check(
          originalEvents <= 3 && event.epoch == lease.captureEpoch &&
              event.sequence == (originalEvents == 3 ? 1 : 90 + originalEvents * 10) &&
              event.epoch == (originalEvents == 3 ? 22 : 21),
          "Capture epoch or retained ordering changed"
      );
      host->revoke();
      return;
    }
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
          lease == next && originalEvents == 1 && event.sequence == 3,
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
        lease == fixtureLease() && std::get<OriginalInputKey>(event.payload).keyboardType == 40 &&
            event.sequence == originalEvents,
        "Original event envelope changed"
    );
    check(
        std::get<OriginalInputKey>(event.payload).transition ==
            (originalEvents == 1 ? OriginalKeyTransition::Down : OriginalKeyTransition::Up),
        "Original transition changed"
    );
    if (originalEvents == 2)
      host->revoke();
  };
  host->onClosed = [&](QString) { application.quit(); };
  if (mode == "preflight" || mode.startsWith("offer-callback-")) {
    host->authorize = {};
    host->onOffer = [&, marker = QString(256, 'f')](const LocalInputOffer &offer) {
      ++callbackEvents;
      check(offer == leaseOffer(fixtureLease()), "Preflight offer changed its selection binding");
      check(host->phase() == LocalInputPhase::Idle && !host->lease(), "Offer acquired lease authority");
      check(!host->offer(offer), "Host sent a core offer");
      check(
          !host->prepare(fixtureLease()) && !host->commit(fixtureLease()) &&
              !host->sendOriginal(fixtureLease(), fixtureEvent(1)),
          "Preflight granted host original-input permission"
      );
      if (mode == "offer-callback-delete")
        host.reset();
      else if (mode == "offer-callback-close")
        host->close();
      else if (mode == "offer-callback-throw")
        throw std::runtime_error("Preflight notification fixture failure");
      else {
        if (mode == "offer-callback-revoke")
          host->revoke();
        check(host->candidate(fixtureLease()) && host->quiesced(fixtureLease()), "Host preflight reply failed");
      }
      check(marker.at(callbackEvents % marker.size()) == 'f', "Preflight callback capture did not survive");
    };
    host->onOfferCancelled = [&](const LocalInputOffer &offer, LocalInputRevocation reason) {
      ++callbackEvents;
      check(
          offer == leaseOffer(fixtureLease()) && reason == LocalInputRevocation::Requested,
          "Preflight cancellation changed binding or reason"
      );
      check(host->cancelOffer(offer, LocalInputRevocation::AuthorityLost), "Host preflight cancellation failed");
    };
  }
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
  if (mode == "mapped-expire" || mode == "mapped-malformed")
    check(originalEvents == 0, "Invalid or expired mapping reached delivery");
  if (mode == "mapped")
    check(originalEvents == mixedRecords().size(), "Mapped input did not deliver every input family exactly once");
  if (mode == "retired-original")
    check(retiredDelivered && originalEvents == 1, "Retired input prevented the next owner's valid delivery");
  else if (mode.startsWith("retired-"))
    check(originalEvents == 0, "Invalid retired input was delivered");
  if (mode == "revoke-prepared")
    check(retiredDelivered, "Host cancellation prevented the next route from activating");
  if (mode == "preflight")
    check(callbackEvents == 2 && originalEvents == 0, "Preflight did not remain an unauthorizing exchange");
  if (mode.startsWith("offer-callback-"))
    check(callbackEvents == 1 && originalEvents == 0, "Offer callback did not run exactly once without input");
  return status;
#else
  Q_UNUSED(application);
  Q_UNUSED(descriptor);
  Q_UNUSED(mode);
  return 1;
#endif
}

void preflightValues()
{
  static_assert(LocalInputChannel::version == 5);
  static_assert(LocalInputChannel::headerBytes == 52 && LocalInputChannel::maxPayloadBytes == 320);
  static_assert(LocalInputChannel::offerPayloadBytes == 128 && LocalInputChannel::leasePayloadBytes == 192);
  static_assert(LocalInputChannel::maxOriginalPayloadBytes == 244);
  const auto original = fixtureLease();
  const auto offer = leaseOffer(original);
  check(validLocalInputOffer(offer) && validLocalInputLease(original), "Invalid version 4 fixture");
  check(
      offerPayload(offer).size() == 128 && leasePayload(original).size() == 192 &&
          originalPayload(original, fixtureEvent(1)).size() == 226,
      "Version 4 golden payload sizes changed"
  );
  const std::array suffixes{
      "01000000280038030001",
      "02800000007fffffff20",
      "031f017fffffff",
      "0200000001fffffffe1f",
      "04800000007fffffffffffffff0000000000010000ffff0000010001",
      "031f0000000000", // Native drag release preserves zero clicks on the wire.
      "01000000280038030002",
      "0500"
  };
  const auto records = mixedRecords();
  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto payload = originalPayload(original, records[i]);
    check(
        validOriginalInputRecord(records[i]) && payload.size() <= LocalInputChannel::maxOriginalPayloadBytes &&
            payload.mid(216) == QByteArray::fromHex(suffixes[i]),
        "Mixed original golden record bytes changed"
    );
  }
  auto countedUp = records[5];
  std::get<OriginalInputButton>(countedUp.payload).clickCount = 3;
  check(
      validOriginalInputRecord(countedUp) &&
          originalPayload(original, countedUp).mid(216) == QByteArray::fromHex("031f0000000003"),
      "Positive-count release wire representation remains unchanged"
  );
  check(originalPayload(original, records[4]).size() == 244, "Scroll payload exceeded its fixed bounded layout");
  check(
      originalPayload(original, records[0]).mid(192, 24) ==
          QByteArray::fromHex("000000000000006400000000000003e88000000000020002"),
      "Original capture sequence/time/flags byte order changed"
  );
  for (const auto member :
       {&LocalInputOffer::routeGeneration, &LocalInputOffer::coreServerIncarnation, &LocalInputOffer::coreRecipient,
        &LocalInputOffer::coreSelectionGeneration, &LocalInputOffer::captureEpoch}) {
    auto lease = original;
    lease.*member = std::numeric_limits<std::uint64_t>::max();
    check(validLocalInputOffer(lease) && validLocalInputLease(lease), "A core token was restricted to JSON precision");
    lease.*member = 0;
    check(!validLocalInputOffer(lease) && !validLocalInputLease(lease), "A zero core token was accepted");
  }
  for (const auto member :
       {&LocalInputLease::session, &LocalInputLease::epoch, &LocalInputLease::focusRequest,
        &LocalInputLease::focusRevision, &LocalInputLease::geometryRevision,
        &LocalInputLease::keyboardRouteGeneration}) {
    auto lease = original;
    lease.*member = (std::uint64_t{1} << 53) - 1;
    check(validLocalInputLease(lease) && leaseOffer(lease) == offer, "Valid host counter changed core offer");
    ++(lease.*member);
    check(!validLocalInputLease(lease), "Host counter exceeded JSON precision");
    lease.*member = 0;
    check(!validLocalInputLease(lease), "A zero host counter was accepted");
  }
  for (int field = 0; field < 4; ++field) {
    auto lease = original;
    if (field == 0)
      lease.deskflowClientIncarnation = {};
    else if (field == 1)
      lease.deskflowPeerPin = {};
    else if (field == 2)
      lease.windowSharePeerPin = {};
    else
      lease.windowShareClientIncarnation = {};
    check(!validLocalInputLease(lease), "An empty preflight identity was accepted");
    check(validLocalInputOffer(lease) == (field == 3), "Offer validity depended on host-only fields");
  }
}
void mappedInput()
{
  static_assert(LocalInputChannel::maxMappedOriginalPayloadBytes == 284);
  LocalInputTarget target{123, {}};
  check(!validLocalInputTarget(target), "An empty endpoint pin was accepted");
  target.viewerPeerFingerprint[0] = 9;
  check(validLocalInputTarget(target), "A valid endpoint target was rejected");
  auto point = LocalInputMappedPoint{17.25, 33.5, 640, 480, localInputMonotonicNs() + 200000000};
  check(liveLocalInputMappedPoint(point), "Valid mapped point was rejected");
  for (const int fault : {0, 1, 2, 3, 4, 5}) {
    auto invalid = point;
    if (fault == 0)
      invalid.x = std::numeric_limits<double>::quiet_NaN();
    if (fault == 1)
      invalid.y = -1;
    if (fault == 2)
      invalid.x = invalid.width;
    if (fault == 3)
      invalid.height = 0;
    if (fault == 4)
      invalid.deadlineNs = localInputMonotonicNs() - 1;
    if (fault == 5)
      invalid.deadlineNs = localInputMonotonicNs() + 1000000000;
    check(!liveLocalInputMappedPoint(invalid), "Invalid point or extended deadline was accepted");
  }
  {
    Fixture fixture("mapped");
    fixture.activate();
    check(fixture.core->supportsMappedInput(), "Mapped capability was not negotiated");
    check(!fixture.core->sendInputTarget(fixtureLease(), target), "Core impersonated the Viewer target attestation");
    check(!fixture.core->sendReleased(fixtureLease()), "Core impersonated Host cleanup completion");
    bool released = false;
    fixture.core->onReleased = [&](const LocalInputOffer &offer) {
      check(
          offer == static_cast<const LocalInputOffer &>(fixtureLease()) && fixture.revoked == 1 &&
              fixture.core->phase() == LocalInputPhase::Idle,
          "Release changed the retained offer or overtook revocation"
      );
      released = true;
    };
    for (const auto &record : mixedRecords()) {
      point.deadlineNs = localInputMonotonicNs() + 200000000;
      check(fixture.core->sendMappedOriginal(fixtureLease(), record, point), "Cannot send mapped original");
    }
    check(
        until([&] { return fixture.revoked || fixture.closed; }) && fixture.revoked == 1 && !fixture.closed,
        "Mapped receiver did not revoke the exact owner after all records"
    );
    check(
        until([&] { return released || fixture.closed; }) && released && !fixture.closed,
        "Mapped release completion did not reach the Core"
    );
    fixture.core->close();
    fixture.childExitedCleanly();
  }
  for (const bool expireDuringAuthorization : {false, true}) {
    Fixture fixture(expireDuringAuthorization ? "mapped-expire" : "mapped-malformed");
    fixture.activate();
    point.deadlineNs = localInputMonotonicNs() + 20000000;
    if (expireDuringAuthorization)
      check(fixture.core->sendMappedOriginal(fixtureLease(), fixtureEvent(1), point), "Cannot queue expiry fixture");
    else {
      // Bypass sender validation to exercise the actual receiver decoder.
      auto payload = originalPayload(fixtureLease(), fixtureEvent(1));
      QByteArray mapping;
      for (const double value : {point.width, point.y, point.width, point.height})
        number(mapping, std::bit_cast<std::uint64_t>(value), 8);
      number(mapping, point.deadlineNs, 8);
      payload.insert(LocalInputChannel::leasePayloadBytes, mapping);
      check(LocalInputChannelTestAccess::raw(*fixture.core, 17, payload), "Cannot queue malformed mapped fixture");
    }
    check(
        until([&] { return fixture.closed; }) && fixture.revoked == 1,
        "Expired or malformed mapped input did not close and release its exact lease"
    );
    fixture.childExitedCleanly();
  }
}
void preflightRoundtrip()
{
  Fixture fixture("preflight");
  fixture.core->authorize = {};
  int candidates = 0, quiesced = 0, cancelled = 0;
  fixture.core->onCandidate = [&](const LocalInputLease &lease) {
    check(lease == fixtureLease() && quiesced == 0, "Candidate binding or wire order changed");
    ++candidates;
  };
  fixture.core->onQuiesced = [&](const LocalInputLease &lease) {
    check(lease == fixtureLease() && candidates == 1, "Quiesced binding or wire order changed");
    ++quiesced;
  };
  fixture.core->onOfferCancelled = [&](const LocalInputOffer &offer, LocalInputRevocation reason) {
    check(
        offer == leaseOffer(fixtureLease()) && reason == LocalInputRevocation::AuthorityLost,
        "Returned preflight cancellation changed"
    );
    ++cancelled;
  };
  const auto offer = leaseOffer(fixtureLease());
  check(!fixture.core->offer(offer), "Offer was sent before private authentication");
  fixture.establish();
  check(!fixture.core->candidate(fixtureLease()) && !fixture.core->quiesced(fixtureLease()), "Core sent a host reply");
  check(
      !fixture.core->offer({}) && !fixture.core->cancelOffer({}) &&
          !fixture.core->cancelOffer(offer, static_cast<LocalInputRevocation>(0)) &&
          !fixture.core->cancelOffer(offer, static_cast<LocalInputRevocation>(4)),
      "Invalid outgoing preflight payload was accepted"
  );
  check(fixture.core->offer(offer), "Core offer failed");
  check(
      until([&] { return (candidates == 1 && quiesced == 1) || fixture.closed; }) && !fixture.closed,
      "Host preflight failed"
  );
  check(
      fixture.core->phase() == LocalInputPhase::Idle && !fixture.core->lease() &&
          !fixture.core->prepare(fixtureLease()) && !fixture.core->commit(fixtureLease()) &&
          !fixture.core->sendOriginal(fixtureLease(), fixtureEvent(1)),
      "Preflight granted an unauthored input lease"
  );
  check(fixture.core->cancelOffer(offer), "Core cancellation failed");
  check(until([&] { return cancelled == 1 || fixture.closed; }) && !fixture.closed, "Host cancellation was lost");
  check(fixture.core->authenticated() && fixture.revoked == 0, "Preflight cancellation revoked unrelated authority");
  fixture.core->close();
  fixture.childExitedCleanly();
}
void preflightWireLayout()
{
  Fixture fixture("wire-v4", {1000, 4000, 4000});
  LocalInputChannelTestAccess::stopTimer(*fixture.core);
  int replies = 0, cancellations = 0;
  const auto receive = [&](const LocalInputLease &lease) {
    check(lease == fixtureLease() && !fixture.core->lease(), "Golden host reply changed or granted a lease");
    ++replies;
  };
  fixture.core->onCandidate = receive;
  fixture.core->onQuiesced = receive;
  fixture.core->onOfferCancelled = [&](const LocalInputOffer &offer, LocalInputRevocation reason) {
    check(
        offer == leaseOffer(fixtureLease()) && reason == LocalInputRevocation::AuthorityLost,
        "Golden host cancellation changed"
    );
    ++cancellations;
  };
  fixture.establish();
  check(fixture.core->offer(leaseOffer(fixtureLease())), "Cannot send golden offer");
  check(until([&] { return replies == 2 || fixture.closed; }) && !fixture.closed, "Golden host replies were rejected");
  check(fixture.core->cancelOffer(leaseOffer(fixtureLease())), "Cannot send golden cancellation");
  check(
      until([&] { return cancellations == 1 || fixture.closed; }) && !fixture.closed,
      "Golden host cancellation was rejected"
  );
  fixture.activate();
  check(fixture.core->sendOriginal(fixtureLease(), fixtureEvent(1)), "Cannot send golden Original");
  for (const auto &record : mixedRecords())
    check(fixture.core->sendOriginal(fixtureLease(), record), "Cannot send golden mixed Original");
  check(
      until([&] { return fixture.revoked == 1 || fixture.closed; }) && !fixture.closed, "Golden Original was rejected"
  );
  fixture.core->close();
  fixture.childExitedCleanly();
}
void preflightDoesNotOwnLeaseState()
{
  Fixture fixture("synthetic");
  fixture.establish();
  auto future = fixtureLease();
  future.routeGeneration = future.coreServerIncarnation = future.coreRecipient = future.coreSelectionGeneration =
      std::numeric_limits<std::uint64_t>::max();
  int candidates = 0, quiesced = 0, cancellations = 0;
  fixture.core->onCandidate = [&](const LocalInputLease &lease) {
    check(lease == future, "Full-width core fields changed during preflight decode");
    ++candidates;
  };
  fixture.core->onQuiesced = [&](const LocalInputLease &lease) {
    check(lease == fixtureLease(), "Coordinator did not receive stale-generation quiescence");
    ++quiesced;
  };
  fixture.core->onOfferCancelled = [&](const LocalInputOffer &offer, LocalInputRevocation) {
    check(offer == leaseOffer(future), "Coordinator did not receive cancellation binding");
    ++cancellations;
  };
  check(fixture.core->offer(future), "Full-width offer could not be queued");
  LocalInputChannelTestAccess::incoming(*fixture.core, 13, leasePayload(future));
  LocalInputChannelTestAccess::incoming(*fixture.core, 14, leasePayload(fixtureLease()));
  LocalInputChannelTestAccess::incoming(
      *fixture.core, 15, offerPayload(future) + static_cast<char>(LocalInputRevocation::Requested)
  );
  check(
      candidates == 1 && quiesced == 1 && cancellations == 1 && !fixture.core->lease() &&
          fixture.core->phase() == LocalInputPhase::Idle && fixture.core->prepare(fixtureLease()),
      "Preflight incorrectly owned lease generation/state instead of leaving classification to coordinator"
  );
  // Synthetic received frames have deliberately consumed private sequence slots;
  // close before receiving real traffic on this codec-only fixture.
  fixture.core->close();
  fixture.childExitedCleanly();
  Fixture active("synthetic");
  active.activate();
  active.core->onOfferCancelled = [](const LocalInputOffer &, LocalInputRevocation) {};
  LocalInputChannelTestAccess::incoming(
      *active.core, 15, offerPayload(fixtureLease()) + static_cast<char>(LocalInputRevocation::AuthorityLost)
  );
  check(
      active.core->phase() == LocalInputPhase::Active && active.core->lease() == fixtureLease() && active.revoked == 0,
      "Preflight cancellation directly released a committed lease"
  );
  active.core->close();
  active.childExitedCleanly();
}
void malformedPreflight()
{
  for (const unsigned type : {12, 13, 14, 15}) {
    for (int fault = 0; fault < 6; ++fault) {
      Fixture fixture("synthetic");
      fixture.establish();
      int delivered = 0;
      fixture.core->onCandidate = [&](const LocalInputLease &) { ++delivered; };
      fixture.core->onQuiesced = [&](const LocalInputLease &) { ++delivered; };
      fixture.core->onOfferCancelled = [&](const LocalInputOffer &, LocalInputRevocation) { ++delivered; };
      auto payload = type == 12 || type == 15 ? offerPayload(fixtureLease()) : leasePayload(fixtureLease());
      if (type == 15)
        payload += static_cast<char>(LocalInputRevocation::Requested);
      if (fault == 0)
        payload.chop(1);
      else if (fault == 1)
        payload.append('x');
      else if (fault == 2)
        payload.replace(0, 8, QByteArray(8, '\0'));
      else if (fault == 3)
        payload.replace(120, 8, QByteArray(8, '\0'));
      else if (fault == 4) {
        payload.replace(120, 8, QByteArray(8, '\0'));
        payload[127] = 3;
      } else
        payload.remove(120, 8); // A v3 offer/lease must never decode as v4.
      if (type == 12) {
        check(LocalInputChannelTestAccess::raw(*fixture.core, type, payload), "Cannot send malformed offer fixture");
        check(until([&] { return fixture.closed; }), "Malformed offer did not close receiver");
      } else
        LocalInputChannelTestAccess::incoming(*fixture.core, type, payload);
      check(fixture.closed && delivered == 0 && !fixture.active, "Malformed preflight reached coordinator");
      fixture.childExitedCleanly();
    }
  }
  for (const unsigned type : {12, 13, 14}) {
    Fixture fixture("synthetic");
    fixture.establish();
    if (type == 12)
      LocalInputChannelTestAccess::incoming(*fixture.core, type, offerPayload(fixtureLease()));
    else {
      check(
          LocalInputChannelTestAccess::raw(*fixture.core, type, leasePayload(fixtureLease())),
          "Cannot send wrong-role fixture"
      );
      check(until([&] { return fixture.closed; }), "Wrong-role preflight did not close receiver");
    }
    check(fixture.closed && !fixture.active, "Wrong-role preflight was accepted");
    fixture.childExitedCleanly();
  }
  for (const char reason : {0, 4}) {
    Fixture fixture("synthetic");
    fixture.establish();
    LocalInputChannelTestAccess::incoming(*fixture.core, 15, offerPayload(fixtureLease()) + reason);
    check(fixture.closed, "Unknown offer cancellation reason was accepted");
    fixture.childExitedCleanly();
  }
  // An otherwise valid lease with a host counter outside JSON precision is
  // malformed, even though core-owned counters retain all 64 bits.
  Fixture bounds("synthetic");
  bounds.establish();
  auto invalid = fixtureLease();
  invalid.focusRequest = std::uint64_t{1} << 53;
  LocalInputChannelTestAccess::incoming(*bounds.core, 13, leasePayload(invalid));
  check(bounds.closed, "Oversized host focus request reached coordinator");
  bounds.childExitedCleanly();
}
void preflightCallbacks()
{
  for (const unsigned type : {13, 14, 15}) {
    for (const QString action : {"revoke", "close", "delete", "throw"}) {
      Fixture fixture("synthetic");
      fixture.activate();
      int callbacks = 0, nested = 0;
      const auto callback = [&, marker = QString(256, 'c')] {
        ++callbacks;
        if (action == "revoke") {
          fixture.core->revoke();
          if (type == 15) {
            fixture.core->onCandidate = [&](const LocalInputLease &) { ++nested; };
            LocalInputChannelTestAccess::incoming(*fixture.core, 13, leasePayload(fixtureLease()));
          } else {
            fixture.core->onOfferCancelled = [&](const LocalInputOffer &, LocalInputRevocation) { ++nested; };
            LocalInputChannelTestAccess::incoming(
                *fixture.core, 15, offerPayload(fixtureLease()) + static_cast<char>(LocalInputRevocation::Requested)
            );
          }
        } else if (action == "close")
          fixture.core->close();
        else if (action == "delete")
          fixture.core.reset();
        else
          throw std::runtime_error("Preflight callback failure");
        check(marker.at(callbacks % marker.size()) == 'c', "Copied preflight callback was destroyed while executing");
      };
      const auto reply = [callback](const LocalInputLease &lease) {
        check(lease == fixtureLease(), "Reentrant preflight binding changed");
        callback();
      };
      fixture.core->onCandidate = reply;
      fixture.core->onQuiesced = reply;
      fixture.core->onOfferCancelled = [callback](const LocalInputOffer &offer, LocalInputRevocation) {
        check(offer == leaseOffer(fixtureLease()), "Reentrant cancellation binding changed");
        callback();
      };
      auto payload = type == 15 ? offerPayload(fixtureLease()) : leasePayload(fixtureLease());
      if (type == 15)
        payload += static_cast<char>(LocalInputRevocation::Requested);
      LocalInputChannelTestAccess::incoming(*fixture.core, type, payload);
      check(callbacks == 1 && fixture.revoked == 1, "Preflight callback lost or duplicated lease release");
      if (action == "revoke") {
        if (nested != 1 || fixture.closed || fixture.core->lease())
          throw std::runtime_error(QStringLiteral(
                                       "Reentrant preflight notification failed: type=%1 action=%2 "
                                       "nested=%3 closed=%4 reason=%5"
          )
                                       .arg(type)
                                       .arg(action)
                                       .arg(nested)
                                       .arg(fixture.closed)
                                       .arg(fixture.reason)
                                       .toStdString());
        fixture.core->close();
      } else
        check(fixture.closed, "Failed/destructive preflight callback retained channel authority");
      fixture.childExitedCleanly();
    }
  }
  for (const QString action : {"revoke", "close", "delete", "throw"}) {
    Fixture fixture("offer-callback-" + action);
    int quiesced = 0;
    fixture.core->onQuiesced = [&](const LocalInputLease &) { ++quiesced; };
    fixture.establish();
    check(fixture.core->offer(leaseOffer(fixtureLease())), "Cannot exercise host offer callback");
    if (action == "revoke") {
      check(until([&] { return quiesced == 1 || fixture.closed; }) && !fixture.closed, "Reentrant host offer failed");
      fixture.core->close();
    } else
      check(until([&] { return fixture.closed; }), "Failed/destructive host offer callback retained channel");
    fixture.childExitedCleanly();
  }
}
void preflightExpiry()
{
  for (const bool incoming : {false, true}) {
    Fixture fixture("patient");
    fixture.establish();
    int callbacks = 0;
    fixture.core->onCandidate = [&](const LocalInputLease &) { ++callbacks; };
    LocalInputChannelTestAccess::stopTimer(*fixture.core);
    QThread::msleep(limits.timeoutMs + 50);
    if (incoming)
      LocalInputChannelTestAccess::incoming(*fixture.core, 13, leasePayload(fixtureLease()));
    else
      check(!fixture.core->offer(leaseOffer(fixtureLease())), "Expired private channel sent preflight");
    check(fixture.closed && callbacks == 0, "Queued preflight revived an expired private channel");
    fixture.childExitedCleanly();
  }
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
void negotiatedScope()
{
  // Independent endpoint capability declarations intersect; neither side's
  // full-input code support alone grants a full-input private contract.
  for (const bool limitedHost : {false, true}) {
    auto coreLimits = limits;
    if (!limitedHost)
      coreLimits.capabilities = localInputKeyboardCapability;
    Fixture f(limitedHost ? "keyboard-only" : "keyboard-lease", coreLimits);
    f.establish();
    check(
        f.core->supports(ForwardingScope::Keyboard) && !f.core->supports(ForwardingScope::All) &&
            !f.core->supports(static_cast<ForwardingScope>(255)) &&
            f.core->authenticatedIdentity()->capabilities == localInputKeyboardCapability,
        "Private bootstrap widened or erased capability intersection"
    );
    check(
        !f.core->offer(leaseOffer(fixtureLease())) && !f.core->prepare(fixtureLease()) &&
            f.core->phase() == LocalInputPhase::Idle && !f.closed,
        "Unnegotiated full scope entered local preflight or acquired authority"
    );
    f.activate();
    check(f.core->sendOriginal(f.expectedLease, fixtureEvent(1)), "Negotiated keyboard lease could not deliver a key");
    check(
        until([&] { return f.revoked || f.closed; }) && !f.closed && f.revoked == 1,
        "Keyboard-only receiver did not release the exact owner"
    );
    f.core->close();
    f.childExitedCleanly();
  }
  for (const bool bypassLocalCheck : {false, true}) {
    Fixture f("keyboard-lease");
    f.activate();
    auto motion = fixtureEvent(1);
    motion.payload = OriginalInputMotion{1, -1, 32};
    if (bypassLocalCheck) {
      check(
          LocalInputChannelTestAccess::rawOriginal(*f.core, originalPayload(f.expectedLease, motion)),
          "Cannot send over-scope receiver fixture"
      );
      check(until([&] { return f.closed; }), "Receiver accepted a pointer under a keyboard lease");
    } else
      check(
          !f.core->sendOriginal(f.expectedLease, motion) && f.closed, "Sender accepted a pointer under a keyboard lease"
      );
    check(f.revoked == 1, "Scope violation failed to release the exact keyboard owner");
    f.childExitedCleanly();
  }
  Fixture forged("synthetic", {20, 400, 400, localInputKeyboardCapability});
  forged.establish();
  int candidates = 0;
  forged.core->onCandidate = [&](const LocalInputLease &) { ++candidates; };
  LocalInputChannelTestAccess::incoming(*forged.core, 13, leasePayload(fixtureLease()));
  check(
      forged.closed && candidates == 0 && !forged.core->lease(),
      "An unnegotiated full-scope reply reached the coordinator"
  );
  forged.childExitedCleanly();
}

void malformedBootstrap()
{
  for (const auto *mode :
       {"magic", "version", "role", "incarnation", "zero-incarnation", "process", "sequence", "oversize",
        "partial-header", "partial-payload", "trailing", "unknown", "replay", "silent", "capabilities-zero",
        "capabilities-unknown", "capabilities-no-keyboard"}) {
    Fixture fixture(QStringLiteral("raw-") + mode);
    check(until([&] { return fixture.closed; }), "Malformed bootstrap did not fail closed");
    check(
        !fixture.core->authenticated() && !fixture.active && fixture.revoked == 0,
        "Malformed bootstrap gained input authority"
    );
  }
}
void handshakeDeadlines()
{
  static_assert(LocalInputLimits{}.handshakeMs == 15000 && LocalInputLimits{}.timeoutMs == 2500);
  {
    Fixture fixture("raw-slow-hello", {20, 200, 1600});
    bool freshInCallback = false;
    fixture.core->onAuthenticated = [&] {
      freshInCallback = fixture.core->authenticated();
      fixture.ready = true;
    };
    fixture.establish();
    check(freshInCallback, "Authenticated callback inherited the older bootstrap read age");
    // The longer handshake allowance ends at authentication. A queued Ping
    // after a loop stall must not borrow that allowance or revive freshness.
    LocalInputChannelTestAccess::freeze(*fixture.core);
    QThread::msleep(250);
    LocalInputChannelTestAccess::incoming(*fixture.core, 4);
    check(
        fixture.closed && !fixture.core->authenticated() && fixture.reason.contains("heartbeat"),
        "Handshake allowance extended the authenticated timeout"
    );
  }
  {
    Fixture fixture("raw-silent", {20, 2000, 200});
    check(
        until([&] { return LocalInputChannelTestAccess::handshake(*fixture.core) == 1; }),
        "Late hello fixture did not send CoreHello"
    );
    LocalInputChannelTestAccess::freeze(*fixture.core);
    const auto self = currentLocalInputProcess(), child = localInputProcess(fixture.child.processId());
    check(self && child, "Late hello fixture lost a process identity");
    QByteArray payload;
    for (const auto identity : {*self, *child}) {
      number(payload, identity.pid, 4);
      number(payload, identity.startStamp, 8);
    }
    QThread::msleep(250);
    LocalInputChannelTestAccess::incoming(*fixture.core, 2, payload);
    check(
        fixture.closed && !fixture.ready && !fixture.core->authenticated() &&
            fixture.reason.contains("stage=host-hello") && fixture.reason.contains("elapsed="),
        "Queued late HostHello revived expired bootstrap"
    );
  }
  for (const QString mode : {"late-ready", "late-ready-transition", "slow-ready"}) {
    Fixture fixture(mode, {20, 2000, 2000});
    fixture.establish();
    fixture.childExitedCleanly();
    check(until([&] { return fixture.closed; }), "Late Ready fixture did not close its endpoint");
  }
}
void changedBindings()
{
  for (int field = 0; field < 16; ++field) {
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
    case 9:
      ++lease.coreServerIncarnation;
      break;
    case 10:
      ++lease.coreRecipient;
      break;
    case 11:
      ++lease.coreSelectionGeneration;
      break;
    case 12:
      ++lease.focusRequest;
      break;
    case 13:
      ++lease.keyboardRouteGeneration;
      break;
    case 14:
      ++lease.captureEpoch;
      break;
    case 15:
      lease.scope = ForwardingScope::Keyboard;
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
  Fixture original("expired-original", {20, 2000, 2000});
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
        event.sequence = 1;
      else if (invalid == "timestamp")
        event.sourceTimestampNs = 99;
      else if (invalid == "malformed")
        std::get<OriginalInputKey>(event.payload).transition = static_cast<OriginalKeyTransition>(255);
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
void mixedInputAndMalformedRecords()
{
  {
    Fixture fixture("mixed");
    fixture.activate();
    for (const auto &record : mixedRecords())
      check(fixture.core->sendOriginal(fixtureLease(), record), "Mixed ordered capture was not queued");
    check(
        until([&] { return fixture.revoked || fixture.closed; }) && !fixture.closed,
        "Mixed input failed to reach its exact owner in order"
    );
    fixture.core->close();
    fixture.childExitedCleanly();
  }
  const auto records = mixedRecords();
  std::vector<QByteArray> malformed;
  for (const auto index : {0, 1, 2, 4, 7}) {
    auto truncated = originalPayload(fixtureLease(), records[index]);
    truncated.chop(1);
    malformed.push_back(truncated);
    malformed.push_back(originalPayload(fixtureLease(), records[index]) + 'x');
  }
  const auto altered = [&](int index, qsizetype offset, unsigned value) {
    auto payload = originalPayload(fixtureLease(), records[index]);
    payload[192 + offset] = static_cast<char>(value);
    malformed.push_back(payload);
  };
  altered(0, 24, 0); // Unknown tag.
  altered(0, 24, 255);
  altered(0, 31, 0);   // Key kind.
  altered(0, 32, 2);   // Key repeat is a strict bool.
  altered(0, 33, 255); // Key transition.
  altered(1, 33, 33);  // Invalid drag owner.
  altered(2, 25, 32);  // Invalid button.
  altered(2, 26, 2);   // Button direction is a strict bool.
  altered(2, 27, 128); // Click count exceeds signed32.
  auto zeroClickDown = records[2];
  std::get<OriginalInputButton>(zeroClickDown.payload).clickCount = 0;
  malformed.push_back(originalPayload(fixtureLease(), zeroClickDown));
  altered(4, 49, 3); // Scroll phase is an enum, never a bitmask.
  altered(4, 50, 4); // Momentum phase.
  altered(4, 50, 1); // Gesture and momentum cannot overlap.
  altered(4, 51, 2); // Continuous is a strict bool.
  altered(4, 51, 0); // Phased discrete scroll is inconsistent.
  altered(7, 25, 1); // Discontinuity/emergency are never delivered.
  altered(7, 25, 2);
  altered(7, 25, 255);
  auto unknownFlags = records[1];
  unknownFlags.flags = originalKey::shiftFlag;
  malformed.push_back(originalPayload(fixtureLease(), unknownFlags));
  auto emergencyKey = fixtureEvent(1);
  std::get<OriginalInputKey>(emergencyKey.payload).code = originalKey::escape;
  emergencyKey.flags = originalKey::shiftFlag | originalKey::leftShiftFlag | originalKey::controlFlag |
                       originalKey::leftControlFlag | originalKey::optionFlag | originalKey::leftOptionFlag;
  malformed.push_back(originalPayload(fixtureLease(), emergencyKey));
  for (const auto &payload : malformed) {
    Fixture fixture;
    fixture.activate();
    check(LocalInputChannelTestAccess::rawOriginal(*fixture.core, payload), "Malformed record fixture did not send");
    check(
        until([&] { return fixture.closed; }) && fixture.revoked == 1,
        "Malformed mixed input did not revoke before delivery"
    );
    fixture.childExitedCleanly();
  }
  for (int fault = 0; fault < 4; ++fault) {
    Fixture fixture;
    fixture.activate();
    auto record = records[1];
    if (fault == 0)
      std::get<OriginalInputMotion>(record.payload).dx = std::int64_t{1} << 31;
    else if (fault == 1)
      std::get<OriginalInputMotion>(record.payload).dy = -(std::int64_t{1} << 31) - 1;
    else if (fault == 2) {
      record = records[4];
      std::get<OriginalInputScroll>(record.payload).fixed16_16[1] = std::int64_t{1} << 31;
    } else
      ++record.epoch;
    check(
        !fixture.core->sendOriginal(fixtureLease(), record) && fixture.closed && fixture.revoked == 1,
        "Invalid signed32 value or mismatched capture epoch escaped local validation"
    );
    fixture.childExitedCleanly();
  }
  for (int stop = 0; stop < 3; ++stop) {
    Fixture fixture;
    fixture.activate();
    auto record = fixtureEvent(1);
    if (stop < 2)
      record.payload = stop == 0 ? OriginalInputBoundary::Emergency : OriginalInputBoundary::Discontinuity;
    else {
      std::get<OriginalInputKey>(record.payload).code = originalKey::escape;
      record.flags = originalKey::shiftFlag | originalKey::leftShiftFlag | originalKey::controlFlag |
                     originalKey::leftControlFlag | originalKey::optionFlag | originalKey::leftOptionFlag;
    }
    check(
        !fixture.core->sendOriginal(fixtureLease(), record) && !fixture.closed && fixture.revoked == 1 &&
            fixture.core->phase() == LocalInputPhase::Idle,
        "Emergency/discontinuity became deliverable input"
    );
    fixture.core->close();
    fixture.childExitedCleanly();
  }
}
void captureEpochCutover()
{
  {
    Fixture fixture("epochs");
    fixture.establish();
    fixture.core->authorize = [](LocalInputLease lease, LocalInputCheck) {
      lease.routeGeneration = fixtureLease().routeGeneration;
      lease.captureEpoch = fixtureLease().captureEpoch;
      return lease == fixtureLease();
    };
    int releases = 0;
    fixture.core->onPrepared = [&](const auto &lease) {
      check(fixture.core->commit(lease), "Epoch lease did not commit");
    };
    fixture.core->onActivated = [&](const auto &lease) {
      if (releases == 1)
        check(
            LocalInputChannelTestAccess::captureAt(*fixture.core, 100, 10000),
            "Same-epoch core preparation reset capture replay state"
        );
      auto record = fixtureEvent(lease.captureEpoch == 22 ? 1 : 100 + releases * 10);
      record.epoch = lease.captureEpoch;
      record.payload = OriginalInputMotion{-3, 4, 32};
      check(fixture.core->sendOriginal(lease, record), "Epoch sequence gap/cutover was rejected");
    };
    fixture.core->onRevoked = [&](const auto &lease, auto) {
      if (++releases < 3) {
        auto next = lease;
        ++next.routeGeneration;
        if (releases == 2)
          ++next.captureEpoch;
        check(fixture.core->prepare(next), "Fresh epoch lease could not be prepared");
      }
    };
    check(fixture.core->prepare(fixtureLease()), "Initial epoch did not prepare");
    check(
        until([&] { return releases == 3 || fixture.closed; }) && releases == 3 && !fixture.closed &&
            LocalInputChannelTestAccess::captureEpoch(*fixture.core) == 22 &&
            LocalInputChannelTestAccess::captureAt(*fixture.core, 1, 100),
        "Epoch-qualified replay counters changed"
    );
    fixture.core->close();
    fixture.childExitedCleanly();
  }
  for (int violation = 0; violation < 3; ++violation) {
    Fixture fixture("epochs");
    fixture.establish();
    fixture.core->authorize = [](const auto &, auto) { return true; };
    fixture.core->onPrepared = [&](const auto &) { fixture.prepared = true; };
    fixture.core->onRevoked = [&](const auto &, auto) { ++fixture.revoked; };
    auto newer = fixtureLease();
    newer.captureEpoch = 22;
    check(fixture.core->prepare(newer), "New capture epoch was not admitted");
    check(
        until([&] { return fixture.prepared || fixture.closed; }) && !fixture.closed,
        "New capture epoch preparation failed"
    );
    fixture.core->revoke(); // No Original was sent: admission itself must retain the epoch.
    auto old = fixtureLease();
    ++old.routeGeneration;
    check(
        !fixture.core->prepare(old) && LocalInputChannelTestAccess::captureEpoch(*fixture.core) == 22,
        "No-event capture preparation allowed epoch regression"
    );
    if (violation == 0) {
      check(LocalInputChannelTestAccess::raw(*fixture.core, 6, leasePayload(old)), "Cannot queue regressed Prepare");
    } else {
      if (violation == 2)
        old = newer; // Same admitted epoch, but unprepared future route is still forbidden.
      old.routeGeneration = violation == 1 ? fixtureLease().routeGeneration - 1 : newer.routeGeneration + 1;
      auto record = fixtureEvent(100);
      record.epoch = old.captureEpoch;
      check(
          LocalInputChannelTestAccess::rawOriginal(*fixture.core, originalPayload(old, record)),
          "Cannot queue stale/unprepared capture epoch fixture"
      );
    }
    check(until([&] { return fixture.closed; }), "Old or unprepared capture epoch gained receiver authority");
    fixture.childExitedCleanly();
  }
}
void bufferedInputEvidence()
{
#ifdef Q_OS_UNIX
  for (const auto &mode : {"buffered-evidence-waiting", "buffered-evidence-recording"}) {
    Fixture fixture(mode, {1000, 10000, 2000});
    fixture.activate();
    QByteArray output;
    check(
        until([&] {
          output += fixture.child.readAllStandardOutput();
          return output.contains("BUFFERED_EVIDENCE_READY\n") || fixture.child.state() == QProcess::NotRunning;
        }) &&
            output.contains("BUFFERED_EVIDENCE_READY\n") && fixture.child.state() != QProcess::NotRunning,
        "Host recorder did not establish an empty authenticated baseline"
    );
    // The burst exceeds read()'s 64-frame scheduling budget and the actual
    // QLocalSocket read buffer. It must be accounted across every refill;
    // nothing is injected directly into the decoder or recorder.
    for (std::uint64_t index = 0; index != bufferedEvidenceFrames; ++index)
      check(LocalInputChannelTestAccess::raw(*fixture.core, 5, {}), "Cannot send private buffered-input burst");
    LocalInputChannelTestAccess::flush(*fixture.core);
    fixture.childExitedCleanly();
  }
#endif
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
void authenticatedIdentityValues()
{
  Fixture fixture;
  check(!fixture.core->authenticatedIdentity(), "Pre-handshake channel exposed an authenticated identity");
  fixture.requireAuthenticatedIdentity("Identity fixture did not authenticate");
  const auto identity = fixture.core->authenticatedIdentity();
  check(
      identity && identity->role == LocalInputRole::Core && identity->self == currentLocalInputProcess() &&
          identity->peer == localInputProcess(fixture.child.processId()),
      "Authenticated identity did not preserve exact role/process births"
  );
  check(
      std::any_of(identity->coreIncarnation.begin(), identity->coreIncarnation.end(), [](auto byte) { return byte; }) &&
          std::any_of(
              identity->hostIncarnation.begin(), identity->hostIncarnation.end(), [](auto byte) { return byte; }
          ),
      "Authenticated identity omitted a channel incarnation"
  );
  bool foreignDenied = false;
  std::thread foreign([&] { foreignDenied = !fixture.core->authenticatedIdentity(); });
  foreign.join();
  check(
      foreignDenied && fixture.core->authenticatedIdentity() == identity,
      "Foreign identity query gained or changed authority"
  );
  bool revokedBeforeNotification = false;
  fixture.core->onClosed = [&](QString) { revokedBeforeNotification = !fixture.core->authenticatedIdentity(); };
  fixture.core->close();
  check(
      revokedBeforeNotification && !fixture.core->authenticatedIdentity(),
      "Closed channel retained authenticated identity"
  );
  Fixture replacement;
  replacement.requireAuthenticatedIdentity("Replacement identity fixture did not authenticate");
  const auto next = replacement.core->authenticatedIdentity();
  check(
      next && next->coreIncarnation != identity->coreIncarnation && next->hostIncarnation != identity->hostIncarnation,
      "New connection reused an old authenticated identity"
  );
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
  for (const int handshakeMs : {-1, 0, 99, 30001, std::numeric_limits<int>::max()}) {
    auto invalidPair = LocalInputSocketPair::create();
    auto invalidLimits = limits;
    invalidLimits.handshakeMs = handshakeMs;
    rejected = false;
    try {
      invalidPair.adoptCore(*identity, invalidLimits);
    } catch (...) {
      rejected = true;
    }
    check(rejected, "Invalid absolute handshake budget was accepted");
  }
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
    QTextStream(stdout) << "RUN: descriptorAndIdentityGuards\n" << Qt::flush;
    descriptorAndIdentityGuards();
    QTextStream(stdout) << "RUN: authenticatedIdentityValues\n" << Qt::flush;
    authenticatedIdentityValues();
    QTextStream(stdout) << "RUN: exclusiveInheritance\n" << Qt::flush;
    exclusiveInheritance();
    QTextStream(stdout) << "RUN: preflightValues\n" << Qt::flush;
    preflightValues();
    QTextStream(stdout) << "RUN: mappedInput\n" << Qt::flush;
    mappedInput();
    QTextStream(stdout) << "RUN: preflightRoundtrip\n" << Qt::flush;
    preflightRoundtrip();
    QTextStream(stdout) << "RUN: preflightWireLayout\n" << Qt::flush;
    preflightWireLayout();
    QTextStream(stdout) << "RUN: preflightDoesNotOwnLeaseState\n" << Qt::flush;
    preflightDoesNotOwnLeaseState();
    QTextStream(stdout) << "RUN: malformedPreflight\n" << Qt::flush;
    malformedPreflight();
    QTextStream(stdout) << "RUN: preflightCallbacks\n" << Qt::flush;
    preflightCallbacks();
    QTextStream(stdout) << "RUN: preflightExpiry\n" << Qt::flush;
    preflightExpiry();
    QTextStream(stdout) << "RUN: roundtrip\n" << Qt::flush;
    roundtrip();
    QTextStream(stdout) << "RUN: authoritativeCommit\n" << Qt::flush;
    authoritativeCommit();
    QTextStream(stdout) << "RUN: malformedBootstrap\n" << Qt::flush;
    malformedBootstrap();
    QTextStream(stdout) << "RUN: negotiatedScope\n" << Qt::flush;
    negotiatedScope();
    QTextStream(stdout) << "RUN: handshakeDeadlines\n" << Qt::flush;
    handshakeDeadlines();
    QTextStream(stdout) << "RUN: changedBindings\n" << Qt::flush;
    changedBindings();
    QTextStream(stdout) << "RUN: expiryAtUse\n" << Qt::flush;
    expiryAtUse();
    QTextStream(stdout) << "RUN: reentrantAuthorization\n" << Qt::flush;
    reentrantAuthorization();
    QTextStream(stdout) << "RUN: destructiveNotifications\n" << Qt::flush;
    destructiveNotifications();
    QTextStream(stdout) << "RUN: cancelledLeaseReuse\n" << Qt::flush;
    cancelledLeaseReuse();
    QTextStream(stdout) << "RUN: retiredOriginals\n" << Qt::flush;
    retiredOriginals();
    QTextStream(stdout) << "RUN: mixedInputAndMalformedRecords\n" << Qt::flush;
    mixedInputAndMalformedRecords();
    QTextStream(stdout) << "RUN: captureEpochCutover\n" << Qt::flush;
    captureEpochCutover();
    QTextStream(stdout) << "RUN: bufferedInputEvidence\n" << Qt::flush;
    bufferedInputEvidence();
    QTextStream(stdout) << "RUN: lossAndLimits\n" << Qt::flush;
    lossAndLimits();
    QTextStream(
        stdout
    ) << "PASS: inherited IPC, process/role/incarnation binding, version 4 preflight isolation and framing, "
         "absolute bootstrap deadlines, independent authenticated expiry, authoritative leases, "
         "original-event framing, stale/replayed/malformed rejection, bounded queues and owner revocation\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
