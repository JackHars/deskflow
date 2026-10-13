/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerProxyTests.h"

#include "base/Event.h"
#include "base/IEventQueue.h"
#include "client/Client.h"
#include "client/ServerProxy.h"
#include "client/ServerProxy1_8.h"
#include "common/Settings.h"
#include "deskflow/AppUtil.h"
#include "deskflow/Computer.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/PacketStreamFilter.h"
#include "deskflow/PlatformComputer.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"
#include "net/ISocketFactory.h"

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
#include "deskflow/InputCutoffProtocol.h"
#include "deskflow/InputPreparationProtocol.h"
#include "deskflow/InputSequence.h"
#include "windowshare/EiPrefixProbe.h"
#endif

#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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

class FakeStream : public deskflow::IStream
{
public:
  void push(const std::string &bytes)
  {
    m_chunks.push_back(bytes);
  }

  void close() override
  {
    m_chunks.clear();
    m_inputShutdown = true;
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    revoke();
#endif
  }

  uint32_t read(void *buffer, uint32_t size) override
  {
    if (m_inputShutdown || m_chunks.empty() || size == 0) {
      return 0;
    }

    auto &front = m_chunks.front();
    const size_t bytesToRead = std::min(static_cast<size_t>(size), front.size());
    if (buffer != nullptr) {
      std::memcpy(buffer, front.data(), bytesToRead);
    }

    front.erase(0, bytesToRead);
    if (front.empty()) {
      m_chunks.pop_front();
    }
    return static_cast<uint32_t>(bytesToRead);
  }

  void write(const void *bytes, uint32_t size) override
  {
    writes.emplace_back(static_cast<const char *>(bytes), size);
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
    close();
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<FakeStream *>(this);
  }

  bool isReady() const override
  {
    return !m_inputShutdown && !m_chunks.empty();
  }

  uint32_t getSize() const override
  {
    // Like PacketStreamFilter, expose only the current packet, not later ones.
    return m_inputShutdown || m_chunks.empty() ? 0 : static_cast<uint32_t>(m_chunks.front().size());
  }

  std::vector<std::string> writes;

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  void authenticate()
  {
    issuer = std::make_unique<deskflow::AuthenticatedPeerIssuer>();
    deskflow::AuthenticatedPeerTicket::Fingerprint fingerprint{};
    fingerprint.fill(0x35);
    if (!issuer->issue(fingerprint))
      throw std::runtime_error("Cannot issue fixture authentication");
  }
  void revoke()
  {
    if (issuer)
      issuer->revoke();
  }
  std::optional<deskflow::AuthenticatedPeerTicket> authenticatedPeerTicket() const override
  {
    return issuer ? issuer->ticket() : std::nullopt;
  }
  std::unique_ptr<deskflow::AuthenticatedPeerIssuer> issuer;
#endif

private:
  std::deque<std::string> m_chunks;
  bool m_inputShutdown = false;
};

class RecordingEventQueue : public IEventQueue
{
public:
  ~RecordingEventQueue() override
  {
    for (const auto &event : m_addedEvents) {
      Event::deleteData(event);
    }
  }

  int loop() override
  {
    return 0;
  }

  void adoptBuffer(IEventQueueBuffer *) override
  {
  }

  bool getEvent(Event &, double = -1.0) override
  {
    return false;
  }

  bool dispatchEvent(const Event &event) override
  {
    auto handler = m_handlers.find(HandlerKey{event.getType(), event.getTarget()});
    if (handler == m_handlers.end())
      handler = m_handlers.find(HandlerKey{EventTypes::Unknown, event.getTarget()});
    if (handler == m_handlers.end()) {
      return false;
    }

    const auto callback = handler->second;
    callback(event);
    return true;
  }

  void addEvent(Event &&event) override
  {
    m_addedEvents.emplace_back(std::move(event));
  }

  EventQueueTimer *newTimer(double, void *target) override
  {
    repeatingTarget = target;
    return timer();
  }
  void *repeatingTarget = nullptr;

  EventQueueTimer *newOneShotTimer(double, void *) override
  {
    return timer();
  }

  void deleteTimer(EventQueueTimer *) override
  {
  }

  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  {
    m_handlers[HandlerKey{type, target}] = handler;
  }

  void removeHandler(EventTypes type, void *target) override
  {
    m_handlers.erase(HandlerKey{type, target});
  }

  void removeHandlers(void *target) override
  {
    for (auto it = m_handlers.begin(); it != m_handlers.end();) {
      if (it->first.target == target) {
        it = m_handlers.erase(it);
      } else {
        ++it;
      }
    }
  }

  void waitForReady() const override
  {
  }

  void *getSystemTarget() override
  {
    return this;
  }

  EventQueueTimer *timer()
  {
    return reinterpret_cast<EventQueueTimer *>(&m_timerStorage);
  }

  const std::vector<Event> &addedEvents() const
  {
    return m_addedEvents;
  }

private:
  struct HandlerKey
  {
    EventTypes type;
    void *target;

    bool operator<(const HandlerKey &other) const
    {
      if (type != other.type) {
        return static_cast<uint32_t>(type) < static_cast<uint32_t>(other.type);
      }
      return std::less<void *>{}(target, other.target);
    }
  };

  int m_timerStorage = 0;
  std::map<HandlerKey, EventHandler> m_handlers;
  std::vector<Event> m_addedEvents;
};

