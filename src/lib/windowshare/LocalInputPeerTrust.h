/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "LocalInputChannel.h"

namespace deskflow::windowshare {

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

private:
  struct Impl;
  explicit LocalInputPeerTrust(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> m_impl;
};

} // namespace deskflow::windowshare
