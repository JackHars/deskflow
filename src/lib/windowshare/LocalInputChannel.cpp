/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputChannel.h"
#include <QFile>
#include <QPointer>
#include <QUuid>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#ifdef Q_OS_MACOS
#include <libproc.h>
#endif

namespace deskflow::windowshare {
namespace {
constexpr std::uint32_t magic = 0x444c4931;

template <std::size_t N> bool nonzero(const std::array<std::uint8_t, N> &value)
{
  return std::any_of(value.begin(), value.end(), [](auto byte) { return byte != 0; });
}
void require(bool condition, const char *reason)
{
  if (!condition)
    throw std::runtime_error(reason);
}
template <typename Callback, typename... Args>
void notify(LocalInputChannel *channel, Callback callback, Args... values)
{
  // Keep the callable and arguments alive if it destroys the owning channel.
  const QPointer<LocalInputChannel> alive(channel);
  try {
    if (callback)
      callback(values...);
  } catch (...) {
    if (alive)
      alive->close(QStringLiteral("Local input callback failed"));
  }
}
struct Writer
{
  QByteArray data;
  void number(std::uint64_t value, int bytes)
  {
    for (int i = bytes - 1; i >= 0; --i)
      data.append(static_cast<char>(value >> (i * 8)));
  }
  template <std::size_t N> void fixed(const std::array<std::uint8_t, N> &value)
  {
    data.append(reinterpret_cast<const char *>(value.data()), N);
  }
};
struct Reader
{
  const QByteArray &data;
  qsizetype offset = 0;
  std::uint64_t number(int bytes)
  {
    require(bytes <= data.size() - offset, "Truncated local input payload");
    std::uint64_t value = 0;
    for (int i = 0; i < bytes; ++i)
      value = (value << 8) | static_cast<unsigned char>(data[offset++]);
    return value;
  }
  template <std::size_t N> std::array<std::uint8_t, N> fixed()
  {
    require(static_cast<qsizetype>(N) <= data.size() - offset, "Truncated local input identity");
    std::array<std::uint8_t, N> result;
    std::copy_n(reinterpret_cast<const std::uint8_t *>(data.constData() + offset), N, result.begin());
    offset += N;
    return result;
  }
  void end() const
  {
    require(offset == data.size(), "Trailing local input data");
  }
};
LocalInputIncarnation incarnation()
{
  const auto value = QUuid::createUuid().toRfc4122();
  LocalInputIncarnation result;
  std::copy_n(reinterpret_cast<const std::uint8_t *>(value.constData()), result.size(), result.begin());
  return result;
}
QByteArray encodeLease(const LocalInputLease &lease)
{
  Writer out;
  out.fixed(lease.deskflowClientIncarnation);
  out.fixed(lease.windowShareClientIncarnation);
  out.fixed(lease.deskflowPeerPin);
  out.fixed(lease.windowSharePeerPin);
  for (auto value : {lease.session, lease.epoch, lease.focusRevision, lease.geometryRevision, lease.routeGeneration})
    out.number(value, 8);
  return out.data;
}
LocalInputLease decodeLease(Reader &in)
{
  LocalInputLease lease;
  lease.deskflowClientIncarnation = in.fixed<16>();
  lease.windowShareClientIncarnation = in.fixed<16>();
  lease.deskflowPeerPin = in.fixed<32>();
  lease.windowSharePeerPin = in.fixed<32>();
  lease.session = in.number(8);
  lease.epoch = in.number(8);
  lease.focusRevision = in.number(8);
  lease.geometryRevision = in.number(8);
  lease.routeGeneration = in.number(8);
  require(validLocalInputLease(lease), "Invalid local input lease binding");
  return lease;
}
QByteArray processes(LocalInputProcess core, LocalInputProcess host)
{
  Writer out;
  for (const auto &process : {core, host}) {
    out.number(process.pid, 4);
    out.number(process.startStamp, 8);
  }
  return out.data;
}
void closeDescriptor(int fd)
{
#ifdef Q_OS_UNIX
  if (fd >= 0)
    ::close(fd);
#else
  Q_UNUSED(fd);
#endif
}
struct Descriptor
{
  int value = -1;
  ~Descriptor()
  {
    closeDescriptor(value);
  }
};
bool privateEndpoint(int fd)
{
#ifdef Q_OS_UNIX
  if (fd < 3)
    return false;
  int type = 0;
  socklen_t size = sizeof(type);
  sockaddr_un local{}, peer{};
  socklen_t localSize = sizeof(local), peerSize = sizeof(peer);
  const auto unnamed = [](const sockaddr_un &address, socklen_t length) {
    if (address.sun_family != AF_UNIX)
      return false;
#ifdef Q_OS_MACOS
    // XNU returns sun_noname as a zero-padded sockaddr, not a two-byte
    // sockaddr_un prefix. Do not accept named/abstract paths on other systems.
    return length == sizeof(sockaddr) &&
           std::all_of(
               address.sun_path, address.sun_path + sizeof(sockaddr) - offsetof(sockaddr_un, sun_path),
               [](char byte) { return byte == 0; }
           );
#else
    return length == offsetof(sockaddr_un, sun_path);
#endif
  };
  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &size) != 0 || type != SOCK_STREAM ||
      getsockname(fd, reinterpret_cast<sockaddr *>(&local), &localSize) != 0 ||
      getpeername(fd, reinterpret_cast<sockaddr *>(&peer), &peerSize) != 0 || !unnamed(local, localSize) ||
      !unnamed(peer, peerSize))
    return false;
  const int flags = fcntl(fd, F_GETFD);
  return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
#else
  Q_UNUSED(fd);
  return false;
#endif
}
} // namespace

