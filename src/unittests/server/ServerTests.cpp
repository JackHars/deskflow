/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerTests.h"

#include "server/Server.h"

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
#include "../deskflow/MockEventQueue.h"
#include "../deskflow/MockKeyState.h"
#include "base/EventQueue.h"
#include "deskflow/AppUtil.h"
#include "deskflow/Computer.h"
#include "deskflow/PlatformComputer.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include "io/IStream.h"
#include "server/ClientProxy1_1.h"
#include "server/ClientProxy1_8.h"
#include "server/InputFilter.h"
#include "server/PrimaryClient.h"
#include "windowshare/InputEvidence.h"

#include <QTemporaryDir>
#include <limits>
#include <memory>
#include <utility>

namespace {

// Exercise the real Computer/PrimaryClient lifecycle without opening a display,
// registering system hotkeys, or synthesizing input on the test machine.
class RoutingPlatform : public PlatformComputer
{
public:
  explicit RoutingPlatform(IEventQueue *events) : PlatformComputer(events), m_keys(*events)
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<RoutingPlatform *>(this);
  }
  bool getClipboard(ClipboardID, IClipboard *) const override
  {
    return false;
  }
  void getShape(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const override
  {
    x = y = 0;
    width = height = 1000;
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
    x = y = 500;
  }
  void getCursorCenter(int32_t &x, int32_t &y) const override
  {
    observeSelection();
    x = y = 500;
  }
  void reconfigure(uint32_t) override
  {
    observeSelection();
  }
  uint32_t activeSides() override
  {
    return 0;
  }
  void warpCursor(int32_t, int32_t) override
  {
  }
  uint32_t registerHotKey(KeyID, KeyModifierMask) override
  {
    return 1;
  }
  void unregisterHotKey(uint32_t) override
  {
  }
  void fakeInputBegin() override
  {
  }
  void fakeInputEnd() override
  {
  }
  int32_t getJumpZoneSize() const override
  {
    return 1;
  }
  bool isAnyMouseButtonDown(uint32_t &button) const override
  {
    button = mouseHeld ? 1 : 0;
    return mouseHeld;
  }
  void fakeMouseButton(ButtonID, bool) override
  {
  }
  void fakeMouseMove(int32_t, int32_t) override
  {
  }
  void fakeMouseRelativeMove(int32_t, int32_t) const override
  {
  }
  void fakeMouseWheel(ScrollDelta) const override
  {
  }
  void updateKeyMap() override
  {
  }
  void updateKeyState() override
  {
  }
  KeyModifierMask pollActiveModifiers() const override
  {
    return modifiers;
  }
  void enable() override
  {
  }
  void disable() override
  {
    observeSelection();
  }
  void enter() override
  {
    observeSelection();
  }
  bool canLeave() override
  {
    observeSelection();
    return mayLeave;
  }
  void leave() override
  {
  }
  bool setClipboard(ClipboardID, const IClipboard *) override
  {
    return true;
  }
  void checkClipboards() override
  {
  }
  void openScreensaver(bool) override
  {
  }
  void closeScreensaver() override
  {
  }
  void screensaver(bool) override
  {
  }
  void resetOptions() override
  {
  }
  void setOptions(const OptionsList &) override
  {
  }
  void setSequenceNumber(uint32_t) override
  {
  }
  std::string getSecureInputApp() const override
  {
    return {};
  }
  bool isPrimary() const override
  {
    return true;
  }

  KeyModifierMask modifiers = 0;
  bool mayLeave = true;
  bool mouseHeld = false;
  mutable bool sawLiveSelection = false;
  std::optional<deskflow::windowshare::SelectedPeer> watchedSelection;

  void observeSelection() const
  {
    if (watchedSelection && watchedSelection->live())
      sawLiveSelection = true;
  }

protected:
  void updateButtons() override
  {
  }
  IKeyState *getKeyState() const override
  {
    return &m_keys;
  }
  void handleSystemEvent(const Event &) override
  {
  }

private:
  mutable MockKeyState m_keys;
};

struct WireCapture
{
  QByteArray bytes;
  std::function<void()> onWrite;
  bool destroyed = false;
  unsigned destructions = 0;
  bool fingerprintOnly = false, sawLiveSelection = false;
  unsigned observations = 0;
  std::unique_ptr<deskflow::AuthenticatedPeerIssuer> identity;
  std::optional<deskflow::windowshare::SelectedPeer> watchedSelection;

  void authenticate()
  {
    identity = std::make_unique<deskflow::AuthenticatedPeerIssuer>();
    deskflow::AuthenticatedPeerTicket::Fingerprint fingerprint{};
    fingerprint.fill(0x5a);
    if (!identity->issue(fingerprint))
      throw std::runtime_error("Cannot issue fake stream authentication ticket");
  }
  void observeSelection()
  {
    ++observations;
    if (watchedSelection && watchedSelection->live())
      sawLiveSelection = true;
  }
  QByteArray take()
  {
    return std::exchange(bytes, {});
  }
};

class RoutingStream : public deskflow::IStream
{
public:
  explicit RoutingStream(std::shared_ptr<WireCapture> capture) : m_capture(std::move(capture))
  {
  }
  ~RoutingStream() override
  {
    m_capture->observeSelection();
    if (m_capture->identity)
      m_capture->identity->revoke();
    m_capture->destroyed = true;
    ++m_capture->destructions;
  }
  void write(const void *buffer, uint32_t n) override
  {
    m_capture->observeSelection();
    m_capture->bytes.append(static_cast<const char *>(buffer), n);
    if (m_capture->onWrite) {
      auto callback = std::exchange(m_capture->onWrite, {});
      callback();
    }
  }
  void close() override
  {
  }
  uint32_t read(void *, uint32_t) override
  {
    return 0;
  }
  void flush() override
  {
  }
  void shutdownInput() override
  {
  }
  void shutdownOutput() override
  {
  }
  void *getEventTarget() const override
  {
    return const_cast<RoutingStream *>(this);
  }
  bool isReady() const override
  {
    return false;
  }
  uint32_t getSize() const override
  {
    return 0;
  }
  std::optional<deskflow::AuthenticatedPeerTicket> authenticatedPeerTicket() const override
  {
    return m_capture->identity ? m_capture->identity->ticket() : std::nullopt;
  }
  std::optional<PeerFingerprint> authenticatedPeerFingerprint() const override
  {
    if (!m_capture->fingerprintOnly)
      return std::nullopt;
    PeerFingerprint fingerprint{};
    fingerprint.fill(0x5a);
    return fingerprint;
  }

private:
  std::shared_ptr<WireCapture> m_capture;
};

class RoutingClient : public ClientProxy1_1
{
public:
  RoutingClient(const char *name, IEventQueue *events, const std::shared_ptr<WireCapture> &capture)
      : ClientProxy1_1(name, new RoutingStream(capture), events),
        m_capture(capture)
  {
  }

  void getShape(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const override
  {
    x = y = 0;
    width = height = 1000;
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
    x = y = 500;
  }
  bool leave() override
  {
    m_capture->observeSelection();
    return mayLeave && ClientProxy1_1::leave();
  }
  bool mayLeave = true;

private:
  std::shared_ptr<WireCapture> m_capture;
};

class TracedRoutingClient : public ClientProxy1_8
{
public:
  TracedRoutingClient(
      const char *name, Server *server, IEventQueue *events, const std::shared_ptr<WireCapture> &capture
  )
      : ClientProxy1_8(name, new RoutingStream(capture), server, events)
  {
  }
  void getShape(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const override
  {
    x = y = 0;
    width = height = 1000;
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
    x = y = 500;
  }
  // Packet framing/parser coverage is in ClientProxyTests. Here drive the
  // validated transition to exercise the real Server routing and writer join.
  void acknowledge(std::uint32_t generation)
  {
    receiveInputSequenceAck(generation);
  }
};

class RoutingEventQueue : public MockEventQueue
{
public:
  EventQueueTimer *newTimer(double, void *target) override
  {
    timerTarget = target;
    return reinterpret_cast<EventQueueTimer *>(&timerStorage);
  }
  void tick()
  {
    dispatchEvent(Event(EventTypes::Timer, timerTarget));
  }
  void *timerTarget = nullptr;
  char timerStorage = 0;
  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  {
    handlers[{type, target}] = handler;
  }
  bool dispatchEvent(const Event &event) override
  {
    const auto found = handlers.find({event.getType(), event.getTarget()});
    if (found == handlers.end())
      return false;
    const auto handler = found->second;
    handler(event);
    return true;
  }
  void addEvent(Event &&event) override
  {
    // This fixture deliberately drops queued notifications, but still owns
    // their payloads just like the real event queue. Free on exception too.
    try {
      if (event.getFlags() & Event::EventFlags::DeliverImmediately)
        dispatchEvent(event);
    } catch (...) {
      Event::deleteData(event);
      throw;
    }
    Event::deleteData(event);
  }
  std::map<std::pair<EventTypes, void *>, EventHandler> handlers;
  void removeHandler(EventTypes type, void *target) override
  {
    if (watchedSelection && watchedSelection->live())
      sawLiveSelection = true;
    handlers.erase({type, target});
    MockEventQueue::removeHandler(type, target);
  }
  bool sawLiveSelection = false;
  std::optional<deskflow::windowshare::SelectedPeer> watchedSelection;
};

struct RoutingFixture
{
  RoutingEventQueue events;
  deskflow::server::Config config{&events};
  RoutingPlatform *platform = new RoutingPlatform(&events);
  deskflow::Computer computer{platform, &events};
  PrimaryClient primary{"primary", &computer};
  std::unique_ptr<Server> server;
  std::shared_ptr<WireCapture> first = std::make_shared<WireCapture>();
  std::shared_ptr<WireCapture> second = std::make_shared<WireCapture>();
  RoutingClient *firstClient = nullptr;
  RoutingClient *secondClient = nullptr;

  RoutingFixture()
  {
    config.addComputer("primary");
    config.addComputer("first");
    config.addComputer("second");
    config.addOption("", kOptionClipboardSharing, 0);
    config.addOption("", kOptionDisableLockToComputer, 1);
    server = std::make_unique<Server>(config, &primary, &computer, &events);
    firstClient = new RoutingClient("first", &events, first);
    secondClient = new RoutingClient("second", &events, second);
    server->adoptClient(firstClient);
    server->adoptClient(secondClient);
    clear();
  }

  void clear()
  {
    first->take();
    second->take();
  }
};

struct RecordingOriginalDispatcher : Server::OriginalInputDispatcher
{
  using Captured = deskflow::windowshare::OriginalInputCaptured;
  struct Sent
  {
    Captured captured;
    deskflow::windowshare::OriginalInputRouteDecision route;
    std::uint64_t owner;
  };
  std::vector<Sent> reserved, committed;
  std::vector<std::uint64_t> cancelled, released;
  std::vector<std::uint64_t> requestedOwners;
  std::vector<Preview> previewed;
  std::optional<std::uint64_t> forcedOwner;
  std::function<void()> onReserve, onCommit, onPoll, onCancelPreparation;
  std::vector<std::uint64_t> polled, cancelledPreparations;
  std::map<std::uint64_t, Reservation> preparations;
  std::vector<Clock::time_point> preparationDeadlines;
  State completion = State::Accepted;
  bool asynchronous = false, waiting = true, throwPoll = false, coalesceHover = false;
  std::optional<Result> forcedPoll;
  std::uint64_t nextTicket = 0;
  int aborted = 0;
  bool accept = true, commitSucceeds = true, alive = true, throwReserve = false;
  std::uint64_t nextOwner = 41;
  Result reserve(
      const Captured &captured, const deskflow::windowshare::OriginalInputRouteDecision &route,
      const deskflow::windowshare::SelectedPeer &, std::uint64_t existingOwner, Clock::time_point deadline
  ) override
  {
    requestedOwners.push_back(existingOwner);
    if (onReserve)
      onReserve();
    if (throwReserve)
      throw std::runtime_error("reserve failed");
    if (!accept)
      return {};
    const auto owner = forcedOwner.value_or(existingOwner ? existingOwner : nextOwner);
    reserved.push_back({captured, route, owner});
    const Reservation reservation{owner, reserved.size()};
    if (asynchronous) {
      preparations.emplace(++nextTicket, reservation);
      preparationDeadlines.push_back(deadline);
      return Result::pending(nextTicket);
    }
    return Result::accepted(reservation);
  }
  Result poll(std::uint64_t ticket) override
  {
    polled.push_back(ticket);
    if (onPoll)
      onPoll();
    if (throwPoll)
      throw std::runtime_error("poll failed");
    if (forcedPoll)
      return *forcedPoll;
    if (waiting)
      return Result::pending(ticket);
    const auto found = preparations.find(ticket);
    if (found == preparations.end())
      return Result::revoked(ticket);
    const auto reservation = found->second;
    preparations.erase(found);
    if (completion == State::Accepted)
      return Result::accepted(reservation, ticket);
    return {completion, ticket, std::nullopt};
  }
  void cancelPreparation(std::uint64_t ticket) noexcept override
  {
    cancelledPreparations.push_back(ticket);
    preparations.erase(ticket);
    if (onCancelPreparation)
      onCancelPreparation();
  }
  void abort() noexcept override
  {
    ++aborted;
    alive = false;
    preparations.clear();
  }
  bool commit(const Reservation &reservation) override
  {
    if (onCommit)
      onCommit();
    if (commitSucceeds)
      committed.push_back(reserved.at(reservation.nonce - 1));
    return commitSucceeds;
  }
  void cancel(const Reservation &reservation) noexcept override
  {
    cancelled.push_back(reservation.owner);
  }
  void release(std::uint64_t owner) noexcept override
  {
    released.push_back(owner);
  }
  bool live(std::uint64_t) const noexcept override
  {
    return alive;
  }
  bool canCoalesceHoverMotion() const noexcept override
  {
    return coalesceHover;
  }
  void preview(std::span<const Preview> values) override
  {
    previewed.assign(values.begin(), values.end());
  }
};

struct OriginalDispatchFixture : RoutingFixture
{
  using Captured = deskflow::windowshare::OriginalInputCaptured;
  using Record = deskflow::windowshare::OriginalInputRecord;
  deskflow::windowshare::OriginalInputStream stream;
  std::shared_ptr<deskflow::windowshare::OriginalInputRoutes> routes =
      std::make_shared<deskflow::windowshare::OriginalInputRoutes>();
  std::shared_ptr<deskflow::windowshare::OriginalInputCapturedInbox> inbox =
      std::make_shared<deskflow::windowshare::OriginalInputCapturedInbox>(stream.epoch());
  std::shared_ptr<RecordingOriginalDispatcher> dispatcher = std::make_shared<RecordingOriginalDispatcher>();
  std::uint64_t sequence = 0;
  bool install(bool enable = true)
  {
    return server->setOriginalInputRoutes(routes) && server->setOriginalInputCapturedInbox(inbox) &&
           (!enable || server->setOriginalInputDispatcher(dispatcher));
  }
  Captured capture(decltype(Record::payload) payload, std::uint16_t parts = 1, bool uncertain = false)
  {
    Record record{stream.epoch().id(), ++sequence, sequence, 0, std::move(payload)};
    if (!stream.push(record))
      throw std::runtime_error("test capture failed");
    auto delivery = stream.pop();
    if (!delivery)
      throw std::runtime_error("test capture missing");
    return {*delivery, {{record.epoch, record.sequence, 0}, parts, uncertain}};
  }
  bool dispatch(EventTypes type, void *data, const Captured &captured, std::uint16_t part = 1)
  {
    return events.dispatchEvent(Event(
        type, primary.getEventTarget(), data, Event::EventFlags::DontFreeData,
        {captured.input.record.epoch, captured.input.record.sequence, part}
    ));
  }
};

struct FencedOriginalDispatcher : RecordingOriginalDispatcher
{
  std::uint64_t fencedRecipient = 0;
  bool hostReleased = false, coreReleased = false;
  mutable std::function<void()> onReadiness;
  bool legacyAcquisitionReady(std::uint64_t recipient) const noexcept override
  {
    if (auto callback = std::exchange(onReadiness, {}))
      callback();
    return recipient != fencedRecipient || (hostReleased && coreReleased);
  }
};

} // namespace

void ServerTests::initTestCase()
{
  m_arch.init();
  // Production key handlers consult AppUtil. Supply only a fixed language;
  // do not query an OS keyboard layout or initialize a native application.
  struct RoutingAppUtil : AppUtil
  {
    int run() override
    {
      return 0;
    }
    std::vector<std::string> getKeyboardLayoutList() override
    {
      return {"en"};
    }
    std::string getCurrentLanguageCode() override
    {
      return "en";
    }
  };
  static RoutingAppUtil appUtil;
  // Config and Computer consult Settings. Give this process disposable files
  // before constructing either, so tests never run configured enter/exit commands.
  static QTemporaryDir settings;
  QVERIFY(settings.isValid());
  qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
  qputenv("XDG_STATE_HOME", settings.path().toUtf8());
  QDir(settings.path()).mkpath(kAppName);
  Settings::setSettingsFile(settings.filePath("routing.conf"));
  Settings::setStateFile(settings.filePath("routing.state"));
  // Core IPC receives notifications but never listens on a socket in these tests.
  new deskflow::core::ipc::CoreIpcServer(this);
}

void ServerTests::originalCapture_legacyKeyExpansion()
{
  using namespace deskflow::windowshare;
  struct Keys : MockEventQueue
  {
    std::vector<Event> events;
    void addEvent(Event &&event) override
    {
      events.push_back(std::move(event));
    }
    ~Keys() override
    {
      for (const auto &event : events)
        delete static_cast<IKeyState::KeyInfo *>(event.getData());
    }
  } events;
  MockKeyState keys(events);
  keys.setHalfDuplexMask(KeyModifierCapsLock);
  {
    OriginalInputStampScope scope({81, 1, 0});
    keys.sendKeyEvent(nullptr, true, false, kKeyCapsLock, KeyModifierCapsLock, 1, 57);
    QCOMPARE(scope.translation().parts, std::uint16_t(2));
    QCOMPARE(events.events.size(), std::size_t(2));
    QCOMPARE(events.events[0].getType(), EventTypes::KeyStateKeyDown);
    QCOMPARE(events.events[1].getType(), EventTypes::KeyStateKeyUp);
    QCOMPARE(events.events[0].originalInputStamp(), (OriginalInputStamp{81, 1, 1}));
    QCOMPARE(events.events[1].originalInputStamp(), (OriginalInputStamp{81, 1, 2}));
  }
  {
    OriginalInputStampScope scope({81, 2, 0});
    keys.sendKeyEvent(nullptr, true, true, kKeyCapsLock, KeyModifierCapsLock, 3, 57);
    QCOMPARE(scope.translation().parts, std::uint16_t(0));
    QCOMPARE(events.events.size(), std::size_t(2));
  }
  {
    OriginalInputStampScope scope({81, 3, 0});
    keys.sendKeyEvent(nullptr, true, true, 'a', 0, 5, 30);
    QCOMPARE(scope.translation().parts, std::uint16_t(1));
    QCOMPARE(events.events.back().getType(), EventTypes::KeyStateKeyRepeat);
    QCOMPARE(events.events.back().originalInputStamp(), (OriginalInputStamp{81, 3, 1}));
    QCOMPARE(static_cast<IKeyState::KeyInfo *>(events.events.back().getData())->m_count, 5);
  }
}

void ServerTests::originalDispatch_captureRaceAndPackets()
{
  using namespace deskflow::windowshare;
  for (bool captureFirst : {false, true}) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    server.switchComputer(f.firstClient, 100, 200, false);
    f.clear();
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    OriginalInputKey raw{7, 29};
    const auto down = f.capture(raw);
    if (captureFirst) {
      QVERIFY(f.inbox->publish(down));
      f.events.tick();
    }
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
    QVERIFY(f.first->take().isEmpty());
    if (!captureFirst) {
      QVERIFY(f.dispatcher->committed.empty());
      QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
      QVERIFY(!f.routes->pop());
      QVERIFY(f.inbox->publish(down));
      f.events.tick();
    }
    QCOMPARE(f.dispatcher->committed.size(), std::size_t(1));
    QCOMPARE(f.dispatcher->committed.front().captured.input.record.sequence, down.input.record.sequence);
    QCOMPARE(
        std::get<OriginalInputKey>(f.dispatcher->committed.front().captured.input.record.payload).keyboardType, 7u
    );
    const auto held = server.m_inputRoutes.repeat(Server::InputRoutes::keyControl('a', 30));
    QVERIFY(held && held->originalOwner == 41 && held->recipients.empty());
    auto decision = f.routes->pop();
    QVERIFY(decision && decision->outcome == OriginalInputRouteOutcome::Original);
    raw.repeat = true;
    raw.transition = OriginalKeyTransition::Repeat;
    const auto repeat = f.capture(raw);
    QVERIFY(f.inbox->publish(repeat));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyRepeat, &key, repeat));
    raw.repeat = false;
    raw.transition = OriginalKeyTransition::Down;
    raw.kind = OriginalKeyKind::Up;
    raw.transition = OriginalKeyTransition::Up;
    const auto up = f.capture(raw);
    QVERIFY(f.inbox->publish(up));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyUp, &key, up));
    QCOMPARE(f.dispatcher->committed.size(), std::size_t(3));
    QVERIFY(f.first->take().isEmpty()); // Actual DKDN/DKRP/DKUP writers were intercepted.
    QVERIFY(f.second->take().isEmpty());
    QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
  }
}

