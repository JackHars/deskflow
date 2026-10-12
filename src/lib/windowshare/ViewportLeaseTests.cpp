/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ViewportLease.h"
#include <barrier>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace deskflow::windowshare {
// Synthetic provider only: no OS surface, capture, input posting or authority.
struct ViewportLeaseTestAccess
{
  static auto request(ViewportLeaseSource &source, const ViewportBinding &binding, ViewportClock::time_point now)
  {
    return source.requestAt(binding, now);
  }
  static auto accept(
      ViewportLeaseSource &source, const ViewportRenewal &request, const ViewportBinding &binding,
      const ViewportGeometry &geometry, ViewportClock::time_point now
  )
  {
    return source.acceptAt(request, binding, geometry, now);
  }
  static bool live(const ViewportObservation &observation, ViewportClock::time_point now)
  {
    return observation.liveAt(now);
  }
  static bool live(const ViewportDeliveryValidation &validation, ViewportClock::time_point now)
  {
    return validation.liveAt(now);
  }
  static ViewportDeliveryValidation validate(
      const ViewportObservation &observation, const ViewportPointerSample &sample,
      std::shared_ptr<const std::atomic_bool> revoked,
      ViewportDeliverySemantics semantics = ViewportDeliverySemantics::SequenceValidatedViewer
  )
  {
    ViewportDeliveryValidation value;
    value.m_observation = observation;
    value.m_sample = sample;
    value.m_revoked = std::move(revoked);
    value.m_semantics = semantics;
    return value;
  }
  static ViewportPressOwnership press(
      const ViewportObservation &observation, const ViewportPointerSample &sample,
      std::shared_ptr<const std::atomic_bool> released
  )
  {
    ViewportPressOwnership value;
    value.m_renewal = observation.renewal();
    value.m_pressGeneration = sample.pressGeneration;
    value.m_pressSequence = sample.sequence;
    value.m_button = sample.button;
    value.m_released = std::move(released);
    return value;
  }
  static auto
  map(const ViewportObservation &observation, const ViewportDeliveryValidation &validation,
      const ViewportPointerSample &sample, ViewportClock::time_point now, const ViewportPressOwnership &owner = {})
  {
    return validation.mapAt(observation, sample, owner, now);
  }
  static void serial(ViewportLeaseSource &source, std::uint64_t serial)
  {
    std::lock_guard lock(source.m_mutex);
    source.m_serial = serial;
  }
  static auto hold(ViewportLeaseSource &source)
  {
    return std::unique_lock(source.m_mutex);
  }
};
} // namespace deskflow::windowshare

using namespace deskflow::windowshare;
using namespace std::chrono_literals;

