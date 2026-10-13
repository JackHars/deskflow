/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "OriginalInputStamp.h"
#include "X11DisplayObserver.h"
#include <array>
#include <chrono>
#include <memory>
#include <optional>

namespace deskflow::windowshare {
using X11CoordinateClock = std::chrono::steady_clock;
struct X11WitnessLocator
{
  std::uint64_t request = 0;
  std::uint32_t window = 0, property = 0, type = 0;
  bool operator==(const X11WitnessLocator &) const = default;
};
struct X11WitnessSnapshot
{
  X11WitnessLocator locator;
  std::array<std::uint8_t, 32> secret{};
  X11DisplayObservation native;
};
// A live namespace rendezvous, not server-process attestation or an input drain.
// The authenticated coordinator must send only the locator to the other endpoint
// and compare its readback against the secret it retained from the publisher.
class X11NamespaceWitness
{
public:
  bool live() const noexcept;
  const X11WitnessSnapshot &snapshot() const noexcept;

private:
  friend class X11CoordinateEndpoint;
  struct State;
  std::shared_ptr<const State> m_state;
};
struct X11PointRequest
{
  std::uint64_t request = 0;
  OriginalInputStamp capture;
  std::int32_t rootX = 0, rootY = 0;
  X11CoordinateClock::time_point deadline;
};
struct X11PointSnapshot
{
  std::uint64_t request = 0;
  OriginalInputStamp capture;
  std::int32_t rootX = 0, rootY = 0, clientX = 0, clientY = 0;
  X11DisplayObservation native;
};
// Two matching native stacking/input-shape traversals at one frozen point.
// This is bounded sequence evidence; no native/Qt consumption or route permit.
class X11PointValidation
{
public:
  bool live() const noexcept;
  const X11PointSnapshot &snapshot() const noexcept;

private:
  friend class X11CoordinateEndpoint;
  struct State;
  std::shared_ptr<const State> m_state;
};
class X11CoordinateEndpoint final
{
public:
  struct Update
  {
    std::uint64_t request = 0;
    std::string error;
    std::optional<X11NamespaceWitness> witness;
    std::optional<X11PointValidation> point;
  };
  // Same owner thread and lifetime as observer; borrows exactly its connection.
  // Owns one unmapped InputOnly window, two fixed atoms and bounded replies.
  explicit X11CoordinateEndpoint(X11DisplayObserver &observer);
  ~X11CoordinateEndpoint();
  X11CoordinateEndpoint(const X11CoordinateEndpoint &) = delete;
  X11CoordinateEndpoint &operator=(const X11CoordinateEndpoint &) = delete;
  bool create(std::uint64_t request, X11CoordinateClock::time_point deadline);
  bool read(X11WitnessLocator locator, X11CoordinateClock::time_point deadline);
  bool confirm(const X11NamespaceWitness &published);
  bool validatePoint(X11PointRequest request);
  void poll();
  bool pending() const noexcept;
  std::optional<Update> takeUpdate();
  bool cancel(std::uint64_t request);
  void invalidate();
  void close();
  // Called only from the connection's actual event owner. No event is dequeued
  // here. The Xlib owner forwards PropertyNotify/DestroyNotify explicitly.
  void propertyChanged(std::uint32_t window, std::uint32_t atom, std::uint32_t sequence);
  void windowDestroyed(std::uint32_t window);
  void handleEvent(const xcb_generic_event_t &event);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace deskflow::windowshare
