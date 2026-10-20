/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "OriginalInputCoordinatorTests.h"

#include "../deskflow/MockEventQueue.h"
#include "deskflow/AppUtil.h"
#include "deskflow/InputPreparationProtocol.h"
#include "deskflow/PacketStreamFilter.h"
#include "io/IStream.h"
#include "server/ClientProxy1_8.h"
#include "windowshare/OriginalInputBatchBudget.h"
#include "windowshare/OriginalInputCoordinator.h"
#if defined(__APPLE__)
#include "platform/OriginalInputCaptureSource_mac.h"
#endif

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace deskflow::windowshare {
struct OriginalInputCoordinatorTestAccess
{
  static void stopPolling(OriginalInputCoordinator &coordinator)
  {
    coordinator.m_timer.stop();
  }
};
class SelectedPeerTestAccess
{
public:
  static SelectedPeer create(AuthenticatedPeerTicket ticket, std::shared_ptr<const std::atomic_bool> revoked)
  {
    return {101, 102, 103, std::move(ticket), std::move(revoked)};
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
using namespace std::chrono_literals;
namespace {
using Result = OriginalInputDispatcher::Result;
using State = OriginalInputDispatcher::State;
using Clock = OriginalInputDispatcher::Clock;

void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
bool until(const std::function<bool()> &condition, int timeoutMs = 3000)
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
void pumpFor(int milliseconds)
{
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}
LocalInputPin pin(std::uint8_t value)
{
  LocalInputPin result{};
  result.fill(value);
  return result;
}
LocalInputPeerPolicy policy()
{
  const std::array pairs{LocalInputPeerPair{pin(1), pin(2)}};
  const auto result = LocalInputPeerPolicy::create(pairs);
  require(result.has_value(), "Fixture peer policy rejected");
  return *result;
}
KeyboardRouteBinding hostBinding()
{
  return {{11, 12}, 17, 13, 14, 15, ForwardingScope::All};
}
LocalInputTarget hostTarget()
{
  return {27, pin(3)};
}
void report(QJsonObject object)
{
  QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
}
int hostChild(int descriptor, bool decline, bool lateTarget)
{
  const auto parent = localInputProcess(getppid());
  require(parent.has_value(), "Fixture parent identity unavailable");
  auto channel = LocalInputChannel::adoptHost(descriptor, *parent);
  require(bool(channel), "Fixture host adoption failed");
  AuthenticatedPeerIssuer issuer;
  const auto ticket = issuer.issue(pin(2));
  require(ticket.has_value(), "Fixture host TLS ticket unavailable");
  bool closed = false, quiesced = false, active = false;
  std::optional<LocalInputLease> delayedTarget;
  std::uint64_t lastSequence = 0;
  QString failure;
  std::unique_ptr<LocalInputPreflight> preflight;
  channel->onClosed = [&](QString reason) {
    closed = true;
    report({{"type", "closed"}, {"reason", reason}});
  };
  channel->onMappedOriginal = [&](const auto &lease, const auto &record, const auto &point) {
    if (!active || lease.scope != ForwardingScope::All || record.epoch != lease.captureEpoch ||
        record.sequence <= lastSequence || !validOriginalInputRecord(record) || !liveLocalInputMappedPoint(point)) {
      failure = "Mapped delivery escaped exact active owner, source order, or deadline";
      channel->close(failure);
      return;
    }
    lastSequence = record.sequence;
    report(
        {{"type", "mapped"},
         {"sequence", QString::number(record.sequence)},
         {"epoch", QString::number(record.epoch)},
         {"family", int(record.payload.index())},
         {"flags", QString::number(record.flags)},
         {"geometry", QString::number(lease.geometryRevision)},
         {"generation", QString::number(lease.keyboardRouteGeneration)},
         {"x", point.x},
         {"y", point.y},
         {"width", point.width},
         {"height", point.height}}
    );
  };
  channel->onOriginal = [&](const auto &, const auto &) {
    failure = "Coordinator downgraded mapped delivery to unmapped Original";
    channel->close(failure);
  };
  LocalInputPreflight::Host host;
  host.connection = [&]() -> std::optional<AuthenticatedPeerObservation> {
    return AuthenticatedPeerObservationTestAccess::create(*ticket);
  };
  host.reserve = [&](ForwardingScope scope) -> std::optional<KeyboardRouteBinding> {
    require(scope == ForwardingScope::All, "Coordinator requested less than full forwarding scope");
    report({{"type", "candidate"}});
    if (decline)
      return std::nullopt;
    return hostBinding();
  };
  host.startQuiescence = [&](const auto &binding) {
    require(binding == hostBinding(), "Host reservation binding changed");
    const auto lease = preflight->lease();
    require(lease.has_value(), "Host target has no pending lease");
    if (std::exchange(lateTarget, false)) {
      delayedTarget = lease;
      report({{"type", "waiting-target"}});
      return true;
    }
    require(channel->sendInputTarget(*lease, hostTarget()), "Host could not send its exact mapped target");
    quiesced = true;
    preflight->quiesced(binding);
    report({{"type", "quiesced"}});
    return true;
  };
  host.isQuiesced = [&](const auto &binding) { return binding == hostBinding() && quiesced && !active; };
  host.activate = [&](const auto &binding) {
    require(binding == hostBinding() && quiesced && !active, "Host activation changed its quiesced owner");
    active = true;
    report({{"type", "active"}});
    return true;
  };
  host.isActive = [&](const auto &binding) { return binding == hostBinding() && active; };
  LocalInputPreflight::Checks checks;
  // Fake Host policy only. It generates no OS input and cannot mint the source
  // transition or Core mapped observation exercised by the parent process.
  checks.codeAdmission = checks.deliveryReady = checks.acquisitionBoundary = [](const auto &) { return true; };
  checks.release = [&](const auto &lease, auto) {
    require(
        lease.scope == ForwardingScope::All && lease.keyboardRouteGeneration == hostBinding().generation,
        "Host cleanup changed scope or owner"
    );
    active = quiesced = false;
    if (delayedTarget && channel->authenticated()) {
      // Model a valid reply already in flight when Core canceled its offer.
      // Sending it before Released makes the crossed-message order deterministic.
      require(channel->sendInputTarget(*delayedTarget, hostTarget()), "Late target was not sent");
      delayedTarget.reset();
      report({{"type", "late-target"}});
    }
    if (channel->authenticated() && !channel->sendReleased(leaseOffer(lease))) {
      failure = "Host could not acknowledge its exact completed owner release";
      return false;
    }
    report({{"type", "release"}});
    return true;
  };
  preflight = LocalInputPreflight::host(*channel, policy(), std::move(host), std::move(checks));
  const auto onPrepare = channel->onPrepare;
  channel->onPrepare = [&, onPrepare](const auto &lease) {
    report({{"type", "prepare"}});
    onPrepare(lease);
  };
  require(until([&] { return closed; }, 12000), "Fixture Host did not close");
  preflight.reset();
  require(!active && !quiesced, "Fixture Host retained an owner after channel teardown");
  if (!failure.isEmpty())
    throw std::runtime_error(failure.toStdString());
  return 0;
}

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
class WireEvents : public MockEventQueue
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
  std::map<std::pair<EventTypes, void *>, EventHandler> handlers;
};
class WireStream : public IStream
{
public:
  WireStream()
  {
    require(issuer.issue(pin(1)).has_value(), "Core TLS issuer failed");
  }
  void write(const void *buffer, std::uint32_t count) override
  {
    output.append(static_cast<const char *>(buffer), count);
  }
  void close() override
  {
    closed = true;
    issuer.revoke();
  }
  std::uint32_t read(void *buffer, std::uint32_t count) override
  {
    const auto size = std::min<std::uint32_t>(count, input.size());
    if (buffer)
      std::memcpy(buffer, input.constData(), size);
    input.remove(0, size);
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
    return const_cast<WireStream *>(this);
  }
  bool isReady() const override
  {
    return !input.isEmpty();
  }
  std::uint32_t getSize() const override
  {
    return input.size();
  }
  std::optional<AuthenticatedPeerTicket> authenticatedPeerTicket() const override
  {
    return issuer.ticket();
  }
  std::optional<PeerFingerprint> authenticatedPeerFingerprint() const override
  {
    return pin(1);
  }
  AuthenticatedPeerIssuer issuer;
  QByteArray input, output;
  bool closed = false;
};
QByteArray number(std::uint64_t value, unsigned bytes)
{
  std::string encoded;
  inputSequenceDetail::append(encoded, value, bytes);
  return QByteArray::fromStdString(encoded);
}
struct CoreWire
{
  WireEvents events;
  WireStream *stream = new WireStream;
  std::unique_ptr<ClientProxy1_8> proxy;
  std::vector<InputCutoffMappedRequest> requests;
  std::vector<InputCutoffMapRequest> maps;
  std::vector<InputCutoffBatchRequest> batches;
  std::vector<InputCutoffBinding> resumes;
  std::vector<InputPreparationRequest> preparations;
  std::vector<InputPreparationPromotion> promotions;
  std::vector<InputPreparationCancellation> cancellations;
  CoreWire()
  {
    proxy = std::make_unique<ClientProxy1_8>(
        "coordinator-fixture", new PacketStreamFilter(&events, stream), reinterpret_cast<Server *>(0x1), &events
    );
    receive("DINF" + QByteArray::fromHex("0000 0000 0400 0300 0000 0200 0180"));
    const auto generation = proxy->requestBatchedInputCutoffCapability();
    require(generation.has_value(), "Mapped capability unavailable");
    proxy->setOptions({kOptionBatchedInputCutoff, *generation});
    receive("CICB" + number(*generation, 4));
    take();
  }
  void enablePreparation(bool acknowledge = true)
  {
    const auto generation = proxy->requestInputPreparationCapability();
    require(generation.has_value(), "Focus preparation capability unavailable");
    proxy->setOptions({kOptionBatchedInputCutoff, *generation, kOptionInputPreparation, *generation});
    receive("CICB" + number(*generation, 4));
    if (acknowledge)
      receive("CIPA" + number(*generation, 4));
    take();
  }
  void receive(const QByteArray &packet)
  {
    stream->input += number(packet.size(), 4) + packet;
    require(
        events.dispatchEvent(Event(EventTypes::StreamInputReady, stream->getEventTarget())),
        "Real ClientProxy decoder did not receive packet"
    );
  }
  void take()
  {
    const auto wire = std::exchange(stream->output, {});
    const std::string_view bytes(wire.constData(), wire.size());
    std::size_t offset = 0;
    while (offset < bytes.size()) {
      require(bytes.size() - offset >= 4, "Core wrote incomplete packet length");
      const auto length = inputSequenceDetail::read(bytes, offset, 4);
      require(length <= bytes.size() - offset, "Core wrote incomplete packet");
      const auto packet = bytes.substr(offset, length);
      offset += length;
      if (packet.starts_with("DIPP")) {
        const auto request = decodeInputPreparationRequest(packet.substr(4));
        require(request.has_value(), "Core wrote malformed focus preparation");
        preparations.push_back(*request);
      } else if (packet.starts_with("DIPM")) {
        const auto request = decodeInputPreparationPromotion(packet.substr(4));
        require(request.has_value(), "Core wrote malformed focus promotion");
        require(!preparations.empty(), "Focus promotion has no retained preparation");
        promotions.push_back(*request);
        requests.push_back({{request->cutoff, request->remainingMs}, preparations.back().point});
      } else if (packet.starts_with("DIPX")) {
        const auto request = decodeInputPreparationCancellation(packet.substr(4));
        require(request.has_value(), "Core wrote malformed focus cancellation");
        cancellations.push_back(*request);
      } else if (packet.starts_with("DICS")) {
        const auto request = decodeInputCutoffMappedRequest(packet.substr(4));
        require(request.has_value(), "Core wrote malformed mapped request");
        requests.push_back(*request);
      } else if (packet.starts_with("DICF")) {
        const auto request = decodeInputCutoffMapRequest(packet.substr(4));
        require(request.has_value(), "Core wrote malformed sealed map request");
        maps.push_back(*request);
        requests.push_back(request->mapping);
      } else if (packet.starts_with("DICB")) {
        const auto request = decodeInputCutoffBatchRequest(packet.substr(4));
        require(request.has_value(), "Core wrote malformed map batch");
        batches.push_back(*request);
        for (const auto &item : request->items) {
          maps.push_back(item);
          requests.push_back(item.mapping);
        }
      } else if (packet.starts_with("DICX")) {
        const auto binding = decodeInputCutoffResume(packet.substr(4));
        require(binding.has_value(), "Core wrote malformed resume");
        resumes.push_back(*binding);
      }
    }
  }
  void receipt(InputCutoffBinding binding, InputCutoffState state)
  {
    receive(QByteArray::fromStdString(encodeInputCutoffReceipt({binding, state})));
    take();
  }
  PreparedInputFocusResult preparedResult() const
  {
    require(!preparations.empty(), "No preparation to focus");
    const auto &request = preparations.back();
    const auto host = hostBinding();
    const auto target = hostTarget();
    return {request.binding, request.point,     target.viewerConnection, host.owner,
            host.geometry,   host.focusRequest, host.focusRevision,      target.viewerPeerFingerprint};
  }
  void prepared(PreparedInputFocusResult result)
  {
    receive(QByteArray::fromStdString(encodePreparedInputFocusResult(result)));
    take();
  }
  void preparationReceipt(
      InputPreparationBinding binding, InputPreparationState state,
      InputPreparationOutcome outcome = InputPreparationOutcome::None
  )
  {
    receive(QByteArray::fromStdString(encodeInputPreparationReceipt({binding, state, outcome})));
    take();
  }
  InputCutoffMappedResult result() const
  {
    require(!requests.empty(), "No exact Core mapped request to complete");
    return result(requests.back());
  }
  InputCutoffMappedResult result(const InputCutoffMappedRequest &request) const
  {
    InputCutoffMappedResult value;
    value.binding = request.cutoff.binding;
    value.point = request.point;
    value.sourceX = 10.25;
    value.sourceY = 20.5;
    value.sourceWidth = 1920;
    value.sourceHeight = 1080;
    value.surfaceIncarnation = 31;
    value.viewportRevision = 32;
    value.nativeSurface = 33;
    value.providerGeneration = 34;
    value.nativeRevision = 35;
    value.viewerPeerFingerprint = hostTarget().viewerPeerFingerprint;
    value.orderingContract = "coordinator-test-reviewed-build:1";
    return value;
  }
  void complete(InputCutoffMappedResult result)
  {
    const auto seal = requests.front().cutoff.binding.request;
    if (!batches.empty() && batches.back().items.front().mapping.cutoff.binding == result.binding) {
      require(batches.back().items.size() == 1, "Single-result helper cannot complete a larger batch");
      receive(QByteArray::fromStdString(encodeInputCutoffBatchResult({seal, {result}})));
    } else {
      receive(QByteArray::fromStdString(encodeInputCutoffSealedResult({result, seal})));
    }
  }
  void completeBatch()
  {
    require(!batches.empty(), "No actual Core batch to complete");
    InputCutoffBatchResult reply{batches.back().sealRequest, {}};
    for (const auto &item : batches.back().items)
      reply.items.push_back(result(item.mapping));
    receive(QByteArray::fromStdString(encodeInputCutoffBatchResult(reply)));
  }
};

#if defined(__APPLE__)
class Capture
{
public:
  Capture()
  {
    source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    event = source ? CGEventCreate(source) : nullptr;
    require(event && capture.start(), "Constructed Mac capture fixture failed");
    CGEventSetIntegerValueField(event, kCGKeyboardEventKeyboardType, 41);
    CGEventSetFlags(event, 0);
  }
  ~Capture()
  {
    if (event)
      CFRelease(event);
    if (source)
      CFRelease(source);
  }
  OriginalInputCaptured key(std::uint16_t code, OriginalKeyTransition transition)
  {
    resetEvent(transition == OriginalKeyTransition::Up ? kCGEventKeyUp : kCGEventKeyDown);
    CGEventSetIntegerValueField(event, kCGKeyboardEventKeyboardType, 41);
    CGEventSetIntegerValueField(event, kCGKeyboardEventKeycode, code);
    CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, transition == OriginalKeyTransition::Repeat);
    return finishCapture();
  }
  OriginalInputCaptured button(bool down)
  {
    resetEvent(down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp);
    CGEventSetIntegerValueField(event, kCGMouseEventSubtype, kCGEventMouseSubtypeDefault);
    CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, 0);
    CGEventSetIntegerValueField(event, kCGMouseEventClickState, 1);
    return finishCapture();
  }
  OriginalInputCaptured motion(bool drag = false)
  {
    resetEvent(drag ? kCGEventLeftMouseDragged : kCGEventMouseMoved);
    CGEventSetIntegerValueField(event, kCGMouseEventSubtype, kCGEventMouseSubtypeDefault);
    CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, 0);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, 2);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, -1);
    return finishCapture();
  }
  OriginalInputCaptured scroll()
  {
    resetEvent(kCGEventScrollWheel);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventIsContinuous, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventScrollPhase, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventMomentumPhase, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1, -1);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2, 1);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventDeltaAxis3, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1, -3);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2, 3);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis3, 0);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis1, -0x10000);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis2, 0x10000);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis3, 0);
    return finishCapture();
  }
  OriginalInputCaptured finishCapture()
  {
    CGEventSetTimestamp(event, ++timestamp);
    const auto stamp = capture.beginCapture(CGEventGetType(event), event);
    if (!stamp)
      throw std::runtime_error(
          "Constructed event lost source continuity: type=" + std::to_string(CGEventGetType(event)) +
          " timestamp=" + std::to_string(timestamp)
      );
    capture.completeCapture({stamp, 1, false});
    auto value = capture.popCaptured();
    require(value.has_value(), "Constructed capture not completed");
    return *value;
  }
  OriginalInputCaptured fresh(std::uint16_t code = 0)
  {
    key(code, OriginalKeyTransition::Up);
    auto value = key(code, OriginalKeyTransition::Down);
    require(value.transition.permitsAcquisition(value.input), "Actual source did not qualify release-then-down");
    return value;
  }
  OriginalInputCaptured freshButton()
  {
    button(false);
    auto value = button(true);
    require(value.transition.permitsAcquisition(value.input), "Actual source did not qualify fresh button down");
    return value;
  }
  OriginalInputCaptureSource capture;

