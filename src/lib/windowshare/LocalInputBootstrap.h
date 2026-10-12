/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "LocalInputChannel.h"
#include <QStringList>

namespace deskflow::windowshare {

// Inspect the original argv before Qt consumes any of it. A probe must be the
// sole option. The ordinary parser independently validates server/config mode.
struct LocalInputCoreBootstrap
{
  static LocalInputCoreBootstrap inspect(int argc, char *const argv[]);
  bool probe = false;
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
  // Reject diagnostic/configuration actions before they have side effects.
  void validateOptions(const QStringList &names) const;
  // Derives and verifies the direct parent independently using packaged code.
  std::unique_ptr<LocalInputChannel> adopt();

private:
  LocalInputHostBootstrap() = default;
  int m_descriptor = -1;
  bool m_probe = false;
};

// Bounded, typed, allowlisted configuration; never forwards arbitrary argv,
// executable paths, process identities, private FDs or initialization actions.
QStringList localInputHostArguments(const QString &configPath);

} // namespace deskflow::windowshare
