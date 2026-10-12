/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "OriginalKeyboard.h"
#include <QByteArray>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace deskflow::windowshare {

using LocalInputIncarnation = std::array<std::uint8_t, 16>;
using LocalInputPin = std::array<std::uint8_t, 32>;

struct LocalInputProcess
{
  std::int64_t pid = 0;
  std::uint64_t startStamp = 0;
  bool operator==(const LocalInputProcess &) const = default;
};

// Same-user process birth identity, read from the kernel. A socketpair's peer
// credentials can describe its creator after fork, so they are not used as
// evidence that the expected child owns the other endpoint.
std::optional<LocalInputProcess> localInputProcess(std::int64_t pid);
std::optional<LocalInputProcess> currentLocalInputProcess();

// Early child bootstrap, before QApplication can load plugins or start workers.
// Restores CLOEXEC and accepts only a connected unnamed Unix stream endpoint.
// This does not transfer ownership or authenticate the launching process.
bool protectLocalInputDescriptor(int descriptor);

struct LocalInputLease
{
  LocalInputIncarnation deskflowClientIncarnation{}, windowShareClientIncarnation{};
  LocalInputPin deskflowPeerPin{}, windowSharePeerPin{};
  std::uint64_t session = 0, epoch = 0, focusRevision = 0, geometryRevision = 0, routeGeneration = 0;
  bool operator==(const LocalInputLease &) const = default;
};
bool validLocalInputLease(const LocalInputLease &lease);

enum class LocalInputRole : std::uint8_t
{
  Core = 1,
  Host = 2
};
enum class LocalInputCheck
{
  Prepare,
  Commit,
  Deliver
};
enum class LocalInputRevocation : std::uint8_t
{
  Requested = 1,
  AuthorityLost,
  TransportLost
};
enum class LocalInputPhase
{
  Idle,
  Preparing,
  Prepared,
  Committing,
  Active
};
struct LocalInputLimits
{
  int heartbeatMs = 500;
  int timeoutMs = 2500;
};

class LocalInputSocketPair;

// Private launch capability, never a public/named IPC service. Both callers
// must independently establish the trusted launch relationship and expected
// PID/start stamp; receiving those values in a packet is not authorization.
// This cannot defend against a privileged process or trusted code that copies
// the descriptor. Possession does not prove executable code-signing identity.
// No OS input is generated here, and no network/JSON native-key input exists.
class LocalInputChannel final : public QObject
{
  friend class LocalInputSocketPair;
  friend struct LocalInputChannelTestAccess;

public:
  static constexpr std::uint16_t version = 1;
  static constexpr qsizetype headerBytes = 52, maxPayloadBytes = 256, maxQueueBytes = 16 * 1024;

  // Takes ownership even on failure. expectedCore must come from an independently
  // trusted bootstrap, not argv, an environment variable, or peer packet fields.
  static std::unique_ptr<LocalInputChannel>
  adoptHost(int descriptor, LocalInputProcess expectedCore, LocalInputLimits limits = {});
  ~LocalInputChannel() override;

  bool authenticated() const;
  LocalInputPhase phase() const;
  std::optional<LocalInputLease> lease() const;
  bool prepare(LocalInputLease lease);                              // Core -> host.
  bool acknowledgePrepare(LocalInputLease lease);                   // Host -> core.
  bool commit(LocalInputLease lease);                               // Core -> host; acknowledgement activates core.
  bool sendOriginal(LocalInputLease lease, OriginalKeyEvent event); // Core -> host only.
  // Retired control acknowledgements/revokes cannot affect a newer route.
  // Fully validated in-flight Original frames for retired routes advance
  // capture replay protection and are discarded without native delivery.
  void revoke(LocalInputRevocation reason = LocalInputRevocation::Requested);
  void close(const QString &reason = QStringLiteral("Local input channel closed"));

