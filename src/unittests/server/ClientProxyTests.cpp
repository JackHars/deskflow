/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClientProxyTests.h"

#include "../deskflow/MockEventQueue.h"
#include "deskflow/AppUtil.h"
#include "deskflow/PacketStreamFilter.h"
#include "io/IStream.h"
#include "server/ClientProxy1_0.h"
#include "server/ClientProxy1_1.h"
#include "server/ClientProxy1_2.h"
#include "server/ClientProxy1_3.h"
#include "server/ClientProxy1_6.h"
#include "server/ClientProxy1_7.h"
#include "server/ClientProxy1_8.h"

#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QByteArray>
#include <QTest>

namespace {

class TestAppUtil : public AppUtil
{
public:
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

class CapturingStream : public deskflow::IStream
{
public:
  QByteArray take()
  {
    auto bytes = m_buffer;
    m_buffer.clear();
    return bytes;
  }

  void write(const void *buffer, uint32_t n) override
  {
    m_buffer.append(static_cast<const char *>(buffer), n);
    const auto callback = onWrite;
    if (callback)
      callback(std::string_view(static_cast<const char *>(buffer), n));
  }

  void close() override
  {
    closed = true;
    if (identity)
      identity->revoke();
  }

  uint32_t read(void *buffer, uint32_t count) override
  {
    const auto size = std::min<std::uint32_t>(count, incoming.size());
    if (buffer)
      std::memcpy(buffer, incoming.constData(), size);
    incoming.remove(0, size);
    return size;
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
    return const_cast<CapturingStream *>(this);
  }

  bool isReady() const override
  {
    return !incoming.isEmpty();
  }

  uint32_t getSize() const override
  {
    return incoming.size();
  }

  std::optional<deskflow::AuthenticatedPeerTicket> authenticatedPeerTicket() const override
  {
    return identity ? identity->ticket() : std::nullopt;
  }
  std::optional<PeerFingerprint> authenticatedPeerFingerprint() const override
  {
    const auto ticket = authenticatedPeerTicket();
    return ticket ? std::optional(ticket->fingerprint()) : std::nullopt;
  }
  void authenticate()
  {
    if (identity)
      identity->revoke();
    identity = std::make_unique<deskflow::AuthenticatedPeerIssuer>();
    deskflow::AuthenticatedPeerTicket::Fingerprint fingerprint{};
    fingerprint.fill(0x5a);
    if (!identity->issue(fingerprint))
      throw std::runtime_error("Unable to create fake TLS lifetime");
  }
  QByteArray incoming;
  bool closed = false;
  std::unique_ptr<deskflow::AuthenticatedPeerIssuer> identity;
  std::function<void(std::string_view)> onWrite;

private:
  QByteArray m_buffer;
};

std::unique_ptr<ClientProxy> makeProxy(int minor, deskflow::IStream *stream, IEventQueue *events)
{
  // the 1.4 and later constructors assert the server pointer is non-null but only store it
  auto *server = reinterpret_cast<Server *>(0x1);

  std::unique_ptr<ClientProxy> proxy;
  switch (minor) {
  case 0:
    proxy = std::make_unique<ClientProxy1_0>("client", stream, events);
    break;
  case 1:
    proxy = std::make_unique<ClientProxy1_1>("client", stream, events);
    break;
  case 2:
    proxy = std::make_unique<ClientProxy1_2>("client", stream, events);
    break;
  case 3:
    proxy = std::make_unique<ClientProxy1_3>("client", stream, events);
    break;
  case 6:
    proxy = std::make_unique<ClientProxy1_6>("client", stream, server, events);
    break;
  case 7:
    proxy = std::make_unique<ClientProxy1_7>("client", stream, server, events);
    break;
  case 8:
    proxy = std::make_unique<ClientProxy1_8>("client", stream, server, events);
    break;
  default:
    break;
  }
  return proxy;
}

struct ProxyUnderTest
{
  MockEventQueue events;
  CapturingStream *stream = new CapturingStream;
  std::unique_ptr<ClientProxy> proxy;

  explicit ProxyUnderTest(int minor) : proxy(makeProxy(minor, stream, &events))
  {
    // drop the query-info and layout-sync messages the constructors send
    stream->take();
  }
};

const KeyID kKey = 0x61;
const KeyModifierMask kMask = 0;
const KeyButton kButton = 0x1e;
const int32_t kCount = 3;
const std::string kLang = "en";

using namespace deskflow::windowshare;
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
constexpr bool traceBuilt = true;
#else
constexpr bool traceBuilt = false;
#endif

class TraceEventQueue : public MockEventQueue
{
public:
  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  {
    handlers[{type, target}] = handler;
  }
  void removeHandler(EventTypes type, void *target) override
  {
    handlers.erase({type, target});
  }
  void removeHandlers(void *target) override
  {
    std::erase_if(handlers, [&](const auto &entry) { return entry.first.second == target; });
  }
  bool dispatchEvent(const Event &event) override
  {
    auto found = handlers.find({event.getType(), event.getTarget()});
    if (found == handlers.end())
      found = handlers.find({EventTypes::Unknown, event.getTarget()});
    if (found == handlers.end())
      return false;
    const auto callback = found->second;
    callback(event);
    return true;
  }
  void addEvent(Event &&event) override
  {
    if (event.getType() == EventTypes::ClientProxyDisconnected)
      ++disconnections;
  }
  unsigned disconnections = 0;
  std::map<std::pair<EventTypes, void *>, EventHandler> handlers;
};

QByteArray integer(std::uint64_t value, unsigned count)
{
  std::string result;
  inputSequenceDetail::append(result, value, count);
  return QByteArray::fromStdString(result);
}

struct TraceFixture
{
  TraceEventQueue events;
  CapturingStream *stream = new CapturingStream;
  std::unique_ptr<ClientProxy> proxy;
  explicit TraceFixture(int minor = 8, bool authenticated = true)
  {
    if (authenticated)
      stream->authenticate();
    proxy = makeProxy(minor, new PacketStreamFilter(&events, stream), &events);
    stream->take();
    receive({"DINF" + QByteArray::fromHex("0000 0000 0400 0300 0000 0200 0180")});
    stream->take();
  }
  void receive(std::initializer_list<QByteArray> messages)
  {
    for (const auto &message : messages)
      stream->incoming += integer(message.size(), 4) + message;
    events.dispatchEvent(Event(EventTypes::StreamInputReady, stream->getEventTarget()));
  }
  std::vector<QByteArray> take()
  {
    const auto wire = stream->take();
    std::vector<QByteArray> result;
    std::size_t offset = 0;
    const std::string_view view(wire.constData(), wire.size());
    while (offset < view.size()) {
      if (view.size() - offset < 4)
        throw std::runtime_error("Partial packet length written");
      const auto size = inputSequenceDetail::read(view, offset, 4);
      if (size > view.size() - offset)
        throw std::runtime_error("Partial packet body written");
      result.emplace_back(view.data() + offset, size);
      offset += size;
    }
    return result;
  }
  std::uint32_t advertise()
  {
    const auto generation = proxy->requestInputSequenceTrace();
    if (!generation)
      throw std::runtime_error("Trace request unexpectedly denied");
    proxy->setOptions({kOptionInputSequenceTrace, *generation});
    take();
    return *generation;
  }
  std::uint32_t negotiate()
  {
    const auto generation = advertise();
    receive({"CISA" + integer(generation, 4)});
    return generation;
  }
  std::uint32_t negotiateCutoff(bool mapped = false, bool sealed = false, bool batched = false)
  {
    const auto generation = batched  ? proxy->requestBatchedInputCutoffCapability()
                            : sealed ? proxy->requestSealedInputCutoffCapability()
                            : mapped ? proxy->requestMappedInputCutoffCapability()
                                     : proxy->requestInputCutoffCapability();
    if (!generation)
      throw std::runtime_error("Cutoff capability request denied");
    proxy->setOptions(
        {batched  ? kOptionBatchedInputCutoff
         : sealed ? kOptionSealedInputCutoff
         : mapped ? kOptionMappedInputCutoff
                  : kOptionInputCutoff,
         *generation}
    );
    take();
    receive({QByteArray(batched ? "CICB" : sealed ? "CICS" : mapped ? "CIMA" : "CICA") + integer(*generation, 4)});
    return *generation;
  }
  std::uint32_t negotiatePreparation()
  {
    const auto generation = proxy->requestInputPreparationCapability();
    if (!generation)
      throw std::runtime_error("Preparation capability unavailable");
    proxy->setOptions({kOptionBatchedInputCutoff, *generation, kOptionInputPreparation, *generation});
    take();
    receive({"CICB" + integer(*generation, 4), "CIPA" + integer(*generation, 4)});
    if (!proxy->inputPreparationCapabilityReady())
      throw std::runtime_error("Preparation not acknowledged");
    return *generation;
  }
  std::optional<InputPreparationBinding> startPreparation()
  {
    return proxy->startInputPreparation(
        {31, 401, 3}, {-10, 27}, InputCutoffClock::now() + std::chrono::milliseconds(450)
    );
  }
  std::optional<InputCutoffBinding> startCutoff()
  {
    return proxy->startInputCutoff(
        {31, 401, 1}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17,
        InputCutoffClock::now() + std::chrono::milliseconds(200)
    );
  }
  std::optional<InputCutoffBinding> startMappedCutoff(InputCutoffPoint point = {-10, 27})
  {
    return proxy->startMappedInputCutoff(
        {31, 401, 3}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17, point,
        InputCutoffClock::now() + std::chrono::milliseconds(200)
    );
  }
  void cutoffReceipt(const InputCutoffBinding &binding, InputCutoffState state)
  {
    receive({QByteArray::fromStdString(encodeInputCutoffReceipt({binding, state}))});
  }
  void receipt(
      std::uint32_t generation, std::uint64_t sequence, InputSequenceOutcome outcome = InputSequenceOutcome::Submitted,
      std::uint16_t count = 1
  )
  {
    receive({QByteArray::fromStdString(encodeInputSequenceReceipt({generation, sequence, outcome, count}))});
  }
};

std::optional<InputSequenceEnvelope> envelope(const QByteArray &packet)
{
  if (packet.size() < 6 || !packet.startsWith("DSEQ"))
    return std::nullopt;
  const std::string_view bytes(packet.constData(), packet.size());
  std::size_t offset = 4;
  const auto size = inputSequenceDetail::read(bytes, offset, 2);
  if (size != bytes.size() - offset)
    return std::nullopt;
  return decodeInputSequenceEnvelope(bytes.substr(offset));
}

InputCutoffMappedResult mappedResult(InputCutoffBinding binding, InputCutoffPoint point = {-10, 27})
{
  InputCutoffMappedResult result;
  result.binding = binding;
  result.point = point;
  result.sourceX = 10.25;
  result.sourceY = 20.5;
  result.sourceWidth = 1920;
  result.sourceHeight = 1080;
  result.surfaceIncarnation = 18;
  result.viewportRevision = 19;
  result.nativeSurface = 20;
  result.providerGeneration = 21;
  result.nativeRevision = 22;
  result.viewerPeerFingerprint.fill(0x7a);
  result.orderingContract = "test-reviewed-build:123";
  return result;
}

} // namespace

void ClientProxyTests::initTestCase()
{
  // the 1.8 constructor reads the keyboard layouts through AppUtil::instance()
  static TestAppUtil appUtil;
}

// These formats are frozen because shipped third-party clients parse them byte
// for byte: Synergy 1.4 through 1.14.1 negotiate 1.4 through 1.7, Barrier and
// Input Leap negotiate 1.6, and Synergy 1.14.2 onwards and Deskflow negotiate 1.8.
void ClientProxyTests::keyDown_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKDN" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.2") << 2 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.3") << 3 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKDL" + QByteArray::fromHex("0061 0000 001e 00000002") + "en";
}

