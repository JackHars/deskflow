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
#include <spawn.h>
#include <string>
#include <sys/proc_info.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <signal.h>

extern char **environ;

namespace deskflow::windowshare::native {
namespace {
constexpr int childInput = 3;
constexpr int childReady = 4;
constexpr int startupTimeoutMs = 3000;
constexpr auto deathTimeout = std::chrono::seconds(20);

struct Descriptor
{
  int fd = -1;
  ~Descriptor()
  {
    if (fd >= 0)
      close(fd);
  }
  Descriptor() = default;
  explicit Descriptor(int value) : fd(value)
  {
  }
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
  Descriptor(Descriptor &&other) noexcept : fd(other.fd)
  {
    other.fd = -1;
  }
  int release()
  {
    int value = fd;
    fd = -1;
    return value;
  }
};

bool pipePair(Descriptor &reader, Descriptor &writer)
{
  int fds[2];
  if (pipe(fds) != 0)
    return false;
  Descriptor a(fds[0]), b(fds[1]);
  // Keep spawn's temporary source descriptors outside the fixed child FD
  // namespace; avoid dup2/close action ordering corruption if stdin is absent.
  reader.fd = fcntl(a.fd, F_DUPFD_CLOEXEC, 16);
  writer.fd = fcntl(b.fd, F_DUPFD_CLOEXEC, 16);
  if (reader.fd < 0 || writer.fd < 0)
    return false;
  return true;
}

std::uint64_t birthMicros(pid_t pid)
{
  proc_bsdinfo info{};
  if (pid <= 0 || proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info) ||
      info.pbi_start_tvsec <= 0)
    return 0;
  return std::uint64_t(info.pbi_start_tvsec) * 1000000 + info.pbi_start_tvusec;
}

bool originalParentAlive(pid_t pid, std::uint64_t born)
{
  const auto observed = birthMicros(pid);
  if (observed)
    return observed == born;
  // A temporarily inaccessible kernel identity is NOT proof that the parent
  // died. On macOS PID reuse cannot authorize recovery while a live process
  // might still own the journal lock.
  return kill(pid, 0) == 0 || errno != ESRCH;
}

bool parseUint(const char *s, std::uint64_t &result)
{
  if (!s || !*s)
    return false;
  result = 0;
  for (const char *p = s; *p; ++p) {
    if (*p < '0' || *p > '9' || result > (UINT64_MAX - (*p - '0')) / 10)
      return false;
    result = result * 10 + (*p - '0');
  }
  return result != 0;
}

bool parentEnded(pid_t pid, std::uint64_t birth)
{
  const auto deadline = std::chrono::steady_clock::now() + deathTimeout;
  while (originalParentAlive(pid, birth)) {
    if (std::chrono::steady_clock::now() >= deadline)
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return true;
}
} // namespace

RecoveryWatchdog::RecoveryWatchdog(const std::string &executable, const std::string &journal, std::string *error)
{
  const auto fail = [error](const char *reason) {
    if (error)
      *error = reason;
  };
  if (executable.empty() || executable.front() != '/' || journal.empty() || journal.front() != '/' ||
      executable.find('\0') != std::string::npos || journal.find('\0') != std::string::npos) {
    fail("Recovery watchdog requires absolute native executable and journal paths");
    return;
  }
  const auto birth = birthMicros(getpid());
  if (!birth) {
    fail("Cannot verify host kernel process-birth identity for watchdog");
    return;
  }
  Descriptor hostRead, hostWrite, ackRead, ackWrite;
  if (!pipePair(hostRead, hostWrite) || !pipePair(ackRead, ackWrite)) {
    fail("Cannot allocate private recovery-watchdog pipes");
    return;
  }
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    fail("Cannot initialize recovery-watchdog spawn actions");
    return;
  }
  // Source FDs are >=16, and childInput/childReady are never reused by a
  // parent descriptor. CLOEXEC on parent endpoints prevents leaks on exec.
  int code = posix_spawn_file_actions_adddup2(&actions, hostRead.fd, childInput);
  if (!code)
    code = posix_spawn_file_actions_adddup2(&actions, ackWrite.fd, childReady);
  if (!code)
    code = posix_spawn_file_actions_addclose(&actions, hostWrite.fd);
  if (!code)
    code = posix_spawn_file_actions_addclose(&actions, ackRead.fd);
  if (!code)
    code = posix_spawn_file_actions_addclose(&actions, hostRead.fd);
  if (!code)
    code = posix_spawn_file_actions_addclose(&actions, ackWrite.fd);
  const std::string pidArg = std::to_string(getpid()), birthArg = std::to_string(birth);
  char *arguments[] = {
      const_cast<char *>(executable.c_str()), const_cast<char *>("--recovery-watchdog-private"),
      const_cast<char *>(pidArg.c_str()), const_cast<char *>(birthArg.c_str()),
      const_cast<char *>(journal.c_str()), nullptr};
  posix_spawnattr_t attributes;
  if (!code)
    code = posix_spawnattr_init(&attributes);
  if (!code) {
    // An independent process group prevents terminal Ctrl+C or a supervising
    // group signal from killing both the Mac host and its recovery process.
    code = posix_spawnattr_setpgroup(&attributes, 0);
    if (!code)
      code = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (!code)
      code = posix_spawn(&m_pid, executable.c_str(), &actions, &attributes, arguments, ::environ);
    posix_spawnattr_destroy(&attributes);
  }
  posix_spawn_file_actions_destroy(&actions);
  if (code || m_pid <= 0) {
    m_pid = -1;
    fail("Cannot start independent recovery-watchdog executable");
    return;
  }
  // Close child endpoints now, otherwise our own read side would not observe
  // a failed child. The watchdog reports readiness before we permit exports.
  close(hostRead.release());
  close(ackWrite.release());
  pollfd descriptor{ackRead.fd, POLLIN | POLLHUP, 0};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(startupTimeoutMs);
  int polled = 0;
  for (;;) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()
    ).count();
    if (remaining <= 0)
      break;
    polled = poll(&descriptor, 1, int(remaining));
    if (polled >= 0 || errno != EINTR)
      break;
  }
  char ready = 0;
  if (polled <= 0 || (descriptor.revents & POLLIN) == 0 || read(ackRead.fd, &ready, 1) != 1 || ready != 'R') {
    fail("Recovery watchdog did not acknowledge its private pipe before export");
    // Do not arm a questionable helper; it can only try the existing --recover
    // after the host exits, when its journal has no relocated windows.
    return;
  }
  m_write = hostWrite.release();
}

