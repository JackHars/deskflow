/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "../io/AuthenticatedPeerTicket.h"
#include "KeyboardRoute.h"
#include "OriginalInputStamp.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ClientProxy;

namespace deskflow::windowshare {

// Correlation for one old-input cutoff, never native-input authority. Each
// endpoint separately retains its actual authenticated connection and owner.
// The caller must finish existing physical-owner releases before requesting a
// cutoff. A backend-prefix receipt alone proves neither Clutter nor Qt drain.
struct InputCutoffBinding
{
  std::uint64_t request = 0;
  std::uint32_t coreConnectionGeneration = 0;
  OriginalInputStamp capture;
  KeyboardRouteBinding forwarding;
  std::uint64_t viewerConnection = 0;
  bool operator==(const InputCutoffBinding &) const = default;
};

inline bool validInputCutoffBinding(const InputCutoffBinding &binding) noexcept
{
  return binding.request && binding.coreConnectionGeneration && binding.capture && binding.capture.part &&
         binding.capture.part <= maximumOriginalInputParts && binding.viewerConnection &&
         binding.forwarding.scope == ForwardingScope::All && validKeyboardRouteBinding(binding.forwarding);
}

using InputCutoffClock = std::chrono::steady_clock;
inline constexpr auto inputCutoffMaximumLifetime = std::chrono::milliseconds(250);

enum class InputCutoffState : std::uint8_t
{
  Pending,
  BackendObserved,
  Denied,
  Resumed,
  DrainObserved,
  // Local sender cleanup state only. It is never a valid peer receipt and
  // cannot extend the lifetime of an old backend or mapped observation.
  Resuming,
  // Local/backend state: the old prefix remains sealed but no current point
  // receipt is live. It is never a valid peer receipt.
  Sealed
};

struct InputCutoffPoint
{
  std::int32_t x = 0, y = 0;
  bool operator==(const InputCutoffPoint &) const = default;
};
inline bool validInputCutoffPoint(InputCutoffPoint point) noexcept
{
  return point.x >= -10000000 && point.x <= 10000000 && point.y >= -10000000 && point.y <= 10000000;
}

inline bool sameInputCutoffLifetime(const InputCutoffBinding &first, const InputCutoffBinding &next) noexcept
{
  return validInputCutoffBinding(first) && validInputCutoffBinding(next) &&
         first.coreConnectionGeneration == next.coreConnectionGeneration && first.capture.epoch == next.capture.epoch &&
         first.forwarding == next.forwarding && first.viewerConnection == next.viewerConnection;
}
inline bool continuesInputCutoffSeal(const InputCutoffBinding &origin, const InputCutoffBinding &next) noexcept
{
  return sameInputCutoffLifetime(origin, next) && next.request > origin.request &&
         (next.capture.sequence > origin.capture.sequence ||
          (next.capture.sequence == origin.capture.sequence && next.capture.part > origin.capture.part));
}

// A bounded group of fresh points under one retained prefix. It carries no
// authority until the authenticated response joins every exact item.
inline constexpr std::size_t inputCutoffMaximumBatch = 16;
struct InputCutoffMapItem
{
  OriginalInputStamp capture;
  InputCutoffPoint point;
  InputCutoffClock::time_point deadline;
  bool presentCursor = false;
};
struct InputCutoffBatchItem
{
  InputCutoffBinding binding;
  InputCutoffPoint point;
  InputCutoffClock::time_point deadline;
  bool presentCursor = false;
};
inline bool validInputCutoffBatch(
    const InputCutoffBinding &previous, const std::vector<InputCutoffBatchItem> &items,
    InputCutoffClock::time_point now = InputCutoffClock::now()
) noexcept
{
  if (items.empty() || items.size() > inputCutoffMaximumBatch)
    return false;
  auto last = previous;
  for (const auto &item : items) {
    if (!continuesInputCutoffSeal(last, item.binding) || !validInputCutoffPoint(item.point) || item.deadline <= now ||
        item.deadline > now + inputCutoffMaximumLifetime)
      return false;
    last = item.binding;
  }
  return true;
}

// A consumed old prefix under one continuously closed backend/All gate.
// This is not a point observation and cannot authorize native delivery alone.
// Only the authenticated decoder can issue it after the sealed protocol reply.
class InputCutoffSeal final
{
public:
  bool live() const noexcept
  {
    return m_revoked && !m_revoked->load(std::memory_order_acquire) && m_peer.live();
  }
  const InputCutoffBinding &binding() const noexcept
  {
    return m_binding;
  }

private:
  friend class ::ClientProxy;
  InputCutoffSeal(
      InputCutoffBinding binding, deskflow::AuthenticatedPeerTicket peer, std::shared_ptr<std::atomic_bool> revoked
  )
      : m_binding(std::move(binding)),
        m_peer(std::move(peer)),
        m_revoked(std::move(revoked))
  {
  }
  InputCutoffBinding m_binding;
  deskflow::AuthenticatedPeerTicket m_peer;
  std::shared_ptr<std::atomic_bool> m_revoked;
};

// Wire data, not locally mintable delivery authority. The authenticated Core
// receiver separately retains the exact request, deadline and revocation.
struct InputCutoffMappedResult
{
  InputCutoffBinding binding;
  InputCutoffPoint point;
  double sourceX = 0, sourceY = 0, sourceWidth = 0, sourceHeight = 0;
  std::uint64_t surfaceIncarnation = 0, viewportRevision = 0;
  std::uint32_t nativeSurface = 0, providerGeneration = 0, nativeRevision = 0;
  deskflow::AuthenticatedPeerTicket::Fingerprint viewerPeerFingerprint{};
  std::string orderingContract;
  bool operator==(const InputCutoffMappedResult &) const = default;
};
inline bool validInputCutoffMappedResult(const InputCutoffMappedResult &result) noexcept
{
  const auto finite = [](double value) { return std::isfinite(value) && std::abs(value) <= 10000000.0; };
  return validInputCutoffBinding(result.binding) && validInputCutoffPoint(result.point) && finite(result.sourceX) &&
         finite(result.sourceY) && finite(result.sourceWidth) && finite(result.sourceHeight) && result.sourceX >= 0 &&
         result.sourceY >= 0 && result.sourceWidth > 0 && result.sourceHeight > 0 &&
         result.sourceX < result.sourceWidth && result.sourceY < result.sourceHeight && result.surfaceIncarnation &&
         result.viewportRevision && result.nativeSurface && result.providerGeneration && result.nativeRevision &&
         std::any_of(
             result.viewerPeerFingerprint.begin(), result.viewerPeerFingerprint.end(), [](auto v) { return v != 0; }
         ) &&
         !result.orderingContract.empty() && result.orderingContract.size() <= 128 &&
         std::all_of(
             result.orderingContract.begin(), result.orderingContract.end(),
             [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                      c == ':' || c == '-' || c == '_';
             }
         );
}

inline bool sameInputCutoffMappedIdentity(const InputCutoffMappedResult &a, const InputCutoffMappedResult &b) noexcept
{
  return sameInputCutoffLifetime(a.binding, b.binding) && a.sourceWidth == b.sourceWidth &&
         a.sourceHeight == b.sourceHeight && a.surfaceIncarnation == b.surfaceIncarnation &&
         a.viewportRevision == b.viewportRevision && a.nativeSurface == b.nativeSurface &&
         a.providerGeneration == b.providerGeneration && a.nativeRevision == b.nativeRevision &&
         a.viewerPeerFingerprint == b.viewerPeerFingerprint && a.orderingContract == b.orderingContract;
}

class InputCutoffObservation final
{
public:
  bool live() const noexcept
  {
    return m_revoked && !m_revoked->load(std::memory_order_acquire) && m_peer.live() &&
           InputCutoffClock::now() < m_deadline && (!m_seal || m_seal->live());
  }
  const InputCutoffMappedResult &snapshot() const noexcept
  {
    return m_result;
  }
  const InputCutoffBinding &binding() const noexcept
  {
    return m_result.binding;
  }
  InputCutoffClock::time_point deadline() const noexcept
  {
    return m_deadline;
  }
  const std::optional<InputCutoffSeal> &seal() const noexcept
  {
    return m_seal;
  }

private:
  friend class ::ClientProxy;
  InputCutoffObservation(
      InputCutoffMappedResult result, deskflow::AuthenticatedPeerTicket peer, std::shared_ptr<std::atomic_bool> revoked,
      InputCutoffClock::time_point deadline, std::optional<InputCutoffSeal> seal = std::nullopt
  )
      : m_result(std::move(result)),
        m_peer(std::move(peer)),
        m_revoked(std::move(revoked)),
        m_deadline(deadline),
        m_seal(std::move(seal))
  {
  }
  InputCutoffMappedResult m_result;
  deskflow::AuthenticatedPeerTicket m_peer;
  std::shared_ptr<std::atomic_bool> m_revoked;
  InputCutoffClock::time_point m_deadline;
  std::optional<InputCutoffSeal> m_seal;
};

} // namespace deskflow::windowshare