void ClientProxyTests::keyDown()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyDown(kKey, kMask, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyRepeat_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKRP" + QByteArray::fromHex("0061 0000 0003");
  QTest::newRow("1.1") << 1 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.6") << 6 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.7") << 7 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.8") << 8 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e 00000002") + "en";
}

void ClientProxyTests::keyRepeat()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyRepeat(kKey, kMask, kCount, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyUp_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKUP" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
}

void ClientProxyTests::keyUp()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyUp(kKey, kMask, kButton);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::inputSequence_requiresAuthenticatedProtocol18()
{
  for (const auto minor : {0, 1, 2, 3, 6, 7}) {
    TraceFixture test(minor);
    QVERIFY(!test.proxy->requestInputSequenceTrace());
    test.proxy->setOptions({kOptionInputSequenceTrace, 1});
    QCOMPARE(test.take().front(), "DSOP" + QByteArray::fromHex("00000000"));
    InputSequenceOriginScope origin({10, 20, 1});
    test.proxy->mouseDown(1);
    QCOMPARE(test.take().front(), "DMDN" + QByteArray::fromHex("01"));
  }
  TraceFixture plain(8, false);
  QVERIFY(!plain.proxy->requestInputSequenceTrace());
  plain.receive({"CISA" + integer(1, 4)});
  QVERIFY(plain.stream->closed);
  QCOMPARE(plain.events.disconnections, 1u);

  TraceFixture test;
  if (!traceBuilt) {
    QVERIFY(!test.proxy->requestInputSequenceTrace());
    test.proxy->setOptions({kOptionInputSequenceTrace, 1});
    QCOMPARE(test.take().front(), "DSOP" + QByteArray::fromHex("00000000"));
    return;
  }
  const auto generation = test.advertise();
  InputSequenceOriginScope origin({10, 20, 1});
  test.proxy->mouseDown(1);
  QCOMPARE(test.take().front(), "DMDN" + QByteArray::fromHex("01")); // No ACK yet.
  test.receive({"CISA" + integer(generation, 4)});
  test.proxy->mouseUp(1);
  QVERIFY(envelope(test.take().front()));
}

void ClientProxyTests::inputSequence_wrapsAllInputWriters_data()
{
  QTest::addColumn<int>("writer");
  QTest::addColumn<QByteArray>("payload");
  QTest::newRow("key-down-1.8") << 0 << "DKDL" + QByteArray::fromHex("0061 0000 001e 00000002") + "en";
  QTest::newRow("key-repeat-1.8") << 1 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e 00000002") + "en";
  QTest::newRow("key-up-1.1") << 2 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("button-down-1.0") << 3 << "DMDN" + QByteArray::fromHex("01");
  QTest::newRow("button-up-1.0") << 4 << "DMUP" + QByteArray::fromHex("01");
  QTest::newRow("motion-1.0") << 5 << "DMMV" + QByteArray::fromHex("fffd 007b");
  QTest::newRow("relative-1.2") << 6 << "DMRM" + QByteArray::fromHex("fff9 000b");
  QTest::newRow("wheel-1.3") << 7 << "DMWM" + QByteArray::fromHex("ff88 00f0");
}

void ClientProxyTests::inputSequence_wrapsAllInputWriters()
{
  if (!traceBuilt)
    QSKIP("Input sequence tracing is disabled in this build");
  QFETCH(int, writer);
  QFETCH(QByteArray, payload);
  TraceFixture test;
  const auto generation = test.negotiate();
  const OriginalInputStamp stamp{0xfedcba9876543210, 0xabcdef0123456789, 16};
  InputSequenceOriginScope origin(stamp);
  switch (writer) {
  case 0:
    test.proxy->keyDown(kKey, kMask, kButton, kLang);
    break;
  case 1:
    test.proxy->keyRepeat(kKey, kMask, kCount, kButton, kLang);
    break;
  case 2:
    test.proxy->keyUp(kKey, kMask, kButton);
    break;
  case 3:
    test.proxy->mouseDown(1);
    break;
  case 4:
    test.proxy->mouseUp(1);
    break;
  case 5:
    test.proxy->mouseMove(-3, 123);
    break;
  case 6:
    test.proxy->mouseRelativeMove(-7, 11);
    break;
  case 7:
    test.proxy->mouseWheel(-120, 240);
    break;
  }
  const auto packets = test.take();
  QCOMPARE(packets.size(), 1u);
  const auto decoded = envelope(packets.front());
  QVERIFY(decoded);
  QCOMPARE(decoded->token, (InputSequenceToken{generation, 1, stamp}));
  QCOMPARE(QByteArray::fromStdString(decoded->payload), payload);
  QCOMPARE(test.proxy->inputSequenceStatistics().outstanding, 1u);
}

void ClientProxyTests::inputSequence_preservesControlsAndUnstampedInput()
{
  if (!traceBuilt)
    QSKIP("Input sequence tracing is disabled in this build");
  TraceFixture test;
  test.negotiate();
  test.proxy->mouseMove(10, 20);
  QCOMPARE(test.take().front(), "DMMV" + QByteArray::fromHex("000a 0014"));
  InputSequenceOriginScope outer({7, 8, 1});
  test.proxy->enter(10, 20, 30, 0, false);
  test.proxy->leave();
  test.proxy->screensaver(true);
  const auto controls = test.take();
  QCOMPARE(controls.size(), 3u);
  QVERIFY(controls[0].startsWith("CINN"));
  QCOMPARE(controls[1], QByteArray("COUT"));
  QCOMPARE(controls[2], "CSEC" + QByteArray::fromHex("01"));
  {
    InputSequenceOriginScope nested({7, 9, 2});
    test.proxy->mouseDown(1);
    QCOMPARE(envelope(test.take().front())->token.capture, (OriginalInputStamp{7, 9, 2}));
  }
  test.proxy->mouseUp(1);
  QCOMPARE(envelope(test.take().front())->token.capture, (OriginalInputStamp{7, 8, 1}));
}

