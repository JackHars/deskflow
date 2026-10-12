/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputSession.h"
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
  result->m_captureSource = std::make_shared<OriginalInputCaptureSource>();
#endif
  result->m_companion = LocalInputCompanion::create(std::move(pair), error, limits);
  if (!result->m_companion)
    return {};
  // The factory validates limits before arithmetic or event-loop waits. Allow
  // the full graceful/terminate interval plus delivery time for the reap event.
  result->m_stopWaitMs = limits.gracefulMs + limits.terminateMs + 700;
  auto *self = result.get();
  result->m_companion->onChannel = [self](std::unique_ptr<LocalInputChannel> channel) {
    channel->onClosed = [weak = std::weak_ptr<SelectedPeerMailbox>(self->m_selectedPeerMailbox)
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
#endif
    self->m_selectedPeerMailbox->close();
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
  m_captureTimer.stop();
  if (m_captureSource)
    m_captureSource->stop();
#endif
  m_selectedPeerMailbox->close();
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
  m_captureTimer.stop();
  if (m_captureSource)
    m_captureSource->stop();
#endif
  m_selectedPeerMailbox->close();
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
#ifdef __APPLE__
std::shared_ptr<OriginalInputCaptureSource> LocalInputCoreSession::captureSource() const
{
  return m_captureSource;
}
void LocalInputCoreSession::drainCapturedInput()
{
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
    source->stop();
    const QPointer<LocalInputCompanion> companion(m_companion.get());
    // This nonblocking, terminal stop closes only the owned companion. A
    // channel callback can destroy this session, so use no members afterward.
    if (companion)
      companion->stop();
    return;
  }
  const auto epoch = source->epoch();
  if (!epoch.live()) {
    // Source::epoch() is also empty before producer startup. The conduit owns
    // the underlying epoch and can distinguish that state from continuity loss.
    (void)source->completedInput()->live();
    m_captureBarrier.reset();
    return;
  }
  if (!m_captureBarrier)
    m_captureBarrier = std::make_unique<OriginalInputNeutralBarrier>(epoch);
  if (!m_captureJoin)
    m_captureJoin = std::make_unique<OriginalInputRouteJoin>(epoch, source->routes());
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
    if (!m_captureBarrier->observe(captured->input)) {
      // Keep the native observer alive for a later raw emergency.
      m_captureBarrier.reset();
      m_captureJoin->revoke();
      source->completedInput()->revoke();
      break;
    }
    if (m_captureJoin->live())
      m_captureJoin->capture(*captured);
    // Keep the sole raw consumer and all continuity/diagnostic observations.
    // A stalled Server revokes only this bounded copy conduit; capture draining
    // and the independent sticky emergency path continue unchanged.
    const auto completed = source->completedInput();
    if (completed->bound())
      completed->publish(*captured);
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
  // A cold signed bundle can spend several seconds validating nested code
  // before the other endpoint adopts its channel. Cover that bounded startup
  // budget and orderly probe shutdown without changing authenticated liveness.
  const bool finished = waitFor([&] { return closed; }, LocalInputCompanionLimits{}.startupMs + 5000);
  channel->onAuthenticated = {};
  channel->onClosed = {};
  channel->close();
  return finished && authenticated ? 0 : 1;
}
} // namespace deskflow::windowshare
