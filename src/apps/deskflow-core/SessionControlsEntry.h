/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace deskflow::windowshare {
// Finder invokes the packaged Core without arguments. It may open controls,
// but explicit server/client/probe/help invocations keep their existing path.
inline QString sessionControlsEntry(const QString &core, const QStringList &arguments)
{
  const QFileInfo executable(core);
  if (arguments.size() != 1 || executable.fileName() != "deskflow-core")
    return {};
  const QDir directory(executable.absolutePath());
  if (directory.dirName() != "MacOS" ||
      !QFileInfo(directory.absoluteFilePath("../Resources/seamless-profile.py")).isFile())
    return {};
  const QFileInfo controls(directory.filePath("deskflow-windowshare"));
  return controls.isFile() && controls.isExecutable() ? controls.canonicalFilePath() : QString();
}
} // namespace deskflow::windowshare
