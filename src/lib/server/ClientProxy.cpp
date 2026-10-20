/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy.h"

#include "base/Log.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/OptionTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"
#include "windowshare/LifecycleDiagnostics.h"

#include <algorithm>
#include <atomic>
#include <limits>

using namespace deskflow::windowshare;

namespace {
void increment(std::uint64_t &counter) noexcept
{
  if (counter != std::numeric_limits<std::uint64_t>::max())
    ++counter;
}
template <class T> T nextCutoffIdentity()
{
  static std::atomic<T> next{1};
  auto value = next.load(std::memory_order_relaxed);
  while (value) {
    const auto successor = value == std::numeric_limits<T>::max() ? 0 : value + 1;
    if (next.compare_exchange_weak(value, successor, std::memory_order_relaxed))
      return value;
  }
  return 0;
}
} // namespace

// ProtocolUtil performs exactly one write for each complete legacy message.
// Intercept that write, leaving every version's existing encoder unchanged.
// Control, enter, clipboard and heartbeat messages use the original stream.
class ClientProxy::InputStream final : public deskflow::IStream
{
public:
  explicit InputStream(ClientProxy &owner, bool input = true) : m_owner(owner), m_input(input)
  {
  }
  void write(const void *buffer, std::uint32_t size) override
  {
    if (m_input)
      m_owner.writeInputPacket(buffer, size);
    else
      m_owner.writeControlPacket(buffer, size);
  }
  void close() override
  {
    m_owner.m_stream->close();
  }
  std::uint32_t read(void *buffer, std::uint32_t size) override
  {
    return m_owner.m_stream->read(buffer, size);
  }
  void flush() override
  {
    m_owner.m_stream->flush();
  }
  void shutdownInput() override
  {
    m_owner.m_stream->shutdownInput();
  }
  void shutdownOutput() override
  {
    m_owner.m_stream->shutdownOutput();
  }
  void *getEventTarget() const override
  {
    return m_owner.m_stream->getEventTarget();
  }
  bool isReady() const override
  {
    return m_owner.m_stream->isReady();
  }
  std::uint32_t getSize() const override
  {
    return m_owner.m_stream->getSize();
  }
  std::optional<PeerFingerprint> authenticatedPeerFingerprint() const override
  {
    return m_owner.m_stream->authenticatedPeerFingerprint();
  }
  std::optional<deskflow::AuthenticatedPeerTicket> authenticatedPeerTicket() const override
  {
    return m_owner.m_stream->authenticatedPeerTicket();
  }

private:
  ClientProxy &m_owner;
  bool m_input;
};

//
// ClientProxy
//

ClientProxy::ClientProxy(const std::string &name, deskflow::IStream *stream)
    : BaseClientProxy(name),
      m_stream(stream),
      m_inputStream(std::make_unique<InputStream>(*this)),
      m_controlStream(std::make_unique<InputStream>(*this, false))
{
  // do nothing
}

ClientProxy::~ClientProxy()
{
  *m_alive = false;
  stopInputSequenceTrace();
  delete m_stream;
}

void ClientProxy::close(const char *msg)
{
  stopInputSequenceTrace();
  LOG_VERBOSE("send close \"%s\" to \"%s\"", msg, getName().c_str());
  ProtocolUtil::writef(getStream(), msg);

  // force the close to be sent before we return
  getStream()->flush();
}

deskflow::IStream *ClientProxy::getStream() const
{
  return m_controlStream.get();
}

void *ClientProxy::getEventTarget() const
{
  return static_cast<IComputer *>(const_cast<ClientProxy *>(this));
}

deskflow::IStream *ClientProxy::inputStream() const noexcept
{
  return m_inputStream.get();
}

bool ClientProxy::inputSequenceTlsLive() const
{
  if (!m_inputSequenceTicket || !m_inputSequenceTicket->live())
    return false;
  const auto current = getStream()->authenticatedPeerTicket();
  return current && current->live() && current->incarnation() == m_inputSequenceTicket->incarnation();
}

std::optional<std::uint32_t> ClientProxy::requestInputSequenceTrace()
{
  if (!supportsInputSequenceTrace() || m_inputSequenceStopped)
    return std::nullopt;
  if (!m_inputSequenceTicket) {
    auto ticket = getStream()->authenticatedPeerTicket();
    if (!ticket || !ticket->live())
      return std::nullopt;
    m_inputSequenceTicket = std::move(ticket);
  }
  if (!inputSequenceTlsLive()) {
    stopInputSequenceTrace();
    return std::nullopt;
  }
  if (m_inputSequenceGeneration)
    return m_inputSequenceGeneration;
  if (m_inputSequenceIssued == std::numeric_limits<std::uint32_t>::max())
    return std::nullopt;
  m_inputSequenceRequested = m_inputSequenceIssued + 1;
  return m_inputSequenceRequested;
}

