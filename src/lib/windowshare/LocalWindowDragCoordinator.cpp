/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalWindowDragCoordinator.h"
#include <QPointer>
#include <QThread>
#include <utility>

namespace deskflow::windowshare {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto holdInterval = std::chrono::milliseconds(100);
constexpr auto hostSilence = std::chrono::milliseconds(350);
constexpr auto maximumDrag = (std::uint64_t{1} << 53) - 1;

LocalWindowDrag command(const WindowDragEdgeCandidate &candidate, std::uint64_t id, LocalInputPin peer)
{
  const auto &edge = candidate.boundary.edge();
  const auto &record = candidate.press.captured.input.record;
  return {
      LocalWindowDrag::Kind::Begin,
      edge.side,
      id,
      edge.server,
      edge.recipient,
      candidate.boundary.generation(),
      record.epoch,
      record.sequence,
      record.sourceTimestampNs,
      edge.motion.sequence,
      edge.peer.incarnation(),
      candidate.press.window(),
      candidate.press.captured.pointer->eventNumber,
      edge.peer.fingerprint(),
      peer,
      edge.source,
      edge.destination,
      edge.sourceX,
      edge.sourceY,
      edge.destinationX,
      edge.destinationY
  };
}
bool exactPress(const LocalWindowPointer &press, const LocalWindowDrag &drag)
{
  return press.kind == LocalWindowPointer::Kind::Down && press.clicks == 1 && press.captureEpoch == drag.captureEpoch &&
         press.sequence == drag.pressSequence && press.sourceTime == drag.pressTime && press.window == drag.window &&
         press.eventNumber == drag.eventNumber;
}
} // namespace

struct LocalWindowDragCore::State
{
  QPointer<LocalInputChannel> channel;
  LocalInputPeerPolicy policy;
  std::function<bool()> admission;
  QTimer timer;
  bool stopped = false, checking = false;
  std::uint64_t next = 0, lastBoundary = 0;
  std::optional<WindowDragEdgeCandidate> candidate;
  std::optional<LocalWindowDrag> sent;
  Clock::time_point deadline{}, lastSent{};

  bool admitted()
  {
    if (stopped || checking || !channel || QThread::currentThread() != channel->thread() ||
        !channel->supportsWindowDrag() || !admission)
      return false;
    checking = true;
    bool result = false;
    try {
      const auto check = admission;
      result = check();
    } catch (...) {
    }
    checking = false;
    return result && !stopped && channel && channel->supportsWindowDrag();
  }
  void cancel()
  {
    const auto old = std::exchange(sent, {});
    candidate.reset();
    if (old && channel && channel->supportsWindowDrag()) {
      auto cancelled = *old;
      cancelled.kind = LocalWindowDrag::Kind::Cancel;
      if (!channel->sendWindowDrag(cancelled) && channel)
        channel->close(QStringLiteral("Window drag cancellation failed"));
    }
  }
  bool send(LocalWindowDrag value)
  {
    sent = value; // Reserve before a callback can stop/destroy this owner.
    lastSent = Clock::now();
    if (channel && channel->sendWindowDrag(value))
      return !stopped && sent.has_value();
    cancel();
    return false;
  }
  void observe(const std::optional<WindowDragEdgeCandidate> &value)
  {
    if (stopped)
      return;
    if (sent && (!candidate || !candidate->current() || Clock::now() >= deadline))
      cancel();
    // Lack of a fresh motion sample alone does not revoke an existing held
    // preparation. No stale sample can start another drag or move a preview.
    if (!value || !value->live())
      return;
    if (sent && candidate && candidate->boundary.generation() != value->boundary.generation())
      cancel();
    if (!admitted()) {
      cancel();
      return;
    }
    const auto peer = policy.windowSharePeer(value->boundary.edge().peer.fingerprint());
    if (!peer || !value->live()) {
      cancel();
      return;
    }
    if (!sent) {
      if (value->boundary.generation() <= lastBoundary || next == maximumDrag)
        return;
      lastBoundary = value->boundary.generation();
      candidate = value;
      deadline = Clock::now() + WindowDragTransaction::dragBudget;
      send(command(*value, ++next, *peer));
      return;
    }
    auto moved = command(*value, sent->drag, *peer);
    if (!sent->sameDrag(moved)) {
      cancel();
      return;
    }
    candidate = value;
    if (moved.sequence > sent->sequence) {
      moved.kind = LocalWindowDrag::Kind::Motion;
      send(moved);
    }
  }
  void poll()
  {
    if (!sent)
      return;
    if (!candidate || !candidate->current() || Clock::now() >= deadline || !admitted()) {
      cancel();
      return;
    }
    if (sent && Clock::now() - lastSent >= holdInterval) {
      auto held = *sent;
      held.kind = LocalWindowDrag::Kind::Hold;
      send(held);
    }
  }
  void stop()
  {
    stopped = true;
    timer.stop();
    cancel();
    admission = {};
  }
};
LocalWindowDragCore::LocalWindowDragCore(
    LocalInputChannel &channel, LocalInputPeerPolicy policy, std::function<bool()> admission
)
    : m_state(std::make_shared<State>())
{
  m_state->channel = &channel;
  m_state->policy = std::move(policy);
  m_state->admission = std::move(admission);
  const std::weak_ptr<State> weak = m_state;
  QObject::connect(
      &m_state->timer, &QTimer::timeout, &m_state->timer,
      [weak] {
        if (const auto state = weak.lock())
          state->poll();
      },
      Qt::QueuedConnection
  );
  m_state->timer.start(25);
}
LocalWindowDragCore::~LocalWindowDragCore()
{
  stop();
}
void LocalWindowDragCore::observe(const std::optional<WindowDragEdgeCandidate> &candidate)
{
  const auto state = m_state;
  state->observe(candidate);
}
void LocalWindowDragCore::stop()
{
  const auto state = m_state;
  state->stop();
}

