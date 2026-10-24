/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "LocalInputPreflight.h"
#include <QStringList>
#include <functional>

namespace deskflow::windowshare {

// One immutable local configuration snapshot. The policy is never inferred
// from the host's network pin, names, addresses, or private protocol messages.
// Missing/empty inputPeerPairs retains the ordinary host with input denied.
class LocalInputHostConfiguration final
{
public:
  static LocalInputHostConfiguration load(const QString &configPath);
  const QStringList &hostArguments() const;
  const LocalInputPeerPolicy &peerPolicy() const;
  const QString &inputEvidenceConfig() const;

private:
  LocalInputHostConfiguration() = default;
  QStringList m_arguments;
  QString m_inputEvidenceConfig;
  LocalInputPeerPolicy m_peerPolicy;
};

// Inspect the original argv before Qt consumes any of it. A probe must be the
// sole option. The ordinary parser independently validates server/config mode.
struct LocalInputCoreBootstrap
{
  static LocalInputCoreBootstrap inspect(int argc, char *const argv[]);
  bool probe = false;
  bool codeAdmissionProbe = false;
  bool permissionsProbe = false, requestPermissions = false;
  QString hostConfig;
  std::optional<LocalInputSocketPair> pair;
};

// Own an inherited endpoint immediately, restoring CLOEXEC before any Qt
// application/plugin initialization. argv supplies only an FD, never trust.
class LocalInputHostBootstrap final
{
public:
  static LocalInputHostBootstrap inspect(int argc, char *const argv[]);
  ~LocalInputHostBootstrap();
  LocalInputHostBootstrap(LocalInputHostBootstrap &&other) noexcept;
  LocalInputHostBootstrap(const LocalInputHostBootstrap &) = delete;
  LocalInputHostBootstrap &operator=(const LocalInputHostBootstrap &) = delete;
  bool inherited() const;
  bool probe() const;
  // Same bounded, explicitly configured map forwarded by the trusted core.
  // This value alone never authorizes delivery or constructs a controller.
  const LocalInputPeerPolicy &peerPolicy() const;
  // Reject diagnostic/configuration actions before they have side effects.
  void validateOptions(const QStringList &names) const;
  // Derives and verifies the direct parent independently using packaged code.
  // Optional recorder setup runs after parent/endpoint validation, before
  // constructing the channel whose initial queues it must observe.
  std::unique_ptr<LocalInputChannel> adopt(const std::function<void()> &verifiedSetup = {});

private:
  LocalInputHostBootstrap() = default;
  int m_descriptor = -1;
  bool m_probe = false, m_peerPairsSupplied = false, m_evidenceSupplied = false;
  LocalInputPeerPolicy m_peerPolicy;
};

} // namespace deskflow::windowshare
