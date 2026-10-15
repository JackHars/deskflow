/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
// Synthetic media-only workload using the production TLS channel and Viewer.
// Always offscreen. No NativeHost, input injection or real source is created.
#include "Transport.h"
#include "VideoDecoder.h"
#include "Viewer.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTextStream>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace deskflow::windowshare;
namespace {
void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
class Listener : public QTcpServer
{
public:
  std::function<void(qintptr)> incoming;
  void incomingConnection(qintptr fd) override
  {
    incoming(fd);
  }
};
void waitFor(const std::function<bool()> &done, QString &error, int milliseconds = 10000)
{
  QEventLoop loop;
  QTimer poll, timeout;
  poll.setInterval(2);
  timeout.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (done() || !error.isEmpty())
      loop.quit();
  });
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start();
  timeout.start(milliseconds);
  if (!done() && error.isEmpty())
    loop.exec();
  if (!error.isEmpty())
    throw std::runtime_error(error.toStdString());
  require(done(), "Media workload timed out");
}
QJsonObject metrics(const MediaMetrics &m)
{
  QJsonArray histogram;
  for (auto count : m.receiveToPaint)
    histogram.append(qint64(count));
  const auto percentile = [&](unsigned percent) -> QJsonValue {
    const auto ns = m.percentileUpperNs(percent);
    return ns ? QJsonValue(double(*ns) / 1000000.) : QJsonValue(QJsonValue::Null);
  };
  return {
      {"received", qint64(m.received)},
      {"admitted", qint64(m.admitted)},
      {"retired", qint64(m.retired)},
      {"ignored", qint64(m.ignored)},
      {"decoded", qint64(m.decoded)},
      {"painted", qint64(m.painted)},
      {"decodeErrors", qint64(m.decodeErrors)},
      {"discardedCompletions", qint64(m.discardedCompletions)},
      {"emptyDecodes", qint64(m.emptyDecodes)},
      {"hardwareDecoded", qint64(m.hardwareDecoded)},
      {"reusedHardwareTransfers", qint64(m.reusedHardwareTransfers)},
      {"replacedBeforePaint", qint64(m.replacedBeforePaint)},
      {"overflows", qint64(m.overflows)},
      {"recoveryRequests", qint64(m.recoveryRequests)},
      {"pendingFrames", qint64(m.pendingFrames)},
      {"pendingBytes", qint64(m.pendingBytes)},
      {"peakFrames", qint64(m.peakFrames)},
      {"peakBytes", qint64(m.peakBytes)},
      {"frameCapacity", qint64(m.frameCapacity)},
      {"byteCapacity", qint64(m.byteCapacity)},
      {"timedRetirements", qint64(m.timedRetirements)},
      {"invalidTimings", qint64(m.invalidTimings)},
      {"decodeQueueMaxMs", double(m.maxDecodeQueueNs) / 1000000.},
      {"decodeWorkMaxMs", double(m.maxDecodeWorkNs) / 1000000.},
      {"completionQueueMaxMs", double(m.maxCompletionQueueNs) / 1000000.},
      {"receiveToQtPaintMaxMs", double(m.maxReceiveToPaintNs) / 1000000.},
      {"receiveToQtPaintP95UpperMs", percentile(95)},
      {"receiveToQtPaintP99UpperMs", percentile(99)},
      {"receiveToQtPaintHistogram", histogram}
  };
}
int bounded(const QCommandLineParser &parser, const QString &name, int low, int high)
{
  bool ok = false;
  const auto value = parser.value(name).toInt(&ok);
  require(ok && value >= low && value <= high, "Workload option outside its supported bounds");
  return value;
}
} // namespace

