/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputCompanion.h"
#include "LocalInputPeerTrust.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcessEnvironment>
#include <QThread>
#include <utility>

namespace deskflow::windowshare {
namespace {
bool mainThread()
{
  const auto *application = QCoreApplication::instance();
  return application && application->thread() == QThread::currentThread();
}

bool privateArgument(const QString &argument)
{
  auto name = argument.section('=', 0, 0);
  while (name.startsWith('-'))
    name.remove(0, 1);
  return name == QStringLiteral("core-input-fd") || argument.contains(QChar('\0'));
}
} // namespace

// A running QProcess must not be destroyed with the launcher: its destructor
// performs a blocking wait. This reaper outlives the launcher until Qt reports
// the owned child stopped. It has no application parent, so application teardown
// cannot destroy a running QProcess either. aboutToQuit closes authority and
// kills immediately; eventual process teardown reclaims an unserviced reaper.
struct LocalInputCompanion::Run final : QObject
{
  QPointer<LocalInputCompanion> owner;
  QProcess process;
  QTimer startup, graceful, escalation, observation;
  QElapsedTimer startupAge, stopAge;
  QString reason;
  int gracefulMs, terminateMs;
  bool stopping = false, finished = false, launched = false;

  explicit Run(LocalInputCompanion *companion)
      : owner(companion),
        gracefulMs(companion->m_limits.gracefulMs),
        terminateMs(companion->m_limits.terminateMs)
  {
    startup.setSingleShot(true);
    graceful.setSingleShot(true);
    escalation.setSingleShot(true);
    graceful.setTimerType(Qt::PreciseTimer);
    escalation.setTimerType(Qt::PreciseTimer);
    observation.setInterval(25);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto &name : environment.keys())
      if (name.startsWith(QStringLiteral("DYLD_")) || name == QStringLiteral("QT_PLUGIN_PATH") ||
          name == QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH") || name == QStringLiteral("QT_QML_PLUGIN_PATH") ||
          name == QStringLiteral("QML_IMPORT_PATH") || name == QStringLiteral("QML2_IMPORT_PATH"))
        environment.remove(name);
    process.setProcessEnvironment(environment);
    // Record launch synchronously; a queued observation can see NotRunning
    // before the queued started/finished handlers have been delivered.
    connect(&process, &QProcess::started, this, [this] { launched = true; });
    connect(
        &process, &QProcess::started, this,
        [this] {
          if (stopping)
            continueStop();
          else if (owner)
            owner->started();
          else
            stop(QStringLiteral("Companion owner was destroyed"));
        },
        Qt::QueuedConnection
    );
    connect(
        &process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int, QProcess::ExitStatus status) {
          if (reason.isEmpty())
            reason = status == QProcess::CrashExit ? QStringLiteral("Trusted companion crashed")
                                                   : QStringLiteral("Trusted companion exited");
          finish();
        },
        Qt::QueuedConnection
    );
    connect(
        &process, &QProcess::errorOccurred, this,
        [this](QProcess::ProcessError) {
          stop(QStringLiteral("Trusted companion process failed: ") + process.errorString().left(512));
        },
        Qt::QueuedConnection
    );
    connect(
        &startup, &QTimer::timeout, this,
        [this] {
          if (owner && owner->m_state == State::Starting)
            stop(QStringLiteral("Trusted companion startup or private handshake timed out"));
        },
        Qt::QueuedConnection
    );
    connect(&graceful, &QTimer::timeout, this, [this] { continueStop(); }, Qt::QueuedConnection);
    connect(
        &escalation, &QTimer::timeout, this,
        [this] {
          if (process.state() != QProcess::NotRunning)
            process.kill();
        },
        Qt::QueuedConnection
    );
    connect(
        &observation, &QTimer::timeout, this,
        [this] {
          if (process.state() == QProcess::NotRunning)
            finish();
          else if (owner)
            owner->observeChannel();
          else
            stop(QStringLiteral("Companion owner was destroyed"));
        },
        Qt::QueuedConnection
    );
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
      stop(QStringLiteral("Application is shutting down"), true);
    });
  }

  void stop(const QString &cause, bool immediate = false)
  {
    if (finished)
      return;
    const QPointer<Run> alive(this);
    if (!stopping) {
      stopping = true;
      reason = cause;
      startup.stop();
      observation.stop();
      // close() can invoke a coordinator callback that pumps a nested loop.
      // Arm the bound before that callback, and tolerate reaping/deletion there.
      stopAge.start();
      if (process.state() != QProcess::NotRunning) {
        graceful.start(gracefulMs);
        escalation.start(gracefulMs + terminateMs);
      }
      if (owner)
        owner->beginStop(reason);
      if (!alive || finished)
        return;
      if (process.state() == QProcess::NotRunning) {
        finish();
        return;
      }
      continueStop();
    }
    if (immediate && process.state() != QProcess::NotRunning)
      process.kill();
  }

  void continueStop()
  {
    if (!stopping || finished || process.state() == QProcess::NotRunning)
      return;
    if (stopAge.elapsed() >= gracefulMs + terminateMs)
      process.kill();
    else if (stopAge.elapsed() >= gracefulMs)
      process.terminate();
    else
      graceful.start(gracefulMs - static_cast<int>(stopAge.elapsed()));
  }

  void finish()
  {
    if (finished || process.state() != QProcess::NotRunning)
      return;
    finished = true;
    startup.stop();
    graceful.stop();
    escalation.stop();
    observation.stop();
    if (reason.isEmpty())
      reason = QStringLiteral("Trusted companion exited");
    const auto result =
        launched ? std::optional<ExitResult>{{process.exitStatus() == QProcess::NormalExit, process.exitCode()}}
                 : std::nullopt;
    if (owner)
      owner->completed(reason, result);
    deleteLater();
  }
};