class InputPlatform : public PlatformComputer
{
public:
  explicit InputPlatform(IEventQueue *events) : PlatformComputer(events)
  {
  }
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  bool beginInputPreparation(
      const deskflow::windowshare::InputPreparationBinding &binding, deskflow::windowshare::InputCutoffPoint point,
      deskflow::windowshare::InputCutoffClock::time_point deadline
  ) override
  {
    ++preparationBegins;
    if (!preparationSupported || preparationBinding)
      return false;
    preparationBinding = binding;
    preparationPoint = point;
    preparationDeadline = deadline;
    preparationState = deskflow::windowshare::InputPreparationState::Preparing;
    const auto callback = onPreparationBegin;
    if (callback)
      callback();
    return true;
  }
  deskflow::windowshare::InputPreparationState
  inputPreparationState(const deskflow::windowshare::InputPreparationBinding &binding) override
  {
    const auto callback = onPreparationObserve;
    if (callback)
      callback();
    return preparationBinding == binding ? preparationState : deskflow::windowshare::InputPreparationState::Failed;
  }
  deskflow::windowshare::InputPreparationOutcome
  inputPreparationOutcome(const deskflow::windowshare::InputPreparationBinding &) override
  {
    return preparationOutcome;
  }
  std::optional<deskflow::windowshare::PreparedInputFocusResult>
  preparedInputFocusResult(const deskflow::windowshare::InputPreparationBinding &binding) override
  {
    return preparationBinding == binding ? prepared : std::nullopt;
  }
  bool promoteInputPreparation(
      const deskflow::windowshare::InputPreparationBinding &preparation,
      const deskflow::windowshare::InputCutoffBinding &binding,
      deskflow::windowshare::InputCutoffClock::time_point deadline
  ) override
  {
    if (preparationBinding != preparation || preparationState != deskflow::windowshare::InputPreparationState::Prepared)
      return false;
    ++preparationPromotions;
    preparationState = deskflow::windowshare::InputPreparationState::Promoting;
    cutoffBinding = binding;
    cutoffPoint = preparationPoint;
    cutoffDeadline = deadline;
    sealOrigin = binding;
    cutoffProbe.begin(binding.request, binding.request, deskflow::windowshare::InputCutoffClock::now(), deadline, true);
    const auto callback = onPreparationPromote;
    if (callback)
      callback();
    return true;
  }
  bool cancelInputPreparation(
      const deskflow::windowshare::InputPreparationBinding &binding,
      deskflow::windowshare::InputCutoffClock::time_point deadline
  ) override
  {
    if (preparationBinding != binding)
      return false;
    ++preparationCancels;
    if (preparationState != deskflow::windowshare::InputPreparationState::Canceling)
      preparationCleanupDeadline = deadline;
    preparationState = deskflow::windowshare::InputPreparationState::Canceling;
    const auto callback = onPreparationCancel;
    if (callback)
      callback();
    return true;
  }
  void abortInputPreparation(const deskflow::windowshare::InputPreparationBinding &binding) noexcept override
  {
    if (preparationBinding != binding)
      return;
    ++preparationAborts;
    preparationBinding.reset();
    prepared.reset();
    cutoffBinding.reset();
    sealOrigin.reset();
    sealValid = false;
    preparationState = deskflow::windowshare::InputPreparationState::Failed;
  }
  void finishPreparationCleanup()
  {
    preparationState = deskflow::windowshare::InputPreparationState::Released;
    prepared.reset();
    cutoffBinding.reset();
    sealOrigin.reset();
    mappedResult.reset();
    sealValid = false;
  }
  bool preparationSupported = false;
  unsigned preparationBegins = 0, preparationPromotions = 0, preparationCancels = 0, preparationAborts = 0;
  std::optional<deskflow::windowshare::InputPreparationBinding> preparationBinding;
  deskflow::windowshare::InputCutoffPoint preparationPoint;
  deskflow::windowshare::InputCutoffClock::time_point preparationDeadline, preparationCleanupDeadline;
  deskflow::windowshare::InputPreparationState preparationState =
      deskflow::windowshare::InputPreparationState::Preparing;
  deskflow::windowshare::InputPreparationOutcome preparationOutcome =
      deskflow::windowshare::InputPreparationOutcome::None;
  std::optional<deskflow::windowshare::PreparedInputFocusResult> prepared;
  std::function<void()> onPreparationBegin, onPreparationObserve, onPreparationPromote, onPreparationCancel;
  bool beginInputCutoff(
      const deskflow::windowshare::InputCutoffBinding &binding,
      deskflow::windowshare::InputCutoffClock::time_point deadline
  ) override
  {
    ++cutoffBegins;
    if (!cutoffSupported || cutoffBinding)
      return false;
    const bool started = cutoffProbe.begin(
        binding.request, binding.request, deskflow::windowshare::InputCutoffClock::now(), deadline, true
    );
    if (!started)
      return false;
    cutoffBinding = binding;
    if (onCutoffBegin)
      onCutoffBegin();
    return started;
  }
  deskflow::windowshare::InputCutoffState
  inputCutoffState(const deskflow::windowshare::InputCutoffBinding &binding) override
  {
    using namespace deskflow::windowshare;
    if (cutoffBinding != binding &&
        std::none_of(batchItems.begin(), batchItems.end(), [&](const auto &item) { return item.binding == binding; }))
      return InputCutoffState::Denied;
    if (!batchItems.empty()) {
      if (!sealValid || sealFault)
        return InputCutoffState::Denied;
      if (InputCutoffClock::now() >= cutoffDeadline)
        return InputCutoffState::Sealed;
      return batchResults.size() == batchItems.size() ? InputCutoffState::DrainObserved : InputCutoffState::Pending;
    }
    if (sealOrigin) {
      if (sealFault)
        return InputCutoffState::Denied;
      if (!sealValid && mappedResult && cutoffProbe.observed(InputCutoffClock::now()))
        sealValid = true;
      if (sealValid) {
        if (InputCutoffClock::now() >= cutoffDeadline)
          return InputCutoffState::Sealed;
        return mappedResult ? InputCutoffState::DrainObserved : InputCutoffState::Pending;
      }
    }
    if (cutoffProbe.observed(InputCutoffClock::now()))
      return cutoffPoint ? (mappedResult ? InputCutoffState::DrainObserved : InputCutoffState::Pending)
                         : InputCutoffState::BackendObserved;
    return cutoffProbe.state() == EiPrefixProbe::State::Denied ? InputCutoffState::Denied : InputCutoffState::Pending;
  }
  bool resumeInputCutoff(const deskflow::windowshare::InputCutoffBinding &binding) override
  {
    if ((sealOrigin ? sealOrigin : cutoffBinding) != binding)
      return false;
    ++cutoffResumes;
    cutoffBinding.reset();
    cutoffPoint.reset();
    mappedResult.reset();
    sealOrigin.reset();
    sealValid = false;
    batchItems.clear();
    batchResults.clear();
    const bool retired = cutoffProbe.retire(binding.request);
    const auto callback = onCutoffResume;
    if (callback)
      callback();
    return retired;
  }
  bool cutoffSupported = false;
  unsigned cutoffBegins = 0, cutoffResumes = 0;
  std::optional<deskflow::windowshare::InputCutoffBinding> cutoffBinding;
  deskflow::windowshare::EiPrefixProbe cutoffProbe;
  std::function<void()> onCutoffBegin;
  std::function<void()> onCutoffResume;
  bool beginMappedInputCutoff(
      const deskflow::windowshare::InputCutoffBinding &binding, deskflow::windowshare::InputCutoffPoint point,
      deskflow::windowshare::InputCutoffClock::time_point deadline
  ) override
  {
    cutoffPoint = point;
    cutoffDeadline = deadline;
    mappedResult.reset();
    if (beginInputCutoff(binding, deadline))
      return true;
    cutoffPoint.reset();
    return false;
  }
  std::optional<deskflow::windowshare::InputCutoffMappedResult>
  mappedInputCutoffResult(const deskflow::windowshare::InputCutoffBinding &binding) override
  {
    auto result = mappedResult;
    if (!batchItems.empty()) {
      const auto found = std::find_if(batchResults.begin(), batchResults.end(), [&](const auto &item) {
        return item.binding == binding;
      });
      result = found == batchResults.end() ? std::nullopt : std::optional(*found);
    }
    const auto callback = onMappedQuery;
    if (callback)
      callback();
    return result;
  }
  std::optional<deskflow::windowshare::InputCutoffPoint> cutoffPoint;
  deskflow::windowshare::InputCutoffClock::time_point cutoffDeadline;
  std::optional<deskflow::windowshare::InputCutoffMappedResult> mappedResult;
  std::function<void()> onMappedQuery;
  bool beginSealedInputCutoff(
      const deskflow::windowshare::InputCutoffBinding &binding, deskflow::windowshare::InputCutoffPoint point,
      deskflow::windowshare::InputCutoffClock::time_point deadline
  ) override
  {
    if (!beginMappedInputCutoff(binding, point, deadline))
      return false;
    sealOrigin = binding;
    return true;
  }
  bool inputCutoffSealLive(const deskflow::windowshare::InputCutoffBinding &binding) override
  {
    return sealOrigin == binding && sealValid && !sealFault;
  }
  bool refreshMappedInputCutoff(
      const deskflow::windowshare::InputCutoffBinding &seal, const deskflow::windowshare::InputCutoffBinding &binding,
      deskflow::windowshare::InputCutoffPoint point, deskflow::windowshare::InputCutoffClock::time_point deadline,
      bool present
  ) override
  {
    if (!inputCutoffSealLive(seal) || !cutoffBinding ||
        !deskflow::windowshare::continuesInputCutoffSeal(*cutoffBinding, binding))
      return false;
    ++mapRequests;
    cutoffBinding = binding;
    cutoffPoint = point;
    cutoffDeadline = deadline;
    mappedResult.reset();
    if (present)
      presented.push_back(point);
    const auto callback = onMapBegin;
    if (callback)
      callback();
    return !sealFault;
  }
  bool beginMappedInputBatch(
      const deskflow::windowshare::InputCutoffBinding &seal,
      const std::vector<deskflow::windowshare::InputCutoffBatchItem> &items
  ) override
  {
    if (!inputCutoffSealLive(seal) || !cutoffBinding ||
        !deskflow::windowshare::validInputCutoffBatch(*cutoffBinding, items))
      return false;
    ++batchRequests;
    batchItems = items;
    batchResults.clear();
    cutoffBinding = items.back().binding;
    cutoffPoint.reset();
    cutoffDeadline = std::min_element(items.begin(), items.end(), [](const auto &a, const auto &b) {
                       return a.deadline < b.deadline;
                     })->deadline;
    mappedResult.reset();
    for (const auto &item : items)
      if (item.presentCursor)
        presented.push_back(item.point);
    const auto callback = onMapBegin;
    if (callback)
      callback();
    return !sealFault;
  }
  std::vector<deskflow::windowshare::InputCutoffBatchItem> batchItems;
  std::vector<deskflow::windowshare::InputCutoffMappedResult> batchResults;
  unsigned batchRequests = 0;
  std::optional<deskflow::windowshare::InputCutoffBinding> sealOrigin;
  bool sealValid = false, sealFault = false;
  unsigned mapRequests = 0;
  std::vector<deskflow::windowshare::InputCutoffPoint> presented;
  std::function<void()> onMapBegin;
#endif

  void *getEventTarget() const override
  {
    return const_cast<InputPlatform *>(this);
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
    x = y = 500;
  }
  void reconfigure(uint32_t) override
  {
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
  bool isAnyMouseButtonDown(uint32_t &) const override
  {
    return false;
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
    return 0;
  }
  void enable() override
  {
  }
  void disable() override
  {
  }
  void enter() override
  {
  }
  bool canLeave() override
  {
    return true;
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
    return false;
  }

protected:
  void updateButtons() override
  {
  }
  IKeyState *getKeyState() const override
  {
    // RecordingClient intercepts input; this fixture never enables a backend.
    return nullptr;
  }
  void handleSystemEvent(const Event &) override
  {
  }
};

class UnusedSocketFactory final : public ISocketFactory
{
public:
  IDataSocket *create(IArchNetwork::AddressFamily, SecurityLevel) const override
  {
    return nullptr;
  }
  IListenSocket *createListen(IArchNetwork::AddressFamily, SecurityLevel) const override
  {
    return nullptr;
  }
};

struct InputCall
{
  std::string kind;
  uint32_t id = 0, mask = 0, button = 0;
  int32_t x = 0, y = 0, count = 0;
  std::string language;
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  std::optional<deskflow::windowshare::InputSequenceToken> token = std::nullopt;
#endif
};

class RecordingClient final : public Client
{
public:
  RecordingClient(IEventQueue *events, deskflow::Computer *computer)
      : Client(events, "sequence-test", NetworkAddress("unused.test", 24800), new UnusedSocketFactory, computer)
  {
  }
  void handshakeComplete() override
  {
    ++handshakes;
  }
  void resetOptions() override
  {
    ++resets;
  }
  void setOptions(const OptionsList &) override
  {
    ++options;
  }
  void keyDown(KeyID id, KeyModifierMask mask, KeyButton button, const std::string &language) override
  {
    append({"keyDown", id, mask, button, 0, 0, 0, language});
  }
  void keyUp(KeyID id, KeyModifierMask mask, KeyButton button) override
  {
    append({"keyUp", id, mask, button, 0, 0, 0, {}});
  }
  void keyRepeat(KeyID id, KeyModifierMask mask, int32_t count, KeyButton button, const std::string &language) override
  {
    append({"keyRepeat", id, mask, button, 0, 0, count, language});
  }
  void mouseDown(ButtonID id) override
  {
    append({"mouseDown", id, 0, 0, 0, 0, 0, {}});
  }
  void mouseUp(ButtonID id) override
  {
    append({"mouseUp", id, 0, 0, 0, 0, 0, {}});
  }
  void mouseMove(int32_t x, int32_t y) override
  {
    append({"mouseMove", 0, 0, 0, x, y, 0, {}});
  }
  void mouseRelativeMove(int32_t x, int32_t y) override
  {
    append({"mouseRelativeMove", 0, 0, 0, x, y, 0, {}});
  }
  void mouseWheel(int32_t x, int32_t y) override
  {
    append({"mouseWheel", 0, 0, 0, x, y, 0, {}});
  }

  unsigned handshakes = 0, resets = 0, options = 0;
  std::vector<InputCall> calls;
  std::function<void(const InputCall &)> onInput;

private:
  void append(InputCall call)
  {
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    call.token = deskflow::windowshare::InputSequenceSubmissionScope::currentToken();
#endif
    calls.push_back(call);
    if (onInput)
      onInput(call);
  }
};

template <typename... Args> std::string wire(const char *format, Args... args)
{
  FakeStream output;
  ProtocolUtil::writef(&output, format, args...);
  return output.writes.at(0);
}

struct InputFixture
{
  RecordingEventQueue events;
  InputPlatform *platform = new InputPlatform(&events);
  deskflow::Computer computer{platform, &events};
  RecordingClient client{&events, &computer};
  FakeStream stream;
  std::unique_ptr<ServerProxy> proxy;

  explicit InputFixture(bool latest = true)
  {
    if (latest)
      proxy = std::make_unique<ServerProxy1_8>(&client, &stream, &events);
    else
      proxy = std::make_unique<ServerProxy>(&client, &stream, &events);
  }
  void dispatch()
  {
    if (!events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget())))
      throw std::runtime_error("Input receiver handler missing");
  }
  void send(const std::string &packet)
  {
    stream.push(packet);
    dispatch();
  }
  void handshake(const OptionsList &options = {})
  {
    send(wire(kMsgDSetOptions, &options));
  }
};

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
using namespace deskflow::windowshare;

InputSequenceToken token(uint32_t generation = 7, uint64_t sequence = 1)
{
  return {generation, sequence, {31, 400 + sequence, 2}};
}

std::string uncheckedEnvelope(const InputSequenceToken &value, std::string_view payload)
{
  std::string frame("DSEQ");
  inputSequenceDetail::append(frame, inputSequenceHeaderBytes + payload.size(), 2);
  inputSequenceDetail::append(frame, value.generation, 4);
  inputSequenceDetail::append(frame, value.wireSequence, 8);
  inputSequenceDetail::append(frame, value.capture.epoch, 8);
  inputSequenceDetail::append(frame, value.capture.sequence, 8);
  inputSequenceDetail::append(frame, value.capture.part, 2);
  frame.append(payload);
  return frame;
}

