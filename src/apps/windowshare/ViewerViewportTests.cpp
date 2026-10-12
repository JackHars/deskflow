/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ViewerViewportEndpoint.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointer>
#include <QProcess>
#include <QTextStream>
#include <QThread>
#include <QtEndian>
#include <algorithm>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace deskflow::windowshare;
namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void rejects(F function)
{
  bool rejected = false;
  try {
    function();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "Malformed viewport value was accepted");
}
bool waitUntil(const std::function<bool()> &predicate, int milliseconds = 2000)
{
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return predicate();
}
GnomeViewportState fixtureState()
{
  // Protocol/lifecycle fixture only: this does not manufacture native evidence.
  GnomeViewportState s;
  s.connection = 17;
  s.session = 42;
  s.epoch = 3;
  s.geometry = 2;
  s.focusRequest = 5;
  s.focusRevision = 7;
  s.surfaceIncarnation = 4;
  s.viewportRevision = 8;
  s.peerFingerprint = QString(64, 'a');
  s.nativeSurface = 19;
  s.clientWidth = 400;
  s.clientHeight = 400;
  s.sourceWidth = 640;
  s.sourceHeight = 480;
  s.imageWidth = 64;
  s.imageHeight = 48;
  s.videoY = 50;
  s.videoWidth = 400;
  s.videoHeight = 300;
  s.dpr = 1.25;
  s.surfaceWidth = 508;
  s.surfaceHeight = 532;
  s.contentX = 4;
  s.contentY = 28;
  s.logicalScale = 1.25;
  return s;
}
QByteArray rawFrame(const QByteArray &body)
{
  QByteArray result(4, '\0');
  qToBigEndian<quint32>(body.size(), result.data());
  return result + body;
}
void protocol()
{
  GnomeViewportReply original;
  original.operation = GnomeViewportOperation::Snapshot;
  original.request = 1;
  original.ok = true;
  original.state = fixtureState();
  const auto frame = gnomeViewportFrame(gnomeViewportJson(original));
  GnomeViewportDecoder decoder;
  for (qsizetype i = 0; i < frame.size(); ++i) {
    decoder.append(frame.mid(i, 1));
    if (i + 1 != frame.size())
      check(!decoder.take(), "Partial frame escaped decoder");
  }
  const auto decoded = decoder.take();
  check(
      decoded && parseGnomeViewportReply(*decoded).state == original.state && decoder.empty(),
      "Snapshot roundtrip lost binding"
  );
  decoder.append(frame + frame);
  check(
      decoder.take().has_value() && decoder.take().has_value() && !decoder.take(), "Coalesced frames were not separated"
  );
  rejects([&] {
    auto json = gnomeViewportJson(original);
    json.insert("extra", 1);
    (void)parseGnomeViewportReply(json);
  });
  rejects([&] {
    auto reply = original;
    reply.state.geometry = 0;
    (void)gnomeViewportJson(reply);
  });
  rejects([&] {
    auto reply = original;
    reply.state.videoX = 1;
    (void)gnomeViewportJson(reply);
  });
  rejects([&] {
    auto reply = original;
    reply.state.logicalScale = std::numeric_limits<double>::quiet_NaN();
    (void)gnomeViewportJson(reply);
  });
  rejects([&] { (void)gnomeViewportJson(GnomeViewportRequest{GnomeViewportOperation::Map, 1, 1, 2, 3}); });
  rejects([&] {
    auto json = gnomeViewportJson(GnomeViewportRequest{GnomeViewportOperation::Snapshot, 2});
    json.insert("request", "02");
    (void)parseGnomeViewportRequest(json);
  });
  rejects([&] {
    GnomeViewportDecoder duplicate;
    duplicate.append(rawFrame("{\"version\":1,\"version\":1}"));
    (void)duplicate.take();
  });
  GnomeViewportDecoder oversized;
  QByteArray header(4, '\0');
  qToBigEndian<quint32>(gnomeViewportMaximumPayload + 1, header.data());
  oversized.append(header);
  rejects([&] { (void)oversized.take(); });
  rejects([&] { oversized.append(frame); });
  GnomeViewportDecoder bounded;
  rejects([&] { bounded.append(QByteArray(2 * (gnomeViewportMaximumPayload + 4) + 1, 'x')); });
}
struct Pair
{
  int peer = -1;
  std::unique_ptr<ViewerViewportEndpoint> endpoint;
  GnomeViewportDecoder decoder;
  std::vector<GnomeViewportReply> replies;
  Pair()
  {
    int sockets[2];
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, "socketpair failed");
    peer = sockets[0];
    ::fcntl(peer, F_SETFL, ::fcntl(peer, F_GETFL) | O_NONBLOCK);
    endpoint = std::make_unique<ViewerViewportEndpoint>(sockets[1]);
    check(
        (::fcntl(sockets[1], F_GETFD) & FD_CLOEXEC) && (::fcntl(sockets[1], F_GETFL) & O_NONBLOCK),
        "Endpoint descriptor was not protected"
    );
  }
  ~Pair()
  {
    if (peer >= 0)
      ::close(peer);
  }
  void send(const GnomeViewportRequest &request)
  {
    const auto bytes = gnomeViewportFrame(gnomeViewportJson(request));
    check(::send(peer, bytes.constData(), bytes.size(), 0) == bytes.size(), "Fixture request failed");
  }
  void drain()
  {
    char bytes[4096];
    while (const auto count = ::recv(peer, bytes, sizeof(bytes), 0)) {
      if (count < 0)
        break;
      decoder.append(QByteArray(bytes, count));
      while (auto reply = decoder.take())
        replies.push_back(parseGnomeViewportReply(*reply));
    }
  }
  GnomeViewportReply reply(std::uint64_t request)
  {
    check(
        waitUntil([&] {
          drain();
          return std::any_of(replies.begin(), replies.end(), [=](const auto &r) { return r.request == request; });
        }),
        "Viewport reply timed out"
    );
    for (const auto &r : replies)
      if (r.request == request)
        return r;
    throw std::runtime_error("No reply");
  }
};
void endpointLifetime()
{
  Pair pair;
  auto state = fixtureState();
  pair.endpoint->onSnapshot = [&] { return std::optional(state); };
  pair.endpoint->onCheck = [&](const auto &expected) { return expected == state; };
  pair.endpoint->onMap = [](const auto &, QPointF point) { return std::optional(point / 2); };
  pair.send({GnomeViewportOperation::Snapshot, 1});
  check(pair.reply(1).state == state, "Endpoint snapshot mismatch");
  pair.send({GnomeViewportOperation::Map, 2, 1, 40, 60});
  check(pair.reply(2).ok && pair.reply(2).x == 20 && pair.reply(2).y == 30, "Endpoint did not call exact mapper");
  pair.send({GnomeViewportOperation::Check, 3, 1});
  check(pair.reply(3).ok, "Exact final check failed");
  ++state.viewportRevision;
  pair.endpoint->invalidate();
  pair.send({GnomeViewportOperation::Map, 4, 1, 40, 60});
  check(!pair.reply(4).ok, "Revoked snapshot mapped");
  check(
      std::any_of(
          pair.replies.begin(), pair.replies.end(),
          [](const auto &r) { return r.operation == GnomeViewportOperation::Invalidated && r.snapshot == 1; }
      ),
      "Retained snapshot revocation was not sent"
  );
  pair.send({GnomeViewportOperation::Snapshot, 5});
  check(pair.reply(5).ok, "Fresh snapshot unavailable");
  pair.endpoint->onMap = [&](const auto &, QPointF point) {
    pair.endpoint->invalidate();
    return std::optional(point);
  };
  pair.send({GnomeViewportOperation::Map, 6, 5, 40, 60});
  check(!pair.reply(6).ok, "Reentrant revocation escaped map check");
  pair.send({GnomeViewportOperation::Snapshot, 7});
  check(pair.reply(7).ok, "Expiry fixture snapshot failed");
  check(
      waitUntil([&] {
        pair.drain();
        return std::any_of(pair.replies.begin(), pair.replies.end(), [](const auto &r) {
          return r.operation == GnomeViewportOperation::Invalidated && r.snapshot == 7 &&
                 r.reason == "snapshot-expired";
        });
      }),
      "Retained snapshot did not expire"
  );
  pair.send({GnomeViewportOperation::Check, 8, 7});
  check(!pair.reply(8).ok, "Expired snapshot revived");
  bool closed = false;
  pair.endpoint->onClosed = [&] { closed = true; };
  pair.send({GnomeViewportOperation::Snapshot, 8});
  check(waitUntil([&] { return closed; }), "Repeated request did not close endpoint");
}
void destruction()
{
  for (int phase = 0; phase < 3; ++phase) {
    Pair pair;
    pair.endpoint->onSnapshot = [&]() -> std::optional<GnomeViewportState> {
      if (phase == 0)
        pair.endpoint.reset();
      return fixtureState();
    };
    pair.endpoint->onCheck = [&](const auto &) {
      if (phase == 1)
        pair.endpoint.reset();
      return true;
    };
    pair.endpoint->onClosed = [&] { pair.endpoint.reset(); };
    if (phase == 2) {
      ::close(pair.peer);
      pair.peer = -1;
    } else
      pair.send({GnomeViewportOperation::Snapshot, 1});
    check(waitUntil([&] { return !pair.endpoint; }), "Destructive callback did not complete safely");
  }
  Pair pair;
  bool closed = false;
  pair.endpoint->onClosed = [&] { closed = true; };
  const char truncated[] = {0, 0};
  check(::send(pair.peer, truncated, sizeof(truncated), 0) == sizeof(truncated), "Partial EOF setup failed");
  ::close(pair.peer);
  pair.peer = -1;
  check(waitUntil([&] { return closed; }), "Partial frame EOF did not close endpoint");
  Pair throwing;
  throwing.endpoint->onClosed = [] { throw std::runtime_error("Fixture lifecycle observer failed"); };
  throwing.endpoint->close();
  check(!throwing.endpoint->live(), "Throwing lifecycle observer reopened endpoint");
}