void ClientProxyTests::inputSequence_receiptRetainsExactOrigin()
{
  if (!traceBuilt)
    QSKIP("Input sequence tracing is disabled in this build");
  TraceFixture test;
  const auto generation = test.negotiate();
  const OriginalInputStamp stamp{99, 42, 3};
  std::vector<InputSequenceToken> tokens;
  std::vector<InputSequenceReceipt> receipts;
  test.proxy->onInputSequenceReceipt = [&](const auto &token, const auto &receipt) {
    tokens.push_back(token);
    receipts.push_back(receipt);
    QCOMPARE(test.proxy->inputSequenceStatistics().outstanding, 2u - tokens.size());
  };
  {
    InputSequenceOriginScope origin(stamp);
    test.proxy->mouseDown(1);
    test.proxy->mouseUp(1); // Expanded source dispatch: same stamp, different wire sequence.
  }
  const auto packets = test.take();
  QCOMPARE(envelope(packets[0])->token, (InputSequenceToken{generation, 1, stamp}));
  QCOMPARE(envelope(packets[1])->token, (InputSequenceToken{generation, 2, stamp}));
  test.receipt(generation, 2, InputSequenceOutcome::Expanded, 4);
  test.receipt(generation, 1, InputSequenceOutcome::Filtered, 0);
  QCOMPARE(tokens.size(), 2u);
  QCOMPARE(tokens[0], (InputSequenceToken{generation, 2, stamp}));
  QCOMPARE(receipts[1], (InputSequenceReceipt{generation, 1, InputSequenceOutcome::Filtered, 0}));
  QCOMPARE(test.proxy->inputSequenceStatistics().receipts, 2u);
  QVERIFY(!test.stream->closed);
  test.receipt(generation, 1, InputSequenceOutcome::Filtered, 0);
  QVERIFY(test.stream->closed); // Exactly one terminal receipt per token.
  QCOMPARE(tokens.size(), 2u);
}

void ClientProxyTests::inputSequence_resetAndDuplicateNegotiation()
{
  if (!traceBuilt)
    QSKIP("Input sequence tracing is disabled in this build");
  TraceFixture test;
  const auto first = test.negotiate();
  InputSequenceOriginScope origin({10, 11, 1});
  test.proxy->mouseDown(1);
  test.take();
  QCOMPARE(test.advertise(), first);
  test.receive({"CISA" + integer(first, 4)});
  test.proxy->setOptions({kOptionHeartbeat, 2000}); // Incremental unrelated options preserve trace.
  test.take();
  test.proxy->mouseUp(1);
  QCOMPARE(envelope(test.take().front())->token.wireSequence, 2u);
  test.proxy->resetOptions();
  test.take();
  test.receive({"CISA" + integer(first, 4)});
  test.proxy->mouseMove(1, 2);
  QVERIFY(!envelope(test.take().front()));
  const auto second = test.negotiate();
  QCOMPARE(second, first + 1);
  unsigned callbacks = 0;
  test.proxy->onInputSequenceReceipt = [&](const auto &, const auto &) { ++callbacks; };
  test.receipt(first, 2);
  test.receipt(first, 1);
  QCOMPARE(test.proxy->inputSequenceStatistics().retiredReceipts, 2u);
  QCOMPARE(callbacks, 0u);
  test.proxy->mouseMove(1, 2);
  QCOMPARE(envelope(test.take().front())->token, (InputSequenceToken{second, 1, {10, 11, 1}}));
  test.receipt(second, 1);
  QCOMPARE(callbacks, 1u);
  test.proxy->setOptions({kOptionInputSequenceTrace, 0});
  test.take();
  test.receive({"CISA" + integer(second, 4)});
  test.proxy->mouseMove(1, 2);
  QVERIFY(!envelope(test.take().front()));
}

void ClientProxyTests::inputSequence_rejectsMalformedAndUnsolicitedReplies_data()
{
  QTest::addColumn<QByteArray>("reply");
  QTest::newRow("zero-ack") << "CISA" + integer(0, 4);
  QTest::newRow("future-ack") << "CISA" + integer(2, 4);
  QTest::newRow("future-receipt") << "CISR" + integer(2, 4) + integer(1, 8) + QByteArray::fromHex("030001");
  QTest::newRow("unknown-wire") << "CISR" + integer(1, 4) + integer(99, 8) + QByteArray::fromHex("030001");
  QTest::newRow("zero-wire") << "CISR" + integer(1, 4) + integer(0, 8) + QByteArray::fromHex("030001");
  QTest::newRow("unknown-outcome") << "CISR" + integer(1, 4) + integer(1, 8) + QByteArray::fromHex("060000");
  QTest::newRow("submitted-zero") << "CISR" + integer(1, 4) + integer(1, 8) + QByteArray::fromHex("030000");
  QTest::newRow("filtered-nonzero") << "CISR" + integer(1, 4) + integer(1, 8) + QByteArray::fromHex("020001");
}

void ClientProxyTests::inputSequence_rejectsMalformedAndUnsolicitedReplies()
{
  if (!traceBuilt)
    QSKIP("Input sequence tracing is disabled in this build");
  QFETCH(QByteArray, reply);
  TraceFixture test;
  test.negotiate();
  InputSequenceOriginScope origin({1, 2, 1});
  test.proxy->mouseDown(1);
  test.take();
  unsigned callbacks = 0;
  test.proxy->onInputSequenceReceipt = [&](const auto &, const auto &) { ++callbacks; };
  test.receive({reply});
  QVERIFY(test.stream->closed);
  QCOMPARE(test.events.disconnections, 1u);
  QCOMPARE(callbacks, 0u);
  QCOMPARE(test.proxy->inputSequenceStatistics().outstanding, 0u);
}

void ClientProxyTests::inputSequence_packetBoundaries_data()
{
  QTest::addColumn<QByteArray>("first");
  QTest::addColumn<QByteArray>("second");
  QTest::newRow("ack-code-only") << QByteArray("CISA") << integer(1, 4);
  QTest::newRow("ack-short-body") << "CISA" + QByteArray::fromHex("0000") << QByteArray::fromHex("0001");
  QTest::newRow("ack-extra-body") << "CISA" + integer(1, 4) + "CNOP" << QByteArray("CNOP");
  QTest::newRow("receipt-code-only") << QByteArray("CISR")
                                     << integer(1, 4) + integer(1, 8) + QByteArray::fromHex("030001");
  QTest::newRow("receipt-short-body") << "CISR" + integer(1, 4) << integer(1, 8) + QByteArray::fromHex("030001");
  QTest::newRow("receipt-extra-body") << "CISR" + integer(1, 4) + integer(1, 8) + QByteArray::fromHex("03000100")
                                      << QByteArray("CNOP");
}

void ClientProxyTests::inputSequence_packetBoundaries()
{
  if (!traceBuilt)
    QSKIP("Input sequence tracing is disabled in this build");
  QFETCH(QByteArray, first);
  QFETCH(QByteArray, second);
  TraceFixture test;
  test.negotiate();
  InputSequenceOriginScope origin({1, 2, 1});
  test.proxy->mouseDown(1);
  test.take();
  test.receive({first, second});
  QVERIFY(test.stream->closed);
  QCOMPARE(test.events.disconnections, 1u);
  QCOMPARE(test.proxy->inputSequenceStatistics().receipts, 0u);
}

void ClientProxyTests::inputSequence_boundsPreserveOrdinaryInput()
{
  if (!traceBuilt)
    QSKIP("Input sequence tracing is disabled in this build");
  TraceFixture test;
  const auto generation = test.negotiate();
  InputSequenceOriginScope origin({5, 6, 1});
  for (std::size_t index = 0; index < inputSequenceMaximumOutstanding; ++index)
    test.proxy->mouseMove(1, 2);
  QCOMPARE(test.take().size(), inputSequenceMaximumOutstanding);
  test.proxy->mouseMove(1, 2);
  QCOMPARE(test.take().front(), "DMMV" + QByteArray::fromHex("0001 0002"));
  QCOMPARE(test.proxy->inputSequenceStatistics().outstanding, inputSequenceMaximumOutstanding);
  QCOMPARE(test.proxy->inputSequenceStatistics().unavailable, 1u);
  test.receipt(generation, 1);
  test.proxy->mouseMove(1, 2);
  QCOMPARE(envelope(test.take().front())->token.wireSequence, inputSequenceMaximumOutstanding + 1);
  test.receipt(generation, 2);
  const std::string largeLanguage(inputSequenceMaximumPayload, 'x');
  test.proxy->keyDown(kKey, kMask, kButton, largeLanguage);
  const auto large = test.take().front();
  QVERIFY(large.startsWith("DKDL"));
  QVERIFY(large.endsWith(QByteArray::fromStdString(largeLanguage)));
  QCOMPARE(test.proxy->inputSequenceStatistics().unavailable, 2u);
  QCOMPARE(test.proxy->inputSequenceStatistics().outstanding, inputSequenceMaximumOutstanding - 1);
}

void ClientProxyTests::inputSequence_tlsRevocationAndReplacement()
{
  if (!traceBuilt)
    QSKIP("Input sequence tracing is disabled in this build");
  for (bool replace : {false, true}) {
    TraceFixture test;
    const auto generation = test.negotiate();
    InputSequenceOriginScope origin({1, 2, 1});
    test.proxy->mouseDown(1);
    test.take();
    if (replace)
      test.stream->authenticate();
    else
      test.stream->identity->revoke();
    test.proxy->mouseUp(1);
    QCOMPARE(test.take().front(), "DMUP" + QByteArray::fromHex("01"));
    QVERIFY(!test.proxy->requestInputSequenceTrace());
    test.receipt(generation, 1);
    QVERIFY(test.stream->closed);
    QCOMPARE(test.proxy->inputSequenceStatistics().receipts, 0u);
  }
  TraceFixture reconnected;
  QCOMPARE(reconnected.negotiate(), 1u); // New connection has its own generation namespace/TLS lifetime.
}

