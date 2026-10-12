/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "HoldingDisplay.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

// This executable is the only process allowed to own the private display.
// WindowServer removes it on process exit even on systems where releasing the
// CGVirtualDisplay object alone leaves the display alive. An owned stdin pipe
// also gives crash cleanup without a daemon, inherited socket or persistent job.
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  struct stat input{}, output{};
  if (args.size() != 4 || args[1] != QStringLiteral("--owned-stdio") || fstat(STDIN_FILENO, &input) != 0 ||
      !S_ISFIFO(input.st_mode) || fstat(STDOUT_FILENO, &output) != 0 || !S_ISFIFO(output.st_mode)) {
    std::fprintf(stderr, "This helper requires the host's owned stdin/stdout pipes.\n");
    return 2;
  }
  const int flags = fcntl(STDIN_FILENO, F_GETFL);
  if (flags < 0 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) != 0) {
    return 2;
  }
  QSocketNotifier inputReady(STDIN_FILENO, QSocketNotifier::Read, &app);
  QObject::connect(&inputReady, &QSocketNotifier::activated, &app, [&] {
    char buffer[64];
    const auto count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
    if (count >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
      app.quit();
    }
  });
  bool widthValid = false, heightValid = false;
  const unsigned width = args[2].toUInt(&widthValid), height = args[3].toUInt(&heightValid);
  deskflow::windowshare::HoldingDisplay display;
  QString reason;
  if (!widthValid || !heightValid || !display.create(&reason, width, height)) {
    const auto response =
        QJsonDocument(QJsonObject{{"state", "error"}, {"error", reason}}).toJson(QJsonDocument::Compact) + '\n';
    ::write(STDOUT_FILENO, response.constData(), static_cast<std::size_t>(response.size()));
    return 1;
  }
  // Parent may have exited while WindowServer was creating the display. Do not
  // leave an orphan waiting for a notifier after its owning pipe already closed.
  pollfd parent{STDIN_FILENO, POLLIN | POLLHUP | POLLERR, 0};
  if (poll(&parent, 1, 0) > 0 && (parent.revents & (POLLHUP | POLLERR))) {
    return 0;
  }
  const auto response = QJsonDocument(
                            QJsonObject{{"state", "ready"}, {"displayId", qint64(display.displayId())}}
                        ).toJson(QJsonDocument::Compact) +
                        '\n';
  if (::write(STDOUT_FILENO, response.constData(), static_cast<std::size_t>(response.size())) != response.size()) {
    return 1;
  }
  const int result = app.exec();
  display.shutdown();
  return result;
}
