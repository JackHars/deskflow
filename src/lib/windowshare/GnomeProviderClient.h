/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "GnomeProviderConfig.h"
#include "GnomeViewportProtocol.h"
#include "InputCutoff.h"
#include "InputPreparation.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace deskflow::windowshare {

// Owns a private D-Bus connection and one provider-owned EIS/viewer lifetime.
// It neither attributes input events nor grants original-input delivery.
// All D-Bus work runs on its own cancellable GLib context, never the input path.
class GnomeProviderClient final
{
  struct Impl;

public:
  // Exact compositor-stage point from a separately retained, authenticated
  // Server capture decision. This value is evidence only: no unverified EIS
  // region-to-stage conversion or native delivery permission is inferred.
  struct RoutedPoint
  {
    std::uint64_t captureEpoch = 0, sequence = 0;
    double desktopX = 0, desktopY = 0;
    bool operator==(const RoutedPoint &) const = default;
  };
  struct ViewportSnapshot
  {
    std::uint64_t request = 0;
    std::string providerOwner, session, viewer, window, surface;
    std::uint32_t generation = 0, nativeRevision = 0;
    double desktopX = 0, desktopY = 0, surfaceX = 0, surfaceY = 0;
    double sourceX = 0, sourceY = 0;
    GnomeViewportState paint;
    std::optional<RoutedPoint> routedPoint;
    // Two actual paired Clutter same-view stage updates were observed if
    // advertised; never a sender-EIS prefix or input-consumption receipt.
    bool stageFramesObserved = false;
  };
  // A bounded join of native current-hit and last-painted Qt observations.
  // This does not attribute an input device/event, fence legacy input, prove
  // compositor presentation, or authorize original-input delivery.
  class ViewportObservation
  {
  public:
    bool live() const noexcept;
    const ViewportSnapshot &snapshot() const noexcept;

  private:
    friend struct Impl;
    std::shared_ptr<const ViewportSnapshot> m_snapshot;
    std::shared_ptr<std::atomic_bool> m_revoked;
    std::chrono::steady_clock::time_point m_deadline;
  };
  struct ViewportUpdate
  {
    std::uint64_t request = 0;
    std::string error;
    std::optional<ViewportObservation> observation;
  };
  struct InputDrainRequest
  {
    InputCutoffBinding cutoff;
    RoutedPoint point;
    InputCutoffClock::time_point deadline;
    bool retainPrefix = false;
  };
  struct InputPreparationRequest
  {
    InputPreparationBinding binding;
    RoutedPoint point;
    InputCutoffClock::time_point deadline;
  };
  // A fresh focus acknowledgement under a separate closed preparation gate.
  // It has no All lease and cannot authorize input delivery or point reuse.
  class PreparedInputFocus
  {
  public:
    bool live() const noexcept;
    const InputPreparationBinding &binding() const noexcept;
    const ViewportSnapshot &snapshot() const noexcept;
    const std::string &contract() const noexcept;

  private:
    friend struct Impl;
    friend class GnomeProviderClient;
    InputPreparationBinding m_binding;
    std::shared_ptr<const ViewportSnapshot> m_snapshot;
    std::shared_ptr<std::atomic<InputPreparationState>> m_state;
    InputCutoffClock::time_point m_deadline;
    std::string m_contract;
    const void *m_provider = nullptr;
    std::uint64_t m_request = 0;
  };
  struct InputPreparationUpdate
  {
    std::uint64_t request = 0;
    std::string error;
    std::optional<PreparedInputFocus> focus;
    InputPreparationOutcome outcome = InputPreparationOutcome::Failed;
  };
  struct InputPreparationCleanupUpdate
  {
    std::uint64_t request = 0;
    std::string error;
    bool released = false;
  };
  // Historical old-prefix completion under one continuously closed backend
  // and exact Viewer All fence. This is not a reusable point validation. Each
  // later input still needs a fresh stamped native hit and painted mapping.
  class InputDrainSeal
  {
  public:
    bool live() const noexcept;
    const InputCutoffBinding &binding() const noexcept;
    const std::string &contract() const noexcept;

  private:
    friend struct Impl;
    friend class GnomeProviderClient;
    std::shared_ptr<std::atomic_bool> m_revoked;
    std::shared_ptr<const ViewportSnapshot> m_origin;
    InputCutoffBinding m_binding;
    std::string m_contract;
    const void *m_provider = nullptr;
    std::uint64_t m_request = 0;
  };
  // Source/build-backed prerequisite from the exact mapped native Mutter and
  // Clutter modules in the retained trusted Shell. It is independently live
  // only while the same segment/provider lifetime and caller deadline remain.
  // The caller must still prove its own requested sender PONG and batch order.
  class SenderOrderingEvidence
  {
  public:
    bool live() const noexcept;
    const std::string &contract() const noexcept;