std::vector<InputSequenceReceipt> receipts(const FakeStream &stream)
{
  std::vector<InputSequenceReceipt> result;
  for (const auto &write : stream.writes) {
    if (!write.starts_with("CISR"))
      continue;
    const auto receipt = decodeInputSequenceReceipt(std::string_view(write).substr(4));
    if (!receipt)
      throw std::runtime_error("Receiver wrote an invalid receipt");
    result.push_back(*receipt);
  }
  return result;
}

void negotiate(InputFixture &fixture, uint32_t generation = 7)
{
  fixture.stream.authenticate();
  fixture.handshake({kOptionInputSequenceTrace, generation});
  fixture.stream.writes.clear();
}
InputCutoffBinding cutoffBinding(std::uint64_t request = 1)
{
  return {request, 9, {31, 400, 1}, {{11, 12}, 13, 14, 15, 16, ForwardingScope::All}, 17};
}
void negotiateCutoff(InputFixture &fixture, bool mapped = false, bool sealed = false, bool batched = false)
{
  fixture.stream.authenticate();
  fixture.handshake(
      {batched  ? kOptionBatchedInputCutoff
       : sealed ? kOptionSealedInputCutoff
       : mapped ? kOptionMappedInputCutoff
                : kOptionInputCutoff,
       9}
  );
  fixture.stream.writes.clear();
  fixture.platform->cutoffSupported = true;
}
InputCutoffMappedResult mappedCutoffResult(const InputCutoffBinding &binding, InputCutoffPoint point)
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
std::vector<InputCutoffReceipt> cutoffReceipts(const FakeStream &stream)
{
  std::vector<InputCutoffReceipt> result;
  for (const auto &bytes : stream.writes) {
    if (!bytes.starts_with("CICR"))
      continue;
    const auto receipt = decodeInputCutoffReceipt(std::string_view(bytes).substr(4));
    if (!receipt)
      throw std::runtime_error("Invalid cutoff reply");
    result.push_back(*receipt);
  }
  return result;
}
void pollCutoff(InputFixture &fixture)
{
  if (!fixture.events.dispatchEvent(Event(EventTypes::Timer, fixture.events.repeatingTarget)))
    throw std::runtime_error("Cutoff poll handler missing");
}
#endif

class TestServerProxy : public ServerProxy
{
public:
  using ServerProxy::ServerProxy;

  bool parseHandshakeMessageReturnsDisconnect(const uint8_t *code)
  {
    return parseHandshakeMessage(code) == ConnectionResult::Disconnect;
  }
};

Client *undereferenceableClient()
{
  // These paths must queue cleanup without calling through to Client.
  return reinterpret_cast<Client *>(0x1);
}

TestAppUtil &testAppUtil()
{
  static TestAppUtil util;
  return util;
}

const Client::DisconnectRequest *disconnectRequest(const RecordingEventQueue &events)
{
  if (events.addedEvents().size() != 1) {
    return nullptr;
  }
  return static_cast<const Client::DisconnectRequest *>(events.addedEvents().front().getDataObject());
}

} // namespace

void ServerProxyTests::initTestCase()
{
  m_arch.init();
  (void)testAppUtil();
  static QTemporaryDir settings;
  QVERIFY(settings.isValid());
  qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
  qputenv("XDG_STATE_HOME", settings.path().toUtf8());
  QDir(settings.path()).mkpath(kAppName);
  Settings::setSettingsFile(settings.filePath("receiver.conf"));
  Settings::setStateFile(settings.filePath("receiver.state"));
  m_log.setFilter(LogLevel::Level::Debug);
}

void ServerProxyTests::handleKeepAliveAlarm_timeout_queuesDisconnectRequest()
{
  RecordingEventQueue events;
  FakeStream stream;
  ServerProxy proxy(undereferenceableClient(), &stream, &events);

  QVERIFY(events.dispatchEvent(Event(EventTypes::Timer, events.timer())));

  QCOMPARE(events.addedEvents().size(), static_cast<size_t>(1));
  const auto &event = events.addedEvents().front();
  QVERIFY(event.getType() == EventTypes::ClientDisconnectRequested);
  QCOMPARE(event.getTarget(), stream.getEventTarget());

  const auto *request = disconnectRequest(events);
  QVERIFY(request != nullptr);
  QVERIFY(request->kind() == Client::DisconnectRequest::Kind::Disconnect);
  QCOMPARE(QString::fromUtf8(request->message()), QStringLiteral("server is not responding"));
}

void ServerProxyTests::handleData_incompleteMessage_queuesDisconnectRequest()
{
  RecordingEventQueue events;
  FakeStream stream;
  stream.push("DF");
  ServerProxy proxy(undereferenceableClient(), &stream, &events);

  QVERIFY(events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget())));

  QCOMPARE(events.addedEvents().size(), static_cast<size_t>(1));
  const auto &event = events.addedEvents().front();
  QVERIFY(event.getType() == EventTypes::ClientDisconnectRequested);
  QCOMPARE(event.getTarget(), stream.getEventTarget());

  const auto *request = disconnectRequest(events);
  QVERIFY(request != nullptr);
  QVERIFY(request->kind() == Client::DisconnectRequest::Kind::Disconnect);
  QCOMPARE(QString::fromUtf8(request->message()), QStringLiteral("incomplete message from server"));
}

void ServerProxyTests::parseHandshakeMessage_protocolError_queuesRefusalRequest()
{
  RecordingEventQueue events;
  FakeStream stream;
  TestServerProxy proxy(undereferenceableClient(), &stream, &events);

  QVERIFY(proxy.parseHandshakeMessageReturnsDisconnect(reinterpret_cast<const uint8_t *>(kMsgEBad)));
  QCOMPARE(events.addedEvents().size(), static_cast<size_t>(1));
  const auto *request = disconnectRequest(events);
  QVERIFY(request != nullptr);
  QVERIFY(request->kind() == Client::DisconnectRequest::Kind::Refuse);
  QVERIFY(request->refusalReason() == deskflow::core::ConnectionRefusal::ProtocolError);
  QCOMPARE(QString::fromUtf8(request->message()), QStringLiteral("server reported a protocol error"));
}

void ServerProxyTests::legacyInput_preservesCompressionLanguageAndModifiers()
{
  InputFixture f;
  f.handshake({kOptionModifierMapForSuper, kKeyModifierIDControl});
  QCOMPARE(f.client.handshakes, 1U);
  QVERIFY(f.stream.writes.empty());

  const std::string language = "en";
  f.stream.push(wire(kMsgDMouseMove, 10, 20));
  f.stream.push(wire(kMsgDMouseMove, 30, 40));
  f.stream.push(wire(kMsgDKeyDown, kKeySuper_L, KeyModifierSuper, 12, &language));
  f.stream.push(wire(kMsgDKeyRepeat, 'a', KeyModifierSuper, 3, 22, &language));
  f.stream.push(wire(kMsgDKeyUp, kKeySuper_L, KeyModifierSuper, 12));
  f.stream.push(wire(kMsgDMouseRelMove, 3, -2));
  f.stream.push(wire(kMsgDMouseRelMove, 4, 5));
  f.stream.push(wire(kMsgDMouseWheel, -120, 240));
  f.dispatch();

  QCOMPARE(f.client.calls.size(), std::size_t(6));
  const auto &calls = f.client.calls;
  QCOMPARE(calls[0].kind, std::string("mouseMove"));
  QCOMPARE(calls[0].x, 30);
  QCOMPARE(calls[0].y, 40);
  QCOMPARE(calls[1].kind, std::string("keyDown"));
  QCOMPARE(calls[1].id, kKeyControl_L);
  QCOMPARE(calls[1].mask, uint32_t(KeyModifierControl));
  QCOMPARE(calls[1].button, 12U);
  QCOMPARE(calls[1].language, language);
  QCOMPARE(calls[2].kind, std::string("keyRepeat"));
  QCOMPARE(calls[2].count, 3);
  QCOMPARE(calls[2].language, language);
  QCOMPARE(calls[2].mask, uint32_t(KeyModifierControl));
  QCOMPARE(calls[3].kind, std::string("keyUp"));
  QCOMPARE(calls[3].id, kKeyControl_L);
  QCOMPARE(calls[4].kind, std::string("mouseRelativeMove"));
  QCOMPARE(calls[4].x, 7);
  QCOMPARE(calls[4].y, 3);
  QCOMPARE(calls[5].kind, std::string("mouseWheel"));
  QCOMPARE(calls[5].x, -120);
  QCOMPARE(calls[5].y, 240);
  QCOMPARE(f.stream.writes.size(), std::size_t(8));
  for (const auto &reply : f.stream.writes)
    QCOMPARE(reply, std::string("CNOP"));
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  for (const auto &call : calls)
    QVERIFY(!call.token);
#endif
}

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
void ServerProxyTests::inputSequence_onlyAuthenticatedLatestProxyAcknowledges()
{
  {
    InputFixture f;
    f.handshake();
    QVERIFY(f.stream.writes.empty());
    f.send(wire(kMsgDMouseWheel, 0, 120));
    QCOMPARE(f.client.calls.size(), std::size_t(1));
    QCOMPARE(f.stream.writes, std::vector<std::string>{"CNOP"});
  }
  {
    InputFixture f;
    f.handshake({kOptionInputSequenceTrace, 7});
    QVERIFY(f.stream.writes.empty());
    f.send(encodeInputSequenceEnvelope(token(), wire(kMsgDMouseDown, 1)));
    QVERIFY(f.client.calls.empty());
    QVERIFY(disconnectRequest(f.events));
    QVERIFY(receipts(f.stream).empty());
  }
  {
    InputFixture f(false);
    f.stream.authenticate();
    f.handshake({kOptionInputSequenceTrace, 7});
    QVERIFY(f.stream.writes.empty());
    QCOMPARE(f.client.handshakes, 1U);
    f.send(wire(kMsgDKeyRepeat1_1, 'a', 0, 3, 4));
    QCOMPARE(f.client.calls.size(), std::size_t(1));
    QCOMPARE(f.client.calls[0].count, 3);
    QVERIFY(!f.client.calls[0].token);
  }
  {
    InputFixture f;
    f.stream.authenticate();
    f.handshake({kOptionInputSequenceTrace, 0x80000001U});
    QCOMPARE(f.stream.writes, std::vector<std::string>{wire(kMsgCInputSequenceAck, 0x80000001U)});
    f.send(encodeInputSequenceEnvelope(token(0x80000001U), wire(kMsgDMouseDown, 1)));
    QCOMPARE(receipts(f.stream).size(), std::size_t(1));
  }
}