std::optional<LocalInputProcess> localInputProcess(std::int64_t pid)
{
  if (pid <= 0 || pid > std::numeric_limits<std::int32_t>::max())
    return std::nullopt;
#ifdef Q_OS_MACOS
  proc_bsdinfo info{};
  if (proc_pidinfo(static_cast<int>(pid), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info) ||
      info.pbi_uid != geteuid() || info.pbi_start_tvsec == 0)
    return std::nullopt;
  return LocalInputProcess{pid, info.pbi_start_tvsec * 1000000 + info.pbi_start_tvusec};
#elif defined(Q_OS_LINUX)
  const auto path = QByteArray("/proc/") + QByteArray::number(pid);
  struct stat info{};
  if (::stat(path.constData(), &info) != 0 || info.st_uid != geteuid())
    return std::nullopt;
  QFile file(QString::fromLatin1(path) + "/stat");
  if (!file.open(QIODevice::ReadOnly))
    return std::nullopt;
  const auto data = file.read(4096);
  const auto closing = data.lastIndexOf(')');
  if (closing < 0)
    return std::nullopt;
  const auto fields = data.mid(closing + 2).simplified().split(' ');
  if (fields.size() <= 19)
    return std::nullopt;
  bool valid = false;
  const auto start = fields[19].toULongLong(&valid);
  return valid && start > 0 ? std::optional<LocalInputProcess>({pid, start}) : std::nullopt;
#else
  return std::nullopt;
#endif
}
std::optional<LocalInputProcess> currentLocalInputProcess()
{
#ifdef Q_OS_UNIX
  return localInputProcess(getpid());
#else
  return std::nullopt;
#endif
}
bool validLocalInputLease(const LocalInputLease &lease)
{
  return nonzero(lease.deskflowClientIncarnation) && nonzero(lease.windowShareClientIncarnation) &&
         nonzero(lease.deskflowPeerPin) && nonzero(lease.windowSharePeerPin) && lease.session != 0 &&
         lease.epoch != 0 && lease.focusRevision != 0 && lease.geometryRevision != 0 && lease.routeGeneration != 0;
}

enum class LocalInputChannel::Type : std::uint8_t
{
  CoreHello = 1,
  HostHello,
  Ready,
  Ping,
  Pong,
  Prepare,
  Prepared,
  Commit,
  Committed,
  Revoke,
  Original
};