  private:
    friend struct Impl;
    ViewportObservation m_viewport;
    std::string m_contract;
    std::shared_ptr<std::atomic_bool> m_sealRevoked;
  };
  // A finite compositor/Qt queue segment after the caller's matched PONG.
  // Unknown mapped builds retain useful segments with no senderOrdering proof.
  // Neither a segment nor its optional prerequisite alone grants delivery.
  class InputDrainSegment
  {
  public:
    bool live() const noexcept;
    const ViewportSnapshot &snapshot() const noexcept;
    const InputCutoffBinding &binding() const noexcept;
    const char *orderingLimitation() const noexcept;
    const std::optional<SenderOrderingEvidence> &senderOrdering() const noexcept;
    const std::optional<InputDrainSeal> &seal() const noexcept;

  private:
    friend struct Impl;
    ViewportObservation m_viewport;
    InputCutoffBinding m_cutoff;
    std::optional<SenderOrderingEvidence> m_senderOrdering;
    std::optional<InputDrainSeal> m_seal;
  };
  struct InputDrainUpdate
  {
    std::uint64_t request = 0;
    std::string error;
    std::optional<InputDrainSegment> segment;
  };
  struct InputMapBatchUpdate
  {
    std::uint64_t request = 0;
    std::string error;
    std::vector<InputDrainSegment> segments;
  };
  struct Update
  {
    // Ownership transfers to the consumer, even when ready is false. Close any
    // descriptor that cannot be passed to libei. Descriptors never live in raw
    // EventQueue payloads and remain owned here until taken.
    int descriptor = -1;
    bool ready = false;
    bool closed = false;
    std::string error;
    std::string session;
    std::string viewer;
    std::uint32_t generation = 0;
  };

  // changed runs on the worker and must only wake the consumer. It must not
  // borrow platform state. Notifications are coalesced until takeUpdate().
  GnomeProviderClient(GnomeProviderConfig configuration, std::function<void()> changed);
  ~GnomeProviderClient();
  GnomeProviderClient(const GnomeProviderClient &) = delete;
  GnomeProviderClient &operator=(const GnomeProviderClient &) = delete;
  Update takeUpdate();
  // Nonblocking, one request/result at a time. Zero means busy/unavailable.
  // The 250ms budget starts here, before worker dispatch or any native call.
  std::uint64_t requestViewportObservation();
  // Read-only validation of an exact frozen compositor-stage point (not the
  // provider's current cursor). Capability-negotiated, single outstanding
  // request, 250ms absolute budget. No native drain or injection is performed.
  std::uint64_t requestRoutedPointObservation(RoutedPoint point);
  // Same single outstanding slot as viewport observations. The caller's
  // absolute deadline is never renewed; at most 250ms remains at admission.
  std::uint64_t requestInputDrain(InputDrainRequest request);
  std::uint64_t requestInputPreparation(InputPreparationRequest request);
  std::optional<InputPreparationUpdate> takeInputPreparationUpdate();
  std::uint64_t promoteInputPreparation(const PreparedInputFocus &prepared, InputDrainRequest request);
  bool cancelInputPreparation(std::uint64_t request, InputCutoffClock::time_point cleanupDeadline);
  std::optional<InputPreparationCleanupUpdate> takeInputPreparationCleanupUpdate();
  // Retained maps reserve one bounded pending-admission slot without waiting
  // for the worker state mutex. A nonzero ID is pending, not a native grant:
  // worker admission may return an exact-ID denial within the original budget.
  // The slot remains occupied until completion is consumed or cancellation
  // settles; no request or per-point deadline is renewed while queued.
  std::uint64_t requestInputMap(const InputDrainSeal &seal, InputDrainRequest request);
  std::optional<InputDrainUpdate> takeInputMapUpdate();
  // One atomic result for 1..16 fresh points under the same historical seal.
  // Every item retains its own original deadline; none survive a batch revoke.
  std::uint64_t requestInputMapBatch(const InputDrainSeal &seal, std::vector<InputDrainRequest> requests);
  std::optional<InputMapBatchUpdate> takeInputMapBatchUpdate();
  // Exact local request ID returned above, not cutoff.request. Revokes pending
  // and copied completed evidence immediately and removes an unread result.
  // An in-flight bounded call settles before the shared slot is reusable.
  bool cancelInputDrain(std::uint64_t request);
  std::optional<InputDrainUpdate> takeInputDrainUpdate();
  // Consume lifecycle, viewport and input-drain updates after a wake. No
  // borrowed Qt object is stored in the cross-thread notification.
  std::optional<ViewportUpdate> takeViewportUpdate();
  bool live() const noexcept;
  // Terminal; cancellation interrupts pending D-Bus calls. No automatic
  // fallback/reconnect can silently replace the retained EIS/viewer identity.
  void stop() noexcept;

private:
  // The private-bus regression adapter can hold the real state mutex and
  // exhaust the ID allocator; neither seam changes production authority.
  friend struct GnomeProviderClientTestAccess;
  std::mutex &stateMutexForTesting();
  std::atomic<std::uint64_t> &requestCounterForTesting();
  bool pendingMapForTesting(bool completed = false) const;
  std::unique_ptr<Impl> m_impl;
};

} // namespace deskflow::windowshare
