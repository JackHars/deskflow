/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "KeyboardRoute.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace deskflow::windowshare {

using ViewportIdentity = std::array<std::uint8_t, 16>;
using ViewportDigest = std::array<std::uint8_t, 32>;
using ViewportClock = std::chrono::steady_clock;

enum class ViewportSpaceKind : std::uint8_t
{
  Unsupported,
  X11Root,
  Wayland
};

// A common, independently established coordinate space, not a screen name or
// matching rectangle. A layout revision changes even when its bounds do not.
// No native provider is installed here. Default/unbound and Wayland spaces deny.
struct ViewportCoordinateSpace
{
  ViewportSpaceKind kind = ViewportSpaceKind::Unsupported;
  ViewportIdentity incarnation{};
  ViewportDigest layoutDigest{};
  std::uint64_t layoutRevision = 0, output = 0;
  bool operator==(const ViewportCoordinateSpace &) const = default;
};

// Fixed-size copies of the private route/selection and its two connections.
// These fields correlate observations; caller-constructed numbers/pins are not
// authenticated evidence and cannot mint delivery validation.
struct ViewportBinding
{
  std::uint64_t routeGeneration = 0, coreServerIncarnation = 0, coreRecipient = 0, coreSelectionGeneration = 0;
  ViewportIdentity deskflowConnection{}, windowShareConnection{};
  ViewportDigest deskflowPeerPin{}, windowSharePeerPin{};
  KeyboardRouteBinding keyboard;
  std::uint64_t pointerRouteGeneration = 0, pointerFenceGeneration = 0;
  std::uint64_t nativeSurface = 0, surfaceIncarnation = 0, viewportRevision = 0;
  ViewportCoordinateSpace remoteSpace, viewerSpace;
  bool operator==(const ViewportBinding &) const = default;
};

struct ViewportPoint
{
  double x = 0, y = 0;
  bool operator==(const ViewportPoint &) const = default;
};

struct ViewportRect
{
  double x = 0, y = 0, width = 0, height = 0;
  bool operator==(const ViewportRect &) const = default;
};

struct ViewportGeometry
{
  // Root coordinates are native pixels. This first model supports one output,
  // positive uniform scale, and translation only: no rotation, shear, output
  // crossing or per-output ambiguity. All native rectangle edges are integral.
  ViewportRect desktopNative, outputNative, clientNative;
  ViewportPoint clientLogicalSize, sourceLogicalSize;
  // Reduced positive rational DPR; clientLogicalSize * DPR must exactly match
  // the integral native extent (within arithmetic tolerance, not pixel rounding).
  std::uint32_t dprNumerator = 0, dprDenominator = 0;
  std::uint32_t imagePixelWidth = 0, imagePixelHeight = 0;
  // The painted KeepAspectRatio fit of the decoded image into the client.
  // Image pixel aspect, rather than source logical aspect, determines this fit.
  ViewportRect videoLogical;
  bool operator==(const ViewportGeometry &) const = default;
};

bool validViewportBinding(const ViewportBinding &binding) noexcept;
bool validViewportGeometry(const ViewportGeometry &geometry) noexcept;
// Pure geometry only. A result is NEVER delivery authority. Letterbox/outside
// points deny; the public geometry helper does not offer an ownership bypass.
std::optional<ViewportPoint> mapViewportGeometry(const ViewportGeometry &geometry, ViewportPoint remote) noexcept;

class ViewportLeaseSource;
class ViewportObservation;
class ViewportDeliveryValidation;
class ViewportPressOwnership;

class ViewportRenewal final
{
  friend class ViewportLeaseSource;
  friend class ViewportObservation;
  friend class ViewportDeliveryValidation;
  friend struct ViewportLeaseTestAccess;

public:
  ViewportRenewal() = default;
  const ViewportBinding &binding() const noexcept;
  std::uint64_t serial() const noexcept;
  ViewportClock::time_point started() const noexcept;
  ViewportClock::time_point deadline() const noexcept;

private:
  bool sameRequest(const ViewportRenewal &other) const noexcept;
  bool sameLifetime(const ViewportRenewal &other) const noexcept;
  bool retained() const noexcept;

  ViewportBinding m_binding;
  std::shared_ptr<const std::atomic<std::uint64_t>> m_token;
  std::uint64_t m_serial = 0, m_lifetime = 0;
  ViewportClock::time_point m_started{}, m_deadline{};
};

// Immutable geometry freshness, not click authority. Expiry starts when the
// host requests renewal, never when the reply arrives. A refresh cannot extend
// retained observations. live() is a check at use, not a lock against revocation.
class ViewportObservation final
{
  friend class ViewportLeaseSource;
  friend class ViewportDeliveryValidation;
  friend struct ViewportLeaseTestAccess;

public:
  ViewportObservation() = default;
  const ViewportRenewal &renewal() const noexcept;
  const ViewportGeometry &geometry() const noexcept;
  bool live() const noexcept;

private:
  bool liveAt(ViewportClock::time_point now) const noexcept;
  ViewportRenewal m_renewal;
  ViewportGeometry m_geometry;
};