void ClientProxyTests::inputSequence_counterExhaustion()
{
  if (!traceBuilt)
    QSKIP("Input sequence tracing is disabled in this build");
  TraceFixture test;
  test.negotiate();
  test.proxy->m_inputSequenceWire = std::numeric_limits<std::uint64_t>::max();
  InputSequenceOriginScope origin({1, 2, 1});
  test.proxy->mouseDown(1);
  QCOMPARE(test.take().front(), "DMDN" + QByteArray::fromHex("01"));
  QCOMPARE(test.proxy->inputSequenceStatistics().outstanding, 0u);
  test.proxy->resetOptions();
  test.proxy->m_inputSequenceIssued = std::numeric_limits<std::uint32_t>::max();
  QVERIFY(!test.proxy->requestInputSequenceTrace());
}

void ClientProxyTests::inputSequence_callbackMayResetOrDestroy()
{
  if (!traceBuilt)
    QSKIP("Input sequence tracing is disabled in this build");
  for (bool destroy : {false, true}) {
    TraceFixture test;
    const auto generation = test.negotiate();
    InputSequenceOriginScope origin({1, 2, 1});
    test.proxy->mouseDown(1);
    test.take();
    unsigned callbacks = 0;
    test.proxy->onInputSequenceReceipt = [&](const auto &, const auto &) {
      ++callbacks;
      if (destroy)
        test.proxy.reset();
      else
        test.proxy->resetOptions();
    };
    test.receipt(generation, 1);
    QCOMPARE(callbacks, 1u);
    if (destroy) {
      QVERIFY(!test.proxy);
    } else {
      test.take();
      test.proxy->mouseUp(1);
      QCOMPARE(test.take().front(), "DMUP" + QByteArray::fromHex("01"));
    }
  }
}

void ClientProxyTests::inputCutoff_requiresNegotiationAndRetainsBinding()
{
  for (int minor : {0, 6, 7, 8}) {
    for (bool authenticated : {false, true}) {
      TraceFixture f(minor, authenticated);
      QCOMPARE(f.proxy->getStream()->authenticatedPeerFingerprint(), f.stream->authenticatedPeerFingerprint());
      const bool supported = traceBuilt && minor == 8 && authenticated;
      QVERIFY(!f.startCutoff());
      const auto generation = f.proxy->requestInputCutoffCapability();
      QCOMPARE(generation.has_value(), supported);
      if (!supported) {
        QVERIFY(!f.proxy->requestMappedInputCutoffCapability());
        QVERIFY(!f.startMappedCutoff());
        f.proxy->mouseUp(1);
        QCOMPARE(f.take().front(), "DMUP" + QByteArray::fromHex("01"));
        continue;
      }
      f.proxy->setOptions({kOptionInputCutoff, *generation});
      f.take();
      QVERIFY(!f.startCutoff()); // Ordinary options do not prove peer support.
      f.receive({"CICA" + integer(*generation, 4)});
      const auto binding = f.startCutoff();
      QVERIFY(binding);
      const auto packets = f.take();
      QCOMPARE(packets.size(), 1u);
      const auto request =
          decodeInputCutoffRequest(std::string_view(packets[0].constData() + 4, packets[0].size() - 4));
      QVERIFY(request && request->binding == *binding && request->remainingMs > 0 && request->remainingMs <= 200);
      QCOMPARE(binding->coreConnectionGeneration, *generation);
      QVERIFY(!f.startCutoff()); // Only one pending or frozen cutoff.
      f.cutoffReceipt(*binding, InputCutoffState::BackendObserved);
      QCOMPARE(f.proxy->inputCutoffState(*binding), InputCutoffState::BackendObserved);
      auto wrong = *binding;
      ++wrong.viewerConnection;
      QVERIFY(!f.proxy->resumeInputCutoff(wrong));
      QCOMPARE(f.proxy->inputCutoffState(wrong), InputCutoffState::Denied);
      QVERIFY(f.proxy->resumeInputCutoff(*binding));
      QCOMPARE(f.proxy->inputCutoffState(*binding), InputCutoffState::Resuming);
      QVERIFY(!f.startCutoff()); // Ordered write is not yet an exact Resumed receipt.
      f.take();
      f.cutoffReceipt(*binding, InputCutoffState::BackendObserved); // Late PONG is obsolete after resume starts.
      QCOMPARE(f.proxy->inputCutoffState(*binding), InputCutoffState::Resuming);
      f.cutoffReceipt(*binding, InputCutoffState::Resumed);
      QCOMPARE(f.proxy->inputCutoffState(*binding), InputCutoffState::Resumed);
      const auto newer = f.startCutoff();
      QVERIFY(newer && newer->request > binding->request);
      f.cutoffReceipt(*binding, InputCutoffState::BackendObserved);
      QCOMPARE(f.proxy->inputCutoffState(*newer), InputCutoffState::Pending);
      QVERIFY(!f.stream->closed);
      f.proxy->resetOptions();
      f.take();
      f.receive({"CICA" + integer(*generation, 4)});
      QVERIFY(!f.stream->closed);
      QVERIFY(!f.startCutoff());
      const auto replacement = f.proxy->requestInputCutoffCapability();
      QVERIFY(replacement && *replacement > *generation);
      f.proxy->setOptions({kOptionInputCutoff, *replacement});
      f.take();
      f.receive({"CICA" + integer(*generation, 4)});
      QVERIFY(!f.startCutoff()); // Old ACK cannot acknowledge this new capability.
      f.receive({"CICA" + integer(*replacement, 4)});
      QVERIFY(f.startCutoff());
    }
  }
}

void ClientProxyTests::inputCutoff_resumesBeforeEveryLegacyWriter()
{
  if (!traceBuilt)
    QSKIP("Input cutoff is disabled in this build");
  const std::vector<std::function<void(ClientProxy &)>> writes{
      [](auto &p) { p.keyDown(kKey, kMask, kButton, kLang); },
      [](auto &p) { p.keyRepeat(kKey, kMask, 2, kButton, kLang); },
      [](auto &p) { p.keyUp(kKey, kMask, kButton); },
      [](auto &p) { p.mouseDown(1); },
      [](auto &p) { p.mouseUp(1); },
      [](auto &p) { p.mouseMove(10, 20); },
      [](auto &p) { p.mouseRelativeMove(1, 2); },
      [](auto &p) { p.mouseWheel(1, 2); },
      [](auto &p) { p.enter(1, 2, 3, 0, false); },
      [](auto &p) { p.leave(); },
      [](auto &p) { p.screensaver(true); },
      [](auto &p) { p.setOptions({kOptionHeartbeat, 2000}); },
      [](auto &p) { p.resetOptions(); }
  };
  for (std::size_t index = 0; index < writes.size(); ++index) {
    TraceFixture f;
    f.negotiateCutoff();
    const auto binding = f.startCutoff();
    QVERIFY(binding);
    f.take();
    f.cutoffReceipt(*binding, InputCutoffState::BackendObserved);
    writes[index](*f.proxy);
    const auto packets = f.take();
    QVERIFY(packets.size() >= 2);
    QCOMPARE(packets[0], QByteArray::fromStdString(encodeInputCutoffResume(*binding)));
    QVERIFY(!packets[1].startsWith("DICX"));
    // Reset retires the whole capability; ordinary writers wait for cleanup.
    QCOMPARE(
        f.proxy->inputCutoffState(*binding),
        index + 1 == writes.size() ? InputCutoffState::Denied : InputCutoffState::Resuming
    );
    f.cutoffReceipt(*binding, InputCutoffState::Resumed);
    QVERIFY(!f.stream->closed);
  }
}