int main(int argc, char **argv)
{
  // Do not allow Qt's -platform argument or ambient display settings to turn
  // an automated workload into visible windows on the user's desktop.
  QStringList arguments;
  for (int i = 0; i < argc; ++i)
    arguments.append(QString::fromLocal8Bit(argv[i]));
  qputenv("QT_QPA_PLATFORM", "offscreen");
  int qtArgc = 1;
  QApplication app(qtArgc, argv);
  app.setQuitOnLastWindowClosed(false);
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOptions(
      {{"duration", "Measured workload seconds (2..3660).", "seconds", "3"},
       {"streams", "Concurrent independent Viewers (2..16).", "count", "4"},
       {"fps", "Offered frames/second/stream (1..60).", "rate", "30"},
       {"scenario", "steady or mixed (decoder burst injection and one hidden stream).", "name", "mixed"},
       {"frame", "Optional single independent Annex B IDR; default 64x48 test image.", "path"},
       {"progress", "Publish warm-up completion and bounded recovery diagnostics on stderr."}}
  );
  if (!parser.parse(arguments)) {
    QTextStream(stderr) << parser.errorText() << '\n';
    return 2;
  }
  if (parser.isSet("help")) {
    QTextStream(stdout) << parser.helpText();
    return 0;
  }
  QJsonObject report{
      {"format", "deskflow-media-load-v1"},
      {"status", "FAIL"},
      {"scope", "synthetic-loopback-tls-decode-offscreen-qt-paint"},
      {"h11_complete", false},
      {"performanceAcceptance", "NOT_RUN"},
      {"physicalInput", false},
      {"nativeHostStarted", false}
  };
  try {
    require(QGuiApplication::platformName() == "offscreen", "Offscreen backend is required");
    require(parser.positionalArguments().isEmpty(), "Unexpected positional arguments");
    const auto seconds = bounded(parser, "duration", 2, 3660);
    const auto streams = bounded(parser, "streams", 2, 16);
    const auto fps = bounded(parser, "fps", 1, 60);
    const auto scenario = parser.value("scenario");
    require(scenario == "steady" || scenario == "mixed", "Unknown media workload scenario");
    QByteArray frame = QByteArray::fromBase64(
        "AAAAAWdCwAraEewEQAAAAwBAAAAPI8SJqAAAAAFozg/IAAAAAWWIhDoRigACGPHAAED2OAAIeUnJyddddddddeA="
    );
    if (parser.isSet("frame")) {
      QFile file(parser.value("frame"));
      const QFileInfo info(file);
      require(
          info.isFile() && !info.isSymLink() && info.size() > 0 && info.size() <= maxPayload,
          "Frame must be a bounded regular non-symlink file"
      );
      require(file.open(QIODevice::ReadOnly), "Cannot open media frame");
      frame = file.read(maxPayload + 1);
    }
    require(
        frame.size() <= maxPayload &&
            decoderSyncAccessUnit(
                std::span(reinterpret_cast<const std::uint8_t *>(frame.constData()), std::size_t(frame.size()))
            ),
        "Workload requires an independent SPS/PPS/IDR frame"
    );
    VideoDecoder probe(false);
    const auto decoded = probe.decode(frame);
    require(
        decoded.error.isEmpty() && !decoded.image.isNull() && decoded.image.width() <= 3840 &&
            decoded.image.height() <= 2160,
        "Cannot decode workload frame within 3840x2160 limit"
    );
    report.insert(
        "plan",
        QJsonObject{
            {"durationSeconds", seconds},
            {"streams", streams},
            {"fps", fps},
            {"scenario", scenario},
            {"width", decoded.image.width()},
            {"height", decoded.image.height()},
            {"frameBytes", frame.size()},
            {"frameSha256", QString::fromLatin1(QCryptographicHash::hash(frame, QCryptographicHash::Sha256).toHex())},
            {"latencyBoundary", "receiveFrame-to-completed-Qt-paint"},
            {"warmupIncludedInCounters", true}
        }
    );
    QTemporaryDir identities;
    require(identities.isValid(), "Cannot create temporary identities");
    initializeIdentity(identities.filePath("host"));
    initializeIdentity(identities.filePath("viewer"));
    const auto hostIdentity = Identity::load(identities.filePath("host"));
    const auto viewerIdentity = Identity::load(identities.filePath("viewer"));
    std::vector<std::unique_ptr<Viewer>> viewers;
    std::vector<std::uint64_t> sent(streams), received(streams);
    std::vector<std::uint64_t> offered(streams), replaced(streams), injected(streams);
    std::vector<std::uint64_t> observedOverflows(streams), observedRecoveries(streams);
    std::vector<bool> pending(streams);
    QElapsedTimer clock;
    QJsonArray recoveryEvents;
    std::uint64_t omittedRecoveryEvents = 0;
    constexpr int recoveryEventCapacity = 64;
    const auto observeRecovery = [&](int index) {
      const auto m = *viewers[index]->mediaMetrics();
      if (m.overflows == observedOverflows[index] && m.recoveryRequests == observedRecoveries[index])
        return;
      observedOverflows[index] = m.overflows;
      observedRecoveries[index] = m.recoveryRequests;
      if (recoveryEvents.size() >= recoveryEventCapacity) {
        ++omittedRecoveryEvents;
        return;
      }
      const QJsonObject event{
          {"event", "media-recovery"},
          {"stream", index + 1},
          {"elapsedSeconds",
           clock.isValid() ? QJsonValue(double(clock.nsecsElapsed()) / 1000000000.) : QJsonValue(QJsonValue::Null)},
          {"overflows", qint64(m.overflows)},
          {"recoveryRequests", qint64(m.recoveryRequests)},
          {"pendingFrames", qint64(m.pendingFrames)},
          {"decodeQueueMaxMs", double(m.maxDecodeQueueNs) / 1000000.},
          {"decodeWorkMaxMs", double(m.maxDecodeWorkNs) / 1000000.},
          {"completionQueueMaxMs", double(m.maxCompletionQueueNs) / 1000000.},
          {"received", qint64(m.received)}
      };
      recoveryEvents.append(event);
      if (parser.isSet("progress"))
        QTextStream(stderr) << QJsonDocument(event).toJson(QJsonDocument::Compact) << '\n' << Qt::flush;
    };
    Channel host(hostIdentity, parseFingerprint(viewerIdentity.fingerprint()));
    Channel peer(viewerIdentity, parseFingerprint(hostIdentity.fingerprint()));
    Listener listener;
    QString error;
    const auto recordError = [&](QString reason) {
      if (error.isEmpty())
        error = reason;
    };
    const auto cleanup = qScopeGuard([&] {
      report.insert("recoveryEvents", recoveryEvents);
      report.insert("recoveryEventCapacity", recoveryEventCapacity);
      report.insert("omittedRecoveryEvents", qint64(omittedRecoveryEvents));
      if (clock.isValid())
        report.insert("elapsedSecondsAtExit", double(clock.nsecsElapsed()) / 1000000000.);
      if (!report.contains("streams")) {
        QJsonArray partial;
        for (const auto &viewer : viewers)
          if (const auto snapshot = viewer->mediaMetrics())
            partial.append(metrics(*snapshot));
        report.insert("partialStreams", partial);
      }
      for (auto &viewer : viewers) {
        viewer->onSend = {};
        viewer->onError = {};
        viewer->disconnected();
      }
      viewers.clear();
    });
    host.onClosed = [&](QString reason, CloseCause) { recordError(reason); };
    peer.onClosed = host.onClosed;
    listener.incoming = [&](qintptr fd) { host.accept(fd); };
    require(listener.listen(QHostAddress::LocalHost, 0), "Cannot listen on loopback");
    peer.connectTo("127.0.0.1", listener.serverPort());
    waitFor([&] { return host.authenticated() && peer.authenticated(); }, error);
    const QJsonObject metadata{
        {"geometry", 1}, {"width", decoded.image.width()}, {"height", decoded.image.height()}, {"codec", "h264"}
    };
    for (int i = 0; i < streams; ++i) {
      auto viewer = std::make_unique<Viewer>(i + 1, 1, metadata);
      require(viewer->enableMediaMetrics(), "Cannot establish empty media metrics baseline");
      viewer->setMediaRecoveryEnabled(true);
      viewer->onSend = [&](auto type, const auto &object, auto session, auto epoch) {
        return peer.send(type, object, session, epoch);
      };
      viewer->onError = recordError;
      viewer->show();
      viewers.push_back(std::move(viewer));
    }
    const auto offer = [&](int index) {
      ++offered[index];
      if (pending[index])
        ++replaced[index];
      pending[index] = true;
    };
    // The production channel admits only one access unit until its TLS output
    // drains. Retain at most one fixture IDR per stream and report replacements
    // instead of bypassing that backpressure or building an unbounded sender.
    QTimer sender;
    sender.setInterval(1);
    int next = 0;
    QObject::connect(&sender, &QTimer::timeout, &app, [&] {
      if (!host.readyForFrame())
        return;
      for (int offset = 0; offset < streams; ++offset) {
        const auto index = (next + offset) % streams;
        if (!pending[index])
          continue;
        if (host.sendFrame(index + 1, 1, 1, frame)) {
          pending[index] = false;
          ++sent[index];
          next = (index + 1) % streams;
        }
        return;
      }
    });
    sender.start();
    host.onPacket = [&](Packet packet) {
      require(
          packet.session > 0 && packet.session <= std::uint64_t(streams) && packet.epoch == 1,
          "Unexpected media workload session"
      );
      if (packet.type == MessageType::Ready)
        viewers[packet.session - 1]->committed();
      else if (packet.type == MessageType::Input && parseObject(packet).value("kind") == "video-recovery") {
        observeRecovery(int(packet.session - 1));
        offer(int(packet.session - 1));
      } else if (packet.type == MessageType::Input && parseObject(packet).value("kind") == "focus") {
        // The fixture never grants input authority. Explicitly reject Qt's
        // automatic activation request so a media-only run cannot time out
        // waiting for a NativeHost that intentionally does not exist.
        const auto request = parseObject(packet).value("request").toInteger();
        viewers[packet.session - 1]->focusAcknowledged(0, std::uint64_t(request));
      } else if (packet.type == MessageType::Return)
        recordError("Viewer returned during media workload");
    };
    peer.onPacket = [&](Packet packet) {
      require(
          packet.type == MessageType::Frame && packet.session > 0 && packet.session <= std::uint64_t(streams) &&
              packet.epoch == 1,
          "Unexpected workload packet"
      );
      auto payload = parseFramePayload(packet);
      ++received[packet.session - 1];
      viewers[packet.session - 1]->receiveFrame(payload.accessUnit, payload.geometry);
      observeRecovery(int(packet.session - 1));
    };
    for (int i = 0; i < streams; ++i)
      offer(i);
    waitFor(
        [&] {
          return std::all_of(viewers.begin(), viewers.end(), [](const auto &v) { return v->firstFramePainted(); });
        },
        error
    );
    clock.start();
    if (parser.isSet("progress"))
      QTextStream(stderr) << "{\"event\":\"workload-started\",\"durationSeconds\":" << seconds << "}\n" << Qt::flush;
    std::vector<std::uint64_t> paintedBefore;
    for (const auto &viewer : viewers)
      paintedBefore.push_back(viewer->mediaMetrics()->painted);
    std::uint64_t slots = 0, missedSlots = 0, hiddenReceived = 0, hiddenPaints = 0;
    bool burst = false, hidden = false, shown = false;
    QTimer workload;
    workload.setInterval(2);
    QObject::connect(&workload, &QTimer::timeout, &app, [&] {
      const auto elapsed = clock.nsecsElapsed();
      const auto due = std::uint64_t(elapsed) * fps / 1000000000ULL;
      if (elapsed >= std::int64_t(seconds) * 1000000000LL)
        return;
      if (scenario == "mixed" && !burst && elapsed >= std::int64_t(seconds) * 250000000LL) {
        burst = true;
        // Explicit fault injection through the real receiveFrame entry point.
        // This bypasses only the transport, and is counted separately from
        // received TLS frames; no claim of a real network burst is made.
        for (int i = 0; i < 64; ++i) {
          ++injected[0];
          viewers[0]->receiveFrame(frame, 1);
        }
        observeRecovery(0);
      }
      if (scenario == "mixed" && !hidden && elapsed >= std::int64_t(seconds) * 500000000LL) {
        hidden = true;
        viewers.back()->hide();
        hiddenReceived = viewers.back()->mediaMetrics()->ignored;
        hiddenPaints = viewers.back()->mediaMetrics()->painted;
        offer(streams - 1);
      }
      if (hidden && !shown && elapsed >= std::int64_t(seconds) * 750000000LL) {
        shown = true;
        const auto observed = *viewers.back()->mediaMetrics();
        if (observed.ignored <= hiddenReceived || observed.painted != hiddenPaints)
          recordError("Hidden Viewer did not pause decoding and painting");
        viewers.back()->show();
      }
      if (due > slots) {
        missedSlots += due - slots - 1;
        slots = due;
        // Rotate the first stream instead of giving one a permanent priority.
        for (int offset = 0; offset < streams; ++offset)
          offer(int((slots + offset) % streams));
      }
    });
    workload.start();
    waitFor([&] { return clock.elapsed() >= seconds * 1000; }, error, seconds * 1000 + 5000);
    workload.stop();
    const auto workloadNs = clock.nsecsElapsed();
    std::vector<std::uint64_t> paintedDuring;
    for (int i = 0; i < streams; ++i)
      paintedDuring.push_back(viewers[i]->mediaMetrics()->painted - paintedBefore[i]);
    waitFor(
        [&] {
          for (int i = 0; i < streams; ++i)
            if (pending[i] || received[i] != sent[i] || viewers[i]->mediaMetrics()->pendingFrames ||
                viewers[i]->videoRecovering())
              return false;
          return true;
        },
        error
    );
    // Finish a possible scheduled paint after the final worker completion.
    for (auto &viewer : viewers)
      viewer->repaint();
    QJsonArray results;
    std::vector<MediaMetrics> finalMetrics;
    for (int i = 0; i < streams; ++i) {
      const auto m = *viewers[i]->mediaMetrics();
      finalMetrics.push_back(m);
      auto entry = metrics(m);
      entry.insert("stream", i + 1);
      entry.insert("sourceOffers", qint64(offered[i]));
      entry.insert("sourceIdrReplacements", qint64(replaced[i]));
      entry.insert("tlsFrames", qint64(sent[i]));
      entry.insert("injectedDecoderBurstFrames", qint64(injected[i]));
      entry.insert("workloadPaintedFrames", qint64(paintedDuring[i]));
      entry.insert("workloadQtPaintRateHz", double(paintedDuring[i]) * 1000000000. / workloadNs);
      results.append(entry);
    }
    // Preserve the complete measured interval and every stream even when an
    // invariant fails. A failed long run must remain diagnosable without a retry.
    report.insert("streams", results);
    report.insert("workloadSeconds", double(workloadNs) / 1000000000.);
    report.insert("drainSeconds", double(clock.nsecsElapsed() - workloadNs) / 1000000000.);
    report.insert("missedOfferSlots", qint64(missedSlots));
    for (int i = 0; i < streams; ++i) {
      const auto &m = finalMetrics[i];
      require(
          m.received == received[i] + injected[i] && m.received == m.admitted + m.ignored && m.admitted == m.retired &&
              m.retired == m.decoded + m.decodeErrors + m.discardedCompletions + m.emptyDecodes && !m.pendingFrames &&
              !m.pendingBytes && m.peakFrames <= m.frameCapacity && m.peakBytes <= m.byteCapacity && !m.decodeErrors &&
              m.painted > 1 && !m.invalidTimings && m.timedRetirements == m.retired &&
              m.reusedHardwareTransfers <= m.hardwareDecoded && m.hardwareDecoded <= m.decoded,
          "Media accounting, capacity, decode or stream progress check failed"
      );
      if (scenario == "steady")
        require(!m.overflows && !m.recoveryRequests, "Steady workload required media recovery");
      if (scenario == "mixed" && i == 0)
        require(m.overflows > 0 && m.recoveryRequests > 0, "Burst did not exercise bounded media recovery");
      if (scenario == "mixed" && i == streams - 1)
        require(
            m.ignored > 0 && m.recoveryRequests > 0 && m.painted > hiddenPaints,
            "Hidden Viewer did not resume with a fresh painted frame"
        );
      viewers[i]->disconnected();
      viewers[i]->onSend = {};
    }
    sender.stop();
    report.insert("status", "PASS");
    // These are workload invariants, not FPS, fairness or H11 latency acceptance.
    report.insert("performanceAcceptance", "NOT_RUN");
    host.onClosed = {};
    peer.onClosed = {};
  } catch (const std::exception &error) {
    report.insert("error", QString::fromUtf8(error.what()));
  }
  QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << '\n';
  return report.value("status") == "PASS" ? 0 : 1;
}
