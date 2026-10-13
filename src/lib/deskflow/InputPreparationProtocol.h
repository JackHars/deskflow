/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "InputCutoffProtocol.h"
#include "windowshare/InputPreparation.h"

namespace deskflow::windowshare {
inline constexpr std::size_t inputPreparationBindingBytes = 30;
inline constexpr std::size_t inputPreparationRequestBytes = 40;
inline constexpr std::size_t inputPreparationResultBytes = 118;
inline constexpr std::size_t inputPreparationPromotionBytes = 119;
inline constexpr std::size_t inputPreparationCancelBytes = 32;
inline constexpr std::size_t inputPreparationReceiptBytes = 32;
struct InputPreparationRequest
{
  InputPreparationBinding binding;
  InputCutoffPoint point;
  std::uint16_t remainingMs = 0;
};
struct InputPreparationPromotion
{
  InputPreparationBinding preparation;
  InputCutoffBinding cutoff;
  std::uint16_t remainingMs = 0;
};
struct InputPreparationCancellation
{
  InputPreparationBinding binding;
  std::uint16_t remainingMs = 0;
};
struct InputPreparationReceipt
{
  InputPreparationBinding binding;
  InputPreparationState state = InputPreparationState::Failed;
  InputPreparationOutcome outcome = InputPreparationOutcome::Failed;
  bool operator==(const InputPreparationReceipt &) const = default;
};
namespace inputPreparationDetail {
inline void append(std::string &bytes, const InputPreparationBinding &binding)
{
  using inputSequenceDetail::append;
  append(bytes, binding.request, 8);
  append(bytes, binding.coreConnectionGeneration, 4);
  append(bytes, binding.capture.epoch, 8);
  append(bytes, binding.capture.sequence, 8);
  append(bytes, binding.capture.part, 2);
}
inline std::optional<InputPreparationBinding> read(std::string_view bytes)
{
  if (bytes.size() != inputPreparationBindingBytes)
    return std::nullopt;
  using inputSequenceDetail::read;
  std::size_t at = 0;
  InputPreparationBinding value;
  value.request = read(bytes, at, 8);
  value.coreConnectionGeneration = static_cast<std::uint32_t>(read(bytes, at, 4));
  value.capture.epoch = read(bytes, at, 8);
  value.capture.sequence = read(bytes, at, 8);
  value.capture.part = static_cast<std::uint16_t>(read(bytes, at, 2));
  return validInputPreparationBinding(value) ? std::optional(value) : std::nullopt;
}
inline bool budget(std::uint64_t value)
{
  return value && value <= inputCutoffMaximumLifetime.count();
}
} // namespace inputPreparationDetail
inline std::string encodeInputPreparationRequest(const InputPreparationRequest &value)
{
  if (!validInputPreparationBinding(value.binding) || !validInputCutoffPoint(value.point) ||
      (!value.remainingMs || value.remainingMs > inputPreparationMaximumLifetime.count()))
    throw std::invalid_argument("Invalid input preparation request");
  std::string bytes("DIPP");
  inputPreparationDetail::append(bytes, value.binding);
  inputSequenceDetail::append(bytes, std::bit_cast<std::uint32_t>(value.point.x), 4);
  inputSequenceDetail::append(bytes, std::bit_cast<std::uint32_t>(value.point.y), 4);
  inputSequenceDetail::append(bytes, value.remainingMs, 2);
  return bytes;
}
inline std::optional<InputPreparationRequest> decodeInputPreparationRequest(std::string_view bytes)
{
  if (bytes.size() != inputPreparationRequestBytes)
    return std::nullopt;
  const auto binding = inputPreparationDetail::read(bytes.substr(0, inputPreparationBindingBytes));
  std::size_t at = inputPreparationBindingBytes;
  InputCutoffPoint point{
      std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(inputSequenceDetail::read(bytes, at, 4))),
      std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(inputSequenceDetail::read(bytes, at, 4)))
  };
  const auto budget = inputSequenceDetail::read(bytes, at, 2);
  if (!binding || !validInputCutoffPoint(point) || !budget || budget > inputPreparationMaximumLifetime.count())
    return std::nullopt;
  return InputPreparationRequest{*binding, point, static_cast<std::uint16_t>(budget)};
}
inline std::string encodePreparedInputFocusResult(const PreparedInputFocusResult &value)
{
  if (!validPreparedInputFocusResult(value))
    throw std::invalid_argument("Invalid prepared focus");
  std::string bytes("CIPF");
  inputPreparationDetail::append(bytes, value.binding);
  using inputSequenceDetail::append;
  append(bytes, std::bit_cast<std::uint32_t>(value.point.x), 4);
  append(bytes, std::bit_cast<std::uint32_t>(value.point.y), 4);
  for (const auto v :
       {value.viewerConnection, value.owner.session, value.owner.epoch, value.geometry, value.focusRequest,
        value.focusRevision})
    append(bytes, v, 8);
  for (auto byte : value.viewerPeerFingerprint)
    append(bytes, byte, 1);
  return bytes;
}
inline std::optional<PreparedInputFocusResult> decodePreparedInputFocusResult(std::string_view bytes)
{
  if (bytes.size() != inputPreparationResultBytes)
    return std::nullopt;
  const auto binding = inputPreparationDetail::read(bytes.substr(0, inputPreparationBindingBytes));
  if (!binding)
    return std::nullopt;
  using inputSequenceDetail::read;
  std::size_t at = inputPreparationBindingBytes;
  PreparedInputFocusResult value;
  value.binding = *binding;
  value.point = {
      std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(read(bytes, at, 4))),
      std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(read(bytes, at, 4)))
  };
  for (auto *v :
       {&value.viewerConnection, &value.owner.session, &value.owner.epoch, &value.geometry, &value.focusRequest,
        &value.focusRevision})
    *v = read(bytes, at, 8);
  for (auto &byte : value.viewerPeerFingerprint)
    byte = static_cast<std::uint8_t>(read(bytes, at, 1));
  return validPreparedInputFocusResult(value) ? std::optional(value) : std::nullopt;
}
inline std::string encodeInputPreparationPromotion(const InputPreparationPromotion &value)
{
  if (!validInputPreparationBinding(value.preparation) || !validInputCutoffBinding(value.cutoff) ||
      value.preparation.coreConnectionGeneration != value.cutoff.coreConnectionGeneration ||
      value.preparation.capture != value.cutoff.capture || value.cutoff.request <= value.preparation.request ||
      !inputPreparationDetail::budget(value.remainingMs))
    throw std::invalid_argument("Invalid preparation promotion");
  std::string bytes("DIPM");
  inputPreparationDetail::append(bytes, value.preparation);
  inputCutoffDetail::append(bytes, value.cutoff);
  inputSequenceDetail::append(bytes, value.remainingMs, 2);
  return bytes;
}
inline std::optional<InputPreparationPromotion> decodeInputPreparationPromotion(std::string_view bytes)
{
  if (bytes.size() != inputPreparationPromotionBytes)
    return std::nullopt;
  const auto preparation = inputPreparationDetail::read(bytes.substr(0, inputPreparationBindingBytes));
  const auto cutoff = inputCutoffDetail::read(bytes.substr(inputPreparationBindingBytes, inputCutoffBindingBytes));
  std::size_t at = inputPreparationBindingBytes + inputCutoffBindingBytes;
  const auto budget = inputSequenceDetail::read(bytes, at, 2);
  if (!preparation || !cutoff || preparation->coreConnectionGeneration != cutoff->coreConnectionGeneration ||
      preparation->capture != cutoff->capture || cutoff->request <= preparation->request ||
      !inputPreparationDetail::budget(budget))
    return std::nullopt;
  return InputPreparationPromotion{*preparation, *cutoff, static_cast<std::uint16_t>(budget)};
}
inline std::string encodeInputPreparationCancellation(const InputPreparationCancellation &value)
{
  if (!validInputPreparationBinding(value.binding) || !inputPreparationDetail::budget(value.remainingMs))
    throw std::invalid_argument("Invalid preparation cancellation");
  std::string bytes("DIPX");
  inputPreparationDetail::append(bytes, value.binding);
  inputSequenceDetail::append(bytes, value.remainingMs, 2);
  return bytes;
}
inline std::optional<InputPreparationCancellation> decodeInputPreparationCancellation(std::string_view bytes)
{
  if (bytes.size() != inputPreparationCancelBytes)
    return std::nullopt;
  const auto binding = inputPreparationDetail::read(bytes.substr(0, inputPreparationBindingBytes));
  std::size_t at = inputPreparationBindingBytes;
  const auto budget = inputSequenceDetail::read(bytes, at, 2);
  if (!binding || !inputPreparationDetail::budget(budget))
    return std::nullopt;
  return InputPreparationCancellation{*binding, static_cast<std::uint16_t>(budget)};
}
inline bool validInputPreparationReceipt(const InputPreparationReceipt &value)
{
  return validInputPreparationBinding(value.binding) &&
         (value.state == InputPreparationState::Failed || value.state == InputPreparationState::Released) &&
         value.outcome <= InputPreparationOutcome::Failed &&
         (value.state != InputPreparationState::Failed || value.outcome != InputPreparationOutcome::None);
}
inline std::string encodeInputPreparationReceipt(const InputPreparationReceipt &value)
{
  if (!validInputPreparationReceipt(value))
    throw std::invalid_argument("Invalid preparation receipt");
  std::string bytes("CIPR");
  inputPreparationDetail::append(bytes, value.binding);
  inputSequenceDetail::append(bytes, static_cast<std::uint8_t>(value.state), 1);
  inputSequenceDetail::append(bytes, static_cast<std::uint8_t>(value.outcome), 1);
  return bytes;
}
inline std::optional<InputPreparationReceipt> decodeInputPreparationReceipt(std::string_view bytes)
{
  if (bytes.size() != inputPreparationReceiptBytes)
    return std::nullopt;
  const auto binding = inputPreparationDetail::read(bytes.substr(0, inputPreparationBindingBytes));
  if (!binding)
    return std::nullopt;
  std::size_t at = inputPreparationBindingBytes;
  InputPreparationReceipt value{
      *binding, static_cast<InputPreparationState>(inputSequenceDetail::read(bytes, at, 1)),
      static_cast<InputPreparationOutcome>(inputSequenceDetail::read(bytes, at, 1))
  };
  return validInputPreparationReceipt(value) ? std::optional(value) : std::nullopt;
}
} // namespace deskflow::windowshare