void ClientProxyTests::inputCutoff_deadlineAndConnectionRevocation()
{
  if (!traceBuilt)
    QSKIP("Input cutoff is disabled in this build");
  {
    TraceFixture f;
    f.negotiateCutoff();
    const auto binding = f.startCutoff();
    QVERIFY(binding);
    f.take();
    f.proxy->m_cutoff->deadline = InputCutoffClock::now();
    f.cutoffReceipt(*binding, InputCutoffState::BackendObserved);
    QCOMPARE(f.proxy->inputCutoffState(*binding), InputCutoffState::Resuming);
    QCOMPARE(f.take().front(), QByteArray::fromStdString(encodeInputCutoffResume(*binding)));
    f.cutoffReceipt(*binding, InputCutoffState::Resumed);
    QCOMPARE(f.proxy->inputCutoffState(*binding), InputCutoffState::Resumed);
  }
  for (bool mapped : {false, true}) {
    TraceFixture f;
    f.negotiateCutoff(mapped);
    const auto binding = mapped ? f.startMappedCutoff() : f.startCutoff();
    QVERIFY(binding);
    f.take();
    if (mapped)
      f.receive({QByteArray::fromStdString(encodeInputCutoffMappedResult(mappedResult(*binding)))});
    else
      f.cutoffReceipt(*binding, InputCutoffState::BackendObserved);
    const auto old = f.proxy->inputCutoffObservation(*binding);
    QCOMPARE(bool(old), mapped);
    if (old)
      QVERIFY(old->live());
    f.proxy->m_cutoff->deadline = InputCutoffClock::now();
    QCOMPARE(f.proxy->inputCutoffState(*binding), InputCutoffState::Resuming);
    QVERIFY(!old || !old->live());
    QVERIFY(!f.proxy->inputCutoffObservation(*binding));
    const auto cleanupDeadline = f.proxy->m_cutoff->resumeDeadline;
    QVERIFY(cleanupDeadline > InputCutoffClock::now());
    QCOMPARE(f.take().front(), QByteArray::fromStdString(encodeInputCutoffResume(*binding)));
    QVERIFY(f.proxy->resumeInputCutoff(*binding));
    QCOMPARE(f.proxy->m_cutoff->resumeDeadline, cleanupDeadline);
    QVERIFY(f.take().empty());
    auto wrong = *binding;
    ++wrong.capture.part;
    QCOMPARE(f.proxy->inputCutoffState(wrong), InputCutoffState::Denied);
    QVERIFY(!f.proxy->resumeInputCutoff(wrong));
    QVERIFY(!f.startCutoff());
    f.proxy->m_cutoff->resumeDeadline = InputCutoffClock::now();
    QCOMPARE(f.proxy->inputCutoffState(*binding), InputCutoffState::Denied);
    QVERIFY(f.proxy->resumeInputCutoff(*binding));
    QCOMPARE(f.proxy->inputCutoffState(*binding), InputCutoffState::Denied);
    QVERIFY(f.take().empty()); // Duplicate resume never restarts the cleanup budget.
    f.cutoffReceipt(*binding, InputCutoffState::Resumed);
    QCOMPARE(f.proxy->inputCutoffState(*binding), InputCutoffState::Resumed);
    const auto newer = mapped ? f.startMappedCutoff() : f.startCutoff();
    QVERIFY(newer && newer->request > binding->request);
    QVERIFY(!old || !old->live());
    QVERIFY(!f.proxy->inputCutoffObservation(*binding));
  }
  for (bool replacement : {false, true}) {
    TraceFixture f;
    f.negotiateCutoff();
    const auto binding = f.startCutoff();
    QVERIFY(binding);
    f.take();
    f.cutoffReceipt(*binding, InputCutoffState::BackendObserved);
    if (replacement)
      f.stream->authenticate();
    else
      f.stream->identity->revoke();
    QCOMPARE(f.proxy->inputCutoffState(*binding), InputCutoffState::Denied);
    QVERIFY(!f.startCutoff());
    f.cutoffReceipt(*binding, InputCutoffState::Resumed);
    QVERIFY(f.stream->closed);
  }
}

void ClientProxyTests::inputCutoff_rejectsWrongAndMalformedReceipts()
{
  if (!traceBuilt)
    QSKIP("Input cutoff is disabled in this build");
  for (int change = 0; change < 6; ++change) {
    TraceFixture f;
    f.negotiateCutoff();
    const auto binding = f.startCutoff();
    QVERIFY(binding);
    auto wrong = *binding;
    if (change == 0)
      ++wrong.request;
    if (change == 1)
      ++wrong.capture.sequence;
    if (change == 2)
      ++wrong.forwarding.geometry;
    if (change == 3)
      ++wrong.viewerConnection;
    auto bytes = encodeInputCutoffReceipt({wrong, InputCutoffState::BackendObserved});
    if (change == 4)
      bytes.resize(4); // Must not borrow body from the following packet.
    if (change == 5)
      bytes.back() = static_cast<char>(InputCutoffState::Resuming); // Local status is never accepted from the peer.
    f.receive({QByteArray::fromStdString(bytes), "CNOP"});
    QVERIFY(f.stream->closed);
    QCOMPARE(f.proxy->inputCutoffState(*binding), InputCutoffState::Denied);
  }
}

void ClientProxyTests::inputCutoff_callbackMayDestroyDuringOptions()
{
  if (!traceBuilt)
    QSKIP("Input cutoff is disabled in this build");
  for (bool duringResume : {true, false}) {
    for (int operation = 0; operation < 3; ++operation) {
      TraceFixture f;
      f.negotiateCutoff();
      const auto binding = f.startCutoff();
      QVERIFY(binding);
      f.take();
      std::vector<std::string> writes;
      f.stream->onWrite = [&](std::string_view bytes) {
        if (!bytes.starts_with("DICX") && !bytes.starts_with("DSOP") && !bytes.starts_with("CROP"))
          return; // PacketStreamFilter writes its length separately.
        writes.emplace_back(bytes);
        if (bytes.starts_with("DICX") == duringResume)
          f.proxy.reset();
      };
      if (operation == 0)
        f.proxy->resetOptions();
      else if (operation == 1)
        f.proxy->setOptions({kOptionInputCutoff, 0, kOptionHeartbeat, 2000});
      else
        f.proxy->setOptions({kOptionHeartbeat, 2000});
      QVERIFY(!f.proxy);
      QCOMPARE(writes.size(), duringResume ? 1u : 2u);
      QCOMPARE(writes.front(), encodeInputCutoffResume(*binding));
      QCOMPARE(f.events.disconnections, 0u);
    }
  }
}

void ClientProxyTests::mappedCutoff_exactObservationAndRevocation()
{
  if (!traceBuilt)
    QSKIP("Mapped cutoff is disabled in this build");
  for (int revoke = 0; revoke < 6; ++revoke) {
    TraceFixture f;
    f.negotiateCutoff(true);
    const auto binding = f.startMappedCutoff();
    QVERIFY(binding);
    const auto packets = f.take();
    QCOMPARE(packets.size(), 1u);
    const auto request =
        decodeInputCutoffMappedRequest(std::string_view(packets[0].constData() + 4, packets[0].size() - 4));
    QVERIFY(request && request->point == (InputCutoffPoint{-10, 27}) && request->cutoff.binding == *binding);
    f.cutoffReceipt(*binding, InputCutoffState::BackendObserved);
    QVERIFY(!f.proxy->inputCutoffObservation(*binding)); // PONG alone does not authorize this mapped request.
    const auto result = mappedResult(*binding);
    f.receive({QByteArray::fromStdString(encodeInputCutoffMappedResult(result))});
    const auto observation = f.proxy->inputCutoffObservation(*binding);
    QVERIFY(observation && observation->live());
    QCOMPARE(observation->snapshot(), result);
    QCOMPARE(observation->binding().capture.part, 3u);
    bool revokedBeforeResumeWrite = false;
    f.stream->onWrite = [&](std::string_view bytes) {
      if (bytes.starts_with("DICX"))
        revokedBeforeResumeWrite = !observation->live();
    };
    if (revoke == 0)
      f.proxy->mouseUp(1);
    else if (revoke == 1)
      f.cutoffReceipt(*binding, InputCutoffState::Denied);
    else if (revoke == 2) {
      f.proxy->m_cutoff->deadline = InputCutoffClock::now();
      QVERIFY(!f.proxy->inputCutoffObservation(*binding));
    } else if (revoke == 3)
      f.proxy->resetOptions();
    else if (revoke == 4)
      f.stream->identity->revoke();
    else
      f.proxy.reset();
    QVERIFY(!observation->live());
    if (revoke < 4) {
      QVERIFY(revokedBeforeResumeWrite);
      const auto cleanup = f.take();
      QVERIFY(!cleanup.empty());
      QCOMPARE(cleanup.front(), QByteArray::fromStdString(encodeInputCutoffResume(*binding)));
      f.receive({QByteArray::fromStdString(encodeInputCutoffMappedResult(result))});
      QVERIFY(!f.proxy->inputCutoffObservation(*binding)); // Late completion never revives an observation.
      f.cutoffReceipt(*binding, InputCutoffState::Resumed);
      QVERIFY(!observation->live());
      if (revoke == 0) {
        QCOMPARE(cleanup.size(), 2u);
        QCOMPARE(cleanup.back(), "DMUP" + QByteArray::fromHex("01"));
        const auto next = f.startMappedCutoff();
        QVERIFY(next && next->request > binding->request);
      }
    }
  }
}

void ClientProxyTests::mappedCutoff_rejectsSubstitutionAndUnnegotiatedResult()
{
  if (!traceBuilt)
    QSKIP("Mapped cutoff is disabled in this build");
  {
    TraceFixture f;
    f.negotiateCutoff();
    QVERIFY(!f.startMappedCutoff());
    const auto binding = f.startCutoff();
    QVERIFY(binding);
    f.receive({QByteArray::fromStdString(encodeInputCutoffMappedResult(mappedResult(*binding)))});
    QVERIFY(f.stream->closed);
  }
  for (int changed = 0; changed < 5; ++changed) {
    TraceFixture f;
    f.negotiateCutoff(true);
    const auto binding = f.startMappedCutoff();
    QVERIFY(binding);
    auto result = mappedResult(*binding);
    if (changed == 0)
      ++result.point.x;
    if (changed == 1)
      ++result.binding.capture.part;
    if (changed == 2)
      ++result.binding.viewerConnection;
    if (changed == 3)
      ++result.binding.forwarding.generation;
    auto bytes = encodeInputCutoffMappedResult(result);
    if (changed == 4)
      bytes.resize(4);
    f.receive({QByteArray::fromStdString(bytes), "CNOP"});
    QVERIFY(f.stream->closed);
    QVERIFY(!f.proxy->inputCutoffObservation(*binding));
  }
}