void ServerTests::originalDispatch_denialExpansionAndFiltered()
{
  using namespace deskflow::windowshare;
  OriginalDispatchFixture f;
  auto &server = *f.server;
  QVERIFY(f.install());
  f.first->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  f.clear();
  f.dispatcher->accept = false;
  IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
  auto raw = OriginalInputKey{};
  auto captured = f.capture(raw);
  QVERIFY(f.inbox->publish(captured));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, captured));
  QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("0061 0000 001e"));
  f.dispatcher->accept = true;
  raw.repeat = true;
  raw.transition = OriginalKeyTransition::Repeat;
  captured = f.capture(raw);
  QVERIFY(f.inbox->publish(captured));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyRepeat, &key, captured));
  QCOMPARE(f.first->take(), "DKRP" + QByteArray::fromHex("0061 0000 0001 001e"));
  raw.repeat = false;
  raw.transition = OriginalKeyTransition::Down;
  raw.kind = OriginalKeyKind::Up;
  raw.transition = OriginalKeyTransition::Up;
  captured = f.capture(raw);
  QVERIFY(f.inbox->publish(captured));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyUp, &key, captured));
  QCOMPARE(f.first->take(), "DKUP" + QByteArray::fromHex("0061 0000 001e"));
  QVERIFY(f.dispatcher->committed.empty()); // Denied press cannot change paths at repeat/up.

  raw.kind = OriginalKeyKind::Down;
  raw.transition = OriginalKeyTransition::Down;
  captured = f.capture(raw, 2);
  QVERIFY(f.inbox->publish(captured));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, captured, 1));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyUp, &key, captured, 2));
  QCOMPARE(
      f.first->take(), "DKDN" + QByteArray::fromHex("0061 0000 001e") + "DKUP" + QByteArray::fromHex("0061 0000 001e")
  );
  captured = f.capture(raw, 1, true);
  QVERIFY(f.inbox->publish(captured));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, captured));
  QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("0061 0000 001e"));
  server.onKeyUp('a', 0, 30, nullptr);
  f.clear();
  QVERIFY(f.inbox->publish(f.capture(raw, 0)));
  f.events.tick();
  QVERIFY(!server.m_originalDispatchTerminal);

  // A filtered later event must not overtake a waiting Server decision.
  while (f.routes->pop()) {
  }
  const auto waiting = f.capture(raw);
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, waiting));
  struct Always : InputFilter::Condition
  {
    Condition *clone() const override
    {
      return new Always;
    }
    std::string format() const override
    {
      return "always";
    }
    InputFilter::FilterStatus match(const Event &) override
    {
      return InputFilter::FilterStatus::Activate;
    }
  };
  struct Consume : InputFilter::Action
  {
    Action *clone() const override
    {
      return new Consume;
    }
    std::string format() const override
    {
      return "consume";
    }
    void perform(const Event &) override
    {
    }
  };
  InputFilter::Rule rule(new Always);
  rule.adoptAction(new Consume, true);
  server.m_inputFilter->addFilterRule(rule);
  const auto filtered = f.capture(raw);
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, filtered));
  QVERIFY(!f.routes->pop());
  QVERIFY(f.inbox->publish(waiting));
  QVERIFY(f.inbox->publish(filtered));
  f.events.tick();
  QCOMPARE(f.dispatcher->committed.size(), std::size_t(1));
  auto first = f.routes->pop();
  auto second = f.routes->pop();
  QVERIFY(first && second);
  QCOMPARE(first->stamp.sequence, waiting.input.record.sequence);
  QCOMPARE(first->outcome, OriginalInputRouteOutcome::Original);
  QCOMPARE(second->stamp.sequence, filtered.input.record.sequence);
  QCOMPARE(second->outcome, OriginalInputRouteOutcome::Filtered);
  QVERIFY(f.first->take().isEmpty());
  QVERIFY(f.routes->live());
}

void ServerTests::originalDispatch_ownersFocusAndReconnect()
{
  using namespace deskflow::windowshare;
  for (bool originallyAccepted : {false, true}) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    f.second->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    f.clear();
    f.dispatcher->accept = originallyAccepted;
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    auto raw = OriginalInputKey{};
    auto captured = f.capture(raw);
    QVERIFY(f.inbox->publish(captured));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, captured));
    server.switchComputer(f.secondClient, 400, 500, false);
    f.clear();
    f.dispatcher->accept = true;
    raw.repeat = true;
    raw.transition = OriginalKeyTransition::Repeat;
    captured = f.capture(raw);
    QVERIFY(f.inbox->publish(captured));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyRepeat, &key, captured));
    if (originallyAccepted) {
      QVERIFY(f.first->take().isEmpty());
      QCOMPARE(f.dispatcher->released, std::vector<std::uint64_t>{41});
    } else {
      QCOMPARE(f.first->take(), "DKRP" + QByteArray::fromHex("0061 0000 0001 001e"));
    }
    QVERIFY(f.second->take().isEmpty());
    server.handleClientDisconnected(f.firstClient);
    auto replacement = std::make_shared<WireCapture>();
    replacement->authenticate();
    auto *client = new RoutingClient("first", &f.events, replacement);
    server.adoptClient(client);
    server.switchComputer(client, 100, 100, false);
    replacement->take();
    f.clear();
    raw.repeat = false;
    raw.transition = OriginalKeyTransition::Down;
    captured = f.capture(raw);
    QVERIFY(f.inbox->publish(captured));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, captured));
    raw.kind = OriginalKeyKind::Up;
    raw.transition = OriginalKeyTransition::Up;
    captured = f.capture(raw);
    QVERIFY(f.inbox->publish(captured));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyUp, &key, captured));
    QVERIFY(replacement->take().isEmpty());
    QVERIFY(f.second->take().isEmpty());
    QCOMPARE(f.dispatcher->committed.size(), originallyAccepted ? std::size_t(1) : std::size_t(0));
    QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
  }
}

void ServerTests::originalDispatch_mixedOrderingAndCoordinates()
{
  using namespace deskflow::windowshare;
  OriginalDispatchFixture f;
  auto &server = *f.server;
  QVERIFY(f.install());
  f.first->authenticate();
  server.switchComputer(f.firstClient, 100, 200, false);
  f.clear();
  IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
  IPlatformComputer::MotionInfo motion{7, -3};
  IPlatformComputer::ButtonInfo button(1, 0);
  IPlatformComputer::WheelInfo wheel{-120, 240};
  const auto down = f.capture(OriginalInputKey{});
  const auto move = f.capture(OriginalInputMotion{7, -3});
  const auto press = f.capture(OriginalInputButton{0, true, 2});
  OriginalInputScroll rawScroll;
  rawScroll.points = {-1, 2};
  const auto scroll = f.capture(rawScroll);
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
  QVERIFY(f.dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion, move));
  QVERIFY(f.dispatch(EventTypes::PrimaryComputerButtonDown, &button, press));
  QVERIFY(f.dispatch(EventTypes::PrimaryComputerWheel, &wheel, scroll));
  // A configured, unstamped input is also ordered behind these decisions.
  server.onKeyDown('z', 0, 44, "en", nullptr);
  QVERIFY(f.first->take().isEmpty());
  QCOMPARE(server.m_x, 107);
  QCOMPARE(server.m_y, 197);
  QVERIFY(f.inbox->publish(down));
  QVERIFY(f.inbox->publish(move));
  QVERIFY(f.inbox->publish(press));
  QVERIFY(f.inbox->publish(scroll));
  f.events.tick();
  QCOMPARE(f.dispatcher->committed.size(), std::size_t(4));
  for (std::size_t i = 0; i != 4; ++i)
    QCOMPARE(f.dispatcher->committed[i].captured.input.record.sequence, i + 1);
  QCOMPARE(f.dispatcher->committed[1].route.x, 107);
  QCOMPARE(f.dispatcher->committed[1].route.y, 197);
  QCOMPARE(f.dispatcher->committed[2].route.x, 107);
  QCOMPARE(f.dispatcher->committed[2].route.y, 197);
  QCOMPARE(f.first->take(), "DMMV" + QByteArray::fromHex("006b 00c5") + "DKDN" + QByteArray::fromHex("007a 0000 002c"));
  const auto release = f.capture(OriginalInputButton{0, false, 2});
  QVERIFY(f.inbox->publish(release));
  QVERIFY(f.dispatch(EventTypes::PrimaryComputerButtonUp, &button, release));
  QCOMPARE(f.dispatcher->committed.size(), std::size_t(5));
  QVERIFY(f.first->take().isEmpty());
}

void ServerTests::originalDispatch_timeoutOverflowAndFeatureDefault()
{
  using namespace deskflow::windowshare;
  for (int mode = 0; mode != 3; ++mode) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install(mode != 0));
    f.first->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    f.clear();
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    auto captured = f.capture(OriginalInputKey{});
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, captured));
    if (mode == 0) {
      QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("0061 0000 001e"));
      QVERIFY(f.inbox->publish(captured));
      f.events.tick();
      QVERIFY(server.m_originalCompleted.empty());
      QVERIFY(f.dispatcher->reserved.empty());
      continue;
    }
    QVERIFY(f.first->take().isEmpty());
    if (mode == 1) {
      server.drainOriginalInput(Server::OriginalClock::now() + Server::originalDispatchTimeout);
      QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("0061 0000 001e"));
      QVERIFY(f.inbox->publish(captured));
      f.events.tick();
      QVERIFY(f.dispatcher->committed.empty());
      server.onKeyUp('a', 0, 30, nullptr);
      QCOMPARE(f.first->take(), "DKUP" + QByteArray::fromHex("0061 0000 001e"));
    } else {
      for (std::size_t i = 0; i < Server::originalPendingCapacity; ++i) {
        IPlatformComputer::WheelInfo wheel{0, 120};
        captured = f.capture(OriginalInputScroll{});
        QVERIFY(f.dispatch(EventTypes::PrimaryComputerWheel, &wheel, captured));
      }
      QVERIFY(server.m_originalDispatchTerminal);
      QVERIFY(server.m_originalPending.empty());
      QVERIFY(!f.first->take().contains("DKDN")); // Overflow must not resurrect queued presses.
      server.onKeyDown('a', 0, 30, "en", nullptr);
      server.onKeyUp('a', 0, 30, nullptr);
      QVERIFY(f.first->take().isEmpty());
    }
    QVERIFY(server.m_originalDispatchTerminal);
    QVERIFY(f.dispatcher->committed.empty());
  }
}

