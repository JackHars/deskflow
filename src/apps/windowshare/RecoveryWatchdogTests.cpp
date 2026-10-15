/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "RecoveryWatchdog.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <libproc.h>
#include <poll.h>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/proc_info.h>
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

void requireFixture(bool good, const char *what)
{
  if (!good)
    throw std::runtime_error(what);
}

struct Descriptor
{
  int fd = -1;
  Descriptor() = default;
  explicit Descriptor(int value) : fd(value)
  {
  }
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
  ~Descriptor()
  {
    reset();
  }
  void reset()
  {
    if (fd >= 0)
      close(fd);
    fd = -1;
  }
};

struct Pipe
{
  Descriptor reader, writer;
  Pipe()
  {
    int descriptors[2];
    requireFixture(pipe(descriptors) == 0, "create isolated watchdog fixture pipe");
    Descriptor readEnd(descriptors[0]), writeEnd(descriptors[1]);
    // Do not collide with the watchdog's fixed descriptor slots 3 and 4.
    reader.fd = fcntl(readEnd.fd, F_DUPFD_CLOEXEC, 16);
    writer.fd = fcntl(writeEnd.fd, F_DUPFD_CLOEXEC, 16);
    requireFixture(reader.fd >= 0 && writer.fd >= 0, "retain isolated watchdog fixture pipe");
  }
};

struct Child
{
  pid_t pid = -1;
  ~Child()
  {
    if (pid <= 0)
      return;
    // All observations use WNOWAIT. This direct child still belongs to us,
    // including after exit, so its numeric PID cannot have been reused.
    kill(pid, SIGKILL);
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
    }
  }
};

bool awaitExitWithoutReaping(pid_t pid, siginfo_t &status, std::chrono::seconds timeout = std::chrono::seconds(3))
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  do {
    status = {};
    if (waitid(P_PID, static_cast<id_t>(pid), &status, WEXITED | WNOHANG | WNOWAIT) == 0) {
      if (status.si_pid == pid)
        return true;
    } else if (errno != EINTR) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (std::chrono::steady_clock::now() < deadline);
  return false;
}

void unreapedHostRecovery(const char *executable, const char *path)
{
  struct MarkerCleanup
  {
    const char *path;
    ~MarkerCleanup()
    {
      unlink(path);
    }
  } marker{path};
  Pipe death, ready;
  Child host, watcher;
  host.pid = fork();
  requireFixture(host.pid >= 0, "fork isolated host for unreaped-exit regression");
  if (host.pid == 0) {
    death.reader.reset();
    ready.reader.reset();
    ready.writer.reset();
    // Only this host owns the lifetime pipe's writer. SIGKILL closes it without
    // running a destructor, while our parent deliberately keeps us unreaped.
    for (;;)
      pause();
  }
  death.writer.reset();
  proc_bsdinfo identity{};
  requireFixture(
      proc_pidinfo(host.pid, PROC_PIDTBSDINFO, 0, &identity, sizeof(identity)) == sizeof(identity) &&
          identity.pbi_start_tvsec > 0,
      "read exact isolated host process birth"
  );
  const std::string pidArgument = std::to_string(host.pid);
  const std::string birthArgument =
      std::to_string(std::uint64_t(identity.pbi_start_tvsec) * 1000000 + identity.pbi_start_tvusec);
  watcher.pid = fork();
  requireFixture(watcher.pid >= 0, "fork directly owned recovery watcher");
  if (watcher.pid == 0) {
    if (dup2(death.reader.fd, 3) < 0 || dup2(ready.writer.fd, 4) < 0)
      _exit(10);
    death.reader.reset();
    ready.reader.reset();
    ready.writer.reset();
    char *arguments[] = {
        const_cast<char *>(executable),
        const_cast<char *>("--recovery-watchdog-private"),
        const_cast<char *>(pidArgument.c_str()),
        const_cast<char *>(birthArgument.c_str()),
        const_cast<char *>(path),
        nullptr
    };
    // This private mode execs only this test binary's marker-writing branch.
    // _exit avoids running copied parent cleanup if the mode refuses startup.
    _exit(runRecoveryWatchdog(5, arguments));
  }
  death.reader.reset();
  ready.writer.reset();
  pollfd acknowledgement{ready.reader.fd, POLLIN | POLLHUP, 0};
  int polled;
  do {
    polled = poll(&acknowledgement, 1, 3000);
  } while (polled < 0 && errno == EINTR);
  char received = 0;
  requireFixture(
      polled > 0 && (acknowledgement.revents & POLLIN) && read(ready.reader.fd, &received, 1) == 1 && received == 'R',
      "isolated watcher acknowledges the live host before its exit"
  );
  requireFixture(access(path, F_OK) != 0, "live isolated host cannot trigger recovery");
  requireFixture(kill(host.pid, SIGKILL) == 0, "terminate only the isolated host");
  siginfo_t hostStatus{};
  requireFixture(
      awaitExitWithoutReaping(host.pid, hostStatus) && hostStatus.si_code == CLD_KILLED &&
          hostStatus.si_status == SIGKILL,
      "observe isolated host death while retaining its zombie identity"
  );
  siginfo_t watcherStatus{};
  requireFixture(
      awaitExitWithoutReaping(watcher.pid, watcherStatus),
      "watchdog must recover an exited host before its parent reaps it"
  );
  requireFixture(
      watcherStatus.si_code == CLD_EXITED && watcherStatus.si_status == 0, "unreaped-host recovery exits successfully"
  );
  Descriptor result(open(path, O_RDONLY));
  char contents[10]{};
  requireFixture(
      result.fd >= 0 && read(result.fd, contents, 9) == 9 && std::strcmp(contents, "recovered") == 0,
      "unreaped-host recovery writes the exact private marker"
  );
}

