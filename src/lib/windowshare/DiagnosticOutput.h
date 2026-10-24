/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <string_view>
#include <unistd.h>

namespace deskflow::windowshare {
// Owned by the diagnostic worker. A raw descriptor avoids holding stderr's
// FILE lock while blocked, including during bounded shutdown. Duplicating
// does not change the original descriptor's blocking mode or lifetime.
class DiagnosticOutput final
{
public:
  explicit DiagnosticOutput(int descriptor) : m_descriptor(duplicate(descriptor))
  {
  }
  ~DiagnosticOutput()
  {
    if (m_descriptor >= 0)
      close(m_descriptor);
  }
  DiagnosticOutput(const DiagnosticOutput &) = delete;
  DiagnosticOutput &operator=(const DiagnosticOutput &) = delete;

  // Call only from the dedicated worker. Linux directs pipe signals to this
  // thread; Darwin can signal another thread and needs the descriptor option
  // installed below. No process-wide signal handler is changed.
  bool write(std::string_view line) const
  {
    if (m_descriptor < 0)
      return false;
#ifndef __APPLE__
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &blocked, nullptr) != 0)
      return false;
#endif
    while (!line.empty()) {
      const auto count = ::write(m_descriptor, line.data(), line.size());
      if (count < 0 && errno == EINTR)
        continue;
      if (count <= 0)
        return false;
      line.remove_prefix(static_cast<std::size_t>(count));
    }
    return true;
  }

private:
  static int duplicate(int descriptor)
  {
    const int owned = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
#ifdef __APPLE__
    // Darwin shares this no-SIGPIPE option with the original stderr file
    // description. It suppresses only closed-pipe termination on that output;
    // blocking mode and other process descriptors/handlers are untouched.
    if (owned >= 0 && fcntl(owned, F_SETNOSIGPIPE, 1) != 0) {
      close(owned);
      return -1;
    }
#endif
    return owned;
  }
  const int m_descriptor;
};
} // namespace deskflow::windowshare
#endif
