/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <xcb/xcb.h>

namespace deskflow::windowshare {

// These are observations, never a ViewportDeliveryValidation or a proof that
// two authenticated peers refer to the same X server. In particular, Unix
// SO_PEERCRED can identify the creator of an inherited listening socket rather
// than the currently serving process. Xwayland extension presence is rejected;
// extension absence does not prove a native Xorg server or its lifetime.
struct X11SocketPeer
{
  std::array<std::uint8_t, 16> bootId{};
  std::uint64_t processStartTicks = 0;
  std::uint32_t pid = 0, uid = 0, gid = 0;
  bool operator==(const X11SocketPeer &) const = default;
};

struct X11NativeRect
{
  std::int32_t x = 0, y = 0;
  std::uint32_t width = 0, height = 0;
  bool operator==(const X11NativeRect &) const = default;
};

struct X11ObservedOutput
{
  std::uint32_t output = 0, crtc = 0;
  X11NativeRect native;
  // Mapping initially supports ordinary, unrotated, unpanned outputs with
  // identity current AND pending transforms. The canonical topology retains
  // unsupported transforms/panning too, so their changes still invalidate.
  bool simpleMapping = false;
  bool operator==(const X11ObservedOutput &) const = default;
};

struct X11DisplaySnapshot
{
  X11SocketPeer socketPeer;
  // Local observer lifetime only; independently opened connections intentionally
  // have different incarnations. This value must not be compared as server ID.
  std::uint64_t connectionIncarnation = 0, revision = 0;
  std::uint32_t root = 0, screen = 0, clientPointer = 0, clientKeyboard = 0;
  X11NativeRect desktop;
  std::uint32_t primaryOutput = 0;
  std::vector<X11ObservedOutput> outputs;
  // Canonical big-endian bounded encoding of root dimensions, RandR resources,
  // outputs, modes, CRTCs, transforms/filter parameters, panning and providers.
  // XIDs are sorted. Comparing only width/height or configTimestamp is unsafe.
  std::vector<std::uint8_t> topology;
  std::uint32_t surface = 0, inputFocus = 0;
  X11NativeRect client;
  bool viewable = false, focusWithinSurface = false;
};

class X11DisplayObservation
{
public:
  X11DisplayObservation() = default;
  const X11DisplaySnapshot &snapshot() const noexcept;
  bool live() const noexcept;

private:
  friend class X11DisplayObserver;
  struct State;
  std::shared_ptr<const State> m_state;
};

// Connection-owner-thread API. Borrows the existing connection; never opens a
// display, changes event-queue ownership, dequeues events, grabs input, or closes
// the borrowed connection/FD. Destroy/close BEFORE its connection is destroyed.
// A private unmapped InputOnly window receives RandR notifications without
// replacing the owner's root/window event masks. Feed owner-delivered native
// events to handleEvent(), and poll regularly (e.g. every 20 ms).
//
// Requests/replies are advanced without waiting for replies. The 250ms deadline
// bounds observation freshness, not OS-level socket writes inside libxcb.
// Two matching full reads plus event invalidation provide an observation, not
// an atomic transaction against other clients, grabs, or later X server changes.
class X11DisplayObserver final
{
public:
  explicit X11DisplayObserver(xcb_connection_t *borrowed, int screenIndex = -1);
  ~X11DisplayObserver();
  X11DisplayObserver(const X11DisplayObserver &) = delete;
  X11DisplayObserver &operator=(const X11DisplayObserver &) = delete;

  // -1 screen selection requires a real surface XID and derives its root from
  // GetGeometry. A specified screen must match the surface root, if supplied.
  void refresh(std::uint32_t surface = 0);
  void poll();
  bool pending() const noexcept;
  void invalidate();
  void close();
  void handleEvent(const xcb_generic_event_t &event);
  std::optional<X11DisplayObservation> observe() const;
  std::string unavailableReason() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace deskflow::windowshare
