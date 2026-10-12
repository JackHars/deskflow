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

QTEST_MAIN(ClientProxyTests)