LocalInputChannel::LocalInputChannel(
    int descriptor, LocalInputRole role, LocalInputProcess peer, LocalInputLimits limits
)
    : m_role(role),
      m_peer(peer),
      m_limits(limits)
{
  Descriptor owned{descriptor};
  const auto self = currentLocalInputProcess();
  require(
      self.has_value() && peer.pid != self->pid && peer.startStamp != 0 && localInputProcess(peer.pid) == peer,
      "Expected local input process is unavailable"
  );
  m_self = *self;
  require(role == LocalInputRole::Core || role == LocalInputRole::Host, "Invalid local input role");
  require(
      limits.heartbeatMs >= 10 && limits.heartbeatMs <= 1000 && limits.timeoutMs >= 3 * limits.heartbeatMs &&
          limits.timeoutMs <= 10000,
      "Invalid local input deadlines"
  );
  require(privateEndpoint(descriptor), "Local input requires an unnamed inherited Unix socketpair");
  require(m_socket.setSocketDescriptor(descriptor, QLocalSocket::ConnectedState), "Cannot adopt local input socket");
  owned.value = -1;
  m_socket.setReadBufferSize(4 * (headerBytes + maxPayloadBytes));
  // A notification may destroy this channel and its by-value socket/timer.
  // Dispatch after those senders have unwound their Qt signal/event stacks.
  connect(&m_socket, &QLocalSocket::readyRead, this, [this] { read(); }, Qt::QueuedConnection);
  connect(
      &m_socket, &QLocalSocket::disconnected, this, [this] { close("Local input peer disconnected"); },
      Qt::QueuedConnection
  );
  connect(
      &m_socket, &QLocalSocket::errorOccurred, this,
      [this](QLocalSocket::LocalSocketError) { close("Local input socket failed"); }, Qt::QueuedConnection
  );
  m_lastRead.start();
  m_timer.setInterval(limits.heartbeatMs);
  connect(&m_timer, &QTimer::timeout, this, [this] { tick(); }, Qt::QueuedConnection);
  m_timer.start();
  if (role == LocalInputRole::Core) {
    m_coreIncarnation = incarnation();
    do {
      m_hostIncarnation = incarnation();
    } while (m_coreIncarnation == m_hostIncarnation);
    QTimer::singleShot(0, this, [this] {
      if (send(Type::CoreHello, processes(m_self, m_peer)))
        m_handshake = 1;
    });
  }
}
LocalInputChannel::~LocalInputChannel()
{
  close();
}
std::unique_ptr<LocalInputChannel>
LocalInputChannel::adoptHost(int descriptor, LocalInputProcess expectedCore, LocalInputLimits limits)
{
  return std::unique_ptr<LocalInputChannel>(
      new LocalInputChannel(descriptor, LocalInputRole::Host, expectedCore, limits)
  );
}
bool LocalInputChannel::authenticated() const
{
  return m_authenticated && !m_closed && m_socket.state() == QLocalSocket::ConnectedState &&
         m_lastRead.elapsed() < m_limits.timeoutMs && peerAlive();
}
LocalInputPhase LocalInputChannel::phase() const
{
  return m_phase;
}
std::optional<LocalInputLease> LocalInputChannel::lease() const
{
  return m_lease;
}
bool LocalInputChannel::peerAlive() const
{
  return localInputProcess(m_peer.pid) == m_peer;
}
bool LocalInputChannel::fresh()
{
  if (m_closed)
    return false;
  if (m_socket.state() != QLocalSocket::ConnectedState || !peerAlive() || m_lastRead.elapsed() >= m_limits.timeoutMs) {
    close(QStringLiteral("Local input peer lost or heartbeat expired"));
    return false;
  }
  return true;
}
bool LocalInputChannel::permits(const LocalInputLease &lease, LocalInputCheck check)
{
  const QPointer<LocalInputChannel> alive(this);
  const auto requested = lease;
  if (!fresh() || !authenticated() || m_authorizing || !validLocalInputLease(requested) || !authorize)
    return false;
  const auto phase = m_phase;
  const auto current = m_lease;
  const auto generation = m_routeGeneration;
  const auto core = m_coreIncarnation, host = m_hostIncarnation;
  m_authorizing = true;
  m_authorityInvalidated = false;
  bool permitted = false;
  try {
    const auto callback = authorize;
    permitted = callback(requested, check);
  } catch (...) {
    if (alive)
      alive->close(QStringLiteral("Local input authorization callback failed"));
  }
  if (!alive)
    return false;
  m_authorizing = false;
  // Check after the call as well: it can block past the deadline, run another
  // event loop, or invalidate/revoke the exact authority that it inspected.
  if (!fresh())
    return false;
  return permitted && !m_authorityInvalidated && authenticated() && m_phase == phase && m_lease == current &&
         m_routeGeneration == generation && m_coreIncarnation == core && m_hostIncarnation == host;
}
bool LocalInputChannel::send(Type type, const QByteArray &payload)
{
  const QPointer<LocalInputChannel> alive(this);
  if (!fresh())
    return false;
  if (payload.size() > maxPayloadBytes || m_sent == std::numeric_limits<std::uint64_t>::max() ||
      m_socket.bytesToWrite() > maxQueueBytes - headerBytes - payload.size()) {
    close("Local input output limit exceeded");
    return false;
  }
  Writer out;
  out.number(magic, 4);
  out.number(version, 2);
  out.number(static_cast<std::uint8_t>(type), 1);
  out.number(static_cast<std::uint8_t>(m_role), 1);
  out.number(payload.size(), 4);
  out.number(++m_sent, 8);
  out.fixed(m_coreIncarnation);
  out.fixed(m_hostIncarnation);
  out.data += payload;
  const auto written = m_socket.write(out.data);
  if (!alive)
    return false;
  if (written != out.data.size()) {
    close("Cannot queue local input frame");
    return false;
  }
  return !m_closed;
}
void LocalInputChannel::read()
{
  const QPointer<LocalInputChannel> alive(this);
  try {
    for (int count = 0; count < 64; ++count) {
      // A timer may be overdue behind readyRead after an event-loop stall.
      // Queued traffic must never revive authority that has already expired.
      if (!fresh())
        return;
      const auto missing = m_expected - m_incoming.size();
      if (missing > 0) {
        const auto bytes = m_socket.read(missing);
        if (!alive || !fresh())
          return;
        m_incoming += bytes;
      }
      if (m_incoming.size() < m_expected)
        break;
      Reader header{m_incoming};
      require(header.number(4) == magic && header.number(2) == version, "Invalid local input protocol");
      const auto typeNumber = header.number(1);
      require(typeNumber >= 1 && typeNumber <= static_cast<unsigned>(Type::Original), "Unknown local input message");
      const auto type = static_cast<Type>(typeNumber);
      require(
          header.number(1) ==
              static_cast<unsigned>(m_role == LocalInputRole::Core ? LocalInputRole::Host : LocalInputRole::Core),
          "Invalid local input sender role"
      );
      const auto size = header.number(4);
      require(size <= maxPayloadBytes, "Oversized local input frame");
      m_expected = headerBytes + size;
      if (m_incoming.size() < m_expected)
        continue;
      const auto sequence = header.number(8);
      require(
          m_received != std::numeric_limits<std::uint64_t>::max() && sequence == m_received + 1,
          "Replayed or skipped local input sequence"
      );
      const auto core = header.fixed<16>(), host = header.fixed<16>();
      require(nonzero(core) && nonzero(host) && core != host, "Invalid local input incarnation");
      if (m_role == LocalInputRole::Host && m_handshake == 0) {
        require(type == Type::CoreHello, "Local input bootstrap required");
        m_coreIncarnation = core;
        m_hostIncarnation = host;
      }
      require(core == m_coreIncarnation && host == m_hostIncarnation, "Stale local input process incarnation");
      require(peerAlive(), "Local input peer process identity lost");
      const auto payload = m_incoming.mid(headerBytes);
      m_incoming.clear();
      m_expected = headerBytes;
      m_received = sequence;
      if (!m_authenticated)
        bootstrap(type, payload);
      else
        receive(type, payload);
      if (!alive || !fresh())
        return;
      m_lastRead.restart();
    }
    if (!m_closed && m_socket.bytesAvailable() > 0 && !m_readScheduled) {
      m_readScheduled = true;
      QTimer::singleShot(0, this, [this] {
        m_readScheduled = false;
        read();
      });
    }
  } catch (const std::exception &error) {
    if (alive)
      alive->close(QString::fromUtf8(error.what()));
  } catch (...) {
    if (alive)
      alive->close(QStringLiteral("Local input frame processing failed"));
  }
}
void LocalInputChannel::bootstrap(Type type, const QByteArray &payload)
{
  require(peerAlive(), "Local input bootstrap peer changed");
  if (m_role == LocalInputRole::Host && m_handshake == 0 && type == Type::CoreHello) {
    require(payload == processes(m_peer, m_self), "Local input launch identity mismatch");
    if (send(Type::HostHello, processes(m_peer, m_self)))
      m_handshake = 1;
    return;
  }
  if (m_role == LocalInputRole::Core && m_handshake == 1 && type == Type::HostHello) {
    require(payload == processes(m_self, m_peer), "Local input child identity mismatch");
    if (!send(Type::Ready))
      return;
  } else {
    require(
        m_role == LocalInputRole::Host && m_handshake == 1 && type == Type::Ready && payload.isEmpty(),
        "Unexpected local input handshake message"
    );
  }
  m_handshake = 2;
  m_authenticated = true;
  notify(this, onAuthenticated);
}
bool LocalInputChannel::prepare(LocalInputLease lease)
{
  if (!fresh() || m_role != LocalInputRole::Core || m_phase != LocalInputPhase::Idle ||
      lease.routeGeneration <= m_routeGeneration || !permits(lease, LocalInputCheck::Prepare))
    return false;
  m_routeGeneration = lease.routeGeneration;
  m_lease = lease;
  m_phase = LocalInputPhase::Preparing;
  return send(Type::Prepare, encodeLease(lease));
}
bool LocalInputChannel::acknowledgePrepare(LocalInputLease lease)
{
  if (!fresh() || m_role != LocalInputRole::Host || m_phase != LocalInputPhase::Preparing || m_lease != lease ||
      !permits(lease, LocalInputCheck::Prepare))
    return false;
  m_phase = LocalInputPhase::Prepared;
  return send(Type::Prepared, encodeLease(lease));
}
bool LocalInputChannel::commit(LocalInputLease lease)
{
  if (!fresh() || m_role != LocalInputRole::Core || m_phase != LocalInputPhase::Prepared || m_lease != lease ||
      !permits(lease, LocalInputCheck::Commit))
    return false;
  m_phase = LocalInputPhase::Committing;
  return send(Type::Commit, encodeLease(lease));
}
bool LocalInputChannel::sendOriginal(LocalInputLease lease, OriginalKeyEvent event)
{
  const QPointer<LocalInputChannel> alive(this);
  if (!fresh() || m_role != LocalInputRole::Core || m_phase != LocalInputPhase::Active || m_lease != lease)
    return false;
  if (!permits(lease, LocalInputCheck::Deliver)) {
    if (alive && !m_closed && m_lease == lease)
      revoke(LocalInputRevocation::AuthorityLost);
    return false;
  }
  if (!validOriginalKeyEvent(event) || event.sample.sequence <= m_originalSequence ||
      event.sample.timestampNs < m_originalTimestamp) {
    close("Invalid original input capture ordering");
    return false;
  }
  Writer out{encodeLease(lease)};
  out.number(event.sample.sequence, 8);
  out.number(event.sample.timestampNs, 8);
  out.number(event.sample.flags, 8);
  out.number(event.sample.keyboardType, 4);
  out.number(event.sample.keyCode, 2);
  out.number(static_cast<unsigned>(event.sample.kind), 1);
  out.number(event.sample.repeat, 1);
  out.number(static_cast<unsigned>(event.transition), 1);
  m_originalSequence = event.sample.sequence;
  m_originalTimestamp = event.sample.timestampNs;
  return send(Type::Original, out.data);
}
void LocalInputChannel::receive(Type type, const QByteArray &payload)
{
  if (type == Type::Ping || type == Type::Pong) {
    require(payload.isEmpty(), "Invalid local input heartbeat");
    if (type == Type::Ping)
      send(Type::Pong);
    return;
  }
  Reader in{payload};
  auto lease = decodeLease(in);
  if (type == Type::Revoke) {
    const auto reason = in.number(1);
    in.end();
    require(
        reason >= 1 && reason <= static_cast<unsigned>(LocalInputRevocation::TransportLost),
        "Invalid local input revocation"
    );
    if (m_lease == lease)
      revoked(static_cast<LocalInputRevocation>(reason));
    else if (lease.routeGeneration >= m_routeGeneration)
      require(m_lastRevoked == lease, "Stale local input revocation");
    // Earlier generations cannot acquire or release any current authority.
    // Their revokes may cross any number of locally cancelled newer prepares.
    return;
  }
  if (type == Type::Prepare) {
    in.end();
    require(
        m_role == LocalInputRole::Host && m_phase == LocalInputPhase::Idle &&
            lease.routeGeneration > m_routeGeneration && permits(lease, LocalInputCheck::Prepare),
        "Unauthorized local input preparation"
    );
    m_routeGeneration = lease.routeGeneration;
    m_lease = lease;
    m_phase = LocalInputPhase::Preparing;
    notify(this, onPrepare, lease);
    return;
  }
  if (type == Type::Prepared || type == Type::Committed || type == Type::Commit) {
    require(
        m_role == (type == Type::Commit ? LocalInputRole::Host : LocalInputRole::Core),
        "Invalid local input control sender role"
    );
  }
  if ((type == Type::Prepared || type == Type::Committed || type == Type::Commit) &&
      (lease.routeGeneration < m_routeGeneration || (!m_lease && m_lastRevoked == lease))) {
    // Revocation cannot retract the peer's already queued control messages.
    // They cannot authorize any newer/current route.
    in.end();
    return;
  }
  if (type == Type::Original) {
    require(m_role == LocalInputRole::Host, "Invalid original input sender role");
    OriginalKeyEvent event;
    event.sample.sequence = in.number(8);
    event.sample.timestampNs = in.number(8);
    event.sample.flags = in.number(8);
    event.sample.keyboardType = static_cast<std::uint32_t>(in.number(4));
    event.sample.keyCode = static_cast<std::uint16_t>(in.number(2));
    event.sample.kind = static_cast<OriginalKeyKind>(in.number(1));
    const auto repeat = in.number(1);
    require(repeat <= 1, "Invalid original key repeat flag");
    event.sample.repeat = repeat != 0;
    event.transition = static_cast<OriginalKeyTransition>(in.number(1));
    in.end();
    require(
        validOriginalKeyEvent(event) && event.sample.sequence > m_originalSequence &&
            event.sample.timestampNs >= m_originalTimestamp,
        "Invalid original input capture ordering"
    );
    const bool retired = lease.routeGeneration < m_routeGeneration ||
                         (lease.routeGeneration == m_routeGeneration && !m_lease && m_lastRevoked == lease);
    if (!retired)
      require(
          m_lease == lease && m_phase == LocalInputPhase::Active && permits(lease, LocalInputCheck::Deliver),
          "Unauthorized original input"
      );
    // Authorization may run a nested event loop that validates a newer retired
    // capture. Never roll its replay high-water back or deliver the older one.
    require(
        event.sample.sequence > m_originalSequence && event.sample.timestampNs >= m_originalTimestamp,
        "Original input capture advanced during authorization"
    );
    m_originalSequence = event.sample.sequence;
    m_originalTimestamp = event.sample.timestampNs;
    if (retired)
      return; // Valid in-flight input retires its capture sequence, never delivers to a new owner.
    notify(this, onOriginal, lease, event);
    return;
  }
  require(m_lease == lease, "Stale local input lease binding");
  in.end();
  if (type == Type::Prepared) {
    require(
        m_role == LocalInputRole::Core && m_phase == LocalInputPhase::Preparing &&
            permits(lease, LocalInputCheck::Prepare),
        "Unexpected local input prepared acknowledgement"
    );
    m_phase = LocalInputPhase::Prepared;
    notify(this, onPrepared, lease);
    return;
  }
  require(permits(lease, LocalInputCheck::Commit), "Local input commit authority lost");
  if (type == Type::Commit) {
    require(m_role == LocalInputRole::Host && m_phase == LocalInputPhase::Prepared, "Unexpected local input commit");
    m_phase = LocalInputPhase::Active;
    if (!send(Type::Committed, encodeLease(lease)))
      return;
  } else {
    require(
        type == Type::Committed && m_role == LocalInputRole::Core && m_phase == LocalInputPhase::Committing,
        "Unexpected local input commit acknowledgement"
    );
    m_phase = LocalInputPhase::Active;
  }
  notify(this, onActivated, lease);
}
void LocalInputChannel::revoked(LocalInputRevocation reason)
{
  const auto previous = m_lease;
  m_phase = LocalInputPhase::Idle;
  m_lease.reset();
  if (m_authorizing)
    m_authorityInvalidated = true;
  if (previous) {
    m_lastRevoked = previous;
    notify(this, onRevoked, *previous, reason);
  }
}
void LocalInputChannel::revoke(LocalInputRevocation reason)
{
  if (m_authorizing)
    m_authorityInvalidated = true;
  if (!fresh() || !m_lease)
    return;
  const auto payload = encodeLease(*m_lease) + QByteArray(1, static_cast<char>(reason));
  // Queue before notifying: a release callback may immediately prepare a newer
  // route, which the peer must observe after this old route's revocation.
  // If send fails, close releases this still-current owner exactly once.
  if (!send(Type::Revoke, payload))
    return;
  revoked(reason);
}
void LocalInputChannel::tick()
{
  const QPointer<LocalInputChannel> alive(this);
  if (!fresh())
    return;
  if (m_lease && m_phase == LocalInputPhase::Active) {
    const auto current = *m_lease;
    if (!permits(current, LocalInputCheck::Deliver)) {
      if (alive && !m_closed && m_lease == current)
        revoke(LocalInputRevocation::AuthorityLost);
      return;
    }
  }
  if (authenticated() && m_socket.bytesToWrite() == 0)
    send(Type::Ping);
}
void LocalInputChannel::close(const QString &reason)
{
  if (m_closed)
    return;
  const QPointer<LocalInputChannel> alive(this);
  const auto why = reason;
  m_closed = true;
  m_authenticated = false;
  m_timer.stop();
  m_socket.abort();
  revoked(LocalInputRevocation::TransportLost);
  if (alive)
    notify(this, onClosed, why);
}

