/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QFileInfo>
#include <QTemporaryFile>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace deskflow::windowshare {

// Ephemeral diagnostic state in the coordinator's private directory. Readers
// see one complete replacement, but the snapshot need not survive a machine
// crash. Never use this for recovery journals, evidence or configuration:
// unlike QSaveFile, this deliberately does not sync the file to durable media.
inline bool writeDisposableSnapshot(const QString &path, const QByteArray &bytes, QString &error)
{
  error.clear();
  const QFileInfo destination(path);
  if (!destination.isAbsolute() || destination.fileName().isEmpty()) {
    error = QStringLiteral("Disposable snapshot requires an absolute file path");
    return false;
  }
  QTemporaryFile file(destination.absolutePath() + QStringLiteral("/.snapshot-XXXXXX"));
  if (!file.open() || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      file.write(bytes) != bytes.size() || !file.flush()) {
    error = file.errorString();
    return false; // QTemporaryFile removes the unpublished file.
  }
  const auto temporary = QFile::encodeName(file.fileName());
  const auto target = QFile::encodeName(path);
  file.close();
  if (file.error() != QFileDevice::NoError) {
    error = file.errorString();
    return false;
  }
  // Both names are in the same private directory. POSIX rename atomically
  // replaces the old name; it does not follow an existing destination symlink.
  if (::rename(temporary.constData(), target.constData()) != 0) {
    error = QString::fromLocal8Bit(std::strerror(errno));
    return false;
  }
  file.setAutoRemove(false);
  return true;
}

} // namespace deskflow::windowshare
