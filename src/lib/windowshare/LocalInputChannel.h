/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "KeyboardRoute.h"
#include "LocalInputMapping.h"
#include "LocalWindowDrag.h"
#include "LocalWindowMove.h"
#include "LocalWindowPointer.h"
#include "OriginalInput.h"
#include <QByteArray>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <array>
#include <chrono>
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

// Private, core-attested selection. All five counters are full nonzero u64s;
// none is a JSON/window counter. Names and addresses never establish identity.
// Wire order: selection counters then captureEpoch (40), Deskflow incarnation
// (16), both pins (64), explicit scope tag (8). The scope is retained by every
// reply and release. captureEpoch is separate from the host/session epoch.
struct LocalInputOffer
{
  std::uint64_t routeGeneration = 0, coreServerIncarnation = 0, coreRecipient = 0, coreSelectionGeneration = 0;
  std::uint64_t captureEpoch = 0;
  LocalInputIncarnation deskflowClientIncarnation{};
  LocalInputPin deskflowPeerPin{}, windowSharePeerPin{};
  ForwardingScope scope = ForwardingScope::Keyboard;
  bool operator==(const LocalInputOffer &) const = default;
};
bool validLocalInputOffer(const LocalInputOffer &offer);

// Adds the independently host-attested connection and exact forwarding fence.
// Wire order: Offer (128), WindowShare incarnation (16), six counters (48).
// These six host counters must be in [1, 2^53-1], matching the JSON protocol.
// keyboardRouteGeneration and the private routeGeneration are distinct scopes.
struct LocalInputLease : LocalInputOffer
{
  LocalInputIncarnation windowShareClientIncarnation{};
  std::uint64_t session = 0, epoch = 0, focusRequest = 0, focusRevision = 0, geometryRevision = 0,
                keyboardRouteGeneration = 0;
  bool operator==(const LocalInputLease &) const = default;
};
LocalInputOffer leaseOffer(const LocalInputLease &lease);
bool validLocalInputLease(const LocalInputLease &lease);

enum class LocalInputRole : std::uint8_t
{
  Core = 1,
  Host = 2
};
// Value identity of an already-authenticated private connection. This carries
// no executable/code authority and is not usable after the channel expires.
struct LocalInputAuthenticatedIdentity
{
  LocalInputRole role;
  LocalInputProcess self, peer;
  LocalInputIncarnation coreIncarnation{}, hostIncarnation{};
  std::uint64_t capabilities = 0;
  bool operator==(const LocalInputAuthenticatedIdentity &) const = default;
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
// Wire capabilities describe code support, never provider readiness/authority.
// Every v10 endpoint supports keyboard leases; full-input and mapped delivery
// are separately negotiated. Mapped delivery additionally requires full input.
inline constexpr std::uint64_t localInputKeyboardCapability = 1, localInputFullCapability = 2,
                               localInputMappedCapability = 4, localInputWindowPointerCapability = 8,
                               localInputWindowDragCapability = 16, localInputWindowMoveCapability = 32;
inline constexpr std::uint64_t localInputKnownCapabilities =
    localInputKeyboardCapability | localInputFullCapability | localInputMappedCapability |
    localInputWindowPointerCapability | localInputWindowDragCapability | localInputWindowMoveCapability;

struct LocalInputLimits
{
  int heartbeatMs = 500;
  int timeoutMs = 2500;
  // Absolute adoption-to-authentication budget, never extended by traffic.
  // Independent of the authenticated heartbeat timeout; valid range 100–30000.
  int handshakeMs = 15000;
  std::uint64_t capabilities = localInputKnownCapabilities;
};

// Scheduling measurements, never input authority or proof of AppKit receipt.
// Work covers the peer's actual mapped-frame handling, including authorization,
// but excludes time queued between processes and between dispatch turns.
struct LocalInputDispatchCost
{
  std::uint64_t records = 0;
  std::chrono::nanoseconds work{};
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
  static constexpr std::uint16_t version = 10;
  static constexpr qsizetype headerBytes = 52, maxPayloadBytes = 320, maxQueueBytes = 16 * 1024;
  static constexpr qsizetype offerPayloadBytes = 128, leasePayloadBytes = 192, maxOriginalPayloadBytes = 244;
  static constexpr qsizetype mappedPointBytes = 40, maxMappedOriginalPayloadBytes = 284;

