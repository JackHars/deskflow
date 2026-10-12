/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "AuthenticatedPeerObservation.h"
#include "KeyboardRoute.h"
#include "LocalInputChannel.h"
#include "SelectedPeerMailbox.h"
#include <array>
#include <span>

namespace deskflow::windowshare {

struct LocalInputPeerPair
{
  LocalInputPin deskflow{}, windowShare{};
  bool operator==(const LocalInputPeerPair &) const = default;
};

// Explicit local policy. Never learn a pair from Offer, discovery, names, or
// addresses. Values are immutable once made; replacement revokes the attempt.
class LocalInputPeerPolicy final
{
public:
  static constexpr std::size_t maximumPairs = 8;
  static std::optional<LocalInputPeerPolicy> create(std::span<const LocalInputPeerPair> pairs);
  std::optional<LocalInputPin> windowSharePeer(const LocalInputPin &deskflow) const;
  bool permits(const LocalInputOffer &offer) const;

private:
  std::array<LocalInputPeerPair, maximumPairs> m_pairs{};
  std::size_t m_size = 0;
};

// Canonical process-scoped representation; the private channel authenticates
// the process incarnation that qualifies this nonzero TLS connection counter.
LocalInputIncarnation localInputConnectionIncarnation(std::uint64_t counter);

// Qt-thread coordinator, independently validating each endpoint's local TLS
// evidence against the same explicit pair. No native capture or delivery is
// installed here. Production does not construct this controller yet: ordered
// pointer capture, module integrity and authoritative neutral permits are still
// required. In particular an empty tracker or a viewer-supplied flag is not a
// valid implementation of Checks.
class LocalInputPreflight final
{
public:
  enum class Phase
  {
    Idle,
    Offered,
    Candidate,
    Quiesced,
    Preparing,
    Committing,
    Active,
    Stopped
  };
  struct Checks
  {
    // All three are required. State and evidence are revalidated after each
    // callback; none of these callbacks may supply peer-learned authority.
    // deliveryReady establishes the ordered keyboard/pointer owner route;
    // neutralBoundary additionally requires completed legacy/owner releases.
    // Active delivery and the existing channel heartbeat recheck integrity and
    // deliveryReady, never physical neutrality. Keep these checks bounded and
    // lightweight; the heartbeat must release idle held owners on failure.
    std::function<bool(const LocalInputLease &)> moduleIntegrity, deliveryReady, neutralBoundary;
    // Disable local delivery and release this exact owner synchronously. False
    // or absence closes the channel; never resume the viewer on failed release.
    std::function<bool(const LocalInputLease &, LocalInputRevocation)> release;
  };
  struct Host
  {
    std::function<std::optional<AuthenticatedPeerObservation>()> connection;
    std::function<std::optional<KeyboardRouteBinding>()> reserve;
    std::function<bool(const KeyboardRouteBinding &)> startQuiescence, isQuiesced;
    // Required exact-binding transfer from the reservation deadline to active
    // ownership. Called once during host Commit authorization, after neutral
    // admission and before the private Committed acknowledgment can be sent.
    // False/throw/cancellation denies that acknowledgment; release must undo
    // any partial activation. This hook alone never permits input delivery.
    std::function<bool(const KeyboardRouteBinding &)> activate;
    // Current active fence/focus/geometry/owner validation, allowing that
    // owner's held input. Never substitute the one-time neutral check here.
    std::function<bool(const KeyboardRouteBinding &)> isActive;
  };
  static std::unique_ptr<LocalInputPreflight> core(
      LocalInputChannel &channel, std::shared_ptr<SelectedPeerMailbox> mailbox, LocalInputPeerPolicy policy,
      Checks checks = {}
  );
  static std::unique_ptr<LocalInputPreflight>
  host(LocalInputChannel &channel, LocalInputPeerPolicy policy, Host host, Checks checks = {});
  ~LocalInputPreflight();
  LocalInputPreflight(const LocalInputPreflight &) = delete;
  LocalInputPreflight &operator=(const LocalInputPreflight &) = delete;

  // Explicit core request with retained local continuity evidence, never a
  // peer-supplied numeric epoch. Default/revoked evidence denies. The attempt
  // retains and rechecks it at every authority use and heartbeat; epoch IDs
  // cannot regress across attempts. The host binds the offered ID without
  // claiming it independently observes the core's physical capture stream.
  // Does not retry or start automatically on a heartbeat.
  bool begin(OriginalInputEpoch capture);
  // Host's deferred, independently revalidated Bridge notification.
  void quiesced(const KeyboardRouteBinding &binding);
  void cancel(LocalInputRevocation reason = LocalInputRevocation::Requested);
  void replacePolicy(LocalInputPeerPolicy policy);
  void stop();
  Phase phase() const;
  std::optional<LocalInputOffer> offer() const;
  std::optional<LocalInputLease> lease() const;

private:
  friend struct LocalInputPreflightTestAccess;
  struct State;
  explicit LocalInputPreflight(std::shared_ptr<State> state);
  std::shared_ptr<State> m_state;
};

} // namespace deskflow::windowshare
