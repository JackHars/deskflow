/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "KeyboardRoute.h"
#include "OriginalInputStamp.h"
#include <chrono>

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
  Resumed
};

} // namespace deskflow::windowshare