enum class ProgressCase
{
  StartupStall,
  EventLoopStall,
  HealthyThenStop
};

void progressHostRecovery(const char *executable, const char *path, ProgressCase test)
{
  Pipe ready;
  Child host;
  host.pid = fork();
  requireFixture(host.pid >= 0, "fork isolated host for progress monitoring");
  if (host.pid == 0) {
    ready.reader.reset();
    {
      std::string error;
      RecoveryWatchdog watchdog(executable, path, &error);
      if (!watchdog.armed())
        _exit(11);
      if (test != ProgressCase::StartupStall && !watchdog.heartbeat())
        _exit(12);
      const char started = 'R';
      if (write(ready.writer.fd, &started, 1) != 1)
        _exit(13);
      ready.writer.reset();
      if (test == ProgressCase::HealthyThenStop) {
        // Stay healthy beyond the five-second active deadline. Only the
        // creating thread is allowed to demonstrate event-loop progress.
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        while (std::chrono::steady_clock::now() < until) {
          if (!watchdog.heartbeat() || access(path, F_OK) == 0)
            _exit(14);
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      } else {
        // A worker that continues running must not keep a stuck owner alive.
        // This also exercises expiration with another thread calling heartbeat.
        std::thread wrongThread([&] {
          for (;;) {
            if (watchdog.heartbeat())
              _exit(15);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
          }
        });
        for (;;)
          pause(); // model a native syscall that never returns to the event loop
      }
    }
    // Normal teardown cancels and joins the progress monitor. Its expiry must
    // not later terminate a host that has already finished native restoration.
    // The independent recovery child still cannot recover while we are alive.
    std::this_thread::sleep_for(std::chrono::seconds(6));
    _exit(access(path, F_OK) == 0 ? 16 : 0);
  }
  ready.writer.reset();
  pollfd acknowledgement{ready.reader.fd, POLLIN | POLLHUP, 0};
  int polled;
  do {
    polled = poll(&acknowledgement, 1, 5000);
  } while (polled < 0 && errno == EINTR);
  char received = 0;
  requireFixture(
      polled > 0 && (acknowledgement.revents & POLLIN) && read(ready.reader.fd, &received, 1) == 1 && received == 'R',
      "progress-monitored host acknowledges both armed watchdogs"
  );
  requireFixture(access(path, F_OK) != 0, "live monitored host has not started recovery");
  const auto started = std::chrono::steady_clock::now();
  siginfo_t status{};
  requireFixture(
      awaitExitWithoutReaping(host.pid, status, std::chrono::seconds(20)),
      "host progress timeout or normal teardown exits within the fixture bound"
  );
  const auto elapsed = std::chrono::steady_clock::now() - started;
  const int expected = test == ProgressCase::HealthyThenStop ? 0 : RecoveryWatchdog::progressTimeoutExitCode;
  requireFixture(
      status.si_code == CLD_EXITED && status.si_status == expected,
      "host exits with the exact progress-timeout or normal status"
  );
  // Allow small scheduling/pipe-observation skew, but catch an immediate kill
  // or accidentally applying the active deadline to unserviced startup.
  const int minimumSeconds = test == ProgressCase::StartupStall ? 14 : test == ProgressCase::EventLoopStall ? 4 : 11;
  requireFixture(elapsed >= std::chrono::seconds(minimumSeconds), "progress deadline did not fire prematurely");
  const auto recoveryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  struct stat marker{};
  while ((stat(path, &marker) != 0 || marker.st_size != 9) && std::chrono::steady_clock::now() < recoveryDeadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  Descriptor result(open(path, O_RDONLY));
  char contents[10]{};
  requireFixture(
      result.fd >= 0 && read(result.fd, contents, 9) == 9 && std::strcmp(contents, "recovered") == 0,
      "progress timeout recovers through the exact private marker while host remains unreaped"
  );
  unlink(path);
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
  try {
    // Run before the existing grandchild happy path. A regression failure
    // kills and reaps both directly owned children before check() exits.
    unreapedHostRecovery(argv[0], path);
    progressHostRecovery(argv[0], path, ProgressCase::StartupStall);
    progressHostRecovery(argv[0], path, ProgressCase::EventLoopStall);
    progressHostRecovery(argv[0], path, ProgressCase::HealthyThenStop);
  } catch (const std::exception &failure) {
    check(false, failure.what());
  }
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
  check(
      waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
      "watchdog must arm before an export and not run recovery while host lives"
  );
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (access(path, F_OK) != 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  const int result = open(path, O_RDONLY);
  check(result >= 0, "independent watchdog triggers recovery after host crash");
  char contents[10]{};
  check(
      read(result, contents, 9) == 9 && std::strcmp(contents, "recovered") == 0,
      "watchdog recovered only via expected journal argument"
  );
  close(result);
  unlink(path);
  std::puts(
      "Recovery watchdog spawn, readiness, crash, stalled startup/event loop, heartbeat and teardown cases passed"
  );
  return 0;
}
