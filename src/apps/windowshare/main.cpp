/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeHost.h"
#include "Transport.h"
#include "Viewer.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QLockFile>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QVBoxLayout>
#include <cmath>
#include <csignal>
#include <map>
#include <memory>

using namespace deskflow::windowshare;
namespace {
#ifndef WINDOWSHARE_BRIDGE_TEST
volatile std::sig_atomic_t interrupted = 0;
void interruptHandler(int)
{
  interrupted = 1;
}
#endif
void require(bool value, const char *error)
{
  if (!value)
    throw std::runtime_error(error);
}
std::uint64_t integer(const QJsonObject &object, const char *name, std::uint64_t maximum = 9007199254740991ULL)
{
  const auto value = object.value(QLatin1String(name));
  require(value.isDouble(), "Missing integer field");
  const double number = value.toDouble();
  require(
      std::isfinite(number) && number >= 0 && number <= static_cast<double>(maximum) && std::floor(number) == number,
      "Invalid integer field"
  );
  return static_cast<std::uint64_t>(number);
}
QJsonValue number(std::uint64_t value)
{
  return QJsonValue(static_cast<qint64>(value));
}
constexpr qsizetype maxCatalogWindows = 512;
QString displayTitle(QString title)
{
  title = title.left(512);
  title.remove(QChar('\0'));
  return title;
}
QJsonObject windowMetadataForWire(QJsonObject metadata)
{
  // Preserve NativeHost's exact identity/title for AX matching and recovery.
  metadata.insert("title", displayTitle(metadata.value("title").toString()));
  return metadata;
}
QJsonArray catalogForWire(const QVector<Candidate> &windows)
{
  QJsonArray catalog;
  qsizetype bytes = QJsonDocument(QJsonObject{{"windows", catalog}}).toJson(QJsonDocument::Compact).size();
  for (const auto &window : windows) {
    if (catalog.size() == maxCatalogWindows)
      break;
    const QJsonObject item{{"id", static_cast<qint64>(window.id)}, {"title", displayTitle(window.title)}};
    const auto itemBytes = QJsonDocument(item).toJson(QJsonDocument::Compact).size() + (catalog.isEmpty() ? 0 : 1);
    if (bytes + itemBytes >= maxControlPayload)
      break;
    bytes += itemBytes;
    catalog.append(item);
  }
  return catalog;
}

class Listener final : public QTcpServer
{
public:
  std::function<void(qintptr)> incoming;

private:
  void incomingConnection(qintptr descriptor) override
  {
    if (incoming)
      incoming(descriptor);
  }
};

class Bridge final : public QWidget
{
  friend struct BridgeTestAccess;

public:
  Bridge(
      bool hosting, Identity identity, QByteArray pin, QString journal, int screen, std::uint32_t initialWindow,
      bool holding
  )
      : m_hosting(hosting),
        m_identity(std::move(identity)),
        m_pin(std::move(pin)),
        m_screen(screen),
        m_initialWindow(initialWindow)
  {
    setWindowTitle(hosting ? "Deskflow Window Share — Mac host" : "Deskflow Window Share — viewer");
    auto *layout = new QVBoxLayout(this);
    auto *explanation = new QLabel(
        hosting
            ? "Share selected app windows with your paired computer.\nEmergency restore: Control–Option–Shift–Escape."
            : "Select a Mac window to open here. Closing its viewer returns it to the Mac.",
        this
    );
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    m_status = new QLabel("Disconnected", this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    m_catalog = new QListWidget(this);
    layout->addWidget(m_catalog);
    auto *open = new QPushButton(hosting ? "Share selected window" : "Open selected window", this);
    auto *refresh = new QPushButton("Refresh windows", this);
    auto *restore = new QPushButton("Return all windows to Mac", this);
    layout->addWidget(open);
    layout->addWidget(refresh);
    layout->addWidget(restore);
    resize(480, 440);
    connect(open, &QPushButton::clicked, this, [this] { openSelected(); });
    connect(m_catalog, &QListWidget::itemDoubleClicked, this, [this] { openSelected(); });
    connect(refresh, &QPushButton::clicked, this, [this] {
      if (m_native)
        m_native->refresh();
      else
        send(MessageType::Catalog, {{"refresh", true}});
    });
    connect(restore, &QPushButton::clicked, this, [this] { returnAll(); });
    m_keyframeRetry.setInterval(25);
    connect(&m_keyframeRetry, &QTimer::timeout, this, [this] { retryKeyframe(); });
    if (m_hosting) {
      m_native = std::make_unique<NativeHost>(std::move(journal));
      m_native->setHoldingDisplayEnabled(holding);
      m_native->onError = [this](QString error) {
        status(error);
        send(MessageType::Error, {{"message", error.left(2048)}});
      };
      m_native->onCatalog = [this](const QVector<Candidate> &windows) {
        const auto catalog = catalogForWire(windows);
        m_lastCatalog = catalog;
        m_catalogReady = true;
        displayCatalog(catalog);
        if (m_established)
          send(MessageType::Catalog, {{"windows", catalog}});
        maybeInitialExport();
      };
      m_native->onOpened = [this](std::uint64_t session, std::uint64_t epoch, const QJsonObject &metadata) {
        m_hostSessions[session] = epoch;
        if (!send(MessageType::Opened, windowMetadataForWire(metadata), session, epoch))
          QTimer::singleShot(0, this, [this, session] { m_native->restore(session); });
      };
      m_native->onFrame =
          [this](std::uint64_t session, std::uint64_t epoch, std::uint64_t geometry, const QByteArray &frame) {
            const auto found = m_hostSessions.find(session);
            if (!m_channel || !m_established || found == m_hostSessions.end() || found->second != epoch)
              return;
            // Once an access unit is dropped, no dependent frame may pass until
            // a deferred keyframe request invalidates the old GOP. Polling the
            // drained socket avoids an encode/drop/re-encode loop on a slow peer.
            if (m_pendingKeyframes.count(session))
              return;
            if (!m_channel->sendFrame(session, epoch, geometry, frame) && m_channel && m_established) {
              m_pendingKeyframes[session] = epoch;
              if (!m_keyframeRetry.isActive())
                m_keyframeRetry.start();
            }
          };
      m_native->onCommitted = [this](std::uint64_t session, std::uint64_t epoch) {
        send(MessageType::Accepted, {{"kind", "committed"}}, session, epoch);
      };
      m_native->onFocused = [this](
                                std::uint64_t session, std::uint64_t epoch, std::uint64_t revision,
                                std::uint64_t request
                            ) {
        send(MessageType::Focus, {{"focusRevision", number(revision)}, {"request", number(request)}}, session, epoch);
      };
      m_native->onClosed = [this](std::uint64_t session) {
        m_pendingKeyframes.erase(session);
        if (m_pendingKeyframes.empty())
          m_keyframeRetry.stop();
        const auto found = m_hostSessions.find(session);
        if (found != m_hostSessions.end()) {
          const auto epoch = found->second;
          m_hostSessions.erase(found);
          send(MessageType::Closed, {}, session, epoch);
        }
      };
      m_native->refresh();
    }
  }
  ~Bridge() override
  {
    clearKeyframeRetries();
    if (m_channel) {
      m_channel->onClosed = {};
      m_channel->onPacket = {};
      m_channel->close();
    }
    stopViewers();
    if (m_native) {
      m_native->releaseAll();
      m_native->restoreAll();
    }
  }
  bool listen(const QHostAddress &address, quint16 port)
  {
    m_listener.incoming = [this](qintptr descriptor) {
      if (m_channel || m_cleaning) {
        QTcpSocket socket;
        socket.setSocketDescriptor(descriptor);
        socket.abort();
        return;
      }
      prepareChannel();
      m_channel->accept(descriptor);
    };
    if (!m_listener.listen(address, port)) {
      status(m_listener.errorString());
      return false;
    }
    status(QString("Listening on %1:%2; awaiting approved peer").arg(address.toString()).arg(m_listener.serverPort()));
    return true;
  }
  void connectTo(const QString &host, quint16 port)
  {
    prepareChannel();
    status("Connecting to approved Mac host…");
    m_channel->connectTo(host, port);
  }

private:
  void status(const QString &message)
  {
    m_status->setText(message);
    QTextStream(stderr) << message << '\n';
  }
  bool send(MessageType type, const QJsonObject &object = {}, std::uint64_t session = 0, std::uint64_t epoch = 0)
  {
    return m_established && m_channel && m_channel->send(type, object, session, epoch);
  }
  void clearKeyframeRetries()
  {
    m_keyframeRetry.stop();
    m_pendingKeyframes.clear();
  }
  void retryKeyframe()
  {
    if (!m_channel || !m_established || !m_native) {
      clearKeyframeRetries();
      return;
    }
    if (!m_channel->readyForFrame())
      return;
    if (m_pendingKeyframes.empty()) {
      m_keyframeRetry.stop();
      return;
    }
    // Limit recovery work to one window per tick. Erase before calling native
    // code: requestKeyframe invalidates old submissions before it can emit the
    // replacement, and a synchronous callback may queue a new recovery request.
    const auto [session, epoch] = *m_pendingKeyframes.begin();
    m_pendingKeyframes.erase(m_pendingKeyframes.begin());
    if (m_pendingKeyframes.empty())
      m_keyframeRetry.stop();
    const auto found = m_hostSessions.find(session);
    if (found != m_hostSessions.end() && found->second == epoch)
      m_native->requestKeyframe(session, epoch);
  }
  void prepareChannel()
  {
    m_established = false;
    clearKeyframeRetries();
    if (!m_hosting)
      m_catalogReady = false;
    auto *channel = new Channel(m_identity, m_pin, this);
    m_channel = channel;
    channel->onAuthenticated = [this, channel] {
      if (m_channel != channel)
        return;
      if (!m_hosting)
        channel->send(
            MessageType::Hello, {{"version", static_cast<int>(protocolVersion)}, {"role", "viewer"}, {"codec", "h264"}}
        );
      QTimer::singleShot(5000, channel, [this, channel] {
        if (m_channel == channel && !m_established)
          channel->close("Window sharing negotiation timed out");
      });
    };
    channel->onPacket = [this, channel](Packet packet) {
      if (m_channel != channel)
        return;
      try {
        receive(packet);
      } catch (const std::exception &error) {
        channel->close(QString::fromUtf8(error.what()));
      }
    };
    channel->onClosed = [this, channel](QString reason) {
      if (m_channel != channel)
        return;
      m_established = false;
      m_channel = nullptr;
      clearKeyframeRetries();
      // send() may fail inside a NativeHost callback. Never destroy its entries
      // reentrantly while capture/geometry code is still using them.
      m_cleaning = true;
      QTimer::singleShot(0, this, [this] {
        stopViewers();
        if (m_native) {
          m_native->releaseAll();
          m_native->restoreAll();
        }
        m_hostSessions.clear();
        m_cleaning = false;
      });
      status("Disconnected: " + reason);
      channel->deleteLater();
    };
  }
  void receive(const Packet &packet)
  {
    if (!m_established) {
      const auto object = parseObject(packet);
      require(packet.session == 0 && packet.epoch == 0, "Negotiation requires connection scope");
      require(
          integer(object, "version") == protocolVersion && object["codec"] == "h264",
          "Unsupported window sharing version or codec"
      );
      if (m_hosting) {
        require(packet.type == MessageType::Hello && object["role"] == "viewer", "Expected viewer negotiation");
        m_established = true;
        send(
            MessageType::Accepted, {{"version", static_cast<int>(protocolVersion)}, {"role", "host"}, {"codec", "h264"}}
        );
        if (m_catalogReady)
          send(MessageType::Catalog, {{"windows", m_lastCatalog}});
        m_native->refresh();
        maybeInitialExport();
      } else {
        require(packet.type == MessageType::Accepted && object["role"] == "host", "Expected host negotiation");
        m_established = true;
      }
      status("Connected to approved peer — window sharing enabled");
      return;
    }
    if (packet.type == MessageType::Error) {
      status("Peer: " + parseObject(packet).value("message").toString().left(2048));
      return;
    }
    if (m_hosting)
      receiveHost(packet);
    else
      receiveViewer(packet);
  }
  void receiveHost(const Packet &packet)
  {
    const auto object = parseObject(packet);
    if (packet.type == MessageType::Catalog || packet.type == MessageType::Export ||
        packet.type == MessageType::ReleaseAll) {
      require(packet.session == 0 && packet.epoch == 0, "Expected connection scope");
      if (packet.type == MessageType::Catalog)
        m_native->refresh();
      else if (packet.type == MessageType::ReleaseAll)
        returnAll();
      else {
        require(m_hostSessions.size() < 16, "Too many exported windows");
        const auto id = integer(object, "windowId", UINT32_MAX);
        require(id != 0, "Invalid window ID");
        m_native->exportWindow(static_cast<std::uint32_t>(id));
      }
      return;
    }
    const auto found = m_hostSessions.find(packet.session);
    // A delayed return/focus acknowledgement cannot address a newer incarnation.
    if (found == m_hostSessions.end() || found->second != packet.epoch)
      return;
    switch (packet.type) {
    case MessageType::Ready:
      m_native->ready(packet.session, packet.epoch);
      break;
    case MessageType::Input:
      m_native->input(packet.session, packet.epoch, object);
      break;
    case MessageType::Resize:
      m_native->resizeWindow(
          packet.session, packet.epoch, static_cast<int>(integer(object, "width", 8192)),
          static_cast<int>(integer(object, "height", 8192)), integer(object, "request")
      );
      break;
    case MessageType::Return:
      m_native->restore(packet.session);
      break;
    default:
      throw std::runtime_error("Message is not valid for the Mac host");
    }
  }
  void receiveViewer(const Packet &packet)
  {
    if (packet.type == MessageType::Catalog) {
      require(packet.session == 0 && packet.epoch == 0, "Expected catalog connection scope");
      const auto value = parseObject(packet).value("windows");
      require(value.isArray(), "Expected catalog window array");
      const auto windows = value.toArray();
      require(windows.size() <= 4096, "Catalog too large");
      displayCatalog(windows);
      m_catalogReady = true;
      maybeInitialExport();
      return;
    }
    if (packet.type == MessageType::Opened) {
      require(packet.session > 0 && packet.epoch > 0, "Invalid window session");
      const auto metadata = parseObject(packet);
      auto found = m_viewers.find(packet.session);
      if (found != m_viewers.end()) {
        require(found->second.epoch == packet.epoch, "Duplicate window session");
        found->second.window->applyMetadata(metadata);
        return;
      }
      require(m_viewers.size() < 16, "Too many viewer windows");
      auto *viewer = new Viewer(packet.session, packet.epoch, metadata);
      viewer->setAttribute(Qt::WA_DeleteOnClose);
      const auto id = packet.session;
      viewer->onSend = [this](MessageType type, const QJsonObject &object, std::uint64_t session, std::uint64_t epoch) {
        return send(type, object, session, epoch);
      };
      viewer->onError = [this](QString error) { status(error); };
      viewer->onReturned = [this, id, epoch = packet.epoch, viewer] { forgetViewer(id, epoch, viewer); };
      connect(viewer, &QObject::destroyed, this, [this, id, epoch = packet.epoch, viewer] {
        forgetViewer(id, epoch, viewer);
      });
      m_viewers.emplace(id, View{packet.epoch, viewer, viewer});
      viewer->placeOnScreen(m_screen);
      viewer->show();
      return;
    }
    auto found = m_viewers.find(packet.session);
    if (found == m_viewers.end() || found->second.epoch != packet.epoch || !found->second.window)
      return;
    auto *viewer = found->second.window.data();
    switch (packet.type) {
    case MessageType::Frame: {
      const auto frame = parseFramePayload(packet);
      viewer->receiveFrame(frame.accessUnit, frame.geometry);
      break;
    }
    case MessageType::Accepted:
      require(parseObject(packet)["kind"] == "committed", "Invalid session acknowledgement");
      viewer->committed();
      break;
    case MessageType::Focus: {
      const auto object = parseObject(packet);
      viewer->focusAcknowledged(integer(object, "focusRevision"), integer(object, "request"));
      break;
    }
    case MessageType::Closed:
      viewer->disconnected();
      viewer->onReturned = {};
      m_viewers.erase(found);
      viewer->close();
      break;
    default:
      throw std::runtime_error("Message is not valid for the viewer");
    }
  }
  void displayCatalog(const QJsonArray &windows)
  {
    m_catalog->clear();
    for (const auto &value : windows) {
      const auto window = value.toObject();
      const auto id = integer(window, "id", UINT32_MAX);
      auto *item = new QListWidgetItem(window["title"].toString().left(512), m_catalog);
      item->setData(Qt::UserRole, static_cast<quint32>(id));
    }
  }
  void maybeInitialExport()
  {
    if (!m_established || !m_catalogReady || m_initialWindow == 0)
      return;
    const auto id = m_initialWindow;
    m_initialWindow = 0;
    if (m_native)
      m_native->exportWindow(id);
    else
      send(MessageType::Export, {{"windowId", static_cast<qint64>(id)}});
  }
  void openSelected()
  {
    if (!m_established) {
      status("Connect an approved peer before sharing a window.");
      return;
    }
    auto *item = m_catalog->currentItem();
    if (!item)
      return;
    const auto id = item->data(Qt::UserRole).toUInt();
    if (m_native)
      m_native->exportWindow(id);
    else
      send(MessageType::Export, {{"windowId", static_cast<qint64>(id)}});
  }
  void returnAll()
  {
    if (m_native) {
      clearKeyframeRetries();
      m_native->releaseAll();
      m_native->restoreAll();
    } else {
      send(MessageType::ReleaseAll);
      stopViewers();
    }
  }
  void stopViewers()
  {
    auto viewers = std::move(m_viewers);
    m_viewers.clear();
    for (auto &[id, view] : viewers)
      if (view.window) {
        view.window->onReturned = {};
        view.window->disconnected();
        view.window->close();
      }
  }
  void forgetViewer(std::uint64_t session, std::uint64_t epoch, const Viewer *window)
  {
    const auto found = m_viewers.find(session);
    if (found != m_viewers.end() && found->second.epoch == epoch && found->second.identity == window)
      m_viewers.erase(found);
  }
  struct View
  {
    std::uint64_t epoch;
    QPointer<Viewer> window;
    // QPointer is already null when QObject::destroyed runs. Keep an opaque
    // identity for comparison only; a delayed callback cannot erase a successor.
    const Viewer *identity;
  };
  bool m_hosting, m_established = false, m_cleaning = false, m_catalogReady = false;
  Identity m_identity;
  QByteArray m_pin;
  int m_screen;
  std::uint32_t m_initialWindow;
  QLabel *m_status = nullptr;
  QListWidget *m_catalog = nullptr;
  Listener m_listener;
  QPointer<Channel> m_channel;
  QTimer m_keyframeRetry;
  std::unique_ptr<NativeHost> m_native;
  std::map<std::uint64_t, std::uint64_t> m_hostSessions;
  std::map<std::uint64_t, std::uint64_t> m_pendingKeyframes;
  std::map<std::uint64_t, View> m_viewers;
  QJsonArray m_lastCatalog;
};
} // namespace

#ifndef WINDOWSHARE_BRIDGE_TEST
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  app.setApplicationName("DeskflowWindowShare");
  app.setOrganizationName("Deskflow");
  app.setApplicationVersion("0.1-experimental");
  app.setQuitOnLastWindowClosed(false);
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  parser.setApplicationDescription("Experimental per-window sharing. Separate from the working Deskflow installation.");
  parser.addOptions(
      {{"enable-seamless", "Explicitly enable experimental window sharing."},
       {"holding-display", "Enable the experimental private virtual holding display on the Mac."},
       {"listen", "Listen on this Mac interface address.", "address"},
       {"connect", "Connect to the approved Mac host.", "host"},
       {"port", "Separate TLS control/media port.", "port", "24810"},
       {"identity", "Private identity directory.", "directory",
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/identity"},
       {"peer-pin", "Approved SHA-256 certificate fingerprint of the other computer.", "fingerprint"},
       {"init-identity", "Create a new private TLS identity; refuses overwrite."},
       {"fingerprint", "Print this identity's public certificate fingerprint."},
       {"probe", "Report macOS capture and Accessibility permissions without requesting them."},
       {"request-permissions", "Ask macOS for capture and Accessibility access."},
       {"recover", "Restore journaled windows without opening a network connection."},
       {"journal", "Persistent restore journal path.", "file",
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/restore.json"},
       {"screen", "Viewer screen index (zero based).", "index", "0"},
       {"export-window", "Explicit window ID to export once connected.", "id", "0"},
       {"quit-after", "Exit cleanly after this many seconds (diagnostics).", "seconds", "0"},
       {"headless", "Do not show the connection window (viewer windows still appear)."}}
  );
  parser.process(app);
  try {
    if (parser.isSet("init-identity"))
      initializeIdentity(parser.value("identity"));
    if (parser.isSet("init-identity") || parser.isSet("fingerprint")) {
      QTextStream(stdout) << Identity::load(parser.value("identity")).fingerprint() << '\n';
      return 0;
    }
    if (parser.isSet("request-permissions"))
      NativeHost::requestPermissions();
    if (parser.isSet("probe") || parser.isSet("request-permissions")) {
      QTextStream(stdout) << QJsonDocument(NativeHost::permissions()).toJson(QJsonDocument::Compact) << '\n';
      return 0;
    }
    const bool hosting = parser.isSet("listen");
    require(
        parser.isSet("recover") || (parser.isSet("enable-seamless") && hosting != parser.isSet("connect")),
        "Use --enable-seamless and exactly one of --listen ADDRESS or --connect HOST. See --help."
    );
    std::unique_ptr<QLockFile> journalLock;
    if (hosting || parser.isSet("recover")) {
      const auto journal = QFileInfo(parser.value("journal")).absoluteFilePath();
      require(QDir().mkpath(QFileInfo(journal).absolutePath()), "Cannot create journal directory");
      journalLock = std::make_unique<QLockFile>(journal + ".lock");
      journalLock->setStaleLockTime(0);
      require(journalLock->tryLock(), "Another window host owns this restore journal");
    }
    if (parser.isSet("recover")) {
      NativeHost native(parser.value("journal"));
      native.onError = [](const QString &error) { QTextStream(stderr) << error << '\n'; };
      return native.recover() ? 0 : 1;
    }
    bool valid = false;
    const auto port = parser.value("port").toUInt(&valid);
    require(valid && port > 0 && port <= 65535, "Invalid port");
    const auto screen = parser.value("screen").toInt(&valid);
    require(valid && screen >= 0, "Invalid screen index");
    const auto window = parser.value("export-window").toUInt(&valid);
    require(valid, "Invalid window ID");
    const auto seconds = parser.value("quit-after").toInt(&valid);
    require(valid && seconds >= 0 && seconds <= 86400, "Invalid exit timer");
    Bridge bridge(
        hosting, Identity::load(parser.value("identity")), parseFingerprint(parser.value("peer-pin")),
        parser.value("journal"), screen, window, parser.isSet("holding-display")
    );
    if (hosting) {
      QHostAddress address;
      require(address.setAddress(parser.value("listen")), "Listen address must be an explicit IP address");
      if (!bridge.listen(address, static_cast<quint16>(port)))
        return 1;
    } else
      bridge.connectTo(parser.value("connect"), static_cast<quint16>(port));
    if (!parser.isSet("headless"))
      bridge.show();
    if (seconds)
      QTimer::singleShot(seconds * 1000, &app, &QApplication::quit);
    std::signal(SIGINT, interruptHandler);
    std::signal(SIGTERM, interruptHandler);
    QTimer signals;
    QObject::connect(&signals, &QTimer::timeout, &app, [&app] {
      if (interrupted)
        app.quit();
    });
    signals.start(100);
    QObject::connect(&app, &QApplication::lastWindowClosed, &app, [&app] { app.quit(); });
    return app.exec();
  } catch (const std::exception &error) {
    QTextStream(stderr) << "Window sharing: " << error.what() << '\n';
    return 1;
  }
}
#endif
