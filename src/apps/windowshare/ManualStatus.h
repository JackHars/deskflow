/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cerrno>
#include <fcntl.h>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>

namespace deskflow::windowshare {
constexpr int manualStartSeconds = 300;

inline bool sameStatusFile(const struct stat &a, const struct stat &b)
{
#ifdef __APPLE__
  const auto am = a.st_mtimespec, bm = b.st_mtimespec, ac = a.st_ctimespec, bc = b.st_ctimespec;
#else
  const auto am = a.st_mtim, bm = b.st_mtim, ac = a.st_ctim, bc = b.st_ctim;
#endif
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size && a.st_uid == b.st_uid &&
         a.st_mode == b.st_mode && a.st_nlink == b.st_nlink && am.tv_sec == bm.tv_sec && am.tv_nsec == bm.tv_nsec &&
         ac.tv_sec == bc.tv_sec && ac.tv_nsec == bc.tv_nsec;
}

struct ManualStatus
{
  QString state;
  int remainingSeconds = 0;
  struct stat file{};
};

struct ManualStatusRead
{
  enum class Kind
  {
    Accepted,
    Replaced,
    Invalid
  };
  Kind kind = Kind::Invalid;
  std::optional<ManualStatus> status;
  const char *reason = "invalid directory";
  int systemError = 0;
};

// Tests interleave an actual rename at these boundaries; no timing sleeps or
// replacement retry loop is needed in either tests or the UI.
enum class ManualStatusReadStage
{
  Opened,
  Read
};
struct ManualStatusReadHook
{
  void operator()(ManualStatusReadStage) const
  {
  }
};

inline bool privateStatusFile(const struct stat &info)
{
  return S_ISREG(info.st_mode) && info.st_uid == getuid() && !(info.st_mode & 0077) && info.st_size > 0 &&
         info.st_size <= 512;
}

template <typename Hook = ManualStatusReadHook>
ManualStatusRead readManualStatus(int directory, const QByteArray &name, int sessionSeconds, Hook hook = {})
{
  using Kind = ManualStatusRead::Kind;
  if (directory < 0 || name.isEmpty() || name.contains('/') || name.contains('\0'))
    return {};
  const int descriptor = openat(directory, name.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (descriptor < 0)
    return {Kind::Invalid, {}, "open failed", errno};
  QFile file;
  if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
    const auto error = errno;
    close(descriptor);
    return {Kind::Invalid, {}, "descriptor adoption failed", error};
  }
  hook(ManualStatusReadStage::Opened);
  // An atomic publication unlinks the old inode even while our descriptor is
  // reading it. Only classify that specific race as transient, and never use
  // the unlinked bytes as a new heartbeat or state transition.
  const auto replaced = [&](const struct stat &old) {
    struct stat current{};
    return privateStatusFile(old) && old.st_nlink == 0 &&
           fstatat(directory, name.constData(), &current, AT_SYMLINK_NOFOLLOW) == 0 && privateStatusFile(current) &&
           current.st_nlink == 1 && (old.st_dev != current.st_dev || old.st_ino != current.st_ino);
  };
  struct stat before{}, after{};
  if (fstat(descriptor, &before) != 0)
    return {Kind::Invalid, {}, "initial stat failed", errno};
  if (replaced(before))
    return {Kind::Replaced, {}, "publication replaced after open"};
  if (!privateStatusFile(before) || before.st_nlink != 1)
    return {Kind::Invalid, {}, "unsafe publication metadata"};
  const auto bytes = file.read(513);
  hook(ManualStatusReadStage::Read);
  if (fstat(descriptor, &after) != 0)
    return {Kind::Invalid, {}, "final stat failed", errno};
  if (replaced(after))
    return {Kind::Replaced, {}, "publication replaced during read"};
  if (bytes.size() != before.st_size || !sameStatusFile(before, after))
    return {Kind::Invalid, {}, "publication changed in place"};
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  // All three scalar fields have no colons. Reject duplicate keys too.
  if (error.error != QJsonParseError::NoError || !document.isObject() || bytes.count(':') != 3)
    return {Kind::Invalid, {}, "invalid publication JSON"};
  const auto value = document.object();
  const auto state = value.value("state").toString();
  const auto seconds = value.value("remainingSeconds");
  if (value.size() != 3 || !value.value("version").isDouble() || value.value("version").toDouble() != 1 ||
      !seconds.isDouble() || seconds.toDouble() != seconds.toInt(-1) || seconds.toInt(-1) < 0 ||
      seconds.toInt() > (state == "ready" ? manualStartSeconds : sessionSeconds) ||
      (state != "preparing" && state != "ready" && state != "active" && state != "stopped") ||
      (state == "active" ? seconds.toInt() == 0 : state != "ready" && seconds.toInt() != 0))
    return {Kind::Invalid, {}, "invalid publication fields"};
  return {Kind::Accepted, ManualStatus{state, seconds.toInt(), after}, "accepted"};
}

class ManualStatusHeartbeat
{
public:
  bool observe(const ManualStatusRead &read, qint64 now)
  {
    if (read.kind == ManualStatusRead::Kind::Accepted && read.status) {
      if (m_last && !sameStatusFile(m_last->file, read.status->file)) {
        m_heartbeat = true;
        m_changedAt = now;
      }
      m_last = read.status;
    }
    // A proven concurrent rename may keep the last validated state only for
    // its existing lease. It never starts/renews a lease or consumes new state.
    return read.kind != ManualStatusRead::Kind::Invalid && m_last && m_heartbeat && now >= m_changedAt &&
           now - m_changedAt < 2000;
  }
  const std::optional<ManualStatus> &last() const
  {
    return m_last;
  }
  qint64 changedAt() const
  {
    return m_changedAt;
  }

private:
  std::optional<ManualStatus> m_last;
  qint64 m_changedAt = 0;
  bool m_heartbeat = false;
};
} // namespace deskflow::windowshare
