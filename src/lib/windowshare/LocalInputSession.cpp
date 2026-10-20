/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputSession.h"
#include "LifecycleDiagnostics.h"
#include "LocalInputCodeAdmission.h"
#ifdef __APPLE__
#include "platform/OriginalInputCaptureSource_mac.h"
#endif
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace deskflow::windowshare {
namespace {
// Timers wake the loop, while monotonic deadlines decide whether it succeeded.
bool waitFor(const std::function<bool()> &done, int milliseconds)
{
  if (done())
    return true;
  QEventLoop loop;
  QDeadlineTimer deadline(milliseconds, Qt::PreciseTimer);
  QTimer wake;
  wake.setInterval(10);
  QObject::connect(&wake, &QTimer::timeout, &loop, [&] {
    if (deadline.hasExpired() || done())
      loop.quit();
  });
  wake.start();
  loop.exec();
  return !deadline.hasExpired() && done();
}
void probeResult(bool authenticated, bool stopped, const QString &error)
{
  QJsonObject result{
      {"privateInputHandshake", authenticated}, {"companionStopped", stopped}, {"originalInputEnabled", false}
  };
  if (!error.isEmpty())
    result.insert("error", error.left(512));
  QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
}
} // namespace

std::unique_ptr<LocalInputCoreSession> LocalInputCoreSession::start(
    LocalInputSocketPair pair, const QStringList &arguments, QString *error, LocalInputCompanionLimits limits
)
{
  auto result = std::unique_ptr<LocalInputCoreSession>(new LocalInputCoreSession);
#ifdef __APPLE__
  // The hidden Core owns capture draining and the private-channel Qt wakeups.
  // The Host's activity token affects only the Host process. Keep this owner
  // interactive too, including startup and ordered companion shutdown, while
  // still allowing ordinary system/display sleep.
  result->m_activity = std::make_unique<LocalInputActivity>();
  lifecycleDiagnostic(
      "Core input", "input.process-activity", "observed", "Requested interactive scheduling for the private session",
      {{"active", result->m_activity->active()}}
  );
  result->m_captureSource = std::make_shared<OriginalInputCaptureSource>();
  result->m_windowDragEdgeMailbox->bindCapture(
      result->m_captureSource->completedInput()->epoch(), result->m_captureSource->capturePosition()
  );
#endif
  result->m_companion = LocalInputCompanion::create(std::move(pair), error, limits);
  if (!result->m_companion)
    return {};
  // The factory validates limits before arithmetic or event-loop waits. Allow
  // the full graceful/terminate interval plus delivery time for the reap event.
  result->m_stopWaitMs = limits.gracefulMs + limits.terminateMs + 700;
  auto *self = result.get();
  result->m_companion->onChannel = [self](std::unique_ptr<LocalInputChannel> channel) {
    channel->onClosed = [weak = std::weak_ptr<SelectedPeerMailbox>(self->m_selectedPeerMailbox),
                         edge = std::weak_ptr<WindowDragEdgeMailbox>(self->m_windowDragEdgeMailbox)
#ifdef __APPLE__
                             ,
                         capture = std::weak_ptr<OriginalInputCaptureSource>(self->m_captureSource)
#endif
    ](const QString &) {
#ifdef __APPLE__
      if (const auto source = capture.lock())
        source->stop();
#endif
      if (const auto mailbox = weak.lock())
        mailbox->close();
      if (const auto mailbox = edge.lock())
        mailbox->close();
    };
    self->m_channel = std::move(channel);
  };
  QString failure;
  result->m_companion->onStopped = [&](QString reason) { failure = std::move(reason); };
  const bool started = result->m_companion->start(arguments);
  const bool completed = started && waitFor(
                                        [&] {
                                          const auto state = result->m_companion->state();
                                          return state == LocalInputCompanion::State::Running ||
                                                 state == LocalInputCompanion::State::Stopped;
                                        },
                                        limits.startupMs + result->m_stopWaitMs
                                    );
  if (!completed || result->m_companion->state() != LocalInputCompanion::State::Running || !result->m_channel ||
      !result->m_channel->authenticated()) {
    // Remove the callback's borrowed stack reference before result is destroyed.
    result->m_companion->onStopped = {};
    result->stop();
    if (error)
      *error =
          failure.isEmpty() ? QStringLiteral("Trusted companion did not authenticate before its deadline") : failure;
    return {};
  }
  result->m_companion->onStopped = [self](QString reason) {
#ifdef __APPLE__
    self->m_captureSource->stop();
    self->m_captureTimer.stop();
    self->m_activity.reset();
#endif
    self->m_selectedPeerMailbox->close();
    self->m_windowDragEdgeMailbox->close();
    const auto callback = self->onStopped;
    if (callback)
      callback(std::move(reason));
  };
  if (error)
    error->clear();
#ifdef __APPLE__
  // Keep all Qt work and state tracking outside the tap. A bounded drain sees
  // every observed event even with no active route. Missing a deadline causes
  // the producer to revoke on overflow, never silently overwrite or coalesce.
  result->m_captureTimer.setTimerType(Qt::PreciseTimer);
  result->m_captureTimer.setInterval(4);
  // Stopping the companion can synchronously close a channel whose callback
  // destroys this session. Keep that callback off the timer's signal stack.
  QObject::connect(
      &result->m_captureTimer, &QTimer::timeout, &result->m_captureTimer, [self] { self->drainCapturedInput(); },
      Qt::QueuedConnection
  );
  result->m_captureTimer.start();
#endif
  return result;
}
LocalInputCoreSession::~LocalInputCoreSession()
{
#ifdef __APPLE__
  *m_callbackLifetime = false;
  onRoutedInput = {};
  onCapturedInput = {};
  onPointerWindow = {};
  onWindowDragEdge = {};
  m_windowDragEdgeJoin.revoke();
  m_captureTimer.stop();
  if (m_captureSource)
    m_captureSource->stop();
#endif
  m_selectedPeerMailbox->close();
  m_windowDragEdgeMailbox->close();
  onStopped = {};
  if (m_companion) {
    m_companion->onChannel = {};
    m_companion->onStopped = {};
    m_companion->stop();
  }
}
bool LocalInputCoreSession::stop()
{
#ifdef __APPLE__
  // Retain independently: callbacks during companion shutdown may destroy us.
  const auto activity = std::move(m_activity);
  m_windowDragEdgeJoin.revoke();
  m_captureTimer.stop();
  if (m_captureSource)
    m_captureSource->stop();
#endif
  m_selectedPeerMailbox->close();
  m_windowDragEdgeMailbox->close();
  const QPointer<LocalInputCompanion> companion(m_companion.get());
  const int waitMs = m_stopWaitMs;
  if (!companion)
    return true;
  companion->stop();
  // A stop notification may destroy this session. Only the local guarded
  // pointer remains usable after stop or an event-loop callback.
  const bool finished =
      waitFor([&] { return !companion || companion->state() == LocalInputCompanion::State::Stopped; }, waitMs);
  return finished && companion && companion->state() == LocalInputCompanion::State::Stopped;
}
LocalInputChannel *LocalInputCoreSession::channel() const
{
  return m_channel.get();
}
std::optional<LocalInputCompanion::ExitResult> LocalInputCoreSession::exitResult() const
{
  return m_companion ? m_companion->exitResult() : std::nullopt;
}
std::shared_ptr<SelectedPeerMailbox> LocalInputCoreSession::selectedPeerMailbox() const
{
  return m_selectedPeerMailbox;
}
std::shared_ptr<WindowDragEdgeMailbox> LocalInputCoreSession::windowDragEdgeMailbox() const
{
  return m_windowDragEdgeMailbox;
}
#ifdef __APPLE__
std::shared_ptr<OriginalInputCaptureSource> LocalInputCoreSession::captureSource() const
{
  return m_captureSource;
}
void LocalInputCoreSession::drainCapturedInput()
{
  const SlowOperationDiagnostic timing("Core input", "capture.consumer-service");
  if (m_captureEmergencyHandled)
    return;
  const auto source = m_captureSource;
  // Emergency is sticky independently of epoch liveness: a running producer
  // still observes the raw chord after an overflow or continuity failure.
  if (source->emergencyRequested()) {
    m_captureEmergencyHandled = true;
    m_captureTimer.stop();
    m_captureBarrier.reset();
    m_selectedPeerMailbox->close();
    m_windowDragEdgeMailbox->close();
    m_windowDragEdgeJoin.revoke();
    source->stop();
    const QPointer<LocalInputCompanion> companion(m_companion.get());
    // This nonblocking, terminal stop closes only the owned companion. A
    // channel callback can destroy this session, so use no members afterward.
    if (companion)
      companion->stop();
    return;
  }
  const auto epoch = source->epoch();
  const auto pointerLost = [&] {
    m_windowDragEdgeJoin.revoke();
    if (m_pointerNotificationsStopped)
      return;
    m_pointerNotificationsStopped = true;
    if (m_channel && m_channel->supportsWindowPointer())
      m_channel->sendWindowPointer({});
  };
  if (!epoch.live()) {
    const auto retained = source->completedInput()->epoch();
    if (!retained.live() && !m_captureFailureLogged) {
      m_captureFailureLogged = true;
      lifecycleDiagnostic(
          "Core input", "capture.continuity", "failed", "Original capture epoch was revoked",
          {{"epoch", QString::number(retained.id())}, {"reason", int(retained.reason())}}
      );
    }
    m_windowDragEdgeJoin.revoke();
    // Source::epoch() is also empty before producer startup. The conduit owns
    // the underlying epoch and can distinguish that state from continuity loss.
    (void)source->completedInput()->live();
    m_captureBarrier.reset();
    if (m_pointerJoin) {
      m_pointerJoin->revoke();
      pointerLost();
    }
    return;
  }
  if (!m_captureBarrier)
    m_captureBarrier = std::make_unique<OriginalInputNeutralBarrier>(epoch);
  if (!m_captureJoin)
    m_captureJoin = std::make_unique<OriginalInputRouteJoin>(epoch, source->routes());
  if (source->annotationsLive()) {
    if (!m_pointerJoin)
      m_pointerJoin = std::make_unique<OriginalPointerJoin>(epoch);
    for (std::size_t count = 0; count < OriginalPointerJoin::capacity; ++count) {
      const auto annotation = source->popAnnotation();
      if (!annotation)
        break;
      m_pointerJoin->annotate(*annotation);
    }
  } else if (m_pointerJoin) {
    m_pointerJoin->revoke();
    const auto lifetime = m_callbackLifetime;
    pointerLost();
    if (!*lifetime)
      return;
  }
  // Core dispatch can win the race with capture completion, or lose it. Drain
  // both bounded streams on this same consumer and join explicit stamps only.
  for (std::size_t count = 0; count < OriginalInputRoutes::capacity; ++count) {
    const auto decision = source->routes()->pop();
    if (!decision)
      break;
    if (!m_captureJoin->decision(*decision))
      break;
  }
  for (std::size_t count = 0; count < OriginalInputStream::capacity; ++count) {
    const auto captured = source->popCaptured();
    if (!captured)
      break;
    m_windowDragEdgeJoin.capture(*captured);
    if (!m_captureBarrier->observe(captured->input)) {
      m_captureFailureLogged = true;
      lifecycleDiagnostic(
          "Core input", "capture.continuity", "failed", "Physical transition consistency check failed",
          {{"epoch", QString::number(captured->input.record.epoch)},
           {"sequence", QString::number(captured->input.record.sequence)},
           {"payloadKind", int(captured->input.record.payload.index())},
           {"reason", int(captured->input.epoch.reason())}}
      );
      m_windowDragEdgeJoin.revoke();
      // Keep the native observer alive for a later raw emergency.
      m_captureBarrier.reset();
      m_captureJoin->revoke();
      if (m_pointerJoin)
        m_pointerJoin->revoke();
      source->completedInput()->revoke();
      break;
    }
    if (m_captureJoin->live())
      m_captureJoin->capture(*captured);
    if (m_pointerJoin)
      m_pointerJoin->capture(*captured);
    // Keep the sole raw consumer and all continuity/diagnostic observations.
    // A stalled Server revokes only this bounded copy conduit; capture draining
    // and the independent sticky emergency path continue unchanged.
    const auto completed = source->completedInput();
    if (completed->bound() && !completed->publish(*captured) && !m_captureFailureLogged) {
      m_captureFailureLogged = true;
      lifecycleDiagnostic(
          "Core input", "capture.delivery", "failed", "Completed capture inbox rejected publication",
          {{"epoch", QString::number(captured->input.record.epoch)},
           {"sequence", QString::number(captured->input.record.sequence)},
           {"epochLive", completed->epoch().live()},
           {"inboxFailure", completed->failureName()},
           {"reason", int(completed->epoch().reason())},
           {"routesLive", source->routes()->live()}}
      );
    }
    const auto callback = onCapturedInput;
    const auto lifetime = m_callbackLifetime;
    if (callback)
      callback(*captured);
    if (!*lifetime)
      return;
  }
  for (std::size_t count = 0; count < OriginalInputStream::capacity; ++count) {
    const auto joined = m_captureJoin->pop();
    if (!joined)
      break;
    const auto callback = onRoutedInput;
    const auto lifetime = m_callbackLifetime;
    if (callback)
      callback(*joined);
    if (!*lifetime)
      return;
  }
  if (!m_captureJoin->live() && !m_routeFailureLogged) {
    m_routeFailureLogged = true;
    lifecycleDiagnostic(
        "Core input", "capture.route-join", "failed", "Captured input and Core route correlation stopped",
        {{"epoch", QString::number(epoch.id())}, {"reason", m_captureJoin->failureName()}}
    );
  }
  if (m_pointerJoin) {
    if (!m_pointerJoin->live()) {
      const auto lifetime = m_callbackLifetime;
      pointerLost();
      if (!*lifetime)
        return;
    }
    for (std::size_t count = 0; count < OriginalPointerJoin::capacity; ++count) {
      const auto pointer = m_pointerJoin->pop();
      if (!pointer)
        break;
      m_windowDragEdgeJoin.pointer(*pointer);
      const auto callback = onPointerWindow;
      const auto lifetime = m_callbackLifetime;
      if (!m_pointerNotificationsStopped && m_channel && m_channel->supportsWindowPointer()) {
        const auto &record = pointer->captured.input.record;
        const auto &position = *pointer->captured.pointer;
        const auto &button = std::get<OriginalInputButton>(record.payload);
        const LocalWindowPointer observation{
            button.down ? LocalWindowPointer::Kind::Down : LocalWindowPointer::Kind::Up,
            record.epoch,
            record.sequence,
            record.sourceTimestampNs,
            record.flags,
            pointer->window(),
            button.clickCount,
            position.eventNumber,
            position.x,
            position.y
        };
        const bool sent = m_channel->sendWindowPointer(observation);
        if (!*lifetime)
          return;
        if (!sent) {
          pointerLost();
          if (!*lifetime)
            return;
        }
      }
      if (callback)
        callback(*pointer);
      if (!*lifetime)
        return;
    }
  }
  const auto edge = m_windowDragEdgeJoin.join(m_windowDragEdgeMailbox->read());
  const auto generation = edge ? edge->boundary.generation() : 0;
  const auto sequence = edge ? edge->boundary.edge().motion.sequence : 0;
  if (generation != m_windowDragEdgeGeneration || sequence != m_windowDragEdgeSequence) {
    m_windowDragEdgeGeneration = generation;
    m_windowDragEdgeSequence = sequence;
    const auto callback = onWindowDragEdge;
    const auto lifetime = m_callbackLifetime;
    if (callback)
      callback(edge);
    if (!*lifetime)
      return;
  }
  // A joined dispatch is not a remote cursor/viewport acknowledgment, a
  // physical-neutral proof, or input authority. Original posting stays off.
}
#endif
int localInputCoreProbe(LocalInputSocketPair pair)
{
  QString error;
  const auto session = LocalInputCoreSession::start(std::move(pair), {"--core-input-probe"}, &error);
  if (!session) {
    probeResult(false, false, error);
    return 1;
  }
  const bool authenticated = session->channel()->authenticated();
  const bool stopped = session->stop();
  const auto exit = session->exitResult();
  const bool clean = stopped && exit && exit->normalExit && exit->exitCode == 0;
  probeResult(
      authenticated, stopped,
      !stopped ? QStringLiteral("Companion shutdown deadline expired")
      : clean  ? QString{}
               : QStringLiteral("Companion probe did not exit normally with success")
  );
  return authenticated && clean ? 0 : 1;
}
int localInputHostProbe(std::unique_ptr<LocalInputChannel> channel)
{
  if (!channel)
    return 1;
  bool authenticated = false, closed = false;
  channel->onAuthenticated = [&] { authenticated = true; };
  channel->onClosed = [&](QString) { closed = true; };
  // Also cover the optional Core code-admission probe before orderly shutdown.
  // No input hooks or authority exist here; authenticated channel liveness
  // retains its independent short deadline throughout this diagnostic wait.
  const bool finished = waitFor(
      [&] { return closed; }, LocalInputCompanionLimits{}.startupMs + LocalInputCodeAdmission::defaultTimeoutMs + 5000
  );
  channel->onAuthenticated = {};
  channel->onClosed = {};
  channel->close();
  return finished && authenticated ? 0 : 1;
}
} // namespace deskflow::windowshare
