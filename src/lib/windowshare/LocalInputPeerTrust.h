/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "LocalInputChannel.h"

#include <chrono>
#include <utility>

namespace deskflow::windowshare {

// An immutable record of a completed inspection, not a live/revocable capability.
// It is tied to both fixed roles and the live bundle-main anchor. Retaining it
// after a process exits, is debugged, or changes does not retain any authority.
// In particular, this is NOT exact loaded-image identity, process-memory
// attestation, or a LocalInputPreflight::Checks::moduleIntegrity provider.
class LocalInputRestrictedLibraryAdmissionSnapshot final
{
public:
  LocalInputProcess selfProcess() const
  {
    return m_self;
  }
  LocalInputProcess peerProcess() const
  {
    return m_peer;
  }
  LocalInputProcess anchorProcess() const
  {
    return m_anchor;
  }
  LocalInputRole peerRole() const
  {
    return m_peerRole;
  }
  const QByteArray &selfCodeHash() const
  {
    return m_selfHash;
  }
  const QByteArray &peerCodeHash() const
  {
    return m_peerHash;
  }
  const QByteArray &anchorCodeHash() const
  {
    return m_anchorHash;
  }
  const QString &teamIdentifier() const
  {
    return m_team;
  }
  std::chrono::steady_clock::time_point inspectionStartedAt() const
  {
    return m_started;
  }
  std::chrono::steady_clock::time_point inspectionFinishedAt() const
  {
    return m_finished;
  }

private:
  friend class LocalInputPeerTrust;
  LocalInputRestrictedLibraryAdmissionSnapshot(
      LocalInputProcess self, LocalInputProcess peer, LocalInputProcess anchor, LocalInputRole peerRole,
      QByteArray selfHash, QByteArray peerHash, QByteArray anchorHash, QString team,
      std::chrono::steady_clock::time_point started, std::chrono::steady_clock::time_point finished
  )
      : m_self(self),
        m_peer(peer),
        m_anchor(anchor),
        m_peerRole(peerRole),
        m_selfHash(std::move(selfHash)),
        m_peerHash(std::move(peerHash)),
        m_anchorHash(std::move(anchorHash)),
        m_team(std::move(team)),
        m_started(started),
        m_finished(finished)
  {
  }
  LocalInputProcess m_self, m_peer, m_anchor;
  LocalInputRole m_peerRole;
  QByteArray m_selfHash, m_peerHash, m_anchorHash;
  QString m_team;
  std::chrono::steady_clock::time_point m_started, m_finished;
};

// Packaged-code integrity, not publisher or user authorization. Trust originates
// in the current running signed bundle main (self or a live ancestor), never in
// peer-provided paths, process IDs, environment, or a same-user signature alone.
// The host must be the core's direct child. Construction/verification are
// synchronous and never launch a process, pump events, or access native input.
class LocalInputPeerTrust final
{
public:
  static std::unique_ptr<LocalInputPeerTrust> forPackagedPeer(LocalInputRole expectedPeer, QString *error = nullptr);
  ~LocalInputPeerTrust();
  LocalInputPeerTrust(const LocalInputPeerTrust &) = delete;
  LocalInputPeerTrust &operator=(const LocalInputPeerTrust &) = delete;
  QString executable() const;
  bool verify(LocalInputProcess expected) const;

  // Optional stronger inspection; ordinary packaged-peer handshakes deliberately
  // do not require it. Requires Apple-issued same-Team signatures, hardened
  // runtime, explicit library validation, absent/empty entitlements and valid,
  // non-debugged dynamic status for the live anchor, self and expected peer.
  // macOS enforces Apple-or-same-Team library admission, not an exact image set.
  // This synchronous Security.framework/disk inspection must never run in an
  // input tap or be treated as an atomic, continuously fresh runtime attestation.
  std::optional<LocalInputRestrictedLibraryAdmissionSnapshot>
  inspectRestrictedLibraryAdmission(LocalInputProcess expected, QString *error = nullptr) const;

private:
  struct Impl;
  explicit LocalInputPeerTrust(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> m_impl;
};

} // namespace deskflow::windowshare