  // Required and default-deny. Core observes its Deskflow PeerAuth identity;
  // host observes its WindowShare TLS identity. Validate that local identity
  // and the private-channel peer attestation against an explicit approved
  // dual-pin mapping; neither endpoint independently observes both sockets.
  // Also query authoritative session/focus/geometry and route state.
  // Prepare/Commit additionally require a neutral physical press boundary,
  // completed owner releases, and a quiesced viewer. Never derive this decision
  // from peer-supplied booleans. Deliver permits held presses for this owner.
  std::function<bool(const LocalInputLease &, LocalInputCheck)> authorize;
  // Use the channel on one Qt event-loop thread. Callbacks may synchronously
  // revoke, close, or destroy their owned channel; notifications keep value
  // snapshots, and an authorization cannot grant through a state change.
  // Throwing callbacks fail closed. Recursive authorization is denied.
  std::function<void()> onAuthenticated;
  std::function<void(const LocalInputLease &)> onPrepare, onPrepared, onActivated;
  std::function<void(const LocalInputLease &, const OriginalKeyEvent &)> onOriginal;
  // Release this exact owner's delivered state; do not fabricate capture events.
  std::function<void(const LocalInputLease &, LocalInputRevocation)> onRevoked;
  std::function<void(QString)> onClosed;

private:
  enum class Type : std::uint8_t;
  LocalInputChannel(int descriptor, LocalInputRole role, LocalInputProcess peer, LocalInputLimits limits);
  bool send(Type type, const QByteArray &payload = {});
  void read();
  void receive(Type type, const QByteArray &payload);
  void bootstrap(Type type, const QByteArray &payload);
  bool permits(const LocalInputLease &lease, LocalInputCheck check);
  bool fresh();
  bool peerAlive() const;
  void revoked(LocalInputRevocation reason);
  void tick();

  QLocalSocket m_socket;
  LocalInputRole m_role;
  LocalInputProcess m_self, m_peer;
  LocalInputLimits m_limits;
  LocalInputIncarnation m_coreIncarnation{}, m_hostIncarnation{};
  QByteArray m_incoming;
  qsizetype m_expected = headerBytes;
  std::uint64_t m_sent = 0, m_received = 0, m_originalSequence = 0, m_originalTimestamp = 0, m_routeGeneration = 0;
  int m_handshake = 0;
  bool m_authenticated = false, m_closed = false, m_readScheduled = false;
  bool m_authorizing = false, m_authorityInvalidated = false;
  LocalInputPhase m_phase = LocalInputPhase::Idle;
  std::optional<LocalInputLease> m_lease, m_lastRevoked;
  QElapsedTimer m_lastRead;
  QTimer m_timer;
};

// Create during serialized early bootstrap, before worker threads/concurrent
// forks or spawns. macOS lacks SOCK_CLOEXEC, so socketpair + fcntl cannot be
// atomic; a private mutex would not serialize unrelated process launchers.
// Both endpoints remain CLOEXEC in the parent. Only the designated child loses
// that flag in its post-fork modifier; adoptHost immediately restores it.
class LocalInputSocketPair final
{
public:
  static LocalInputSocketPair create();
  LocalInputSocketPair(LocalInputSocketPair &&other) noexcept;
  LocalInputSocketPair &operator=(LocalInputSocketPair &&other) noexcept;
  ~LocalInputSocketPair();
  LocalInputSocketPair(const LocalInputSocketPair &) = delete;
  LocalInputSocketPair &operator=(const LocalInputSocketPair &) = delete;
  int childDescriptor() const;
  // Installs this pair's modifier; do not replace it or launch concurrently.
  // Only the FD number, never an identity/capability value, belongs in argv.
  void configureChild(QProcess &process) const;
  // Call immediately after successful launch. Closes the parent's child copy.
  std::unique_ptr<LocalInputChannel> adoptCore(LocalInputProcess child, LocalInputLimits limits = {});

private:
  LocalInputSocketPair(int core, int child);
  void reset();
  int m_core = -1, m_child = -1;
};

} // namespace deskflow::windowshare
