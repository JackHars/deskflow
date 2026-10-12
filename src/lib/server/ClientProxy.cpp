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
#include <limits>

using namespace deskflow::windowshare;

namespace {
void increment(std::uint64_t &counter) noexcept
{
  if (counter != std::numeric_limits<std::uint64_t>::max())
    ++counter;
}
} // namespace

// ProtocolUtil performs exactly one write for each complete legacy message.
// Intercept that write, leaving every version's existing encoder unchanged.
// Control, enter, clipboard and heartbeat messages use the original stream.
class ClientProxy::InputStream final : public deskflow::IStream
{
public:
  explicit InputStream(ClientProxy &owner) : m_owner(owner)
  {
  }
  void write(const void *buffer, std::uint32_t size) override
  {
    m_owner.writeInputPacket(buffer, size);
  }
  void close() override
  {
    m_owner.getStream()->close();
  }
  std::uint32_t read(void *buffer, std::uint32_t size) override
  {
    return m_owner.getStream()->read(buffer, size);
  }
  void flush() override
  {
    m_owner.getStream()->flush();
  }
  void shutdownInput() override
  {
    m_owner.getStream()->shutdownInput();
  }
  void shutdownOutput() override
  {
    m_owner.getStream()->shutdownOutput();
  }
  void *getEventTarget() const override
  {
    return m_owner.getStream()->getEventTarget();
  }
  bool isReady() const override
  {
    return m_owner.getStream()->isReady();
  }
  std::uint32_t getSize() const override
  {
    return m_owner.getStream()->getSize();
  }

private:
  ClientProxy &m_owner;
};

//
// ClientProxy
//

ClientProxy::ClientProxy(const std::string &name, deskflow::IStream *stream)
    : BaseClientProxy(name),
      m_stream(stream),
      m_inputStream(std::make_unique<InputStream>(*this))
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
  return m_stream;
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
  OptionsList result;
  result.reserve(options.size());
  bool found = false;
  for (std::size_t index = 0; index < options.size(); index += 2) {
    const auto id = options[index], value = options[index + 1];
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