private:
  void resetEvent(CGEventType type)
  {
    CFRelease(event);
    event = CGEventCreate(source);
    require(event != nullptr, "Constructed Mac event allocation failed");
    CGEventSetType(event, type);
    CGEventSetFlags(event, 0);
  }
  CGEventSourceRef source = nullptr;
  CGEventRef event = nullptr;
  std::uint64_t timestamp = 100;
};
#endif

struct Fixture
{
  CoreWire wire;
  QProcess child;
  std::unique_ptr<LocalInputChannel> channel;
  std::unique_ptr<OriginalInputCoordinator> coordinator;
  std::shared_ptr<OriginalInputDispatcher> dispatcher;
  std::shared_ptr<SelectedPeerMailbox> mailbox = std::make_shared<SelectedPeerMailbox>();
  std::shared_ptr<std::atomic_bool> selectionRevoked = std::make_shared<std::atomic_bool>(false);
  SelectedPeer selected;
  QTimer publisher, maintenance;
  QByteArray childBytes;
  std::vector<QJsonObject> hostEvents;
  bool closed = false, admitted = true;
  QString closeReason;
  int quiescences = 0;
  std::function<void()> afterQuiesce, afterStart, afterObserve, priorRevoked, afterPreparation, afterPromotion;
  std::function<void(OriginalInputStamp &, KeyboardRouteBinding &, InputCutoffPoint &)> changeStart;
  std::optional<InputCutoffObservation> lastObservation;