namespace {
using Access = ViewportLeaseTestAccess;
constexpr auto start = ViewportClock::time_point(1000ms);
static_assert(!std::is_aggregate_v<ViewportDeliveryValidation> && !std::is_aggregate_v<ViewportPressOwnership>);
static_assert(sizeof(ViewportBinding) < 512 && sizeof(ViewportGeometry) < 256);

void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void point(const std::optional<ViewportPoint> &actual, double x, double y, const char *message)
{
  check(actual && std::abs(actual->x - x) < 1e-8 && std::abs(actual->y - y) < 1e-8, message);
}

ViewportBinding binding()
{
  ViewportBinding value;
  value.routeGeneration = 1;
  value.coreServerIncarnation = 2;
  value.coreRecipient = 3;
  value.coreSelectionGeneration = 4;
  value.deskflowConnection.fill(5);
  value.windowShareConnection.fill(6);
  value.deskflowPeerPin.fill(7);
  value.windowSharePeerPin.fill(8);
  value.keyboard = {{9, 10}, 11, 12, 13, 14};
  value.pointerRouteGeneration = 15;
  value.pointerFenceGeneration = 16;
  value.nativeSurface = 17;
  value.surfaceIncarnation = 18;
  value.viewportRevision = 19;
  value.remoteSpace.kind = ViewportSpaceKind::X11Root;
  value.remoteSpace.incarnation.fill(20);
  value.remoteSpace.layoutDigest.fill(21);
  value.remoteSpace.layoutRevision = 22;
  value.remoteSpace.output = 23;
  value.viewerSpace = value.remoteSpace;
  return value;
}

ViewportGeometry geometry()
{
  ViewportGeometry value;
  value.desktopNative = {-2560, -1440, 5120, 2880};
  value.outputNative = {-1920, -1080, 1920, 1080};
  value.clientNative = {-1700, -900, 1000, 750};
  value.clientLogicalSize = {800, 600};
  value.sourceLogicalSize = {1280, 720};
  value.dprNumerator = 5;
  value.dprDenominator = 4;
  value.imagePixelWidth = 1920;
  value.imagePixelHeight = 1080;
  value.videoLogical = {0, 75, 800, 450};
  return value;
}

ViewportPointerSample sample(ViewportPointerKind kind = ViewportPointerKind::Press)
{
  ViewportPointerSample value;
  value.kind = kind;
  value.sequence = 100;
  value.remote = {-1200, -525};
  if (kind == ViewportPointerKind::Press || kind == ViewportPointerKind::Drag || kind == ViewportPointerKind::Release) {
    value.button = 0;
    value.pressGeneration = 42;
  }
  return value;
}

ViewportObservation observe(
    ViewportLeaseSource &source, ViewportClock::time_point now = start, const ViewportBinding &owner = binding(),
    const ViewportGeometry &view = geometry()
)
{
  const auto request = Access::request(source, owner, now);
  check(request.has_value(), "Fixture renewal rejected");
  const auto observation = Access::accept(source, *request, owner, view, now + 1ms);
  check(observation.has_value(), "Fixture geometry rejected");
  return *observation;
}

void defaultDeny()
{
  ViewportLeaseSource source;
  check(!source.closed() && !source.read(), "New source retained authority");
  check(!validViewportBinding({}) && !source.request({}), "Unbound/default coordinate space accepted");
  check(!validViewportGeometry({}) && !mapViewportGeometry({}, {}), "Missing geometry was mapped");
  check(!ViewportObservation{}.live() && !ViewportDeliveryValidation{}.live(), "Default evidence was live");
  auto owner = binding();
  for (const auto kind :
       {ViewportSpaceKind::Unsupported, ViewportSpaceKind::Wayland, static_cast<ViewportSpaceKind>(99)}) {
    owner.remoteSpace.kind = owner.viewerSpace.kind = kind;
    check(!validViewportBinding(owner) && !source.request(owner), "Unsupported coordinate provider accepted");
  }
  const auto observation = observe(source);
  point(mapViewportGeometry(observation.geometry(), sample().remote), 640, 360, "Pure geometry fixture failed");
  check(!Access::map(observation, {}, sample(), start + 2ms), "Constructed geometry alone authorized a click");
  const auto pending = source.request(binding());
  check(pending.has_value(), "Public host request rejected");
  const auto actual = source.accept(*pending, binding(), geometry());
  check(actual && actual->live(), "Public request/accept did not use monotonic freshness");
  check(!mapViewportForDelivery(*actual, {}, sample()), "Public mapping accepted default delivery proof");
}

void mapping()
{
  auto view = geometry();
  check(validViewportGeometry(view), "Fractional DPR with nonzero origins rejected");
  point(mapViewportGeometry(view, {-1200, -525}), 640, 360, "Root origin or DPR was ignored");
  point(mapViewportGeometry(view, {-1575, -743.75}), 160, 80, "Fractional pixel conversion was rounded");
  point(mapViewportGeometry(view, {-1700, -806.25}), 0, 0, "Inclusive top-left content edge rejected");
  check(!mapViewportGeometry(view, {-700, -525}), "Exclusive right edge accepted");
  check(!mapViewportGeometry(view, {-1200, -243.75}), "Exclusive bottom edge accepted");
  check(!mapViewportGeometry(view, {-1200, -899}), "Top letterbox accepted");
  check(!mapViewportGeometry(view, {-1200, -200}), "Bottom letterbox accepted");
  check(!mapViewportGeometry(view, {-1800, -525}), "Outside client accepted");

  // The decoded frame defines the painted fit even if the source logical aspect
  // differs. Mapping then independently converts to source logical coordinates.
  view.sourceLogicalSize.y = 800;
  point(mapViewportGeometry(view, {-1200, -525}), 640, 400, "Source/image coordinate spaces were conflated");
  view.imagePixelWidth = view.imagePixelHeight = 1000;
  view.videoLogical = {100, 0, 600, 600};
  check(validViewportGeometry(view), "Pillarbox fixture rejected");
  check(!mapViewportGeometry(view, {-1690, -525}), "Side letterbox accepted");
  point(mapViewportGeometry(view, {-1575, -900}), 0, 0, "Pillarbox origin was not applied");
}

void invalidGeometry()
{
  const std::array<std::function<void(ViewportGeometry &)>, 23> corrupt{{
      [](auto &v) { v.clientNative.x = std::numeric_limits<double>::quiet_NaN(); },
      [](auto &v) { v.videoLogical.y = std::numeric_limits<double>::infinity(); },
      [](auto &v) { v.sourceLogicalSize.x = std::numeric_limits<double>::max(); },
      [](auto &v) { v.desktopNative.width = std::numeric_limits<double>::max(); },
      [](auto &v) { v.desktopNative.x = -1'000'001; },
      [](auto &v) { v.desktopNative.height = 0; },
      [](auto &v) { v.outputNative.x = -3000; },
      [](auto &v) { v.clientNative.x = -2000; },
      [](auto &v) { v.clientNative.x += 0.5; },
      [](auto &v) { v.clientNative.height = 749; },
      [](auto &v) { v.clientLogicalSize.x = 800.5; },
      [](auto &v) { v.clientLogicalSize.y = 0; },
      [](auto &v) { v.sourceLogicalSize.y = -1; },
      [](auto &v) { v.dprNumerator = 0; },
      [](auto &v) { v.dprDenominator = 0; },
      [](auto &v) {
        v.dprNumerator = 10;
        v.dprDenominator = 8;
      },
      [](auto &v) { v.dprNumerator = std::numeric_limits<std::uint32_t>::max(); },
      [](auto &v) { v.imagePixelWidth = 0; },
      [](auto &v) { v.imagePixelHeight = 16385; },
      [](auto &v) { v.videoLogical.x += 1; },
      [](auto &v) { v.videoLogical.width = 0; },
      [](auto &v) { v.videoLogical.x = -1e-10; },
      [](auto &v) { v.videoLogical = {0, 0, 800, 600}; },
  }};
  for (const auto &change : corrupt) {
    auto view = geometry();
    change(view);
    check(
        !validViewportGeometry(view) && !mapViewportGeometry(view, sample().remote),
        "Invalid/ambiguous mapping accepted"
    );
  }
  for (const double value :
       {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::max()})
    check(!mapViewportGeometry(geometry(), {value, 0}), "Nonfinite/overflowing pointer accepted");

  auto view = geometry();
  view.clientNative.width = 6400;
  view.dprNumerator = 8;
  view.dprDenominator = 1;
  check(!validViewportGeometry(view), "Cross-output client/anisotropic scale accepted");
}

void requestFreshness()
{
  ViewportLeaseSource source;
  const auto request = Access::request(source, binding(), start);
  check(
      request && request->started() == start && request->deadline() == start + 250ms, "Host request deadline missing"
  );
  const auto observation = Access::accept(source, *request, binding(), geometry(), start + 249ms);
  check(observation && Access::live(*observation, start + 249ms), "Timely delayed response rejected");
  check(!Access::live(*observation, start + 250ms), "Reply arrival extended freshness");
  check(!Access::live(*observation, start - 1ms), "Future request granted authority");
  const auto revoked = std::make_shared<std::atomic_bool>(false);
  const auto validation = Access::validate(*observation, sample(), revoked);
  check(!Access::live(validation, start + 250ms), "Retained delivery validation outlived observation");

  const auto first = observe(source, start + 300ms);
  const auto refreshed = observe(source, start + 400ms);
  check(first.renewal().serial() < refreshed.renewal().serial(), "Renewal serial did not advance");
  check(Access::live(first, start + 549ms), "Refresh revoked still-fresh same-binding observation");
  check(!Access::live(first, start + 550ms) && Access::live(refreshed, start + 550ms), "Refresh extended old copy");
  const auto oldValidation = Access::validate(first, sample(), revoked);
  check(
      !Access::map(refreshed, oldValidation, sample(), start + 450ms),
      "Old reply validation authorized renewed geometry"
  );
  check(Access::map(first, oldValidation, sample(), start + 450ms).has_value(), "Still-live exact validation rejected");
  const auto late = Access::request(source, binding(), start + 500ms);
  check(late && !Access::accept(source, *late, binding(), geometry(), start + 750ms), "Deadline boundary accepted");
  check(
      !Access::live(refreshed, start + 550ms) && !source.read(), "Expired current reply failed to revoke its lifetime"
  );
}

void staleReplies()
{
  ViewportLeaseSource source, other;
  const auto first = Access::request(source, binding(), start);
  const auto foreign = Access::request(other, binding(), start);
  const auto latest = Access::request(source, binding(), start + 1ms);
  check(first && foreign && latest, "Pending renewal fixture rejected");
  check(!Access::accept(source, *first, binding(), geometry(), start + 2ms), "Superseded reply accepted");
  check(!Access::accept(source, *foreign, binding(), geometry(), start + 2ms), "Foreign request identity accepted");
  const auto current = Access::accept(source, *latest, binding(), geometry(), start + 2ms);
  check(current.has_value(), "Stale reply destroyed latest pending request");
  check(!Access::accept(source, *latest, binding(), geometry(), start + 3ms), "Duplicate reply accepted");
  check(Access::live(*current, start + 3ms), "Duplicate reply revoked valid current geometry");

  const auto pending = Access::request(source, binding(), start + 4ms);
  auto changed = binding();
  ++changed.keyboard.focusRevision;
  check(pending && !Access::accept(source, *pending, changed, geometry(), start + 5ms), "Wrong focus reply accepted");
  check(!Access::live(*current, start + 5ms), "Wrong current reply left prior validation live");
  const auto before = Access::request(source, binding(), start + 10ms);
  check(
      before && !Access::accept(source, *before, binding(), geometry(), start + 9ms), "Reply before request accepted"
  );
}

void bindingAndGeometryInvalidation()
{
  const std::array<std::function<void(ViewportBinding &)>, 23> changes{{
      [](auto &b) { ++b.routeGeneration; },
      [](auto &b) { ++b.coreServerIncarnation; },
      [](auto &b) { ++b.coreRecipient; },
      [](auto &b) { ++b.coreSelectionGeneration; },
      [](auto &b) { ++b.deskflowConnection[0]; },
      [](auto &b) { ++b.windowShareConnection[0]; },
      [](auto &b) { ++b.deskflowPeerPin[0]; },
      [](auto &b) { ++b.windowSharePeerPin[0]; },
      [](auto &b) { ++b.keyboard.owner.session; },
      [](auto &b) { ++b.keyboard.owner.epoch; },
      [](auto &b) { ++b.keyboard.generation; },
      [](auto &b) { ++b.keyboard.focusRequest; },
      [](auto &b) { ++b.keyboard.focusRevision; },
      [](auto &b) { ++b.keyboard.geometry; },
      [](auto &b) { ++b.pointerRouteGeneration; },
      [](auto &b) { ++b.pointerFenceGeneration; },
      [](auto &b) { ++b.nativeSurface; },
      [](auto &b) { ++b.surfaceIncarnation; },
      [](auto &b) { ++b.viewportRevision; },
      [](auto &b) {
        ++b.remoteSpace.incarnation[0];
        b.viewerSpace = b.remoteSpace;
      },
      [](auto &b) {
        ++b.remoteSpace.layoutRevision;
        b.viewerSpace = b.remoteSpace;
      },
      [](auto &b) {
        ++b.remoteSpace.layoutDigest[0];
        b.viewerSpace = b.remoteSpace;
      },
      [](auto &b) {
        ++b.remoteSpace.output;
        b.viewerSpace = b.remoteSpace;
      },
  }};
  const auto revoked = std::make_shared<std::atomic_bool>(false);
  for (const auto &change : changes) {
    ViewportLeaseSource source;
    const auto old = observe(source);
    const auto validation = Access::validate(old, sample(), revoked);
    auto next = binding();
    change(next);
    const auto request = Access::request(source, next, start + 2ms);
    check(request.has_value(), "Changed valid binding rejected");
    check(
        !Access::live(old, start + 2ms) && !Access::live(validation, start + 2ms),
        "Binding change retained old authority"
    );
    const auto fresh = Access::accept(source, *request, next, geometry(), start + 3ms);
    check(fresh.has_value(), "Same-size layout/identity update could not establish new geometry");
    check(!Access::map(*fresh, validation, sample(), start + 4ms), "Old validation followed new binding");
  }

  ViewportLeaseSource source;
  const auto old = observe(source);
  const auto same = Access::request(source, binding(), start + 2ms);
  auto moved = geometry();
  ++moved.clientNative.x;
  check(validViewportGeometry(moved), "Moved geometry fixture malformed");
  check(
      same && !Access::accept(source, *same, binding(), moved, start + 3ms), "Geometry changed under unchanged revision"
  );
  check(!Access::live(old, start + 3ms) && !source.read(), "Silent geometry change retained cached authority");
  auto revised = binding();
  ++revised.viewportRevision;
  const auto fresh = observe(source, start + 4ms, revised, moved);
  check(Access::live(fresh, start + 5ms), "Revised geometry could not recover");
  source.invalidate();
  const auto reused = observe(source, start + 6ms, revised, moved);
  check(
      !Access::live(fresh, start + 7ms) && Access::live(reused, start + 7ms), "Tuple reuse revived revoked observation"
  );

  auto wrongSpace = revised;
  ++wrongSpace.viewerSpace.output;
  check(
      !validViewportBinding(wrongSpace) && !Access::request(source, wrongSpace, start + 8ms),
      "Unmatched layout/output accepted"
  );
  check(!Access::live(reused, start + 8ms), "Invalid replacement retained geometry");
  wrongSpace = binding();
  wrongSpace.deskflowPeerPin.fill(0);
  check(!validViewportBinding(wrongSpace), "Absent authenticated identity accepted");
  wrongSpace = binding();
  wrongSpace.keyboard.focusRevision = std::uint64_t{1} << 53;
  check(!validViewportBinding(wrongSpace), "Overflowing protocol focus counter accepted");
}

void expiryBreaksPressContinuity()
{
  for (const bool pendingBeforeExpiry : {false, true}) {
    ViewportLeaseSource source;
    const auto first = observe(source);
    const auto released = std::make_shared<std::atomic_bool>(false);
    const auto revoked = std::make_shared<std::atomic_bool>(false);
    const auto owner = Access::press(first, sample(), released);
    if (pendingBeforeExpiry) {
      const auto pending = Access::request(source, binding(), start + 200ms);
      check(pending && pending->deadline() == start + 450ms, "Continuity fixture request failed");
      check(
          !Access::accept(source, *pending, binding(), geometry(), start + 251ms),
          "Late renewal bridged an expired observation's press lifetime"
      );
      check(!source.read(), "Late continuity reply retained old geometry");
    }
    const auto fresh = observe(source, start + 260ms);
    auto drag = sample(ViewportPointerKind::Drag);
    ++drag.sequence;
    const auto proof = Access::validate(fresh, drag, revoked);
    check(Access::live(fresh, start + 262ms), "New lifetime could not recover after expiry");
    check(
        !Access::map(fresh, proof, drag, start + 262ms, owner), "Expired press ownership revived with fresh geometry"
    );
    check(!Access::live(first, start + 2ms), "Expiry did not permanently revoke old lifetime token");
  }
}

void mutexWaitDoesNotBackdate()
{
  ViewportLeaseSource source;
  auto lock = Access::hold(source);
  std::barrier begin(2);
  std::optional<ViewportRenewal> request;
  std::thread requester([&] {
    begin.arrive_and_wait();
    request = source.request(binding());
  });
  begin.arrive_and_wait();
  std::this_thread::sleep_for(30ms);
  const auto released = ViewportClock::now();
  lock.unlock();
  requester.join();
  check(request && request->started() >= released, "Host request sampled its start before mutex wait");

  // A previously live observation expires while accept() waits on the mutex.
  // The pending renewal's own deadline is later, so the continuity check is the
  // deciding condition, not simply an already-expired request.
  ViewportLeaseSource waiting;
  const auto old = observe(waiting, ViewportClock::now() - 100ms);
  const auto pending = waiting.request(binding());
  check(pending.has_value(), "Mutex continuity request fixture failed");
  auto hold = Access::hold(waiting);
  std::optional<ViewportObservation> result;
  std::thread accepter([&] {
    begin.arrive_and_wait();
    result = waiting.accept(*pending, binding(), geometry());
  });
  begin.arrive_and_wait();
  std::this_thread::sleep_until(old.renewal().deadline() + 5ms);
  hold.unlock();
  accepter.join();
  check(!result && !waiting.read(), "Mutex wait bridged expired press lifetime using its entry timestamp");
  check(!Access::live(old, old.renewal().started() + 2ms), "Expired continuity did not revoke retained token");
}

void validationAndOwnership()
{
  ViewportLeaseSource source;
  const auto observation = observe(source);
  const auto revoked = std::make_shared<std::atomic_bool>(false);
  const auto released = std::make_shared<std::atomic_bool>(false);
  const auto down = sample();
  const auto proof = Access::validate(observation, down, revoked);
  point(Access::map(observation, proof, down, start + 2ms), 640, 360, "Exact sequence-bound click denied");
  const auto fixed = Access::validate(observation, down, revoked, ViewportDeliverySemantics::FixedNativeOwner);
  check(
      Access::map(observation, fixed, down, start + 2ms).has_value(), "Explicit synthetic fixed-owner validation denied"
  );
  for (const auto semantics : {ViewportDeliverySemantics::Unsupported, static_cast<ViewportDeliverySemantics>(99)})
    check(
        !Access::map(observation, Access::validate(observation, down, revoked, semantics), down, start + 2ms),
        "Unsupported delivery semantics accepted"
    );
  check(
      !Access::map(observation, Access::validate(observation, down, {}), down, start + 2ms),
      "Unbound provider token accepted"
  );
  auto highButton = down;
  highButton.button = 31;
  check(
      Access::map(observation, Access::validate(observation, highButton, revoked), highButton, start + 2ms).has_value(),
      "Supported original button 31 denied"
  );
  highButton.button = 32;
  check(
      !Access::map(observation, Access::validate(observation, highButton, revoked), highButton, start + 2ms),
      "Out-of-range original button accepted"
  );

  auto changed = down;
  ++changed.sequence;
  check(!Access::map(observation, proof, changed, start + 2ms), "Validation reused for another sequence");
  changed = down;
  changed.remote.x += 1;
  check(!Access::map(observation, proof, changed, start + 2ms), "Validated sequence retargeted to another point");
  changed = down;
  changed.button = 1;
  check(!Access::map(observation, proof, changed, start + 2ms), "Validated sequence changed button");
  changed = down;
  ++changed.pressGeneration;
  check(!Access::map(observation, proof, changed, start + 2ms), "Validated sequence changed press generation");
  changed = down;
  changed.kind = ViewportPointerKind::Release;
  check(!Access::map(observation, proof, changed, start + 2ms), "Validated down became up");

  const auto owner = Access::press(observation, down, released);
  auto drag = sample(ViewportPointerKind::Drag);
  ++drag.sequence;
  drag.remote = {-8000, 5000};
  const auto dragProof = Access::validate(observation, drag, revoked);
  check(!Access::map(observation, dragProof, drag, start + 3ms), "Unowned drag clamped");
  const auto edge = Access::map(observation, dragProof, drag, start + 3ms, owner);
  check(
      edge && edge->x == 0 && edge->y < 720 && edge->y > 719.99,
      "Exact owned drag did not clamp within half-open source"
  );
  auto up = drag;
  up.kind = ViewportPointerKind::Release;
  ++up.sequence;
  const auto upProof = Access::validate(observation, up, revoked);
  check(Access::map(observation, upProof, up, start + 3ms, owner).has_value(), "Exact outside release rejected");
  up.remote = down.remote;
  check(
      !Access::map(observation, Access::validate(observation, up, revoked), up, start + 3ms),
      "Inside up bypassed ownership"
  );

  for (const auto kind : {ViewportPointerKind::Press, ViewportPointerKind::Wheel, ViewportPointerKind::Move}) {
    auto outside = sample(kind);
    outside.remote = {-1200, -899};
    const auto outsideProof = Access::validate(observation, outside, revoked);
    check(
        !Access::map(observation, outsideProof, outside, start + 3ms, owner), "Press/wheel/move clamped into letterbox"
    );
  }
  for (unsigned variant = 0; variant < 3; ++variant) {
    auto wrong = drag;
    if (variant == 0)
      ++wrong.pressGeneration;
    if (variant == 1)
      ++wrong.button;
    if (variant == 2)
      wrong.sequence = down.sequence;
    const auto wrongProof = Access::validate(observation, wrong, revoked);
    check(
        !Access::map(observation, wrongProof, wrong, start + 3ms, owner), "Wrong press identity/ordering acquired owner"
    );
  }
  ViewportLeaseSource other;
  const auto otherObservation = observe(other);
  const auto foreignOwner = Access::press(otherObservation, down, released);
  check(
      !Access::map(observation, dragProof, drag, start + 3ms, foreignOwner),
      "Numeric tuple copied another source's ownership"
  );

  const auto refresh = observe(source, start + 100ms);
  const auto renewedProof = Access::validate(refresh, drag, revoked);
  check(
      Access::map(refresh, renewedProof, drag, start + 300ms, owner).has_value(),
      "Owned press could not span same-binding refresh"
  );
  check(!Access::map(observation, dragProof, drag, start + 300ms, owner), "Old retained geometry gained new freshness");
  released->store(true, std::memory_order_release);
  check(!Access::map(refresh, renewedProof, drag, start + 301ms, owner), "Released press retained clamp permission");
  revoked->store(true, std::memory_order_release);
  check(
      Access::live(refresh, start + 301ms) && !Access::live(renewedProof, start + 301ms),
      "Provider authority conflated with geometry freshness"
  );
}

void lifetimeAndExhaustion()
{
  const auto revoked = std::make_shared<std::atomic_bool>(false);
  ViewportObservation retained;
  ViewportDeliveryValidation proof;
  {
    ViewportLeaseSource source;
    retained = observe(source);
    proof = Access::validate(retained, sample(), revoked);
  }
  check(!Access::live(retained, start + 2ms) && !Access::live(proof, start + 2ms), "Destruction retained authority");

  ViewportLeaseSource source;
  const auto old = observe(source);
  const auto pending = Access::request(source, binding(), start + 2ms);
  source.close();
  source.invalidate();
  source.close();
  check(source.closed() && !source.read() && !source.request(binding()), "Closed source reopened");
  check(
      pending && !Access::accept(source, *pending, binding(), geometry(), start + 3ms), "Close accepted pending reply"
  );
  check(!Access::live(old, start + 3ms), "Close retained old geometry");

  ViewportLeaseSource exhausted;
  Access::serial(exhausted, std::numeric_limits<std::uint64_t>::max() - 1);
  const auto last = observe(exhausted);
  check(last.renewal().serial() == std::numeric_limits<std::uint64_t>::max(), "Final request serial unusable");
  check(!Access::request(exhausted, binding(), start + 2ms) && exhausted.closed(), "Serial exhaustion wrapped");
  check(!Access::live(last, start + 2ms), "Serial exhaustion retained authority");

  ViewportLeaseSource overflow;
  check(
      !Access::request(overflow, binding(), ViewportClock::time_point::max() - 100ms) && overflow.closed(),
      "Deadline arithmetic overflow did not fail closed"
  );
}

void concurrentInvalidation()
{
  auto source = std::make_shared<ViewportLeaseSource>();
  std::barrier begin(3);
  std::atomic<unsigned> attempts{0};
  std::atomic_bool closed{false}, failed{false};
  std::thread producer([&] {
    begin.arrive_and_wait();
    for (unsigned count = 0; !closed.load(std::memory_order_acquire); ++count) {
      auto owner = binding();
      owner.viewportRevision += count;
      const auto request = source->request(owner);
      if (request)
        (void)source->accept(*request, owner, geometry());
      if (count % 7 == 0)
        source->invalidate();
      attempts.fetch_add(1, std::memory_order_release);
    }
    if (source->request(binding()))
      failed.store(true, std::memory_order_release);
  });
  std::thread reader([&] {
    begin.arrive_and_wait();
    std::array<ViewportObservation, 8> retained;
    unsigned index = 0;
    while (!closed.load(std::memory_order_acquire)) {
      if (const auto value = source->read()) {
        retained[index++ % retained.size()] = *value;
        (void)value->live();
      }
    }
    for (const auto &value : retained)
      if (value.live())
        failed.store(true, std::memory_order_release);
  });
  begin.arrive_and_wait();
  while (attempts.load(std::memory_order_acquire) < 100)
    std::this_thread::yield();
  source->close();
  closed.store(true, std::memory_order_release);
  producer.join();
  reader.join();
  check(
      !failed.load(std::memory_order_acquire) && source->closed() && !source->read(),
      "Concurrent closure revived authority"
  );
}
} // namespace

int main()
{
  try {
    defaultDeny();
    mapping();
    invalidGeometry();
    requestFreshness();
    staleReplies();
    bindingAndGeometryInvalidation();
    expiryBreaksPressContinuity();
    mutexWaitDoesNotBackdate();
    validationAndOwnership();
    lifetimeAndExhaustion();
    concurrentInvalidation();
    std::cout
        << "PASS: isolated viewport mapping, host-timed immutable freshness, exact identity/layout invalidation, "
           "sequence-bound delivery, press ownership, default/unsupported denial, terminal closure and overflow\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