void ServerTests::originalDispatch_reentrantFailureAndDestruction()
{
  using namespace deskflow::windowshare;
  for (int mode = 0; mode != 4; ++mode) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    f.second->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    f.clear();
    if (mode == 0)
      f.dispatcher->commitSucceeds = false;
    else if (mode == 1)
      f.dispatcher->onReserve = [&] { server.switchComputer(f.secondClient, 300, 400, false); };
    else if (mode == 2)
      f.dispatcher->onReserve = [&] { f.server.reset(); };
    else
      f.dispatcher->throwReserve = true;
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    const auto down = f.capture(OriginalInputKey{});
    QVERIFY(f.inbox->publish(down));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
    QVERIFY(!f.first->take().contains("DKDN"));
    QVERIFY(!f.second->take().contains("DKDN"));
    QVERIFY(f.dispatcher->committed.empty());
    if (mode == 2) {
      QVERIFY(!f.server);
      QCOMPARE(f.dispatcher->cancelled.size(), std::size_t(1));
      continue;
    }
    f.dispatcher->onReserve = {};
    server.onKeyDown('a', 0, 30, "en", nullptr);
    server.onKeyRepeat('a', 0, 1, 30, "en");
    server.onKeyUp('a', 0, 30, nullptr);
    QVERIFY(f.first->take().isEmpty());
    QVERIFY(f.second->take().isEmpty());
    QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
  }
}

void ServerTests::originalDispatch_pendingOrderingAndCaptureArrival()
{
  using namespace deskflow::windowshare;
  OriginalDispatchFixture f;
  auto &server = *f.server;
  QVERIFY(f.install());
  f.first->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  f.clear();
  f.dispatcher->asynchronous = true;
  OriginalInputRouteJoin history(f.stream.epoch(), f.routes);
  const auto capturedAt = Server::OriginalClock::now();
  IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
  const auto down = f.capture(OriginalInputKey{});
  QVERIFY(history.capture(down, capturedAt));
  QVERIFY(f.inbox->publish(down));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
  QCOMPARE(server.m_originalPending.size(), std::size_t(1));
  QCOMPARE(server.m_originalPending.front().preparation, std::uint64_t(1));
  QVERIFY(!f.routes->pop()); // A pending attempt is not an early routing decision.
  OriginalInputKey raw;
  raw.repeat = true;
  raw.transition = OriginalKeyTransition::Repeat;
  const auto repeat = f.capture(raw);
  QVERIFY(history.capture(repeat, capturedAt));
  QVERIFY(f.inbox->publish(repeat));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyRepeat, &key, repeat));
  raw.kind = OriginalKeyKind::Up;
  raw.transition = OriginalKeyTransition::Up;
  raw.repeat = false;
  const auto up = f.capture(raw);
  QVERIFY(history.capture(up, capturedAt));
  QVERIFY(f.inbox->publish(up));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyUp, &key, up));
  QCOMPARE(server.m_originalPending.size(), std::size_t(3));
  QVERIFY(f.first->take().isEmpty());
  QVERIFY(f.dispatcher->committed.empty());
  QCOMPARE(f.dispatcher->reserved.size(), std::size_t(1));
  const auto deadline = server.m_originalPending.front().preparationDeadline;
  for (int i = 0; i != 4; ++i)
    f.events.tick();
  QCOMPARE(server.m_originalPending.front().preparationDeadline, deadline);
  QCOMPARE(f.dispatcher->preparationDeadlines.size(), std::size_t(1));
  f.dispatcher->waiting = false;
  f.dispatcher->asynchronous = false;
  // All captures arrived before their 100 ms deadline. Waiting behind the
  // first native preparation must not turn the later two into capture losses,
  // nor revoke the shared route through its independent historical join.
  // The old 250 ms join expired while the 500 ms preparation was still valid.
  const auto completedAt = deadline - std::chrono::milliseconds(50);
  QVERIFY(!history.pop(completedAt));
  QVERIFY(history.live());
  server.drainOriginalInput(completedAt);
  QVERIFY(!server.m_originalDispatchTerminal);
  QVERIFY(server.m_originalPending.empty());
  QCOMPARE(f.dispatcher->committed.size(), std::size_t(3));
  QCOMPARE(f.dispatcher->requestedOwners, (std::vector<std::uint64_t>{0, 41, 41}));
  for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
    const auto decision = f.routes->pop();
    QVERIFY(decision);
    QCOMPARE(decision->stamp.sequence, sequence);
    QCOMPARE(decision->outcome, OriginalInputRouteOutcome::Original);
    QVERIFY(history.decision(*decision, completedAt));
    const auto observed = history.pop(completedAt);
    QVERIFY(observed && observed->live());
    QCOMPARE(observed->route.outcome, OriginalInputRouteOutcome::Original);
    QCOMPARE(f.dispatcher->committed.at(sequence - 1).captured.input.record.sequence, sequence);
  }
  QVERIFY(!f.routes->pop());
  QVERIFY(f.first->take().isEmpty());
  QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
}

void ServerTests::originalDispatch_hoverCompressionPreservesBarriersAndHistory()
{
  using namespace deskflow::windowshare;
  OriginalDispatchFixture f;
  auto &server = *f.server;
  QVERIFY(f.install());
  f.first->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  f.clear();
  f.dispatcher->asynchronous = true;
  f.dispatcher->coalesceHover = true;
  IPlatformComputer::MotionInfo motion{1, 0};
  for (unsigned i = 0; i < 120; ++i) {
    const auto capture = f.capture(OriginalInputMotion{1, 0});
    QVERIFY(f.inbox->publish(capture));
    QVERIFY(f.dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion, capture));
    QVERIFY(server.m_originalPending.size() <= 2);
  }
  // A reserved point never moves under its outstanding preparation. The only
  // other retained point already includes all 120 Server motion deltas.
  QCOMPARE(f.dispatcher->reserved.size(), std::size_t(1));
  QCOMPARE(f.dispatcher->reserved.front().route.x, 101);
  QCOMPARE(server.m_originalPending.back().route.x, 220);
  QCOMPARE(server.m_originalPending.back().route.stamp.sequence, std::uint64_t(120));
  QVERIFY(!f.routes->pop());
  f.dispatcher->waiting = false;
  f.dispatcher->asynchronous = false;
  f.events.tick();
  QVERIFY(!server.m_originalDispatchTerminal);
  QCOMPARE(f.dispatcher->committed.size(), std::size_t(2));
  QCOMPARE(f.dispatcher->committed.back().captured.input.record.sequence, std::uint64_t(120));
  QCOMPARE(std::get<OriginalInputMotion>(f.dispatcher->committed.back().captured.input.record.payload).dx, 1);
  for (std::uint64_t sequence = 1; sequence <= 120; ++sequence) {
    const auto decision = f.routes->pop();
    QVERIFY(decision);
    QCOMPARE(decision->stamp.sequence, sequence);
    QCOMPARE(
        decision->outcome,
        sequence == 1 || sequence == 120 ? OriginalInputRouteOutcome::Original : OriginalInputRouteOutcome::Coalesced
    );
  }
  // Held keys are a barrier even when mouse records have no drag button.
  IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
  auto capture = f.capture(OriginalInputKey{});
  QVERIFY(f.inbox->publish(capture));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, capture));
  f.dispatcher->asynchronous = true;
  f.dispatcher->waiting = true;
  for (unsigned i = 0; i < 4; ++i) {
    capture = f.capture(OriginalInputMotion{1, 0});
    QVERIFY(f.inbox->publish(capture));
    QVERIFY(f.dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion, capture));
  }
  QCOMPARE(server.m_originalPending.size(), std::size_t(4));
  f.dispatcher->asynchronous = false;
  f.dispatcher->waiting = false;
  f.events.tick();
  QCOMPARE(f.dispatcher->committed.size(), std::size_t(7));
  QVERIFY(server.m_originalPending.empty());
}

void ServerTests::originalDispatch_inlineTerminalReservationContract()
{
  using namespace deskflow::windowshare;
  struct InlineDispatcher : RecordingOriginalDispatcher
  {
    bool malformed = false;
    Result reserve(
        const Captured &captured, const OriginalInputRouteDecision &route, const SelectedPeer &selected,
        std::uint64_t owner, Clock::time_point deadline
    ) override
    {
      auto result = RecordingOriginalDispatcher::reserve(captured, route, selected, owner, deadline);
      if (malformed && result.state == State::Accepted)
        result.ticket = nextTicket + 1;
      return result;
    }
  };
  for (bool malformed : {false, true}) {
    OriginalDispatchFixture f;
    auto dispatcher = std::make_shared<InlineDispatcher>();
    f.dispatcher = dispatcher;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    f.clear();
    dispatcher->asynchronous = true;
    dispatcher->waiting = false;
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    const auto down = f.capture(OriginalInputKey{});
    QVERIFY(f.inbox->publish(down));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
    QCOMPARE(server.m_originalPending.front().preparation, std::uint64_t(1));
    f.events.tick();
    QCOMPARE(dispatcher->committed.size(), std::size_t(1));
    // Model a prefetched second record: its reservation nonce remains unique,
    // but an inline terminal result has no asynchronous preparation ticket.
    dispatcher->asynchronous = false;
    dispatcher->malformed = malformed;
    OriginalInputKey rawUp;
    rawUp.kind = OriginalKeyKind::Up;
    rawUp.transition = OriginalKeyTransition::Up;
    const auto up = f.capture(rawUp);
    QVERIFY(f.inbox->publish(up));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyUp, &key, up));
    QCOMPARE(server.m_originalDispatchTerminal, malformed);
    QCOMPARE(dispatcher->committed.size(), malformed ? std::size_t(1) : std::size_t(2));
    QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
    QVERIFY(server.m_originalPending.empty());
    QVERIFY(f.first->take().isEmpty()); // Neither outcome falls through to DKUP.
    auto decision = f.routes->pop();
    QVERIFY(decision && decision->outcome == OriginalInputRouteOutcome::Original);
    decision = f.routes->pop();
    QVERIFY(decision);
    QCOMPARE(decision->outcome, malformed ? OriginalInputRouteOutcome::Failed : OriginalInputRouteOutcome::Original);
  }
}

void ServerTests::originalDispatch_retainedLookaheadPreventsHoverReplacement()
{
  using namespace deskflow::windowshare;
  OriginalDispatchFixture f;
  auto &server = *f.server;
  QVERIFY(f.install());
  f.first->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  f.clear();
  f.dispatcher->asynchronous = true;
  f.dispatcher->coalesceHover = true;
  bool retained = false;
  f.dispatcher->onPoll = [&] {
    if (!retained && f.dispatcher->previewed.size() == 2) {
      // The production coordinator now closes this eligibility when a map
      // batch retains future points, before their per-record reserve calls.
      retained = true;
      f.dispatcher->coalesceHover = false;
    }
  };
  IPlatformComputer::WheelInfo wheel{0, 120};
  OriginalInputScroll raw;
  raw.points = {0, 1};
  const auto scroll = f.capture(raw);
  QVERIFY(f.inbox->publish(scroll));
  QVERIFY(f.dispatch(EventTypes::PrimaryComputerWheel, &wheel, scroll));
  IPlatformComputer::MotionInfo motion{1, 0};
  const auto first = f.capture(OriginalInputMotion{1, 0});
  QVERIFY(f.inbox->publish(first));
  QVERIFY(f.dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion, first));
  QVERIFY(retained);
  QCOMPARE(f.dispatcher->previewed[1].route.x, 101);
  const auto second = f.capture(OriginalInputMotion{1, 0});
  QVERIFY(f.inbox->publish(second));
  QVERIFY(f.dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion, second));
  QCOMPARE(server.m_originalPending.size(), std::size_t(3));
  QCOMPARE(f.dispatcher->previewed.size(), std::size_t(3));
  QCOMPARE(f.dispatcher->previewed[1].captured.input.record.sequence, first.input.record.sequence);
  QCOMPARE(f.dispatcher->previewed[2].captured.input.record.sequence, second.input.record.sequence);
  QVERIFY(!f.routes->pop());
  f.dispatcher->asynchronous = false;
  f.dispatcher->waiting = false;
  f.events.tick();
  QVERIFY(!server.m_originalDispatchTerminal && server.m_originalPending.empty());
  QCOMPARE(f.dispatcher->committed.size(), std::size_t(3));
  for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
    QCOMPARE(f.dispatcher->committed[sequence - 1].captured.input.record.sequence, sequence);
    const auto decision = f.routes->pop();
    QVERIFY(
        decision && decision->stamp.sequence == sequence && decision->outcome == OriginalInputRouteOutcome::Original
    );
  }
  QCOMPARE(f.first->take(), "DMMV" + QByteArray::fromHex("0065 0064") + "DMMV" + QByteArray::fromHex("0066 0064"));
}

void ServerTests::originalDispatch_ineligibleInputWaitsForExactCleanup()
{
  using namespace deskflow::windowshare;
  // Missing stamps, expansion and uncertainty are all ordinary legacy inputs
  // when no handoff exists. None certifies cleanup of a closed full gate.
  for (int mode = 0; mode != 3; ++mode) {
    OriginalDispatchFixture f;
    auto dispatcher = std::make_shared<FencedOriginalDispatcher>();
    f.dispatcher = dispatcher;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    dispatcher->fencedRecipient = server.selectedPeer()->recipient();
    f.clear();
    IPlatformComputer::KeyInfo owned{'a', 0, 30, 1, ""};
    const auto down = f.capture(OriginalInputKey{});
    QVERIFY(f.inbox->publish(down));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &owned, down));
    QCOMPARE(dispatcher->committed.size(), std::size_t(1));
    const auto deliver = [&](EventTypes type, void *data, decltype(OriginalInputRecord::payload) payload) {
      if (mode == 0) {
        QVERIFY(f.events.dispatchEvent(Event(type, f.primary.getEventTarget(), data, Event::EventFlags::DontFreeData)));
      } else {
        const auto captured = f.capture(std::move(payload), mode == 1 ? 2 : 1, mode == 2);
        QVERIFY(f.inbox->publish(captured));
        QVERIFY(f.dispatch(type, data, captured));
        if (mode == 1)
          QVERIFY(f.dispatch(type, data, captured, 2));
      }
    };
    IPlatformComputer::KeyInfo key{'b', 0, 48, 1, ""};
    OriginalInputKey rawKey;
    rawKey.code = 11;
    deliver(EventTypes::KeyStateKeyDown, &key, rawKey);
    IPlatformComputer::ButtonInfo button(1, 0);
    deliver(EventTypes::PrimaryComputerButtonDown, &button, OriginalInputButton{0, true, 1});
    IPlatformComputer::WheelInfo wheel{0, 120};
    deliver(EventTypes::PrimaryComputerWheel, &wheel, OriginalInputScroll{});
    IPlatformComputer::MotionInfo motion{1, 0};
    deliver(EventTypes::PrimaryComputerMotionOnSecondary, &motion, OriginalInputMotion{1, 0});
    server.onKeyDown(kKeySetModifiers, KeyModifierShift, 0, "", nullptr);
    server.m_relativeMoves = server.m_lockedToComputer = true;
    server.onMouseMoveSecondary(2, 3);
    server.m_relativeMoves = server.m_lockedToComputer = false;
    QVERIFY(f.first->take().isEmpty());
    QCOMPARE(dispatcher->released, std::vector<std::uint64_t>{41});
    QCOMPARE(dispatcher->committed.size(), std::size_t(1));
    QVERIFY(!server.m_originalDispatchTerminal);
    const auto press = server.m_inputRoutes.repeat(Server::InputRoutes::keyControl('b', 48));
    const auto heldButton = server.m_inputRoutes.repeat({Server::InputRoutes::Kind::Button, 1});
    QVERIFY(press && !press->originalOwner && press->recipients.empty());
    QVERIFY(heldButton && !heldButton->originalOwner && heldButton->recipients.empty());
    // One acknowledgement is insufficient, including for pointer writes that
    // would implicitly resume the Core seal before Host cleanup completes.
    dispatcher->hostReleased = true;
    server.onKeyDown('c', 0, 46, "", nullptr);
    server.onMouseMoveSecondary(1, 0);
    server.onMouseWheel(0, 120);
    QVERIFY(f.first->take().isEmpty());
    dispatcher->coreReleased = true;
    server.onKeyDown('d', 0, 32, "", nullptr);
    server.onKeyUp('d', 0, 32, nullptr);
    QCOMPARE(
        f.first->take(), "DKDN" + QByteArray::fromHex("0064 0000 0020") + "DKUP" + QByteArray::fromHex("0064 0000 0020")
    );
    server.onKeyDown('b', 0, 48, "", nullptr);
    QVERIFY(f.first->take().isEmpty()); // A held tombstone cannot become a new press.
    if (mode) {
      rawKey.kind = OriginalKeyKind::Up;
      rawKey.transition = OriginalKeyTransition::Up;
      QVERIFY(f.inbox->publish(f.capture(rawKey, 0)));
      QVERIFY(f.inbox->publish(f.capture(OriginalInputButton{0, false, 1}, 0)));
      f.events.tick(); // No translated ups: physical completion retires the exact tombstones.
    } else {
      server.onKeyUp('b', 0, 48, nullptr);
      server.onMouseUp(1);
    }
    server.onKeyUp('a', 0, 30, nullptr);
    server.onKeyUp('c', 0, 46, nullptr);
    QVERIFY(f.first->take().isEmpty());
    QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
    server.onKeyDown('b', 0, 48, "", nullptr);
    server.onKeyUp('b', 0, 48, nullptr);
    QCOMPARE(
        f.first->take(), "DKDN" + QByteArray::fromHex("0062 0000 0030") + "DKUP" + QByteArray::fromHex("0062 0000 0030")
    );
  }
}