struct LocalWindowDragHost::State
{
  QPointer<LocalInputChannel> channel;
  LocalInputPeerPolicy policy;
  Providers providers;
  QTimer timer;
  bool stopped = false, checking = false;
  std::optional<LocalWindowPointer> press;
  std::optional<LocalWindowDrag> current;
  std::optional<AuthenticatedPeerObservation> connection;
  std::optional<WindowDragBinding> binding;
  Clock::time_point deadline{}, lastRead{};

  bool admitted()
  {
    if (stopped || checking || !channel || QThread::currentThread() != channel->thread() ||
        !channel->supportsWindowDrag() || !providers.admission || !providers.connection)
      return false;
    checking = true;
    bool result = false;
    try {
      const auto check = providers.admission;
      const auto read = providers.connection;
      result = check();
      const auto now = !stopped && result ? read() : std::nullopt;
      result = now && now->live() && current && current->windowSharePin == now->ticket().fingerprint() &&
               (!connection || connection->sameConnection(*now));
      if (result)
        connection = now;
    } catch (...) {
      result = false;
    }
    checking = false;
    return result && !stopped && channel && channel->supportsWindowDrag() && current && press &&
           exactPress(*press, *current) && Clock::now() < deadline && Clock::now() - lastRead < hostSilence;
  }
  void cancel()
  {
    const auto old = std::exchange(binding, {});
    current.reset();
    connection.reset();
    if (old) {
      const auto callback = providers.cancel;
      try {
        if (callback)
          callback(*old);
      } catch (...) {
        if (channel)
          channel->close(QStringLiteral("Window drag cleanup failed"));
      }
    }
  }
  void receive(const LocalWindowDrag &value)
  {
    if (stopped)
      return;
    if (value.kind == LocalWindowDrag::Kind::Cancel) {
      cancel();
      return;
    }
    if (value.kind == LocalWindowDrag::Kind::Begin) {
      cancel();
      if (stopped || !press || !exactPress(*press, value) ||
          policy.windowSharePeer(value.corePin) != std::optional(value.windowSharePin) || !providers.prepare ||
          !providers.cancel)
        return;
      current = value;
      lastRead = Clock::now();
      deadline = lastRead + WindowDragTransaction::dragBudget;
      if (!admitted()) {
        cancel();
        return;
      }
      binding = WindowDragBinding{
          value.drag,
          connection->ticket().incarnation(),
          value.boundary,
          value.captureEpoch,
          value.pressSequence,
          value.pressTime,
          value.window,
          value.eventNumber,
          WindowDragDirection::Export
      };
      const auto requested = *binding;
      const auto prepare = providers.prepare;
      const auto cleanup = providers.cancel;
      bool prepared = false;
      try {
        prepared = prepare(requested, value);
      } catch (...) {
      }
      if (!prepared || binding != requested || !admitted()) {
        if (binding == requested)
          cancel();
        else {
          // A callback can stop this owner before returning a newly-created
          // preview. The exact late result still belongs to this request.
          try {
            cleanup(requested);
          } catch (...) {
            if (channel)
              channel->close(QStringLiteral("Late window drag cleanup failed"));
          }
        }
      }
      return;
    }
    if (!current || !binding || !current->sameDrag(value))
      return; // A denied/expired preparation is never revived by held traffic.
    // Test the old deadline BEFORE refreshing it; late traffic cannot revive.
    if (!admitted()) {
      cancel();
      return;
    }
    current = value;
    lastRead = Clock::now();
    if ((value.kind == LocalWindowDrag::Kind::Motion || value.kind == LocalWindowDrag::Kind::Hold) &&
        providers.motion) {
      const auto callback = providers.motion;
      const auto expected = *binding;
      try {
        callback(expected, value);
      } catch (...) {
        cancel();
        return;
      }
      if (binding == expected && !admitted())
        cancel();
    }
  }
  void pointer(const LocalWindowPointer &value)
  {
    cancel();
    press.reset();
    if (!stopped && validLocalWindowPointer(value) && value.kind == LocalWindowPointer::Kind::Down && value.window &&
        value.clicks == 1)
      press = value;
  }
  void stop()
  {
    stopped = true;
    timer.stop();
    press.reset();
    cancel();
  }
};
LocalWindowDragHost::LocalWindowDragHost(LocalInputChannel &channel, LocalInputPeerPolicy policy, Providers providers)
    : m_state(std::make_shared<State>())
{
  m_state->channel = &channel;
  m_state->policy = std::move(policy);
  m_state->providers = std::move(providers);
  const std::weak_ptr<State> weak = m_state;
  channel.onWindowDrag = [weak](const LocalWindowDrag &drag) {
    if (const auto state = weak.lock())
      state->receive(drag);
  };
  QObject::connect(
      &m_state->timer, &QTimer::timeout, &m_state->timer,
      [weak] {
        if (const auto state = weak.lock(); state && state->current && !state->admitted())
          state->cancel();
      },
      Qt::QueuedConnection
  );
  m_state->timer.start(25);
}
LocalWindowDragHost::~LocalWindowDragHost()
{
  stop();
}
void LocalWindowDragHost::pointer(const LocalWindowPointer &pointer)
{
  const auto state = m_state;
  state->pointer(pointer);
}
void LocalWindowDragHost::stop()
{
  const auto state = m_state;
  state->stop();
}
} // namespace deskflow::windowshare