void ClientProxyTests::sealedCutoff_refreshRetainsPrefixAndExactCleanup()
{
  if (!traceBuilt)
    QSKIP("Input cutoff is disabled in this build");
  TraceFixture f;
  const auto generation = f.negotiateCutoff(true, true);
  QVERIFY(!f.startMappedCutoff()); // IPC2 cannot silently acquire a durable seal.
  const auto origin = f.proxy->startSealedInputCutoff(
      {31, 401, 3}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17, {-10, 27},
      InputCutoffClock::now() + std::chrono::milliseconds(200)
  );
  QVERIFY(origin && origin->coreConnectionGeneration == generation);
  auto packets = f.take();
  QCOMPARE(packets.size(), 1u);
  QVERIFY(packets.front().startsWith("DICS"));
  f.cutoffReceipt(*origin, InputCutoffState::BackendObserved);
  QVERIFY(!f.proxy->inputCutoffSeal(*origin));
  f.receive({QByteArray::fromStdString(encodeInputCutoffSealedResult({mappedResult(*origin), origin->request}))});
  auto observation = f.proxy->inputCutoffObservation(*origin);
  const auto seal = f.proxy->inputCutoffSeal(*origin);
  QVERIFY(observation && observation->seal() && seal && seal->live());
  QCOMPARE(seal->binding(), *origin);
  // An expired point cannot be reused, but the closed prefix is still a
  // historical fact. A later mapping receives its own absolute deadline.
  f.proxy->m_cutoff->deadline = InputCutoffClock::now();
  QCOMPARE(f.proxy->inputCutoffState(*origin), InputCutoffState::Sealed);
  QVERIFY(!f.proxy->inputCutoffObservation(*origin));
  QVERIFY(!observation->live());
  QVERIFY(seal->live());
  QVERIFY(f.take().empty()); // No resume or second ping between fresh maps.
  InputCutoffBinding latest = *origin;
  for (std::uint64_t index = 0; index != 32; ++index) {
    const OriginalInputStamp capture{31, 402 + index, 1};
    const InputCutoffPoint point{int(index * 7), int(index * 11)};
    const auto deadline = InputCutoffClock::now() + std::chrono::milliseconds(200);
    const auto next = f.proxy->refreshMappedInputCutoff(*seal, capture, point, deadline, index % 2 == 0);
    QVERIFY(next && continuesInputCutoffSeal(latest, *next));
    packets = f.take();
    QCOMPARE(packets.size(), 1u);
    QVERIFY(packets.front().startsWith("DICF"));
    const auto request =
        decodeInputCutoffMapRequest(std::string_view(packets.front().constData() + 4, packets.front().size() - 4));
    QVERIFY(
        request && request->mapping.cutoff.binding == *next && request->mapping.point == point &&
        request->sealRequest == origin->request && request->presentCursor == (index % 2 == 0)
    );
    QVERIFY(!f.proxy->refreshMappedInputCutoff(*seal, {31, 500 + index, 1}, point, deadline, false));
    QVERIFY(!f.proxy->inputCutoffObservation(latest));
    f.receive(
        {QByteArray::fromStdString(encodeInputCutoffSealedResult({mappedResult(*next, point), origin->request}))}
    );
    observation = f.proxy->inputCutoffObservation(*next);
    QVERIFY(observation && observation->live() && observation->seal() && observation->seal()->live());
    QVERIFY(!f.proxy->inputCutoffObservation(latest)); // Origin query cannot substitute a later point.
    QCOMPARE(observation->binding().capture, capture);
    QCOMPARE(observation->snapshot().point, point);
    QCOMPARE(observation->deadline(), deadline);
    QVERIFY(!f.proxy->refreshMappedInputCutoff(*seal, capture, point, deadline, false));
    latest = *next;
  }
  bool revokedBeforeLegacy = false;
  f.stream->onWrite = [&](std::string_view bytes) {
    if (bytes.starts_with("DICX"))
      revokedBeforeLegacy = !seal->live() && !observation->live();
  };
  f.proxy->mouseUp(1);
  QVERIFY(revokedBeforeLegacy);
  packets = f.take();
  QCOMPARE(packets.size(), 2u);
  QCOMPARE(packets.front(), QByteArray::fromStdString(encodeInputCutoffResume(*origin)));
  QVERIFY(packets.back().startsWith("DMUP"));
  QCOMPARE(f.proxy->inputCutoffState(*origin), InputCutoffState::Resuming);
  f.cutoffReceipt(*origin, InputCutoffState::Resumed);
  QCOMPARE(f.proxy->inputCutoffState(latest), InputCutoffState::Resumed);
  QVERIFY(!seal->live());
  QVERIFY(!f.proxy->refreshMappedInputCutoff(
      *seal, {31, 999, 1}, {1, 2}, InputCutoffClock::now() + std::chrono::milliseconds(200), true
  ));
}

void ClientProxyTests::sealedCutoff_rejectsSubstitutionAndRevokesLifetime()
{
  if (!traceBuilt)
    QSKIP("Input cutoff is disabled in this build");
  for (int invalid = 0; invalid != 10; ++invalid) {
    TraceFixture f;
    f.negotiateCutoff(true, true);
    const auto origin = f.proxy->startSealedInputCutoff(
        {31, 401, 1}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17, {-10, 27},
        InputCutoffClock::now() + std::chrono::milliseconds(200)
    );
    QVERIFY(origin);
    f.receive({QByteArray::fromStdString(encodeInputCutoffSealedResult({mappedResult(*origin), origin->request}))});
    const auto seal = f.proxy->inputCutoffSeal(*origin);
    QVERIFY(seal);
    const auto next = f.proxy->refreshMappedInputCutoff(
        *seal, {31, 402, 1}, {90, 27}, InputCutoffClock::now() + std::chrono::milliseconds(200), true
    );
    QVERIFY(next);
    f.take();
    auto result = InputCutoffSealedResult{mappedResult(*next, {90, 27}), origin->request};
    if (invalid == 0)
      ++result.mapping.point.x;
    if (invalid == 1)
      ++result.mapping.binding.capture.part;
    if (invalid == 2)
      ++result.mapping.binding.forwarding.geometry;
    if (invalid == 3)
      result.sealRequest = next->request;
    if (invalid < 4) {
      f.receive({QByteArray::fromStdString(encodeInputCutoffSealedResult(result))});
      QVERIFY(f.stream->closed);
    } else if (invalid == 4)
      f.proxy->resetOptions();
    else if (invalid == 5)
      f.stream->identity->revoke();
    else if (invalid == 6)
      f.proxy.reset();
    else if (invalid == 7) {
      f.proxy->m_cutoff->deadline = InputCutoffClock::now();
      f.receive({QByteArray::fromStdString(encodeInputCutoffSealedResult(result))});
    } else if (invalid == 8) {
      f.receive({QByteArray::fromStdString(encodeInputCutoffMappedResult(result.mapping))});
      QVERIFY(f.stream->closed); // A point-only result cannot renew a retained sealed transaction.
    } else {
      QVERIFY(f.proxy->resumeInputCutoff(*origin));
      f.cutoffReceipt(*next, InputCutoffState::Resumed);
      QVERIFY(f.stream->closed); // Cleanup ACK must identify the origin, never a later map.
    }
    QVERIFY(!seal->live());
  }
}

