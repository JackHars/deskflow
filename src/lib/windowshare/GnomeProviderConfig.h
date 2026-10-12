/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QStringList>

namespace deskflow::windowshare {

// Explicit local configuration for the experimental GNOME-owned EIS session.
// The caller supplies viewer connection data, never arbitrary child arguments,
// environment overrides, shell text, native input authority or a fallback bus.
class GnomeProviderConfig final
{
public:
  static GnomeProviderConfig load(const QString &path);
  const QString &viewerExecutable() const noexcept
  {
    return m_executable;
  }
  const QStringList &viewerArguments() const noexcept
  {
    return m_arguments;
  }
  const QString &peerFingerprint() const noexcept
  {
    return m_peerFingerprint;
  }

private:
  QString m_executable, m_peerFingerprint;
  QStringList m_arguments;
};

} // namespace deskflow::windowshare