OptionsList ClientProxy::prepareInputSequenceOptions(const OptionsList &options)
{
  const auto alive = proxyLifetime();
  OptionsList result;
  result.reserve(options.size());
  bool found = false, foundCutoff = false, foundPreparation = false;
  for (std::size_t index = 0; index < options.size(); index += 2) {
    const auto id = options[index], value = options[index + 1];
    if (id == kOptionInputPreparation) {
      if (foundPreparation || !supportsInputSequenceTrace())
        continue;
      foundPreparation = true;
      if (!value) {
        if (m_preparation && m_preparation->state->load() != InputPreparationState::Released) {
          failInputPreparation();
          return {};
        }
        revokeInputPreparation();
      } else {
        if ((!m_preparationRequested && !m_preparationIssued) || !inputCutoffTlsLive() ||
            (value != m_cutoffGeneration && value != m_cutoffRequested) ||
            (!m_cutoffBatched && !m_cutoffRequestedBatched))
          continue;
        if (!m_preparationIssued)
          m_preparationAcknowledged = false;
        m_preparationRequested = false;
        m_preparationIssued = true;
      }
    }
    if (id == kOptionInputCutoff || id == kOptionMappedInputCutoff || id == kOptionSealedInputCutoff ||
        id == kOptionBatchedInputCutoff) {
      const bool batched = id == kOptionBatchedInputCutoff;
      const bool sealed = id == kOptionSealedInputCutoff || batched;
      const bool mapped = id != kOptionInputCutoff;
      if (foundCutoff || !supportsInputSequenceTrace() ||
          (value &&
           (value != m_cutoffRequested || mapped != m_cutoffRequestedMapped || sealed != m_cutoffRequestedSealed ||
            batched != m_cutoffRequestedBatched) &&
           (value != m_cutoffGeneration || mapped != m_cutoffMapped || sealed != m_cutoffSealed ||
            batched != m_cutoffBatched)))
        continue;
      foundCutoff = true;
      if (!value) {
        retireInputCutoffCapability();
        if (!*alive)
          return {};
      } else if (value != m_cutoffGeneration) {
        if (!inputCutoffTlsLive() || (m_cutoff && m_cutoff->state != InputCutoffState::Resumed))
          continue;
        revokeMappedInputCutoff();
        m_cutoffMapped = mapped;
        m_cutoffSealed = sealed;
        m_cutoffBatched = batched;
        m_cutoffIssued = m_cutoffGeneration = value;
        m_cutoffRequested = 0;
        m_cutoffAcknowledged = false;
      }
    }
    if (id == kOptionInputSequenceTrace) {
      // This option is reserved for the programmatic request above. Repeated
      // DSOP with the current generation is idempotent, never a sequence reset.
      if (found || !supportsInputSequenceTrace())
        continue;
      found = true;
      if (!value) {
        retireInputSequenceTrace();
      } else {
        if (m_inputSequenceStopped || !inputSequenceTlsLive() ||
            (value != m_inputSequenceGeneration && value != m_inputSequenceRequested))
          continue;
        if (value != m_inputSequenceGeneration) {
          m_inputSequenceIssued = m_inputSequenceGeneration = value;
          m_inputSequenceRequested = 0;
          m_inputSequenceWire = 0;
          m_inputSequenceAcknowledged = false;
        }
      }
    }
    result.push_back(id);
    result.push_back(value);
  }
  return result;
}

void ClientProxy::retireInputSequenceTrace() noexcept
{
  m_inputSequenceGeneration = m_inputSequenceRequested = 0;
  m_inputSequenceWire = 0;
  m_inputSequenceAcknowledged = false;
  // Outstanding records remain bounded and identifiable for late replies.
}

void ClientProxy::stopInputSequenceTrace() noexcept
{
  revokeInputPreparation();
  retireInputSequenceTrace();
  m_inputSequenceStopped = true;
  for (auto &pending : m_inputSequencePending)
    pending.reset();
  m_inputSequenceStatistics.outstanding = 0;
  revokeMappedInputCutoff();
  m_cutoff.reset();
  m_cutoffTicket.reset();
  m_cutoffGeneration = m_cutoffRequested = 0;
  m_cutoffAcknowledged = false;
}

ClientProxy::InputSequenceStatistics ClientProxy::inputSequenceStatistics() const noexcept
{
  return m_inputSequenceStatistics;
}

