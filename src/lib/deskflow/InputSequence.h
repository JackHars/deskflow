/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "windowshare/OriginalInputStamp.h"
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace deskflow::windowshare {

// Negotiated trace of the existing legacy dispatch. None of these values is
// original-input authority, a compositor fence, or application delivery proof.
struct InputSequenceToken
{
  std::uint32_t generation = 0;
  std::uint64_t wireSequence = 0;
  OriginalInputStamp capture;
  bool operator==(const InputSequenceToken &) const = default;
};
enum class InputSequenceOutcome : std::uint8_t
{
  Unsupported = 1,
  Filtered = 2,
  Submitted = 3,
  Expanded = 4,
  Failed = 5
};
struct InputSequenceReceipt
{
  std::uint32_t generation = 0;
  std::uint64_t wireSequence = 0;
  InputSequenceOutcome outcome = InputSequenceOutcome::Unsupported;
  std::uint16_t submissions = 0;
  bool operator==(const InputSequenceReceipt &) const = default;
};
struct InputSequenceEnvelope
{
  InputSequenceToken token;
  std::string payload;
};
inline constexpr std::size_t inputSequenceMaximumPayload = 1024;
inline constexpr std::size_t inputSequenceHeaderBytes = 30;
inline constexpr std::size_t inputSequenceMaximumBody = inputSequenceHeaderBytes + inputSequenceMaximumPayload;
inline constexpr std::size_t inputSequenceReceiptBodyBytes = 15;
inline constexpr std::size_t inputSequenceMaximumOutstanding = 256;

inline bool validInputSequenceToken(const InputSequenceToken &token) noexcept
{
  return token.generation && token.wireSequence && token.capture && token.capture.part &&
         token.capture.part <= maximumOriginalInputParts;
}
inline bool validInputSequenceReceipt(const InputSequenceReceipt &receipt) noexcept
{
  if (!receipt.generation || !receipt.wireSequence)
    return false;
  switch (receipt.outcome) {
  case InputSequenceOutcome::Unsupported:
  case InputSequenceOutcome::Filtered:
    return receipt.submissions == 0;
  case InputSequenceOutcome::Submitted:
    return receipt.submissions == 1;
  case InputSequenceOutcome::Expanded:
    return receipt.submissions > 1;
  case InputSequenceOutcome::Failed:
    return true;
  }
  return false;
}
namespace inputSequenceDetail {
inline void append(std::string &bytes, std::uint64_t value, unsigned count)
{
  for (unsigned index = count; index; --index)
    bytes.push_back(static_cast<char>((value >> ((index - 1) * 8)) & 0xff));
}
inline std::uint64_t read(std::string_view bytes, std::size_t &offset, unsigned count) noexcept
{
  std::uint64_t value = 0;
  for (unsigned index = 0; index < count; ++index)
    value = (value << 8) | static_cast<unsigned char>(bytes[offset++]);
  return value;
}
} // namespace inputSequenceDetail

// Only protocol 1.8 negotiates this extension. Validate full inner framing
// before allowing any existing parser to perform an input side effect. In
// particular DKRP has a language string in 1.8; a short historical layout is
// not accepted as an ambiguous alternative.
inline bool validInputSequencePayload(std::string_view payload) noexcept
{
  if (payload.size() < 4 || payload.size() > inputSequenceMaximumPayload)
    return false;
  const auto code = payload.substr(0, 4);
  if (code == "DMDN" || code == "DMUP")
    return payload.size() == 5;
  if (code == "DMMV" || code == "DMRM" || code == "DMWM")
    return payload.size() == 8;
  if (code == "DKDN" || code == "DKUP")
    return payload.size() == 10;
  if (code != "DKDL" && code != "DKRP")
    return false;
  std::size_t offset = code == "DKDL" ? 10 : 12;
  if (payload.size() < offset + 4)
    return false;
  const auto length = inputSequenceDetail::read(payload, offset, 4);
  return length == payload.size() - offset;
}

// Full frame: DSEQ, uint16 body length, generation:u32, wire sequence:u64,
// capture epoch:u64, capture sequence:u64, capture part:u16, legacy message.
// All integers use network byte order. No generic unbounded string decoder.
inline std::string encodeInputSequenceEnvelope(const InputSequenceToken &token, std::string_view payload)
{
  if (!validInputSequenceToken(token) || !validInputSequencePayload(payload))
    throw std::invalid_argument("Invalid input sequence envelope");
  std::string bytes("DSEQ");
  bytes.reserve(6 + inputSequenceHeaderBytes + payload.size());
  inputSequenceDetail::append(bytes, inputSequenceHeaderBytes + payload.size(), 2);
  inputSequenceDetail::append(bytes, token.generation, 4);
  inputSequenceDetail::append(bytes, token.wireSequence, 8);
  inputSequenceDetail::append(bytes, token.capture.epoch, 8);
  inputSequenceDetail::append(bytes, token.capture.sequence, 8);
  inputSequenceDetail::append(bytes, token.capture.part, 2);
  bytes.append(payload);
  return bytes;
}
// Caller reads/bounds the uint16 body length before allocating or reading it.
inline std::optional<InputSequenceEnvelope> decodeInputSequenceEnvelope(std::string_view body)
{
  if (body.size() < inputSequenceHeaderBytes + 4 || body.size() > inputSequenceMaximumBody)
    return std::nullopt;
  InputSequenceEnvelope result;
  std::size_t offset = 0;
  result.token.generation = static_cast<std::uint32_t>(inputSequenceDetail::read(body, offset, 4));
  result.token.wireSequence = inputSequenceDetail::read(body, offset, 8);
  result.token.capture.epoch = inputSequenceDetail::read(body, offset, 8);
  result.token.capture.sequence = inputSequenceDetail::read(body, offset, 8);
  result.token.capture.part = static_cast<std::uint16_t>(inputSequenceDetail::read(body, offset, 2));
  const auto payload = body.substr(offset);
  if (!validInputSequenceToken(result.token) || !validInputSequencePayload(payload))
    return std::nullopt;
  result.payload = payload;
  return result;
}
// Full fixed frame: CISR, generation:u32, wire sequence:u64, outcome:u8,
// submission count:u16. The sender joins it to its retained exact capture stamp.
inline std::string encodeInputSequenceReceipt(const InputSequenceReceipt &receipt)
{
  if (!validInputSequenceReceipt(receipt))
    throw std::invalid_argument("Invalid input sequence receipt");
  std::string bytes("CISR");
  inputSequenceDetail::append(bytes, receipt.generation, 4);
  inputSequenceDetail::append(bytes, receipt.wireSequence, 8);
  inputSequenceDetail::append(bytes, static_cast<std::uint8_t>(receipt.outcome), 1);
  inputSequenceDetail::append(bytes, receipt.submissions, 2);
  return bytes;
}
inline std::optional<InputSequenceReceipt> decodeInputSequenceReceipt(std::string_view body) noexcept
{
  if (body.size() != inputSequenceReceiptBodyBytes)
    return std::nullopt;
  InputSequenceReceipt result;
  std::size_t offset = 0;
  result.generation = static_cast<std::uint32_t>(inputSequenceDetail::read(body, offset, 4));
  result.wireSequence = inputSequenceDetail::read(body, offset, 8);
  result.outcome = static_cast<InputSequenceOutcome>(inputSequenceDetail::read(body, offset, 1));
  result.submissions = static_cast<std::uint16_t>(inputSequenceDetail::read(body, offset, 2));
  return validInputSequenceReceipt(result) ? std::optional(result) : std::nullopt;
}

// Server event-thread scope. It annotates only actual input writes inside this
// event's routing; it neither reconstructs capture from translated keys nor
// treats multiple dispatches from one captured record as one delivery.
class InputSequenceOriginScope final
{
public:
  explicit InputSequenceOriginScope(OriginalInputStamp stamp) noexcept : m_previous(s_current), m_stamp(stamp)
  {
    s_current = this;
  }
  ~InputSequenceOriginScope()
  {
    s_current = m_previous;
  }
  InputSequenceOriginScope(const InputSequenceOriginScope &) = delete;
  InputSequenceOriginScope &operator=(const InputSequenceOriginScope &) = delete;
  static OriginalInputStamp current() noexcept
  {
    return s_current ? s_current->m_stamp : OriginalInputStamp{};
  }

private:
  inline static thread_local InputSequenceOriginScope *s_current = nullptr;
  InputSequenceOriginScope *m_previous;
  OriginalInputStamp m_stamp;
};

// Client dispatch-thread scope, never installed in an event tap. Count only
// actual native-backend API submissions. A submitted libei frame is not a PONG,
// a particular Clutter device/event, a Qt receipt or original-input authority.
class InputSequenceSubmissionScope final
{
public:
  explicit InputSequenceSubmissionScope(InputSequenceToken token) noexcept
      : m_previous(s_current),
        m_token(token),
        m_exceptions(std::uncaught_exceptions())
  {
    if (m_previous) {
      m_previous->m_failed = true;
      m_failed = true;
    }
    if (!validInputSequenceToken(token))
      m_failed = true;
    s_current = this;
  }
  ~InputSequenceSubmissionScope()
  {
    s_current = m_previous;
  }
  InputSequenceSubmissionScope(const InputSequenceSubmissionScope &) = delete;
  InputSequenceSubmissionScope &operator=(const InputSequenceSubmissionScope &) = delete;
  static std::optional<InputSequenceToken> currentToken() noexcept
  {
    return s_current ? std::optional(s_current->m_token) : std::nullopt;
  }
  static void backendParticipated() noexcept
  {
    if (s_current)
      s_current->m_backend = true;
  }
  // A known client gate can consume a packet before any platform call. Keep
  // that explicit filtering distinct from an uninstrumented backend.
  static void filtered() noexcept
  {
    if (s_current)
      s_current->m_filtered = true;
  }
  static void submitted() noexcept
  {
    if (!s_current)
      return;
    s_current->m_backend = true;
    if (s_current->m_submissions == std::numeric_limits<std::uint16_t>::max())
      s_current->m_failed = true;
    else
      ++s_current->m_submissions;
  }
  static void fail() noexcept
  {
    if (s_current)
      s_current->m_failed = true;
  }
  InputSequenceReceipt receipt() const noexcept
  {
    const auto outcome = m_failed || (m_filtered && m_submissions) || std::uncaught_exceptions() > m_exceptions
                             ? InputSequenceOutcome::Failed
                         : !m_backend && !m_filtered ? InputSequenceOutcome::Unsupported
                         : !m_submissions            ? InputSequenceOutcome::Filtered
                         : m_submissions == 1        ? InputSequenceOutcome::Submitted
                                                     : InputSequenceOutcome::Expanded;
    return {m_token.generation, m_token.wireSequence, outcome, m_submissions};
  }

private:
  inline static thread_local InputSequenceSubmissionScope *s_current = nullptr;
  InputSequenceSubmissionScope *m_previous;
  InputSequenceToken m_token;
  int m_exceptions;
  std::uint16_t m_submissions = 0;
  bool m_backend = false, m_filtered = false, m_failed = false;
};

} // namespace deskflow::windowshare