void ServerTests::originalDispatch_legacyReleaseAndOtherRecipientsSurviveGate()
{
  using namespace deskflow::windowshare;
  OriginalDispatchFixture f;
  auto dispatcher = std::make_shared<FencedOriginalDispatcher>();
  f.dispatcher = dispatcher;
  auto &server = *f.server;
  QVERIFY(f.install());
  f.first->authenticate();
  f.second->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  // Existing delivered ownership is still entitled to its exact release.
  server.onKeyDown('a', 0, 30, "", nullptr);
  server.onMouseDown(2);
  server.onKeyDown(kKeySetModifiers, KeyModifierShift, 0, "", nullptr);
  dispatcher->fencedRecipient = server.selectedPeer()->recipient();
  f.clear();
  server.onKeyRepeat('a', 0, 1, 30, "");
  QVERIFY(f.first->take().isEmpty());
  server.onKeyUp('a', 0, 30, nullptr);
  server.onMouseUp(2);
  server.onKeyDown(kKeyClearModifiers, KeyModifierShift, 0, "", nullptr);
  QCOMPARE(
      f.first->take(), "DKUP" + QByteArray::fromHex("0061 0000 001e") + "DMUP" + QByteArray(1, '\2') + "DKDN" +
                           QByteArray::fromHex("ee07 0001 0000")
  );
  QVERIFY(f.second->take().isEmpty());
  // A blocked first recipient must not disable an unrelated broadcast target.
  server.onKeyDown('z', 0, 44, "", "*");
  QVERIFY(f.first->take().isEmpty());
  QCOMPARE(f.second->take(), "DKDN" + QByteArray::fromHex("007a 0000 002c"));
  dispatcher->hostReleased = dispatcher->coreReleased = true;
  server.onKeyDown('z', 0, 44, "", "*");
  server.onKeyUp('z', 0, 44, nullptr);
  QVERIFY(f.first->take().isEmpty());
  QCOMPARE(
      f.second->take(), "DKDN" + QByteArray::fromHex("007a 0000 002c") + "DKUP" + QByteArray::fromHex("007a 0000 002c")
  );
  QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
}

void ServerTests::originalDispatch_closedGateSurvivesFlushAndTerminal()
{
  using namespace deskflow::windowshare;
  for (int mode = 0; mode != 4; ++mode) {
    OriginalDispatchFixture f;
    auto dispatcher = std::make_shared<FencedOriginalDispatcher>();
    f.dispatcher = dispatcher;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    f.second->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    dispatcher->fencedRecipient = server.selectedPeer()->recipient();
    f.clear();
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    if (mode < 2) {
      const auto captured = f.capture(OriginalInputKey{});
      QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, captured)); // Completion is still missing.
      QVERIFY(f.first->take().isEmpty());
      if (mode == 0)
        server.switchComputer(f.secondClient, 200, 300, false);
      else
        server.drainOriginalInput(server.m_originalPending.front().deadline);
      QCOMPARE(server.m_originalDispatchTerminal, mode == 1);
      QVERIFY(!f.first->take().contains("DKDN"));
      f.clear();
      server.onKeyDown('b', 0, 48, "", nullptr);
      server.onMouseMoveSecondary(1, 0);
      server.onMouseWheel(0, 120);
      QVERIFY(f.first->take().isEmpty());
      const auto second = f.second->take();
      if (mode == 0) {
        QVERIFY(second.contains("DKDN"));
        QVERIFY(second.contains("DMMV"));
        QVERIFY(second.contains("DMWM"));
      } else {
        QVERIFY(second.isEmpty());
        dispatcher->hostReleased = dispatcher->coreReleased = true;
        server.onKeyDown('c', 0, 46, "", nullptr);
        QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("0063 0000 002e"));
      }
    } else {
      dispatcher->onReadiness = [&] {
        if (mode == 2)
          server.switchComputer(f.secondClient, 200, 300, false);
        else
          f.server.reset();
      };
      server.onKeyDown('a', 0, 30, "", nullptr);
      QVERIFY(!f.first->take().contains("DKDN"));
      QVERIFY(!f.second->take().contains("DKDN"));
      if (mode == 2) {
        QVERIFY(server.m_active == f.secondClient);
        const auto press = server.m_inputRoutes.repeat(Server::InputRoutes::keyControl('a', 30));
        QVERIFY(press && press->recipients.empty());
      } else {
        QVERIFY(!f.server);
      }
    }
  }
}

void ServerTests::originalDispatch_pendingRefusalAndRawRelease()
{
  using namespace deskflow::windowshare;
  using State = Server::OriginalInputDispatcher::State;
  for (const auto completion : {State::Refused, State::Revoked}) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    f.clear();
    f.dispatcher->asynchronous = true;
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    const auto down = f.capture(OriginalInputKey{});
    QVERIFY(f.inbox->publish(down));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
    QVERIFY(f.first->take().isEmpty());
    f.dispatcher->waiting = false;
    f.dispatcher->completion = completion;
    f.dispatcher->asynchronous = false;
    f.events.tick();
    QVERIFY(server.m_originalPending.empty());
    const auto press = server.m_inputRoutes.repeat(Server::InputRoutes::keyControl('a', 30));
    QVERIFY(press);
    QVERIFY(!press->originalOwner);
    if (completion == State::Refused) {
      QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("0061 0000 001e"));
      QVERIFY(!press->recipients.empty());
      server.onKeyUp('a', 0, 30, nullptr);
      QCOMPARE(f.first->take(), "DKUP" + QByteArray::fromHex("0061 0000 001e"));
    } else {
      QVERIFY(f.first->take().isEmpty());
      QVERIFY(press->recipients.empty());
      QCOMPARE(server.m_originalKeyControls.size(), std::size_t(1));
      OriginalInputKey rawUp;
      rawUp.kind = OriginalKeyKind::Up;
      rawUp.transition = OriginalKeyTransition::Up;
      // No translated KeyUp: the raw completion still retires this exact
      // suppressed acquisition, rather than leaving a permanent held key.
      QVERIFY(f.inbox->publish(f.capture(rawUp, 0)));
      f.events.tick();
      QVERIFY(server.m_originalKeyControls.empty());
      QVERIFY(f.first->take().isEmpty());
    }
    QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
    QVERIFY(f.dispatcher->committed.empty());
  }
}

void ServerTests::originalDispatch_pendingCancellationLifetime()
{
  using namespace deskflow::windowshare;
  for (int mode = 0; mode != 5; ++mode) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    f.second->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    f.clear();
    f.dispatcher->asynchronous = true;
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    const auto down = f.capture(OriginalInputKey{});
    QVERIFY(f.inbox->publish(down));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
    const auto deadline = server.m_originalPending.front().preparationDeadline;
    if (mode == 0)
      server.drainOriginalInput(deadline);
    else if (mode == 1)
      server.switchComputer(f.secondClient, 300, 400, false);
    else if (mode == 2)
      server.handleClientDisconnected(f.firstClient);
    else if (mode == 3)
      f.server.reset();
    else {
      // Cancellation itself is allowed to destroy the owner, too.
      f.dispatcher->onCancelPreparation = [&] { f.server.reset(); };
      server.drainOriginalInput(deadline);
    }
    QVERIFY(!f.first->take().contains("DKDN"));
    QVERIFY(!f.second->take().contains("DKDN"));
    QCOMPARE(f.dispatcher->cancelledPreparations, (std::vector<std::uint64_t>{1}));
    QVERIFY(f.dispatcher->committed.empty());
    QVERIFY(f.dispatcher->preparations.empty());
    if (!f.server)
      continue;
    QVERIFY(server.m_originalPending.empty());
    OriginalInputKey rawUp;
    rawUp.kind = OriginalKeyKind::Up;
    rawUp.transition = OriginalKeyTransition::Up;
    QVERIFY(f.inbox->publish(f.capture(rawUp, 0)));
    f.events.tick();
    QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
  }
}

void ServerTests::originalDispatch_pendingMalformedRepliesAndDeadline()
{
  using namespace deskflow::windowshare;
  using Result = Server::OriginalInputDispatcher::Result;
  for (int mode = 0; mode != 6; ++mode) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    f.clear();
    f.dispatcher->asynchronous = true;
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    const auto down = f.capture(OriginalInputKey{});
    QVERIFY(f.inbox->publish(down));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
    if (mode == 0)
      f.dispatcher->forcedPoll = Result::accepted({41, 1}, 2); // Wrong request.
    else if (mode == 1)
      f.dispatcher->forcedPoll = Result::pending(0); // Missing identity.
    else if (mode == 2)
      f.dispatcher->forcedPoll = Result::accepted({0, 1}, 1); // Missing owner.
    else if (mode == 3)
      f.dispatcher->forcedPoll = Result{Server::OriginalInputDispatcher::State::Pending, 1, {{41, 1}}};
    else if (mode == 4)
      f.dispatcher->throwPoll = true;
    else {
      f.dispatcher->waiting = false;
      server.drainOriginalInput(server.m_originalPending.front().preparationDeadline);
    }
    if (mode != 5)
      f.events.tick();
    QVERIFY(server.m_originalDispatchTerminal);
    QVERIFY(server.m_originalPending.empty());
    QVERIFY(f.dispatcher->committed.empty());
    QVERIFY(!f.first->take().contains("DKDN"));
    QCOMPARE(f.dispatcher->cancelledPreparations, (std::vector<std::uint64_t>{1}));
    QCOMPARE(f.dispatcher->aborted, 1);
    // Even a later well-formed reply cannot revive this retired request.
    f.dispatcher->forcedPoll = Result::accepted({41, 1}, 1);
    const auto polls = f.dispatcher->polled.size();
    f.events.tick();
    QCOMPARE(f.dispatcher->polled.size(), polls);
    QVERIFY(f.dispatcher->committed.empty());
    QVERIFY(server.m_originalReservationCall == nullptr);
  }
}

void ServerTests::originalDispatch_pendingReentryAndLimit()
{
  using namespace deskflow::windowshare;
  for (int mode = 0; mode != 4; ++mode) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    f.second->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    f.clear();
    f.dispatcher->asynchronous = true;
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    const auto down = f.capture(OriginalInputKey{});
    QVERIFY(f.inbox->publish(down));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
    f.dispatcher->waiting = mode == 3;
    f.dispatcher->onPoll = [&] {
      if (mode == 0)
        server.switchComputer(f.secondClient, 300, 400, false);
      else if (mode == 1)
        f.server.reset();
      else if (mode == 2)
        server.handleClientDisconnected(f.firstClient);
      else {
        for (std::size_t i = 0; i < Server::originalPendingCapacity; ++i) {
          server.onMouseWheel(0, 120);
          QVERIFY(server.m_originalPending.size() < Server::originalPendingCapacity);
        }
      }
    };
    f.events.tick();
    f.dispatcher->onPoll = {};
    QVERIFY(f.dispatcher->committed.empty());
    QVERIFY(!f.first->take().contains("DKDN"));
    QVERIFY(!f.second->take().contains("DKDN"));
    QCOMPARE(f.dispatcher->cancelledPreparations, (std::vector<std::uint64_t>{1}));
    if (!f.server)
      continue;
    QVERIFY(server.m_originalPending.empty());
    QVERIFY(server.m_originalReservationCall == nullptr);
    if (mode == 3)
      QVERIFY(server.m_originalDispatchTerminal);
  }
}

void ServerTests::originalDispatch_pendingFrozenPointAndTicketReuse()
{
  using namespace deskflow::windowshare;
  OriginalDispatchFixture f;
  auto &server = *f.server;
  QVERIFY(f.install());
  f.first->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  f.clear();
  f.dispatcher->asynchronous = true;
  IPlatformComputer::MotionInfo first{10, 20}, second{30, 40};
  auto capture = f.capture(OriginalInputMotion{10, 20});
  QVERIFY(f.inbox->publish(capture));
  QVERIFY(f.dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &first, capture));
  capture = f.capture(OriginalInputMotion{30, 40});
  QVERIFY(f.inbox->publish(capture));
  QVERIFY(f.dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &second, capture));
  QCOMPARE(server.m_x, 140);
  QCOMPARE(server.m_y, 160);
  QVERIFY(f.first->take().isEmpty());
  f.dispatcher->asynchronous = false;
  f.dispatcher->waiting = false;
  f.events.tick();
  QCOMPARE(f.dispatcher->committed.size(), std::size_t(2));
  QCOMPARE(f.dispatcher->committed[0].route.x, 110);
  QCOMPARE(f.dispatcher->committed[0].route.y, 120);
  QCOMPARE(f.dispatcher->committed[1].route.x, 140);
  QCOMPARE(f.dispatcher->committed[1].route.y, 160);
  QCOMPARE(f.first->take(), "DMMV" + QByteArray::fromHex("006e 0078") + "DMMV" + QByteArray::fromHex("008c 00a0"));
  // A provider may not reuse an old pending identity for another capture.
  f.dispatcher->asynchronous = true;
  f.dispatcher->nextTicket = 0;
  IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
  capture = f.capture(OriginalInputKey{});
  QVERIFY(f.inbox->publish(capture));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, capture));
  QVERIFY(server.m_originalDispatchTerminal);
  QCOMPARE(f.dispatcher->committed.size(), std::size_t(2));
  QVERIFY(!f.first->take().contains("DKDN"));
}

void ServerTests::originalDispatch_pendingDragRevocation()
{
  using namespace deskflow::windowshare;
  OriginalDispatchFixture f;
  auto &server = *f.server;
  QVERIFY(f.install());
  f.first->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  f.clear();
  IPlatformComputer::ButtonInfo button(1, 0);
  auto captured = f.capture(OriginalInputButton{0, true});
  QVERIFY(f.inbox->publish(captured));
  QVERIFY(f.dispatch(EventTypes::PrimaryComputerButtonDown, &button, captured));
  QCOMPARE(f.dispatcher->committed.size(), std::size_t(1));
  f.dispatcher->asynchronous = true;
  IPlatformComputer::MotionInfo motion{10, 20};
  captured = f.capture(OriginalInputMotion{10, 20, 0});
  QVERIFY(f.inbox->publish(captured));
  QVERIFY(f.dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion, captured));
  QVERIFY(f.first->take().isEmpty());
  f.dispatcher->waiting = false;
  f.dispatcher->completion = Server::OriginalInputDispatcher::State::Revoked;
  f.events.tick();
  // Revocation of a pending drag must release the held original now, not
  // wait for a provider heartbeat to happen to notice another failure.
  QCOMPARE(f.dispatcher->released, (std::vector<std::uint64_t>{41}));
  QCOMPARE(f.dispatcher->committed.size(), std::size_t(1));
  QCOMPARE(f.first->take(), "DMMV" + QByteArray::fromHex("006e 0078"));
  const auto held = server.m_inputRoutes.repeat({Server::InputRoutes::Kind::Button, 1});
  QVERIFY(held && !held->originalOwner && held->recipients.empty());
  QVERIFY(f.inbox->publish(f.capture(OriginalInputButton{0, false}, 0)));
  f.events.tick();
  QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
  QVERIFY(f.first->take().isEmpty());
}

