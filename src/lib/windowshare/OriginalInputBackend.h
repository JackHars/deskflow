/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "InputCutoff.h"
#include "InputPreparation.h"
#include "OriginalInputRoute.h"
#include "SelectedPeer.h"
#include <functional>

namespace deskflow::windowshare {
// Core event-thread callbacks. Never copy this object into a Qt/native worker.
struct OriginalInputBackend
{
  // Capability is present only after peer acknowledgement. Acquisition
  // callbacks return an exact cleanup binding once a wire write is attempted,
  // even if that write synchronously revokes its connection. An empty result
  // means no write/gate was acquired; exceptions have an unknown outcome.
  std::function<std::optional<std::uint32_t>(const SelectedPeer &)> preparationCapability;
  std::function<std::optional<InputPreparationBinding>(
      const SelectedPeer &, OriginalInputStamp, InputCutoffPoint, InputCutoffClock::time_point
  )>
      prepare;
  std::function<std::optional<PreparedInputFocus>(const SelectedPeer &, InputPreparationBinding)> prepared;
  std::function<InputPreparationState(const SelectedPeer &, InputPreparationBinding)> preparationState;
  std::function<InputPreparationOutcome(const SelectedPeer &, InputPreparationBinding)> preparationOutcome;
  std::function<std::optional<InputCutoffBinding>(
      const SelectedPeer &, const PreparedInputFocus &, const KeyboardRouteBinding &, InputCutoffClock::time_point
  )>
      promote;
  std::function<bool(const SelectedPeer &, InputPreparationBinding, InputCutoffClock::time_point)> cancelPreparation;
  std::function<bool(const SelectedPeer &, const OriginalInputRouteDecision &, bool)> quiesce;
  std::function<std::optional<InputCutoffBinding>(
      const SelectedPeer &, OriginalInputStamp, KeyboardRouteBinding, std::uint64_t, InputCutoffPoint,
      InputCutoffClock::time_point
  )>
      start;
  std::function<std::optional<InputCutoffBinding>(
      const SelectedPeer &, const InputCutoffSeal &, OriginalInputStamp, InputCutoffPoint, InputCutoffClock::time_point,
      bool
  )>
      refresh;
  std::function<std::optional<std::vector<InputCutoffBinding>>(
      const SelectedPeer &, const InputCutoffSeal &, const std::vector<InputCutoffMapItem> &
  )>
      batch;
  std::function<std::optional<InputCutoffObservation>(const SelectedPeer &, InputCutoffBinding)> observe;
  std::function<InputCutoffState(const SelectedPeer &, InputCutoffBinding)> state;
  std::function<bool(const SelectedPeer &, InputCutoffBinding)> resume;
};
} // namespace deskflow::windowshare