  explicit Fixture(
      bool decline = false, bool firstClick = false, bool acknowledgePreparation = true, bool lateTarget = false
  )
  {
    if (firstClick)
      wire.enablePreparation(acknowledgePreparation);
    auto pair = LocalInputSocketPair::create();
    pair.configureChild(child);
    child.start(
        QCoreApplication::applicationFilePath(), {"--coordinator-host", QString::number(pair.childDescriptor()),
                                                  lateTarget ? "late-target"
                                                  : decline  ? "decline"
                                                             : "accept"}
    );
    require(child.waitForStarted(2000), "Coordinator Host child failed to start");
    const auto process = localInputProcess(child.processId());
    require(process.has_value(), "Coordinator Host process identity unavailable");
    channel = pair.adoptCore(*process);
    channel->onClosed = [&](QString reason) {
      closed = true;
      closeReason = reason;
    };
    channel->onRevoked = [&](const auto &, auto) {
      const auto callback = priorRevoked;
      if (callback)
        callback();
    };
    require(
        until([&] {
          readHost();
          return channel->authenticated() || closed;
        }) &&
            !closed,
        "Coordinator private handshake failed"
    );
    selected = SelectedPeerTestAccess::create(*wire.stream->authenticatedPeerTicket(), selectionRevoked);
    require(mailbox->replace(selected), "Coordinator selection was not published");
    publisher.setInterval(20);
    QObject::connect(&publisher, &QTimer::timeout, &publisher, [&] { mailbox->replace(selected); });
    publisher.start();
    coordinator = std::make_unique<OriginalInputCoordinator>(
        *channel, mailbox, policy(),
        OriginalInputCoordinator::Providers{
            [&] { return admitted; },
            [](const auto &captured) { return captured.transition.permitsAcquisition(captured.input); }
        }
    );
    OriginalInputBackend backend;
    if (firstClick) {
      backend.preparationCapability = [&](const auto &) -> std::optional<std::uint32_t> {
        const auto value = wire.proxy->requestInputPreparationCapability();
        return wire.proxy->inputPreparationCapabilityReady() ? value : std::nullopt;
      };
      backend.prepare = [&](const auto &, auto stamp, auto point, auto deadline) {
        const auto value = wire.proxy->startInputPreparation(stamp, point, deadline);
        wire.take();
        const auto callback = afterPreparation;
        if (callback)
          callback();
        return value;
      };
      backend.prepared = [&](const auto &, auto binding) { return wire.proxy->preparedInputFocus(binding); };
      backend.preparationState = [&](const auto &, auto binding) { return wire.proxy->inputPreparationState(binding); };
      backend.preparationOutcome = [&](const auto &, auto binding) {
        return wire.proxy->inputPreparationOutcome(binding);
      };
      backend.promote = [&](const auto &, const auto &prepared, const auto &binding, auto deadline) {
        const auto value = wire.proxy->promoteInputPreparation(prepared, binding, deadline);
        wire.take();
        const auto callback = afterPromotion;
        if (callback)
          callback();
        return value;
      };
      backend.cancelPreparation = [&](const auto &, auto binding, auto deadline) {
        const bool value = wire.proxy->cancelInputPreparation(binding, deadline);
        wire.take();
        return value;
      };
    }
    backend.quiesce = [&](const auto &peer, const auto &route, bool releaseOwners) {
      require(peer.sameSelection(selected) && route.recipient == selected.recipient(), "Backend changed selection");
      require(releaseOwners == (quiescences == 0), "Backend lost first-owner release requirement");
      ++quiescences;
      const auto callback = afterQuiesce;
      if (callback)
        callback();
      return true;
    };
    backend.start = [&](const auto &, OriginalInputStamp stamp, KeyboardRouteBinding forwarding, std::uint64_t viewer,
                        InputCutoffPoint point, Clock::time_point deadline) {
      if (changeStart)
        changeStart(stamp, forwarding, point);
      auto value = wire.proxy->startSealedInputCutoff(stamp, forwarding, viewer, point, deadline);
      wire.take();
      const auto callback = afterStart;
      if (callback)
        callback();
      return value;
    };
    backend.refresh = [&](const auto &, const auto &seal, auto stamp, auto point, auto deadline, bool cursor) {
      auto value = wire.proxy->refreshMappedInputCutoff(seal, stamp, point, deadline, cursor);
      wire.take();
      return value;
    };
    backend.batch = [&](const auto &, const auto &seal, const auto &items) {
      auto value = wire.proxy->startMappedInputBatch(seal, items);
      wire.take();
      return value;
    };
    backend.observe = [&](const auto &, auto binding) {
      auto value = wire.proxy->inputCutoffObservation(binding);
      if (value)
        lastObservation = value;
      const auto callback = afterObserve;
      if (callback)
        callback();
      return value;
    };
    backend.state = [&](const auto &, auto binding) {
      const auto value = wire.proxy->inputCutoffState(binding);
      wire.take();
      return value;
    };
    backend.resume = [&](const auto &, auto binding) {
      const bool resumed = wire.proxy->resumeInputCutoff(binding);
      wire.take();
      return resumed;
    };
    dispatcher = coordinator->createDispatcher(std::move(backend));
    require(bool(dispatcher), "Coordinator dispatcher factory failed");
    // Models the Server event thread's independent 5 ms idle maintenance.
    maintenance.setInterval(5);
    QObject::connect(&maintenance, &QTimer::timeout, &maintenance, [&] {
      dispatcher->maintain();
      wire.take();
    });
    maintenance.start();
    // Let the public timer publish readiness, without accessing private state.
    pumpFor(8);
  }
  ~Fixture()
  {
    priorRevoked = {};
    maintenance.stop();
    publisher.stop();
    coordinator.reset();
    dispatcher.reset();
    if (channel) {
      channel->onClosed = {};
      channel->close();
    }
    if (child.state() != QProcess::NotRunning) {
      child.kill();
      child.waitForFinished(2000);
    }
  }
  void readHost()
  {
    childBytes += child.readAllStandardOutput();
    for (;;) {
      const auto end = childBytes.indexOf('\n');
      if (end < 0)
        break;
      const auto line = childBytes.left(end);
      childBytes.remove(0, end + 1);
      const auto value = QJsonDocument::fromJson(line);
      require(value.isObject(), "Fixture Host emitted invalid report");
      hostEvents.push_back(value.object());
    }
  }
  std::size_t count(const QString &type)
  {
    readHost();
    return std::count_if(hostEvents.begin(), hostEvents.end(), [&](const auto &event) {
      return event["type"] == type;
    });
  }
  OriginalInputRouteDecision route(const OriginalInputCaptured &captured) const
  {
    OriginalInputRouteDecision value;
    value.stamp = {captured.input.record.epoch, captured.input.record.sequence, 1};
    value.kind =
        std::holds_alternative<OriginalInputMotion>(captured.input.record.payload)   ? OriginalInputRouteKind::Motion
        : std::holds_alternative<OriginalInputButton>(captured.input.record.payload) ? OriginalInputRouteKind::Button
        : std::holds_alternative<OriginalInputScroll>(captured.input.record.payload) ? OriginalInputRouteKind::Scroll
                                                                                     : OriginalInputRouteKind::Key;
    value.server = selected.serverIncarnation();
    value.recipient = selected.recipient();
    value.ticket = selected.ticket();
    value.x = 147;
    value.y = 219;
    value.pointKnown = true;
    return value;
  }
  Result reserve(
      const OriginalInputCaptured &captured, std::uint64_t owner = 0, Clock::time_point deadline = Clock::now() + 1s
  )
  {
    return dispatcher->reserve(captured, route(captured), selected, owner, deadline);
  }
  Result poll(std::uint64_t ticket)
  {
    auto value = dispatcher->poll(ticket);
    dispatcher->maintain();
    wire.take();
    readHost();
    return value;
  }
  void started(std::uint64_t ticket, std::size_t count = 1)
  {
    require(
        until([&] {
          const auto value = poll(ticket);
          require(value.state == State::Pending, "Preparation terminated before its mapped request");
          return wire.requests.size() == count;
        }),
        "Coordinator did not reach actual mapped Core request"
    );
  }
  void focusStarted(std::uint64_t ticket)
  {
    require(
        until([&] {
          const auto value = poll(ticket);
          require(value.state == State::Pending, "Focus preparation terminated before its exact request");
          return wire.preparations.size() == 1;
        }),
        "Coordinator did not start first-click focus preparation"
    );
  }
  Result accepted(std::uint64_t ticket)
  {
    Result result = Result::pending(ticket);
    require(
        until([&] {
          // until() confirms its predicate after the event loop exits. A
          // terminal Accepted consumes the ticket and must only be polled once.
          if (result.state == State::Pending)
            result = poll(ticket);
          return result.state != State::Pending;
        }),
        "Coordinator did not finish preparation"
    );
    if (result.state != State::Accepted || !result.reservation) {
      readHost();
      QTextStream(stderr) << "Preparation state=" << int(result.state) << " channelClosed=" << closed
                          << " observation=" << bool(lastObservation)
                          << " observationLive=" << (lastObservation && lastObservation->live()) << Qt::endl;
      for (const auto &event : hostEvents)
        QTextStream(stderr) << QJsonDocument(event).toJson(QJsonDocument::Compact) << Qt::endl;
      throw std::runtime_error("Mapped preparation was not accepted");
    }
    return result;
  }
  void finish()
  {
    priorRevoked = {};
    if (coordinator)
      coordinator->stop();
    dispatcher->abort();
    channel->close();
    require(
        until([&] {
          readHost();
          return child.state() == QProcess::NotRunning;
        }),
        "Coordinator Host did not exit"
    );
    if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0)
      throw std::runtime_error("Coordinator Host failed: " + child.readAllStandardError().toStdString());
  }
};
} // namespace