void ServerProxyTests::inputSequence_generationResetAndWithdrawal()
{
  const auto payload = wire(kMsgDMouseDown, 1);
  InputFixture f;
  negotiate(f);
  f.send(encodeInputSequenceEnvelope(token(), payload));
  const OptionsList duplicate{kOptionInputSequenceTrace, 7};
  f.send(wire(kMsgDSetOptions, &duplicate));
  const OptionsList unrelated{kOptionModifierMapForSuper, kKeyModifierIDControl};
  f.send(wire(kMsgDSetOptions, &unrelated));
  f.send(encodeInputSequenceEnvelope(token(7, 2), payload));
  const OptionsList next{kOptionInputSequenceTrace, 8};
  f.send(wire(kMsgDSetOptions, &next));
  f.send(encodeInputSequenceEnvelope(token(8, 1), payload));
  const auto result = receipts(f.stream);
  QCOMPARE(result.size(), std::size_t(3));
  QVERIFY((result[0] == InputSequenceReceipt{7, 1, InputSequenceOutcome::Unsupported, 0}));
  QVERIFY((result[1] == InputSequenceReceipt{7, 2, InputSequenceOutcome::Unsupported, 0}));
  QVERIFY((result[2] == InputSequenceReceipt{8, 1, InputSequenceOutcome::Unsupported, 0}));
  QVERIFY(f.events.addedEvents().empty());

  // Both reset and zero retire the prior generation. Neither can authorize a
  // replay, and a later higher advertisement starts at wire sequence one.
  for (const bool reset : {false, true}) {
    InputFixture withdrawn;
    negotiate(withdrawn);
    const OptionsList zero{kOptionInputSequenceTrace, 0};
    withdrawn.send(reset ? std::string(kMsgCResetOptions) : wire(kMsgDSetOptions, &zero));
    withdrawn.send(wire(kMsgDSetOptions, &next));
    withdrawn.send(encodeInputSequenceEnvelope(token(8, 1), payload));
    QCOMPARE(receipts(withdrawn.stream).size(), std::size_t(1));
    withdrawn.send(reset ? std::string(kMsgCResetOptions) : wire(kMsgDSetOptions, &zero));
    withdrawn.send(wire(kMsgDSetOptions, &next));
    QVERIFY(disconnectRequest(withdrawn.events));
    QCOMPARE(receipts(withdrawn.stream).size(), std::size_t(1));
  }
  {
    InputFixture duplicateOptions;
    negotiate(duplicateOptions);
    const OptionsList invalid{kOptionInputSequenceTrace, 8, kOptionInputSequenceTrace, 8};
    duplicateOptions.send(wire(kMsgDSetOptions, &invalid));
    QVERIFY(disconnectRequest(duplicateOptions.events));
    QVERIFY(duplicateOptions.client.calls.empty());
  }
}

void ServerProxyTests::inputSequence_mixedMotionKeepsExactScopesAndReceipts()
{
  InputFixture f;
  negotiate(f);
  f.client.onInput = [](const auto &) { InputSequenceSubmissionScope::submitted(); };
  f.stream.push(wire(kMsgDMouseMove, 10, 20));
  f.stream.push(wire(kMsgDMouseMove, 30, 40));
  f.stream.push(encodeInputSequenceEnvelope(token(), wire(kMsgDMouseMove, 50, 60)));
  f.stream.push(wire(kMsgDMouseMove, 70, 80));
  f.stream.push(wire(kMsgDMouseRelMove, 2, 3));
  f.stream.push(encodeInputSequenceEnvelope(token(7, 2), wire(kMsgDMouseRelMove, 4, 5)));
  f.stream.push(wire(kMsgDMouseWheel, 0, 120));
  f.dispatch();
  QCOMPARE(f.client.calls.size(), std::size_t(6));
  const auto &calls = f.client.calls;
  QCOMPARE(calls[0].x, 30);
  QVERIFY(!calls[0].token);
  QCOMPARE(calls[1].x, 50);
  QVERIFY(calls[1].token && *calls[1].token == token());
  QCOMPARE(calls[2].x, 70);
  QVERIFY(!calls[2].token);
  QCOMPARE(calls[3].kind, std::string("mouseRelativeMove"));
  QCOMPARE(calls[3].x, 2);
  QVERIFY(!calls[3].token);
  QCOMPARE(calls[4].x, 4);
  QVERIFY(calls[4].token && *calls[4].token == token(7, 2));
  QCOMPARE(calls[5].kind, std::string("mouseWheel"));
  QVERIFY(!calls[5].token);
  const auto result = receipts(f.stream);
  QCOMPARE(result.size(), std::size_t(2));
  QVERIFY((result[0] == InputSequenceReceipt{7, 1, InputSequenceOutcome::Submitted, 1}));
  QVERIFY((result[1] == InputSequenceReceipt{7, 2, InputSequenceOutcome::Submitted, 1}));
  // Receipts use the actual transport and precede its ordinary CNOP response.
  QCOMPARE(f.stream.writes[2], encodeInputSequenceReceipt(result[0]));
  QCOMPARE(f.stream.writes[3], std::string("CNOP"));
  QVERIFY(!InputSequenceSubmissionScope::currentToken());
}

void ServerProxyTests::inputSequence_languageAndModifierSemantics()
{
  InputFixture f;
  f.stream.authenticate();
  f.handshake({kOptionInputSequenceTrace, 7, kOptionModifierMapForSuper, kKeyModifierIDControl});
  f.stream.writes.clear();
  const std::string language = "en";
  f.send(encodeInputSequenceEnvelope(token(), wire(kMsgDKeyDown, kKeySuper_R, KeyModifierSuper, 14, &language)));
  f.send(encodeInputSequenceEnvelope(token(7, 2), wire(kMsgDKeyRepeat, 'b', KeyModifierSuper, 4, 17, &language)));
  f.send(encodeInputSequenceEnvelope(token(7, 3), wire(kMsgDKeyUp, kKeySuper_R, KeyModifierSuper, 14)));
  QCOMPARE(f.client.calls.size(), std::size_t(3));
  QCOMPARE(f.client.calls[0].id, kKeyControl_R);
  QCOMPARE(f.client.calls[0].mask, uint32_t(KeyModifierControl));
  QCOMPARE(f.client.calls[0].language, language);
  QCOMPARE(f.client.calls[1].count, 4);
  QCOMPARE(f.client.calls[1].language, language);
  QCOMPARE(f.client.calls[1].mask, uint32_t(KeyModifierControl));
  QCOMPARE(f.client.calls[2].id, kKeyControl_R);
  QCOMPARE(f.client.calls[2].button, 14U);
  for (std::size_t index = 0; index < 3; ++index)
    QVERIFY(f.client.calls[index].token && *f.client.calls[index].token == token(7, index + 1));
  // The bounded maximum language message is still handled once, unchanged.
  const std::string largestLanguage(inputSequenceMaximumPayload - 14, 'x');
  f.send(encodeInputSequenceEnvelope(token(7, 4), wire(kMsgDKeyDown, 'a', 0, 15, &largestLanguage)));
  QCOMPARE(f.client.calls.size(), std::size_t(4));
  QCOMPARE(f.client.calls.back().language, largestLanguage);
  QCOMPARE(receipts(f.stream).size(), std::size_t(4));
}

void ServerProxyTests::inputSequence_malformedPacketsHaveNoSideEffects_data()
{
  QTest::addColumn<QByteArray>("packet");
  const auto add = [](const char *name, const std::string &frame) {
    QTest::newRow(name) << QByteArray(frame.data(), static_cast<qsizetype>(frame.size()));
  };
  const auto valid = encodeInputSequenceEnvelope(token(), wire(kMsgDMouseDown, 1));
  add("code-only-cannot-consume-next-packet", "DSEQ");
  add("short-header", std::string("DSEQ\0\1", 6));
  add("truncated", valid.substr(0, valid.size() - 1));
  add("trailing-data", valid + "x");
  add("nested-envelope", uncheckedEnvelope(token(), valid));
  add("control-message", uncheckedEnvelope(token(), "COUT"));
  add("two-inputs", uncheckedEnvelope(token(), wire(kMsgDMouseDown, 1) + wire(kMsgDMouseUp, 1)));
  add("old-repeat-without-language", uncheckedEnvelope(token(), wire(kMsgDKeyRepeat1_1, 'a', 0, 1, 5)));
  add("truncated-key", uncheckedEnvelope(token(), wire(kMsgDKeyDown1_1, 'a', 0, 5).substr(0, 9)));
  std::string language = "en";
  auto badLanguage = wire(kMsgDKeyDown, 'a', 0, 5, &language);
  badLanguage[13] = 3;
  add("language-length-mismatch", uncheckedEnvelope(token(), badLanguage));
  add("oversized-payload", uncheckedEnvelope(token(), std::string(inputSequenceMaximumPayload + 1, 'x')));
}

void ServerProxyTests::inputSequence_malformedPacketsHaveNoSideEffects()
{
  QFETCH(QByteArray, packet);
  InputFixture f;
  negotiate(f);
  f.stream.push(packet.toStdString());
  f.stream.push(wire(kMsgDMouseDown, 2));
  f.dispatch();
  QVERIFY(f.client.calls.empty());
  QVERIFY(receipts(f.stream).empty());
  QVERIFY(disconnectRequest(f.events));
  QCOMPARE(f.stream.writes, std::vector<std::string>{"EBAD"});
  // Deferred disconnect cannot let another ready callback execute queued input.
  f.dispatch();
  QVERIFY(f.client.calls.empty());
  QCOMPARE(f.events.addedEvents().size(), std::size_t(1));
}

void ServerProxyTests::inputSequence_noncontiguousOrStaleMetadataDisconnects_data()
{
  QTest::addColumn<quint32>("generation");
  QTest::addColumn<quint64>("sequence");
  QTest::newRow("zero-generation") << quint32(0) << quint64(2);
  QTest::newRow("old-generation") << quint32(6) << quint64(2);
  QTest::newRow("future-generation") << quint32(8) << quint64(2);
  QTest::newRow("zero-sequence") << quint32(7) << quint64(0);
  QTest::newRow("duplicate-sequence") << quint32(7) << quint64(1);
  QTest::newRow("skipped-sequence") << quint32(7) << quint64(3);
}

void ServerProxyTests::inputSequence_noncontiguousOrStaleMetadataDisconnects()
{
  QFETCH(quint32, generation);
  QFETCH(quint64, sequence);
  InputFixture f;
  negotiate(f);
  const auto payload = wire(kMsgDMouseDown, 1);
  f.send(encodeInputSequenceEnvelope(token(), payload));
  f.send(uncheckedEnvelope(token(generation, sequence), payload));
  QCOMPARE(f.client.calls.size(), std::size_t(1));
  QCOMPARE(receipts(f.stream).size(), std::size_t(1));
  QVERIFY(disconnectRequest(f.events));
}

