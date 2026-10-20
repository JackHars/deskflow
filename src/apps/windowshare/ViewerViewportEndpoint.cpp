/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ViewerViewportEndpoint.h"
#include <QPointer>
#include <QSet>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace deskflow::windowshare {
namespace {
void require(bool value, const char *reason)
{
  if (!value)
    throw std::runtime_error(reason);
}
bool protect(int fd)
{
  int type = 0;
  socklen_t length = sizeof(type);
  sockaddr_storage local{}, peer{};
  socklen_t localSize = sizeof(local), peerSize = sizeof(peer);
  const auto flags = ::fcntl(fd, F_GETFD);
  const auto status = ::fcntl(fd, F_GETFL);
  if (fd < 3 || flags < 0 || status < 0 || ::getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &length) ||
      type != SOCK_STREAM || ::getsockname(fd, reinterpret_cast<sockaddr *>(&local), &localSize) ||
      ::getpeername(fd, reinterpret_cast<sockaddr *>(&peer), &peerSize) || local.ss_family != AF_UNIX ||
      peer.ss_family != AF_UNIX || ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) ||
      ::fcntl(fd, F_SETFL, status | O_NONBLOCK))
    return false;
#ifdef SO_NOSIGPIPE
  const int enabled = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)))
    return false;
#endif
  return true;
}
} // namespace

ViewerViewportBootstrap ViewerViewportBootstrap::inspect(int argc, char *const argv[])
{
  ViewerViewportBootstrap result;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--gnome-viewport-fd" || arg.starts_with("--gnome-viewport-fd=") || arg == "-gnome-viewport-fd" ||
        arg.starts_with("-gnome-viewport-fd=")) {
      require(result.m_descriptor < 0 && arg == "--gnome-viewport-fd=4", "Use one canonical --gnome-viewport-fd=4");
      result.m_descriptor = 4;
      require(protect(4), "Inherited viewport descriptor is not a connected Unix stream");
    }
  }
  return result;
}
ViewerViewportBootstrap::~ViewerViewportBootstrap()
{
  if (m_descriptor >= 0)
    ::close(m_descriptor);
}
ViewerViewportBootstrap::ViewerViewportBootstrap(ViewerViewportBootstrap &&other) noexcept
    : m_descriptor(std::exchange(other.m_descriptor, -1))
{
}
int ViewerViewportBootstrap::takeDescriptor() noexcept
{
  return std::exchange(m_descriptor, -1);
}
void ViewerViewportBootstrap::validateOptions(const QStringList &options) const
{
  if (!inherited())
    return;
  const QSet<QString> allowed{
      "gnome-viewport-fd",
      "enable-seamless",
      "headless",
      "connect",
      "port",
      "identity",
      "peer-pin",
      "screen",
      "export-window",
      "quit-after",
      "share-clipboard-text",
      "capture-fps"
  };
  QSet<QString> seen;
  for (const auto &option : options) {
    require(allowed.contains(option) && !seen.contains(option), "Invalid inherited viewport option");
    seen.insert(option);
  }
  for (const auto &name : {"gnome-viewport-fd", "enable-seamless", "headless", "connect", "identity", "peer-pin"})
    require(seen.contains(name), "Inherited viewport requires explicit headless viewer configuration");
}

