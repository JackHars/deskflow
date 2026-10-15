/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "windowshare/OriginalInputStamp.h"

#include <atomic>
#include <utility>

namespace deskflow {

// Optional, process-retained diagnostics. The base queue knows no recorder,
// Qt object or native provider. Callbacks must not allocate, wait or throw.
enum class EventInputObservation
{
  Admit,
  Retire,
  Dispatch,
  Return,
  Lost,
  AllAdmit,
  AllRetire,
  AllDispatch,
  AllReturn,
  AllLost
};
struct EventInputObserver
{
  void *context;
  void (*initialize)(void *, const void *, std::uint64_t, std::uint64_t) noexcept;
  void (*observe)(void *, const void *, windowshare::OriginalInputStamp, EventInputObservation) noexcept;
};
inline std::atomic<const EventInputObserver *> eventInputObserverInstalled{nullptr};
inline std::atomic<bool> eventInputQueueConstructed{false};
static_assert(std::atomic<const EventInputObserver *>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

inline bool installEventInputObserver(const EventInputObserver &observer) noexcept
{
  const EventInputObserver *previous = nullptr;
  // Pair sequentially consistent publication with queue construction. Either
  // the new queue sees this observer, or installation diagnoses preexisting
  // unobserved lifetimes (including Events retrieved from a now-dead queue).
  if (!eventInputObserverInstalled.compare_exchange_strong(previous, &observer)) {
    previous->observe(previous->context, nullptr, {}, EventInputObservation::Lost);
    observer.observe(observer.context, nullptr, {}, EventInputObservation::Lost);
    return false;
  }
  if (eventInputQueueConstructed.load())
    observer.observe(observer.context, nullptr, {}, EventInputObservation::Lost);
  return true;
}

// An Event owns this token while queued or retrieved; dispatch moves it into
// a local scope before invoking arbitrary handlers. Neither the Event nor its
// queue needs to survive a handler. Moving transfers, never copies, ownership.
// Its layout is identical in every target and in ordinary disabled builds.
class EventInputToken final
{
public:
  EventInputToken() = default;
  EventInputToken(const EventInputToken &) = delete;
  EventInputToken &operator=(const EventInputToken &) = delete;
  EventInputToken(EventInputToken &&other) noexcept
  {
    take(other);
  }
  EventInputToken &operator=(EventInputToken &&other) noexcept
  {
    if (this != &other) {
      release();
      take(other);
    }
    return *this;
  }
  ~EventInputToken()
  {
    release();
  }
  void admit(
      const EventInputObserver *observer, const void *queue, windowshare::OriginalInputStamp stamp, bool all = false
  ) noexcept
  {
    if (m_observer) {
      if (m_observer != observer || m_queue != queue || m_stamp != stamp || m_all != all)
        notify(EventInputObservation::Lost);
      if (m_owned)
        return;
    }
    if (!observer)
      return;
    m_observer = observer;
    m_queue = queue;
    m_stamp = stamp;
    m_all = all;
    m_owned = true;
    m_dispatching = m_complete = false;
    notify(EventInputObservation::Admit);
  }
  EventInputToken takeForDispatch() noexcept
  {
    EventInputToken result;
    result.m_observer = m_observer;
    result.m_queue = m_queue;
    result.m_stamp = m_stamp;
    result.m_all = m_all;
    result.m_owned = std::exchange(m_owned, false);
    // Keep only a non-owning provenance check in the Event. Its public stamp
    // setter must diagnose mutation even while a callback owns the lifetime.
    return result;
  }
  void verify(windowshare::OriginalInputStamp stamp) const noexcept
  {
    if (m_observer && stamp != m_stamp)
      notify(EventInputObservation::Lost);
  }
  void dispatch() noexcept
  {
    if (m_owned) {
      m_dispatching = true;
      notify(EventInputObservation::Dispatch);
    }
  }
  void complete() noexcept
  {
    m_complete = true;
  }

private:
  void notify(EventInputObservation action) const noexcept
  {
    if (m_all)
      action = static_cast<EventInputObservation>(
          static_cast<unsigned>(action) + static_cast<unsigned>(EventInputObservation::AllAdmit)
      );
    m_observer->observe(m_observer->context, m_queue, m_stamp, action);
  }
  void release() noexcept
  {
    if (!m_owned)
      return;
    if (!m_complete)
      notify(EventInputObservation::Lost);
    if (m_dispatching)
      notify(EventInputObservation::Return);
    notify(EventInputObservation::Retire);
    m_observer = nullptr;
    m_owned = false;
  }
  void take(EventInputToken &other) noexcept
  {
    m_observer = std::exchange(other.m_observer, nullptr);
    m_queue = other.m_queue;
    m_stamp = other.m_stamp;
    m_owned = std::exchange(other.m_owned, false);
    m_dispatching = other.m_dispatching;
    m_complete = other.m_complete;
    m_all = other.m_all;
  }
  const EventInputObserver *m_observer = nullptr;
  const void *m_queue = nullptr;
  windowshare::OriginalInputStamp m_stamp;
  bool m_owned = false, m_dispatching = false, m_complete = false, m_all = false;
};

} // namespace deskflow