RecoveryWatchdog::~RecoveryWatchdog()
{
  if (m_write >= 0)
    close(m_write);
}

bool RecoveryWatchdog::armed() const
{
  return m_write >= 0 && m_pid > 0;
}

bool RecoveryWatchdog::alive()
{
  if (!armed())
    return false;
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(m_pid, &status, WNOHANG);
  } while (waited < 0 && errno == EINTR);
  if (waited == m_pid)
    return false;
  // ECHILD means we no longer own the helper's lifecycle; do not assume it
  // remains alive based on reusable numeric PID alone.
  return waited == 0;
}

bool recoveryWatchdogMode(int argc, char **argv)
{
  return argc >= 2 && std::strcmp(argv[1], "--recovery-watchdog-private") == 0;
}

int runRecoveryWatchdog(int argc, char **argv)
{
  // No Qt, window enumeration, network listener or code-admission side effect
  // may precede or occur inside this private process mode.
  if (argc != 5 || !recoveryWatchdogMode(argc, argv) || !argv[4] || argv[4][0] != '/')
    return 2;
  std::uint64_t pid = 0, birth = 0;
  if (!parseUint(argv[2], pid) || !parseUint(argv[3], birth) || pid > INT32_MAX || !originalParentAlive(pid_t(pid), birth))
    return 2;
  char ready = 'R';
  if (write(childReady, &ready, 1) != 1)
    return 2;
  close(childReady);
  char ignored[64];
  while (true) {
    const auto received = read(childInput, ignored, sizeof(ignored));
    if (received == 0)
      break;
    if (received < 0 && errno == EINTR)
      continue;
    if (received < 0)
      return 3;
    // The parent never needs to send input. Reject unexpected contents so
    // this private pipe is never mistaken for an input/control command stream.
    return 3;
  }
  close(childInput);
  if (!parentEnded(pid_t(pid), birth))
    return 4;
  // --recover rechecks the journal lock and exact pid+birth/CG/AX identities,
  // preserves unresolved entries and never assumes a recovered window by name.
  // Offscreen Qt avoids presenting a new application UI after a host crash.
  if (setenv("QT_QPA_PLATFORM", "offscreen", 1) != 0)
    return 5;
  execl(argv[0], argv[0], "--recover", "--journal", argv[4], static_cast<char *>(nullptr));
  std::perror("Deskflow recovery watchdog exec");
  return 5;
}

} // namespace deskflow::windowshare::native