ViewerViewportEndpoint::ViewerViewportEndpoint(int fd, QObject *parent) : QObject(parent), m_descriptor(fd)
{
  if (!protect(fd)) {
    if (fd >= 0)
      ::close(fd);
    m_descriptor = -1;
    throw std::runtime_error("Invalid viewport endpoint");
  }
  try {
    m_read = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read, this);
    m_write = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Write, this);
    m_write->setEnabled(false);
    connect(m_read.get(), &QSocketNotifier::activated, this, [this] { read(); });
    connect(m_write.get(), &QSocketNotifier::activated, this, [this] { write(); });
    m_dragExpiry.setInterval(25);
    connect(&m_dragExpiry, &QTimer::timeout, this, [this] { expireWindowDrags(); });
    m_expiry.setSingleShot(true);
    connect(&m_expiry, &QTimer::timeout, this, [this] { invalidateSnapshot(QStringLiteral("snapshot-expired")); });
    m_preparationPoll.setInterval(1);
    connect(&m_preparationPoll, &QTimer::timeout, this, [this] { pollPreparationFocus(); });
    m_preparationExpiry.setSingleShot(true);
    connect(&m_preparationExpiry, &QTimer::timeout, this, [this] {
      // Expiry revokes the operation but leaves forwarding closed. Give the
      // owner one bounded cleanup interval; EOF requests actual Bridge shutdown.
      const auto pending = std::exchange(m_preparationPending, {});
      const auto binding = m_preparation ? std::optional(m_preparation->binding) : std::nullopt;
      const QPointer<ViewerViewportEndpoint> alive(this);
      m_preparationPoll.stop();
      if (pending)
        replyPreparation(*pending, false, QStringLiteral("preparation-expired"));
      if (alive && live() && m_preparation && !m_preparation->promoted)
        QTimer::singleShot(250, this, [this, binding] {
          if (m_preparation && binding == m_preparation->binding && !m_preparation->promoted &&
              m_preparation->deadline.hasExpired())
            close(QStringLiteral("preparation-expired-without-cleanup"));
        });
    });
    m_queueExpiry.setSingleShot(true);
    connect(&m_queueExpiry, &QTimer::timeout, this, [this] {
      if (m_pendingCheck)
        finishQtQueue(m_pendingCheck->request, false);
      else if (m_preparationPending)
        finishPreparationQueue(m_preparationPending->request, false);
    });
  } catch (...) {
    ::close(std::exchange(m_descriptor, -1));
    throw;
  }
}
ViewerViewportEndpoint::~ViewerViewportEndpoint()
{
  onClosed = {};
  close();
}
void ViewerViewportEndpoint::close(QString reason)
{
  if (m_descriptor < 0)
    return;
  m_closeReason = reason.left(192).replace('\n', ' ').replace('\r', ' ');
  m_read->setEnabled(false);
  m_write->setEnabled(false);
  ::close(std::exchange(m_descriptor, -1));
  m_expiry.stop();
  m_queueExpiry.stop();
  m_preparationPoll.stop();
  m_preparationExpiry.stop();
  m_dragExpiry.stop();
  m_dragPending.clear();
  m_dragRetired.clear();
  m_dragWatches.clear();
  m_dragRetiredWatches.clear();
  m_dragRetiredWatchOrder.clear();
  m_preparationPending.reset();
  m_preparation.reset();
  m_queueWitness.cancel();
  m_pendingCheck.reset();
  m_snapshot = 0;
  m_state.reset();
  m_seal.reset();
  m_output.clear();
  const auto callback = std::move(onClosed);
  try {
    if (callback)
      callback();
  } catch (...) {
    // The descriptor and retained state are already terminal. A lifecycle
    // observer must not unwind through Qt's socket notifier or our destructor.
  }
}
bool ViewerViewportEndpoint::send(const GnomeViewportReply &reply)
{
  return sendObject(gnomeViewportJson(reply));
}
bool ViewerViewportEndpoint::sendObject(const QJsonObject &object)
{
  if (!live())
    return false;
  try {
    const auto frame = gnomeViewportFrame(object);
    if (m_output.size() + frame.size() > 2 * (gnomeViewportMaximumPayload + 4)) {
      close(QStringLiteral("output-capacity-exceeded"));
      return false;
    }
    m_output += frame;
    const QPointer<ViewerViewportEndpoint> alive(this);
    write();
    return alive && live();
  } catch (const std::exception &error) {
    close(QStringLiteral("output-invalid: ") + QString::fromUtf8(error.what()).left(160));
    return false;
  } catch (...) {
    close(QStringLiteral("output-invalid"));
    return false;
  }
}
bool ViewerViewportEndpoint::requestWindowDrag(
    GnomeWindowDragRequest request, std::function<void(const GnomeWindowDragReply &)> finished
)
{
  if (!live() || !finished || request.request || m_dragPending.size() >= 16 ||
      m_dragRequest == ((std::uint64_t{1} << 53) - 1))
    return false;
  for (const auto &[id, pending] : m_dragPending)
    if (pending.binding.association == request.association)
      return false;
  if (request.action == GnomeWindowDragAction::Watch && !m_dragWatches.contains(request.association)) {
    const auto pendingWatches = std::count_if(m_dragPending.begin(), m_dragPending.end(), [](const auto &item) {
      return item.second.binding.action == GnomeWindowDragAction::Watch;
    });
    if (m_dragWatches.size() + pendingWatches >= 16)
      return false;
  }
  request.request = ++m_dragRequest;
  try {
    const auto object = gnomeWindowDragJson(request);
    m_dragPending.emplace(request.request, DragPending{request, QDeadlineTimer(250), std::move(finished)});
    if (request.action == GnomeWindowDragAction::Unwatch) {
      const auto old = m_dragWatches.find(request.association);
      auto retired = old != m_dragWatches.end() ? old->second : DragWatch{};
      retired.active = retired.enabled = false;
      m_dragRetiredWatches[request.association] = retired;
      std::erase(m_dragRetiredWatchOrder, request.association);
      m_dragRetiredWatchOrder.push_back(request.association);
      if (old != m_dragWatches.end())
        m_dragWatches.erase(old);
      if (m_dragRetiredWatchOrder.size() > 16) {
        m_dragRetiredWatches.erase(m_dragRetiredWatchOrder.front());
        m_dragRetiredWatchOrder.pop_front();
      }
    }
    m_dragExpiry.start();
    return sendObject(object);
  } catch (...) {
    m_dragPending.erase(request.request);
    return false;
  }
}
void ViewerViewportEndpoint::expireWindowDrags()
{
  const QPointer<ViewerViewportEndpoint> alive(this);
  for (auto it = m_dragPending.begin(); it != m_dragPending.end();) {
    if (!it->second.deadline.hasExpired()) {
      ++it;
      continue;
    }
    auto pending = std::move(it->second);
    it = m_dragPending.erase(it);
    m_dragRetired.emplace(pending.binding.request, pending.binding);
    if (m_dragRetired.size() > 16)
      m_dragRetired.erase(m_dragRetired.begin());
    pending.finished(GnomeWindowDragReply{pending.binding, false, {}, 0});
    if (!alive || !live())
      return;
    // A callback may modify the map, including the next entry.
    it = m_dragPending.begin();
  }
  if (m_dragPending.empty())
    m_dragExpiry.stop();
}
void ViewerViewportEndpoint::handleWindowDrag(const QJsonObject &object)
{
  if (object.value("kind") == QJsonValue("event")) {
    const auto event = parseGnomeWindowDragEvent(object);
    const auto found = m_dragWatches.find(event.association);
    if (found == m_dragWatches.end() && m_dragRetiredWatches.contains(event.association))
      return;
    if (found == m_dragWatches.end())
      throw std::runtime_error("Unrequested native window drag observation");
    auto &watch = found->second;
    if (!watch.enabled)
      return; // The retired watcher cannot grant authority through a late event.
    if (event.phase == GnomeWindowDragEvent::Phase::Begin) {
      if (watch.active || event.grab <= watch.grab)
        throw std::runtime_error("Replayed native window drag begin");
      watch = {event.grab, event.sequence, true, true};
    } else {
      if (!watch.active || event.grab != watch.grab || event.sequence < watch.sequence ||
          (event.sequence == watch.sequence && event.phase != GnomeWindowDragEvent::Phase::Cancel))
        throw std::runtime_error("Changed native window drag observation");
      watch.sequence = event.sequence;
      if (event.phase != GnomeWindowDragEvent::Phase::Motion)
        watch.active = false;
    }
    const auto callback = onWindowDrag;
    if (callback)
      callback(event);
    return;
  }
  const auto reply = parseGnomeWindowDragReply(object);
  const auto found = m_dragPending.find(reply.binding.request);
  if (found == m_dragPending.end()) {
    const auto retired = m_dragRetired.find(reply.binding.request);
    if (retired == m_dragRetired.end() || retired->second != reply.binding)
      throw std::runtime_error("Unexpected native window drag reply");
    m_dragRetired.erase(retired);
    return;
  }
  if (found->second.binding != reply.binding)
    throw std::runtime_error("Native window drag reply changed its binding");
  auto pending = std::move(found->second);
  m_dragPending.erase(found);
  auto result = reply;
  if (pending.deadline.hasExpired())
    result.ok = false;
  if (result.ok) {
    if (reply.binding.action == GnomeWindowDragAction::Watch) {
      auto retired = m_dragRetiredWatches.extract(reply.binding.association);
      std::erase(m_dragRetiredWatchOrder, reply.binding.association);
      auto &watch = m_dragWatches[reply.binding.association];
      if (!retired.empty())
        watch = retired.mapped();
      watch.enabled = true;
    }
  }
  pending.finished(result);
}
void ViewerViewportEndpoint::write()
{
  while (live() && !m_output.isEmpty()) {
#ifdef MSG_NOSIGNAL
    const auto count = ::send(m_descriptor, m_output.constData(), m_output.size(), MSG_NOSIGNAL);
#else
    const auto count = ::send(m_descriptor, m_output.constData(), m_output.size(), 0);
#endif
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      break;
    if (count <= 0) {
      close(QStringLiteral("socket-write-failed"));
      return;
    }
    m_output.remove(0, count);
  }
  if (live())
    m_write->setEnabled(!m_output.isEmpty());
}
void ViewerViewportEndpoint::invalidate(const QString &reason)
{
  invalidateState(reason, false);
}
void ViewerViewportEndpoint::forwardingFenceChanged()
{
  invalidateState(QStringLiteral("forwarding-fence-changed"), true);
}
void ViewerViewportEndpoint::invalidateState(const QString &reason, bool forwardingOnly)
{
  const QPointer<ViewerViewportEndpoint> alive(this);
  if (m_preparation && m_preparation->focused && !m_preparation->promoted &&
      (!m_preparationPending || m_preparationPending->operation != GnomeViewportOperation::PreparationCleanup)) {
    const auto expected = *m_preparation->focused;
    const auto snapshot = onSnapshot;
    const auto actual = snapshot ? snapshot() : std::nullopt;
    if (!alive)
      return;
    if (actual != std::optional(expected)) {
      // An unfocused preparation cannot be silently rebound to another focus.
      // Closing requests owned Bridge teardown, never a successful release.
      close(QStringLiteral("prepared-focus-changed: ") + reason.left(128));
      return;
    }
  }
  retireSeal(reason);
  if (alive)
    invalidateSnapshot(reason, forwardingOnly);
}
void ViewerViewportEndpoint::retireSeal(const QString &reason)
{
  const auto seal = std::exchange(m_seal, {});
  if (!seal || !live())
    return;
  GnomeViewportReply reply;
  reply.operation = GnomeViewportOperation::SealInvalidated;
  reply.cutoff = seal->origin;
  reply.reason = reason;
  send(reply);
}
void ViewerViewportEndpoint::invalidateSnapshot(const QString &reason, bool preservePreparationQueue)
{
  const QPointer<ViewerViewportEndpoint> alive(this);
  const bool preparationQueue = m_preparation && m_preparationPending && !m_pendingCheck &&
                                m_preparation->binding == m_preparationPending->preparation &&
                                (m_preparationPending->operation == GnomeViewportOperation::PreparationDrain ||
                                 m_preparationPending->operation == GnomeViewportOperation::PreparationCleanup);
  // Forwarding may resume while its separate preparation gate is still closed.
  // Do not cancel that gate's in-flight native witness merely because All
  // changed. Its original timer and fresh exact-fence check remain required.
  // Geometry, focus, connection and other invalidations still cancel it.
  if (!(preservePreparationQueue && preparationQueue)) {
    if (preparationQueue) {
      finishPreparationQueue(m_preparationPending->request, false);
      if (!alive)
        return;
    }
    m_queueWitness.cancel();
    if (!alive)
      return;
    m_queueExpiry.stop();
  }
  if (const auto pending = std::exchange(m_pendingCheck, std::nullopt); pending && live()) {
    GnomeViewportReply denied;
    denied.operation = pending->request.operation;
    denied.request = pending->request.request;
    denied.snapshot = pending->request.snapshot;
    denied.cutoff = pending->request.cutoff;
    denied.seal = pending->request.seal;
    denied.reason = QStringLiteral("qt-queue-invalidated");
    const QPointer<ViewerViewportEndpoint> alive(this);
    send(denied);
    if (!alive)
      return;
  }
  if (!m_snapshot)
    return;
  const auto snapshot = std::exchange(m_snapshot, 0);
  m_state.reset();
  m_expiry.stop();
  GnomeViewportReply reply;
  reply.operation = GnomeViewportOperation::Invalidated;
  reply.snapshot = snapshot;
  reply.reason = reason;
  send(reply);
}
bool ViewerViewportEndpoint::current(std::uint64_t snapshot)
{
  if (!live() || !m_state || snapshot != m_snapshot || m_deadline.hasExpired())
    return false;
  const auto check = onCheck;
  const auto state = *m_state;
  const QPointer<ViewerViewportEndpoint> alive(this);
  const bool valid = check && check(state);
  return alive && live() && valid && m_state && snapshot == m_snapshot && *m_state == state && !m_deadline.hasExpired();
}
bool ViewerViewportEndpoint::startWaylandQueue(
    QWindow *window, const WaylandViewportGeometry &expected, std::function<void(bool)> finished
)
{
  return m_queueWitness.begin(window, expected, std::move(finished));
}
bool ViewerViewportEndpoint::startWaylandInputDrain(
    QWindow *window, const WaylandViewportGeometry &expected, QDeadlineTimer deadline, std::function<bool()> exactFence,
    std::function<void(bool)> finished
)
{
  return m_queueWitness.beginInputDrain(window, expected, deadline, std::move(exactFence), std::move(finished));
}
bool ViewerViewportEndpoint::currentInputDrain(const GnomeViewportRequest &request, QDeadlineTimer deadline)
{
  const QPointer<ViewerViewportEndpoint> alive(this);
  if (deadline.hasExpired() || !validInputCutoffBinding(request.cutoff) || !current(request.snapshot) || !alive)
    return false;
  const auto state = *m_state;
  const auto &binding = request.cutoff.forwarding;
  if (request.cutoff.viewerConnection != state.connection || binding.owner != Owner{state.session, state.epoch} ||
      binding.focusRequest != state.focusRequest || binding.focusRevision != state.focusRevision ||
      binding.geometry != state.geometry)
    return false;
  const auto check = onInputDrainCheck;
  const bool valid = check && check(state, request.cutoff);
  return alive && live() && valid && m_state && *m_state == state && request.snapshot == m_snapshot &&
         !m_deadline.hasExpired() && !deadline.hasExpired();
}
bool ViewerViewportEndpoint::currentInputMap(const GnomeViewportRequest &request, QDeadlineTimer deadline)
{
  if (!m_seal || request.seal != m_seal->origin.request || !continuesInputCutoffSeal(m_seal->origin, request.cutoff))
    return false;
  const auto origin = m_seal->origin;
  const auto state = m_seal->state;
  const QPointer<ViewerViewportEndpoint> alive(this);
  const bool current = currentInputDrain(request, deadline);
  return alive && current && m_seal && m_seal->origin == origin && m_seal->state == state && m_state &&
         *m_state == state;
}
void ViewerViewportEndpoint::finishQtQueue(const GnomeViewportRequest &request, bool observed)
{
  if (!m_pendingCheck || m_pendingCheck->request != request || !live())
    return; // An old callback cannot complete a later operation.
  if (m_pendingCheck->starting) {
    if (!m_pendingCheck->completion)
      m_pendingCheck->completion = observed;
    return; // The starter must return success before a synchronous receipt.
  }
  const auto pending = std::exchange(m_pendingCheck, std::nullopt);
  m_queueExpiry.stop();
  m_queueWitness.cancel();
  GnomeViewportReply reply;
  reply.operation = pending->request.operation;
  reply.request = pending->request.request;
  reply.snapshot = pending->request.snapshot;
  reply.cutoff = pending->request.cutoff;
  reply.seal = pending->request.seal;
  const QPointer<ViewerViewportEndpoint> alive(this);
  reply.ok =
      observed && !pending->deadline.hasExpired() &&
      (reply.operation == GnomeViewportOperation::InputDrain || reply.operation == GnomeViewportOperation::InputSeal
           ? currentInputDrain(pending->request, pending->deadline)
           : current(pending->request.snapshot));
  if (!alive)
    return;
  if (!reply.ok)
    reply.reason = QStringLiteral("qt-queue-incomplete-or-viewport-changed");
  else if (reply.operation == GnomeViewportOperation::InputSeal)
    m_seal = RetainedSeal{request.cutoff, request.cutoff, *m_state};
  send(reply);
}

