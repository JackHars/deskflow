/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ViewportLease.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace deskflow::windowshare {
namespace {
constexpr double coordinateLimit = 1'000'000, desktopLimit = 65'536, imageLimit = 16'384;

template <std::size_t N> bool nonzero(const std::array<std::uint8_t, N> &value) noexcept
{
  return std::any_of(value.begin(), value.end(), [](auto byte) { return byte != 0; });
}

bool finitePoint(ViewportPoint point) noexcept
{
  return std::isfinite(point.x) && std::isfinite(point.y) && std::abs(point.x) <= coordinateLimit &&
         std::abs(point.y) <= coordinateLimit;
}

bool near(double a, double b) noexcept
{
  return std::abs(a - b) <= 1e-9;
}

bool nativeRect(ViewportRect rect) noexcept
{
  return finitePoint({rect.x, rect.y}) && std::isfinite(rect.width) && std::isfinite(rect.height) && rect.width >= 1 &&
         rect.height >= 1 && rect.width <= desktopLimit && rect.height <= desktopLimit &&
         std::trunc(rect.x) == rect.x && std::trunc(rect.y) == rect.y && std::trunc(rect.width) == rect.width &&
         std::trunc(rect.height) == rect.height && finitePoint({rect.x + rect.width, rect.y + rect.height});
}

bool contained(ViewportRect inner, ViewportRect outer) noexcept
{
  return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

bool inside(ViewportPoint point, ViewportRect rect) noexcept
{
  return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

bool logicalSize(ViewportPoint size) noexcept
{
  return finitePoint(size) && size.x >= 1 && size.y >= 1 && size.x <= imageLimit && size.y <= imageLimit;
}

bool validSample(const ViewportPointerSample &sample) noexcept
{
  if (!sample.sequence || !finitePoint(sample.remote))
    return false;
  switch (sample.kind) {
  case ViewportPointerKind::Move:
  case ViewportPointerKind::Wheel:
    return sample.button == ViewportPointerSample::noButton && !sample.pressGeneration;
  case ViewportPointerKind::Press:
  case ViewportPointerKind::Drag:
  case ViewportPointerKind::Release:
    return sample.button < ViewportPointerSample::buttonCount && sample.pressGeneration;
  default:
    return false;
  }
}

std::optional<ViewportPoint> map(const ViewportGeometry &geometry, ViewportPoint remote, bool clamp) noexcept
{
  if (!validViewportGeometry(geometry) || !finitePoint(remote))
    return std::nullopt;
  const double scale = double(geometry.dprNumerator) / geometry.dprDenominator;
  const ViewportPoint logical{
      (remote.x - geometry.clientNative.x) / scale, (remote.y - geometry.clientNative.y) / scale
  };
  const auto &video = geometry.videoLogical;
  if (!clamp && !inside(logical, video))
    return std::nullopt;
  const double x = std::clamp((logical.x - video.x) / video.width, 0.0, 1.0);
  const double y = std::clamp((logical.y - video.y) / video.height, 0.0, 1.0);
  return ViewportPoint{
      std::min(x * geometry.sourceLogicalSize.x, std::nextafter(geometry.sourceLogicalSize.x, 0.0)),
      std::min(y * geometry.sourceLogicalSize.y, std::nextafter(geometry.sourceLogicalSize.y, 0.0))
  };
}
} // namespace

bool validViewportBinding(const ViewportBinding &binding) noexcept
{
  for (const auto value :
       {binding.routeGeneration, binding.coreServerIncarnation, binding.coreRecipient, binding.coreSelectionGeneration,
        binding.pointerRouteGeneration, binding.pointerFenceGeneration, binding.nativeSurface,
        binding.surfaceIncarnation, binding.viewportRevision})
    if (!value)
      return false;
  const auto &space = binding.remoteSpace;
  return validKeyboardRouteBinding(binding.keyboard) && nonzero(binding.deskflowConnection) &&
         nonzero(binding.windowShareConnection) && nonzero(binding.deskflowPeerPin) &&
         nonzero(binding.windowSharePeerPin) && space.kind == ViewportSpaceKind::X11Root &&
         nonzero(space.incarnation) && nonzero(space.layoutDigest) && space.layoutRevision && space.output &&
         space == binding.viewerSpace;
}

bool validViewportGeometry(const ViewportGeometry &g) noexcept
{
  if (!nativeRect(g.desktopNative) || !nativeRect(g.outputNative) || !nativeRect(g.clientNative) ||
      !contained(g.outputNative, g.desktopNative) || !contained(g.clientNative, g.outputNative) ||
      !logicalSize(g.clientLogicalSize) || !logicalSize(g.sourceLogicalSize) || !g.dprNumerator || !g.dprDenominator ||
      g.dprNumerator > 8192 || g.dprDenominator > 1024 || std::gcd(g.dprNumerator, g.dprDenominator) != 1 ||
      !g.imagePixelWidth || !g.imagePixelHeight || g.imagePixelWidth > imageLimit || g.imagePixelHeight > imageLimit)
    return false;
  const double dpr = double(g.dprNumerator) / g.dprDenominator;
  if (dpr < 0.5 || dpr > 8 || !near(g.clientLogicalSize.x * dpr, g.clientNative.width) ||
      !near(g.clientLogicalSize.y * dpr, g.clientNative.height))
    return false;
  const double fit = std::min(g.clientLogicalSize.x / g.imagePixelWidth, g.clientLogicalSize.y / g.imagePixelHeight);
  const double width = g.imagePixelWidth * fit, height = g.imagePixelHeight * fit;
  return finitePoint({g.videoLogical.x, g.videoLogical.y}) && g.videoLogical.width > 0 && g.videoLogical.height > 0 &&
         contained(g.videoLogical, {0, 0, g.clientLogicalSize.x, g.clientLogicalSize.y}) &&
         near(g.videoLogical.width, width) && near(g.videoLogical.height, height) &&
         near(g.videoLogical.x, (g.clientLogicalSize.x - width) / 2) &&
         near(g.videoLogical.y, (g.clientLogicalSize.y - height) / 2);
}

std::optional<ViewportPoint> mapViewportGeometry(const ViewportGeometry &geometry, ViewportPoint remote) noexcept
{
  return map(geometry, remote, false);
}

const ViewportBinding &ViewportRenewal::binding() const noexcept
{
  return m_binding;
}

std::uint64_t ViewportRenewal::serial() const noexcept
{
  return m_serial;
}

ViewportClock::time_point ViewportRenewal::started() const noexcept
{
  return m_started;
}

ViewportClock::time_point ViewportRenewal::deadline() const noexcept
{
  return m_deadline;
}

bool ViewportRenewal::sameRequest(const ViewportRenewal &other) const noexcept
{
  return m_serial && m_serial == other.m_serial && sameLifetime(other) && m_started == other.m_started &&
         m_deadline == other.m_deadline;
}

bool ViewportRenewal::sameLifetime(const ViewportRenewal &other) const noexcept
{
  return m_token && m_token == other.m_token && m_lifetime && m_lifetime == other.m_lifetime &&
         m_binding == other.m_binding;
}

bool ViewportRenewal::retained() const noexcept
{
  return m_token && m_lifetime && m_token->load(std::memory_order_acquire) == m_lifetime;
}

const ViewportRenewal &ViewportObservation::renewal() const noexcept
{
  return m_renewal;
}

const ViewportGeometry &ViewportObservation::geometry() const noexcept
{
  return m_geometry;
}

bool ViewportObservation::live() const noexcept
{
  return liveAt(ViewportClock::now());
}

bool ViewportObservation::liveAt(ViewportClock::time_point now) const noexcept
{
  return now >= m_renewal.m_started && now < m_renewal.m_deadline && m_renewal.retained();
}

ViewportLeaseSource::ViewportLeaseSource() : m_token(std::make_shared<std::atomic<std::uint64_t>>(0))
{
}

ViewportLeaseSource::~ViewportLeaseSource()
{
  close();
}

std::optional<ViewportRenewal> ViewportLeaseSource::request(const ViewportBinding &binding)
{
  std::lock_guard lock(m_mutex);
  return requestLocked(binding, ViewportClock::now());
}

std::optional<ViewportRenewal>
ViewportLeaseSource::requestAt(const ViewportBinding &binding, ViewportClock::time_point now)
{
  std::lock_guard lock(m_mutex);
  return requestLocked(binding, now);
}

std::optional<ViewportRenewal>
ViewportLeaseSource::requestLocked(const ViewportBinding &binding, ViewportClock::time_point now)
{
  if (m_closed)
    return std::nullopt;
  if (m_serial == std::numeric_limits<std::uint64_t>::max() || now > ViewportClock::time_point::max() - freshness) {
    closeLocked();
    return std::nullopt;
  }
  if (!validViewportBinding(binding)) {
    invalidateLocked();
    return std::nullopt;
  }
  if (m_current && !m_current->liveAt(now))
    invalidateLocked();
  ++m_serial;
  if (!m_binding || *m_binding != binding) {
    invalidateLocked();
    m_binding = binding;
    m_token->store(m_serial, std::memory_order_release);
  }
  ViewportRenewal request;
  request.m_binding = binding;
  request.m_serial = m_serial;
  request.m_lifetime = m_token->load(std::memory_order_acquire);
  request.m_token = m_token;
  request.m_started = now;
  request.m_deadline = now + freshness;
  m_pending = request;
  return request;
}

std::optional<ViewportObservation> ViewportLeaseSource::accept(
    const ViewportRenewal &request, const ViewportBinding &observedBinding, const ViewportGeometry &geometry
)
{
  std::lock_guard lock(m_mutex);
  const auto previous = m_current;
  const auto observation = acceptLocked(request, observedBinding, geometry, ViewportClock::now());
  const auto now = ViewportClock::now();
  // Also cover descheduling during validation, after the mutex was acquired.
  if (observation && (!observation->liveAt(now) || (previous && !previous->liveAt(now)))) {
    invalidateLocked();
    return std::nullopt;
  }
  return observation;
}

std::optional<ViewportObservation> ViewportLeaseSource::acceptAt(
    const ViewportRenewal &request, const ViewportBinding &observedBinding, const ViewportGeometry &geometry,
    ViewportClock::time_point now
)
{
  std::lock_guard lock(m_mutex);
  return acceptLocked(request, observedBinding, geometry, now);
}

std::optional<ViewportObservation> ViewportLeaseSource::acceptLocked(
    const ViewportRenewal &request, const ViewportBinding &observedBinding, const ViewportGeometry &geometry,
    ViewportClock::time_point now
)
{
  if (m_closed || !m_pending || !m_pending->sameRequest(request))
    return std::nullopt;
  if (observedBinding != request.m_binding || !request.retained() || now < request.m_started ||
      now >= request.m_deadline || !validViewportGeometry(geometry) ||
      (m_current && (!m_current->liveAt(now) || m_current->geometry() != geometry))) {
    invalidateLocked();
    return std::nullopt;
  }
  ViewportObservation observation;
  observation.m_renewal = request;
  observation.m_geometry = geometry;
  m_pending.reset();
  m_current = observation;
  return observation;
}

std::optional<ViewportObservation> ViewportLeaseSource::read() const
{
  std::lock_guard lock(m_mutex);
  return m_current;
}

void ViewportLeaseSource::invalidateLocked()
{
  m_token->store(0, std::memory_order_release);
  m_binding.reset();
  m_pending.reset();
  m_current.reset();
}

void ViewportLeaseSource::invalidate()
{
  std::lock_guard lock(m_mutex);
  invalidateLocked();
}

void ViewportLeaseSource::closeLocked()
{
  m_closed = true;
  invalidateLocked();
}

void ViewportLeaseSource::close()
{
  std::lock_guard lock(m_mutex);
  closeLocked();
}

bool ViewportLeaseSource::closed() const
{
  std::lock_guard lock(m_mutex);
  return m_closed;
}

bool ViewportDeliveryValidation::live() const noexcept
{
  return liveAt(ViewportClock::now());
}

bool ViewportDeliveryValidation::liveAt(ViewportClock::time_point now) const noexcept
{
  return (m_semantics == ViewportDeliverySemantics::SequenceValidatedViewer ||
          m_semantics == ViewportDeliverySemantics::FixedNativeOwner) &&
         m_revoked && !m_revoked->load(std::memory_order_acquire) && validSample(m_sample) && m_observation.liveAt(now);
}

std::optional<ViewportPoint> ViewportDeliveryValidation::mapAt(
    const ViewportObservation &observation, const ViewportPointerSample &sample, const ViewportPressOwnership &owner,
    ViewportClock::time_point now
) const noexcept
{
  if (!liveAt(now) || !observation.liveAt(now) || m_sample != sample ||
      !m_observation.renewal().sameRequest(observation.renewal()))
    return std::nullopt;
  const bool owned = sample.kind == ViewportPointerKind::Drag || sample.kind == ViewportPointerKind::Release;
  if (owned && (!owner.m_renewal.sameLifetime(observation.renewal()) || !owner.m_released ||
                owner.m_released->load(std::memory_order_acquire) || !owner.m_pressSequence ||
                owner.m_pressSequence >= sample.sequence || owner.m_pressGeneration != sample.pressGeneration ||
                owner.m_button != sample.button))
    return std::nullopt;
  const auto point = map(observation.geometry(), sample.remote, owned);
  // A source/provider/press may be revoked concurrently with calculation.
  if (!liveAt(now) || (owned && owner.m_released->load(std::memory_order_acquire)))
    return std::nullopt;
  return point;
}

std::optional<ViewportPoint> mapViewportForDelivery(
    const ViewportObservation &observation, const ViewportDeliveryValidation &validation,
    const ViewportPointerSample &sample, const ViewportPressOwnership &owner
) noexcept
{
  const auto point = validation.mapAt(observation, sample, owner, ViewportClock::now());
  // Calculation is bounded but can be descheduled. Never return a point using
  // an entry-time freshness check after its observation has expired.
  if (!validation.live() || !observation.live())
    return std::nullopt;
  return point;
}

} // namespace deskflow::windowshare