void ServerTests::originalDispatch_missingUpsAndDragOwner()
{
  using namespace deskflow::windowshare;
  for (int mode = 0; mode != 5; ++mode) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    f.clear();
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    IPlatformComputer::ButtonInfo button(1, 0);
    const bool mouse = mode == 2;
    const auto down = mouse ? f.capture(OriginalInputButton{0, true}) : f.capture(OriginalInputKey{});
    QVERIFY(f.inbox->publish(down));
    QVERIFY(f.dispatch(
        mouse ? EventTypes::PrimaryComputerButtonDown : EventTypes::KeyStateKeyDown,
        mouse ? static_cast<void *>(&button) : static_cast<void *>(&key), down
    ));
    QCOMPARE(f.dispatcher->committed.size(), std::size_t(1));
    if (mode == 1) {
      struct OnUp : InputFilter::Condition
      {
        Condition *clone() const override
        {
          return new OnUp;
        }
        std::string format() const override
        {
          return "up";
        }
        InputFilter::FilterStatus match(const Event &event) override
        {
          return event.getType() == EventTypes::KeyStateKeyUp ? InputFilter::FilterStatus::Activate
                                                              : InputFilter::FilterStatus::NoMatch;
        }
      };
      struct Consume : InputFilter::Action
      {
        Action *clone() const override
        {
          return new Consume;
        }
        std::string format() const override
        {
          return "consume";
        }
        void perform(const Event &) override
        {
        }
      };
      InputFilter::Rule rule(new OnUp);
      rule.adoptAction(new Consume, true);
      server.m_inputFilter->addFilterRule(rule);
    }
    OriginalInputKey rawUp;
    rawUp.kind = OriginalKeyKind::Up;
    rawUp.transition = OriginalKeyTransition::Up;
    const std::uint16_t parts = (mode == 0 || mode == 2) ? 0 : mode == 3 ? 2 : 1;
    const auto up = mouse ? f.capture(OriginalInputButton{0, false}, parts) : f.capture(rawUp, parts);
    if (parts) {
      QVERIFY(f.dispatch(EventTypes::KeyStateKeyUp, &key, up));
      if (parts == 2)
        QVERIFY(f.dispatch(EventTypes::KeyStateKeyUp, &key, up, 2));
    }
    QVERIFY(f.inbox->publish(up));
    f.events.tick();
    if (mode == 1)
      server.drainOriginalInput(Server::OriginalClock::now() + Server::originalDispatchTimeout);
    QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
    QCOMPARE(f.dispatcher->committed.size(), mode == 4 ? std::size_t(2) : std::size_t(1));
    if (mode == 4) {
      QVERIFY(f.dispatcher->released.empty());
      QCOMPARE(f.dispatcher->requestedOwners.back(), std::uint64_t(41));
    } else {
      QCOMPARE(f.dispatcher->released, std::vector<std::uint64_t>{41});
    }
    QVERIFY(f.first->take().isEmpty());
  }
  for (int mode = 0; mode != 3; ++mode) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    f.clear();
    if (mode != 2) {
      IPlatformComputer::ButtonInfo button(1, 0);
      const auto down = f.capture(OriginalInputButton{0, true});
      QVERIFY(f.inbox->publish(down));
      QVERIFY(f.dispatch(EventTypes::PrimaryComputerButtonDown, &button, down));
    }
    if (mode == 1)
      f.dispatcher->forcedOwner = 42;
    IPlatformComputer::MotionInfo motion{1, 1};
    const auto drag = f.capture(OriginalInputMotion{1, 1, 0});
    QVERIFY(f.inbox->publish(drag));
    QVERIFY(f.dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion, drag));
    if (mode == 0) {
      QCOMPARE(f.dispatcher->requestedOwners.back(), std::uint64_t(41));
      QCOMPARE(f.dispatcher->committed.size(), std::size_t(2));
    } else if (mode == 1) {
      QCOMPARE(f.dispatcher->requestedOwners.back(), std::uint64_t(41));
      QCOMPARE(f.dispatcher->committed.size(), std::size_t(1));
      QVERIFY(
          std::find(f.dispatcher->released.begin(), f.dispatcher->released.end(), 41) != f.dispatcher->released.end()
      );
    } else {
      QVERIFY(f.dispatcher->reserved.empty());
    }
  }
}

void ServerTests::originalDispatch_transitionAndReentrantOrdering()
{
  using namespace deskflow::windowshare;
  for (bool wrap : {false, true}) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    f.second->authenticate();
    server.switchComputer(f.firstClient, 100, 200, false);
    f.clear();
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    IPlatformComputer::MotionInfo motion{7, -3};
    const auto capture = wrap ? f.capture(OriginalInputMotion{7, -3}) : f.capture(OriginalInputKey{});
    QVERIFY(f.dispatch(
        wrap ? EventTypes::PrimaryComputerMotionOnSecondary : EventTypes::KeyStateKeyDown,
        wrap ? static_cast<void *>(&motion) : static_cast<void *>(&key), capture
    ));
    server.switchComputer(wrap ? f.firstClient : f.secondClient, 900, 900, false);
    const auto packets = f.first->take();
    if (wrap)
      QCOMPARE(packets, "DMMV" + QByteArray::fromHex("006b 00c5") + "DMMV" + QByteArray::fromHex("0384 0384"));
    else
      QVERIFY(packets.startsWith("DKDN" + QByteArray::fromHex("0061 0000 001e") + "COUT"));
    QVERIFY(f.inbox->publish(capture));
    f.events.tick();
    QVERIFY(f.first->take().isEmpty());
    QVERIFY(f.dispatcher->committed.empty());
  }

  // A real nested event during a legacy packet write must not inherit the
  // replay's frozen recipient or bypass the pending operation order.
  OriginalDispatchFixture f;
  auto &server = *f.server;
  QVERIFY(f.install());
  f.first->authenticate();
  f.second->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  f.clear();
  IPlatformComputer::MotionInfo motion{1, 1};
  IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
  const auto move = f.capture(OriginalInputMotion{1, 1});
  const auto down = f.capture(OriginalInputKey{});
  QVERIFY(f.inbox->publish(move));
  QVERIFY(f.inbox->publish(down));
  f.first->onWrite = [&] {
    server.switchComputer(f.secondClient, 300, 400, false);
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
    QVERIFY(!f.first->bytes.contains("DKDN"));
    QVERIFY(!f.second->bytes.contains("DKDN"));
  };
  QVERIFY(f.dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion, move));
  QVERIFY(!f.first->take().contains("DKDN"));
  // Owner41 was revoked by the callback switch. A reused reservation cannot
  // resurrect it; the event remains original-or-tombstone on the new peer.
  QVERIFY(!f.second->take().contains("DKDN"));
  QVERIFY(server.m_originalPending.empty());
  QVERIFY(f.dispatcher->committed.empty()); // DMMV callback revoked the original motion reservation.
}

void ServerTests::originalDispatch_terminalAndPartialReleaseAccounting()
{
  using namespace deskflow::windowshare;
  for (int mode = 0; mode != 6; ++mode) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    f.clear();
    IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
    const auto down = f.capture(OriginalInputKey{});
    QVERIFY(f.inbox->publish(down));
    QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
    QCOMPARE(f.dispatcher->committed.size(), std::size_t(1));
    if (mode < 2)
      server.cancelOriginalDispatch();
    else if (mode == 3) {
      f.first->identity->revoke();
      f.events.tick();
      QCOMPARE(f.dispatcher->released, std::vector<std::uint64_t>{41});
    }
    const bool filteredUp = mode == 1 || mode == 2 || mode >= 4;
    if (filteredUp) {
      struct OnUp : InputFilter::Condition
      {
        Condition *clone() const override
        {
          return new OnUp;
        }
        std::string format() const override
        {
          return "up";
        }
        InputFilter::FilterStatus match(const Event &event) override
        {
          return event.getType() == EventTypes::KeyStateKeyUp ? InputFilter::FilterStatus::Activate
                                                              : InputFilter::FilterStatus::NoMatch;
        }
      };
      struct Consume : InputFilter::Action
      {
        Action *clone() const override
        {
          return new Consume;
        }
        std::string format() const override
        {
          return "consume";
        }
        void perform(const Event &) override
        {
        }
      };
      InputFilter::Rule rule(new OnUp);
      rule.adoptAction(new Consume, true);
      server.m_inputFilter->addFilterRule(rule);
    }
    OriginalInputKey rawUp;
    rawUp.kind = OriginalKeyKind::Up;
    rawUp.transition = OriginalKeyTransition::Up;
    const auto up = f.capture(rawUp, mode == 2 ? 2 : filteredUp ? 1 : 0);
    QVERIFY(f.inbox->publish(up));
    if (mode == 2)
      QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, up, 1)); // Half-duplex translation of physical up.
    if (filteredUp)
      QVERIFY(f.dispatch(EventTypes::KeyStateKeyUp, &key, up, mode == 2 ? 2 : 1));
    f.events.tick();
    if (mode >= 4) {
      // Cancellation must retain an already-cached filtered physical up.
      QVERIFY(server.m_originalCompleted.contains({up.input.record.epoch, up.input.record.sequence}));
      QCOMPARE(server.m_inputRoutes.size(), std::size_t(1));
      if (mode == 5)
        f.routes->revoke();
      else
        server.cancelOriginalDispatch();
      f.events.tick();
      f.events.tick(); // A revoked route source must not erase cleanup on a later drain.
    }
    server.drainOriginalInput(Server::OriginalClock::now() + Server::originalDispatchTimeout);
    QCOMPARE(server.m_inputRoutes.size(), std::size_t(0));
    QVERIFY(server.m_originalKeyControls.empty());
    QCOMPARE(f.dispatcher->committed.size(), std::size_t(1));
    QVERIFY(f.first->take().isEmpty());
    server.onKeyDown('a', 0, 30, "en", nullptr);
    server.onKeyUp('a', 0, 30, nullptr);
    QCOMPARE(
        f.first->take(), "DKDN" + QByteArray::fromHex("0061 0000 001e") + "DKUP" + QByteArray::fromHex("0061 0000 001e")
    );
  }
}

void ServerTests::originalDispatch_cleanupCannotEraseNewLegacyOwner()
{
  using namespace deskflow::windowshare;
  OriginalDispatchFixture f;
  auto &server = *f.server;
  QVERIFY(f.install());
  f.first->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  f.clear();
  IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
  auto down = f.capture(OriginalInputKey{});
  QVERIFY(f.inbox->publish(down));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
  const auto original = server.m_inputRoutes.repeat(Server::InputRoutes::keyControl('a', 30));
  QVERIFY(original);
  f.dispatcher->forcedOwner = 42;
  OriginalInputKey rawUp;
  rawUp.kind = OriginalKeyKind::Up;
  rawUp.transition = OriginalKeyTransition::Up;
  auto up = f.capture(rawUp);
  QVERIFY(f.inbox->publish(up));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyUp, &key, up));
  QVERIFY(server.m_originalKeyControls.empty());
  f.dispatcher->forcedOwner.reset();
  f.dispatcher->accept = false;
  down = f.capture(OriginalInputKey{});
  QVERIFY(f.inbox->publish(down));
  QVERIFY(f.dispatch(EventTypes::KeyStateKeyDown, &key, down));
  const auto legacy = server.m_inputRoutes.repeat(Server::InputRoutes::keyControl('a', 30));
  QVERIFY(legacy && legacy->incarnation != original->incarnation && !legacy->recipients.empty());
  QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("0061 0000 001e"));
  // Even a retained stale cleanup reference names the old acquisition only.
  server.m_originalKeyControls.emplace(0, Server::OriginalPhysicalPress{original->control, original->incarnation});
  up = f.capture(rawUp, 0);
  QVERIFY(f.inbox->publish(up));
  f.events.tick();
  const auto retained = server.m_inputRoutes.repeat(legacy->control);
  QVERIFY(retained && retained->incarnation == legacy->incarnation);
  server.closeClient(f.firstClient, kMsgCClose);
  QCOMPARE(f.first->take(), "DKUP" + QByteArray::fromHex("0061 0000 001e") + "CBYE");
}

void ServerTests::originalDispatch_flushDestructionAndDisconnect()
{
  using namespace deskflow::windowshare;
  for (int mode = 0; mode != 5; ++mode) {
    OriginalDispatchFixture f;
    auto &server = *f.server;
    QVERIFY(f.install());
    f.first->authenticate();
    f.second->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    f.clear();
    if (mode == 4) {
      server.onKeyDown('a', 0, 30, "en", nullptr);
      QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("0061 0000 001e"));
      IPlatformComputer::KeyInfo key{'a', 0, 30, 1, ""};
      OriginalInputKey rawUp;
      rawUp.kind = OriginalKeyKind::Up;
      rawUp.transition = OriginalKeyTransition::Up;
      const auto up = f.capture(rawUp);
      QVERIFY(f.dispatch(EventTypes::KeyStateKeyUp, &key, up));
      QVERIFY(f.first->bytes.isEmpty());
      f.first->onWrite = [&] { f.server.reset(); };
      server.switchComputer(f.secondClient, 300, 400, false);
      QVERIFY(!f.server);
      QVERIFY(f.first->take().startsWith("DKUP" + QByteArray::fromHex("0061 0000 001e")));
      QCOMPARE(f.first->destructions, 1u);
      QCOMPARE(f.second->destructions, 1u);
      continue;
    }
    IPlatformComputer::MotionInfo motion{1, 1};
    const auto capture = f.capture(OriginalInputMotion{1, 1});
    QVERIFY(f.dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion, capture));
    if (mode == 0) {
      f.first->onWrite = [&] { f.server.reset(); };
      server.switchComputer(f.secondClient, 300, 400, false);
      QVERIFY(!f.server);
      QVERIFY(f.first->destroyed && f.second->destroyed);
    } else if (mode == 1) {
      server.handleClientDisconnected(f.firstClient);
      QVERIFY(f.first->destroyed);
      QVERIFY(f.first->take().isEmpty());
      QVERIFY(f.inbox->publish(capture));
      f.events.tick();
      QVERIFY(f.first->take().isEmpty());
    } else if (mode == 2) {
      // The destination may disappear during the older recipient's flush.
      f.first->onWrite = [&] { server.handleClientDisconnected(f.secondClient); };
      server.switchComputer(f.secondClient, 300, 400, false);
      QVERIFY(f.second->destroyed);
      QCOMPARE(server.m_active, static_cast<BaseClientProxy *>(f.firstClient));
    } else {
      // Flushing another recipient can deliver a nested disconnect of the
      // same client. The outer handler retains sole ownership of deletion.
      f.first->onWrite = [&] { server.handleClientDisconnected(f.secondClient); };
      server.handleClientDisconnected(f.secondClient);
      QCOMPARE(f.second->destructions, 1u);
      QVERIFY(!f.first->destroyed);
      QCOMPARE(server.m_active, static_cast<BaseClientProxy *>(f.firstClient));
      QVERIFY(server.m_disconnectingInputClients.empty());
    }
  }
}

