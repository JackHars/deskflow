/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LatestSnapshotWriter.h"
#if defined(__unix__) || defined(__APPLE__)
#include "DisposableSnapshotFile.h"
#include <QDir>
#include <QTemporaryDir>
#include <atomic>
#include <sys/stat.h>
#endif
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace deskflow::windowshare;
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
#if defined(__unix__) || defined(__APPLE__)
void snapshotFiles()
{
  QTemporaryDir directory;
  check(directory.isValid(), "private snapshot directory unavailable");
  const auto path = directory.filePath("state.json");
  const QByteArray first(32768, 'a'), second(65536, 'b');
  QString error;
  check(writeDisposableSnapshot(path, first, error) && error.isEmpty(), "initial snapshot failed");
  QFile retained(path);
  check(retained.open(QIODevice::ReadOnly), "cannot retain the original snapshot");
  check(writeDisposableSnapshot(path, second, error), "snapshot replacement failed");
  check(retained.readAll() == first, "replacement changed an already opened snapshot");
  QFile current(path);
  check(current.open(QIODevice::ReadOnly) && current.readAll() == second, "new reader did not see replacement");
  struct stat info{};
  check(
      stat(QFile::encodeName(path).constData(), &info) == 0 && (info.st_mode & 0777) == 0600,
      "snapshot permissions are not private"
  );
  current.close();
  retained.close();

  // Real concurrent opens must never see an absent, truncated or mixed file.
  std::mutex mutex;
  std::condition_variable changed;
  bool started = false;
  std::atomic<bool> readersValid{true};
  std::thread reader([&] {
    {
      std::lock_guard lock(mutex);
      started = true;
    }
    changed.notify_one();
    for (unsigned i = 0; i < 500; ++i) {
      QFile observed(path);
      const bool opened = observed.open(QIODevice::ReadOnly);
      const auto bytes = opened ? observed.readAll() : QByteArray{};
      if (!opened || (bytes != first && bytes != second))
        readersValid = false;
    }
  });
  struct Join
  {
    std::thread &thread;
    ~Join()
    {
      if (thread.joinable())
        thread.join();
    }
  } joined{reader};
  {
    std::unique_lock lock(mutex);
    changed.wait(lock, [&] { return started; });
  }
  for (unsigned i = 0; i < 100; ++i)
    check(writeDisposableSnapshot(path, i % 2 ? first : second, error), "concurrent replacement failed");
  reader.join();
  check(readersValid, "concurrent reader observed incomplete snapshot");
  check(QDir(directory.path()).mkdir("occupied"), "cannot create rename-failure fixture");
  check(
      !writeDisposableSnapshot(directory.filePath("occupied"), first, error) && !error.isEmpty(),
      "failed rename did not report failure"
  );
  check(QFileInfo(directory.filePath("occupied")).isDir(), "failed rename replaced existing directory");
  check(
      QDir(directory.path()).entryList({".snapshot-*"}, QDir::Files | QDir::Hidden).isEmpty(),
      "failed publication leaked temporary files"
  );
  check(
      !writeDisposableSnapshot(directory.filePath("absent/state.json"), first, error) && !error.isEmpty(),
      "missing parent did not report failure"
  );
  check(!writeDisposableSnapshot("relative-state.json", first, error), "relative snapshot path accepted");

  const auto link = directory.filePath("link.json");
  check(QFile::link(path, link), "cannot create symlink replacement fixture");
  check(writeDisposableSnapshot(link, second, error), "symlink replacement failed");
  check(!QFileInfo(link).isSymLink(), "publication followed the destination symlink");
  check(current.open(QIODevice::ReadOnly) && current.readAll() == first, "publication modified the symlink target");
}
#endif
int main()
{
  try {
#if defined(__unix__) || defined(__APPLE__)
    snapshotFiles();
#endif
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false, release = false;
    std::vector<std::string> writes;
    LatestSnapshotWriter writer([&](const std::string &value) {
      std::unique_lock lock(mutex);
      if (!started) {
        started = true;
        changed.notify_one();
        changed.wait(lock, [&] { return release; });
      }
      writes.push_back(value);
      return true;
    });
    writer.publish("first");
    {
      std::unique_lock lock(mutex);
      changed.wait(lock, [&] { return started; });
    }
    // The disk callback is deliberately blocked. Publishing must still return
    // and the pending queue must retain only the newest complete snapshot.
    for (int i = 0; i < 1000; ++i)
      writer.publish(std::to_string(i));
    writer.publish("final neutral state");
    {
      std::lock_guard lock(mutex);
      release = true;
    }
    changed.notify_one();
    writer.finish();
    check(
        writes == std::vector<std::string>{"first", "final neutral state"},
        "blocked writer lost final state or queued stale snapshots"
    );
    check(!writer.publish("too late") && writer.failures() == 0, "finished writer accepted new work");
    writer.finish();
    LatestSnapshotWriter failed([](const std::string &) { return false; });
    failed.publish("failed write");
    failed.finish();
    check(failed.failures() == 1, "write failure was hidden");
    LatestSnapshotWriter throwing([](const std::string &) -> bool { throw std::runtime_error("disk failure"); });
    throwing.publish("exception");
    throwing.finish();
    check(throwing.failures() == 1, "worker exception was hidden");
    unsigned finalWrites = 0;
    {
      LatestSnapshotWriter scoped([&](const std::string &) {
        ++finalWrites;
        return true;
      });
      scoped.publish("shutdown");
    }
    check(finalWrites == 1, "destructor did not drain the last snapshot");
    std::puts("PASS: atomic private snapshots, bounded publication, final drain and failure reporting");
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
