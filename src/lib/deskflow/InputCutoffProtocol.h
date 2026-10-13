/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "InputSequence.h"
#include "windowshare/InputCutoff.h"

namespace deskflow::windowshare {

inline constexpr std::size_t inputCutoffBindingBytes = 87;
inline constexpr std::size_t inputCutoffRequestBytes = inputCutoffBindingBytes + 2;
inline constexpr std::size_t inputCutoffReceiptBytes = inputCutoffBindingBytes + 1;
struct InputCutoffRequest
{
  InputCutoffBinding binding;
  std::uint16_t remainingMs = 0;
};
struct InputCutoffReceipt
{
  InputCutoffBinding binding;
  InputCutoffState state = InputCutoffState::Denied;
  bool operator==(const InputCutoffReceipt &) const = default;
};

namespace inputCutoffDetail {
inline void append(std::string &bytes, const InputCutoffBinding &binding)
{
  using inputSequenceDetail::append;
  append(bytes, binding.request, 8);
  append(bytes, binding.coreConnectionGeneration, 4);
  append(bytes, binding.capture.epoch, 8);
  append(bytes, binding.capture.sequence, 8);
  append(bytes, binding.capture.part, 2);
  const auto &route = binding.forwarding;
  for (const auto value :
       {route.owner.session, route.owner.epoch, route.generation, route.focusRequest, route.focusRevision,
        route.geometry})
    append(bytes, value, 8);
  append(bytes, static_cast<std::uint8_t>(route.scope), 1);
  append(bytes, binding.viewerConnection, 8);
}
inline std::optional<InputCutoffBinding> read(std::string_view bytes)
{
  if (bytes.size() != inputCutoffBindingBytes)
    return std::nullopt;
  using inputSequenceDetail::read;
  std::size_t offset = 0;
  InputCutoffBinding result;
  result.request = read(bytes, offset, 8);
  result.coreConnectionGeneration = static_cast<std::uint32_t>(read(bytes, offset, 4));
  result.capture.epoch = read(bytes, offset, 8);
  result.capture.sequence = read(bytes, offset, 8);
  result.capture.part = static_cast<std::uint16_t>(read(bytes, offset, 2));
  auto &route = result.forwarding;
  for (auto *value :
       {&route.owner.session, &route.owner.epoch, &route.generation, &route.focusRequest, &route.focusRevision,
        &route.geometry})
    *value = read(bytes, offset, 8);
  route.scope = static_cast<ForwardingScope>(read(bytes, offset, 1));
  result.viewerConnection = read(bytes, offset, 8);
  return validInputCutoffBinding(result) ? std::optional(result) : std::nullopt;
}
} // namespace inputCutoffDetail

inline std::string encodeInputCutoffRequest(const InputCutoffRequest &request)
{
  if (!validInputCutoffBinding(request.binding) || !request.remainingMs ||
      request.remainingMs > inputCutoffMaximumLifetime.count())
    throw std::invalid_argument("Invalid input cutoff request");
  std::string bytes("DICQ");
  inputCutoffDetail::append(bytes, request.binding);
  inputSequenceDetail::append(bytes, request.remainingMs, 2);
  return bytes;
}
inline std::optional<InputCutoffRequest> decodeInputCutoffRequest(std::string_view bytes)
{
  if (bytes.size() != inputCutoffRequestBytes)
    return std::nullopt;
  const auto binding = inputCutoffDetail::read(bytes.substr(0, inputCutoffBindingBytes));
  std::size_t offset = inputCutoffBindingBytes;
  const auto budget = inputSequenceDetail::read(bytes, offset, 2);
  if (!binding || !budget || budget > inputCutoffMaximumLifetime.count())
    return std::nullopt;
  return InputCutoffRequest{*binding, static_cast<std::uint16_t>(budget)};
}
inline std::string encodeInputCutoffResume(const InputCutoffBinding &binding)
{
  if (!validInputCutoffBinding(binding))
    throw std::invalid_argument("Invalid input cutoff resume");
  std::string bytes("DICX");
  inputCutoffDetail::append(bytes, binding);
  return bytes;
}
inline std::optional<InputCutoffBinding> decodeInputCutoffResume(std::string_view bytes)
{
  return inputCutoffDetail::read(bytes);
}
inline std::string encodeInputCutoffReceipt(const InputCutoffReceipt &receipt)
{
  if (!validInputCutoffBinding(receipt.binding) ||
      (receipt.state != InputCutoffState::BackendObserved && receipt.state != InputCutoffState::Denied &&
       receipt.state != InputCutoffState::Resumed))
    throw std::invalid_argument("Invalid input cutoff receipt");
  std::string bytes("CICR");
  inputCutoffDetail::append(bytes, receipt.binding);
  inputSequenceDetail::append(bytes, static_cast<std::uint8_t>(receipt.state), 1);
  return bytes;
}
inline std::optional<InputCutoffReceipt> decodeInputCutoffReceipt(std::string_view bytes)
{
  if (bytes.size() != inputCutoffReceiptBytes)
    return std::nullopt;
  const auto binding = inputCutoffDetail::read(bytes.substr(0, inputCutoffBindingBytes));
  const auto state = static_cast<InputCutoffState>(static_cast<unsigned char>(bytes.back()));
  if (!binding || (state != InputCutoffState::BackendObserved && state != InputCutoffState::Denied &&
                   state != InputCutoffState::Resumed))
    return std::nullopt;
  return InputCutoffReceipt{*binding, state};
}

} // namespace deskflow::windowshare
