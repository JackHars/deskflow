/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "InputSequence.h"
#include "windowshare/InputCutoff.h"
#include <bit>

namespace deskflow::windowshare {

inline constexpr std::size_t inputCutoffBindingBytes = 87;
inline constexpr std::size_t inputCutoffRequestBytes = inputCutoffBindingBytes + 2;
inline constexpr std::size_t inputCutoffReceiptBytes = inputCutoffBindingBytes + 1;
inline constexpr std::size_t inputCutoffMappedRequestBytes = inputCutoffRequestBytes + 8;
inline constexpr std::size_t inputCutoffMappedResultBytes = inputCutoffBindingBytes + 8 + 32 + 16 + 12 + 32 + 129;
inline constexpr std::size_t inputCutoffMapRequestBytes = inputCutoffMappedRequestBytes + 9;
inline constexpr std::size_t inputCutoffSealedResultBytes = inputCutoffMappedResultBytes + 8;
inline constexpr std::size_t inputCutoffBatchHeaderBytes = 9;
inline constexpr std::size_t inputCutoffBatchItemBytes = inputCutoffMappedRequestBytes + 1;
inline constexpr std::size_t inputCutoffBatchRequestMaximumBytes =
    inputCutoffBatchHeaderBytes + inputCutoffMaximumBatch * inputCutoffBatchItemBytes;
inline constexpr std::size_t inputCutoffBatchResultMaximumBytes =
    inputCutoffBatchHeaderBytes + inputCutoffMaximumBatch * inputCutoffMappedResultBytes;
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
struct InputCutoffMappedRequest
{
  InputCutoffRequest cutoff;
  InputCutoffPoint point;
};
struct InputCutoffMapRequest
{
  InputCutoffMappedRequest mapping;
  std::uint64_t sealRequest = 0;
  bool presentCursor = false;
};
struct InputCutoffSealedResult
{
  InputCutoffMappedResult mapping;
  std::uint64_t sealRequest = 0;
};

struct InputCutoffBatchRequest
{
  std::uint64_t sealRequest = 0;
  std::vector<InputCutoffMapRequest> items;
};
struct InputCutoffBatchResult
{
  std::uint64_t sealRequest = 0;
  std::vector<InputCutoffMappedResult> items;
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
inline std::string encodeInputCutoffMappedRequest(const InputCutoffMappedRequest &request)
{
  if (!validInputCutoffPoint(request.point))
    throw std::invalid_argument("Invalid input cutoff point");
  auto bytes = encodeInputCutoffRequest(request.cutoff);
  bytes.replace(0, 4, "DICM");
  inputSequenceDetail::append(bytes, std::bit_cast<std::uint32_t>(request.point.x), 4);
  inputSequenceDetail::append(bytes, std::bit_cast<std::uint32_t>(request.point.y), 4);
  return bytes;
}
inline std::optional<InputCutoffMappedRequest> decodeInputCutoffMappedRequest(std::string_view bytes)
{
  if (bytes.size() != inputCutoffMappedRequestBytes)
    return std::nullopt;
  const auto cutoff = decodeInputCutoffRequest(bytes.substr(0, inputCutoffRequestBytes));
  std::size_t offset = inputCutoffRequestBytes;
  const InputCutoffPoint point{
      std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(inputSequenceDetail::read(bytes, offset, 4))),
      std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(inputSequenceDetail::read(bytes, offset, 4)))
  };
  if (!cutoff || !validInputCutoffPoint(point))
    return std::nullopt;
  return InputCutoffMappedRequest{*cutoff, point};
}
inline std::string encodeInputCutoffMappedResult(const InputCutoffMappedResult &result)
{
  if (!validInputCutoffMappedResult(result))
    throw std::invalid_argument("Invalid input cutoff mapping");
  using inputSequenceDetail::append;
  std::string bytes("CIMR");
  inputCutoffDetail::append(bytes, result.binding);
  append(bytes, std::bit_cast<std::uint32_t>(result.point.x), 4);
  append(bytes, std::bit_cast<std::uint32_t>(result.point.y), 4);
  for (auto value : {result.sourceX, result.sourceY, result.sourceWidth, result.sourceHeight})
    append(bytes, std::bit_cast<std::uint64_t>(value), 8);
  append(bytes, result.surfaceIncarnation, 8);
  append(bytes, result.viewportRevision, 8);
  for (auto value : {result.nativeSurface, result.providerGeneration, result.nativeRevision})
    append(bytes, value, 4);
  for (auto value : result.viewerPeerFingerprint)
    append(bytes, value, 1);
  append(bytes, result.orderingContract.size(), 1);
  bytes.append(result.orderingContract);
  bytes.append(128 - result.orderingContract.size(), '\0');
  return bytes;
}
inline std::optional<InputCutoffMappedResult> decodeInputCutoffMappedResult(std::string_view bytes)
{
  if (bytes.size() != inputCutoffMappedResultBytes)
    return std::nullopt;
  const auto binding = inputCutoffDetail::read(bytes.substr(0, inputCutoffBindingBytes));
  if (!binding)
    return std::nullopt;
  using inputSequenceDetail::read;
  std::size_t offset = inputCutoffBindingBytes;
  InputCutoffMappedResult result;
  result.binding = *binding;
  result.point.x = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(read(bytes, offset, 4)));
  result.point.y = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(read(bytes, offset, 4)));
  for (auto *value : {&result.sourceX, &result.sourceY, &result.sourceWidth, &result.sourceHeight})
    *value = std::bit_cast<double>(read(bytes, offset, 8));
  result.surfaceIncarnation = read(bytes, offset, 8);
  result.viewportRevision = read(bytes, offset, 8);
  for (auto *value : {&result.nativeSurface, &result.providerGeneration, &result.nativeRevision})
    *value = static_cast<std::uint32_t>(read(bytes, offset, 4));
  for (auto &value : result.viewerPeerFingerprint)
    value = static_cast<std::uint8_t>(read(bytes, offset, 1));
  const auto length = read(bytes, offset, 1);
  if (!length || length > 128)
    return std::nullopt;
  result.orderingContract = bytes.substr(offset, length);
  if (!std::all_of(bytes.begin() + offset + length, bytes.end(), [](char c) { return c == '\0'; }))
    return std::nullopt;
  return validInputCutoffMappedResult(result) ? std::optional(result) : std::nullopt;
}
inline std::string encodeInputCutoffSealedRequest(const InputCutoffMappedRequest &request)
{
  auto bytes = encodeInputCutoffMappedRequest(request);
  bytes.replace(0, 4, "DICS");
  return bytes;
}
inline std::string encodeInputCutoffMapRequest(const InputCutoffMapRequest &request)
{
  if (!request.sealRequest || request.mapping.cutoff.binding.request <= request.sealRequest)
    throw std::invalid_argument("Invalid input cutoff map seal");
  auto bytes = encodeInputCutoffMappedRequest(request.mapping);
  bytes.replace(0, 4, "DICF");
  inputSequenceDetail::append(bytes, request.sealRequest, 8);
  inputSequenceDetail::append(bytes, request.presentCursor ? 1 : 0, 1);
  return bytes;
}
inline std::optional<InputCutoffMapRequest> decodeInputCutoffMapRequest(std::string_view bytes)
{
  if (bytes.size() != inputCutoffMapRequestBytes)
    return std::nullopt;
  const auto mapping = decodeInputCutoffMappedRequest(bytes.substr(0, inputCutoffMappedRequestBytes));
  std::size_t offset = inputCutoffMappedRequestBytes;
  const auto seal = inputSequenceDetail::read(bytes, offset, 8);
  const auto cursor = inputSequenceDetail::read(bytes, offset, 1);
  if (!mapping || !seal || mapping->cutoff.binding.request <= seal || cursor > 1)
    return std::nullopt;
  return InputCutoffMapRequest{*mapping, seal, bool(cursor)};
}
inline std::string encodeInputCutoffSealedResult(const InputCutoffSealedResult &result)
{
  if (!result.sealRequest || result.sealRequest > result.mapping.binding.request)
    throw std::invalid_argument("Invalid input cutoff result seal");
  auto bytes = encodeInputCutoffMappedResult(result.mapping);
  bytes.replace(0, 4, "CIMS");
  inputSequenceDetail::append(bytes, result.sealRequest, 8);
  return bytes;
}
inline std::optional<InputCutoffSealedResult> decodeInputCutoffSealedResult(std::string_view bytes)
{
  if (bytes.size() != inputCutoffSealedResultBytes)
    return std::nullopt;
  const auto mapping = decodeInputCutoffMappedResult(bytes.substr(0, inputCutoffMappedResultBytes));
  std::size_t offset = inputCutoffMappedResultBytes;
  const auto seal = inputSequenceDetail::read(bytes, offset, 8);
  if (!mapping || !seal || seal > mapping->binding.request)
    return std::nullopt;
  return InputCutoffSealedResult{*mapping, seal};
}