void ServerProxyTests::inputSequence_backendOutcomesAndFailure()
{
  using Outcome = InputSequenceOutcome;
  for (const auto outcome :
       {Outcome::Unsupported, Outcome::Filtered, Outcome::Submitted, Outcome::Expanded, Outcome::Failed}) {
    InputFixture f;
    negotiate(f);
    f.client.onInput = [outcome](const auto &) {
      if (outcome == Outcome::Unsupported)
        return;
      InputSequenceSubmissionScope::backendParticipated();
      if (outcome == Outcome::Filtered)
        return;
      InputSequenceSubmissionScope::submitted();
      if (outcome == Outcome::Expanded)
        InputSequenceSubmissionScope::submitted();
      if (outcome == Outcome::Failed)
        throw std::runtime_error("fixture backend failure after one submission");
    };
    f.send(encodeInputSequenceEnvelope(token(), wire(kMsgDMouseWheel, 0, 120)));
    const auto result = receipts(f.stream);
    QCOMPARE(result.size(), std::size_t(1));
    const uint16_t count = outcome == Outcome::Expanded                                      ? 2
                           : outcome == Outcome::Unsupported || outcome == Outcome::Filtered ? 0
                                                                                             : 1;
    QVERIFY((result[0] == InputSequenceReceipt{7, 1, outcome, count}));
    QCOMPARE(f.client.calls.size(), std::size_t(1));
    QCOMPARE(f.events.addedEvents().empty(), outcome != Outcome::Failed);
    QVERIFY(!InputSequenceSubmissionScope::currentToken());
  }
  InputFixture ignored;
  negotiate(ignored);
  ignored.proxy->onInfoChanged();
  ignored.stream.writes.clear();
  ignored.send(encodeInputSequenceEnvelope(token(), wire(kMsgDMouseMove, 30, 40)));
  QVERIFY(ignored.client.calls.empty());
  const auto result = receipts(ignored.stream);
  QCOMPARE(result.size(), std::size_t(1));
  QVERIFY((result[0] == InputSequenceReceipt{7, 1, Outcome::Filtered, 0}));
  ignored.send(std::string(kMsgCInfoAck));
  ignored.send(encodeInputSequenceEnvelope(token(7, 2), wire(kMsgDMouseMove, 30, 40)));
  QCOMPARE(ignored.client.calls.size(), std::size_t(1));
}

void ServerProxyTests::inputSequence_revocationAndReentryCannotEmitSuccess()
{
  for (const bool replace : {false, true}) {
    InputFixture f;
    negotiate(f);
    if (replace)
      f.stream.authenticate();
    else
      f.stream.revoke();
    f.send(encodeInputSequenceEnvelope(token(), wire(kMsgDMouseDown, 1)));
    QVERIFY(f.client.calls.empty());
    QVERIFY(receipts(f.stream).empty());
    QVERIFY(disconnectRequest(f.events));
  }
  for (const bool reentry : {false, true}) {
    InputFixture f;
    negotiate(f);
    f.client.onInput = [&f, reentry](const auto &) {
      InputSequenceSubmissionScope::submitted();
      if (reentry)
        f.dispatch();
      else
        f.stream.revoke();
    };
    f.stream.push(encodeInputSequenceEnvelope(token(), wire(kMsgDMouseDown, 1)));
    f.stream.push(wire(kMsgDMouseUp, 1));
    f.dispatch();
    QCOMPARE(f.client.calls.size(), std::size_t(1));
    QVERIFY(receipts(f.stream).empty());
    QVERIFY(disconnectRequest(f.events));
    QVERIFY(!InputSequenceSubmissionScope::currentToken());
  }
  {
    InputFixture f;
    negotiate(f);
    f.client.onInput = [&f](const auto &call) {
      if (!call.token)
        f.dispatch();
    };
    f.stream.push(wire(kMsgDMouseMove, 20, 30));
    f.stream.push(encodeInputSequenceEnvelope(token(), wire(kMsgDMouseDown, 1)));
    f.stream.push(wire(kMsgDMouseUp, 1));
    f.dispatch();
    QCOMPARE(f.client.calls.size(), std::size_t(1));
    QCOMPARE(f.client.calls[0].kind, std::string("mouseMove"));
    QVERIFY(!f.client.calls[0].token);
    QVERIFY(receipts(f.stream).empty());
    QVERIFY(disconnectRequest(f.events));
    QVERIFY(!InputSequenceSubmissionScope::currentToken());
  }
}

void ServerProxyTests::inputSequence_realPacketFilterPreservesEnvelopeBoundary()
{
  RecordingEventQueue events;
  deskflow::Computer computer{new InputPlatform(&events), &events};
  RecordingClient client{&events, &computer};
  FakeStream transport;
  transport.authenticate();
  PacketStreamFilter packets(&events, &transport, false);
  ServerProxy1_8 proxy(&client, &packets, &events);
  const auto packet = [](const std::string &payload) {
    std::string bytes;
    inputSequenceDetail::append(bytes, payload.size(), 4);
    bytes += payload;
    return bytes;
  };
  const OptionsList options{kOptionInputSequenceTrace, 7};
  transport.push(packet(wire(kMsgDSetOptions, &options)));
  QVERIFY(events.dispatchEvent(Event(EventTypes::StreamInputReady, transport.getEventTarget())));
  QCOMPARE(client.handshakes, 1U);
  QCOMPARE(transport.writes.size(), std::size_t(2));
  QCOMPARE(transport.writes[1], wire(kMsgCInputSequenceAck, 7));

  client.onInput = [](const auto &) { InputSequenceSubmissionScope::submitted(); };
  transport.writes.clear();
  transport.push(packet(encodeInputSequenceEnvelope(token(), wire(kMsgDMouseMove, 20, 30))));
  transport.push(packet(wire(kMsgDMouseUp, 1)));
  QVERIFY(events.dispatchEvent(Event(EventTypes::StreamInputReady, transport.getEventTarget())));
  QCOMPARE(client.calls.size(), std::size_t(2));
  QVERIFY(client.calls[0].token && *client.calls[0].token == token());
  QVERIFY(!client.calls[1].token);
  const auto result = receipts(transport);
  QCOMPARE(result.size(), std::size_t(1));
  QVERIFY((result[0] == InputSequenceReceipt{7, 1, InputSequenceOutcome::Submitted, 1}));

  client.calls.clear();
  transport.writes.clear();
  const auto next = wire(kMsgDMouseDown, 2);
  transport.push(packet("DSEQ") + packet(next));
  QVERIFY(events.dispatchEvent(Event(EventTypes::StreamInputReady, transport.getEventTarget())));
  QVERIFY(client.calls.empty());
  QVERIFY(receipts(transport).empty());
  QVERIFY(disconnectRequest(events));
  QCOMPARE(packets.getSize(), uint32_t(next.size()));
  std::string untouched(next.size(), '\0');
  QCOMPARE(packets.read(untouched.data(), static_cast<uint32_t>(untouched.size())), uint32_t(next.size()));
  QCOMPARE(untouched, next);
}

void ServerProxyTests::inputSequence_malformedOptionsCannotNegotiate()
{
  const OptionsList duplicate{kOptionInputSequenceTrace, 7, kOptionInputSequenceTrace, 7};
  const OptionsList odd{kOptionInputSequenceTrace};
  const OptionsList valid{kOptionInputSequenceTrace, 7};
  const auto complete = wire(kMsgDSetOptions, &valid);
  auto wrongCount = complete;
  wrongCount[7] = 0;
  for (const auto &options :
       {wire(kMsgDSetOptions, &duplicate), wire(kMsgDSetOptions, &odd), complete.substr(0, complete.size() - 1),
        complete + "CNOP", wrongCount}) {
    InputFixture f;
    f.stream.authenticate();
    f.send(options);
    QCOMPARE(f.client.handshakes, 0U);
    QCOMPARE(f.client.options, 0U);
    QVERIFY(disconnectRequest(f.events));
    QCOMPARE(f.stream.writes, std::vector<std::string>{"EBAD"});
  }
}

void ServerProxyTests::inputSequence_optionsCannotCrossPacketBoundary()
{
  RecordingEventQueue events;
  deskflow::Computer computer{new InputPlatform(&events), &events};
  RecordingClient client{&events, &computer};
  FakeStream transport;
  transport.authenticate();
  PacketStreamFilter packets(&events, &transport, false);
  ServerProxy1_8 proxy(&client, &packets, &events);
  const auto packet = [](const std::string &payload) {
    std::string bytes;
    inputSequenceDetail::append(bytes, payload.size(), 4);
    bytes += payload;
    return bytes;
  };
  const OptionsList options{kOptionInputSequenceTrace, 7};
  const auto complete = wire(kMsgDSetOptions, &options);
  const std::string next = kMsgCNoop;
  // Remove only the advertised generation. The following packet's valid CNOP
  // bytes must never complete that uint32 or activate the negotiation.
  transport.push(packet(complete.substr(0, complete.size() - 4)) + packet(next));
  QVERIFY(events.dispatchEvent(Event(EventTypes::StreamInputReady, transport.getEventTarget())));
  QVERIFY2(
      std::none_of(
          transport.writes.begin(), transport.writes.end(), [](const auto &write) { return write.starts_with("CISA"); }
      ),
      "Truncated DSOP borrowed the following packet and acknowledged its bytes as an ISQ1 generation"
  );
  QCOMPARE(client.handshakes, 0U);
  QCOMPARE(client.options, 0U);
  QVERIFY(client.calls.empty());
  QVERIFY(disconnectRequest(events));
  QCOMPARE(packets.getSize(), uint32_t(next.size()));
  std::string untouched(next.size(), '\0');
  QCOMPARE(packets.read(untouched.data(), static_cast<uint32_t>(untouched.size())), uint32_t(next.size()));
  QCOMPARE(untouched, next);
}
void ServerProxyTests::inputCutoff_flushesMotionDelegatesAndResumes()
{
  InputFixture f;
  negotiateCutoff(f);
  const auto binding = cutoffBinding();
  bool flushedBeforeBackend = false;
  f.platform->onCutoffBegin = [&] {
    flushedBeforeBackend = f.client.calls.size() == 1 && f.client.calls[0].kind == "mouseMove" &&
                           f.client.calls[0].x == 30 && f.client.calls[0].y == 40;
  };
  f.stream.push(wire(kMsgDMouseMove, 10, 20));
  f.stream.push(wire(kMsgDMouseMove, 30, 40));
  f.stream.push(encodeInputCutoffRequest({binding, 250}));
  f.dispatch();
  QVERIFY(flushedBeforeBackend);
  QCOMPARE(f.platform->cutoffBegins, 1u);
  QVERIFY(f.platform->cutoffBinding == binding);
  QVERIFY(cutoffReceipts(f.stream).empty());
  f.platform->cutoffProbe.pong(binding.request, InputCutoffClock::now());
  pollCutoff(f);
  QVERIFY(cutoffReceipts(f.stream).empty()); // Entire EI event batch still pending.
  f.platform->cutoffProbe.finishEventBatch(InputCutoffClock::now());
  pollCutoff(f);
  QCOMPARE(cutoffReceipts(f.stream), (std::vector<InputCutoffReceipt>{{binding, InputCutoffState::BackendObserved}}));
  pollCutoff(f);
  QCOMPARE(cutoffReceipts(f.stream).size(), 1u);
  f.send(encodeInputCutoffResume(binding));
  QCOMPARE(f.platform->cutoffResumes, 1u);
  QVERIFY(!f.platform->cutoffBinding);
  QCOMPARE(cutoffReceipts(f.stream).back(), (InputCutoffReceipt{binding, InputCutoffState::Resumed}));
  f.send(encodeInputCutoffResume(binding)); // Exact cleanup is idempotent.
  QCOMPARE(f.platform->cutoffResumes, 1u);
  f.send(wire(kMsgDKeyUp, 'a', 0, 7));
  QCOMPARE(f.client.calls.back().kind, std::string("keyUp"));
  QVERIFY(!disconnectRequest(f.events));
  f.send(encodeInputCutoffRequest({binding, 250}));
  QVERIFY(disconnectRequest(f.events)); // Even a resumed request ID cannot be reused.
  QCOMPARE(f.platform->cutoffBegins, 1u);
}