void ClientProxy::writeInputPacket(const void *buffer, std::uint32_t size)
{
  if (queuePreparationWrite(buffer, size, true))
    return;
  const auto stamp = InputSequenceOriginScope::current();
  if (!m_inputSequenceAcknowledged || !stamp) {
    getStream()->write(buffer, size);
    return;
  }
  const std::string_view payload(static_cast<const char *>(buffer), size);
  const auto slot = std::find(m_inputSequencePending.begin(), m_inputSequencePending.end(), std::nullopt);
  if (!inputSequenceTlsLive()) {
    stopInputSequenceTrace();
  } else if (
      slot != m_inputSequencePending.end() && m_inputSequenceWire != std::numeric_limits<std::uint64_t>::max() &&
      validInputSequenceToken({m_inputSequenceGeneration, m_inputSequenceWire + 1, stamp}) &&
      validInputSequencePayload(payload)
  ) {
    const InputSequenceToken token{m_inputSequenceGeneration, m_inputSequenceWire + 1, stamp};
    const auto bytes = encodeInputSequenceEnvelope(token, payload);
    *slot = token;
    ++m_inputSequenceWire;
    ++m_inputSequenceStatistics.outstanding;
    increment(m_inputSequenceStatistics.enveloped);
    getStream()->write(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    return;
  }
  // Trace capacity, sequence exhaustion or unsupported payload must not drop
  // ordinary input. This write has no token or successful submission claim.
  increment(m_inputSequenceStatistics.unavailable);
  getStream()->write(buffer, size);
}

void ClientProxy::receiveInputSequenceAck(std::uint32_t generation)
{
  if (m_inputSequenceStopped || !generation || generation > m_inputSequenceIssued || !inputSequenceTlsLive())
    throw BadClientException("Unsolicited or unauthenticated input sequence acknowledgement");
  if (generation == m_inputSequenceGeneration)
    m_inputSequenceAcknowledged = true;
  // An actually issued, retired generation is recognized but cannot reactivate.
}

void ClientProxy::receiveInputSequenceReceipt(const InputSequenceReceipt &receipt)
{
  if (m_inputSequenceStopped || !validInputSequenceReceipt(receipt) || !inputSequenceTlsLive())
    throw BadClientException("Invalid or unauthenticated input sequence receipt");
  const auto slot = std::find_if(m_inputSequencePending.begin(), m_inputSequencePending.end(), [&](const auto &value) {
    return value && value->generation == receipt.generation && value->wireSequence == receipt.wireSequence;
  });
  if (slot == m_inputSequencePending.end())
    throw BadClientException("Unknown or duplicate input sequence receipt");
  const auto token = **slot;
  slot->reset();
  --m_inputSequenceStatistics.outstanding;
  if (receipt.generation != m_inputSequenceGeneration) {
    increment(m_inputSequenceStatistics.retiredReceipts);
    return;
  }
  increment(m_inputSequenceStatistics.receipts);
  const auto callback = onInputSequenceReceipt;
  if (callback)
    callback(token, receipt);
  // Do not access this after the callback; it may have destroyed the proxy.
}

bool ClientProxy::inputPreparationCapabilityReady() const
{
  return m_preparationAcknowledged && m_cutoffAcknowledged && m_cutoffBatched && inputCutoffTlsLive();
}

std::optional<std::uint32_t> ClientProxy::requestInputPreparationCapability()
{
  const auto generation = requestBatchedInputCutoffCapability();
  if (generation)
    m_preparationRequested = true;
  return generation;
}

void ClientProxy::receiveInputPreparationAck(std::uint32_t generation)
{
  if (!m_preparationIssued || !m_cutoffBatched || !generation || generation != m_cutoffGeneration ||
      !inputCutoffTlsLive())
    throw BadClientException("Unrequested input preparation acknowledgement");
  m_preparationAcknowledged = true;
}

void ClientProxy::revokeInputPreparation() noexcept
{
  if (m_preparation) {
    m_preparation->state->store(InputPreparationState::Failed, std::memory_order_release);
    m_retiredPreparation = m_preparation->binding;
    m_preparation.reset();
  }
  m_preparationRequested = m_preparationIssued = m_preparationAcknowledged = false;
}

void ClientProxy::failInputPreparation() noexcept
{
  revokeInputPreparation();
  revokeMappedInputCutoff();
  try {
    m_stream->close();
  } catch (...) {
  }
}

std::optional<InputPreparationBinding> ClientProxy::startInputPreparation(
    OriginalInputStamp capture, InputCutoffPoint point, InputCutoffClock::time_point deadline
)
{
  const auto now = InputCutoffClock::now();
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
  if (!m_preparationAcknowledged || !m_cutoffAcknowledged || !m_cutoffBatched || !inputCutoffTlsLive() ||
      (m_cutoff && m_cutoff->state != InputCutoffState::Resumed) ||
      (m_preparation && (m_preparation->state->load() != InputPreparationState::Released || m_preparation->flushing)) ||
      !validInputCutoffPoint(point) || !remaining || remaining < 0 || deadline > now + inputPreparationMaximumLifetime)
    return std::nullopt;
  InputPreparationBinding binding{nextCutoffIdentity<std::uint64_t>(), m_cutoffGeneration, capture};
  if (!validInputPreparationBinding(binding))
    return std::nullopt;
  if (m_preparation)
    m_retiredPreparation = m_preparation->binding;
  m_preparation = Preparation{binding, point, deadline};
  const auto bytes = encodeInputPreparationRequest({binding, point, static_cast<std::uint16_t>(remaining)});
  m_stream->write(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
  return binding; // Wire attempt acquired this exact cleanup address, even under reentry.
}

InputPreparationState ClientProxy::inputPreparationState(const InputPreparationBinding &binding)
{
  if (!m_preparation || m_preparation->binding != binding)
    return InputPreparationState::Failed;
  if (!inputCutoffTlsLive()) {
    m_preparation->state->store(InputPreparationState::Failed, std::memory_order_release);
    return InputPreparationState::Failed;
  }
  const auto state = m_preparation->state->load(std::memory_order_acquire);
  const auto now = InputCutoffClock::now();
  if (state == InputPreparationState::Canceling && now >= m_preparation->cleanupDeadline) {
    failInputPreparation();
    return InputPreparationState::Failed;
  }
  if (state != InputPreparationState::Released && state != InputPreparationState::Sealed &&
      state != InputPreparationState::Canceling && now >= m_preparation->deadline) {
    m_preparation->outcome = InputPreparationOutcome::Failed;
    m_preparation->state->store(InputPreparationState::Failed, std::memory_order_release);
    return InputPreparationState::Failed;
  }
  return state;
}

InputPreparationOutcome ClientProxy::inputPreparationOutcome(const InputPreparationBinding &binding)
{
  return m_preparation && m_preparation->binding == binding ? m_preparation->outcome : InputPreparationOutcome::Failed;
}

std::optional<PreparedInputFocus> ClientProxy::preparedInputFocus(const InputPreparationBinding &binding)
{
  const auto alive = proxyLifetime();
  const auto state = inputPreparationState(binding);
  if (!*alive || !m_preparation || !m_preparation->focus ||
      (state != InputPreparationState::Prepared && state != InputPreparationState::Promoting &&
       state != InputPreparationState::Sealed))
    return std::nullopt;
  PreparedInputFocus result(*m_preparation->focus, *m_cutoffTicket, m_preparation->state, m_preparation->deadline);
  return result.live() ? std::optional(result) : std::nullopt;
}

void ClientProxy::receivePreparedInputFocus(const PreparedInputFocusResult &result)
{
  if (!validPreparedInputFocusResult(result) || !inputCutoffTlsLive() || !m_preparationAcknowledged)
    throw BadClientException("Invalid prepared input focus");
  if (m_retiredPreparation == result.binding && (!m_preparation || m_preparation->binding != result.binding))
    return;
  if (!m_preparation || m_preparation->binding != result.binding || m_preparation->point != result.point ||
      (m_preparation->focus && m_preparation->focus != result))
    throw BadClientException("Prepared focus substituted its exact request");
  const auto state = m_preparation->state->load();
  if (state == InputPreparationState::Canceling || state == InputPreparationState::Released ||
      state == InputPreparationState::Failed)
    return;
  if (InputCutoffClock::now() >= m_preparation->deadline) {
    cancelInputPreparation(result.binding, InputCutoffClock::now() + inputCutoffMaximumLifetime);
    return;
  }
  if (state == InputPreparationState::Preparing) {
    m_preparation->focus = result;
    m_preparation->state->store(InputPreparationState::Prepared, std::memory_order_release);
  }
}

std::optional<InputCutoffBinding> ClientProxy::promoteInputPreparation(
    const PreparedInputFocus &focus, const KeyboardRouteBinding &forwarding, InputCutoffClock::time_point deadline
)
{
  const auto now = InputCutoffClock::now();
  if (!m_preparation || m_preparation->binding != focus.binding() || m_preparation->state != focus.m_state ||
      m_preparation->state->load() != InputPreparationState::Prepared || !focus.live() || !inputCutoffTlsLive() ||
      !m_cutoffAcknowledged || !m_cutoffBatched || (m_cutoff && m_cutoff->state != InputCutoffState::Resumed))
    return std::nullopt;
  deadline = std::min({deadline, m_preparation->deadline, now + inputCutoffMaximumLifetime});
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
  InputCutoffBinding binding{
      nextCutoffIdentity<std::uint64_t>(), m_cutoffGeneration, focus.binding().capture, forwarding,
      focus.snapshot().viewerConnection
  };
  if (remaining <= 0 || deadline > now + inputCutoffMaximumLifetime ||
      !matchesInputPreparationPromotion(focus.snapshot(), binding))
    return std::nullopt;
  m_preparation->state->store(InputPreparationState::Promoting, std::memory_order_release);
  m_preparation->promotion = binding;
  if (m_cutoff) {
    m_retiredCutoff = m_cutoff->binding;
    revokeMappedInputCutoff();
  }
  m_cutoff = Cutoff{binding, deadline};
  m_cutoff->point = focus.snapshot().point;
  m_cutoff->seal = binding;
  const auto bytes = encodeInputPreparationPromotion({focus.binding(), binding, static_cast<std::uint16_t>(remaining)});
  m_stream->write(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
  return binding; // Local identity survives synchronous cancellation/destruction.
}

bool ClientProxy::cancelInputPreparation(const InputPreparationBinding &binding, InputCutoffClock::time_point deadline)
{
  if (!m_preparation || m_preparation->binding != binding || !inputCutoffTlsLive())
    return false;
  const auto state = m_preparation->state->load();
  if (state == InputPreparationState::Released)
    return true;
  if (state == InputPreparationState::Canceling)
    return InputCutoffClock::now() < m_preparation->cleanupDeadline;
  const auto now = InputCutoffClock::now();
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
  if (remaining <= 0 || deadline > now + inputCutoffMaximumLifetime)
    return false;
  m_preparation->state->store(InputPreparationState::Canceling, std::memory_order_release);
  m_preparation->cleanupDeadline = deadline;
  if (m_preparation->promotion)
    revokeMappedInputCutoff();
  const auto bytes = encodeInputPreparationCancellation({binding, static_cast<std::uint16_t>(remaining)});
  const auto alive = proxyLifetime();
  m_stream->write(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
  return *alive && inputCutoffTlsLive() && m_preparation && m_preparation->binding == binding;
}

bool ClientProxy::queuePreparationWrite(const void *buffer, std::uint32_t size, bool input)
{
  if (!m_preparation)
    return false;
  const auto state = m_preparation->state->load();
  if (state == InputPreparationState::Released && !m_preparation->flushing)
    return false;
  const std::string_view packet(static_cast<const char *>(buffer), size);
  const bool release = packet.starts_with("DKUP") || packet.starts_with("DMUP") ||
                       (packet.starts_with("DKDN") && packet.size() >= 6 &&
                        static_cast<unsigned char>(packet[4]) == 0xee && static_cast<unsigned char>(packet[5]) == 0x07);
  if (input && !release) {
    failInputPreparation();
    return true;
  }
  constexpr std::size_t maximumPackets = 64, maximumBytes = 65536;
  if (size > maximumBytes || m_preparation->queued.size() >= maximumPackets ||
      m_preparation->queuedBytes > maximumBytes - size) {
    failInputPreparation();
    return true;
  }
  const auto binding = m_preparation->binding;
  const auto alive = proxyLifetime();
  // Reserve the ordered position before cancellation writes can reenter.
  // A synchronous cleanup ACK may drain this very entry; never resend it.
  m_preparation->queued.emplace_back(packet);
  m_preparation->queuedBytes += size;
  const bool canceled = state == InputPreparationState::Released ||
                        (state == InputPreparationState::Sealed && m_preparation->promotion
                             ? resumeInputCutoff(*m_preparation->promotion)
                             : cancelInputPreparation(binding, InputCutoffClock::now() + inputCutoffMaximumLifetime));
  if (!*alive)
    return true;
  if (!canceled)
    failInputPreparation();
  return true;
}

void ClientProxy::receiveInputPreparationReceipt(const InputPreparationReceipt &receipt)
{
  if (!validInputPreparationReceipt(receipt) || !inputCutoffTlsLive() || !m_preparationAcknowledged)
    throw BadClientException("Invalid input preparation receipt");
  if (m_retiredPreparation == receipt.binding && (!m_preparation || m_preparation->binding != receipt.binding))
    return;
  if (!m_preparation || m_preparation->binding != receipt.binding)
    throw BadClientException("Unknown input preparation receipt");
  const auto state = m_preparation->state->load();
  if (state == InputPreparationState::Released)
    return;
  m_preparation->outcome = receipt.outcome;
  if (receipt.state == InputPreparationState::Failed) {
    if (state != InputPreparationState::Canceling) {
      m_preparation->state->store(InputPreparationState::Failed, std::memory_order_release);
      cancelInputPreparation(receipt.binding, InputCutoffClock::now() + inputCutoffMaximumLifetime);
    }
    return;
  }
  // Released can also prove that unsupported/no-target admission made no gate.
  if (state != InputPreparationState::Canceling && state != InputPreparationState::Preparing)
    throw BadClientException("Unrequested preparation release");
  if (state == InputPreparationState::Preparing && receipt.outcome != InputPreparationOutcome::Unsupported &&
      receipt.outcome != InputPreparationOutcome::NoTarget)
    throw BadClientException("Unproven early preparation release");
  if (state == InputPreparationState::Canceling && InputCutoffClock::now() >= m_preparation->cleanupDeadline) {
    failInputPreparation();
    return;
  }
  if (m_preparation->promotion && m_cutoff) {
    revokeMappedInputCutoff();
    m_cutoff->state = InputCutoffState::Resumed;
    m_cutoff->resuming = false;
  }
  m_preparation->state->store(InputPreparationState::Released, std::memory_order_release);
  m_preparation->flushing = true;
  const auto alive = proxyLifetime();
  std::size_t delivered = 0;
  while (m_preparation && !m_preparation->queued.empty()) {
    if (++delivered > 64 || InputCutoffClock::now() >= m_preparation->cleanupDeadline) {
      failInputPreparation();
      return;
    }
    auto packet = std::move(m_preparation->queued.front());
    m_preparation->queued.pop_front();
    m_preparation->queuedBytes -= packet.size();
    m_stream->write(packet.data(), static_cast<std::uint32_t>(packet.size()));
    if (!*alive)
      return;
    if (!inputCutoffTlsLive()) {
      failInputPreparation();
      return;
    }
  }
  if (m_preparation)
    m_preparation->flushing = false;
}

bool ClientProxy::inputCutoffTlsLive() const
{
  const auto peer = m_stream->authenticatedPeerTicket();
  return !m_inputSequenceStopped && m_cutoffTicket && m_cutoffTicket->live() && peer && peer->live() &&
         peer->incarnation() == m_cutoffTicket->incarnation();
}

std::optional<std::uint32_t> ClientProxy::requestInputCutoffCapability()
{
  return requestInputCutoffCapability(false);
}

std::optional<std::uint32_t> ClientProxy::requestMappedInputCutoffCapability()
{
  return requestInputCutoffCapability(true);
}

std::optional<std::uint32_t> ClientProxy::requestSealedInputCutoffCapability()
{
  return requestInputCutoffCapability(true, true);
}

std::optional<std::uint32_t> ClientProxy::requestBatchedInputCutoffCapability()
{
  return requestInputCutoffCapability(true, true, true);
}

std::optional<std::uint32_t> ClientProxy::requestInputCutoffCapability(bool mapped, bool sealed, bool batched)
{
  if (!supportsInputSequenceTrace() || m_inputSequenceStopped)
    return std::nullopt;
  if (!m_cutoffTicket)
    m_cutoffTicket = m_stream->authenticatedPeerTicket();
  if (!inputCutoffTlsLive())
    return std::nullopt;
  if (m_cutoffGeneration && mapped == m_cutoffMapped && sealed == m_cutoffSealed && batched == m_cutoffBatched)
    return m_cutoffGeneration;
  if (m_cutoff && m_cutoff->state != InputCutoffState::Resumed)
    return std::nullopt;
  if (!m_cutoffRequested || mapped != m_cutoffRequestedMapped || sealed != m_cutoffRequestedSealed ||
      batched != m_cutoffRequestedBatched) {
    m_cutoffRequested = nextCutoffIdentity<std::uint32_t>();
    m_cutoffRequestedMapped = mapped;
    m_cutoffRequestedSealed = sealed;
    m_cutoffRequestedBatched = batched;
  }
  return m_cutoffRequested ? std::optional(m_cutoffRequested) : std::nullopt;
}

void ClientProxy::receiveInputCutoffAck(std::uint32_t generation, bool mapped, bool sealed, bool batched)
{
  if (!generation || generation > m_cutoffIssued || !inputCutoffTlsLive())
    throw BadClientException("Unsolicited input cutoff capability acknowledgement");
  if (generation == m_cutoffGeneration) {
    if (mapped != m_cutoffMapped || sealed != m_cutoffSealed || batched != m_cutoffBatched)
      throw BadClientException("Input cutoff capability version mismatch");
    m_cutoffAcknowledged = true;
  }
  // An older issued capability can arrive after reset or a replacement
  // negotiation; recognizing it must not acknowledge the current generation.
}

std::optional<InputCutoffBinding> ClientProxy::startInputCutoff(
    OriginalInputStamp capture, const KeyboardRouteBinding &forwarding, std::uint64_t viewerConnection,
    InputCutoffClock::time_point deadline
)
{
  return startInputCutoff(capture, forwarding, viewerConnection, deadline, std::nullopt);
}

std::optional<InputCutoffBinding> ClientProxy::startMappedInputCutoff(
    OriginalInputStamp capture, const KeyboardRouteBinding &forwarding, std::uint64_t viewerConnection,
    InputCutoffPoint point, InputCutoffClock::time_point deadline
)
{
  if (!validInputCutoffPoint(point))
    return std::nullopt;
  return startInputCutoff(capture, forwarding, viewerConnection, deadline, point);
}

std::optional<InputCutoffBinding> ClientProxy::startSealedInputCutoff(
    OriginalInputStamp capture, const KeyboardRouteBinding &forwarding, std::uint64_t viewerConnection,
    InputCutoffPoint point, InputCutoffClock::time_point deadline
)
{
  return startInputCutoff(capture, forwarding, viewerConnection, deadline, point, true);
}

std::optional<InputCutoffBinding> ClientProxy::startInputCutoff(
    OriginalInputStamp capture, const KeyboardRouteBinding &forwarding, std::uint64_t viewerConnection,
    InputCutoffClock::time_point deadline, std::optional<InputCutoffPoint> point, bool sealed
)
{
  const auto now = InputCutoffClock::now();
  if (!m_cutoffAcknowledged || (point && (!m_cutoffMapped || !validInputCutoffPoint(*point))) ||
      (m_preparation && m_preparation->state->load() != InputPreparationState::Released) || sealed != m_cutoffSealed ||
      (sealed && !point) || !inputCutoffTlsLive() || (m_cutoff && m_cutoff->state != InputCutoffState::Resumed) ||
      deadline <= now || deadline > now + inputCutoffMaximumLifetime)
    return std::nullopt;
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
  InputCutoffBinding binding{
      nextCutoffIdentity<std::uint64_t>(), m_cutoffGeneration, capture, forwarding, viewerConnection
  };
  if (!validInputCutoffBinding(binding) || !remaining)
    return std::nullopt;
  if (m_cutoff)
    m_retiredCutoff = m_cutoff->seal.value_or(m_cutoff->binding);
  revokeMappedInputCutoff();
  m_cutoff = Cutoff{binding, deadline};
  m_cutoff->point = point;
  if (sealed)
    m_cutoff->seal = binding;
  const InputCutoffRequest request{binding, static_cast<std::uint16_t>(remaining)};
  const auto bytes = sealed  ? encodeInputCutoffSealedRequest({request, *point})
                     : point ? encodeInputCutoffMappedRequest({request, *point})
                             : encodeInputCutoffRequest(request);
  m_stream->write(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
  // Once written, retain the exact cleanup address even if a synchronous
  // callback canceled the operation or destroyed this proxy.
  return binding;
}

std::optional<InputCutoffBinding> ClientProxy::refreshMappedInputCutoff(
    const InputCutoffSeal &seal, OriginalInputStamp capture, InputCutoffPoint point,
    InputCutoffClock::time_point deadline, bool presentCursor
)
{
  const auto now = InputCutoffClock::now();
  if (!m_cutoff || !m_cutoffSealed || !m_cutoffAcknowledged || !inputCutoffTlsLive() || !seal.live() ||
      !m_cutoff->sealIssued || m_cutoff->seal != seal.binding() || m_cutoff->sealRevoked != seal.m_revoked ||
      m_cutoff->resuming || m_cutoff->state == InputCutoffState::Pending ||
      m_cutoff->state == InputCutoffState::Denied || m_cutoff->state == InputCutoffState::Resumed ||
      !validInputCutoffPoint(point) || deadline <= now || deadline > now + inputCutoffMaximumLifetime)
    return std::nullopt;
  auto binding = seal.binding();
  binding.request = nextCutoffIdentity<std::uint64_t>();
  binding.capture = capture;
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
  if (!remaining || !continuesInputCutoffSeal(m_cutoff->binding, binding))
    return std::nullopt;
  m_retiredCutoff = m_cutoff->binding;
  m_cutoff->revoked->store(true, std::memory_order_release);
  m_cutoff->binding = binding;
  m_cutoff->deadline = deadline;
  m_cutoff->point = point;
  m_cutoff->mapped.reset();
  m_cutoff->batch.clear();
  m_cutoff->revoked = std::make_shared<std::atomic_bool>(false);
  m_cutoff->state = InputCutoffState::Pending;
  const auto bytes = encodeInputCutoffMapRequest(
      {{{binding, static_cast<std::uint16_t>(remaining)}, point}, seal.binding().request, presentCursor}
  );
  const auto alive = proxyLifetime();
  m_stream->write(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
  return *alive && inputCutoffTlsLive() && m_cutoff && m_cutoff->binding == binding && !m_cutoff->resuming &&
                 seal.live()
             ? std::optional(binding)
             : std::nullopt;
}

bool ClientProxy::matchesInputCutoff(const InputCutoffBinding &binding) const noexcept
{
  return m_cutoff && (m_cutoff->binding == binding || m_cutoff->seal == binding ||
                      std::any_of(m_cutoff->batch.begin(), m_cutoff->batch.end(), [&](const auto &point) {
                        return point.item.binding == binding;
                      }));
}

std::optional<std::vector<InputCutoffBinding>>
ClientProxy::startMappedInputBatch(const InputCutoffSeal &seal, const std::vector<InputCutoffMapItem> &items)
{
  const auto now = InputCutoffClock::now();
  if (!m_cutoff || !m_cutoffBatched || !m_cutoffAcknowledged || !inputCutoffTlsLive() || !seal.live() ||
      !m_cutoff->sealIssued || m_cutoff->seal != seal.binding() || m_cutoff->sealRevoked != seal.m_revoked ||
      m_cutoff->resuming || m_cutoff->state == InputCutoffState::Pending ||
      m_cutoff->state == InputCutoffState::Denied || m_cutoff->state == InputCutoffState::Resumed || items.empty() ||
      items.size() > inputCutoffMaximumBatch)
    return std::nullopt;
  std::vector<InputCutoffBinding> bindings;
  std::vector<Cutoff::BatchPoint> points;
  InputCutoffBatchRequest request{seal.binding().request};
  auto previous = m_cutoff->binding;
  auto earliest = now + inputCutoffMaximumLifetime;
  for (const auto &item : items) {
    auto binding = seal.binding();
    binding.request = nextCutoffIdentity<std::uint64_t>();
    binding.capture = item.capture;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(item.deadline - now).count();
    if (!validInputCutoffPoint(item.point) || item.deadline <= now ||
        item.deadline > now + inputCutoffMaximumLifetime || !remaining || !continuesInputCutoffSeal(previous, binding))
      return std::nullopt;
    previous = binding;
    earliest = std::min(earliest, item.deadline);
    bindings.push_back(binding);
    points.push_back({{binding, item.point, item.deadline, item.presentCursor}, std::nullopt});
    request.items.push_back(
        {{{binding, static_cast<std::uint16_t>(remaining)}, item.point}, request.sealRequest, item.presentCursor}
    );
  }
  const auto bytes = encodeInputCutoffBatchRequest(request);
  m_retiredCutoff = m_cutoff->binding;
  m_cutoff->revoked->store(true, std::memory_order_release);
  m_cutoff->revoked = std::make_shared<std::atomic_bool>(false);
  m_cutoff->batch = std::move(points);
  m_cutoff->binding = bindings.back();
  m_cutoff->deadline = earliest;
  m_cutoff->point.reset();
  m_cutoff->mapped.reset();
  m_cutoff->state = InputCutoffState::Pending;
  const auto alive = proxyLifetime();
  lifecycleDiagnostic(
      "Mac input", "input.map-batch", "started", "Sending ordered input map request",
      {{"firstSequence", QString::number(items.front().capture.sequence)},
       {"lastSequence", QString::number(items.back().capture.sequence)},
       {"records", qint64(items.size())},
       {"remainingUs",
        qint64(std::chrono::duration_cast<std::chrono::microseconds>(earliest - InputCutoffClock::now()).count())}}
  );
  m_stream->write(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
  lifecycleDiagnostic(
      "Mac input", "input.map-batch-write", "completed", "Input map request handed to the transport",
      {{"lastSequence", QString::number(items.back().capture.sequence)},
       {"elapsedUs",
        qint64(std::chrono::duration_cast<std::chrono::microseconds>(InputCutoffClock::now() - now).count())}}
  );
  return *alive && inputCutoffTlsLive() && m_cutoff && m_cutoff->binding == bindings.back() && !m_cutoff->resuming &&
                 seal.live()
             ? std::optional(bindings)
             : std::nullopt;
}

std::optional<InputCutoffSeal> ClientProxy::inputCutoffSeal(const InputCutoffBinding &binding)
{
  if (!matchesInputCutoff(binding) || !m_cutoff->seal || !m_cutoff->sealIssued || !inputCutoffTlsLive() ||
      m_cutoff->resuming || m_cutoff->state == InputCutoffState::Denied || m_cutoff->state == InputCutoffState::Resumed)
    return std::nullopt;
  InputCutoffSeal result(*m_cutoff->seal, *m_cutoffTicket, m_cutoff->sealRevoked);
  return result.live() ? std::optional(result) : std::nullopt;
}

bool ClientProxy::resumeInputCutoff(const InputCutoffBinding &binding)
{
  if (!matchesInputCutoff(binding) || !inputCutoffTlsLive())
    return false;
  if (m_cutoff->resuming || m_cutoff->state == InputCutoffState::Resumed)
    return true;
  // Revoke the observation before the ordered write can reenter this proxy.
  revokeMappedInputCutoff();
  m_cutoff->resuming = true;
  m_cutoff->resumeDeadline = InputCutoffClock::now() + inputCutoffMaximumLifetime;
  m_cutoff->state = InputCutoffState::Denied;
  const auto exact = m_cutoff->seal.value_or(binding);
  if (m_preparation && m_preparation->promotion == exact) {
    m_preparation->state->store(InputPreparationState::Canceling, std::memory_order_release);
    m_preparation->cleanupDeadline = m_cutoff->resumeDeadline;
  }
  const auto bytes = encodeInputCutoffResume(exact);
  const auto alive = proxyLifetime();
  m_stream->write(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
  return *alive && inputCutoffTlsLive() && matchesInputCutoff(binding);
}

InputCutoffState ClientProxy::inputCutoffState(const InputCutoffBinding &binding)
{
  if (!matchesInputCutoff(binding))
    return InputCutoffState::Denied;
  if (!inputCutoffTlsLive()) {
    revokeMappedInputCutoff();
    return InputCutoffState::Denied;
  }
  if (m_cutoff->state == InputCutoffState::Resumed)
    return InputCutoffState::Resumed;
  if (m_cutoff->resuming)
    return InputCutoffClock::now() < m_cutoff->resumeDeadline ? InputCutoffState::Resuming : InputCutoffState::Denied;
  if (!m_cutoff->batch.empty() && m_cutoff->state == InputCutoffState::DrainObserved) {
    const auto point = std::find_if(m_cutoff->batch.begin(), m_cutoff->batch.end(), [&](const auto &entry) {
      return entry.item.binding == binding;
    });
    const auto deadline = point == m_cutoff->batch.end() ? m_cutoff->deadline : point->item.deadline;
    return InputCutoffClock::now() < deadline ? InputCutoffState::DrainObserved : InputCutoffState::Sealed;
  }
  if (InputCutoffClock::now() >= m_cutoff->deadline) {
    if (inputCutoffSeal(binding)) {
      m_cutoff->revoked->store(true, std::memory_order_release);
      return m_cutoff->state == InputCutoffState::Pending ? InputCutoffState::Denied : InputCutoffState::Sealed;
    }
    // A fresh bounded cleanup can follow an expired observation. This sends
    // no input and never renews the old observation's absolute deadline.
    if (!resumeInputCutoff(binding))
      return InputCutoffState::Denied;
    return inputCutoffState(binding); // Recheck after a potentially reentrant write.
  }
  return m_cutoff->state;
}

void ClientProxy::receiveInputCutoffReceipt(const InputCutoffReceipt &receipt)
{
  if (!inputCutoffTlsLive() || !validInputCutoffBinding(receipt.binding))
    throw BadClientException("Unauthenticated input cutoff receipt");
  if (m_retiredCutoff == receipt.binding && (!m_cutoff || m_cutoff->seal != receipt.binding))
    return;
  if (!matchesInputCutoff(receipt.binding))
    throw BadClientException("Unknown input cutoff receipt binding");
  if (receipt.state == InputCutoffState::Resumed) {
    if (m_cutoff->seal && receipt.binding != *m_cutoff->seal)
      throw BadClientException("Sealed input cutoff resumed a map instead of its origin");
    if (!m_cutoff->resuming && m_cutoff->state != InputCutoffState::Resumed)
      throw BadClientException("Unrequested input cutoff resume");
    revokeMappedInputCutoff();
    m_cutoff->state = InputCutoffState::Resumed;
    m_cutoff->resuming = false;
    if (m_preparation && m_preparation->promotion == receipt.binding) {
      receiveInputPreparationReceipt({m_preparation->binding, InputPreparationState::Released, m_preparation->outcome});
      return;
    }
    return;
  }
  if (m_cutoff->resuming || m_cutoff->state == InputCutoffState::Resumed)
    return;
  if (receipt.state != InputCutoffState::BackendObserved && receipt.state != InputCutoffState::Denied)
    throw BadClientException("Invalid input cutoff outcome");
  if (InputCutoffClock::now() >= m_cutoff->deadline && !m_cutoff->sealIssued) {
    resumeInputCutoff(receipt.binding);
    return;
  }
  if (m_cutoff->state == InputCutoffState::Denied && receipt.state != InputCutoffState::Denied)
    throw BadClientException("Revoked input cutoff observation revived");
  if ((m_cutoff->point || !m_cutoff->batch.empty()) && receipt.state == InputCutoffState::BackendObserved)
    return; // IPC2 requires its mapped completion, never PONG alone.
  m_cutoff->state = receipt.state;
  if (receipt.state == InputCutoffState::Denied)
    resumeInputCutoff(receipt.binding);
}

void ClientProxy::revokeMappedInputCutoff() noexcept
{
  if (m_cutoff) {
    m_cutoff->revoked->store(true, std::memory_order_release);
    m_cutoff->sealRevoked->store(true, std::memory_order_release);
  }
}

void ClientProxy::receiveMappedInputCutoffResult(const InputCutoffMappedResult &result)
{
  if (!inputCutoffTlsLive() || !validInputCutoffMappedResult(result))
    throw BadClientException("Unauthenticated or invalid mapped cutoff result");
  if (m_retiredCutoff == result.binding)
    return;
  if (!m_cutoff || !m_cutoffMapped || m_cutoff->seal || m_cutoff->binding != result.binding ||
      m_cutoff->point != result.point)
    throw BadClientException("Mapped cutoff substituted its exact request");
  if (m_cutoff->resuming || m_cutoff->state == InputCutoffState::Resumed)
    return;
  if (InputCutoffClock::now() >= m_cutoff->deadline) {
    resumeInputCutoff(result.binding);
    return;
  }
  if (m_cutoff->state == InputCutoffState::Denied || (m_cutoff->mapped && m_cutoff->mapped != result))
    throw BadClientException("Mapped cutoff changed or revived its observation");
  m_cutoff->mapped = result;
  m_cutoff->state = InputCutoffState::DrainObserved;
}

void ClientProxy::receiveSealedInputCutoffResult(const InputCutoffSealedResult &result)
{
  const auto &mapped = result.mapping;
  if (!inputCutoffTlsLive() || !validInputCutoffMappedResult(mapped))
    throw BadClientException("Unauthenticated or invalid sealed cutoff result");
  if (m_retiredCutoff == mapped.binding && (!m_cutoff || m_cutoff->binding != mapped.binding))
    return;
  if (!m_cutoff || !m_cutoffSealed || !m_cutoff->batch.empty() || !m_cutoff->seal ||
      m_cutoff->binding != mapped.binding || m_cutoff->seal->request != result.sealRequest ||
      m_cutoff->point != mapped.point)
    throw BadClientException("Sealed cutoff substituted its exact request");
  if (m_cutoff->resuming || m_cutoff->state == InputCutoffState::Resumed)
    return;
  if (InputCutoffClock::now() >= m_cutoff->deadline) {
    resumeInputCutoff(mapped.binding);
    return;
  }
  if (m_cutoff->state == InputCutoffState::Denied || m_cutoff->sealRevoked->load(std::memory_order_acquire) ||
      (m_cutoff->mapped && m_cutoff->mapped != mapped))
    throw BadClientException("Sealed cutoff changed or revived its observation");
  m_cutoff->mapped = mapped;
  m_cutoff->sealIssued = true;
  m_cutoff->state = InputCutoffState::DrainObserved;
  if (m_preparation && m_preparation->promotion == mapped.binding &&
      m_preparation->state->load() == InputPreparationState::Promoting)
    m_preparation->state->store(InputPreparationState::Sealed, std::memory_order_release);
}

void ClientProxy::receiveInputCutoffBatchResult(const InputCutoffBatchResult &result)
{
  if (!inputCutoffTlsLive() || !m_cutoff || !m_cutoffBatched || !m_cutoff->seal ||
      result.sealRequest != m_cutoff->seal->request || result.items.empty() ||
      result.items.size() != m_cutoff->batch.size())
    throw BadClientException("Unrequested input cutoff batch result");
  // Validate the entire group before publishing any independent receipt.
  for (std::size_t index = 0; index < result.items.size(); ++index) {
    const auto &expected = m_cutoff->batch[index];
    const auto &item = result.items[index];
    if (!validInputCutoffMappedResult(item) || item.binding != expected.item.binding ||
        item.point != expected.item.point || !sameInputCutoffMappedIdentity(result.items.front(), item) ||
        (expected.mapped && expected.mapped != item))
      throw BadClientException("Input cutoff batch substituted or changed an item");
  }
  if (m_cutoff->resuming || m_cutoff->state == InputCutoffState::Resumed)
    return;
  if (m_cutoff->state == InputCutoffState::DrainObserved)
    return; // An identical duplicate cannot expire still-live sibling points.
  lifecycleDiagnostic(
      "Mac input", "input.map-batch", "observed", "Received the ordered input map result",
      {{"lastSequence", QString::number(result.items.back().binding.capture.sequence)},
       {"records", qint64(result.items.size())},
       {"remainingUs",
        qint64(
            std::chrono::duration_cast<std::chrono::microseconds>(m_cutoff->deadline - InputCutoffClock::now()).count()
        )}}
  );
  if (InputCutoffClock::now() >= m_cutoff->deadline) {
    resumeInputCutoff(*m_cutoff->seal);
    return;
  }
  if (m_cutoff->state == InputCutoffState::Denied || m_cutoff->sealRevoked->load(std::memory_order_acquire))
    throw BadClientException("Input cutoff batch revived a revoked seal");
  for (std::size_t index = 0; index < result.items.size(); ++index)
    m_cutoff->batch[index].mapped = result.items[index];
  m_cutoff->state = InputCutoffState::DrainObserved;
}

std::optional<InputCutoffObservation> ClientProxy::inputCutoffObservation(const InputCutoffBinding &binding)
{
  const auto alive = proxyLifetime();
  const auto state = inputCutoffState(binding);
  if (!*alive || state != InputCutoffState::DrainObserved || !m_cutoff)
    return std::nullopt;
  if (!m_cutoff->batch.empty()) {
    const auto point = std::find_if(m_cutoff->batch.begin(), m_cutoff->batch.end(), [&](const auto &entry) {
      return entry.item.binding == binding;
    });
    if (point == m_cutoff->batch.end() || !point->mapped)
      return std::nullopt;
    InputCutoffObservation result(
        *point->mapped, *m_cutoffTicket, m_cutoff->revoked, point->item.deadline, inputCutoffSeal(binding)
    );
    return result.live() ? std::optional(result) : std::nullopt;
  }
  if (m_cutoff->binding != binding || !m_cutoff->mapped)
    return std::nullopt;
  InputCutoffObservation result(
      *m_cutoff->mapped, *m_cutoffTicket, m_cutoff->revoked, m_cutoff->deadline, inputCutoffSeal(binding)
  );
  return result.live() ? std::optional(result) : std::nullopt;
}

void ClientProxy::retireInputCutoffCapability()
{
  if (m_preparation && m_preparation->state->load() != InputPreparationState::Released) {
    failInputPreparation();
    return;
  }
  revokeInputPreparation();
  if (m_cutoff) {
    const auto alive = proxyLifetime();
    const auto binding = m_cutoff->binding;
    if (!resumeInputCutoff(binding) || !*alive)
      return;
    m_retiredCutoff = m_cutoff->seal.value_or(binding);
    m_cutoff.reset();
  }
  m_cutoffGeneration = m_cutoffRequested = 0;
  m_cutoffAcknowledged = false;
}

void ClientProxy::writeControlPacket(const void *buffer, std::uint32_t size)
{
  const std::string_view packet(static_cast<const char *>(buffer), size);
  if (!packet.starts_with("CNOP") && !packet.starts_with("CALV") && queuePreparationWrite(buffer, size))
    return;
  if (m_cutoff && !m_cutoff->resuming && m_cutoff->state != InputCutoffState::Resumed && !packet.starts_with("CNOP") &&
      !packet.starts_with("CALV")) {
    const auto alive = proxyLifetime();
    const auto binding = m_cutoff->binding;
    if (!resumeInputCutoff(binding)) {
      if (*alive)
        m_stream->close();
      return;
    }
  }
  m_stream->write(buffer, size);
}