void ServerTests::originalCapture_exactDispatchAndOwnership()
{
  using namespace deskflow::windowshare;
  RoutingFixture f;
  auto routes = std::make_shared<OriginalInputRoutes>();
  auto &server = *f.server;
  QVERIFY(server.setOriginalInputRoutes(routes));
  f.first->authenticate();
  f.second->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  const auto firstRecipient = server.m_inputRecipients.at(f.firstClient);
  const auto secondRecipient = server.m_inputRecipients.at(f.secondClient);
  f.clear();
  std::uint64_t sequence = 0;
  const auto dispatch = [&](EventTypes type, void *data) {
    Event event(
        type, f.primary.getEventTarget(), data, Event::EventFlags::DontFreeData, OriginalInputStamp{37, ++sequence, 1}
    );
    return f.events.dispatchEvent(event);
  };
  IPlatformComputer::KeyInfo key{'a', 0, 0x1e, 1, ""};
  QVERIFY(dispatch(EventTypes::KeyStateKeyDown, &key));
  auto down = routes->pop();
  QVERIFY(down && down->outcome == OriginalInputRouteOutcome::Dispatched);
  QCOMPARE(down->stamp, (OriginalInputStamp{37, 1, 1}));
  QCOMPARE(down->recipient, firstRecipient);
  QCOMPARE(down->ticket.incarnation(), f.first->identity->ticket()->incarnation());
  QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("0061 0000 001e"));
  QVERIFY(!routes->pop()); // InputFilter forwarding must not manufacture a part.

  IPlatformComputer::MotionInfo motion{7, -3};
  QVERIFY(dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion));
  auto moved = routes->pop();
  QVERIFY(moved && moved->pointKnown && moved->outcome == OriginalInputRouteOutcome::Dispatched);
  QCOMPARE(moved->x, 107);
  QCOMPARE(moved->y, 97);
  QCOMPARE(f.first->take(), "DMMV" + QByteArray::fromHex("006b 0061"));
  IPlatformComputer::ButtonInfo button(1, 0);
  QVERIFY(dispatch(EventTypes::PrimaryComputerButtonDown, &button));
  auto pressed = routes->pop();
  QVERIFY(pressed && pressed->kind == OriginalInputRouteKind::Button && pressed->pointKnown);
  QCOMPARE(pressed->x, 107);
  QCOMPARE(pressed->y, 97);
  server.switchComputer(f.secondClient, 300, 400, false);
  f.clear();
  QVERIFY(dispatch(EventTypes::KeyStateKeyRepeat, &key));
  auto repeated = routes->pop();
  QVERIFY(repeated && repeated->outcome == OriginalInputRouteOutcome::Dispatched && !repeated->pointKnown);
  QCOMPARE(repeated->recipient, firstRecipient); // Held press, not latest selected peer.
  QVERIFY(dispatch(EventTypes::KeyStateKeyUp, &key));
  QCOMPARE(routes->pop()->recipient, firstRecipient);
  QVERIFY(dispatch(EventTypes::PrimaryComputerButtonUp, &button));
  auto released = routes->pop();
  QVERIFY(released && released->outcome == OriginalInputRouteOutcome::Uncorrelated && !released->pointKnown);
  QCOMPARE(released->recipient, firstRecipient); // Old owner has no current pointer decision.
  QVERIFY(f.second->take().isEmpty());
  IPlatformComputer::WheelInfo wheel{-120, 240};
  QVERIFY(dispatch(EventTypes::PrimaryComputerWheel, &wheel));
  auto scrolled = routes->pop();
  QVERIFY(scrolled && scrolled->outcome == OriginalInputRouteOutcome::Dispatched && scrolled->pointKnown);
  QCOMPARE(scrolled->recipient, secondRecipient);
  QCOMPARE(scrolled->x, 300);
  QCOMPARE(scrolled->y, 400);
  f.second->identity->revoke();
  QVERIFY(!scrolled->ticket.live());
  server.revokeSelectedPeer();
  QVERIFY(!routes->live());
}

void ServerTests::originalCapture_negotiatedPacketsRetainRecipientAndStamp()
{
  using namespace deskflow::windowshare;
  RoutingFixture f;
  auto &server = *f.server;
  f.config.addComputer("traced");
  f.config.addComputer("untrusted");
  f.config.addOption("traced", kOptionInputSequenceTrace, 101);
  f.config.addOption("", kOptionInputSequenceTrace, 202);
  QVERIFY(server.setConfig(f.config));
  auto capture = std::make_shared<WireCapture>();
  capture->authenticate();
  auto *client = new TracedRoutingClient("traced", &server, &f.events, capture);
  server.adoptClient(client);
  QVERIFY(server.m_clientSet.contains(client));
  QVERIFY(!capture->take().contains("ISQ1")); // Config cannot enable tracing.

  auto routes = std::make_shared<OriginalInputRoutes>();
  QVERIFY(server.setOriginalInputRoutes(routes));
  server.sendOptions(client);
  const auto options = capture->take();
  const auto option = options.indexOf("ISQ1");
  QVERIFY(option >= 0);
  QCOMPARE(options.sliced(option + 4, 4), QByteArray::fromHex("00000001"));
  client->acknowledge(1);
  f.first->authenticate();
  server.sendOptions(f.firstClient); // An authenticated historical protocol stays unchanged.
  QVERIFY(!f.first->take().contains("ISQ1"));
  auto untrusted = std::make_shared<WireCapture>();
  server.adoptClient(new TracedRoutingClient("untrusted", &server, &f.events, untrusted));
  QVERIFY(!untrusted->take().contains("ISQ1"));

  server.switchComputer(client, 100, 200, false);
  QVERIFY(!capture->take().contains("DSEQ")); // Unstamped enter/control writes.
  std::uint64_t sequence = 0;
  const auto dispatch = [&](EventTypes type, void *data) {
    Event event(
        type, f.primary.getEventTarget(), data, Event::EventFlags::DontFreeData, OriginalInputStamp{81, ++sequence, 1}
    );
    return f.events.dispatchEvent(event);
  };
  IPlatformComputer::MotionInfo motion{7, -3};
  QVERIFY(dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion));
  auto decision = routes->pop();
  QVERIFY(decision && decision->pointKnown);
  QCOMPARE(decision->recipient, server.m_inputRecipients.at(client));
  QCOMPARE(decision->x, 107);
  QCOMPARE(decision->y, 197);
  auto wire = capture->take();
  QVERIFY(wire.startsWith("DSEQ"));
  auto decoded = decodeInputSequenceEnvelope(std::string_view(wire.constData() + 6, wire.size() - 6));
  QVERIFY(decoded);
  QCOMPARE(decoded->token, (InputSequenceToken{1, 1, decision->stamp}));
  QCOMPARE(QByteArray::fromStdString(decoded->payload), "DMMV" + QByteArray::fromHex("006b 00c5"));

  IPlatformComputer::KeyInfo key{'a', 0, 0x1e, 1, ""};
  QVERIFY(dispatch(EventTypes::KeyStateKeyDown, &key));
  capture->take();
  routes->pop();
  server.switchComputer(f.firstClient, 300, 400, false);
  capture->take();
  f.clear();
  QVERIFY(dispatch(EventTypes::KeyStateKeyUp, &key));
  decision = routes->pop();
  QVERIFY(decision);
  QCOMPARE(decision->recipient, server.m_inputRecipients.at(client)); // Press owner, not new selected peer.
  wire = capture->take();
  decoded = decodeInputSequenceEnvelope(std::string_view(wire.constData() + 6, wire.size() - 6));
  QVERIFY(decoded);
  QCOMPARE(decoded->token, (InputSequenceToken{1, 3, decision->stamp}));
  QCOMPARE(QByteArray::fromStdString(decoded->payload), "DKUP" + QByteArray::fromHex("0061 0000 001e"));
  QVERIFY(f.first->take().isEmpty());

  routes->revoke();
  server.switchComputer(client, 100, 200, false);
  capture->take();
  QVERIFY(dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion));
  QCOMPARE(capture->take(), "DMMV" + QByteArray::fromHex("006b 00c5"));
  server.sendOptions(client);
  QVERIFY(!capture->take().contains("ISQ1"));
}

void ServerTests::originalCapture_motionCrossingUsesEnterDecision()
{
  using namespace deskflow::windowshare;
  RoutingFixture f;
  f.first->authenticate();
  f.second->authenticate();
  QVERIFY(f.config.connect("first", Direction::Right, 0, 1, "second", 0, 1));
  auto routes = std::make_shared<OriginalInputRoutes>();
  auto &server = *f.server;
  QVERIFY(server.setOriginalInputRoutes(routes));
  server.switchComputer(f.firstClient, 990, 200, false);
  f.clear();
  IPlatformComputer::MotionInfo motion{20, 0};
  Event event(
      EventTypes::PrimaryComputerMotionOnSecondary, f.primary.getEventTarget(), &motion,
      Event::EventFlags::DontFreeData, OriginalInputStamp{51, 1, 1}
  );
  QVERIFY(f.events.dispatchEvent(event));
  const auto result = routes->pop();
  QVERIFY(result && result->outcome == OriginalInputRouteOutcome::Dispatched && result->pointKnown);
  QCOMPARE(result->recipient, server.m_inputRecipients.at(f.secondClient));
  QCOMPARE(result->x, 10);
  QCOMPARE(result->y, 200);
  QVERIFY(f.second->take().startsWith("CINN" + QByteArray::fromHex("000a 00c8")));
  QVERIFY(!routes->pop());
}

void ServerTests::originalCapture_filterAndAmbiguityDenials()
{
  using namespace deskflow::windowshare;
  RoutingFixture f;
  auto routes = std::make_shared<OriginalInputRoutes>();
  auto &server = *f.server;
  QVERIFY(server.setOriginalInputRoutes(routes));
  f.first->authenticate();
  f.second->authenticate();
  server.switchComputer(f.firstClient, 999, 999, false);
  f.clear();
  std::uint64_t sequence = 0;
  const auto dispatch = [&](EventTypes type, void *data) {
    Event event(
        type, f.primary.getEventTarget(), data, Event::EventFlags::DontFreeData, OriginalInputStamp{61, ++sequence, 1}
    );
    return f.events.dispatchEvent(event);
  };
  IPlatformComputer::MotionInfo motion{5, 5};
  QVERIFY(dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion));
  QCOMPARE(routes->pop()->outcome, OriginalInputRouteOutcome::NoSend);
  QVERIFY(f.first->take().isEmpty()); // Clamping preserved ordinary zero-send behavior.
  server.m_relativeMoves = true;
  server.m_lockedToComputer = true;
  QVERIFY(dispatch(EventTypes::PrimaryComputerMotionOnSecondary, &motion));
  auto relative = routes->pop();
  QVERIFY(relative && relative->outcome == OriginalInputRouteOutcome::Relative && !relative->pointKnown);
  server.m_relativeMoves = false;
  server.m_lockedToComputer = false;
  server.m_keyboardBroadcasting = true;
  IPlatformComputer::KeyInfo key{'b', 0, 0x30, 1, ""};
  QVERIFY(dispatch(EventTypes::KeyStateKeyDown, &key));
  QCOMPARE(routes->pop()->outcome, OriginalInputRouteOutcome::Ambiguous);
  QVERIFY(!f.first->take().isEmpty() && !f.second->take().isEmpty());
  server.m_keyboardBroadcasting = false;

  // Exercise real rule matching/action dispatch. The configured action has no
  // physical provenance even if this test dispatches while a capture scope is
  // active, as an immediate queue could do.
  struct Always : InputFilter::Condition
  {
    Condition *clone() const override
    {
      return new Always;
    }
    std::string format() const override
    {
      return "always";
    }
    InputFilter::FilterStatus match(const Event &) override
    {
      return InputFilter::FilterStatus::Activate;
    }
  };
  struct Generated : InputFilter::Action
  {
    RoutingEventQueue *events;
    IPlatformComputer::KeyInfo key{'c', 0, 0x2e, 1, ""};
    explicit Generated(RoutingEventQueue *events) : events(events)
    {
    }
    Action *clone() const override
    {
      return new Generated(events);
    }
    std::string format() const override
    {
      return "generated";
    }
    void perform(const Event &event) override
    {
      events->addEvent(Event(
          EventTypes::KeyStateKeyDown, event.getTarget(), &key,
          Event::EventFlags::DeliverImmediately | Event::EventFlags::DontFreeData
      ));
    }
  };
  InputFilter::Rule rule(new Always);
  rule.adoptAction(new Generated(&f.events), true);
  server.m_inputFilter->addFilterRule(rule);
  OriginalInputStampScope scope({61, ++sequence, 0});
  Event physical(EventTypes::KeyStateKeyDown, f.primary.getEventTarget(), &key, Event::EventFlags::DontFreeData);
  QVERIFY(f.events.dispatchEvent(physical));
  auto filtered = routes->pop();
  QVERIFY(filtered && filtered->outcome == OriginalInputRouteOutcome::Filtered);
  QCOMPARE(filtered->stamp, physical.originalInputStamp());
  QCOMPARE(scope.translation().parts, std::uint16_t(1));
  QVERIFY(!routes->pop());
  QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("0063 0000 002e"));
}

void ServerTests::inputRoutes_keepPressRecipientAcrossSwitch()
{
  RoutingFixture f;
  auto &server = *f.server;
  server.switchComputer(f.firstClient, 100, 100, false);
  f.clear();
  server.onKeyDown('a', 0, 0x1e, "en", "");
  server.onMouseDown(1);
  QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("0061 0000 001e") + "DMDN" + QByteArray(1, '\1'));

  server.switchComputer(f.secondClient, 100, 100, false);
  f.clear();
  server.onKeyRepeat('a', 0, 3, 0x1e, "en");
  // macOS key-up may omit the symbol; physical button still identifies owner.
  server.onKeyUp(kKeyNone, 0, 0x1e, "");
  server.onMouseUp(1);
  QCOMPARE(
      f.first->take(), "DKRP" + QByteArray::fromHex("0061 0000 0003 001e") + "DKUP" +
                           QByteArray::fromHex("0000 0000 001e") + "DMUP" + QByteArray(1, '\1')
  );
  QVERIFY(f.second->take().isEmpty());
}

void ServerTests::inputRoutes_localPressDoesNotAcquireRemoteRecipient()
{
  RoutingFixture f;
  auto &server = *f.server;
  server.onKeyDown('a', 0, 0x1e, "en", "");
  server.onKeyDown(kKeyShift_L, KeyModifierShift, 0x2a, "en", "");
  server.onMouseDown(1);
  f.platform->modifiers = KeyModifierShift | KeyModifierCapsLock;
  server.switchComputer(f.firstClient, 100, 100, false);
  // Enter must synchronize toggles, without importing a local held Shift.
  QCOMPARE(f.first->take(), "CINN" + QByteArray::fromHex("0064 0064 00000001 1000"));
  server.onKeyRepeat('a', 0, 2, 0x1e, "en");
  server.onKeyDown('a', 0, 0x1e, "en", "");
  server.onKeyUp('a', 0, 0x1e, "");
  server.onMouseUp(1);
  QVERIFY(f.first->take().isEmpty());

  // A new remote key can legitimately synthesize the desired held modifier.
  // Its eventual local physical release must clear only the recipient that saw it.
  server.onKeyDown('b', KeyModifierShift, 0x30, "en", "");
  server.onKeyUp('b', KeyModifierShift, 0x30, "");
  QCOMPARE(
      f.first->take(), "DKDN" + QByteArray::fromHex("0062 0001 0030") + "DKUP" + QByteArray::fromHex("0062 0001 0030")
  );
  server.onKeyUp(kKeyShift_L, 0, 0x2a, "");
  QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("ee07 0001 0000"));
  QVERIFY(f.second->take().isEmpty());
}

void ServerTests::inputRoutes_disconnectDoesNotRetargetHeldInput()
{
  RoutingFixture f;
  auto &server = *f.server;
  server.switchComputer(f.firstClient, 100, 100, false);
  server.onKeyDown('a', 0, 0x1e, "en", "");
  server.onMouseDown(1);
  server.switchComputer(f.secondClient, 100, 100, false);
  f.clear();
  server.handleClientDisconnected(f.firstClient);
  QVERIFY(f.first->destroyed);
  QVERIFY(f.first->take().isEmpty()); // no write through the disconnected proxy

  auto replacement = std::make_shared<WireCapture>();
  auto *replacementClient = new RoutingClient("first", &f.events, replacement);
  server.adoptClient(replacementClient);
  server.switchComputer(replacementClient, 100, 100, false);
  replacement->take();
  f.clear();
  server.onKeyRepeat('a', 0, 1, 0x1e, "en");
  server.onKeyDown('a', 0, 0x1e, "en", "");
  server.onMouseDown(1);
  server.onKeyUp('a', 0, 0x1e, "");
  server.onMouseUp(1);
  QVERIFY(replacement->take().isEmpty());
  QVERIFY(f.second->take().isEmpty());
  server.onKeyDown('a', 0, 0x1e, "en", "");
  server.onKeyUp('a', 0, 0x1e, "");
  QCOMPARE(
      replacement->take(),
      "DKDN" + QByteArray::fromHex("0061 0000 001e") + "DKUP" + QByteArray::fromHex("0061 0000 001e")
  );
}

