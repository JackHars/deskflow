/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "net/SecurityLevel.h"
#include "net/TCPSocket.h"

#include <atomic>
#include <memory>
#include <mutex>

class Event;
class IEventQueue;
class SocketMultiplexer;
class ISocketMultiplexerJob;
class QString;

struct Ssl;

//! Secure socket
/*!
A secure socket using SSL.
*/
class SecureSocket : public TCPSocket
{
public:
  SecureSocket(
      IEventQueue *events, SocketMultiplexer *socketMultiplexer, IArchNetwork::AddressFamily family,
      SecurityLevel securityLevel = SecurityLevel::Encrypted
  );
  SecureSocket(
      IEventQueue *events, SocketMultiplexer *socketMultiplexer, ArchSocket socket,
      SecurityLevel securityLevel = SecurityLevel::Encrypted
  );
  SecureSocket(SecureSocket const &) = delete;
  SecureSocket(SecureSocket &&) = delete;
  ~SecureSocket() override;

  SecureSocket &operator=(SecureSocket const &) = delete;
  SecureSocket &operator=(SecureSocket &&) = delete;

  // ISocket overrides
  void close() override;
  void shutdownInput() override;
  void shutdownOutput() override;

  // IDataSocket overrides
  void connect(const NetworkAddress &) override;

  ISocketMultiplexerJob *newJob() override;
  bool isFatal() const override
  {
    return m_fatal;
  }
  void isFatal(bool b)
  {
    if (b)
      revokePeerIdentity();
    m_fatal = b;
  }
  bool isSecureReady() const;
  std::optional<PeerFingerprint> authenticatedPeerFingerprint() const override;
  std::optional<deskflow::AuthenticatedPeerTicket> authenticatedPeerTicket() const override;
  void secureConnect();
  void secureAccept();
  int secureRead(void *buffer, int size, int &read);
  int secureWrite(const void *buffer, int size, int &wrote);
  JobResult doRead() override;
  JobResult doWrite() override;
  void initSsl(bool server);
  bool loadCertificate(const QString &filename);

private:
  // SSL
  void initContext(bool server);
  void createSSL();
  void freeSSL();
  int secureAccept(int s);
  int secureConnect(int s);
  bool showCertificate() const;
  void checkResult(int n, int &retry);
  void disconnect();
  bool verifyCertFingerprint(const QString &FingerprintDatabasePath);
  bool issuePeerTicket();
  void revokePeerIdentity();

  ISocketMultiplexerJob *serviceConnect(ISocketMultiplexerJob *const socket, bool, bool, bool);

  ISocketMultiplexerJob *serviceAccept(ISocketMultiplexerJob *const socket, bool, bool, bool);

  void handleTCPConnected(const Event &event);
  void sendEvent(EventTypes) override;
  void setJob(ISocketMultiplexerJob *) override;

private:
  friend class SecurePeerIdentityTests;

  // External job submissions must finish before close removes the last job.
  // Multiplexer callbacks never take this mutex; they return their next job.
  std::mutex m_jobMutex;
  std::atomic_bool m_closing = false;

  // Protect TLS state and verified identity across the multiplexer thread,
  // close(), and callers checking the identity of an active stream.
  mutable std::mutex ssl_mutex_;

  std::unique_ptr<Ssl> m_ssl;
  std::optional<PeerFingerprint> m_verifiedPeerFingerprint;
  deskflow::AuthenticatedPeerIssuer m_peerIdentityIssuer;
  bool m_connectStarted = false;
  std::atomic_bool m_peerIdentityLive = false;
  std::atomic_bool m_secureReady = false;
  std::atomic_bool m_fatal = false;
  SecurityLevel m_securityLevel = SecurityLevel::Encrypted;

  bool m_writeRetry = false;
  int m_writeRetrySize = 0;
  int m_writeStaticBufferSize = 0;
  void *m_writeStaticBuffer = nullptr;
};