void ViewerViewportEndpoint::replyPreparation(const GnomeViewportRequest &request, bool ok, const QString &why)
{
  GnomeViewportReply reply;
  reply.operation = request.operation;
  reply.request = request.request;
  reply.preparation = request.preparation;
  reply.target = request.target;
  reply.cutoff = request.cutoff;
  reply.ok = ok;
  reply.reason = why.isEmpty() ? QStringLiteral("preparation-unavailable") : why;
  send(reply);
}
bool ViewerViewportEndpoint::currentPreparation(const GnomePreparationTarget &target)
{
  if (!live() || !m_preparation ||
      std::find(m_preparation->inventory.targets.begin(), m_preparation->inventory.targets.end(), target) ==
          m_preparation->inventory.targets.end())
    return false;
  const auto binding = m_preparation->binding;
  const auto callback = onPreparationCheck;
  const QPointer<ViewerViewportEndpoint> alive(this);
  const bool valid = callback && callback(binding, target);
  return alive && live() && m_preparation && m_preparation->binding == binding && valid;
}
void ViewerViewportEndpoint::finishPreparationQueue(std::uint64_t id, bool complete)
{
  if (!m_preparation || !m_preparationPending || m_preparationPending->request != id)
    return;
  const auto request = *m_preparationPending;
  const QPointer<ViewerViewportEndpoint> alive(this);
  const bool valid = complete && !m_preparationOperationDeadline.hasExpired() && currentPreparation(request.target);
  if (!alive)
    return;
  if (!m_preparationPending || m_preparationPending->request != id)
    return;
  m_preparationPending.reset();
  m_queueExpiry.stop();
  if (!valid) {
    replyPreparation(request, false, QStringLiteral("preparation-queue-incomplete"));
    return;
  }
  if (request.operation == GnomeViewportOperation::PreparationCleanup) {
    const auto finish = onPreparationFinish;
    const bool retired = finish && finish(request.preparation, request.target, std::nullopt);
    if (!alive)
      return;
    if (!retired || !m_preparation || m_preparation->binding != request.preparation) {
      replyPreparation(request, false, QStringLiteral("preparation-cleanup-failed"));
      return;
    }
    m_preparation.reset();
    m_preparationExpiry.stop();
    replyPreparation(request, true);
  } else {
    m_preparation->drained = true;
    replyPreparation(request, true);
  }
}
void ViewerViewportEndpoint::pollPreparationFocus()
{
  if (!m_preparation || !m_preparationPending ||
      m_preparationPending->operation != GnomeViewportOperation::PreparationFocus) {
    m_preparationPoll.stop();
    return;
  }
  const auto request = *m_preparationPending;
  const QPointer<ViewerViewportEndpoint> alive(this);
  if (m_preparationOperationDeadline.hasExpired() || !currentPreparation(request.target)) {
    if (!alive)
      return;
    m_preparationPoll.stop();
    m_preparationPending.reset();
    replyPreparation(request, false, QStringLiteral("preparation-focus-expired"));
    return;
  }
  const auto focus = onPreparationFocus;
  const auto state = focus ? focus(request.preparation, request.target) : std::nullopt;
  if (!alive || !m_preparation || !m_preparationPending || m_preparationPending->request != request.request)
    return;
  if (!state)
    return;
  if (!currentPreparation(request.target) || !alive || !m_preparation || m_preparationOperationDeadline.hasExpired())
    return;
  if (state->connection != m_preparation->inventory.connection ||
      state->peerFingerprint != m_preparation->inventory.peerFingerprint) {
    close(QStringLiteral("preparation-focus-peer-changed"));
    return;
  }
  m_preparation->focused = state;
  m_preparationPoll.stop();
  m_preparationPending.reset();
  GnomeViewportReply reply;
  reply.operation = request.operation;
  reply.request = request.request;
  reply.preparation = request.preparation;
  reply.target = request.target;
  reply.ok = true;
  reply.state = *state;
  send(reply);
}
void ViewerViewportEndpoint::handlePreparation(const GnomeViewportRequest &request)
{
  const QPointer<ViewerViewportEndpoint> alive(this);
  if (request.operation == GnomeViewportOperation::PreparationInventory) {
    if (m_preparation || m_seal) {
      replyPreparation(request, false, QStringLiteral("preparation-busy"));
      return;
    }
    invalidateSnapshot(QStringLiteral("preparation-started"));
    if (!alive || !live())
      return;
    Preparation prep;
    prep.binding = request.preparation;
    prep.deadline = QDeadlineTimer(request.budgetMs, Qt::PreciseTimer);
    m_preparation = std::move(prep);
    const auto callback = onPreparationInventory;
    const auto value = callback ? callback(request.preparation) : std::nullopt;
    if (!alive || !live())
      return;
    if (!value) {
      close(QStringLiteral("preparation-inventory-gate-unavailable"));
      return;
    } // Partial or unsupported gate adoption is terminal.
    if (value->targets.empty()) {
      m_preparation.reset();
      replyPreparation(request, false, QStringLiteral("preparation-no-target"));
      return;
    }
    if (!m_preparation || m_preparation->binding != request.preparation || m_preparation->deadline.hasExpired()) {
      close(QStringLiteral("preparation-inventory-expired-or-replaced"));
      return;
    }
    m_preparation->inventory = *value;
    m_preparationExpiry.start(static_cast<int>(m_preparation->deadline.remainingTime()));
    GnomeViewportReply reply;
    reply.operation = request.operation;
    reply.request = request.request;
    reply.preparation = request.preparation;
    reply.ok = true;
    reply.inventory = *value;
    send(reply);
    return;
  }
  if (!m_preparation || m_preparation->binding != request.preparation) {
    replyPreparation(request, false, QStringLiteral("preparation-stale"));
    return;
  }
  const bool cleanup = request.operation == GnomeViewportOperation::PreparationCleanup;
  if ((!cleanup && m_preparation->deadline.hasExpired()) || !currentPreparation(request.target)) {
    if (alive)
      replyPreparation(request, false, QStringLiteral("preparation-target-changed"));
    return;
  }
  m_preparationOperationDeadline = QDeadlineTimer(request.budgetMs, Qt::PreciseTimer);
  if (!cleanup && m_preparation->deadline < m_preparationOperationDeadline)
    m_preparationOperationDeadline = m_preparation->deadline;
  if (m_preparation->target && *m_preparation->target != request.target) {
    replyPreparation(request, false, QStringLiteral("preparation-target-substituted"));
    return;
  }
  m_preparation->target = request.target;
  if (request.operation == GnomeViewportOperation::PreparationMap) {
    const auto callback = onPreparationMap;
    const auto mapped =
        callback ? callback(request.preparation, request.target, QPointF(request.x, request.y)) : std::nullopt;
    if (!alive)
      return;
    if (!mapped || m_preparationOperationDeadline.hasExpired() || !currentPreparation(request.target)) {
      if (alive)
        replyPreparation(request, false, QStringLiteral("preparation-map-unavailable"));
      return;
    }
    GnomeViewportReply reply;
    reply.operation = request.operation;
    reply.request = request.request;
    reply.preparation = request.preparation;
    reply.target = request.target;
    reply.ok = true;
    reply.x = mapped->x();
    reply.y = mapped->y();
    send(reply);
    return;
  }
  if (request.operation == GnomeViewportOperation::PreparationDrain || cleanup) {
    if (cleanup)
      m_preparationExpiry.stop();
    m_preparationOperationDeadline = std::min(m_preparationOperationDeadline, QDeadlineTimer(250, Qt::PreciseTimer));
    m_preparationPending = request;
    m_queueExpiry.start(static_cast<int>(std::max<qint64>(1, m_preparationOperationDeadline.remainingTime())));
    const auto callback = onPreparationDrain;
    const bool started = callback && callback(
                                         request.preparation, request.target, m_preparationOperationDeadline,
                                         [alive, id = request.request](bool ok) {
                                           if (alive)
                                             alive->finishPreparationQueue(id, ok);
                                         }
                                     );
    if (alive && !started)
      finishPreparationQueue(request.request, false);
    return;
  }
  if (request.operation == GnomeViewportOperation::PreparationFocus) {
    if (!m_preparation->drained || m_preparation->promoted) {
      replyPreparation(request, false, QStringLiteral("preparation-prefix-not-drained"));
      return;
    }
    m_preparationPending = request;
    m_preparationPoll.start();
    pollPreparationFocus();
    return;
  }
  if (request.operation == GnomeViewportOperation::PreparationPromote) {
    const auto state = m_preparation->focused;
    const auto &all = request.cutoff;
    if (!state || m_preparation->promoted || !m_preparation->drained || all.viewerConnection != state->connection ||
        all.forwarding.owner != Owner{state->session, state->epoch} || all.forwarding.geometry != state->geometry ||
        all.forwarding.focusRequest != state->focusRequest || all.forwarding.focusRevision != state->focusRevision) {
      replyPreparation(request, false, QStringLiteral("preparation-promotion-mismatch"));
      return;
    }
    const auto callback = onPreparationFinish;
    const bool finished = callback && callback(request.preparation, request.target, all);
    if (!alive)
      return;
    if (!finished || !m_preparation || m_preparationOperationDeadline.hasExpired()) {
      replyPreparation(request, false, QStringLiteral("preparation-promotion-failed"));
      return;
    }
    m_preparation->promoted = all;
    m_preparationExpiry.stop();
    replyPreparation(request, true);
    return;
  }
  close(QStringLiteral("unexpected-preparation-operation"));
}