  // Takes ownership even on failure. expectedCore must come from an independently
  // trusted bootstrap, not argv, an environment variable, or peer packet fields.
  static std::unique_ptr<LocalInputChannel>
  adoptHost(int descriptor, LocalInputProcess expectedCore, LocalInputLimits limits = {});
  ~LocalInputChannel() override;

  bool authenticated() const;
  // Diagnostic only; never refreshes liveness or grants input authority.
  QString authorityFailureReason() const;
  // Qt owner thread only. Negotiated code support is not a delivery permit.
  bool supports(ForwardingScope scope) const;
  bool supportsMappedInput() const;
  bool supportsWindowPointer() const;
  // Metadata only; separate capability and replay state, valid in Idle and
  // every input lease phase. No original-input authorizer is invoked.
  bool sendWindowPointer(const LocalWindowPointer &pointer);
  bool supportsWindowDrag() const;
  bool sendWindowDrag(const LocalWindowDrag &drag);
  bool sendWindowDragStatus(const LocalWindowDragStatus &status);
  bool supportsWindowMove() const;
  bool sendWindowMove(const LocalWindowMove &);
  // Owner Qt thread only; wrong-thread, stale and unauthenticated queries deny.
  // Does not invoke callbacks or refresh the authenticated-read deadline.
  std::optional<LocalInputAuthenticatedIdentity> authenticatedIdentity() const;
  LocalInputPhase phase() const;
  std::optional<LocalInputLease> lease() const;
  // Preflight transports attestations only. It never changes lease phase,
  // route/capture high-water marks, or authorization. The coordinator owns
  // pending attempts, deadlines, approval, and stale-generation classification.
  bool offer(LocalInputOffer offer);     // Core -> host.
  bool candidate(LocalInputLease lease); // Host -> core.
  bool quiesced(LocalInputLease lease);  // Host -> core.
  // Ordered before Quiesced by the host coordinator. This is an attestation
  // under the same exact pending lease, not an independently usable permit.
  bool sendInputTarget(LocalInputLease lease, LocalInputTarget target);
  // Host confirms exact native cleanup and the Viewer's ordered resume ack.
  // This retires authority only; it cannot prepare or activate a lease.
  bool sendReleased(LocalInputOffer offer);
  // Bidirectional preflight cancellation, distinct from revoke() of a lease.
  bool cancelOffer(LocalInputOffer offer, LocalInputRevocation reason = LocalInputRevocation::Requested);
  bool prepare(LocalInputLease lease);            // Core -> host.
  bool acknowledgePrepare(LocalInputLease lease); // Host -> core.
  bool commit(LocalInputLease lease);             // Core -> host; acknowledgement activates core.
  // One ordered key/motion/button/scroll/checkpoint stream; core -> host only.
  // Epoch is derived from the bound lease on decode and must match on send.
  // Motion/scroll use signed32, losslessly validated before encoding. Emergency
  // and discontinuity revoke instead of becoming deliverable records.
  bool sendOriginal(LocalInputLease lease, OriginalInputRecord record);
  bool sendMappedOriginal(LocalInputLease lease, OriginalInputRecord record, LocalInputMappedPoint point);
  // Scheduling only: an ordered Ping/Pong proves the peer finished dispatching
  // the preceding mapped frames. It grants no delivery authority and never
  // renews a point receipt. Poll on the owner thread before mapping a new batch.
  bool mappedOutputDrained();
  // Consume cost only after every preceding mapped write has an exact ordered
  // drain reply. Repeated polls cannot count the same work twice.
  std::optional<LocalInputDispatchCost> takeMappedDispatchCost();
  // Retired control acknowledgements/revokes cannot affect a newer route.
  // Fully validated in-flight Original frames for retired routes advance
  // capture replay protection and are discarded without native delivery.
  // Reuse in one capture epoch retains sequence/time high-water marks. Only a
  // new lease can admit a higher epoch. Older-epoch records are terminal: FIFO
  // ordering requires their legitimate traffic to precede that new Prepare.
  void revoke(LocalInputRevocation reason = LocalInputRevocation::Requested);
  void close(const QString &reason = QStringLiteral("Local input channel closed"));

