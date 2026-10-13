/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "InputCutoff.h"

namespace deskflow::windowshare {

inline constexpr auto inputPreparationMaximumLifetime = std::chrono::milliseconds(500);

// A pre-focus request has no Viewer connection or forwarding lease yet. The
// authenticated backend discovers those actual identities under a separate
// closed preparation gate. This binding never grants native input authority.
struct InputPreparationBinding
{
  std::uint64_t request = 0;
  std::uint32_t coreConnectionGeneration = 0;
  OriginalInputStamp capture;
  bool operator==(const InputPreparationBinding &) const = default;
};

inline bool validInputPreparationBinding(const InputPreparationBinding &binding) noexcept
{
  return binding.request && binding.coreConnectionGeneration && binding.capture && binding.capture.part &&
         binding.capture.part <= maximumOriginalInputParts;
}

enum class InputPreparationState : std::uint8_t
{
  Preparing,
  Prepared,
  Promoting,
  Sealed,
  Canceling,
  Released,
  Failed
};

enum class InputPreparationOutcome : std::uint8_t
{
  None,
  NoTarget,
  Unsupported,
  Failed
};

// Only the identities needed to match a genuine Host lease cross Core TLS.
// Native hit, painted mapping and drain proof remain retained by the provider;
// promotion must independently return the ordinary exact mapped seal receipt.
struct PreparedInputFocusResult
{
  InputPreparationBinding binding;
  InputCutoffPoint point;
  std::uint64_t viewerConnection = 0;
  Owner owner;
  std::uint64_t geometry = 0, focusRequest = 0, focusRevision = 0;
  deskflow::AuthenticatedPeerTicket::Fingerprint viewerPeerFingerprint{};
  bool operator==(const PreparedInputFocusResult &) const = default;
};

inline bool validPreparedInputFocusResult(const PreparedInputFocusResult &result) noexcept
{
  constexpr std::uint64_t maximum = (std::uint64_t{1} << 53) - 1;
  if (!validInputPreparationBinding(result.binding) || !validInputCutoffPoint(result.point) ||
      !result.viewerConnection ||
      !std::any_of(result.viewerPeerFingerprint.begin(), result.viewerPeerFingerprint.end(), [](auto value) {
        return value != 0;
      }))
    return false;
  for (const auto value :
       {result.owner.session, result.owner.epoch, result.geometry, result.focusRequest, result.focusRevision})
    if (!value || value > maximum)
      return false;
  return true;
}

inline bool
matchesInputPreparationPromotion(const PreparedInputFocusResult &prepared, const InputCutoffBinding &cutoff) noexcept
{
  return validPreparedInputFocusResult(prepared) && validInputCutoffBinding(cutoff) &&
         cutoff.request > prepared.binding.request &&
         cutoff.coreConnectionGeneration == prepared.binding.coreConnectionGeneration &&
         cutoff.capture == prepared.binding.capture && cutoff.viewerConnection == prepared.viewerConnection &&
         cutoff.forwarding.owner == prepared.owner && cutoff.forwarding.geometry == prepared.geometry &&
         cutoff.forwarding.focusRequest == prepared.focusRequest &&
         cutoff.forwarding.focusRevision == prepared.focusRevision;
}

class PreparedInputFocus final
{
public:
  // Continuity of the exact preparation only. Sealed is an expected transfer,
  // not revocation; this handle still expires at its original deadline. New
  // native delivery always requires the separately issued mapped observation.
  bool live() const noexcept
  {
    if (!m_state || !m_peer.live() || InputCutoffClock::now() >= m_deadline)
      return false;
    const auto state = m_state->load(std::memory_order_acquire);
    return state == InputPreparationState::Prepared || state == InputPreparationState::Promoting ||
           state == InputPreparationState::Sealed;
  }
  const PreparedInputFocusResult &snapshot() const noexcept
  {
    return m_result;
  }
  const InputPreparationBinding &binding() const noexcept
  {
    return m_result.binding;
  }
  InputCutoffClock::time_point deadline() const noexcept
  {
    return m_deadline;
  }

private:
  friend class ::ClientProxy;
  PreparedInputFocus(
      PreparedInputFocusResult result, deskflow::AuthenticatedPeerTicket peer,
      std::shared_ptr<std::atomic<InputPreparationState>> state, InputCutoffClock::time_point deadline
  )
      : m_result(std::move(result)),
        m_peer(std::move(peer)),
        m_state(std::move(state)),
        m_deadline(deadline)
  {
  }
  PreparedInputFocusResult m_result;
  deskflow::AuthenticatedPeerTicket m_peer;
  std::shared_ptr<std::atomic<InputPreparationState>> m_state;
  InputCutoffClock::time_point m_deadline;
};

} // namespace deskflow::windowshare