std::unique_ptr<LocalInputCompanion>
LocalInputCompanion::create(LocalInputSocketPair pair, QString *error, LocalInputCompanionLimits limits)
{
  const auto failed = [&](const QString &reason) -> std::unique_ptr<LocalInputCompanion> {
    if (error)
      *error = reason;
    return {};
  };
  if (!mainThread())
    return failed(QStringLiteral("Trusted companion requires the main Qt thread"));
  if (limits.startupMs < 100 || limits.startupMs > 30000 || limits.gracefulMs < 0 || limits.gracefulMs > 5000 ||
      limits.terminateMs < 10 || limits.terminateMs > 10000 || limits.channel.heartbeatMs < 10 ||
      limits.channel.heartbeatMs > 1000 || limits.channel.timeoutMs < 3 * limits.channel.heartbeatMs ||
      limits.channel.timeoutMs > 10000)
    return failed(QStringLiteral("Invalid trusted companion deadlines"));
  try {
    pair.childDescriptor();
    auto trust = LocalInputPeerTrust::forPackagedPeer(LocalInputRole::Host, error);
    if (!trust)
      return {};
    if (!QDir::isAbsolutePath(trust->executable()) || trust->executable().contains(QChar('\0')))
      return failed(QStringLiteral("Trusted companion executable must be an absolute packaged path"));
    if (error)
      error->clear();
    return std::unique_ptr<LocalInputCompanion>(new LocalInputCompanion(std::move(pair), std::move(trust), limits));
  } catch (...) {
    return failed(QStringLiteral("Cannot establish trusted companion launch capability"));
  }
}

LocalInputCompanion::LocalInputCompanion(
    LocalInputSocketPair pair, std::unique_ptr<LocalInputPeerTrust> trust, LocalInputCompanionLimits limits
)
    : m_pair(std::make_unique<LocalInputSocketPair>(std::move(pair))),
      m_trust(std::move(trust)),
      m_limits(limits)
{
}

LocalInputCompanion::~LocalInputCompanion()
{
  const QPointer<Run> run(std::exchange(m_run, nullptr));
  if (run)
    run->owner.clear();
  m_state = State::Stopping;
  m_pair.reset();
  // Arm the detached reaper before channel callbacks can enter a nested loop.
  if (run)
    run->stop(QStringLiteral("Companion launcher destroyed"));
  const auto channel = m_channel;
  m_channel.clear();
  if (channel)
    channel->close(QStringLiteral("Companion launcher destroyed"));
}

