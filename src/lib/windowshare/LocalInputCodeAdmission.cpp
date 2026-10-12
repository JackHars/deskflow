/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputCodeAdmission.h"

#include <QPointer>
#include <QThread>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

namespace deskflow::windowshare {
namespace {
using Clock = std::chrono::steady_clock;
// No static thread/future/jthread owns the worker: shutdown cannot join it.
constinit std::atomic<bool> workerBusy = false;
constexpr auto wrongThread = "Code admission is only available on its owning Qt thread";
} // namespace

struct LocalInputCodeAdmissionLifetime
{
  const std::thread::id owner = std::this_thread::get_id();
  QThread *const qtOwner = QThread::currentThread();
  const LocalInputAuthenticatedIdentity identity;
  const QPointer<LocalInputChannel> channel;
  std::atomic<bool> revoked = false;

  LocalInputCodeAdmissionLifetime(LocalInputChannel &value, LocalInputAuthenticatedIdentity authenticated)
      : identity(authenticated),
        channel(&value)
  {
  }
  bool onOwner() const
  {
    return std::this_thread::get_id() == owner && QThread::currentThread() == qtOwner;
  }
  bool current(std::optional<LocalInputProcess> anchor = std::nullopt)
  {
    if (!onOwner() || revoked.load(std::memory_order_acquire))
      return false;
    try {
      if (channel && channel->thread() == qtOwner && channel->authenticatedIdentity() == identity &&
          (!anchor || localInputProcess(anchor->pid) == anchor))
        return true;
    } catch (...) {
      // Process inspection allocation/failure cannot preserve authority.
    }
    revoked.store(true, std::memory_order_release);
    return false;
  }
};

struct LocalInputCodeAdmission::Job
{
  std::mutex mutex;
  bool complete = false;
  std::optional<LocalInputExactPermittedLibrariesSnapshot> inspection;
  QString error;
};

LocalInputCodeAdmissionProof::LocalInputCodeAdmissionProof(
    std::shared_ptr<LocalInputCodeAdmissionLifetime> lifetime,
    std::shared_ptr<const LocalInputExactPermittedLibrariesSnapshot> inspection
)
    : m_lifetime(std::move(lifetime)),
      m_inspection(std::move(inspection))
{
}
bool LocalInputCodeAdmissionProof::live() const
{
  return m_lifetime->current(m_inspection->restrictedAdmission().anchorProcess());
}
const LocalInputExactPermittedLibrariesSnapshot &LocalInputCodeAdmissionProof::inspection() const
{
  return *m_inspection;
}
const LocalInputAuthenticatedIdentity &LocalInputCodeAdmissionProof::connection() const
{
  return m_lifetime->identity;
}

LocalInputCodeAdmission::LocalInputCodeAdmission(
    std::shared_ptr<LocalInputCodeAdmissionLifetime> lifetime, std::shared_ptr<Job> job, Clock::time_point requested,
    Clock::time_point deadline
)
    : m_lifetime(std::move(lifetime)),
      m_job(std::move(job)),
      m_requested(requested),
      m_deadline(deadline)
{
}
LocalInputCodeAdmission::~LocalInputCodeAdmission()
{
  m_lifetime->revoked.store(true, std::memory_order_release);
}
std::unique_ptr<LocalInputCodeAdmission>
LocalInputCodeAdmission::request(LocalInputChannel &channel, QString *error, int timeoutMs)
try {
  if (error)
    error->clear();
  const auto reject = [&](const QString &reason) -> std::unique_ptr<LocalInputCodeAdmission> {
    if (error)
      *error = reason;
    return nullptr;
  };
  if (channel.thread() != QThread::currentThread())
    return reject(QString::fromLatin1(wrongThread));
  if (timeoutMs < 100 || timeoutMs > 30000)
    return reject(QStringLiteral("Code-admission request budget must be 100..30000ms"));
  const auto identity = channel.authenticatedIdentity();
  if (!identity)
    return reject(QStringLiteral("Code admission requires a live authenticated private channel"));
  const auto requested = Clock::now();
  const auto deadline = requested + std::chrono::milliseconds(timeoutMs);
  auto job = std::make_shared<Job>();
  auto lifetime = std::make_shared<LocalInputCodeAdmissionLifetime>(channel, *identity);
  auto guard =
      std::unique_ptr<LocalInputCodeAdmission>(new LocalInputCodeAdmission(lifetime, job, requested, deadline));
  if (workerBusy.exchange(true, std::memory_order_acq_rel))
    return reject(QStringLiteral("A code-admission inspection is already in flight"));
  try {
    std::thread([job, identity = *identity] {
      // Deliberately no Channel, QObject, QCoreApplication, owner, or callback
      // captured here. PeerTrust constructs all independent native value state.
      std::optional<LocalInputExactPermittedLibrariesSnapshot> result;
      QString failure;
      struct ReleaseWorkerSlot
      {
        ~ReleaseWorkerSlot()
        {
          workerBusy.store(false, std::memory_order_release);
        }
      } releaseWorkerSlot;
      try {
        const auto peerRole = identity.role == LocalInputRole::Core ? LocalInputRole::Host : LocalInputRole::Core;
        const auto trust = LocalInputPeerTrust::forPackagedPeer(peerRole, &failure);
        if (trust)
          result = trust->inspectExactPermittedLibraries(identity.peer, &failure);
        if (!result && failure.isEmpty())
          failure = QStringLiteral("Exact code-admission inspection denied");
        failure = failure.left(512);
      } catch (...) {
        result.reset();
        failure = QStringLiteral("Exact code-admission inspection failed");
      }
      try {
        const std::lock_guard lock(job->mutex);
        job->inspection = std::move(result);
        job->error = std::move(failure);
        job->complete = true;
      } catch (...) {
        // A publication failure leaves the request pending until its absolute
        // deadline; it cannot escape the detached worker or mint authority.
      }
    }).detach();
  } catch (...) {
    workerBusy.store(false, std::memory_order_release);
    return reject(QStringLiteral("Cannot start the code-admission inspection worker"));
  }
  return guard;
} catch (...) {
  if (error)
    *error = QStringLiteral("Cannot allocate the code-admission request");
  return nullptr;
}

void LocalInputCodeAdmission::deny(QString reason, State state)
{
  m_lifetime->revoked.store(true, std::memory_order_release);
  m_state = state;
  m_error = std::move(reason);
  m_job.reset();
}
void LocalInputCodeAdmission::poll()
{
  try {
    pollResult();
  } catch (...) {
    deny(QStringLiteral("Code-admission result publication failed"));
  }
}
void LocalInputCodeAdmission::pollResult()
{
  if (m_state == State::Denied || m_state == State::Revoked)
    return;
  if (!m_lifetime->current()) {
    deny(QStringLiteral("Code-admission channel or process lifetime ended"), State::Revoked);
    return;
  }
  if (m_state == State::Ready) {
    if (!m_lifetime->current(m_inspection->restrictedAdmission().anchorProcess()))
      deny(QStringLiteral("Code-admission anchor lifetime ended"), State::Revoked);
    return;
  }
  if (Clock::now() >= m_deadline) {
    deny(QStringLiteral("Code-admission request expired (elapsed=%1ms)")
             .arg(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - m_requested).count()));
    return;
  }
  std::optional<LocalInputExactPermittedLibrariesSnapshot> completed;
  QString failure;
  {
    // Never wait for a worker descheduled during publication. The owner's
    // caller deadline remains enforceable even with no worker progress.
    const std::unique_lock lock(m_job->mutex, std::try_to_lock);
    if (!lock || !m_job->complete)
      return;
    completed = std::move(m_job->inspection);
    failure = std::move(m_job->error);
  }
  m_job.reset();
  if (!completed) {
    deny(failure.isEmpty() ? QStringLiteral("Exact code-admission inspection denied") : failure);
    return;
  }
  const auto &restricted = completed->restrictedAdmission();
  const auto anchor = restricted.anchorProcess();
  const auto &identity = m_lifetime->identity;
  const auto expectedRole = identity.role == LocalInputRole::Core ? LocalInputRole::Host : LocalInputRole::Core;
  const auto coreProcess = identity.role == LocalInputRole::Core ? identity.self : identity.peer;
  if (anchor != coreProcess) {
    deny(QStringLiteral("Code admission requires the live bundle-main anchor to be the Core endpoint"));
    return;
  }
  if (restricted.selfProcess() != identity.self || restricted.peerProcess() != identity.peer ||
      restricted.peerRole() != expectedRole || completed->inspectionStartedAt() < m_requested ||
      completed->inspectionFinishedAt() < completed->inspectionStartedAt() ||
      completed->inspectionFinishedAt() > Clock::now() || Clock::now() >= m_deadline || !m_lifetime->current(anchor)) {
    deny(QStringLiteral("Code-admission result no longer matches the live request"));
    return;
  }
  m_inspection = std::make_shared<const LocalInputExactPermittedLibrariesSnapshot>(std::move(*completed));
  // Allocation above does not pump events, but it is included in the absolute
  // deadline, and all authority is rechecked at the final publication boundary.
  if (Clock::now() >= m_deadline || !m_lifetime->current(anchor)) {
    deny(QStringLiteral("Code-admission request expired or lost authority before publication"));
    return;
  }
  m_state = State::Ready;
}
LocalInputCodeAdmission::State LocalInputCodeAdmission::state()
{
  if (!m_lifetime->onOwner())
    return State::Denied;
  poll();
  return m_state;
}
std::optional<LocalInputCodeAdmissionProof> LocalInputCodeAdmission::proof()
{
  if (state() != State::Ready)
    return std::nullopt;
  return LocalInputCodeAdmissionProof(m_lifetime, m_inspection);
}
QString LocalInputCodeAdmission::error()
{
  if (!m_lifetime->onOwner())
    return QString::fromLatin1(wrongThread);
  poll();
  return m_error;
}
void LocalInputCodeAdmission::revoke()
{
  // Safe terminal invalidation even if miscalled from another thread. Only the
  // owner subsequently updates its local status/error through poll().
  m_lifetime->revoked.store(true, std::memory_order_release);
  if (m_lifetime->onOwner() && m_state != State::Denied && m_state != State::Revoked)
    deny(QStringLiteral("Code admission was revoked"), State::Revoked);
}

} // namespace deskflow::windowshare