void ServerTests::inputRoutes_orderlyCloseReleasesBeforeClosing()
{
  RoutingFixture f;
  auto &server = *f.server;
  server.switchComputer(f.firstClient, 100, 100, false);
  server.onKeyDown('a', KeyModifierShift, 0x1e, "en", "");
  server.onMouseDown(1);
  server.switchComputer(f.secondClient, 100, 100, false);
  f.clear();
  server.closeClient(f.firstClient, kMsgCClose);
  QCOMPARE(
      f.first->take(), "DKUP" + QByteArray::fromHex("0061 0000 001e") + "DMUP" + QByteArray(1, '\1') + "DKDN" +
                           QByteArray::fromHex("ee07 0001 0000") + "CBYE"
  );
  QVERIFY(f.second->take().isEmpty());
  server.onKeyUp('a', 0, 0x1e, "");
  server.onMouseUp(1);
  QVERIFY(f.first->take().isEmpty());
  QVERIFY(f.second->take().isEmpty());
}

void ServerTests::windowDragEdge_configuredMappingsKeepLocalOwner()
{
  using namespace deskflow::windowshare;
  struct EdgeCase
  {
    Direction side;
    int x, y, destinationX, destinationY;
    WindowDragEdgeSide observed;
  };
  for (const auto &test : std::array{
           EdgeCase{Direction::Right, 999, 250, 0, 250, WindowDragEdgeSide::Right},
           EdgeCase{Direction::Left, 0, 250, 999, 250, WindowDragEdgeSide::Left},
           EdgeCase{Direction::Top, 250, 0, 250, 999, WindowDragEdgeSide::Top},
           EdgeCase{Direction::Bottom, 250, 999, 250, 0, WindowDragEdgeSide::Bottom}
       }) {
    RoutingFixture f;
    auto &server = *f.server;
    QVERIFY(f.config.connect("primary", test.side, 0, 1, "first", 0, 1));
    QVERIFY(server.setConfig(f.config));
    server.m_disableLockToComputer = false;
    f.platform->mouseHeld = true;
    auto mailbox = std::make_shared<WindowDragEdgeMailbox>();
    QVERIFY(server.setWindowDragEdgeMailbox(mailbox));
    QVERIFY(!server.setWindowDragEdgeMailbox(mailbox));
    QVERIFY(server.setOriginalInputRoutes(std::make_shared<OriginalInputRoutes>()));
    IPlatformComputer::ButtonInfo button(1, 0);
    Event down(
        EventTypes::PrimaryComputerButtonDown, f.primary.getEventTarget(), &button, Event::EventFlags::DontFreeData,
        OriginalInputStamp{77, 1, 1}
    );
    QVERIFY(f.events.dispatchEvent(down));
    IPlatformComputer::MotionInfo point{test.x, test.y};
    Event move(
        EventTypes::PrimaryComputerMotionOnPrimary, f.primary.getEventTarget(), &point, Event::EventFlags::DontFreeData,
        OriginalInputStamp{77, 2, 1}
    );
    QVERIFY(f.events.dispatchEvent(move));
    QVERIFY(!mailbox->read()); // Configured computer names are not authentication.
    f.first->authenticate();
    f.clear();
    Event next(
        EventTypes::PrimaryComputerMotionOnPrimary, f.primary.getEventTarget(), &point, Event::EventFlags::DontFreeData,
        OriginalInputStamp{77, 3, 1}
    );
    QVERIFY(f.events.dispatchEvent(next));
    const auto observed = mailbox->read();
    QVERIFY(observed && observed->live());
    const auto &edge = observed->edge();
    QCOMPARE(edge.press, (OriginalInputStamp{77, 1, 1}));
    QCOMPARE(edge.motion, (OriginalInputStamp{77, 3, 1}));
    QCOMPARE(edge.side, test.observed);
    QCOMPARE(edge.recipient, server.m_inputRecipients.at(f.firstClient));
    QCOMPARE(edge.destinationX, test.destinationX);
    QCOMPARE(edge.destinationY, test.destinationY);
    QCOMPARE(server.m_active, static_cast<BaseClientProxy *>(&f.primary));
    QVERIFY(!server.selectedPeer());
    QVERIFY(f.first->take().isEmpty() && f.second->take().isEmpty());
    const auto owner = server.m_inputRoutes.repeat({InputRouteState::Kind::Button, 1});
    QVERIFY(owner && owner->recipients.empty() && !owner->originalOwner);
    Event up(
        EventTypes::PrimaryComputerButtonUp, f.primary.getEventTarget(), &button, Event::EventFlags::DontFreeData,
        OriginalInputStamp{77, 4, 1}
    );
    QVERIFY(f.events.dispatchEvent(up));
    QVERIFY(!observed->live() && !mailbox->read());
    QVERIFY(f.first->take().isEmpty()); // Up remains with its original local owner.
    f.platform->mouseHeld = false;
    QVERIFY(server.onMouseMovePrimary(test.x, test.y));
    QCOMPARE(server.m_active, static_cast<BaseClientProxy *>(f.firstClient));
  }
}

void ServerTests::windowDragDrop_realReleaseBeforeCursorTransfer()
{
  using namespace deskflow::windowshare;
  for (bool returning : {false, true})
    for (int failure = 0; failure != 5; ++failure) {
      RoutingFixture f;
      auto &server = *f.server;
      QVERIFY(
          returning ? f.config.connect("first", Direction::Left, 0, 1, "primary", 0, 1)
                    : f.config.connect("primary", Direction::Right, 0, 1, "first", 0, 1)
      );
      QVERIFY(server.setConfig(f.config));
      server.m_disableLockToComputer = false;
      f.platform->mouseHeld = true;
      f.first->authenticate();
      if (returning)
        server.switchComputer(f.firstClient, 0, 500, false);
      f.clear();
      OriginalInputStream capture;
      const auto epoch = capture.epoch().id();
      auto position = std::make_shared<std::atomic<std::uint64_t>>(2);
      auto mailbox = std::make_shared<WindowDragEdgeMailbox>();
      QVERIFY(mailbox->bindCapture(capture.epoch(), position));
      QVERIFY(server.setWindowDragEdgeMailbox(mailbox));
      QVERIFY(server.setOriginalInputRoutes(std::make_shared<OriginalInputRoutes>()));
      QVERIFY(server.setOriginalInputCapturedInbox(std::make_shared<OriginalInputCapturedInbox>(capture.epoch())));
      IPlatformComputer::ButtonInfo button(1, 0);
      IPlatformComputer::MotionInfo point =
          returning ? IPlatformComputer::MotionInfo{-1, 0} : IPlatformComputer::MotionInfo{999, 500};
      Event down(
          EventTypes::PrimaryComputerButtonDown, f.primary.getEventTarget(), &button, Event::EventFlags::DontFreeData,
          OriginalInputStamp{epoch, 1, 1}
      );
      Event move(
          returning ? EventTypes::PrimaryComputerMotionOnSecondary : EventTypes::PrimaryComputerMotionOnPrimary,
          f.primary.getEventTarget(), &point, Event::EventFlags::DontFreeData, OriginalInputStamp{epoch, 2, 1}
      );
      QVERIFY(f.events.dispatchEvent(down) && f.events.dispatchEvent(move));
      const auto edge = mailbox->read();
      QVERIFY(edge && !mailbox->readRelease());
      QCOMPARE(edge->edge().returning, returning);
      QCOMPARE(f.first->take().count("DMDN"), returning ? 1 : 0);
      f.clear();
      position->store(3);
      Event up(
          EventTypes::PrimaryComputerButtonUp, f.primary.getEventTarget(), &button, Event::EventFlags::DontFreeData,
          OriginalInputStamp{epoch, 3, 1}
      );
      QVERIFY(f.events.dispatchEvent(up));
      f.platform->mouseHeld = false;
      const auto released = mailbox->readRelease();
      QVERIFY(released && released->live() && !edge->current());
      const auto upBytes = f.first->take();
      QCOMPARE(upBytes.count("DMUP"), returning ? 1 : 0); // The original owner receives exactly one real up.
      QVERIFY(!upBytes.contains("DMDN"));
      QCOMPARE(server.selectedPeer().has_value(), returning);
      QVERIFY(mailbox->requestTransfer(*released, 140, 520));
      QVERIFY(!mailbox->requestTransfer(*released, 141, 520)); // One reservation.
      if (failure == 1)
        position->store(4); // New physical input before Qt wakes.
      if (failure == 2)
        QVERIFY(server.setConfig(f.config));
      if (failure == 3)
        f.first->identity->revoke();
      if (failure == 4)
        capture.revoke(OriginalInputRevocation::Stopped);
      server.drainWindowDragTransfer();
      const auto result = mailbox->transferResult();
      QVERIFY(result && result->generation == released->generation() && result->up == released->up());
      QCOMPARE(result->switched, failure == 0);
      if (!failure) {
        QCOMPARE(
            server.m_active,
            returning ? static_cast<BaseClientProxy *>(&f.primary) : static_cast<BaseClientProxy *>(f.firstClient)
        );
        QCOMPARE(server.selectedPeer().has_value(), !returning);
        QCOMPARE(server.m_x, 140);
        QCOMPARE(server.m_y, 520);
      } else
        QCOMPARE(
            server.m_active,
            returning ? static_cast<BaseClientProxy *>(f.firstClient) : static_cast<BaseClientProxy *>(&f.primary)
        );
      const auto bytes = f.first->take();
      QVERIFY(!bytes.contains("DMDN") && !bytes.contains("DMUP"));
      QVERIFY(!released->live() && !mailbox->requestTransfer(*released, 140, 520));
    }
}

void ServerTests::windowDragEdge_revokesOnReleaseAndTopology()
{
  using namespace deskflow::windowshare;
  for (int cause = 0; cause != 9; ++cause) {
    RoutingFixture f;
    auto &server = *f.server;
    QVERIFY(f.config.connect("primary", Direction::Right, 0, 1, "first", 0, 1));
    QVERIFY(server.setConfig(f.config));
    server.m_disableLockToComputer = false;
    f.platform->mouseHeld = true;
    f.first->authenticate();
    auto mailbox = std::make_shared<WindowDragEdgeMailbox>();
    QVERIFY(server.setWindowDragEdgeMailbox(mailbox));
    QVERIFY(server.setOriginalInputRoutes(std::make_shared<OriginalInputRoutes>()));
    IPlatformComputer::ButtonInfo button(1, 0);
    IPlatformComputer::MotionInfo point{999, 500};
    Event down(
        EventTypes::PrimaryComputerButtonDown, f.primary.getEventTarget(), &button, Event::EventFlags::DontFreeData,
        OriginalInputStamp{81, 1, 1}
    );
    Event move(
        EventTypes::PrimaryComputerMotionOnPrimary, f.primary.getEventTarget(), &point, Event::EventFlags::DontFreeData,
        OriginalInputStamp{81, 2, 1}
    );
    QVERIFY(f.events.dispatchEvent(down) && f.events.dispatchEvent(move));
    const auto observed = mailbox->read();
    QVERIFY(observed);
    switch (cause) {
    case 0:
      server.onMouseUp(1);
      break;
    case 1:
      server.onMouseMovePrimary(500, 500);
      break;
    case 2:
      server.onKeyDown(kKeyEscape, 0, 53, "", "");
      break;
    case 3:
      QVERIFY(server.setConfig(f.config));
      break;
    case 4:
      server.handleShapeChanged(f.firstClient);
      break;
    case 5:
      server.handleClientDisconnected(f.firstClient);
      break;
    case 6:
      server.onScreensaver(true);
      break;
    case 7:
      server.revokeSelectedPeer();
      break;
    case 8:
      f.first->identity->revoke();
      break;
    }
    QVERIFY(!observed->live() && !mailbox->read());
  }
}

void ServerTests::windowDragEdge_honorsDelaysCornersAndOriginalIdentity()
{
  using namespace deskflow::windowshare;
  RoutingFixture f;
  auto &server = *f.server;
  QVERIFY(f.config.connect("primary", Direction::Right, 0.25f, 0.75f, "first", 0, 1));
  QVERIFY(server.setConfig(f.config));
  server.m_disableLockToComputer = false;
  f.platform->mouseHeld = true;
  f.first->authenticate();
  auto mailbox = std::make_shared<WindowDragEdgeMailbox>();
  QVERIFY(server.setWindowDragEdgeMailbox(mailbox));
  QVERIFY(server.setOriginalInputRoutes(std::make_shared<OriginalInputRoutes>()));
  std::uint64_t sequence = 0;
  const auto dispatch = [&](EventTypes type, void *data, bool original = true) {
    const auto stamp = original ? OriginalInputStamp{91, ++sequence, 1} : OriginalInputStamp{};
    return f.events.dispatchEvent(
        Event(type, f.primary.getEventTarget(), data, Event::EventFlags::DontFreeData, stamp)
    );
  };
  IPlatformComputer::ButtonInfo button(1, 0);
  IPlatformComputer::MotionInfo point{999, 500};
  QVERIFY(dispatch(EventTypes::PrimaryComputerButtonDown, &button, false));
  QVERIFY(dispatch(EventTypes::PrimaryComputerMotionOnPrimary, &point));
  QVERIFY(!mailbox->read()); // Configured/synthetic down cannot borrow a later raw move.
  server.onMouseUp(1);
  QVERIFY(dispatch(EventTypes::PrimaryComputerButtonDown, &button));
  point.m_y = 100;
  QVERIFY(dispatch(EventTypes::PrimaryComputerMotionOnPrimary, &point));
  QVERIFY(!mailbox->read()); // Outside the configured edge segment.
  point.m_y = 500;
  server.m_switchWaitDelay = 0.04;
  QVERIFY(dispatch(EventTypes::PrimaryComputerMotionOnPrimary, &point));
  QVERIFY(!mailbox->read());
  QTest::qSleep(45);
  server.handleSwitchWaitTimeout(); // Physical lock still denies the ordinary switch.
  QVERIFY(!mailbox->read());        // Timer cannot manufacture an original motion.
  QVERIFY(dispatch(EventTypes::PrimaryComputerMotionOnPrimary, &point));
  const auto waited = mailbox->read();
  QVERIFY(waited);
  QCOMPARE(waited->edge().destinationY, 501); // Existing fractional edge mapping.
  server.m_lockedToComputer = true;
  QVERIFY(dispatch(EventTypes::PrimaryComputerMotionOnPrimary, &point));
  QVERIFY(!waited->live() && !mailbox->read());
  server.m_lockedToComputer = false;
  server.m_switchWaitDelay = 0;
  server.m_switchTwoTapDelay = 1;
  QVERIFY(dispatch(EventTypes::PrimaryComputerMotionOnPrimary, &point));
  QVERIFY(!mailbox->read());
  server.m_switchTwoTapDelay = 0;
  // addComputer installs per-screen defaults and addOption is insert-only.
  // Change this fixture's owned map, without modifying persistent Settings.
  auto *options = const_cast<deskflow::server::Config::ScreenOptions *>(f.config.getOptions("primary"));
  QVERIFY(options);
  (*options)[kOptionComputerSwitchCorners] = s_topRightCornerMask;
  (*options)[kOptionComputerSwitchCornerSize] = 600;
  QVERIFY(dispatch(EventTypes::PrimaryComputerMotionOnPrimary, &point));
  QVERIFY(!mailbox->read()); // Locked corner wins even with a held valid press.
}