inline std::optional<InputCutoffBatchRequest> decodeInputCutoffBatchRequest(std::string_view bytes)
{
  if (bytes.size() < inputCutoffBatchHeaderBytes || bytes.size() > inputCutoffBatchRequestMaximumBytes)
    return std::nullopt;
  std::size_t offset = 0;
  InputCutoffBatchRequest result;
  result.sealRequest = inputSequenceDetail::read(bytes, offset, 8);
  const auto count = inputSequenceDetail::read(bytes, offset, 1);
  if (!result.sealRequest || !count || count > inputCutoffMaximumBatch ||
      bytes.size() != inputCutoffBatchHeaderBytes + count * inputCutoffBatchItemBytes)
    return std::nullopt;
  for (std::size_t index = 0; index < count; ++index) {
    const auto mapping = decodeInputCutoffMappedRequest(bytes.substr(offset, inputCutoffMappedRequestBytes));
    offset += inputCutoffMappedRequestBytes;
    const auto cursor = inputSequenceDetail::read(bytes, offset, 1);
    if (!mapping || cursor > 1 || mapping->cutoff.binding.request <= result.sealRequest ||
        (!result.items.empty() &&
         !continuesInputCutoffSeal(result.items.back().mapping.cutoff.binding, mapping->cutoff.binding)))
      return std::nullopt;
    result.items.push_back({*mapping, result.sealRequest, bool(cursor)});
  }
  return result;
}
inline std::string encodeInputCutoffBatchRequest(const InputCutoffBatchRequest &request)
{
  if (request.items.empty() || request.items.size() > inputCutoffMaximumBatch)
    throw std::invalid_argument("Invalid input cutoff batch size");
  std::string bytes("DICB");
  inputSequenceDetail::append(bytes, request.sealRequest, 8);
  inputSequenceDetail::append(bytes, request.items.size(), 1);
  for (const auto &item : request.items) {
    if (item.sealRequest != request.sealRequest)
      throw std::invalid_argument("Mixed input cutoff batch seal");
    bytes.append(encodeInputCutoffMappedRequest(item.mapping).substr(4));
    inputSequenceDetail::append(bytes, item.presentCursor ? 1 : 0, 1);
  }
  if (!decodeInputCutoffBatchRequest(std::string_view(bytes).substr(4)))
    throw std::invalid_argument("Invalid input cutoff batch request");
  return bytes;
}
inline std::optional<InputCutoffBatchResult> decodeInputCutoffBatchResult(std::string_view bytes)
{
  if (bytes.size() < inputCutoffBatchHeaderBytes || bytes.size() > inputCutoffBatchResultMaximumBytes)
    return std::nullopt;
  std::size_t offset = 0;
  InputCutoffBatchResult result;
  result.sealRequest = inputSequenceDetail::read(bytes, offset, 8);
  const auto count = inputSequenceDetail::read(bytes, offset, 1);
  if (!result.sealRequest || !count || count > inputCutoffMaximumBatch ||
      bytes.size() != inputCutoffBatchHeaderBytes + count * inputCutoffMappedResultBytes)
    return std::nullopt;
  for (std::size_t index = 0; index < count; ++index) {
    const auto item = decodeInputCutoffMappedResult(bytes.substr(offset, inputCutoffMappedResultBytes));
    offset += inputCutoffMappedResultBytes;
    if (!item || item->binding.request <= result.sealRequest ||
        (!result.items.empty() && (!continuesInputCutoffSeal(result.items.back().binding, item->binding) ||
                                   !sameInputCutoffMappedIdentity(result.items.front(), *item))))
      return std::nullopt;
    result.items.push_back(*item);
  }
  return result;
}
inline std::string encodeInputCutoffBatchResult(const InputCutoffBatchResult &result)
{
  if (result.items.empty() || result.items.size() > inputCutoffMaximumBatch)
    throw std::invalid_argument("Invalid input cutoff batch result size");
  std::string bytes("CIMB");
  inputSequenceDetail::append(bytes, result.sealRequest, 8);
  inputSequenceDetail::append(bytes, result.items.size(), 1);
  for (const auto &item : result.items)
    bytes.append(encodeInputCutoffMappedResult(item).substr(4));
  if (!decodeInputCutoffBatchResult(std::string_view(bytes).substr(4)))
    throw std::invalid_argument("Invalid input cutoff batch result");
  return bytes;
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