// One pending renewal and one observation; no callbacks or borrowed objects.
// Owners must invalidate on source/focus/selection/connection/surface/layout or
// pointer-fence loss. Changing any binding here also revokes the old lifetime.
// Geometry must not change within a binding; such a reply invalidates it.
// Same-binding renewal preserves press continuity only while the preceding
// observation remains live. A continuity reply after expiry is rejected; a new
// host request establishes a new lifetime and cannot revive old press evidence.
class ViewportLeaseSource final
{
  friend struct ViewportLeaseTestAccess;

public:
  static constexpr auto freshness = std::chrono::milliseconds(250);
  ViewportLeaseSource();
  ~ViewportLeaseSource();
  ViewportLeaseSource(const ViewportLeaseSource &) = delete;
  ViewportLeaseSource &operator=(const ViewportLeaseSource &) = delete;

  std::optional<ViewportRenewal> request(const ViewportBinding &binding);
  std::optional<ViewportObservation>
  accept(const ViewportRenewal &request, const ViewportBinding &observedBinding, const ViewportGeometry &geometry);
  std::optional<ViewportObservation> read() const;
  void invalidate();
  void close(); // Terminal, also called by destruction and counter exhaustion.
  bool closed() const;

private:
  std::optional<ViewportRenewal> requestAt(const ViewportBinding &binding, ViewportClock::time_point now);
  std::optional<ViewportRenewal> requestLocked(const ViewportBinding &binding, ViewportClock::time_point now);
  std::optional<ViewportObservation> acceptAt(
      const ViewportRenewal &request, const ViewportBinding &observedBinding, const ViewportGeometry &geometry,
      ViewportClock::time_point now
  );
  std::optional<ViewportObservation> acceptLocked(
      const ViewportRenewal &request, const ViewportBinding &observedBinding, const ViewportGeometry &geometry,
      ViewportClock::time_point now
  );
  void invalidateLocked();
  void closeLocked();

  mutable std::mutex m_mutex;
  const std::shared_ptr<std::atomic<std::uint64_t>> m_token;
  std::optional<ViewportBinding> m_binding;
  std::optional<ViewportRenewal> m_pending;
  std::optional<ViewportObservation> m_current;
  std::uint64_t m_serial = 0;
  bool m_closed = false;
};

enum class ViewportPointerKind : std::uint8_t
{
  Unsupported,
  Move,
  Press,
  Drag,
  Release,
  Wheel
};

struct ViewportPointerSample
{
  static constexpr std::uint8_t buttonCount = 32;
  static constexpr std::uint8_t noButton = 255;
  ViewportPointerKind kind = ViewportPointerKind::Unsupported;
  std::uint64_t sequence = 0, pressGeneration = 0;
  ViewportPoint remote;
  std::uint8_t button = noButton;
  bool operator==(const ViewportPointerSample &) const = default;
};

enum class ViewportDeliverySemantics : std::uint8_t
{
  Unsupported,
  SequenceValidatedViewer,
  FixedNativeOwner
};

// There is deliberately NO production constructor/factory in this foundation.
// A future reviewed provider must establish current authority and either an
// exact sequence-bound viewer hit-target validation or explicit fixed native
// owner semantics. A cached rect, Qt global position, enum or peer claim cannot
// create this proof. The test friend is solely for synthetic provider fixtures.
class ViewportDeliveryValidation final
{
  friend struct ViewportLeaseTestAccess;
  friend std::optional<ViewportPoint> mapViewportForDelivery(
      const ViewportObservation &, const ViewportDeliveryValidation &, const ViewportPointerSample &,
      const ViewportPressOwnership &
  ) noexcept;

public:
  ViewportDeliveryValidation() = default;
  bool live() const noexcept;

private:
  bool liveAt(ViewportClock::time_point now) const noexcept;
  std::optional<ViewportPoint> mapAt(
      const ViewportObservation &observation, const ViewportPointerSample &sample, const ViewportPressOwnership &owner,
      ViewportClock::time_point now
  ) const noexcept;

  ViewportObservation m_observation;
  ViewportPointerSample m_sample;
  ViewportDeliverySemantics m_semantics = ViewportDeliverySemantics::Unsupported;
  std::shared_ptr<const std::atomic_bool> m_revoked;
};

// Exact already-delivered press evidence, independently revoked on owner release.
// It binds the complete route/lifetime, button, press generation and down sequence.
// It may span same-binding renewals, but is never fresh delivery authority alone.
// As above, only a synthetic test provider can construct it in this slice.
class ViewportPressOwnership final
{
  friend class ViewportDeliveryValidation;
  friend struct ViewportLeaseTestAccess;

public:
  ViewportPressOwnership() = default;

private:
  ViewportRenewal m_renewal;
  std::shared_ptr<const std::atomic_bool> m_released;
  std::uint64_t m_pressGeneration = 0, m_pressSequence = 0;
  std::uint8_t m_button = ViewportPointerSample::noButton;
};

// Requires fresh exact-sample validation, and exact press ownership for drag/up.
// Only owned drag/up may clamp outside the video. The caller must serialize
// validation/delivery with provider invalidation and its ordered ownership ledger;
// a returned point is not a reservation or replay guard. After revocation, owner
// cleanup uses its stored endpoint rather than trying to map fresh input here.
std::optional<ViewportPoint> mapViewportForDelivery(
    const ViewportObservation &observation, const ViewportDeliveryValidation &validation,
    const ViewportPointerSample &sample, const ViewportPressOwnership &owner = {}
) noexcept;

} // namespace deskflow::windowshare