void OriginalInputCoordinatorTests::initTestCase()
{
  static TestAppUtil appUtil;
}
void OriginalInputCoordinatorTests::unknownAcquisitionDoesNotStartHandoff()
{
  Fixture f;
  OriginalInputStream capture;
  const OriginalInputRecord record{capture.epoch().id(), 1, 100, 0, OriginalInputKey{41, 0}};
  QVERIFY(capture.push(record));
  const auto raw = capture.pop();
  QVERIFY(raw);
  const OriginalInputCaptured unknown{*raw, {{record.epoch, record.sequence, 0}, 1, false}};
  QCOMPARE(f.reserve(unknown).state, State::Refused);
  pumpFor(15);
  QCOMPARE(f.count("candidate"), 0u);
  QVERIFY(f.wire.requests.empty());
  f.finish();
}

void OriginalInputCoordinatorTests::emptyHostDeclinePreservesOrdinaryInput()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  Fixture f(true);
  Capture source;
  const auto pending = f.reserve(source.fresh());
  QCOMPARE(pending.state, State::Pending);
  Result result = Result::pending(pending.ticket);
  QVERIFY(until([&] {
    if (result.state == State::Pending)
      result = f.poll(pending.ticket);
    return result.state != State::Pending;
  }));
  QCOMPARE(result.state, State::Refused);
  QCOMPARE(f.count("candidate"), 1u);
  QCOMPARE(f.count("quiesced"), 0u);
  QCOMPARE(f.count("mapped"), 0u);
  QVERIFY(f.wire.requests.empty());
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::lateTargetAfterCancellationPreservesNextHandoff()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  Fixture f(false, false, true, true);
  Capture source;
  const auto pending = f.reserve(source.fresh());
  QCOMPARE(pending.state, State::Pending);
  QVERIFY(until([&] { return f.count("waiting-target") == 1; }));
  f.dispatcher->cancelPreparation(pending.ticket);
  QVERIFY(until([&] { return f.count("release") == 1 || f.closed; }));
  QVERIFY2(!f.closed, qPrintable(f.closeReason));
  QCOMPARE(f.count("late-target"), 1u);
  QCOMPARE(f.count("prepare"), 0u);
  QCOMPARE(f.count("mapped"), 0u);
  QVERIFY(f.wire.requests.empty());

  QVERIFY(until([&] { return f.dispatcher->legacyAcquisitionReady(f.selected.recipient()); }));
  const auto next = f.reserve(source.fresh(1));
  QCOMPARE(next.state, State::Pending);
  f.started(next.ticket);
  f.wire.complete(f.wire.result());
  const auto ready = f.accepted(next.ticket);
  QVERIFY(f.dispatcher->commit(*ready.reservation));
  QVERIFY(until([&] { return f.count("mapped") == 1; }));
  f.dispatcher->release(ready.reservation->owner);
  QVERIFY(until([&] { return f.count("release") == 2; }));
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::firstClickWaitsForFocusAndExactHostBeforeDelivery()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  Fixture f(false, true);
  Capture source;
  const auto captured = source.freshButton();
  const auto pending = f.reserve(captured, 0, Clock::now() + 500ms);
  QCOMPARE(pending.state, State::Pending);
  f.focusStarted(pending.ticket);
  QCOMPARE(f.wire.preparations.front().binding.capture, f.route(captured).stamp);
  QCOMPARE(f.wire.preparations.front().point, (InputCutoffPoint{147, 219}));
  pumpFor(12);
  QCOMPARE(f.count("candidate"), 0u);
  QCOMPARE(f.count("mapped"), 0u);
  QVERIFY(f.wire.requests.empty());
  QVERIFY(!f.dispatcher->legacyAcquisitionReady(f.selected.recipient()));

  f.wire.prepared(f.wire.preparedResult());
  f.started(pending.ticket);
  QCOMPARE(f.wire.promotions.size(), 1u);
  QCOMPARE(f.wire.promotions.front().preparation, f.wire.preparations.front().binding);
  QCOMPARE(f.wire.promotions.front().cutoff.forwarding, hostBinding());
  QCOMPARE(f.wire.promotions.front().cutoff.viewerConnection, hostTarget().viewerConnection);
  QCOMPARE(f.count("quiesced"), 1u);
  QCOMPARE(f.count("mapped"), 0u);
  QCOMPARE(f.quiescences, 1); // Initial legacy release occurs once, before DIPP.

  f.wire.complete(f.wire.result());
  const auto ready = f.accepted(pending.ticket);
  QCOMPARE(f.count("mapped"), 0u);
  QVERIFY(f.dispatcher->commit(*ready.reservation));
  QVERIFY(until([&] { return f.count("mapped") == 1; }));
  const auto origin = f.wire.requests.front().cutoff.binding;
  f.dispatcher->release(ready.reservation->owner);
  QVERIFY(until([&] {
    f.dispatcher->maintain();
    return !f.wire.resumes.empty();
  }));
  QCOMPARE(f.wire.resumes, (std::vector<InputCutoffBinding>{origin}));
  QVERIFY(f.wire.cancellations.empty()); // Validated Sealed transferred cleanup to DICX.
  f.wire.receipt(origin, InputCutoffState::Resumed);
  QVERIFY(until([&] {
    f.dispatcher->maintain();
    return f.dispatcher->legacyAcquisitionReady(f.selected.recipient());
  }));
  QCOMPARE(f.count("mapped"), 1u);
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::committedInputWakesWithoutPollingTimer_data()
{
  QTest::addColumn<int>("invalidate");
  QTest::newRow("deliver") << 0;
  QTest::newRow("admission-lost") << 1;
  QTest::newRow("destroyed") << 2;
}
void OriginalInputCoordinatorTests::committedInputWakesWithoutPollingTimer()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  QFETCH(int, invalidate);
  Fixture f;
  Capture source;
  const auto pending = f.reserve(source.fresh());
  QCOMPARE(pending.state, State::Pending);
  f.started(pending.ticket);
  f.wire.complete(f.wire.result());
  const auto ready = f.accepted(pending.ticket);
  OriginalInputCoordinatorTestAccess::stopPolling(*f.coordinator);
  bool committed = false;
  std::thread producer([&] { committed = f.dispatcher->commit(*ready.reservation); });
  producer.join();
  QVERIFY(committed);
  QCOMPARE(f.count("mapped"), 0u);
  if (invalidate == 1)
    f.admitted = false;
  else if (invalidate == 2)
    f.coordinator.reset();
  if (invalidate) {
    QVERIFY(until([&] { return f.count("release") == 1; }));
    QCOMPARE(f.count("mapped"), 0u);
    QVERIFY(!f.dispatcher->live(ready.reservation->owner));
  } else {
    QVERIFY(until([&] { return f.count("mapped") == 1; }));
    QVERIFY(!f.closed);
  }
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::firstClickRejectsHostSubstitution_data()
{
  QTest::addColumn<QString>("field");
  for (const auto *field : {"session", "epoch", "geometry", "focus-request", "focus-revision", "viewer", "pin"})
    QTest::newRow(field) << QString::fromLatin1(field);
}
void OriginalInputCoordinatorTests::firstClickRejectsHostSubstitution()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  QFETCH(QString, field);
  Fixture f(false, true);
  Capture source;
  const auto pending = f.reserve(source.freshButton(), 0, Clock::now() + 500ms);
  f.focusStarted(pending.ticket);
  auto focus = f.wire.preparedResult();
  if (field == "session")
    ++focus.owner.session;
  else if (field == "epoch")
    ++focus.owner.epoch;
  else if (field == "geometry")
    ++focus.geometry;
  else if (field == "focus-request")
    ++focus.focusRequest;
  else if (field == "focus-revision")
    ++focus.focusRevision;
  else if (field == "viewer")
    ++focus.viewerConnection;
  else
    ++focus.viewerPeerFingerprint.front();
  f.wire.prepared(focus);
  Result result = Result::pending(pending.ticket);
  QVERIFY(until([&] {
    if (result.state == State::Pending)
      result = f.poll(pending.ticket);
    return result.state != State::Pending;
  }));
  QCOMPARE(result.state, State::Revoked);
  QVERIFY(f.wire.promotions.empty());
  QVERIFY(f.wire.requests.empty());
  QCOMPARE(f.count("mapped"), 0u);
  QVERIFY(until([&] {
    f.dispatcher->maintain();
    return f.wire.cancellations.size() == 1;
  }));
  QVERIFY(!f.dispatcher->legacyAcquisitionReady(f.selected.recipient()));
  f.wire.preparationReceipt(focus.binding, InputPreparationState::Released);
  QVERIFY(until([&] {
    f.dispatcher->maintain();
    return f.dispatcher->legacyAcquisitionReady(f.selected.recipient());
  }));
  QVERIFY(f.wire.resumes.empty());
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::firstClickCancellationRetainsOneCleanupOwner_data()
{
  QTest::addColumn<bool>("duringPromotion");
  QTest::newRow("preparation-write") << false;
  QTest::newRow("promotion-write") << true;
}
void OriginalInputCoordinatorTests::firstClickCancellationRetainsOneCleanupOwner()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  QFETCH(bool, duringPromotion);
  Fixture f(false, true);
  Capture source;
  const auto pending = f.reserve(source.freshButton(), 0, Clock::now() + 500ms);
  bool canceled = false;
  const auto cancel = [&] {
    canceled = true;
    f.dispatcher->cancelPreparation(pending.ticket);
    // Run actual Qt cleanup before the backend acquisition callback returns.
    // It must not discard this recipient while its binding is still in flight.
    pumpFor(25);
    require(
        !f.dispatcher->legacyAcquisitionReady(f.selected.recipient()),
        "Reentrant Qt cleanup forgot an in-flight native acquisition"
    );
  };
  if (duringPromotion) {
    f.focusStarted(pending.ticket);
    f.wire.prepared(f.wire.preparedResult());
    f.afterPromotion = cancel;
  } else {
    f.afterPreparation = cancel;
  }
  Result result = Result::pending(pending.ticket);
  QVERIFY(until([&] {
    if (result.state == State::Pending)
      result = f.poll(pending.ticket);
    return result.state != State::Pending;
  }));
  QVERIFY(canceled);
  QCOMPARE(result.state, State::Revoked);
  QCOMPARE(f.wire.preparations.size(), 1u);
  QCOMPARE(f.wire.promotions.size(), duringPromotion ? 1u : 0u);
  QVERIFY(until([&] {
    f.dispatcher->maintain();
    return f.wire.cancellations.size() == 1;
  }));
  QVERIFY(f.wire.resumes.empty());
  QVERIFY(!f.dispatcher->legacyAcquisitionReady(f.selected.recipient()));
  if (duringPromotion)
    QVERIFY(until([&] { return f.count("release") > 0; }));
  pumpFor(10);
  QVERIFY(!f.dispatcher->legacyAcquisitionReady(f.selected.recipient()));
  const auto binding = f.wire.preparations.front().binding;
  f.wire.preparationReceipt(binding, InputPreparationState::Released);
  QVERIFY(until([&] {
    f.dispatcher->maintain();
    return f.dispatcher->legacyAcquisitionReady(f.selected.recipient());
  }));
  QCOMPARE(f.wire.cancellations.size(), 1u);
  QVERIFY(f.wire.resumes.empty());
  QCOMPARE(f.count("mapped"), 0u);
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::firstClickNoTargetRequiresExactRelease()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  Fixture f(false, true);
  Capture source;
  const auto pending = f.reserve(source.freshButton(), 0, Clock::now() + 500ms);
  f.focusStarted(pending.ticket);
  pumpFor(12);
  QCOMPARE(f.poll(pending.ticket).state, State::Pending);
  QVERIFY(!f.dispatcher->legacyAcquisitionReady(f.selected.recipient()));
  f.wire.preparationReceipt(
      f.wire.preparations.front().binding, InputPreparationState::Released, InputPreparationOutcome::NoTarget
  );
  QCOMPARE(f.poll(pending.ticket).state, State::Refused);
  QVERIFY(until([&] { return f.dispatcher->legacyAcquisitionReady(f.selected.recipient()); }));
  QCOMPARE(f.count("candidate"), 0u);
  QCOMPARE(f.count("mapped"), 0u);
  QVERIFY(f.wire.promotions.empty());
  QVERIFY(f.wire.cancellations.empty());
  QVERIFY(f.wire.resumes.empty());
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::firstClickLostTransportCannotAcknowledgeRelease()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  Fixture f(false, true);
  Capture source;
  const auto pending = f.reserve(source.freshButton(), 0, Clock::now() + 500ms);
  f.focusStarted(pending.ticket);
  f.wire.stream->close();
  QCOMPARE(f.poll(pending.ticket).state, State::Revoked);
  pumpFor(20);
  f.dispatcher->maintain();
  QVERIFY(!f.dispatcher->legacyAcquisitionReady(f.selected.recipient()));
  QVERIFY(f.wire.promotions.empty());
  QVERIFY(f.wire.resumes.empty());
  QCOMPARE(f.count("mapped"), 0u);
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::firstClickOlderPeerFallsBackBeforeAnyGate()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  Fixture f(false, true, false);
  Capture source;
  const auto pending = f.reserve(source.freshButton(), 0, Clock::now() + 500ms);
  Result result = Result::pending(pending.ticket);
  QVERIFY(until([&] {
    if (result.state == State::Pending)
      result = f.poll(pending.ticket);
    return result.state != State::Pending;
  }));
  QCOMPARE(result.state, State::Refused);
  QCOMPARE(f.quiescences, 0);
  QVERIFY(f.wire.preparations.empty());
  QVERIFY(f.wire.promotions.empty());
  QVERIFY(f.wire.cancellations.empty());
  QCOMPARE(f.count("candidate"), 0u);
  QVERIFY(until([&] { return f.dispatcher->legacyAcquisitionReady(f.selected.recipient()); }));
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::consecutiveMappedRecordsRetainSealUntilOwnerRelease()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  Fixture f;
  Capture source;
  const auto first = source.fresh();
  const auto pending = f.reserve(first);
  QCOMPARE(pending.state, State::Pending);
  f.started(pending.ticket);
  const auto binding = f.wire.requests.back().cutoff.binding;
  QCOMPARE(binding.capture, f.route(first).stamp);
  QCOMPARE(binding.forwarding, hostBinding());
  QCOMPARE(binding.viewerConnection, hostTarget().viewerConnection);
  QCOMPARE(f.wire.requests.back().point, (InputCutoffPoint{147, 219}));
  f.wire.complete(f.wire.result());
  const auto firstReady = f.accepted(pending.ticket);
  QVERIFY(f.dispatcher->commit(*firstReady.reservation));
  QVERIFY(until([&] { return f.count("mapped") == 1; }));
  const auto seal = f.wire.proxy->inputCutoffSeal(binding);
  QVERIFY(seal && seal->live());
  // Point expiry is not loss of the continuously sealed historical prefix.
  // A held repeat after an idle pause still needs its own fresh point receipt.
  pumpFor(280);
  QVERIFY(f.wire.resumes.empty());
  QCOMPARE(f.wire.proxy->inputCutoffState(binding), InputCutoffState::Sealed);
  QVERIFY(f.lastObservation && !f.lastObservation->live());
  QVERIFY(f.dispatcher->live(firstReady.reservation->owner));
  const auto second = source.key(0, OriginalKeyTransition::Repeat);
  const auto next = f.reserve(second, firstReady.reservation->owner);
  QCOMPARE(next.state, State::Pending);
  f.started(next.ticket, 2);
  QVERIFY(f.wire.resumes.empty());
  QCOMPARE(f.wire.maps.size(), 1u);
  QCOMPARE(f.wire.maps.back().sealRequest, binding.request);
  QVERIFY(!f.wire.maps.back().presentCursor);
  QVERIFY(f.wire.requests.back().cutoff.binding.request > binding.request);
  QCOMPARE(f.wire.requests.back().cutoff.binding.capture, f.route(second).stamp);
  f.wire.complete(f.wire.result());
  const auto secondReady = f.accepted(next.ticket);
  QCOMPARE(secondReady.reservation->owner, firstReady.reservation->owner);
  QVERIFY(f.dispatcher->commit(*secondReady.reservation));
  QVERIFY(until([&] { return f.count("mapped") == 2; }));
  QCOMPARE(f.count("prepare"), 1u);
  QCOMPARE(f.count("active"), 1u);
  QCOMPARE(f.quiescences, 1);
  std::vector<QJsonObject> delivered;
  for (const auto &event : f.hostEvents)
    if (event["type"] == "mapped")
      delivered.push_back(event);
  QCOMPARE(delivered[0]["sequence"].toString(), QString::number(first.input.record.sequence));
  QCOMPARE(delivered[1]["sequence"].toString(), QString::number(second.input.record.sequence));
  for (const auto &event : delivered) {
    QCOMPARE(event["epoch"].toString(), QString::number(first.input.record.epoch));
    QCOMPARE(event["geometry"].toString(), QString::number(hostBinding().geometry));
    QCOMPARE(event["x"].toDouble(), 10.25);
    QCOMPARE(event["y"].toDouble(), 20.5);
  }
  f.dispatcher->release(firstReady.reservation->owner);
  QVERIFY(until([&] { return f.wire.resumes.size() == 1 && f.count("release") == 1; }));
  QCOMPARE(f.wire.resumes.front(), binding);
  QVERIFY(!seal->live());
  QCOMPARE(f.wire.proxy->inputCutoffState(binding), InputCutoffState::Resuming);
  f.wire.receipt(binding, InputCutoffState::Resumed);
  f.dispatcher->maintain();
  QCOMPARE(f.wire.proxy->inputCutoffState(binding), InputCutoffState::Resumed);
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::nativeDeliveryBudgetRespondsToDispatchCost()
{
  OriginalInputBatchBudget budget;
  QCOMPARE(budget.limit(), std::size_t(4));
  budget.observe(16, 80ms);
  QCOMPARE(budget.limit(), OriginalInputBatchBudget::maximumPrefix);
  budget.observe(4, 80ms);
  QVERIFY(budget.limit() <= 6); // Reserve at least half the 250 ms lifetime.
  const auto reduced = budget.limit();
  budget.observe(0, 1ns);
  budget.observe(inputCutoffMaximumBatch + 1, 1ns);
  budget.observe(4, 0ns);
  budget.observe(4, -1ns);
  QCOMPARE(budget.limit(), reduced);
  budget.observe(4, 4ms);
  QVERIFY(budget.limit() > reduced && budget.limit() <= OriginalInputBatchBudget::maximumPrefix);
  for (unsigned i = 0; i < 32; ++i)
    budget.observe(4, 4ms);
  QCOMPARE(budget.limit(), OriginalInputBatchBudget::maximumPrefix);
  budget.observe(1, 200ms);
  QCOMPARE(budget.limit(), std::size_t(1));
  budget.observe(1, std::chrono::nanoseconds::max());
  QCOMPARE(budget.limit(), std::size_t(1));
  QCOMPARE(OriginalInputBatchBudget{}.limit(), std::size_t(4)); // A new owner has no inherited timing proof.
}