int bootstrapChild(int argc, char **argv)
{
  try {
    const bool absent = QByteArray(argv[1]) == "--bootstrap-fixture=absent";
    const auto before = ::fcntl(4, F_GETFD);
    auto bootstrap = ViewerViewportBootstrap::inspect(argc, argv);
    check(bootstrap.inherited() != absent, "Wrong inherited descriptor result");
    if (absent) {
      check(::fcntl(4, F_GETFD) == before, "Ordinary mode changed unrelated descriptor");
      return 0;
    }
    check(
        (::fcntl(4, F_GETFD) & FD_CLOEXEC) && (::fcntl(4, F_GETFL) & O_NONBLOCK), "Bootstrap did not protect descriptor"
    );
    const QStringList valid{"gnome-viewport-fd", "enable-seamless", "headless", "connect", "identity", "peer-pin"};
    bootstrap.validateOptions(valid);
    for (const auto &forbidden : {"listen", "help", "init-identity", "core-input-fd", "gnome-viewport-fd"})
      rejects([&] { bootstrap.validateOptions(valid + QStringList{forbidden}); });
    auto incomplete = valid;
    incomplete.removeAll("headless");
    rejects([&] { bootstrap.validateOptions(incomplete); });
    const int descriptor = bootstrap.takeDescriptor();
    check(descriptor == 4 && !bootstrap.inherited(), "Bootstrap did not transfer ownership");
    ::close(descriptor);
    return 0;
  } catch (...) {
    return 1;
  }
}