void ClientProxyTests::batchedCutoff_preservesEveryReceiptAndExactCleanup()
{
  if (!traceBuilt)
    QSKIP("Input cutoff is disabled in this build");
  TraceFixture f;
  auto notifications = std::make_shared<unsigned>(0);
  f.proxy->setInputMappingNotifier([notifications] { ++*notifications; });
  f.negotiateCutoff(true, true, true);
  const auto origin = f.proxy->startSealedInputCutoff(
      {31, 401, 3}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17, {-10, 27},
      InputCutoffClock::now() + std::chrono::milliseconds(250)
  );
  QVERIFY(origin);
  f.take();
  f.receive({QByteArray::fromStdString(encodeInputCutoffSealedResult({mappedResult(*origin), origin->request}))});
  const auto seal = f.proxy->inputCutoffSeal(*origin);
  QVERIFY(seal && seal->live());
  QCOMPARE(*notifications, 1u);
  std::vector<InputCutoffObservation> previous;
  std::vector<InputCutoffBinding> latest;
  for (unsigned group = 0; group < 4; ++group) {
    std::vector<InputCutoffMapItem> items;
    for (unsigned index = 0; index < inputCutoffMaximumBatch; ++index)
      items.push_back(
          {{31, 402 + group * 16 + index, static_cast<std::uint16_t>(1 + index % 3)},
           {int(index * 3), int(index * 7)},
           InputCutoffClock::now() + std::chrono::milliseconds(200 + index),
           index % 2 == 0}
      );
    const auto bindings = f.proxy->startMappedInputBatch(*seal, items);
    QVERIFY(bindings && bindings->size() == inputCutoffMaximumBatch);
    QVERIFY(std::all_of(previous.begin(), previous.end(), [](const auto &item) { return !item.live(); }));
    const auto packets = f.take();
    QCOMPARE(packets.size(), 1u);
    QVERIFY(packets.front().startsWith("DICB"));
    const auto wire =
        decodeInputCutoffBatchRequest(std::string_view(packets.front().constData() + 4, packets.front().size() - 4));
    QVERIFY(wire && wire->sealRequest == origin->request && wire->items.size() == items.size());
    InputCutoffBatchResult result{origin->request};
    for (std::size_t index = 0; index < items.size(); ++index) {
      QCOMPARE(wire->items[index].mapping.cutoff.binding, (*bindings)[index]);
      QCOMPARE(wire->items[index].mapping.point, items[index].point);
      QCOMPARE(wire->items[index].presentCursor, items[index].presentCursor);
      QVERIFY(!f.proxy->inputCutoffObservation((*bindings)[index]));
      result.items.push_back(mappedResult((*bindings)[index], items[index].point));
    }
    QVERIFY(!f.proxy->startMappedInputBatch(*seal, items)); // No unresolved replacement.
    f.cutoffReceipt(bindings->back(), InputCutoffState::BackendObserved);
    QVERIFY(!f.proxy->inputCutoffObservation(bindings->front())); // PONG is insufficient.
    QCOMPARE(*notifications, group + 1);                          // Only exact mapped receipts wake the consumer.
    const auto packet = QByteArray::fromStdString(encodeInputCutoffBatchResult(result));
    f.receive({packet, packet}); // Identical duplicate cannot replace any point.
    QCOMPARE(*notifications, group + 2);
    previous.clear();
    for (std::size_t index = 0; index < items.size(); ++index) {
      const auto observed = f.proxy->inputCutoffObservation((*bindings)[index]);
      QVERIFY(observed && observed->live() && observed->seal() && observed->seal()->live());
      QCOMPARE(observed->binding().capture, items[index].capture);
      QCOMPARE(observed->snapshot().point, items[index].point);
      QCOMPARE(observed->deadline(), items[index].deadline);
      previous.push_back(*observed);
    }
    // One lookup expiry never revokes still-live sibling records.
    f.proxy->m_cutoff->batch.front().item.deadline = InputCutoffClock::now();
    QVERIFY(!f.proxy->inputCutoffObservation(bindings->front()));
    const auto stillLive = f.proxy->inputCutoffObservation(bindings->back());
    QVERIFY(stillLive && stillLive->live());
    f.proxy->m_cutoff->deadline = InputCutoffClock::now();
    f.receive({packet});
    QCOMPARE(*notifications, group + 2);
    QVERIFY(f.proxy->inputCutoffObservation(bindings->back()).has_value());
    QVERIFY(previous.back().live());
    QVERIFY(seal->live());
    latest = *bindings;
  }
  QVERIFY(f.proxy->resumeInputCutoff(latest[7]));
  const auto packets = f.take();
  QCOMPARE(packets.size(), 1u);
  QCOMPARE(packets.front(), QByteArray::fromStdString(encodeInputCutoffResume(*origin)));
  QVERIFY(!seal->live());
  QVERIFY(std::none_of(previous.begin(), previous.end(), [](const auto &item) { return item.live(); }));
  QCOMPARE(f.proxy->inputCutoffState(latest.front()), InputCutoffState::Resuming);
  f.cutoffReceipt(*origin, InputCutoffState::Resumed);
  QCOMPARE(f.proxy->inputCutoffState(latest.front()), InputCutoffState::Resumed);
}

void ClientProxyTests::mappedCutoff_notificationMayRetireProxy()
{
  if (!traceBuilt)
    QSKIP("Input cutoff is disabled in this build");
  for (bool batch : {false, true}) {
    TraceFixture f;
    f.negotiateCutoff(true, true, true);
    const auto origin = f.proxy->startSealedInputCutoff(
        {31, 401, 3}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17, {-10, 27},
        InputCutoffClock::now() + std::chrono::milliseconds(250)
    );
    QVERIFY(origin);
    auto packet = QByteArray::fromStdString(encodeInputCutoffSealedResult({mappedResult(*origin), origin->request}));
    if (batch) {
      f.receive({packet});
      const auto seal = f.proxy->inputCutoffSeal(*origin);
      QVERIFY(seal && seal->live());
      const std::vector<InputCutoffMapItem> items{
          {{31, 402, 1}, {40, 50}, InputCutoffClock::now() + std::chrono::milliseconds(200), true}
      };
      const auto bindings = f.proxy->startMappedInputBatch(*seal, items);
      QVERIFY(bindings && bindings->size() == 1);
      packet = QByteArray::fromStdString(
          encodeInputCutoffBatchResult({origin->request, {mappedResult(bindings->front(), items.front().point)}})
      );
    }
    f.proxy->setInputMappingNotifier([&] { f.proxy.reset(); });
    f.receive({packet});
    QVERIFY(!f.proxy);
  }
}

void ClientProxyTests::batchedCutoff_rejectsPartialChangedAndLateResults()
{
  if (!traceBuilt)
    QSKIP("Input cutoff is disabled in this build");
  for (int invalid = 0; invalid < 8; ++invalid) {
    TraceFixture f;
    auto notifications = std::make_shared<unsigned>(0);
    f.proxy->setInputMappingNotifier([notifications] { ++*notifications; });
    f.negotiateCutoff(true, true, true);
    const auto origin = f.proxy->startSealedInputCutoff(
        {31, 401, 3}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17, {-10, 27},
        InputCutoffClock::now() + std::chrono::milliseconds(250)
    );
    QVERIFY(origin);
    f.take();
    f.receive({QByteArray::fromStdString(encodeInputCutoffSealedResult({mappedResult(*origin), origin->request}))});
    const auto seal = f.proxy->inputCutoffSeal(*origin);
    QVERIFY(seal);
    const std::vector<InputCutoffMapItem> items{
        {{31, 402, 1}, {40, 50}, InputCutoffClock::now() + std::chrono::milliseconds(200), true},
        {{31, 403, 2}, {60, 70}, InputCutoffClock::now() + std::chrono::milliseconds(210), false}
    };
    const auto bindings = f.proxy->startMappedInputBatch(*seal, items);
    QVERIFY(bindings);
    f.take();
    InputCutoffBatchResult result{
        origin->request, {mappedResult((*bindings)[0], items[0].point), mappedResult((*bindings)[1], items[1].point)}
    };
    if (invalid == 0)
      ++result.items[1].point.x;
    if (invalid == 1)
      result.items.pop_back();
    if (invalid == 2)
      ++result.items[1].binding.capture.part;
    if (invalid == 3)
      --result.sealRequest;
    if (invalid == 4)
      f.proxy->m_cutoff->deadline = InputCutoffClock::now();
    if (invalid == 5) {
      f.receive({QByteArray::fromStdString(encodeInputCutoffBatchResult(result))});
      ++result.items[1].sourceX; // A changed duplicate revokes the entire group.
    }
    if (invalid == 6)
      f.stream->identity->revoke();
    if (invalid == 7) {
      f.receive({QByteArray::fromStdString(encodeInputCutoffSealedResult({result.items.back(), origin->request}))});
    } else {
      f.receive({QByteArray::fromStdString(encodeInputCutoffBatchResult(result))});
    }
    QVERIFY(!f.proxy->inputCutoffObservation(bindings->front()));
    QVERIFY(!f.proxy->inputCutoffObservation(bindings->back()));
    QVERIFY(!seal->live());
    QCOMPARE(*notifications, invalid == 5 ? 2u : 1u);
  }
  TraceFixture old;
  old.negotiateCutoff(true, true);
  const auto origin = old.proxy->startSealedInputCutoff(
      {31, 401, 3}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17, {-10, 27},
      InputCutoffClock::now() + std::chrono::milliseconds(250)
  );
  QVERIFY(origin);
  old.receive({QByteArray::fromStdString(encodeInputCutoffSealedResult({mappedResult(*origin), origin->request}))});
  const auto seal = old.proxy->inputCutoffSeal(*origin);
  QVERIFY(seal);
  QVERIFY(!old.proxy->startMappedInputBatch(
      *seal, {{{31, 402, 1}, {1, 2}, InputCutoffClock::now() + std::chrono::milliseconds(200), false}}
  ));
  TraceFixture destroyed;
  destroyed.negotiateCutoff(true, true, true);
  const auto owned = destroyed.proxy->startSealedInputCutoff(
      {31, 401, 3}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17, {-10, 27},
      InputCutoffClock::now() + std::chrono::milliseconds(250)
  );
  QVERIFY(owned);
  destroyed.receive({QByteArray::fromStdString(encodeInputCutoffSealedResult({mappedResult(*owned), owned->request}))});
  const auto retained = destroyed.proxy->inputCutoffSeal(*owned);
  QVERIFY(retained);
  destroyed.stream->onWrite = [&](std::string_view bytes) {
    if (bytes.starts_with("DICB"))
      destroyed.proxy.reset();
  };
  QVERIFY(!destroyed.proxy->startMappedInputBatch(
      *retained, {{{31, 402, 1}, {1, 2}, InputCutoffClock::now() + std::chrono::milliseconds(200), false}}
  ));
  QVERIFY(!destroyed.proxy && !retained->live());
}