void ServerProxyTests::inputCutoff_requiresNegotiationAndExactBoundedPackets()
{
  for (bool latest : {false, true}) {
    InputFixture f(latest);
    f.handshake({kOptionInputCutoff, 9}); // No authenticated connection.
    QVERIFY(std::none_of(f.stream.writes.begin(), f.stream.writes.end(), [](const auto &bytes) {
      return bytes.starts_with("CICA");
    }));
    f.send(encodeInputCutoffRequest({cutoffBinding(), 250}));
    QCOMPARE(f.platform->cutoffBegins, 0u);
    QVERIFY(disconnectRequest(f.events));
  }
  const auto request = encodeInputCutoffRequest({cutoffBinding(), 250});
  for (const auto &bad : {request.substr(0, 4), request.substr(0, request.size() - 1), request + '\0'}) {
    InputFixture f;
    negotiateCutoff(f);
    f.stream.push(bad);
    f.stream.push(wire(kMsgDMouseDown, 1));
    f.dispatch();
    QCOMPARE(f.platform->cutoffBegins, 0u);
    QVERIFY(f.client.calls.empty());
    QVERIFY(disconnectRequest(f.events));
  }
  InputFixture unsupported;
  negotiateCutoff(unsupported);
  unsupported.platform->cutoffSupported = false;
  unsupported.send(request);
  QCOMPARE(
      cutoffReceipts(unsupported.stream), (std::vector<InputCutoffReceipt>{{cutoffBinding(), InputCutoffState::Denied}})
  );
  unsupported.send(encodeInputCutoffResume(cutoffBinding()));
  QCOMPARE(unsupported.platform->cutoffResumes, 0u);
  QVERIFY(!disconnectRequest(unsupported.events));
}

void ServerProxyTests::inputCutoff_freezeRejectsLaterInputAndWrongResume()
{
  const std::string language = "en";
  const OptionsList options{kOptionHeartbeat, 2000};
  auto wrong = cutoffBinding();
  ++wrong.forwarding.focusRevision;
  const std::vector packets{wire(kMsgDMouseMove, 10, 20),  wire(kMsgDMouseRelMove, 1, 2),
                            wire(kMsgDMouseWheel, 3, 4),   wire(kMsgDMouseDown, 1),
                            wire(kMsgDMouseUp, 1),         wire(kMsgDKeyDown, 'a', 0, 7, &language),
                            wire(kMsgDKeyUp, 'a', 0, 7),   wire(kMsgDKeyRepeat, 'a', 0, 2, 7, &language),
                            wire(kMsgCEnter, 1, 2, 3, 0),  wire(kMsgCLeave),
                            wire(kMsgCResetOptions),       wire(kMsgDSetOptions, &options),
                            encodeInputCutoffResume(wrong)};
  for (const auto &packet : packets) {
    InputFixture f;
    negotiateCutoff(f);
    f.send(encodeInputCutoffRequest({cutoffBinding(), 250}));
    f.send(packet);
    QVERIFY(f.client.calls.empty());
    QVERIFY(cutoffReceipts(f.stream).empty());
    QCOMPARE(f.platform->cutoffResumes, 1u);
    QVERIFY(disconnectRequest(f.events));
  }
}

void ServerProxyTests::inputCutoff_deadlineInvalidationAndDestruction()
{
  {
    InputFixture f;
    negotiateCutoff(f);
    const auto binding = cutoffBinding();
    f.send(encodeInputCutoffRequest({binding, 10}));
    QTest::qSleep(12);
    pollCutoff(f);
    QCOMPARE(cutoffReceipts(f.stream).back(), (InputCutoffReceipt{binding, InputCutoffState::Denied}));
    f.platform->cutoffProbe.pong(binding.request, InputCutoffClock::now());
    f.platform->cutoffProbe.finishEventBatch(InputCutoffClock::now());
    pollCutoff(f);
    QCOMPARE(cutoffReceipts(f.stream).size(), 1u);
    f.send(encodeInputCutoffResume(binding));
    QCOMPARE(f.platform->cutoffResumes, 1u);
  }
  for (bool disconnect : {false, true}) {
    InputFixture f;
    negotiateCutoff(f);
    const auto binding = cutoffBinding();
    f.send(encodeInputCutoffRequest({binding, 250}));
    f.platform->cutoffProbe.pong(binding.request, InputCutoffClock::now());
    f.platform->cutoffProbe.finishEventBatch(InputCutoffClock::now());
    pollCutoff(f);
    if (disconnect)
      f.stream.revoke();
    else
      f.platform->cutoffProbe.invalidate();
    pollCutoff(f);
    if (disconnect) {
      QVERIFY(disconnectRequest(f.events));
      QCOMPARE(f.platform->cutoffResumes, 1u);
    } else {
      QCOMPARE(cutoffReceipts(f.stream).back(), (InputCutoffReceipt{binding, InputCutoffState::Denied}));
      f.send(encodeInputCutoffResume(binding));
    }
  }
  {
    InputFixture f;
    negotiateCutoff(f);
    f.platform->onCutoffResume = [&] {
      f.stream.push(wire(kMsgDMouseDown, 1));
      f.dispatch();
    };
    f.platform->onCutoffBegin = [&] { f.proxy.reset(); };
    f.send(encodeInputCutoffRequest({cutoffBinding(), 250}));
    QVERIFY(!f.proxy);
    QVERIFY(f.client.calls.empty());
    QVERIFY(cutoffReceipts(f.stream).empty());
    QCOMPARE(f.platform->cutoffResumes, 1u);
    QVERIFY(!f.platform->cutoffBinding);
  }
  for (bool throwsAfterDestroy : {false, true}) {
    InputFixture f;
    negotiateCutoff(f);
    f.platform->onCutoffBegin = [&] { f.stream.revoke(); };
    f.platform->onCutoffResume = [&] {
      f.proxy.reset();
      if (throwsAfterDestroy)
        throw BadClientException("Backend destroyed proxy during cleanup");
    };
    f.send(encodeInputCutoffRequest({cutoffBinding(), 250}));
    QVERIFY(!f.proxy);
    QVERIFY(f.client.calls.empty());
    QVERIFY(f.stream.writes.empty());
    QCOMPARE(f.platform->cutoffResumes, 1u);
    QVERIFY(!f.platform->cutoffBinding);
  }
}
void ServerProxyTests::mappedCutoff_delegatesExactPointAndWaitsForDrain()
{
  InputFixture f;
  negotiateCutoff(f, true);
  const auto binding = cutoffBinding();
  const InputCutoffPoint point{-321, 456};
  bool flushed = false;
  f.platform->onCutoffBegin = [&] {
    flushed = f.client.calls.size() == 1 && f.client.calls.front().x == 30 && f.client.calls.front().y == 40;
  };
  const auto before = InputCutoffClock::now();
  f.stream.push(wire(kMsgDMouseMove, 30, 40));
  f.stream.push(encodeInputCutoffMappedRequest({{binding, 200}, point}));
  f.dispatch();
  QVERIFY(flushed);
  QCOMPARE(f.platform->cutoffPoint, std::optional(point));
  const auto deadline = f.platform->cutoffDeadline;
  QVERIFY(deadline > before && deadline <= InputCutoffClock::now() + std::chrono::milliseconds(200));
  f.platform->cutoffProbe.pong(binding.request, InputCutoffClock::now());
  pollCutoff(f);
  f.platform->cutoffProbe.finishEventBatch(InputCutoffClock::now());
  pollCutoff(f);
  QVERIFY(cutoffReceipts(f.stream).empty()); // No mapped receipt from PONG or batch completion alone.
  auto result = mappedCutoffResult(binding, point);
  f.platform->mappedResult = result;
  pollCutoff(f);
  const auto count = std::count(f.stream.writes.begin(), f.stream.writes.end(), encodeInputCutoffMappedResult(result));
  QCOMPARE(count, 1);
  pollCutoff(f);
  QCOMPARE(std::count(f.stream.writes.begin(), f.stream.writes.end(), encodeInputCutoffMappedResult(result)), 1);
  QCOMPARE(f.platform->cutoffDeadline, deadline); // Progress cannot renew the original deadline.
  f.platform->mappedResult.reset();               // Exact native/provider/Qt receipt invalidated after publication.
  pollCutoff(f);
  QCOMPARE(cutoffReceipts(f.stream).back(), (InputCutoffReceipt{binding, InputCutoffState::Denied}));
  f.send(encodeInputCutoffResume(binding));
  QCOMPARE(f.platform->cutoffResumes, 1u);
  f.send(wire(kMsgDKeyUp, 'a', 0, 7));
  QCOMPARE(f.client.calls.back().kind, std::string("keyUp"));
  f.platform->mappedResult = result; // A late provider result has no retained transaction to publish into.
  const auto writes = f.stream.writes.size();
  f.send(wire(kMsgCNoop));
  QVERIFY(!disconnectRequest(f.events));
  QCOMPARE(std::count(f.stream.writes.begin(), f.stream.writes.end(), encodeInputCutoffMappedResult(result)), 1);
  QVERIFY(f.stream.writes.size() >= writes);
}

