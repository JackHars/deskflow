/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "LocalInputPeerTrust.h"

#include <memory>
#include <optional>

namespace deskflow::windowshare {

struct LocalInputCodeAdmissionLifetime;

// Retained evidence of the sealed, kernel-enforced code-admission policy of the
// exact private-channel executable lifetimes. On macOS 14+, all three pinned
// roles (live bundle anchor, self and peer) require Apple-issued same-Team
// signatures, hardened runtime, explicit library validation, no entitlements,
// and the root-sealed manifest's exact Team AND CDHash library constraint.
// The bundle-main anchor MUST be the authenticated Core endpoint. Distinct
// ancestors are inspection-only: their exec cannot be observed through this
// core/host channel. Both adopted endpoints are CLOEXEC; preserving a copied or
// deliberately made-inheritable descriptor is outside the trusted-code scope.
// Apple/system code remains an OS exception. This neither inventories current
// mappings nor attests process memory. It does not detect permitted dlopen/
// dlclose changes, prevent defects in admitted code, or defend against kernel/
// privileged compromise or trusted code delegating its private descriptor.
//
// Copies share terminal revocation. live() must be called on the requesting Qt
// thread at each use; it checks channel freshness/identity and process births,
// never signatures/disk. Wrong-thread use denies without accessing the channel.
// Closing/destroying/expiring the channel, losing the live anchor, or revoking/
// destroying its guard invalidates every copy. No capture/delivery authority is
// conferred: user policy, selection, focus and ordered ownership are separate.
class LocalInputCodeAdmissionProof final
{
public:
  bool live() const;
  const LocalInputExactPermittedLibrariesSnapshot &inspection() const;
  const LocalInputAuthenticatedIdentity &connection() const;

private:
  friend class LocalInputCodeAdmission;
  LocalInputCodeAdmissionProof(
      std::shared_ptr<LocalInputCodeAdmissionLifetime> lifetime,
      std::shared_ptr<const LocalInputExactPermittedLibrariesSnapshot> inspection
  );
  std::shared_ptr<LocalInputCodeAdmissionLifetime> m_lifetime;
  std::shared_ptr<const LocalInputExactPermittedLibrariesSnapshot> m_inspection;
};

// Explicit optional post-authentication admission, independent of ordinary
// bootstrap/heartbeat budgets. Production never accepts an injected verifier.
// All methods except destruction require the channel's current Qt thread.
// Wrong-thread state/error/proof requests deny and do not poll or mutate state.
// No method pumps an event loop, calls an outgoing callback, or changes channel
// authority. Failed admission leaves ordinary Deskflow/WindowShare unaffected.
//
// At most ONE inspection worker is in flight per process, with no work queue.
// It owns only immutable input/heap result state, not Qt objects or this guard.
// Timeout is an absolute caller deadline (100..30000ms), not cancellation of
// synchronous Security/filesystem calls. Late results are discarded. A stalled
// worker keeps the worker slot busy until it exits; guard destruction/revoke
// never joins or waits. Owners poll state()/proof()/error() on their Qt thread.
class LocalInputCodeAdmission final
{
public:
  // Cold full bundles can exceed 15 seconds when both endpoints inspect their
  // sealed dependencies concurrently. This is a one-shot startup allowance;
  // it never renews liveness, authorizes input while pending or retries work.
  static constexpr int defaultTimeoutMs = 30000;
  enum class State
  {
    Pending,
    Ready,
    Denied,
    Revoked
  };
  static std::unique_ptr<LocalInputCodeAdmission>
  request(LocalInputChannel &channel, QString *error = nullptr, int timeoutMs = defaultTimeoutMs);
  ~LocalInputCodeAdmission();
  LocalInputCodeAdmission(const LocalInputCodeAdmission &) = delete;
  LocalInputCodeAdmission &operator=(const LocalInputCodeAdmission &) = delete;

  State state();
  std::optional<LocalInputCodeAdmissionProof> proof();
  QString error();
  void revoke();

private:
  struct Job;
  LocalInputCodeAdmission(
      std::shared_ptr<LocalInputCodeAdmissionLifetime> lifetime, std::shared_ptr<Job> job,
      std::chrono::steady_clock::time_point requested, std::chrono::steady_clock::time_point deadline
  );
  void poll();
  void pollResult();
  void deny(QString reason, State state = State::Denied);
  std::shared_ptr<LocalInputCodeAdmissionLifetime> m_lifetime;
  std::shared_ptr<Job> m_job;
  std::shared_ptr<const LocalInputExactPermittedLibrariesSnapshot> m_inspection;
  std::chrono::steady_clock::time_point m_requested, m_deadline;
  State m_state = State::Pending;
  QString m_error;
};

} // namespace deskflow::windowshare
