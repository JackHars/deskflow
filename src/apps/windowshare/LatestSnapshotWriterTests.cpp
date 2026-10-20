/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LatestSnapshotWriter.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace deskflow::windowshare;
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
int main()
{
  try {
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
    std::puts("PASS: asynchronous bounded snapshots, final drain and failure reporting");
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