  // Required and default-deny. Core observes its Deskflow PeerAuth identity;
  // host observes its WindowShare TLS identity. Validate that local identity
  // and the private-channel peer attestation against an explicit approved
  // dual-pin mapping; neither endpoint independently observes both sockets.
  // Also query authoritative session/focus/geometry and route state.
  // Prepare/Commit require a proven source acquisition boundary, completed
  // delivered-owner releases, and a quiesced viewer. This can be a complete
  // physical-neutral fence or conservative per-control transition ownership;
  // an empty tracker supplies neither. Never derive it from peer booleans.
  // Deliver permits held presses for this exact owner.
  std::function<bool(const LocalInputLease &, LocalInputCheck)> authorize;
  // Use the channel on one Qt event-loop thread. Callbacks may synchronously
  // revoke, close, or destroy their owned channel; notifications keep value
  // snapshots, and an authorization cannot grant through a state change.
  // Throwing callbacks fail closed. Recursive authorization is denied.
  std::function<void()> onAuthenticated;
  std::function<void(const LocalWindowPointer &)> onWindowPointer;
  std::function<void(const LocalWindowDrag &)> onWindowDrag;
  std::function<void(const LocalWindowDragStatus &)> onWindowDragStatus;
  std::function<void(const LocalWindowMove &)> onWindowMove;
  std::function<void(const LocalInputOffer &)> onOffer;
  std::function<void(const LocalInputLease &)> onCandidate, onQuiesced;
  std::function<void(const LocalInputLease &, const LocalInputTarget &)> onInputTarget;
  std::function<void(const LocalInputOffer &)> onReleased;
  // Scheduling notification after the exact mapped-prefix dispatch barrier
  // has been validated. It is not a new receipt or delivery permission.
  std::function<void()> onMappedOutputReady;
  std::function<void(const LocalInputOffer &, LocalInputRevocation)> onOfferCancelled;
  std::function<void(const LocalInputLease &)> onPrepare, onPrepared, onActivated;
  std::function<void(const LocalInputLease &, const OriginalInputRecord &)> onOriginal;
  std::function<void(const LocalInputLease &, const OriginalInputRecord &, const LocalInputMappedPoint &)>
      onMappedOriginal;
  // Release this exact owner's delivered state; do not fabricate capture events.
  std::function<void(const LocalInputLease &, LocalInputRevocation)> onRevoked;
  std::function<void(QString)> onClosed;

private:
  enum class Type : std::uint8_t;
  LocalInputChannel(int descriptor, LocalInputRole role, LocalInputProcess peer, LocalInputLimits limits);
  bool send(Type type, const QByteArray &payload = {});
  void read();
  void scheduleRead();
  void observeBufferedInput();
  void receive(Type type, const QByteArray &payload);
  void bootstrap(Type type, const QByteArray &payload);
  bool permits(const LocalInputLease &lease, LocalInputCheck check);
  bool
  sendOriginalRecord(LocalInputLease lease, OriginalInputRecord record, std::optional<LocalInputMappedPoint> point);
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
  std::uint64_t m_peerCapabilities = 0, m_capabilities = 0;
  std::uint64_t m_sent = 0, m_received = 0, m_originalSequence = 0, m_originalTimestamp = 0, m_routeGeneration = 0,
                m_captureEpoch = 0;
  std::uint64_t m_pingsSent = 0, m_pongsReceived = 0, m_lastMappedWrite = 0, m_drainedMappedWrite = 0;
  std::uint64_t m_drainPing = 0, m_drainMappedWrite = 0;
  struct DispatchTotals
  {
    std::uint64_t throughFrame = 0, records = 0, workNs = 0;
  };
  DispatchTotals m_nativeDispatch, m_peerDispatch, m_drainDispatch, m_takenDispatch;
  std::uint64_t m_mappedWrites = 0, m_drainMappedRecords = 0;
  std::uint64_t m_evidencePendingBytes = 0;  // Accepted bytes not yet reported by bytesWritten.
  std::uint64_t m_evidenceBufferedBytes = 0; // Actual Qt receive buffer, before queued read().
  int m_handshake = 0;
  bool m_authenticated = false, m_closed = false, m_readScheduled = false, m_reading = false;
  QString m_closeReason;
  using DispatchClock = std::chrono::steady_clock;
  // Private test clock affects work scheduling only, never authority deadlines.
  std::function<DispatchClock::time_point()> m_dispatchNow = [] { return DispatchClock::now(); };
  bool m_authorizing = false, m_authorityInvalidated = false;
  LocalInputPhase m_phase = LocalInputPhase::Idle;
  std::optional<LocalInputLease> m_lease, m_lastRevoked;
  LocalWindowPointerGate m_windowPointerGate;
  LocalWindowDragGate m_windowDragGate;
  LocalWindowMoveGate m_windowMoveGate;
  QElapsedTimer m_lastRead, m_handshakeAge;
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
