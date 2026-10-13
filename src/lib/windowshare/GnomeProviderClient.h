/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "GnomeProviderConfig.h"
#include "GnomeViewportProtocol.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

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
  // Consume both update kinds after a wake; neither stores native descriptors
  // or borrowed Qt objects in the cross-thread notification.
  std::optional<ViewportUpdate> takeViewportUpdate();
  bool live() const noexcept;
  // Terminal; cancellation interrupts pending D-Bus calls. No automatic
  // fallback/reconnect can silently replace the retained EIS/viewer identity.
  void stop() noexcept;

private:
  std::unique_ptr<Impl> m_impl;
};

} // namespace deskflow::windowshare