void ServerProxyTests::mappedCutoff_rejectsMissingProofAndStaleCompletion()
{
  for (int invalid = 0; invalid < 5; ++invalid) {
    InputFixture f;
    negotiateCutoff(f, invalid != 0);
    const auto binding = cutoffBinding();
    const InputCutoffPoint point{-10, 27};
    f.send(encodeInputCutoffMappedRequest({{binding, 250}, point}));
    if (invalid == 0) {
      QCOMPARE(f.platform->cutoffBegins, 0u);
      QVERIFY(disconnectRequest(f.events));
      continue;
    }
    f.platform->cutoffProbe.pong(binding.request, InputCutoffClock::now());
    f.platform->cutoffProbe.finishEventBatch(InputCutoffClock::now());
    auto result = mappedCutoffResult(binding, point);
    if (invalid == 1)
      result.orderingContract.clear();
    if (invalid == 2)
      ++result.point.x;
    if (invalid == 3)
      ++result.binding.capture.part;
    if (invalid == 4)
      f.platform->onMappedQuery = [&] { f.proxy.reset(); };
    f.platform->mappedResult = result;
    pollCutoff(f);
    QVERIFY(std::none_of(f.stream.writes.begin(), f.stream.writes.end(), [](const auto &bytes) {
      return bytes.starts_with("CIMR");
    }));
    if (invalid == 4) {
      QVERIFY(!f.proxy);
      QCOMPARE(f.platform->cutoffResumes, 1u);
      QVERIFY(!f.platform->cutoffBinding);
    } else {
      QCOMPARE(cutoffReceipts(f.stream).back(), (InputCutoffReceipt{binding, InputCutoffState::Denied}));
      f.send(encodeInputCutoffResume(binding));
      QCOMPARE(f.platform->cutoffResumes, 1u);
    }
  }
}
void ServerProxyTests::sealedCutoff_refreshesWithoutReopeningLegacy()
{
  InputFixture f;
  negotiateCutoff(f, true, true);
  const auto origin = cutoffBinding();
  f.stream.push(wire(kMsgDMouseMove, 30, 40));
  f.stream.push(encodeInputCutoffSealedRequest({{origin, 250}, {30, 40}}));
  f.dispatch();
  QCOMPARE(f.client.calls.size(), 1u); // Initial compressed prefix is submitted before its ping.
  QCOMPARE(f.platform->cutoffBegins, 1u);
  QVERIFY(!f.platform->sealValid);
  f.platform->cutoffProbe.pong(origin.request, InputCutoffClock::now());
  f.platform->cutoffProbe.finishEventBatch(InputCutoffClock::now());
  pollCutoff(f);
  QVERIFY(std::none_of(f.stream.writes.begin(), f.stream.writes.end(), [](const auto &v) {
    return v.starts_with("CIMS");
  }));
  f.platform->mappedResult = mappedCutoffResult(origin, {30, 40});
  pollCutoff(f);
  QVERIFY(f.platform->sealValid);
  QCOMPARE(
      std::count(
          f.stream.writes.begin(), f.stream.writes.end(),
          encodeInputCutoffSealedResult({*f.platform->mappedResult, origin.request})
      ),
      1
  );
  // The prior point can expire while the exact prefix stays closed. No new
  // point or permission is published merely because the seal is retained.
  f.platform->cutoffDeadline = InputCutoffClock::now();
  pollCutoff(f);
  QVERIFY(cutoffReceipts(f.stream).empty());
  auto latest = origin;
  for (std::uint64_t index = 0; index != 32; ++index) {
    auto next = latest;
    ++next.request;
    ++next.capture.sequence;
    const InputCutoffPoint point{int(40 + index), int(50 + index)};
    const auto before = InputCutoffClock::now();
    f.send(encodeInputCutoffMapRequest({{{next, 200}, point}, origin.request, index % 2 == 0}));
    QVERIFY(!disconnectRequest(f.events));
    QCOMPARE(f.platform->cutoffBinding, std::optional(next));
    QCOMPARE(f.platform->cutoffPoint, std::optional(point));
    QVERIFY(
        f.platform->cutoffDeadline > before &&
        f.platform->cutoffDeadline <= InputCutoffClock::now() + std::chrono::milliseconds(200)
    );
    QCOMPARE(f.platform->cutoffBegins, 1u);
    QCOMPARE(f.platform->cutoffResumes, 0u);
    f.platform->mappedResult = mappedCutoffResult(next, point);
    pollCutoff(f);
    QCOMPARE(
        std::count(
            f.stream.writes.begin(), f.stream.writes.end(),
            encodeInputCutoffSealedResult({*f.platform->mappedResult, origin.request})
        ),
        1
    );
    latest = next;
  }
  QCOMPARE(f.platform->mapRequests, 32u);
  QCOMPARE(f.platform->presented.size(), 16u);
  QCOMPARE(f.client.calls.size(), 1u); // Explicit presentation did not enter an ordinary key/button/wheel path.
  f.send(encodeInputCutoffResume(origin));
  QCOMPARE(f.platform->cutoffResumes, 1u);
  QVERIFY(!f.platform->sealOrigin);
  QCOMPARE(cutoffReceipts(f.stream).back(), (InputCutoffReceipt{origin, InputCutoffState::Resumed}));
  f.send(wire(kMsgDKeyUp, 'a', 0, 7));
  QCOMPARE(f.client.calls.back().kind, std::string("keyUp"));
}

void ServerProxyTests::sealedCutoff_rejectsStaleOverlappingAndInvalidatedMaps()
{
  for (int invalid = 0; invalid != 9; ++invalid) {
    InputFixture f;
    negotiateCutoff(f, true, true);
    const auto origin = cutoffBinding();
    f.send(encodeInputCutoffSealedRequest({{origin, 250}, {30, 40}}));
    f.platform->cutoffProbe.pong(origin.request, InputCutoffClock::now());
    f.platform->cutoffProbe.finishEventBatch(InputCutoffClock::now());
    f.platform->mappedResult = mappedCutoffResult(origin, {30, 40});
    pollCutoff(f);
    auto next = origin;
    ++next.request;
    ++next.capture.sequence;
    auto sealRequest = origin.request;
    if (invalid == 0)
      next.capture = origin.capture;
    if (invalid == 1)
      ++next.forwarding.geometry;
    if (invalid == 2)
      ++next.viewerConnection;
    if (invalid == 3)
      ++next.coreConnectionGeneration;
    if (invalid == 4) {
      ++next.request;
      ++sealRequest;
    }
    if (invalid == 5)
      f.platform->sealFault = true;
    if (invalid == 7)
      f.platform->onMapBegin = [&] { f.proxy.reset(); };
    if (invalid == 8)
      f.platform->onMapBegin = [&] { f.stream.revoke(); };
    f.stream.writes.clear();
    f.send(encodeInputCutoffMapRequest({{{next, 200}, {50, 60}}, sealRequest, true}));
    if (invalid == 6) { // A second request cannot replace an unresolved point.
      ++next.request;
      ++next.capture.sequence;
      f.send(encodeInputCutoffMapRequest({{{next, 200}, {70, 80}}, sealRequest, true}));
    }
    QVERIFY(std::none_of(f.stream.writes.begin(), f.stream.writes.end(), [](const auto &v) {
      return v.starts_with("CIMS");
    }));
    if (invalid == 5) {
      QCOMPARE(cutoffReceipts(f.stream).back(), (InputCutoffReceipt{next, InputCutoffState::Denied}));
      f.send(encodeInputCutoffResume(origin));
    } else if (invalid != 7)
      QVERIFY(disconnectRequest(f.events));
    else
      QVERIFY(!f.proxy);
    QVERIFY(!f.platform->sealOrigin);
    QCOMPARE(f.platform->cutoffResumes, 1u);
  }
  InputFixture legacy;
  negotiateCutoff(legacy, true);
  legacy.send(encodeInputCutoffSealedRequest({{cutoffBinding(), 200}, {30, 40}}));
  QCOMPARE(legacy.platform->cutoffBegins, 0u);
  QVERIFY(disconnectRequest(legacy.events));
}
void ServerProxyTests::batchedCutoff_delegatesAtomicallyWithoutNewPrefix()
{
  InputFixture f;
  negotiateCutoff(f, true, true, true);
  const auto origin = cutoffBinding();
  f.stream.push(wire(kMsgDMouseMove, 30, 40));
  f.stream.push(encodeInputCutoffSealedRequest({{origin, 250}, {30, 40}}));
  f.dispatch();
  f.platform->cutoffProbe.pong(origin.request, InputCutoffClock::now());
  f.platform->cutoffProbe.finishEventBatch(InputCutoffClock::now());
  f.platform->mappedResult = mappedCutoffResult(origin, {30, 40});
  pollCutoff(f);
  QVERIFY(f.platform->sealValid);
  auto latest = origin;
  for (unsigned group = 0; group < 4; ++group) {
    InputCutoffBatchRequest request{origin.request};
    for (unsigned index = 0; index < inputCutoffMaximumBatch; ++index) {
      ++latest.request;
      ++latest.capture.sequence;
      latest.capture.part = 1 + index % 3;
      request.items.push_back(
          {{{latest, static_cast<std::uint16_t>(200 + index)}, {int(index * 3), int(index * 7)}},
           origin.request,
           index % 2 == 0}
      );
    }
    f.stream.writes.clear();
    const auto before = InputCutoffClock::now();
    f.send(encodeInputCutoffBatchRequest(request));
    QVERIFY(!disconnectRequest(f.events));
    QCOMPARE(f.platform->batchItems.size(), inputCutoffMaximumBatch);
    QCOMPARE(f.platform->cutoffBegins, 1u);
    QCOMPARE(f.platform->cutoffResumes, 0u);
    for (std::size_t index = 0; index < inputCutoffMaximumBatch; ++index) {
      const auto &item = f.platform->batchItems[index];
      QCOMPARE(item.binding, request.items[index].mapping.cutoff.binding);
      QCOMPARE(item.point, request.items[index].mapping.point);
      QVERIFY(
          item.deadline > before && item.deadline <= InputCutoffClock::now() + std::chrono::milliseconds(200 + index)
      );
      f.platform->batchResults.push_back(mappedCutoffResult(item.binding, item.point));
      if (index + 1 < inputCutoffMaximumBatch) {
        pollCutoff(f);
        QVERIFY(std::none_of(f.stream.writes.begin(), f.stream.writes.end(), [](const auto &v) {
          return v.starts_with("CIMB");
        }));
      }
    }
    pollCutoff(f);
    const auto expected = encodeInputCutoffBatchResult({origin.request, f.platform->batchResults});
    QCOMPARE(std::count(f.stream.writes.begin(), f.stream.writes.end(), expected), 1);
    pollCutoff(f);
    QCOMPARE(std::count(f.stream.writes.begin(), f.stream.writes.end(), expected), 1);
  }
  QCOMPARE(f.platform->batchRequests, 4u);
  QCOMPARE(f.platform->presented.size(), 32u);
  QCOMPARE(f.client.calls.size(), 1u);
  f.send(encodeInputCutoffResume(origin));
  QCOMPARE(f.platform->cutoffResumes, 1u);
  f.send(wire(kMsgDKeyUp, 'a', 0, 7));
  QCOMPARE(f.client.calls.back().kind, std::string("keyUp"));
}

