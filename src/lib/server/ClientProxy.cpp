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
  bool found = false, foundCutoff = false;
  for (std::size_t index = 0; index < options.size(); index += 2) {
    const auto id = options[index], value = options[index + 1];
    if (id == kOptionInputCutoff) {
      if (foundCutoff || !supportsInputSequenceTrace() ||
          (value && value != m_cutoffRequested && value != m_cutoffGeneration))
        continue;
      foundCutoff = true;
      if (!value) {
        retireInputCutoffCapability();
        if (!*alive)
          return {};
      } else if (value != m_cutoffGeneration) {
        if (!inputCutoffTlsLive())
          continue;
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
  retireInputSequenceTrace();
  m_inputSequenceStopped = true;
  for (auto &pending : m_inputSequencePending)
    pending.reset();
  m_inputSequenceStatistics.outstanding = 0;
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
  const auto stamp = InputSequenceOriginScope::current();
  if (!m_inputSequenceAcknowledged || !stamp) {
    getStream()->write(buffer, size);
    return;
  }
  const std::string_view payload(static_cast<const char *>(buffer), size);
  const auto slot = std::find(m_inputSequencePending.begin(), m_inputSequencePending.end(), std::nullopt);
  if (!inputSequenceTlsLive()) {
    stopInputSequenceTrace();
  } else if (slot != m_inputSequencePending.end() && m_inputSequenceWire != std::numeric_limits<std::uint64_t>::max() &&
             validInputSequenceToken({m_inputSequenceGeneration, m_inputSequenceWire + 1, stamp}) &&
             validInputSequencePayload(payload)) {
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

bool ClientProxy::inputCutoffTlsLive() const
{
  const auto peer = m_stream->authenticatedPeerTicket();
  return !m_inputSequenceStopped && m_cutoffTicket && m_cutoffTicket->live() && peer && peer->live() &&
         peer->incarnation() == m_cutoffTicket->incarnation();
}

std::optional<std::uint32_t> ClientProxy::requestInputCutoffCapability()
{
  if (!supportsInputSequenceTrace() || m_inputSequenceStopped)
    return std::nullopt;
  if (!m_cutoffTicket)
    m_cutoffTicket = m_stream->authenticatedPeerTicket();
  if (!inputCutoffTlsLive())
    return std::nullopt;
  if (m_cutoffGeneration)
    return m_cutoffGeneration;
  if (!m_cutoffRequested)
    m_cutoffRequested = nextCutoffIdentity<std::uint32_t>();
  return m_cutoffRequested ? std::optional(m_cutoffRequested) : std::nullopt;
}

void ClientProxy::receiveInputCutoffAck(std::uint32_t generation)
{
  if (!generation || generation > m_cutoffIssued || !inputCutoffTlsLive())
    throw BadClientException("Unsolicited input cutoff capability acknowledgement");
  if (generation == m_cutoffGeneration)
    m_cutoffAcknowledged = true;
  // An older issued capability can arrive after reset or a replacement
  // negotiation; recognizing it must not acknowledge the current generation.
}

std::optional<InputCutoffBinding> ClientProxy::startInputCutoff(
    OriginalInputStamp capture, const KeyboardRouteBinding &forwarding, std::uint64_t viewerConnection,
    InputCutoffClock::time_point deadline
)
{
  const auto now = InputCutoffClock::now();
  if (!m_cutoffAcknowledged || !inputCutoffTlsLive() || (m_cutoff && m_cutoff->state != InputCutoffState::Resumed) ||
      deadline <= now || deadline > now + inputCutoffMaximumLifetime)
    return std::nullopt;
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
  InputCutoffBinding binding{
      nextCutoffIdentity<std::uint64_t>(), m_cutoffGeneration, capture, forwarding, viewerConnection
  };
  if (!validInputCutoffBinding(binding) || !remaining)
    return std::nullopt;
  if (m_cutoff)
    m_retiredCutoff = m_cutoff->binding;
  m_cutoff = Cutoff{binding, deadline};
  const auto bytes = encodeInputCutoffRequest({binding, static_cast<std::uint16_t>(remaining)});
  const auto alive = proxyLifetime();
  m_stream->write(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
  if (!*alive || !inputCutoffTlsLive() || !m_cutoff || m_cutoff->binding != binding || m_cutoff->resuming)
    return std::nullopt;
  return binding;
}

bool ClientProxy::resumeInputCutoff(const InputCutoffBinding &binding)
{
  if (!m_cutoff || m_cutoff->binding != binding || !inputCutoffTlsLive())
    return false;
  if (m_cutoff->resuming || m_cutoff->state == InputCutoffState::Resumed)
    return true;
  // Revoke the observation before the ordered write can reenter this proxy.
  m_cutoff->resuming = true;
  m_cutoff->state = InputCutoffState::Denied;
  const auto bytes = encodeInputCutoffResume(binding);
  const auto alive = proxyLifetime();
  m_stream->write(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
  return *alive && inputCutoffTlsLive() && m_cutoff && m_cutoff->binding == binding;
}

InputCutoffState ClientProxy::inputCutoffState(const InputCutoffBinding &binding)
{
  if (!m_cutoff || m_cutoff->binding != binding || !inputCutoffTlsLive())
    return InputCutoffState::Denied;
  if (m_cutoff->state == InputCutoffState::Resumed)
    return InputCutoffState::Resumed;
  if (InputCutoffClock::now() >= m_cutoff->deadline) {
    resumeInputCutoff(binding);
    return InputCutoffState::Denied;
  }
  return m_cutoff->resuming ? InputCutoffState::Denied : m_cutoff->state;
}

void ClientProxy::receiveInputCutoffReceipt(const InputCutoffReceipt &receipt)
{
  if (!inputCutoffTlsLive() || !validInputCutoffBinding(receipt.binding))
    throw BadClientException("Unauthenticated input cutoff receipt");
  if (m_retiredCutoff == receipt.binding)
    return;
  if (!m_cutoff || m_cutoff->binding != receipt.binding)
    throw BadClientException("Unknown input cutoff receipt binding");
  if (receipt.state == InputCutoffState::Resumed) {
    if (!m_cutoff->resuming && m_cutoff->state != InputCutoffState::Resumed)
      throw BadClientException("Unrequested input cutoff resume");
    m_cutoff->state = InputCutoffState::Resumed;
    m_cutoff->resuming = false;
    return;
  }
  if (m_cutoff->resuming || m_cutoff->state == InputCutoffState::Resumed)
    return;
  if (receipt.state != InputCutoffState::BackendObserved && receipt.state != InputCutoffState::Denied)
    throw BadClientException("Invalid input cutoff outcome");
  if (InputCutoffClock::now() >= m_cutoff->deadline) {
    resumeInputCutoff(receipt.binding);
    return;
  }
  if (m_cutoff->state == InputCutoffState::Denied && receipt.state != InputCutoffState::Denied)
    throw BadClientException("Revoked input cutoff observation revived");
  m_cutoff->state = receipt.state;
  if (receipt.state == InputCutoffState::Denied)
    resumeInputCutoff(receipt.binding);
}

void ClientProxy::retireInputCutoffCapability()
{
  if (m_cutoff) {
    const auto alive = proxyLifetime();
    const auto binding = m_cutoff->binding;
    if (!resumeInputCutoff(binding) || !*alive)
      return;
    m_retiredCutoff = binding;
    m_cutoff.reset();
  }
  m_cutoffGeneration = m_cutoffRequested = 0;
  m_cutoffAcknowledged = false;
}

void ClientProxy::writeControlPacket(const void *buffer, std::uint32_t size)
{
  const std::string_view packet(static_cast<const char *>(buffer), size);
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