void OriginalInputCoordinatorTests::mixedInputBatchesKeepEveryRecordAndDrainInOrder()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  Fixture f;
  Capture source;
  source.button(false); // Actual source observation primes this control.
  const auto first = source.fresh();
  auto pending = f.reserve(first);
  QCOMPARE(pending.state, State::Pending);
  f.started(pending.ticket);
  const auto origin = f.wire.requests.front().cutoff.binding;
  f.wire.complete(f.wire.result());
  const auto acquired = f.accepted(pending.ticket);
  const auto owner = acquired.reservation->owner;
  QVERIFY(f.dispatcher->commit(*acquired.reservation));
  QVERIFY(until([&] { return f.count("mapped") == 1; }));
  std::vector<std::uint64_t> sequences{first.input.record.sequence};
  std::size_t requested = 1;
  std::size_t batches = 0;
  for (unsigned batch = 0; batch < 8; ++batch) {
    std::vector<OriginalInputCaptured> captured;
    captured.push_back(source.button(true));
    for (unsigned item = 1; item < inputCutoffMaximumBatch - 1; ++item) {
      if (item % 3 == 0)
        captured.push_back(source.key(0, OriginalKeyTransition::Repeat));
      else if (item % 3 == 1)
        captured.push_back(source.motion(true));
      else
        captured.push_back(source.scroll());
    }
    captured.push_back(source.button(false));
    std::vector<OriginalInputDispatcher::Preview> preview;
    for (const auto &value : captured) {
      preview.push_back({value, f.route(value), f.selected, Clock::now() + 500ms});
      sequences.push_back(value.input.record.sequence);
    }
    f.dispatcher->preview(preview);
    OriginalInputDispatcher::Result ready;
    // Full previews may be split according to observed native delivery cost.
    // No later receipt may start aging behind the preceding prefix's native
    // work, but all mixed records still arrive once and in source order.
    for (std::size_t offset = 0; offset < captured.size();) {
      pending = f.reserve(captured[offset], owner);
      QCOMPARE(pending.state, State::Pending);
      QVERIFY(!f.dispatcher->canCoalesceHoverMotion());
      QVERIFY(until([&] {
        const auto state = f.poll(pending.ticket).state;
        require(state == State::Pending, "Mapped prefix terminated before its actual reply");
        return f.wire.batches.size() == batches + 1;
      }));
      QCOMPARE(f.wire.batches.size(), ++batches);
      const auto prefix = f.wire.batches.back().items.size();
      QVERIFY(prefix > 0 && prefix <= OriginalInputBatchBudget::maximumPrefix && prefix <= captured.size() - offset);
      requested += prefix;
      QCOMPARE(f.wire.requests.size(), requested);
      for (std::size_t item = 0; item < prefix; ++item)
        QCOMPARE(
            f.wire.batches.back().items[item].mapping.cutoff.binding.capture.sequence,
            captured[offset + item].input.record.sequence
        );
      pumpFor(20);
      f.wire.completeBatch();
      ready = f.accepted(pending.ticket);
      QVERIFY(f.dispatcher->commit(*ready.reservation));
      for (std::size_t item = offset + 1; item < offset + prefix; ++item) {
        ready = f.reserve(captured[item], owner);
        QCOMPARE(ready.state, State::Accepted);
        QCOMPARE(ready.ticket, std::uint64_t(0));
        QVERIFY(ready.reservation && ready.reservation->owner == owner);
        QVERIFY(!f.dispatcher->canCoalesceHoverMotion());
        QVERIFY(f.dispatcher->commit(*ready.reservation));
      }
      offset += prefix;
    }
    // Core can receive the next record before Qt sends any of this full
    // batch. Keep one bounded waiting preparation without replacing its
    // predecessors' still-needed receipts or revoking a healthy owner.
    const auto next = source.key(0, OriginalKeyTransition::Repeat);
    pending = f.reserve(next, owner);
    QCOMPARE(pending.state, State::Pending);
    QCOMPARE(f.poll(pending.ticket).state, State::Pending);
    QCOMPARE(f.wire.requests.size(), requested);
    QVERIFY(f.dispatcher->live(owner));
    f.started(pending.ticket, ++requested);
    QCOMPARE(f.wire.batches.size(), ++batches);
    QCOMPARE(f.wire.batches.back().items.size(), std::size_t(1));
    f.wire.completeBatch();
    ready = f.accepted(pending.ticket);
    QVERIFY(f.dispatcher->commit(*ready.reservation));
    sequences.push_back(next.input.record.sequence);
    QVERIFY(until([&] { return f.count("mapped") == sequences.size(); }));
    QVERIFY(f.wire.resumes.empty());
    QCOMPARE(f.quiescences, 1);
  }
  std::vector<std::uint64_t> received;
  for (const auto &event : f.hostEvents)
    if (event["type"] == "mapped")
      received.push_back(event["sequence"].toString().toULongLong());
  QCOMPARE(received, sequences);
  f.dispatcher->release(owner);
  QVERIFY(until([&] { return f.wire.resumes.size() == 1 && f.count("release") == 1; }));
  QCOMPARE(f.wire.resumes.front(), origin);
  f.wire.receipt(origin, InputCutoffState::Resumed);
  f.dispatcher->maintain();
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::rejectsMappedSubstitution_data()
{
  QTest::addColumn<QString>("changed");
  for (const auto *field : {"capture", "point", "geometry", "target"})
    QTest::newRow(field) << QString::fromLatin1(field);
}
void OriginalInputCoordinatorTests::rejectsMappedSubstitution()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  QFETCH(QString, changed);
  Fixture f;
  Capture source;
  f.changeStart = [&](auto &stamp, auto &binding, auto &point) {
    if (changed == "capture")
      ++stamp.sequence;
    if (changed == "point")
      ++point.x;
    if (changed == "geometry")
      ++binding.geometry;
  };
  const auto pending = f.reserve(source.fresh());
  QCOMPARE(pending.state, State::Pending);
  f.started(pending.ticket);
  auto value = f.wire.result();
  if (changed == "target")
    value.viewerPeerFingerprint = pin(9);
  f.wire.complete(value);
  const auto decoded = f.wire.proxy->inputCutoffObservation(value.binding);
  QVERIFY(
      decoded && decoded->live()
  ); // It passed the real stream decoder; the cross-channel join must still reject it.
  QCOMPARE(f.poll(pending.ticket).state, State::Revoked);
  QVERIFY(until([&] { return f.count("release") > 0; }));
  QCOMPARE(f.count("prepare"), 0u);
  QCOMPARE(f.count("mapped"), 0u);
  QCOMPARE(f.wire.resumes, (std::vector<InputCutoffBinding>{value.binding}));
  QVERIFY(!decoded->live());
  f.wire.receipt(value.binding, InputCutoffState::Resumed);
  QCOMPARE(f.wire.proxy->inputCutoffState(value.binding), InputCutoffState::Resumed);
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::pongAndExpiredObservationCannotPrepare()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  Fixture f;
  Capture source;
  const auto pending = f.reserve(source.fresh(), 0, Clock::now() + 150ms);
  QCOMPARE(pending.state, State::Pending);
  f.started(pending.ticket);
  const auto binding = f.wire.requests.back().cutoff.binding;
  f.wire.receipt(binding, InputCutoffState::BackendObserved);
  QCOMPARE(f.poll(pending.ticket).state, State::Pending);
  pumpFor(15);
  QCOMPARE(f.count("prepare"), 0u);
  QCOMPARE(f.count("active"), 0u);
  QVERIFY(!f.wire.proxy->inputCutoffObservation(binding));
  f.wire.complete(f.wire.result());
  const auto decoded = f.wire.proxy->inputCutoffObservation(binding);
  QVERIFY(decoded && decoded->live());
  QVERIFY(until([&] { return !decoded->live(); }, 1000));
  QCOMPARE(f.poll(pending.ticket).state, State::Revoked);
  QVERIFY(until([&] { return f.count("release") > 0; }));
  QCOMPARE(f.count("prepare"), 0u);
  QCOMPARE(f.count("mapped"), 0u);
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::cancellationDuringBackendCallback_data()
{
  QTest::addColumn<QString>("callback");
  for (const auto *stage : {"quiesce", "start", "observe"})
    QTest::newRow(stage) << QString::fromLatin1(stage);
}
void OriginalInputCoordinatorTests::cancellationDuringBackendCallback()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  QFETCH(QString, callback);
  Fixture f;
  Capture source;
  const auto first = source.fresh();
  const auto unknown = source.key(1, OriginalKeyTransition::Down);
  const auto pending = f.reserve(first);
  QCOMPARE(pending.state, State::Pending);
  bool called = false;
  Result unfinished = Result::refused();
  const auto cancel = [&] {
    called = true;
    f.dispatcher->cancelPreparation(pending.ticket);
    unfinished = f.reserve(unknown);
  };
  if (callback == "quiesce")
    f.afterQuiesce = cancel;
  if (callback == "start")
    f.afterStart = cancel;
  if (callback == "observe") {
    f.started(pending.ticket);
    f.wire.complete(f.wire.result());
    f.afterObserve = cancel;
  }
  Result result = Result::pending(pending.ticket);
  QVERIFY(until([&] {
    result = f.poll(pending.ticket);
    return called;
  }));
  QCOMPARE(result.state, State::Revoked);
  QCOMPARE(unfinished.state, State::Revoked); // An unfinished handoff cannot return legacy permission.
  std::optional<InputCutoffBinding> cleanup;
  if (callback == "quiesce")
    QVERIFY(f.wire.requests.empty());
  else {
    QCOMPARE(f.wire.requests.size(), 1u);
    const auto binding = f.wire.requests.back().cutoff.binding;
    cleanup = binding;
    QCOMPARE(f.wire.resumes, (std::vector<InputCutoffBinding>{binding}));
    QVERIFY(!f.lastObservation || !f.lastObservation->live());
  }
  QVERIFY(until([&] { return f.count("release") > 0; }));
  if (cleanup) {
    // The exact Host released reply alone cannot reopen legacy while the
    // retained Core cutoff is still awaiting its independent resume receipt.
    QCOMPARE(f.reserve(unknown).state, State::Revoked);
    const auto binding = *cleanup;
    f.wire.receipt(binding, InputCutoffState::Resumed);
    f.dispatcher->maintain();
    QCOMPARE(f.wire.proxy->inputCutoffState(binding), InputCutoffState::Resumed);
    QVERIFY(!f.wire.proxy->inputCutoffObservation(binding));
  }
  QVERIFY(until([&] { return f.reserve(unknown).state == State::Refused; }));
  QCOMPARE(f.count("prepare"), 0u);
  QCOMPARE(f.count("mapped"), 0u);
  f.finish();
