/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "LocalInputChannel.h"

#include <chrono>
#include <utility>
#include <vector>

namespace deskflow::windowshare {

// An immutable record of a completed inspection, not a live/revocable capability.
// It is tied to both fixed roles and the live bundle-main anchor. Retaining it
// after a process exits, is debugged, or changes does not retain any authority.
// In particular, this is NOT exact loaded-image identity, process-memory
// attestation, or a LocalInputPreflight::Checks::codeAdmission provider.
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

// Historical inspection of the exact non-Apple library identities permitted by
// each live role's authenticated executable signature. The OS's Apple/system
// exception remains; hashes identify code, not its loading path, a loaded-image
// inventory, or uncompromised process memory. This is not retained authority or
// a LocalInputPreflight::Checks::codeAdmission provider.
class LocalInputExactPermittedLibrariesSnapshot final
{
public:
  const LocalInputRestrictedLibraryAdmissionSnapshot &restrictedAdmission() const
  {
    return m_restricted;
  }
  const QByteArray &manifestHash() const
  {
    return m_manifestHash;
  }
  const std::vector<QByteArray> &permittedCodeHashes() const
  {
    return m_hashes;
  }
  std::size_t manifestRecordCount() const
  {
    return m_records;
  }
  std::size_t librarySliceCount() const
  {
    return m_slices;
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
  LocalInputExactPermittedLibrariesSnapshot(
      LocalInputRestrictedLibraryAdmissionSnapshot restricted, QByteArray manifestHash, std::vector<QByteArray> hashes,
      std::size_t records, std::size_t slices, std::chrono::steady_clock::time_point started,
      std::chrono::steady_clock::time_point finished
  )
      : m_restricted(std::move(restricted)),
        m_manifestHash(std::move(manifestHash)),
        m_hashes(std::move(hashes)),
        m_records(records),
        m_slices(slices),
        m_started(started),
        m_finished(finished)
  {
  }
  LocalInputRestrictedLibraryAdmissionSnapshot m_restricted;
  QByteArray m_manifestHash;
  std::vector<QByteArray> m_hashes;
  std::size_t m_records, m_slices;
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

  // Requires macOS 14+, the stronger policy above, a root-sealed canonical
  // Resources/deskflow-exact-libraries.json manifest, and an independently parsed
  // library constraint (signature special slot 11) in the pinned anchor, self
  // and peer. Each constraint must express exactly the manifest's Team AND its
  // SHA-256 CodeDirectory hash set. Manifest library slices are checked against
  // their actual signed files. No private Security APIs or executable helpers
  // are invoked. This bounded synchronous inspection is unsuitable for a tap;
  // its result does not assert atomic or ongoing process integrity.
  std::optional<LocalInputExactPermittedLibrariesSnapshot>
  inspectExactPermittedLibraries(LocalInputProcess expected, QString *error = nullptr) const;

private:
  struct Impl;
  explicit LocalInputPeerTrust(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> m_impl;
};

} // namespace deskflow::windowshare