void ViewerViewportEndpoint::handle(const GnomeViewportRequest &request)
{
  // Only one native queue operation may be outstanding. Pipelining while
  // callbacks are unresolved could reorder response identities or overrun
  // the snapshot deadline. Fail the inherited private stream closed.
  const bool cancelPreparation = request.operation == GnomeViewportOperation::PreparationCleanup && m_preparation &&
                                 m_preparation->binding == request.preparation;
  if ((m_pendingCheck && !cancelPreparation) || (m_preparationPending && !cancelPreparation) ||
      request.request <= m_lastRequest) {
    close(request.request <= m_lastRequest ? QStringLiteral("request-replayed") : QStringLiteral("request-overlapped"));
    return;
  }
  if (cancelPreparation) {
    const QPointer<ViewerViewportEndpoint> alive(this);
    const auto pending = std::exchange(m_preparationPending, {});
    m_preparationPoll.stop();
    m_queueWitness.cancel();
    if (pending)
      replyPreparation(*pending, false, QStringLiteral("preparation-canceled"));
    if (!alive || !live())
      return;
    invalidateSnapshot(QStringLiteral("preparation-cleanup"));
    if (!alive || !live())
      return;
  }
  m_lastRequest = request.request;
  if (isGnomePreparationOperation(request.operation)) {
    handlePreparation(request);
    return;
  }
  const QPointer<ViewerViewportEndpoint> alive(this);
  GnomeViewportReply reply;
  reply.operation = request.operation;
  reply.request = request.request;
  reply.snapshot = request.snapshot;
  reply.cutoff = request.cutoff;
  reply.seal = request.seal;
  reply.reason = QStringLiteral("viewport-unavailable");
  if (request.operation == GnomeViewportOperation::Snapshot) {
    invalidateSnapshot(QStringLiteral("snapshot-replaced"));
    if (!alive || !live())
      return;
    const QDeadlineTimer deadline(gnomeViewportLifetimeMs, Qt::PreciseTimer);
    const auto callback = onSnapshot;
    const auto state = callback ? callback() : std::nullopt;
    if (!alive || !live())
      return;
    if (state && !deadline.hasExpired()) {
      m_state = *state;
      m_snapshot = request.request;
      m_deadline = deadline;
      if (current(m_snapshot)) {
        if (!alive)
          return;
        reply.ok = true;
        reply.state = *state;
        m_expiry.start(static_cast<int>(std::max<qint64>(1, deadline.remainingTime())));
      } else {
        if (!alive)
          return;
        m_state.reset();
        m_snapshot = 0;
      }
    }
  } else {
    // Take the inherited budget before any external validation callback. The
    // retained snapshot is an independent earlier absolute deadline.
    const bool inputDrain = request.operation == GnomeViewportOperation::InputDrain ||
                            request.operation == GnomeViewportOperation::InputSeal;
    const bool inputMap = request.operation == GnomeViewportOperation::InputMap;
    const bool inputMapBatch = request.operation == GnomeViewportOperation::InputMapBatch;
    const auto deadline = inputDrain || inputMap || inputMapBatch
                              ? std::min(m_deadline, QDeadlineTimer(request.budgetMs, Qt::PreciseTimer))
                              : m_deadline;
    const bool valid = current(request.snapshot);
    if (!alive || !live())
      return;
    if (valid && (request.operation == GnomeViewportOperation::Check || inputDrain)) {
      const bool exact = !inputDrain || currentInputDrain(request, deadline);
      if (!alive || !live())
        return;
      if (exact && (inputDrain ? bool(onInputDrain) : bool(onQtQueue))) {
        if (inputDrain) {
          retireSeal(QStringLiteral("seal-replaced"));
          if (!alive || !live())
            return;
        }
        const auto start = onQtQueue;
        const auto startDrain = onInputDrain;
        const auto state = *m_state;
        m_pendingCheck = PendingQueue{request, deadline, true, std::nullopt};
        m_queueExpiry.start(static_cast<int>(std::max<qint64>(1, deadline.remainingTime())));
        const auto finished = [alive, request](bool observed) {
          if (alive)
            alive->finishQtQueue(request, observed);
        };
        const bool started =
            inputDrain ? startDrain(state, request.cutoff, deadline, finished) : start(state, finished);
        if (!alive || !live())
          return;
        if (!m_pendingCheck || m_pendingCheck->request != request)
          return; // A callback invalidated or replaced the retained operation.
        m_pendingCheck->starting = false;
        if (started) {
          if (const auto completed = m_pendingCheck->completion)
            finishQtQueue(request, *completed);
          return; // The native Wayland callbacks arrive asynchronously.
        }
        m_queueExpiry.stop();
        m_queueWitness.cancel();
        m_pendingCheck.reset();
        reply.reason = QStringLiteral("qt-wayland-queue-unavailable");
      } else if (inputDrain) {
        reply.reason = exact ? QStringLiteral("qt-input-drain-unavailable") : QStringLiteral("all-fence-not-current");
      } else {
        // Legacy/fixture-only Check remains a read-only geometry observation.
        // The real GNOME Bridge always supplies onQtQueue and refuses it
        // when its exact QtWayland private-API dependency is missing.
        reply.ok = true;
      }
    } else if (valid && inputMapBatch) {
      if (!m_seal || m_seal->origin != request.cutoff || request.points.empty() ||
          !continuesInputCutoffSeal(m_seal->last, request.points.front().cutoff)) {
        reply.reason = QStringLiteral("prefix-seal-not-current");
      } else {
        // Consume the whole exact ordered attempt once. A failure publishes
        // no partial point list and cannot retry any already checked stamp.
        m_seal->last = request.points.back().cutoff;
        const auto callback = onMap;
        const auto expected = *m_state;
        bool complete = true;
        for (const auto &item : request.points) {
          auto pointRequest = request;
          pointRequest.cutoff = item.cutoff;
          pointRequest.seal = request.cutoff.request;
          const bool before = currentInputMap(pointRequest, deadline);
          if (!alive || !live())
            return;
          if (!before) {
            complete = false;
            break;
          }
          const auto point = callback ? callback(expected, QPointF(item.x, item.y)) : std::nullopt;
          if (!alive || !live())
            return;
          const bool after = currentInputMap(pointRequest, deadline);
          if (!alive || !live())
            return;
          if (!point || !after) {
            complete = false;
            break;
          }
          reply.points.push_back({item.cutoff, point->x(), point->y()});
        }
        reply.ok = complete;
        if (!complete) {
          reply.points.clear();
          reply.reason = QStringLiteral("batch-point-or-prefix-changed");
        }
      }
    } else if (valid) {
      if (inputMap) {
        const bool exact =
            m_seal && continuesInputCutoffSeal(m_seal->last, request.cutoff) && currentInputMap(request, deadline);
        if (!alive || !live())
          return;
        if (!exact) {
          reply.reason = QStringLiteral("prefix-seal-not-current");
          send(reply);
          return;
        }
        // Consume each stamped map attempt once, including rejected points.
        m_seal->last = request.cutoff;
      }
      const auto callback = onMap;
      const auto expected = *m_state;
      const auto point = callback ? callback(expected, QPointF(request.x, request.y)) : std::nullopt;
      if (!alive || !live())
        return;
      const bool stillCurrent = inputMap ? currentInputMap(request, deadline) : current(request.snapshot);
      if (!alive || !live())
        return;
      if (point && stillCurrent) {
        reply.ok = true;
        reply.x = point->x();
        reply.y = point->y();
      } else
        reply.reason = QStringLiteral("point-outside-painted-content-or-state-changed");
    } else
      reply.reason = QStringLiteral("stale-snapshot");
  }
  if (alive && live())
    send(reply);
}
void ViewerViewportEndpoint::read()
{
  if (!live() || m_reading)
    return;
  m_reading = true;
  const QPointer<ViewerViewportEndpoint> alive(this);
  try {
    unsigned messages = 0;
    for (unsigned batch = 0; batch < 8 && live(); ++batch) {
      char bytes[4096];
      const auto count = ::recv(m_descriptor, bytes, sizeof(bytes), 0);
      if (count < 0 && errno == EINTR)
        continue;
      if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        break;
      if (count <= 0) {
        close(count == 0 ? QStringLiteral("socket-peer-closed") : QStringLiteral("socket-read-failed"));
        return;
      }
      m_decoder.append(QByteArray(bytes, count));
      while (auto object = m_decoder.take()) {
        if (++messages > 16) {
          close(QStringLiteral("request-batch-exceeded"));
          return;
        }
        if (isGnomeWindowDrag(*object))
          handleWindowDrag(*object);
        else
          handle(parseGnomeViewportRequest(*object));
        if (!alive || !live())
          return;
      }
    }
    m_reading = false;
  } catch (const std::exception &error) {
    if (alive)
      close(QStringLiteral("request-invalid: ") + QString::fromUtf8(error.what()).left(160));
  } catch (...) {
    if (alive)
      close(QStringLiteral("request-invalid"));
  }
}
} // namespace deskflow::windowshare
