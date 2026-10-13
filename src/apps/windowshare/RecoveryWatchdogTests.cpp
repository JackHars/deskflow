/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "RecoveryWatchdog.h"

#include <cerrno>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace deskflow::windowshare::native;
namespace {
void check(bool good, const char *what)
{
  if (!good) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}
} // namespace
int main(int argc, char **argv)
{
  if (recoveryWatchdogMode(argc, argv))
    return runRecoveryWatchdog(argc, argv);
  if (argc == 4 && std::string(argv[1]) == "--recover" && std::string(argv[2]) == "--journal") {
    // The real application instead reopens its QLockFile and calls
    // NativeHost::recoverJournal. Here we only prove watchdog exec-on-crash.
    const int fd = open(argv[3], O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
      return 8;
    const bool done = write(fd, "recovered", 9) == 9;
    close(fd);
    return done ? 0 : 9;
  }
  check(argc == 1, "unexpected watchdog test arguments");
  char path[] = "/tmp/deskflow-watchdog-test.XXXXXX";
  const int fd = mkstemp(path);
  check(fd >= 0, "secure test path creation");
  close(fd);
  unlink(path);
  // Negative case: the host MUST NOT accept an unlaunchable watchdog, since
  // accepting and moving the source would create crash-orphan windows.
  std::string error;
  RecoveryWatchdog missing("/definitely/not/a/deskflow/binary", path, &error);
  check(!missing.armed() && !error.empty(), "invalid watchdog binary refuses export");
  const auto pid = fork();
  check(pid >= 0, "fork simulated host");
  if (pid == 0) {
    std::string childError;
    RecoveryWatchdog watch(argv[0], path, &childError);
    if (!watch.armed()) {
      std::fprintf(stderr, "watchdog start: %s\n", childError.c_str());
      _exit(4);
    }
    // No recovery while the original host is still alive/owns the journal.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    if (access(path, F_OK) == 0)
      _exit(5);
    _exit(0); // simulate sudden exit without NativeHost destructor
  }
  int status = 0;
  check(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "watchdog must arm before an export and not run recovery while host lives");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (access(path, F_OK) != 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  const int result = open(path, O_RDONLY);
  check(result >= 0, "independent watchdog triggers recovery after host crash");
  char contents[10]{};
  check(read(result, contents, 9) == 9 && std::strcmp(contents, "recovered") == 0,
        "watchdog recovered only via expected journal argument");
  close(result);
  unlink(path);
  std::puts("Recovery watchdog spawn, readiness, crash and exec cases passed");
  return 0;
}