void bootstrap()
{
  const auto run = [](int descriptor, const QStringList &arguments) {
    const int childCopy = ::fcntl(descriptor, F_DUPFD_CLOEXEC, 10);
    check(childCopy >= 0, "Could not prepare private inherited descriptor");
    QProcess child;
    child.setChildProcessModifier([childCopy] {
      if (::dup2(childCopy, 4) != 4 || ::fcntl(4, F_SETFD, 0))
        ::_exit(90);
    });
    child.start(QCoreApplication::applicationFilePath(), arguments);
    const bool started = child.waitForStarted(2000);
    ::close(childCopy);
    const bool finished = started && child.waitForFinished(3000);
    if (!finished) {
      child.kill();
      child.waitForFinished(1000);
    }
    check(started && finished && child.exitStatus() == QProcess::NormalExit, "Bootstrap child did not exit normally");
    return child.exitCode();
  };
  const QStringList canonical{"--bootstrap-fixture=valid", "--gnome-viewport-fd=4"};
  int sockets[2];
  check(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, "Bootstrap socketpair failed");
  check(run(sockets[1], canonical) == 0, "Valid connected inherited stream was rejected");
  check(run(sockets[1], {"--bootstrap-fixture=absent"}) == 0, "Ordinary startup modified descriptor4");
  for (const QStringList &args :
       {QStringList{"--bootstrap-fixture=valid", "--gnome-viewport-fd", "4"},
        QStringList{"--bootstrap-fixture=valid", "--gnome-viewport-fd=04"},
        QStringList{"--bootstrap-fixture=valid", "-gnome-viewport-fd=4"},
        canonical + QStringList{"--gnome-viewport-fd=4"}})
    check(run(sockets[1], args) != 0, "Noncanonical inherited option was accepted");
  ::close(sockets[0]);
  ::close(sockets[1]);
  check(::socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0, "Bootstrap datagram fixture failed");
  check(run(sockets[1], canonical) != 0, "Inherited datagram was accepted");
  ::close(sockets[0]);
  ::close(sockets[1]);
  check(::pipe(sockets) == 0, "Bootstrap pipe fixture failed");
  check(run(sockets[1], canonical) != 0, "Inherited pipe was accepted");
  ::close(sockets[0]);
  ::close(sockets[1]);
  const int unconnected = ::socket(AF_UNIX, SOCK_STREAM, 0);
  check(unconnected >= 0, "Bootstrap unconnected fixture failed");
  check(run(unconnected, canonical) != 0, "Unconnected inherited stream was accepted");
  ::close(unconnected);
}
} // namespace
int main(int argc, char **argv)
{
  if (argc > 1 && QByteArray(argv[1]).startsWith("--bootstrap-fixture="))
    return bootstrapChild(argc, argv);
  QCoreApplication app(argc, argv);
  try {
    protocol();
    endpointLifetime();
    destruction();
    bootstrap();
    QTextStream(stdout) << "PASS: bounded viewport framing/schema, mapping association, invalidation/expiry, monotonic "
                           "requests, EOF and destructive callbacks\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