bool LocalInputCompanion::start(QStringList arguments)
{
  if (!mainThread() || m_state != State::Idle)
    return false;
  for (const auto &argument : arguments)
    if (privateArgument(argument)) {
      completed(QStringLiteral("Caller cannot supply a private input descriptor"));
      return false;
    }
  if (!onChannel) {
    completed(QStringLiteral("A trusted companion channel owner is required"));
    return false;
  }
  m_state = State::Starting;
  try {
    arguments.append(QStringLiteral("--core-input-fd=%1").arg(m_pair->childDescriptor()));
    m_run = new Run(this);
    m_pair->configureChild(m_run->process);
    m_run->startup.start(m_limits.startupMs);
    m_run->startupAge.start();
    m_run->observation.start();
    m_run->process.start(m_trust->executable(), arguments);
  } catch (...) {
    if (m_run)
      m_run->stop(QStringLiteral("Cannot launch trusted companion"));
    else
      completed(QStringLiteral("Cannot launch trusted companion"));
    return false;
  }
  return true;
}

void LocalInputCompanion::started()
{
  if (m_state != State::Starting || !m_run)
    return;
  const QPointer<LocalInputCompanion> alive(this);
  try {
    if (m_run->startupAge.hasExpired(m_limits.startupMs)) {
      m_run->stop(QStringLiteral("Trusted companion startup or private handshake timed out"));
      return;
    }
    const auto child = localInputProcess(m_run->process.processId());
    if (!child || !m_trust->verify(*child)) {
      m_run->stop(QStringLiteral("Companion process identity verification failed"));
      return;
    }
    if (m_run->startupAge.hasExpired(m_limits.startupMs)) {
      m_run->stop(QStringLiteral("Trusted companion startup or private handshake timed out"));
      return;
    }
    m_child = child;
    auto channel = m_pair->adoptCore(*child, m_limits.channel);
    m_pair.reset();
    m_channel = channel.get();
    auto *run = m_run;
    connect(channel.get(), &QObject::destroyed, run, [run] {
      run->stop(QStringLiteral("Companion channel owner released the channel"));
    });
    const auto callback = onChannel;
    if (!callback) {
      run->stop(QStringLiteral("Companion channel owner is unavailable"));
      return;
    }
    callback(std::move(channel));
    if (alive)
      observeChannel();
  } catch (...) {
    if (alive && m_run)
      m_run->stop(QStringLiteral("Trusted companion channel setup failed"));
  }
}

void LocalInputCompanion::observeChannel()
{
  if (!m_run || (m_state != State::Starting && m_state != State::Running))
    return;
  if (m_state == State::Starting && m_run->startupAge.hasExpired(m_limits.startupMs)) {
    m_run->stop(QStringLiteral("Trusted companion startup or private handshake timed out"));
    return;
  }
  if (!m_channel) {
    if (m_child)
      m_run->stop(QStringLiteral("Companion channel owner released the channel"));
    return;
  }
  if (m_channel->authenticated()) {
    if (m_state == State::Starting) {
      m_state = State::Running;
      m_run->startup.stop();
    }
  } else if (m_state == State::Running) {
    m_run->stop(QStringLiteral("Trusted companion private channel lost authority"));
  }
}

void LocalInputCompanion::beginStop(const QString &reason)
{
  m_state = State::Stopping;
  m_pair.reset();
  const auto channel = m_channel;
  m_channel.clear();
  if (channel)
    channel->close(reason);
}

void LocalInputCompanion::completed(const QString &reason, std::optional<ExitResult> result)
{
  m_run = nullptr;
  m_state = State::Stopped;
  m_exitResult = result;
  m_pair.reset();
  const QPointer<LocalInputCompanion> alive(this);
  const auto channel = m_channel;
  m_channel.clear();
  if (channel)
    channel->close(reason);
  if (!alive)
    return;
  const auto callback = onStopped;
  try {
    if (callback)
      callback(reason);
  } catch (...) {
    // There is no remaining process or authority to grant after completion.
  }
}

void LocalInputCompanion::stop()
{
  if (!mainThread() || m_state == State::Stopped || m_state == State::Stopping)
    return;
  if (m_run)
    m_run->stop(QStringLiteral("Trusted companion stopped"));
  else
    completed(QStringLiteral("Trusted companion stopped before launch"));
}

LocalInputCompanion::State LocalInputCompanion::state() const
{
  return m_state;
}

std::optional<LocalInputProcess> LocalInputCompanion::childProcess() const
{
  return m_child;
}

std::optional<LocalInputCompanion::ExitResult> LocalInputCompanion::exitResult() const
{
  return m_exitResult;
}

} // namespace deskflow::windowshare
