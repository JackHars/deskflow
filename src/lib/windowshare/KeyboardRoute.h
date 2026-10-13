/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "Core.h"

namespace deskflow::windowshare {

enum class ForwardingScope
{
  Keyboard,
  All
};

// A forwarding fence, not native-input authority or a native-queue barrier.
// Scope is part of the exact binding; a keyboard-only reply cannot acknowledge
// or resume a full-input route. Original Mac events remain private IPC.
struct KeyboardRouteBinding
{
  Owner owner;
  std::uint64_t generation = 0, focusRequest = 0, focusRevision = 0, geometry = 0;
  ForwardingScope scope = ForwardingScope::Keyboard;
  bool operator==(const KeyboardRouteBinding &) const = default;
};

inline bool validKeyboardRouteBinding(const KeyboardRouteBinding &binding)
{
  if (binding.scope != ForwardingScope::Keyboard && binding.scope != ForwardingScope::All)
    return false;
  constexpr std::uint64_t maximum = (std::uint64_t{1} << 53) - 1;
  for (const auto value :
       {binding.owner.session, binding.owner.epoch, binding.generation, binding.focusRequest, binding.focusRevision,
        binding.geometry})
    if (!value || value > maximum)
      return false;
  return true;
}

enum class KeyboardRouteAction
{
  Request,
  Acknowledge,
  Withdraw
};
enum class KeyboardRouteMode
{
  Viewer,
  Original
};
enum class KeyboardRouteResult
{
  None,
  Ready,
  Busy,
  Stale,
  Denied
};
enum class KeyboardRouteReason
{
  None,
  FocusChanged,
  GeometryChanged,
  Hidden,
  Returned,
  Disconnected,
  Timeout,
  AuthorityLost,
  Failed
};
struct KeyboardRouteControl
{
  KeyboardRouteBinding binding;
  KeyboardRouteAction action = KeyboardRouteAction::Request;
  KeyboardRouteMode mode = KeyboardRouteMode::Original;
  KeyboardRouteResult result = KeyboardRouteResult::None;
  KeyboardRouteReason reason = KeyboardRouteReason::None;
  // Optional negotiated metadata on a ready full-input acknowledgement.
  // This is the sending Viewer's connection counter, never the Host's counter.
  std::uint64_t viewerConnection = 0;
  bool operator==(const KeyboardRouteControl &) const = default;
};

} // namespace deskflow::windowshare
