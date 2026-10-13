/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ViewerViewportEndpoint.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QKeyEvent>
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
#include <utility>
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
InputCutoffBinding fixtureCutoff()
{
  const auto state = fixtureState();
  return {
      91,
      6,
      {71, 83, 2},
      {{state.session, state.epoch}, 11, state.focusRequest, state.focusRevision, state.geometry, ForwardingScope::All},
      state.connection
  };
}
GnomeViewportRequest drainRequest(std::uint64_t request, std::uint64_t snapshot, std::uint32_t budget = 200)
{
  GnomeViewportRequest result;
  result.operation = GnomeViewportOperation::InputDrain;
  result.request = request;
  result.snapshot = snapshot;
  result.cutoff = fixtureCutoff();
  result.budgetMs = budget;
  return result;
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
void inputDrainProtocol()
{
  const auto request = drainRequest(2, 1);
  const auto json = gnomeViewportJson(request);
  check(parseGnomeViewportRequest(json) == request, "Input drain lost exact request/cutoff correlation");
  for (const bool ok : {false, true}) {
    GnomeViewportReply reply;
    reply.operation = GnomeViewportOperation::InputDrain;
    reply.request = request.request;
    reply.snapshot = request.snapshot;
    reply.cutoff = request.cutoff;
    reply.ok = ok;
    reply.reason = QStringLiteral("fixture-denied");
    const auto encoded = gnomeViewportJson(reply);
    const auto decoded = parseGnomeViewportReply(encoded);
    check(
        decoded.cutoff == request.cutoff && decoded.snapshot == 1 && decoded.request == 2 && decoded.ok == ok,
        "Input drain denial/success did not retain the whole cutoff"
    );
    for (const auto *key : {"cutoff", "snapshot"})
      rejects([&] {
        auto malformed = encoded;
        malformed.remove(QLatin1String(key));
        (void)parseGnomeViewportReply(malformed);
      });
  }
  for (const auto *key :
       {"request", "coreConnectionGeneration", "captureEpoch", "captureSequence", "capturePart", "viewerConnection",
        "session", "epoch", "generation", "focusRequest", "focusRevision", "geometry", "scope"}) {
    rejects([&] {
      auto malformed = json;
      auto cutoff = malformed.value("cutoff").toObject();
      cutoff.remove(QLatin1String(key));
      malformed.insert("cutoff", cutoff);
      (void)parseGnomeViewportRequest(malformed);
    });
    rejects([&] {
      auto malformed = json;
      auto cutoff = malformed.value("cutoff").toObject();
      cutoff.insert(QLatin1String(key), QStringLiteral("0"));
      malformed.insert("cutoff", cutoff);
      (void)parseGnomeViewportRequest(malformed);
    });
  }
  for (const auto &[field, value] : std::vector<std::pair<QString, QJsonValue>>{
           {"scope", "keyboard"},
           {"scope", "unknown"},
           {"request", "091"},
           {"request", 91},
           {"coreConnectionGeneration", "4294967296"},
           {"capturePart", "17"},
           {"generation", "9007199254740992"},
           {"extra", "1"}
       })
    rejects([&] {
      auto malformed = json;
      auto cutoff = malformed.value("cutoff").toObject();
      cutoff.insert(field, value);
      malformed.insert("cutoff", cutoff);
      (void)parseGnomeViewportRequest(malformed);
    });
  for (const QJsonValue &budget : {QJsonValue(0), QJsonValue(251), QJsonValue(1.5), QJsonValue("20")})
    rejects([&] {
      auto malformed = json;
      malformed.insert("budgetMs", budget);
      (void)parseGnomeViewportRequest(malformed);
    });
  auto substitute = request;
  ++substitute.cutoff.capture.sequence;
  check(
      parseGnomeViewportRequest(gnomeViewportJson(substitute)) != request,
      "A different completed Core capture was treated as the same request"
  );
  rejects([&] {
    auto malformed = json;
    malformed.insert("operation", "check");
    (void)parseGnomeViewportRequest(malformed);
  });
  rejects([&] {
    GnomeViewportDecoder decoder;
    auto bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
    bytes.replace("\"capturePart\":\"2\"", "\"capturePart\":\"2\",\"capturePart\":\"2\"");
    decoder.append(rawFrame(bytes));
    (void)decoder.take();
  });
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
void deferredQtQueueCheckpoint()
{
  {
    Pair pair;
    auto state = fixtureState();
    std::function<void(bool)> complete;
    unsigned starts = 0;
    pair.endpoint->onSnapshot = [&] { return std::optional(state); };
    pair.endpoint->onCheck = [&](const auto &expected) { return expected == state; };
    pair.endpoint->onQtQueue = [&](const auto &expected, std::function<void(bool)> callback) {
      check(expected == state, "Qt queue request lost exact painted viewport association");
      ++starts;
      complete = std::move(callback);
      return true;
    };
    pair.send({GnomeViewportOperation::Snapshot, 1});
    check(pair.reply(1).ok, "Qt queue fixture has no snapshot");
    pair.send({GnomeViewportOperation::Check, 2, 1});
    check(waitUntil([&] { return starts == 1; }), "Qt queue callback was not requested");
    pair.drain();
    check(
        std::none_of(pair.replies.begin(), pair.replies.end(), [](const auto &reply) { return reply.request == 2; }),
        "A Qt check replied before two native queue sync callbacks could finish"
    );
    complete(true);
    check(pair.reply(2).ok, "Completed owned Qt observation did not publish one check");
    complete(true);
    pair.drain();
    check(
        std::count_if(pair.replies.begin(), pair.replies.end(), [](const auto &reply) { return reply.request == 2; }) ==
            1,
        "Duplicate Qt completion published another reply"
    );

    pair.send({GnomeViewportOperation::Snapshot, 3});
    check(pair.reply(3).ok, "Replacement Qt queue snapshot was refused");
    pair.send({GnomeViewportOperation::Check, 4, 3});
    check(waitUntil([&] { return starts == 2; }), "Replacement Qt queue was not started");
    ++state.viewportRevision;
    complete(true);
    check(!pair.reply(4).ok, "Qt completion with changed painted geometry escaped final recheck");
  }
  {
    Pair pair;
    auto state = fixtureState();
    std::function<void(bool)> late;
    pair.endpoint->onSnapshot = [&] { return std::optional(state); };
    pair.endpoint->onCheck = [&](const auto &expected) { return expected == state; };
    pair.endpoint->onQtQueue = [&](const auto &, std::function<void(bool)> callback) {
      late = std::move(callback);
      return true;
    };
    pair.send({GnomeViewportOperation::Snapshot, 1});
    check(pair.reply(1).ok, "Invalidation fixture snapshot unavailable");
    pair.send({GnomeViewportOperation::Check, 2, 1});
    check(waitUntil([&] { return bool(late); }), "Deferred Qt check never began");
    pair.endpoint->invalidate(QStringLiteral("window-destroyed"));
    check(!pair.reply(2).ok, "Invalidated Qt observation remained eligible");
    late(true);
    pair.drain();
    check(
        std::count_if(pair.replies.begin(), pair.replies.end(), [](const auto &reply) { return reply.request == 2; }) ==
            1,
        "Late Qt event reused the canceled reply identity"
    );
    check(
        std::any_of(
            pair.replies.begin(), pair.replies.end(),
            [](const auto &reply) {
              return reply.operation == GnomeViewportOperation::Invalidated && reply.snapshot == 1;
            }
        ),
        "Qt invalidation did not also revoke its retained native snapshot"
    );
  }
  {
    Pair pair;
    std::function<void(bool)> late;
    pair.endpoint->onSnapshot = fixtureState;
    pair.endpoint->onCheck = [](const auto &) { return true; };
    pair.endpoint->onQtQueue = [&](const auto &, std::function<void(bool)> callback) {
      late = std::move(callback);
      return true;
    };
    pair.send({GnomeViewportOperation::Snapshot, 1});
    check(pair.reply(1).ok, "Pipelining fixture snapshot unavailable");
    pair.send({GnomeViewportOperation::Check, 2, 1});
    check(waitUntil([&] { return bool(late); }), "Pipelining fixture did not start");
    bool closed = false;
    pair.endpoint->onClosed = [&] { closed = true; };
    pair.send({GnomeViewportOperation::Check, 3, 1});
    check(waitUntil([&] { return closed; }), "Pipelined requests failed to close native private endpoint");
    late(true);
    pair.endpoint.reset();
    late(true); // No borrowed destroyed native/Qt pointers in retained completion.
  }
  {
    Pair pair;
    pair.endpoint->onSnapshot = fixtureState;
    pair.endpoint->onCheck = [](const auto &) { return true; };
    pair.endpoint->onQtQueue = [&pair](const auto &, std::function<void(bool)> finished) {
      return pair.endpoint->startWaylandQueue(nullptr, {}, std::move(finished));
    };
    pair.send({GnomeViewportOperation::Snapshot, 1});
    check(pair.reply(1).ok, "Unavailable-native fixture could not establish snapshot");
    pair.send({GnomeViewportOperation::Check, 2, 1});
    const auto result = pair.reply(2);
    check(
        !result.ok && result.reason == QStringLiteral("qt-wayland-queue-unavailable"),
        "Missing native QtWayland queue was accidentally a success-returning drain"
    );
  }
  {
    Pair pair;
    pair.endpoint->onSnapshot = fixtureState;
    pair.endpoint->onCheck = [](const auto &) { return true; };
    pair.endpoint->onQtQueue = [&](const auto &, std::function<void(bool)> completion) {
      completion(true); // Explicit immediate reentry from a fixture.
      return true;
    };
    pair.send({GnomeViewportOperation::Snapshot, 1});
    check(pair.reply(1).ok, "Reentrant fixture snapshot unavailable");
    pair.send({GnomeViewportOperation::Check, 2, 1});
    check(pair.reply(2).ok, "Synchronous fixture reentry lost its exact request");
    pair.drain();
    check(
        std::count_if(pair.replies.begin(), pair.replies.end(), [](const auto &reply) { return reply.request == 2; }) ==
            1,
        "Synchronous callback and return path sent duplicate Qt receipts"
    );
  }
}
void fencedInputQueue()
{
  {
    Pair pair;
    const auto state = fixtureState();
    const auto cutoff = fixtureCutoff();
    bool fenced = true;
    std::vector<std::function<void(bool)>> completions;
    pair.endpoint->onSnapshot = [state] { return state; };
    pair.endpoint->onCheck = [state](const auto &expected) { return expected == state; };
    pair.endpoint->onInputDrainCheck = [&](const auto &expected, const auto &binding) {
      return expected == state && fenced && binding == cutoff;
    };
    pair.endpoint->onInputDrain = [&](const auto &expected, const auto &binding, QDeadlineTimer deadline, auto done) {
      check(
          expected == state && binding == cutoff && !deadline.hasExpired() && deadline.remainingTime() <= 200,
          "Native input drain did not receive exact binding and inherited budget"
      );
      completions.push_back(std::move(done));
      return true;
    };
    pair.send({GnomeViewportOperation::Snapshot, 1});
    check(pair.reply(1).ok, "Fenced drain snapshot unavailable");
    pair.send(drainRequest(2, 1));
    check(waitUntil([&] { return completions.size() == 1; }), "Fenced drain was not started");
    pair.drain();
    check(
        std::none_of(pair.replies.begin(), pair.replies.end(), [](const auto &r) { return r.request == 2; }),
        "Fenced drain completed before its native queue callbacks"
    );
    completions[0](true);
    check(pair.reply(2).ok && pair.reply(2).cutoff == cutoff, "Fenced drain did not echo its exact cutoff");
    // Resume invalidates even a completed receipt, without a geometry change.
    fenced = false;
    pair.endpoint->invalidate(QStringLiteral("forwarding-fence-changed"));
    pair.drain();
    check(
        std::any_of(
            pair.replies.begin(), pair.replies.end(),
            [](const auto &r) {
              return r.operation == GnomeViewportOperation::Invalidated && r.snapshot == 1 &&
                     r.reason == QStringLiteral("forwarding-fence-changed");
            }
        ),
        "A resumed gate left the completed snapshot receipt live"
    );
    pair.send(drainRequest(3, 1));
    check(!pair.reply(3).ok && pair.reply(3).cutoff == cutoff, "A completed drain revived its revoked snapshot");

    fenced = true;
    pair.send({GnomeViewportOperation::Snapshot, 4});
    check(pair.reply(4).ok, "Fresh fenced snapshot unavailable");
    pair.send(drainRequest(5, 4));
    check(waitUntil([&] { return completions.size() == 2; }), "Fresh fenced drain did not start");
    completions[0](true); // Old callback while a new operation is pending.
    pair.drain();
    check(
        std::none_of(pair.replies.begin(), pair.replies.end(), [](const auto &r) { return r.request == 5; }),
        "An old native completion acknowledged a different cutoff operation"
    );
    fenced = false;
    completions[1](true);
    check(!pair.reply(5).ok, "Final Qt receipt skipped exact fence revalidation");
    completions[1](true);
    pair.drain();
    check(
        std::count_if(pair.replies.begin(), pair.replies.end(), [](const auto &r) { return r.request == 5; }) == 1,
        "Duplicate completion emitted a second fenced receipt"
    );
  }
  for (unsigned variant = 0; variant < 7; ++variant) {
    Pair pair;
    const auto state = fixtureState();
    pair.endpoint->onSnapshot = [state] { return state; };
    pair.endpoint->onCheck = [state](const auto &expected) { return expected == state; };
    bool started = false;
    pair.endpoint->onInputDrainCheck = [](const auto &, const auto &binding) {
      return binding.forwarding == fixtureCutoff().forwarding;
    };
    pair.endpoint->onInputDrain = [&](const auto &, const auto &, QDeadlineTimer, auto) {
      started = true;
      return true;
    };
    pair.send({GnomeViewportOperation::Snapshot, 1});
    check(pair.reply(1).ok, "Mismatch fixture snapshot unavailable");
    auto request = drainRequest(2, 1);
    switch (variant) {
    case 0:
      ++request.cutoff.viewerConnection;
      break;
    case 1:
      ++request.cutoff.forwarding.owner.session;
      break;
    case 2:
      ++request.cutoff.forwarding.owner.epoch;
      break;
    case 3:
      ++request.cutoff.forwarding.focusRequest;
      break;
    case 4:
      ++request.cutoff.forwarding.focusRevision;
      break;
    case 5:
      ++request.cutoff.forwarding.geometry;
      break;
    case 6:
      ++request.cutoff.forwarding.generation;
      break;
    }
    pair.send(request);
    check(
        !pair.reply(2).ok && pair.reply(2).cutoff == request.cutoff && !started,
        "A mismatched All fence or viewer connection reached the native queue"
    );
  }
  {
    Pair pair;
    pair.endpoint->onSnapshot = fixtureState;
    pair.endpoint->onCheck = [](const auto &) { return true; };
    pair.endpoint->onInputDrainCheck = [](const auto &, const auto &) { return true; };
    pair.send({GnomeViewportOperation::Snapshot, 1});
    check(pair.reply(1).ok, "Missing support fixture snapshot unavailable");
    pair.send({GnomeViewportOperation::Check, 2, 1});
    check(pair.reply(2).ok, "Legacy observational Check changed behavior");
    pair.send(drainRequest(3, 1));
    check(
        !pair.reply(3).ok && pair.reply(3).reason == QStringLiteral("qt-input-drain-unavailable"),
        "Missing native support fell back to generic observational Check"
    );
    pair.endpoint->onInputDrain = [&](const auto &, const auto &, QDeadlineTimer deadline, auto done) {
      return pair.endpoint->startWaylandInputDrain(nullptr, {}, deadline, [] { return true; }, std::move(done));
    };
    pair.send(drainRequest(4, 1));
    check(!pair.reply(4).ok, "Unavailable real QtWayland input queue fabricated a drain receipt");
  }
  for (const bool started : {false, true}) {
    Pair pair;
    pair.endpoint->onSnapshot = fixtureState;
    pair.endpoint->onCheck = [](const auto &) { return true; };
    pair.endpoint->onInputDrainCheck = [](const auto &, const auto &) { return true; };
    pair.endpoint->onInputDrain = [started](const auto &, const auto &, QDeadlineTimer, auto done) {
      done(true);
      done(false); // Only the first result belongs to this operation.
      return started;
    };
    pair.send({GnomeViewportOperation::Snapshot, 1});
    check(pair.reply(1).ok, "Synchronous drain snapshot unavailable");
    pair.send(drainRequest(2, 1));
    check(pair.reply(2).ok == started, "A completion bypassed its starter's eventual failure");
  }
  {
    Pair pair;
    std::function<void(bool)> late;
    pair.endpoint->onSnapshot = fixtureState;
    pair.endpoint->onCheck = [](const auto &) { return true; };
    pair.endpoint->onInputDrainCheck = [](const auto &, const auto &) { return true; };
    pair.endpoint->onInputDrain = [&](const auto &, const auto &, QDeadlineTimer deadline, auto done) {
      check(deadline.remainingTime() <= 20, "Endpoint renewed its inherited drain budget");
      late = std::move(done);
      return true;
    };
    pair.send({GnomeViewportOperation::Snapshot, 1});
    check(pair.reply(1).ok, "Budget fixture snapshot unavailable");
    pair.send(drainRequest(2, 1, 20));
    check(!pair.reply(2).ok && bool(late), "A missing native completion exceeded its bounded deadline");
    late(true);
    pair.drain();
    check(
        std::count_if(pair.replies.begin(), pair.replies.end(), [](const auto &r) { return r.request == 2; }) == 1,
        "Late completion revived an expired input drain"
    );
  }
  for (unsigned phase = 0; phase < 3; ++phase) {
    Pair pair;
    std::function<void(bool)> late;
    unsigned checks = 0;
    pair.endpoint->onSnapshot = fixtureState;
    pair.endpoint->onCheck = [](const auto &) { return true; };
    pair.endpoint->onInputDrainCheck = [&](const auto &, const auto &) {
      if (phase == 0 || (phase == 2 && ++checks == 2))
        pair.endpoint.reset();
      return true;
    };
    pair.endpoint->onInputDrain = [&](const auto &, const auto &, QDeadlineTimer, auto done) {
      late = std::move(done);
      if (phase == 1)
        pair.endpoint.reset();
      return true;
    };
    pair.send({GnomeViewportOperation::Snapshot, 1});
    check(pair.reply(1).ok, "Destructive drain snapshot unavailable");
    pair.send(drainRequest(2, 1));
    check(waitUntil([&] { return !pair.endpoint || bool(late); }), "Destructive drain did not start");
    if (late)
      late(true);
    check(!pair.endpoint, "Destructive validation/completion did not run");
    if (late)
      late(true);
  }
}

void nestedInputDispatch(ViewerApplication &app)
{
  class Filter final : public QObject
  {
  public:
    QObject *target = nullptr;
    std::function<void()> nested;
    bool eventFilter(QObject *object, QEvent *event) override
    {
      if (object != target || (event->type() != QEvent::KeyPress && event->type() != QEvent::InputMethodQuery))
        return false;
      nested();
      return true;
    }
  } filter;
  QObject receiver;
  filter.target = &receiver;
  app.installEventFilter(&filter);
  check(app.inputDispatchIdle(), "Input dispatch guard started busy");
  Pair pair;
  pair.endpoint->onSnapshot = fixtureState;
  pair.endpoint->onCheck = [](const auto &) { return true; };
  pair.endpoint->onInputDrainCheck = [&](const auto &, const auto &) { return app.inputDispatchIdle(); };
  unsigned starts = 0;
  std::function<void(bool)> finish;
  pair.endpoint->onInputDrain = [&](const auto &, const auto &, QDeadlineTimer, auto done) {
    ++starts;
    finish = std::move(done);
    return true;
  };
  pair.send({GnomeViewportOperation::Snapshot, 1});
  check(pair.reply(1).ok, "Nested dispatch fixture snapshot unavailable");
  filter.nested = [&] {
    check(!app.inputDispatchIdle(), "Input filter ran before the application's dispatch-depth guard");
    pair.send(drainRequest(2, 1));
    check(
        !pair.reply(2).ok && starts == 0,
        "Nested processEvents admitted a drain while an older input filter remained on stack"
    );
  };
  QKeyEvent event(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
  QCoreApplication::sendEvent(&receiver, &event);
  check(app.inputDispatchIdle(), "Input depth did not unwind after a nested event loop");
  filter.nested = [&] {
    check(!app.inputDispatchIdle(), "Pre-QPA input-method query escaped the dispatch-depth guard");
    pair.send(drainRequest(3, 1));
    check(
        !pair.reply(3).ok && starts == 0, "Input-method reset/query admitted a drain before the older key reached QPA"
    );
  };
  QInputMethodQueryEvent query(Qt::ImEnabled);
  QCoreApplication::sendEvent(&receiver, &query);
  check(app.inputDispatchIdle(), "Input-method query depth did not unwind");
  pair.send(drainRequest(4, 1));
  check(waitUntil([&] { return starts == 1; }), "Fresh drain did not start after input handler unwound");
  filter.nested = [&] {
    finish(true);
    check(!pair.reply(4).ok, "Nested native completion ignored the older input handler still on stack");
  };
  QCoreApplication::sendEvent(&receiver, &event);
  check(app.inputDispatchIdle(), "Completion callback corrupted input dispatch depth");
  pair.send(drainRequest(5, 1));
  check(waitUntil([&] { return starts == 2; }), "Idle drain retry did not begin");
  finish(true);
  check(pair.reply(5).ok, "A fully unwound input dispatch could not finish its exact fixture operation");
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
  qputenv("QT_QPA_PLATFORM", "offscreen");
  ViewerApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  (void)app.enableInputDrainDispatch();
  try {
    protocol();
    inputDrainProtocol();
    endpointLifetime();
    deferredQtQueueCheckpoint();
    fencedInputQueue();
    nestedInputDispatch(app);
    destruction();
    bootstrap();
    QTextStream(stdout) << "PASS: bounded viewport framing/schema, mapping association, invalidation/expiry, monotonic "
                           "requests, async Qt queue checkpoints, exact All cutoff/receipt correlation, "
                           "revoked/completed/late/expired drains, nested input-filter denial, "
                           "unsupported native denial, EOF and destructive callbacks\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