void ServerTests::selectedPeer_requiresCurrentAuthenticatedRemote()
{
  RoutingFixture f;
  auto &server = *f.server;
  QVERIFY(!server.selectedPeer());
  server.switchComputer(f.firstClient, 100, 100, false);
  QVERIFY(!server.selectedPeer());
  f.first->fingerprintOnly = true;
  server.switchComputer(f.firstClient, 100, 100, false);
  QVERIFY(!server.selectedPeer()); // A legacy fingerprint copy is insufficient.
  f.first->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  const auto selected = server.selectedPeer();
  QVERIFY(selected && selected->live());
  QVERIFY(selected->serverIncarnation() != 0);
  QCOMPARE(selected->recipient(), server.m_inputRecipients.at(f.firstClient));
  QVERIFY(selected->generation() != 0);
  QCOMPARE(selected->ticket().incarnation(), f.first->identity->ticket()->incarnation());
  QVERIFY(selected->sameSelection(*server.selectedPeer()));

  f.first->identity->revoke(); // Independent TLS-thread revocation needs no Server event.
  QVERIFY(!selected->live());
  QVERIFY(!server.selectedPeer());
  server.switchComputer(f.firstClient, 100, 100, false);
  QVERIFY(!server.selectedPeer());
  server.switchComputer(&f.primary, 100, 100, false);
  QVERIFY(!server.selectedPeer());
}

void ServerTests::selectedPeer_revokesBeforeSwitchAndFailedLeave()
{
  RoutingFixture f;
  auto &server = *f.server;
  f.first->authenticate();
  f.second->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  const auto first = server.selectedPeer();
  QVERIFY(first);
  f.first->watchedSelection = first;
  f.second->watchedSelection = first;
  f.firstClient->mayLeave = false;
  server.switchComputer(f.secondClient, 100, 100, false);
  QVERIFY(!first->live());
  QVERIFY(!f.first->sawLiveSelection);
  QCOMPARE(server.m_active, static_cast<BaseClientProxy *>(f.firstClient));
  QVERIFY(!server.selectedPeer()); // Failed leave never republishes the old lease.

  f.firstClient->mayLeave = true;
  server.switchComputer(f.secondClient, 100, 100, false);
  const auto second = server.selectedPeer();
  QVERIFY(second && second->live());
  QVERIFY(second->generation() > first->generation());
  QVERIFY(second->recipient() != first->recipient());
  QVERIFY(!second->sameSelection(*first));
  QVERIFY(!f.first->sawLiveSelection);
  QVERIFY(!f.second->sawLiveSelection);
  f.second->watchedSelection = second;
  server.switchComputer(&f.primary, 100, 100, false);
  QVERIFY(!second->live());
  QVERIFY(!f.second->sawLiveSelection);
  QVERIFY(!server.selectedPeer());
}

void ServerTests::selectedPeer_reconnectDoesNotReviveCachedSelection()
{
  RoutingFixture f;
  auto &server = *f.server;
  f.first->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  const auto old = server.selectedPeer();
  QVERIFY(old);
  f.first->watchedSelection = old;
  server.handleClientDisconnected(f.firstClient);
  QVERIFY(!old->live());
  QVERIFY(!f.first->sawLiveSelection);
  QVERIFY(!server.selectedPeer());

  auto replacement = std::make_shared<WireCapture>();
  replacement->authenticate(); // Same name and exact pin, fresh authenticated connection.
  auto *client = new RoutingClient("first", &f.events, replacement);
  server.adoptClient(client);
  server.switchComputer(client, 100, 100, false);
  const auto current = server.selectedPeer();
  QVERIFY(current && current->live());
  QCOMPARE(current->serverIncarnation(), old->serverIncarnation());
  QCOMPARE(current->ticket().fingerprint(), old->ticket().fingerprint());
  QVERIFY(current->recipient() != old->recipient());
  QVERIFY(current->ticket().incarnation() != old->ticket().incarnation());
  QVERIFY(current->generation() > old->generation());
  QVERIFY(!old->live());

  RoutingFixture fresh;
  fresh.first->authenticate();
  fresh.server->switchComputer(fresh.firstClient, 100, 100, false);
  const auto restarted = fresh.server->selectedPeer();
  QVERIFY(restarted);
  QVERIFY(restarted->serverIncarnation() != current->serverIncarnation());
  QVERIFY(!restarted->sameSelection(*current));
}

void ServerTests::selectedPeer_revokesBeforeRemoveCloseAndForceLeave()
{
  for (int operation = 0; operation < 3; ++operation) {
    RoutingFixture f;
    auto &server = *f.server;
    f.first->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    const auto selected = server.selectedPeer();
    QVERIFY(selected);
    f.first->watchedSelection = selected;
    f.platform->watchedSelection = selected;
    f.events.watchedSelection = selected;
    if (operation == 0) {
      QVERIFY(server.removeClient(f.firstClient));
      QVERIFY(!selected->live());
      QVERIFY(selected->ticket().live()); // Selection revokes before stream destruction.
      server.forceLeaveClient(f.firstClient);
      server.handleClientDisconnected(f.firstClient);
      // removeClient detached ownership, so a later disconnect is correctly
      // ignored by Server. The test now owns and must delete this proxy.
      delete f.firstClient;
      f.firstClient = nullptr;
    } else if (operation == 1) {
      server.closeClient(f.firstClient, kMsgCClose);
      QVERIFY(selected->ticket().live());
    } else {
      server.forceLeaveClient(f.firstClient);
      QVERIFY(selected->ticket().live());
    }
    QVERIFY(!selected->live());
    QVERIFY(!server.selectedPeer());
    QVERIFY(!f.first->sawLiveSelection);
    QVERIFY(!f.platform->sawLiveSelection);
    QVERIFY(!f.events.sawLiveSelection);
  }
}

void ServerTests::selectedPeer_configBroadcastAndScreensaverFence()
{
  RoutingFixture f;
  auto &server = *f.server;
  f.first->authenticate();
  f.second->authenticate();
  server.switchComputer(f.firstClient, 100, 100, false);
  auto selected = server.selectedPeer();
  QVERIFY(selected);
  deskflow::server::Config rejected(&f.events);
  rejected.addComputer("first");
  QVERIFY(!server.setConfig(rejected));
  QVERIFY(selected->live());
  QVERIFY(server.selectedPeer()->sameSelection(*selected));

  f.platform->watchedSelection = selected;
  f.first->watchedSelection = selected;
  QVERIFY(server.setConfig(f.config));
  QVERIFY(!selected->live());
  QVERIFY(!f.platform->sawLiveSelection);
  QVERIFY(!f.first->sawLiveSelection);
  const auto configured = server.selectedPeer();
  QVERIFY(configured && configured->generation() > selected->generation());

  const auto broadcast = [&](Server::KeyboardBroadcastInfo::State state, const char *scope) {
    Server::KeyboardBroadcastInfo info(state, scope);
    Event event(EventTypes::ServerKeyboardBroadcast, nullptr, nullptr, Event::EventFlags::DontFreeData);
    event.setDataObject(&info);
    server.handleKeyboardBroadcastEvent(event);
  };
  broadcast(Server::KeyboardBroadcastInfo::kOn, "first");
  QVERIFY(!configured->live());
  QVERIFY(!server.selectedPeer());
  server.switchComputer(f.secondClient, 100, 100, false);
  QVERIFY(!server.selectedPeer());
  broadcast(Server::KeyboardBroadcastInfo::kOn, "second");
  QVERIFY(!server.selectedPeer());
  broadcast(Server::KeyboardBroadcastInfo::kOff, "second");
  selected = server.selectedPeer();
  QVERIFY(selected && selected->live());
  broadcast(Server::KeyboardBroadcastInfo::kOff, "first"); // Scope changes fence even while off.
  QVERIFY(!selected->live());
  selected = server.selectedPeer();
  QVERIFY(selected && selected->live());
  f.second->watchedSelection = selected;
  server.onScreensaver(true);
  QVERIFY(!selected->live());
  QVERIFY(!f.second->sawLiveSelection);
  QVERIFY(!server.selectedPeer());
  server.handleClientDisconnected(f.secondClient); // Clears m_activeSaver while saver stays active.
  server.switchComputer(f.firstClient, 100, 100, false);
  QVERIFY(!server.selectedPeer());
  broadcast(Server::KeyboardBroadcastInfo::kOff, "second");
  QVERIFY(!server.selectedPeer());
  server.onScreensaver(false);
  QVERIFY(server.selectedPeer());
}

void ServerTests::inputFilter_ownedActionsAndConnectionLifetime()
{
  RoutingFixture f;
  RoutingEventQueue events;
  InputFilter filter(&events);
  InputFilter::Rule rule(new InputFilter::ComputerConnectedCondition(&events, ""));
  rule.adoptAction(new InputFilter::SwitchToComputerAction(&events, ""), true);
  filter.addFilterRule(rule);
  filter.setPrimaryClient(&f.primary);
  std::string switched;
  events.addHandler(EventTypes::ServerSwitchToComputer, &filter, [&](const Event &event) {
    QVERIFY(!event.getData());
    const auto *info = dynamic_cast<Server::SwitchToComputerInfo *>(event.getDataObject());
    QVERIFY(info);
    switched = info->m_computer;
  });
  // Exercise the actual InputFilter borrowed-event view, not only Condition.
  Event connected(EventTypes::ServerConnected, f.primary.getEventTarget(), new Server::ComputerConnectedInfo("first"));
  QVERIFY(events.dispatchEvent(connected));
  QCOMPARE(switched, std::string("first"));
  Event::deleteData(connected);

  unsigned received = 0;
  for (const auto type :
       {EventTypes::ServerLockCursorToComputer, EventTypes::ServerSwitchInDirection,
        EventTypes::ServerKeyboardBroadcast}) {
    events.addHandler(type, this, [&](const Event &event) {
      QVERIFY(!event.getData());
      QVERIFY(event.getDataObject());
      QVERIFY(event.getFlags() & Event::EventFlags::DeliverImmediately);
      ++received;
    });
  }
  const Event input(EventTypes::KeyStateKeyDown, this);
  InputFilter::LockCursorToComputerAction(&events).perform(input);
  InputFilter::SwitchInDirectionAction(&events, Direction::Left).perform(input);
  InputFilter::KeyboardBroadcastAction(&events).perform(input);
  QCOMPARE(received, 3U);
  filter.setPrimaryClient(nullptr);
}

void ServerTests::inputFilter_actualQueueObservedBorrowedDispatch()
{
  using namespace deskflow::windowshare;
  RoutingFixture f;
  auto recorder = std::make_unique<InputEvidenceRecorder>(InputEvidenceRole::Core);
  for (std::size_t i = 0; i != InputEvidenceRecorder::queueCount; ++i) {
    const auto queue = static_cast<InputEvidenceQueue>(i);
    if (!eventInputEvidenceQueue(queue))
      recorder->initialize(queue, recorder.get(), 256);
  }
  InputEvidenceOwner owner;
  owner.values.fill(1);
  owner.values[4] = 7;
  owner.identity.fill(7);
  recorder->bindOwner(owner);
  // Independent test process boundary; production never resets this marker.
  deskflow::eventInputQueueConstructed.store(false);
  QVERIFY(deskflow::installEventInputObserver(recorder->eventObserver()));
  struct Reset
  {
    ~Reset()
    {
      deskflow::eventInputObserverInstalled.store(nullptr);
    }
  } reset;
  EventQueue events;
  InputFilter filter(&events);
  filter.setPrimaryClient(&f.primary);
  QVERIFY(recorder->arm(180000000000ULL));
  unsigned borrowed = 0, configured = 0;
  events.addHandler(EventTypes::ServerKeyboardBroadcast, &filter, [&](const Event &event) {
    ++configured;
    QVERIFY(!event.originalInputStamp());
  });
  events.addHandler(EventTypes::KeyStateKeyDown, &filter, [&](const Event &event) {
    ++borrowed;
    QVERIFY(event.getFlags() & Event::EventFlags::DontFreeData);
    QCOMPARE(event.originalInputStamp(), (OriginalInputStamp{7, borrowed, 1}));
    InputFilter::KeyboardBroadcastAction(&events).perform(event);
    if (borrowed == 1) {
      const Event nested(EventTypes::KeyStateKeyDown, f.primary.getEventTarget(), nullptr, 0, {7, 2, 1});
      QVERIFY(events.dispatchEvent(nested));
    }
    recorder->stop();
    recorder->finish();
    QCOMPARE(recorder->state(), InputEvidenceState::Draining);
  });
  const Event source(EventTypes::KeyStateKeyDown, f.primary.getEventTarget(), nullptr, 0, {7, 1, 1});
  QVERIFY(events.dispatchEvent(source));
  QCOMPARE(borrowed, 2U);
  QCOMPARE(configured, 2U);
  recorder->finish();
  QCOMPARE(recorder->state(), InputEvidenceState::Stopped);
  QCOMPARE(recorder->loss(), InputEvidenceLoss::None);
  std::array<std::uint64_t, 2> peaks{}, added{}, retired{};
  while (const auto row = recorder->pop()) {
    QVERIFY(row->kind == InputEvidenceKind::Queue); // no duplicate physical-source records
    if (row->queue == InputEvidenceQueue::EventOutstanding || row->queue == InputEvidenceQueue::EventDispatch) {
      const auto index = row->queue == InputEvidenceQueue::EventDispatch;
      peaks[index] = std::max(peaks[index], row->depth);
      added[index] = row->enqueued;
      retired[index] = row->retired;
      QCOMPARE(row->capacity, std::uint64_t{0});
    }
  }
  QCOMPARE(peaks, (std::array<std::uint64_t, 2>{4, 4}));
  QCOMPARE(added, peaks);
  QCOMPARE(retired, peaks);
  filter.setPrimaryClient(nullptr);
}

void ServerTests::selectedPeer_shutdownAndOverflowAreTerminal()
{
  std::atomic<std::uint64_t> next{std::numeric_limits<std::uint64_t>::max() - 1};
  QCOMPARE(Server::reserveServerIncarnation(next), std::numeric_limits<std::uint64_t>::max());
  QCOMPARE(Server::reserveServerIncarnation(next), std::uint64_t{0});
  QCOMPARE(Server::reserveServerIncarnation(next), std::uint64_t{0});
  for (int operation = 0; operation < 4; ++operation) {
    RoutingFixture f;
    auto &server = *f.server;
    f.first->authenticate();
    server.switchComputer(f.firstClient, 100, 100, false);
    const auto selected = server.selectedPeer();
    QVERIFY(selected);
    f.first->watchedSelection = selected;
    f.platform->watchedSelection = selected;
    if (operation == 0) {
      server.revokeSelectedPeer();
      server.revokeSelectedPeer();
      server.switchComputer(f.firstClient, 100, 100, false);
      QVERIFY(server.setConfig(f.config));
      QVERIFY(!server.selectedPeer());
    } else if (operation == 1) {
      server.m_selectionGeneration = std::numeric_limits<std::uint64_t>::max();
      server.switchComputer(f.firstClient, 100, 100, false);
      QVERIFY(server.m_selectionShutdown);
      QVERIFY(!server.selectedPeer());
      server.switchComputer(&f.primary, 100, 100, false);
      server.switchComputer(f.firstClient, 100, 100, false);
      QVERIFY(!server.selectedPeer());
    } else if (operation == 2) {
      server.disconnect();
      QVERIFY(!server.selectedPeer());
    } else {
      f.server.reset();
    }
    QVERIFY(!selected->live());
    QVERIFY(!f.first->sawLiveSelection);
    QVERIFY(!f.platform->sawLiveSelection);
  }
}
#endif

void ServerTests::connectionEvent_destroysOwnedPayload()
{
  int destroyed = 0;
  struct Payload : Server::ComputerConnectedInfo
  {
    explicit Payload(int &count) : ComputerConnectedInfo(std::string(1024, 'x')), count(count)
    {
    }
    ~Payload() override
    {
      ++count;
    }
    int &count;
  };
  Event event(EventTypes::ServerConnected, this, new Payload(destroyed));
  QVERIFY(!event.getData());
  QVERIFY(event.getDataObject());
  Event::deleteData(event);
  QCOMPARE(destroyed, 1);
}

void ServerTests::SwitchToComputerInfo_alloc_computer()
{
  auto actual = new Server::SwitchToComputerInfo("test");
  QCOMPARE(actual->m_computer, "test");
  delete actual;
}

void ServerTests::KeyboardBroadcastInfo_alloc_stateAndComputers()
{
  auto info = new Server::KeyboardBroadcastInfo(Server::KeyboardBroadcastInfo::State::kOn, "test");
  QCOMPARE(info->m_state, Server::KeyboardBroadcastInfo::State::kOn);
  QCOMPARE(info->m_computers, "test");
  delete info;
}

QTEST_MAIN(ServerTests)