#endif
}

void OriginalInputCoordinatorTests::stopMayDestroyCoordinatorFromPriorRevocationCallback()
{
#if !defined(__APPLE__)
  QSKIP("Positive source admission uses the actual Mac capture adapter");
#else
  Fixture f;
  Capture source;
  const auto pending = f.reserve(source.fresh());
  QCOMPARE(pending.state, State::Pending);
  f.started(pending.ticket);
  f.wire.complete(f.wire.result());
  const auto ready = f.accepted(pending.ticket);
  QVERIFY(f.dispatcher->live(ready.reservation->owner));
  int revoked = 0;
  f.priorRevoked = [&] {
    ++revoked;
    f.coordinator.reset();
  };
  f.coordinator->stop();
  QVERIFY(!f.coordinator);
  QCOMPARE(revoked, 1);
  QVERIFY(!f.dispatcher->live(ready.reservation->owner));
  QVERIFY(!f.dispatcher->commit(*ready.reservation));
  QVERIFY(!f.channel->onInputTarget);
  QVERIFY(until([&] { return f.count("release") > 0; }));
  QCOMPARE(f.count("mapped"), 0u);
  f.finish();
#endif
}

int main(int argc, char **argv)
{
  const bool child = argc == 4 && std::string_view(argv[1]) == "--coordinator-host";
  const int descriptor = child ? QByteArray(argv[2]).toInt() : -1;
  if (child && !protectLocalInputDescriptor(descriptor))
    return 2;
  QCoreApplication application(argc, argv);
  try {
    if (child)
      return hostChild(descriptor, std::string_view(argv[3]) == "decline", std::string_view(argv[3]) == "late-target");
    OriginalInputCoordinatorTests tests;
    return QTest::qExec(&tests, argc, argv);
  } catch (const std::exception &error) {
    QTextStream(stderr) << error.what() << Qt::endl;
    return 1;
  }
}