LocalInputSocketPair::LocalInputSocketPair(int core, int child) : m_core(core), m_child(child)
{
}
LocalInputSocketPair LocalInputSocketPair::create()
{
#ifdef Q_OS_UNIX
  int descriptors[2];
#ifdef Q_OS_LINUX
  constexpr int flags = SOCK_STREAM | SOCK_CLOEXEC;
#else
  constexpr int flags = SOCK_STREAM;
#endif
  require(socketpair(AF_UNIX, flags, 0, descriptors) == 0, "Cannot create private local input socketpair");
  LocalInputSocketPair pair(descriptors[0], descriptors[1]);
  for (int *fd : {&pair.m_core, &pair.m_child}) {
    if (*fd < 3) {
      const auto replacement = fcntl(*fd, F_DUPFD_CLOEXEC, 3);
      require(replacement >= 0, "Cannot reserve private local input descriptor");
      closeDescriptor(*fd);
      *fd = replacement;
    }
    require(privateEndpoint(*fd), "Cannot protect local input socketpair");
  }
  return pair;
#else
  throw std::runtime_error("Private local input requires Unix socketpairs");
#endif
}
LocalInputSocketPair::LocalInputSocketPair(LocalInputSocketPair &&other) noexcept
    : m_core(std::exchange(other.m_core, -1)),
      m_child(std::exchange(other.m_child, -1))
{
}
LocalInputSocketPair &LocalInputSocketPair::operator=(LocalInputSocketPair &&other) noexcept
{
  if (this != &other) {
    reset();
    m_core = std::exchange(other.m_core, -1);
    m_child = std::exchange(other.m_child, -1);
  }
  return *this;
}
void LocalInputSocketPair::reset()
{
  closeDescriptor(std::exchange(m_core, -1));
  closeDescriptor(std::exchange(m_child, -1));
}
LocalInputSocketPair::~LocalInputSocketPair()
{
  reset();
}
int LocalInputSocketPair::childDescriptor() const
{
  require(m_child >= 0, "Local input child descriptor already transferred");
  return m_child;
}
void LocalInputSocketPair::configureChild(QProcess &process) const
{
  require(
      m_core >= 0 && m_child >= 0 && process.state() == QProcess::NotRunning, "Invalid local input child launch state"
  );
#ifdef Q_OS_UNIX
  process.setChildProcessModifier([core = m_core, child = m_child] {
    ::close(core);
    const auto flags = fcntl(child, F_GETFD);
    if (flags < 0 || fcntl(child, F_SETFD, flags & ~FD_CLOEXEC) != 0)
      _exit(127);
  });
#else
  throw std::runtime_error("Private local input requires Unix process inheritance");
#endif
}
std::unique_ptr<LocalInputChannel> LocalInputSocketPair::adoptCore(LocalInputProcess child, LocalInputLimits limits)
{
  require(m_core >= 0 && m_child >= 0, "Local input socketpair already transferred");
  closeDescriptor(std::exchange(m_child, -1));
  return std::unique_ptr<LocalInputChannel>(
      new LocalInputChannel(std::exchange(m_core, -1), LocalInputRole::Core, child, limits)
  );
}

} // namespace deskflow::windowshare
