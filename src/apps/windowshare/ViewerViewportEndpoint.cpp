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
  const QSet<QString> allowed{"gnome-viewport-fd", "enable-seamless", "headless", "connect",       "port",
                              "identity",          "peer-pin",        "screen",   "export-window", "quit-after"};
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
    m_expiry.setSingleShot(true);
    connect(&m_expiry, &QTimer::timeout, this, [this] { invalidate(QStringLiteral("snapshot-expired")); });
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
void ViewerViewportEndpoint::close()
{
  if (m_descriptor < 0)
    return;
  m_read->setEnabled(false);
  m_write->setEnabled(false);
  ::close(std::exchange(m_descriptor, -1));
  m_expiry.stop();
  m_snapshot = 0;
  m_state.reset();
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
  if (!live())
    return false;
  try {
    const auto frame = gnomeViewportFrame(gnomeViewportJson(reply));
    if (m_output.size() + frame.size() > 2 * (gnomeViewportMaximumPayload + 4)) {
      close();
      return false;
    }
    m_output += frame;
    const QPointer<ViewerViewportEndpoint> alive(this);
    write();
    return alive && live();
  } catch (...) {
    close();
    return false;
  }
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
      close();
      return;
    }
    m_output.remove(0, count);
  }
  if (live())
    m_write->setEnabled(!m_output.isEmpty());
}
void ViewerViewportEndpoint::invalidate(const QString &reason)
{
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
void ViewerViewportEndpoint::handle(const GnomeViewportRequest &request)
{
  if (request.request <= m_lastRequest) {
    close();
    return;
  }
  m_lastRequest = request.request;
  const QPointer<ViewerViewportEndpoint> alive(this);
  GnomeViewportReply reply;
  reply.operation = request.operation;
  reply.request = request.request;
  reply.snapshot = request.snapshot;
  reply.reason = QStringLiteral("viewport-unavailable");
  if (request.operation == GnomeViewportOperation::Snapshot) {
    invalidate(QStringLiteral("snapshot-replaced"));
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
    const bool valid = current(request.snapshot);
    if (!alive || !live())
      return;
    if (valid && request.operation == GnomeViewportOperation::Check)
      reply.ok = true;
    else if (valid) {
      const auto callback = onMap;
      const auto expected = *m_state;
      const auto point = callback ? callback(expected, QPointF(request.x, request.y)) : std::nullopt;
      if (!alive || !live())
        return;
      const bool stillCurrent = current(request.snapshot);
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
        close();
        return;
      }
      m_decoder.append(QByteArray(bytes, count));
      while (auto object = m_decoder.take()) {
        if (++messages > 16) {
          close();
          return;
        }
        handle(parseGnomeViewportRequest(*object));
        if (!alive || !live())
          return;
      }
    }
    m_reading = false;
  } catch (...) {
    if (alive)
      close();
  }
}
} // namespace deskflow::windowshare