void ServerProxyTests::batchedCutoff_rejectsOverlapLossAndSubstitution()
{
  for (int invalid = 0; invalid < 10; ++invalid) {
    InputFixture f;
    negotiateCutoff(f, true, true, invalid != 0);
    const auto origin = cutoffBinding();
    f.send(encodeInputCutoffSealedRequest({{origin, 250}, {30, 40}}));
    f.platform->cutoffProbe.pong(origin.request, InputCutoffClock::now());
    f.platform->cutoffProbe.finishEventBatch(InputCutoffClock::now());
    f.platform->mappedResult = mappedCutoffResult(origin, {30, 40});
    pollCutoff(f);
    InputCutoffBatchRequest request{origin.request};
    auto next = origin;
    for (unsigned index = 0; index < 2; ++index) {
      ++next.request;
      ++next.capture.sequence;
      request.items.push_back({{{next, 200}, {int(50 + index), 60}}, origin.request, true});
    }
    if (invalid == 1)
      for (auto &item : request.items)
        ++item.mapping.cutoff.binding.forwarding.geometry;
    if (invalid == 2)
      for (auto &item : request.items)
        --item.mapping.cutoff.binding.capture.sequence;
    if (invalid == 3)
      f.platform->sealFault = true;
    if (invalid == 4)
      f.platform->onMapBegin = [&] { f.proxy.reset(); };
    if (invalid == 5)
      f.platform->onMapBegin = [&] { f.stream.revoke(); };
    f.stream.writes.clear();
    f.send(encodeInputCutoffBatchRequest(request));
    if (invalid == 6)
      f.send(encodeInputCutoffBatchRequest(request));
    if (invalid >= 7) {
      for (const auto &item : f.platform->batchItems)
        f.platform->batchResults.push_back(mappedCutoffResult(item.binding, item.point));
      if (invalid == 7)
        ++f.platform->batchResults.back().point.x;
      if (invalid == 8)
        ++f.platform->batchResults.back().viewportRevision;
      if (invalid == 9)
        f.platform->onMappedQuery = [&] { f.proxy.reset(); };
      pollCutoff(f);
    }
    QVERIFY(std::none_of(f.stream.writes.begin(), f.stream.writes.end(), [](const auto &v) {
      return v.starts_with("CIMB");
    }));
    if (invalid == 3 || invalid == 7 || invalid == 8) {
      QCOMPARE(cutoffReceipts(f.stream).back().state, InputCutoffState::Denied);
      f.send(encodeInputCutoffResume(origin));
    } else if (invalid != 4 && invalid != 9)
      QVERIFY(disconnectRequest(f.events));
    else
      QVERIFY(!f.proxy);
    QVERIFY(!f.platform->sealOrigin);
    QCOMPARE(f.platform->cutoffResumes, 1u);
  }
}

void ServerProxyTests::preparation_ordersFocusPromotionAndLinkedCleanup()
{
  InputFixture f;
  f.stream.authenticate();
  f.handshake({kOptionBatchedInputCutoff, 9, kOptionInputPreparation, 9});
  QVERIFY(std::any_of(f.stream.writes.begin(), f.stream.writes.end(), [](const auto &v) {
    return v.starts_with("CIPA");
  }));
  f.stream.writes.clear();
  f.platform->preparationSupported = true;
  const InputPreparationBinding preparation{1, 9, {31, 400, 1}};
  const InputCutoffPoint point{30, 40};
  f.platform->onPreparationBegin = [&] {
    QCOMPARE(f.client.calls.size(), 1u);
    QCOMPARE(f.client.calls.front().kind, std::string("mouseMove"));
  };
  f.stream.push(wire(kMsgDMouseMove, 30, 40));
  f.stream.push(encodeInputPreparationRequest({preparation, point, 450}));
  f.dispatch();
  QCOMPARE(f.platform->preparationBegins, 1u);
  QCOMPARE(f.platform->preparationBinding, std::optional(preparation));
  QVERIFY(f.platform->preparationDeadline > InputCutoffClock::now() + std::chrono::milliseconds(250));
  QVERIFY(f.stream.writes.empty() || f.stream.writes.back() == "CNOP");
  PreparedInputFocusResult focus{preparation, point, 17, {11, 12}, 16, 14, 15};
  focus.viewerPeerFingerprint.fill(0x7a);
  f.platform->prepared = focus;
  f.platform->preparationState = InputPreparationState::Prepared;
  pollCutoff(f);
  QCOMPARE(std::count(f.stream.writes.begin(), f.stream.writes.end(), encodePreparedInputFocusResult(focus)), 1);
  pollCutoff(f);
  QCOMPARE(std::count(f.stream.writes.begin(), f.stream.writes.end(), encodePreparedInputFocusResult(focus)), 1);
  const auto promoted = cutoffBinding(2);
  f.send(encodeInputPreparationPromotion({preparation, promoted, 200}));
  QCOMPARE(f.platform->preparationPromotions, 1u);
  QCOMPARE(f.platform->cutoffBegins, 0u);
  QVERIFY(std::none_of(f.stream.writes.begin(), f.stream.writes.end(), [](const auto &v) {
    return v.starts_with("CIMS");
  }));
  f.platform->cutoffProbe.pong(promoted.request, InputCutoffClock::now());
  f.platform->cutoffProbe.finishEventBatch(InputCutoffClock::now());
  f.platform->mappedResult = mappedCutoffResult(promoted, point);
  pollCutoff(f);
  QCOMPARE(
      std::count(
          f.stream.writes.begin(), f.stream.writes.end(),
          encodeInputCutoffSealedResult({*f.platform->mappedResult, promoted.request})
      ),
      1
  );
  f.stream.writes.clear();
  f.send(encodeInputCutoffResume(promoted));
  QCOMPARE(f.platform->preparationCancels, 1u);
  QCOMPARE(f.platform->cutoffResumes, 0u);
  QVERIFY(cutoffReceipts(f.stream).empty());
  const auto cleanupDeadline = f.platform->preparationCleanupDeadline;
  f.send(encodeInputCutoffResume(promoted));
  QCOMPARE(f.platform->preparationCancels, 1u);
  QCOMPARE(f.platform->preparationCleanupDeadline, cleanupDeadline);
  QVERIFY(cutoffReceipts(f.stream).empty());
  f.platform->finishPreparationCleanup();
  pollCutoff(f);
  QCOMPARE(cutoffReceipts(f.stream).back(), (InputCutoffReceipt{promoted, InputCutoffState::Resumed}));
  QCOMPARE(f.platform->cutoffResumes, 0u);
  f.send(wire(kMsgDKeyUp, 'a', 0, 7));
  QCOMPARE(f.client.calls.back().kind, std::string("keyUp"));
  QVERIFY(!disconnectRequest(f.events));
}

void ServerProxyTests::preparation_rejectsSubstitutionAndNeverInfersRelease()
{
  for (int test = 0; test < 11; ++test) {
    InputFixture f;
    f.stream.authenticate();
    f.handshake(
        test == 0 ? OptionsList{kOptionBatchedInputCutoff, 9}
                  : OptionsList{kOptionBatchedInputCutoff, 9, kOptionInputPreparation, 9}
    );
    f.stream.writes.clear();
    f.platform->preparationSupported = test != 1;
    const InputPreparationBinding binding{1, 9, {31, 400, 1}};
    if (test == 7)
      f.platform->onPreparationBegin = [&] { f.proxy.reset(); };
    if (test == 8)
      f.platform->onPreparationObserve = [&] { f.proxy.reset(); };
    f.send(encodeInputPreparationRequest({binding, {30, 40}, 450}));
    if (test == 0) {
      QCOMPARE(f.platform->preparationBegins, 0u);
      QVERIFY(disconnectRequest(f.events));
      continue;
    }
    if (test == 1) {
      QVERIFY(
          std::find(
              f.stream.writes.begin(), f.stream.writes.end(),
              encodeInputPreparationReceipt(
                  {binding, InputPreparationState::Released, InputPreparationOutcome::Unsupported}
              )
          ) != f.stream.writes.end()
      );
      QCOMPARE(f.platform->preparationCancels, 0u);
      continue;
    }
    if (test == 7 || test == 8) {
      QVERIFY(!f.proxy);
      QVERIFY(f.platform->preparationAborts >= 1);
      continue;
    }
    if (test == 2) {
      f.platform->preparationState = InputPreparationState::Failed;
      f.platform->preparationOutcome = InputPreparationOutcome::NoTarget;
      pollCutoff(f);
      QCOMPARE(f.platform->preparationCancels, 1u);
      QVERIFY(
          std::find(
              f.stream.writes.begin(), f.stream.writes.end(),
              encodeInputPreparationReceipt({binding, InputPreparationState::Failed, InputPreparationOutcome::NoTarget})
          ) != f.stream.writes.end()
      );
    } else if (test == 3) {
      auto wrong = binding;
      ++wrong.capture.part;
      f.send(encodeInputPreparationCancellation({wrong, 200}));
      QVERIFY(disconnectRequest(f.events));
      QCOMPARE(f.platform->preparationAborts, 1u);
      continue;
    } else if (test == 4) {
      f.send(wire(kMsgDMouseDown, 1));
      QVERIFY(disconnectRequest(f.events));
      QCOMPARE(f.platform->preparationAborts, 1u);
      continue;
    } else if (test == 5) {
      f.stream.revoke();
      pollCutoff(f);
      QVERIFY(disconnectRequest(f.events));
      QVERIFY(f.platform->preparationAborts >= 1);
      continue;
    } else if (test == 6) {
      PreparedInputFocusResult focus{binding, {31, 40}, 17, {11, 12}, 16, 14, 15};
      focus.viewerPeerFingerprint.fill(0x7a);
      f.platform->prepared = focus;
      f.platform->preparationState = InputPreparationState::Prepared;
      pollCutoff(f);
      QCOMPARE(f.platform->preparationCancels, 1u);
    } else {
      if (test == 9)
        f.platform->onPreparationCancel = [&] { f.proxy.reset(); };
      f.send(encodeInputPreparationCancellation({binding, 200}));
      if (test == 9) {
        QVERIFY(!f.proxy);
        QVERIFY(f.platform->preparationAborts >= 1);
        continue;
      }
      f.platform->preparationState = InputPreparationState::Failed;
      pollCutoff(f);
      QVERIFY(disconnectRequest(f.events));
      QVERIFY(f.platform->preparationAborts >= 1);
      continue;
    }
    QVERIFY(std::none_of(f.stream.writes.begin(), f.stream.writes.end(), [](const auto &v) {
      if (!v.starts_with("CIPR"))
        return false;
      const auto receipt = decodeInputPreparationReceipt(std::string_view(v).substr(4));
      return receipt && receipt->state == InputPreparationState::Released;
    }));
    f.platform->finishPreparationCleanup();
    pollCutoff(f);
    const auto expected = test == 2 ? InputPreparationOutcome::NoTarget : InputPreparationOutcome::Failed;
    QVERIFY(
        std::find(
            f.stream.writes.begin(), f.stream.writes.end(),
            encodeInputPreparationReceipt({binding, InputPreparationState::Released, expected})
        ) != f.stream.writes.end()
    );
  }
}

#endif

QTEST_MAIN(ServerProxyTests)