void ClientProxyTests::preparation_negotiatesPromotesAndWaitsForCleanup()
{
  if (!traceBuilt)
    QSKIP("Preparation is disabled in this build");
  TraceFixture f;
  const auto generation = f.negotiatePreparation();
  const auto binding = f.startPreparation();
  QVERIFY(binding);
  QCOMPARE(binding->coreConnectionGeneration, generation);
  const auto initial = f.take();
  QCOMPARE(initial.size(), 1u);
  const auto request = decodeInputPreparationRequest(initial[0].toStdString().substr(4));
  QVERIFY(request && request->binding == *binding && request->point == (InputCutoffPoint{-10, 27}));
  QVERIFY(request->remainingMs > 250 && request->remainingMs <= 450);
  QVERIFY(!f.proxy->preparedInputFocus(*binding));
  PreparedInputFocusResult result{*binding, {-10, 27}, 17, {11, 12}, 16, 14, 15};
  result.viewerPeerFingerprint.fill(0x7a);
  const auto packet = QByteArray::fromStdString(encodePreparedInputFocusResult(result));
  f.receive({packet, packet});
  const auto focus = f.proxy->preparedInputFocus(*binding);
  QVERIFY(focus && focus->live());
  const KeyboardRouteBinding all{{11, 12}, 13, 14, 15, 16, ForwardingScope::All};
  auto wrong = all;
  ++wrong.geometry;
  QVERIFY(!f.proxy->promoteInputPreparation(*focus, wrong, InputCutoffClock::now() + std::chrono::milliseconds(200)));
  QVERIFY(f.take().empty());
  const auto promoted =
      f.proxy->promoteInputPreparation(*focus, all, InputCutoffClock::now() + std::chrono::milliseconds(200));
  QVERIFY(promoted && matchesInputPreparationPromotion(result, *promoted));
  QCOMPARE(f.proxy->inputPreparationState(*binding), InputPreparationState::Promoting);
  QVERIFY(focus->live());
  const auto promotion = f.take();
  QCOMPARE(promotion.size(), 1u);
  QCOMPARE(decodeInputPreparationPromotion(promotion[0].toStdString().substr(4))->cutoff, *promoted);
  f.receive({QByteArray::fromStdString(encodeInputCutoffSealedResult({mappedResult(*promoted), promoted->request}))});
  QCOMPARE(f.proxy->inputPreparationState(*binding), InputPreparationState::Sealed);
  const auto observation = f.proxy->inputCutoffObservation(*promoted);
  QVERIFY(observation && observation->live() && focus->live());
  // An already-owned release waits behind the asynchronous linked drain.
  f.proxy->mouseUp(1);
  const auto cleanup = f.take();
  QCOMPARE(cleanup.size(), 1u);
  QCOMPARE(cleanup[0], QByteArray::fromStdString(encodeInputCutoffResume(*promoted)));
  QCOMPARE(f.proxy->inputCutoffState(*promoted), InputCutoffState::Resuming);
  QVERIFY(!focus->live() && !observation->live());
  QVERIFY(f.proxy->resumeInputCutoff(*promoted));
  QVERIFY(f.take().empty());
  f.cutoffReceipt(*promoted, InputCutoffState::Resumed);
  const auto release = f.take();
  QCOMPARE(release.size(), 1u);
  QVERIFY(release[0].startsWith("DMUP"));
  QCOMPARE(f.proxy->inputPreparationState(*binding), InputPreparationState::Released);
  f.cutoffReceipt(*promoted, InputCutoffState::Resumed);
  QVERIFY(f.take().empty());
  QVERIFY(!f.stream->closed);
  const auto newer = f.startPreparation();
  QVERIFY(newer && newer->request > promoted->request);
  f.take();
  f.receive({packet}); // Old prepared focus cannot replace the new operation.
  QVERIFY(!f.proxy->preparedInputFocus(*newer));
}

void ClientProxyTests::preparation_boundsCancellationAndPreservesAttemptIdentity()
{
  if (!traceBuilt)
    QSKIP("Preparation is disabled in this build");
  {
    TraceFixture old;
    old.negotiateCutoff(true, true, true);
    QVERIFY(!old.startPreparation());
    QVERIFY(old.take().empty());
  }
  for (int test = 0; test < 6; ++test) {
    TraceFixture f;
    f.negotiatePreparation();
    const auto binding = f.startPreparation();
    QVERIFY(binding);
    f.take();
    if (test == 0) {
      f.receive({QByteArray::fromStdString(encodeInputPreparationReceipt(
          {*binding, InputPreparationState::Released, InputPreparationOutcome::Unsupported}
      ))});
      QCOMPARE(f.proxy->inputPreparationState(*binding), InputPreparationState::Released);
      QVERIFY(!f.stream->closed);
      continue;
    }
    if (test == 1) {
      f.receive({QByteArray::fromStdString(
          encodeInputPreparationReceipt({*binding, InputPreparationState::Failed, InputPreparationOutcome::NoTarget})
      )});
      QCOMPARE(f.proxy->inputPreparationState(*binding), InputPreparationState::Canceling);
      QCOMPARE(f.take().size(), 1u);
    } else {
      f.proxy->keyUp(kKey, kMask, kButton);
      const auto packets = f.take();
      QCOMPARE(packets.size(), 1u);
      QVERIFY(packets[0].startsWith("DIPX"));
    }
    const auto originalDeadline = f.proxy->m_preparation->cleanupDeadline;
    QVERIFY(f.proxy->cancelInputPreparation(*binding, InputCutoffClock::now() + inputCutoffMaximumLifetime));
    QCOMPARE(f.proxy->m_preparation->cleanupDeadline, originalDeadline);
    QVERIFY(f.take().empty());
    if (test == 2) { // Expiry is not a release ACK.
      f.proxy->m_preparation->cleanupDeadline = InputCutoffClock::now();
      QCOMPARE(f.proxy->inputPreparationState(*binding), InputPreparationState::Failed);
      QVERIFY(f.stream->closed);
      continue;
    }
    if (test == 3) {
      for (unsigned i = 0; i < 64; ++i)
        f.proxy->mouseUp(1);
      QVERIFY(f.stream->closed);
      continue;
    }
    if (test == 4) {
      f.proxy->keyDown(kKey, kMask, kButton, "en");
      QVERIFY(f.stream->closed);
      continue;
    }
    if (test == 5) {
      f.stream->identity->revoke();
      QCOMPARE(f.proxy->inputPreparationState(*binding), InputPreparationState::Failed);
      QVERIFY(f.take().empty());
      continue;
    }
    f.receive({QByteArray::fromStdString(
        encodeInputPreparationReceipt({*binding, InputPreparationState::Released, InputPreparationOutcome::NoTarget})
    )});
    QCOMPARE(f.proxy->inputPreparationState(*binding), InputPreparationState::Released);
    QCOMPARE(f.proxy->inputPreparationOutcome(*binding), InputPreparationOutcome::NoTarget);
    QVERIFY(f.take().empty());
  }
  {
    TraceFixture f;
    f.negotiatePreparation();
    const auto binding = f.startPreparation();
    QVERIFY(binding);
    f.take();
    bool queued = false;
    f.stream->onWrite = [&](std::string_view bytes) {
      if (bytes.starts_with("DIPX") && !std::exchange(queued, true))
        f.proxy->mouseUp(2);
    };
    f.proxy->keyUp(kKey, kMask, kButton);
    const auto cancellation = f.take();
    QCOMPARE(cancellation.size(), 1u);
    QVERIFY(cancellation[0].startsWith("DIPX"));
    f.receive({QByteArray::fromStdString(
        encodeInputPreparationReceipt({*binding, InputPreparationState::Released, InputPreparationOutcome::None})
    )});
    const auto ordered = f.take();
    QCOMPARE(ordered.size(), 2u);
    QVERIFY(ordered[0].startsWith("DKUP"));
    QVERIFY(ordered[1].startsWith("DMUP"));
    QVERIFY(!f.stream->closed);
  }
  for (const bool preparation : {false, true}) {
    TraceFixture f;
    if (preparation)
      f.negotiatePreparation();
    else
      f.negotiateCutoff(true, true, true);
    f.stream->onWrite = [&](std::string_view bytes) {
      if (bytes.starts_with(preparation ? "DIPP" : "DICS"))
        f.proxy.reset();
    };
    if (preparation) {
      const auto attempted = f.startPreparation();
      QVERIFY(attempted);
    } else {
      const auto attempted = f.proxy->startSealedInputCutoff(
          {31, 401, 3}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17, {-10, 27},
          InputCutoffClock::now() + std::chrono::milliseconds(200)
      );
      QVERIFY(attempted);
    }
    QVERIFY(!f.proxy);
  }
}

QTEST_MAIN(ClientProxyTests)
