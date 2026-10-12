/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "KeyboardRouteProtocol.h"
#include "NativeHost.h"
#include "Transport.h"
#include "Viewer.h"
#include <QApplication>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QDeadlineTimer>
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
#include <algorithm>
#include <array>
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
    m_open = new QPushButton(hosting ? "Share selected window" : "Open selected window", this);
    m_refresh = new QPushButton("Refresh windows", this);
    auto *restore = new QPushButton("Return all windows to Mac", this);
    layout->addWidget(m_open);
    layout->addWidget(m_refresh);
    layout->addWidget(restore);
    if (!hosting) {
      m_retry = new QPushButton("Retry connection", this);
      m_cancel = new QPushButton("Cancel connection", this);
      layout->addWidget(m_retry);
      layout->addWidget(m_cancel);
      connect(m_retry, &QPushButton::clicked, this, [this] { retryConnection(); });
      connect(m_cancel, &QPushButton::clicked, this, [this] { cancelConnection(); });
    }
    resize(480, 440);
    connect(m_open, &QPushButton::clicked, this, [this] { openSelected(); });
    connect(m_catalog, &QListWidget::itemDoubleClicked, this, [this] { openSelected(); });
    connect(m_refresh, &QPushButton::clicked, this, [this] {
      if (m_native)
        m_native->refresh();
      else
        send(MessageType::Catalog, {{"refresh", true}});
    });
    connect(restore, &QPushButton::clicked, this, [this] { returnAll(); });
    m_keyframeRetry.setInterval(25);
    connect(&m_keyframeRetry, &QTimer::timeout, this, [this] { retryKeyframe(); });
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] { startConnection(); });
    m_stableTimer.setSingleShot(true);
    connect(&m_stableTimer, &QTimer::timeout, this, [this] {
      if (m_established && m_catalogReady && !m_shuttingDown)
        m_retryStep = 0;
    });
    m_keyboardTimeout.setSingleShot(true);
    connect(&m_keyboardTimeout, &QTimer::timeout, this, [this] {
      if (m_keyboardRoute && m_keyboardRoute->phase == KeyboardPhase::AwaitViewer) {
        if (m_channel)
          m_channel->close("Keyboard handoff resume timed out");
      } else
        withdrawOriginalKeyboard(KeyboardRouteReason::Timeout);
    });
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
        updateControls();
        if (m_established)
          send(MessageType::Catalog, {{"windows", catalog}});
        maybeInitialExport();
      };
      m_native->onOpened = [this](std::uint64_t session, std::uint64_t epoch, const QJsonObject &metadata) {
        if (m_keyboardRoute && m_keyboardRoute->binding.owner == Owner{session, epoch} &&
            m_keyboardRoute->binding.geometry != integer(metadata, "geometry"))
          withdrawOriginalKeyboard(KeyboardRouteReason::GeometryChanged);
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
        if (m_keyboardRoute && m_keyboardRoute->binding.owner == Owner{session, epoch} &&
            (m_keyboardRoute->binding.focusRevision != revision || m_keyboardRoute->binding.focusRequest != request))
          withdrawOriginalKeyboard(KeyboardRouteReason::FocusChanged);
        send(MessageType::Focus, {{"focusRevision", number(revision)}, {"request", number(request)}}, session, epoch);
      };
      m_native->onKeyboardRouteInvalidated = [this](KeyboardRouteBinding binding, KeyboardRouteReason reason) {
        if (m_keyboardRoute && m_keyboardRoute->binding == binding)
          withdrawOriginalKeyboard(reason);
      };
      m_native->onClosed = [this](std::uint64_t session) {
        if (m_keyboardRoute && m_keyboardRoute->binding.owner.session == session)
          withdrawOriginalKeyboard(KeyboardRouteReason::Returned, false);
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
    updateControls();
  }
  std::function<void()> onQuitRequested;
  // A local controller must revoke its private original-input authority before
  // either native admission or viewer forwarding can resume. Network messages
  // cannot install this callback, and main does not activate this route yet.
  // These callbacks borrow this controller. They may close the channel or
  // withdraw a route, but must use deleteLater for controller destruction.
  // Revocation is synchronous; quiesced notification is deferred outside the
  // transport/native callback stack and revalidated immediately before delivery.
  std::function<bool(const KeyboardRouteBinding &, KeyboardRouteReason)> revokeOriginalKeyboard;
  std::function<void(const KeyboardRouteBinding &)> onKeyboardQuiesced;
  bool requestOriginalKeyboard(std::uint64_t session, std::uint64_t epoch)
  {
    if (!m_native || !m_established || !m_keyboardCapable || m_keyboardRoute || m_cleaning || m_shuttingDown ||
        !revokeOriginalKeyboard || m_keyboardGeneration == 9007199254740991ULL)
      return false;
    const auto found = m_hostSessions.find(session);
    if (found == m_hostSessions.end() || found->second != epoch)
      return false;
    const auto binding = m_native->keyboardRouteCandidate(session, epoch, m_keyboardGeneration + 1);
    if (!binding || !validKeyboardRouteBinding(*binding))
      return false;
    m_keyboardGeneration = binding->generation;
    m_keyboardRoute = KeyboardAttempt{*binding};
    m_keyboardRoute->deadline = QDeadlineTimer(m_keyboardTimeoutMs, Qt::PreciseTimer);
    m_keyboardTimeout.start(m_keyboardTimeoutMs);
    return sendKeyboardRoute({*binding, KeyboardRouteAction::Request, KeyboardRouteMode::Original});
  }
  void withdrawOriginalKeyboard(KeyboardRouteReason reason, bool resumeViewer = true)
  {
    if (!m_keyboardRoute)
      return;
    const QPointer<Bridge> alive(this);
    const auto binding = m_keyboardRoute->binding;
    if (m_keyboardRoute->phase == KeyboardPhase::Revoking) {
      m_keyboardRoute->resumeViewer = m_keyboardRoute->resumeViewer && resumeViewer;
      return;
    }
    if (!m_keyboardRoute->revoked) {
      m_keyboardRoute->revoked = true;
      m_keyboardRoute->phase = KeyboardPhase::Revoking;
      m_keyboardRoute->resumeViewer = resumeViewer;
      m_keyboardTimeout.stop();
      bool released = false;
      try {
        const auto revoke = revokeOriginalKeyboard;
        released = revoke && revoke(binding, reason);
      } catch (...) {
      }
      if (!alive || !m_keyboardRoute || m_keyboardRoute->binding != binding)
        return;
      if (!released) {
        m_keyboardTimeout.stop();
        retireKeyboardRoute();
        if (m_channel)
          m_channel->close("Original keyboard authority could not be revoked");
        return;
      }
      if (!m_native->resumeKeyboard(binding) && m_keyboardRoute->quiesced) {
        retireKeyboardRoute();
        if (m_channel)
          m_channel->close("Native keyboard fence could not be resumed");
        return;
      }
      resumeViewer = m_keyboardRoute->resumeViewer;
      m_keyboardRoute->phase = KeyboardPhase::AwaitViewer;
    } else if (resumeViewer)
      return;
    if (resumeViewer && m_channel && m_established && !m_cleaning && !m_shuttingDown) {
      m_keyboardRoute->deadline = QDeadlineTimer(m_keyboardTimeoutMs, Qt::PreciseTimer);
      m_keyboardTimeout.start(m_keyboardTimeoutMs);
      sendKeyboardRoute({binding, KeyboardRouteAction::Request, KeyboardRouteMode::Viewer});
    } else
      retireKeyboardRoute();
  }
  ~Bridge() override
  {
    m_shuttingDown = true;
    m_reconnectEnabled = false;
    m_reconnectTimer.stop();
    m_stableTimer.stop();
    clearKeyframeRetries();
    if (m_channel) {
      m_channel->onClosed = {};
      m_channel->onPacket = {};
      m_channel->close();
    }
    withdrawOriginalKeyboard(KeyboardRouteReason::Disconnected, false);
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
      // A reconnect may arrive before the old peer's disconnect is observed.
      // Keep it in the bounded listen backlog until cleanup, rather than
      // sending an unauthenticated EOF that must be treated as terminal.
      m_listener.pauseAccepting();
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
    if (m_hosting || m_channel || m_cleaning || m_shuttingDown)
      return;
    m_connectHost = host;
    m_connectPort = port;
    retryConnection();
  }

protected:
  void closeEvent(QCloseEvent *event) override
  {
    m_shuttingDown = true;
    cancelConnection();
    m_listener.close();
    event->accept();
    if (onQuitRequested)
      onQuitRequested();
  }

private:
  enum class KeyboardPhase
  {
    AwaitQuiescence,
    Quiesced,
    Revoking,
    AwaitViewer
  };
  struct KeyboardAttempt
  {
    KeyboardRouteBinding binding;
    KeyboardPhase phase = KeyboardPhase::AwaitQuiescence;
    bool revoked = false, quiesced = false;
    bool resumeViewer = true;
    QDeadlineTimer deadline{QDeadlineTimer::Forever};
  };
  bool sendKeyboardRoute(const KeyboardRouteControl &control)
  {
    return m_keyboardCapable && send(
                                    MessageType::KeyboardRoute, encodeKeyboardRouteControl(control),
                                    control.binding.owner.session, control.binding.owner.epoch
                                );
  }
  void retireKeyboardRoute()
  {
    m_keyboardTimeout.stop();
    if (m_keyboardRoute)
      m_retiredKeyboardRoute = m_keyboardRoute->binding;
    m_keyboardRoute.reset();
  }
  void receiveKeyboardRoute(const Packet &packet)
  {
    require(m_keyboardCapable, "Keyboard route capability was not negotiated");
    const auto control = parseKeyboardRouteControl(packet);
    if (!m_hosting) {
      require(control.action == KeyboardRouteAction::Request, "Invalid keyboard route direction");
      const auto found = m_viewers.find(packet.session);
      if (found != m_viewers.end() && found->second.epoch == packet.epoch && found->second.window)
        found->second.window->keyboardRoute(control);
      return;
    }
    require(control.action != KeyboardRouteAction::Request, "Invalid keyboard route direction");
    if (!m_keyboardRoute || m_keyboardRoute->binding != control.binding) {
      if (control.binding.generation < m_keyboardGeneration || m_retiredKeyboardRoute == control.binding)
        return;
      throw std::runtime_error("Unsolicited or mismatched keyboard route reply");
    }
    if (control.action == KeyboardRouteAction::Withdraw) {
      withdrawOriginalKeyboard(control.reason);
      return;
    }
    if (m_keyboardRoute->phase == KeyboardPhase::Revoking)
      return;
    if (m_keyboardRoute->phase == KeyboardPhase::AwaitViewer && m_keyboardRoute->deadline.hasExpired()) {
      if (m_channel)
        m_channel->close("Keyboard handoff resume timed out");
      return;
    }
    if (m_keyboardRoute->phase == KeyboardPhase::AwaitQuiescence && control.mode == KeyboardRouteMode::Original) {
      if (m_keyboardRoute->deadline.hasExpired()) {
        withdrawOriginalKeyboard(KeyboardRouteReason::Timeout);
        return;
      }
      if (control.result != KeyboardRouteResult::Ready) {
        withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost, false);
        return;
      }
      if (!m_native->quiesceKeyboard(control.binding)) {
        withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
        return;
      }
      m_keyboardRoute->quiesced = true;
      if (m_keyboardRoute->deadline.hasExpired()) {
        withdrawOriginalKeyboard(KeyboardRouteReason::Timeout);
        return;
      }
      m_keyboardTimeout.stop();
      m_keyboardRoute->phase = KeyboardPhase::Quiesced;
      const auto binding = control.binding;
      const auto generation = m_connectionGeneration;
      QTimer::singleShot(0, this, [this, binding, generation] {
        if (!m_keyboardRoute || m_keyboardRoute->binding != binding ||
            m_keyboardRoute->phase != KeyboardPhase::Quiesced || m_connectionGeneration != generation ||
            !m_established || m_cleaning || m_shuttingDown)
          return;
        const bool neutral = m_native->keyboardRouteQuiesced(binding);
        if (m_keyboardRoute->deadline.hasExpired()) {
          withdrawOriginalKeyboard(KeyboardRouteReason::Timeout);
          return;
        }
        if (!neutral) {
          withdrawOriginalKeyboard(KeyboardRouteReason::AuthorityLost);
          return;
        }
        try {
          const auto callback = onKeyboardQuiesced;
          if (callback)
            callback(binding);
        } catch (...) {
          if (m_connectionGeneration == generation && m_keyboardRoute && m_keyboardRoute->binding == binding)
            withdrawOriginalKeyboard(KeyboardRouteReason::Failed);
        }
      });
    } else if (m_keyboardRoute->phase == KeyboardPhase::AwaitViewer && control.mode == KeyboardRouteMode::Viewer) {
      require(
          control.result == KeyboardRouteResult::Ready ||
              (!m_keyboardRoute->quiesced && control.result == KeyboardRouteResult::Stale),
          "Viewer did not resume the revoked keyboard route"
      );
      retireKeyboardRoute();
    }
    // An old Original ack may cross cancellation and the Viewer request. It
    // never reactivates a quiesced or revoked attempt.
  }
  void updateControls()
  {
    m_open->setEnabled(m_established && m_catalogReady && !m_cleaning && !m_shuttingDown);
    m_refresh->setEnabled(!m_cleaning && !m_shuttingDown && (m_hosting || m_established));
    if (m_retry)
      m_retry->setEnabled(!m_shuttingDown && !m_channel && !m_connectHost.isEmpty() && !m_retryRequested);
    if (m_cancel)
      m_cancel->setEnabled(!m_shuttingDown && (m_channel || m_cleaning || m_reconnectTimer.isActive()));
  }
  void clearCatalog()
  {
    m_catalogReady = false;
    m_lastCatalog = {};
    m_catalog->clear();
    updateControls();
  }
  void retryConnection()
  {
    if (m_hosting || m_shuttingDown || m_channel || m_connectHost.isEmpty())
      return;
    m_reconnectEnabled = true;
    m_retryStep = 0;
    m_reconnectTimer.stop();
    m_retryRequested = true;
    if (!m_cleaning)
      startConnection();
    updateControls();
  }
  void cancelConnection()
  {
    m_reconnectEnabled = false;
    m_retryRequested = false;
    m_initialWindow = 0;
    m_reconnectTimer.stop();
    m_stableTimer.stop();
    if (m_channel)
      m_channel->close("Connection canceled", CloseCause::Local);
    else if (!m_cleaning && !m_shuttingDown)
      status("Connection canceled. Retry to connect again.");
    updateControls();
  }
  void startConnection()
  {
    if (m_hosting || m_shuttingDown || !m_reconnectEnabled || m_cleaning || m_channel || m_connectHost.isEmpty())
      return;
    m_reconnectTimer.stop();
    m_retryRequested = false;
    try {
      prepareChannel();
      status("Connecting to approved Mac host…");
      m_channel->connectTo(m_connectHost, m_connectPort);
    } catch (const std::exception &error) {
      m_reconnectEnabled = false;
      status("Cannot connect: " + QString::fromUtf8(error.what()));
    }
    updateControls();
  }
  void scheduleReconnect()
  {
    if (m_hosting || m_shuttingDown || !m_reconnectEnabled || m_cleaning || m_channel || m_reconnectTimer.isActive())
      return;
    const int delay = m_retryDelays[m_retryStep];
    m_retryStep = std::min(m_retryStep + 1, m_retryDelays.size() - 1);
    m_reconnectTimer.start(delay);
    status(QString("Disconnected: %1\nRetrying in %2 seconds…").arg(m_disconnectReason).arg(delay / 1000.0));
    updateControls();
  }
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
    m_negotiating = false;
    m_keyboardCapable = false;
    m_stableTimer.stop();
    clearKeyframeRetries();
    clearCatalog();
    auto *channel = new Channel(m_identity, m_pin, this);
    m_channel = channel;
    const auto generation = ++m_connectionGeneration;
    channel->onAuthenticated = [this, channel, generation] {
      if (m_channel != channel || m_connectionGeneration != generation || m_shuttingDown)
        return;
      m_negotiating = true;
      if (!m_hosting)
        channel->send(
            MessageType::Hello, {{"version", static_cast<int>(protocolVersion)},
                                 {"role", "viewer"},
                                 {"codec", "h264"},
                                 {"capabilities", keyboardRouteCapabilities()}}
        );
      QTimer::singleShot(5000, channel, [this, channel, generation] {
        if (m_channel == channel && m_connectionGeneration == generation && !m_established)
          channel->close("Window sharing negotiation timed out", CloseCause::Protocol);
      });
    };
    channel->onPacket = [this, channel, generation](Packet packet) {
      if (m_channel != channel || m_connectionGeneration != generation || m_shuttingDown)
        return;
      try {
        receive(packet);
      } catch (const std::exception &error) {
        channel->close(QString::fromUtf8(error.what()), CloseCause::Protocol);
      }
    };
    channel->onClosed = [this, channel, generation](QString reason, CloseCause cause) {
      if (m_channel != channel || m_connectionGeneration != generation)
        return;
      // A peer that drops TLS while rejecting Hello may be rejecting our
      // identity or protocol. Only a fully negotiated loss is retryable there.
      const bool retryable = cause == CloseCause::Network && (!m_negotiating || m_established);
      if (!retryable)
        m_reconnectEnabled = false;
      if (m_established)
        m_initialWindow = 0;
      m_established = false;
      m_negotiating = false;
      m_channel = nullptr;
      m_stableTimer.stop();
      m_reconnectTimer.stop();
      clearKeyframeRetries();
      // send() may fail inside a NativeHost callback. Never destroy its entries
      // reentrantly while capture/geometry code is still using them.
      m_cleaning = true;
      withdrawOriginalKeyboard(KeyboardRouteReason::Disconnected, false);
      clearCatalog();
      QTimer::singleShot(0, this, [this, generation, retryable] {
        if (m_connectionGeneration != generation)
          return;
        stopViewers();
        if (m_native) {
          m_native->releaseAll();
          m_native->restoreAll();
        }
        m_hostSessions.clear();
        m_cleaning = false;
        if (m_hosting && !m_shuttingDown)
          m_listener.resumeAccepting();
        updateControls();
        if (m_retryRequested)
          startConnection();
        else if (retryable)
          scheduleReconnect();
      });
      m_disconnectReason = reason;
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
      m_keyboardCapable = peerSupportsKeyboardRoute(object);
      if (m_hosting) {
        require(packet.type == MessageType::Hello && object["role"] == "viewer", "Expected viewer negotiation");
        m_established = true;
        if (!send(
                MessageType::Accepted,
                {{"version", static_cast<int>(protocolVersion)},
                 {"role", "host"},
                 {"codec", "h264"},
                 {"capabilities", m_keyboardCapable ? keyboardRouteCapabilities() : QJsonArray{}}}
            ))
          return;
        // Every new connection waits for enumeration rather than publishing a
        // prior peer's catalog. NativeHost revalidates identity again on export.
        clearCatalog();
        m_native->refresh();
        maybeInitialExport();
      } else {
        require(packet.type == MessageType::Accepted && object["role"] == "host", "Expected host negotiation");
        m_established = true;
      }
      m_negotiating = false;
      updateControls();
      status("Connected to approved peer — window sharing enabled");
      return;
    }
    if (packet.type == MessageType::KeyboardRoute) {
      receiveKeyboardRoute(packet);
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
      const bool firstCatalog = !m_catalogReady;
      m_catalogReady = true;
      if (firstCatalog && m_established)
        m_stableTimer.start(m_stableInterval);
      updateControls();
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
      const auto generation = m_connectionGeneration;
      viewer->onSend = [this, viewer, generation](
                           MessageType type, const QJsonObject &object, std::uint64_t session, std::uint64_t epoch
                       ) {
        if (!currentViewer(session, epoch, viewer, generation))
          return false;
        return send(type, object, session, epoch);
      };
      viewer->onKeyboardRouteSend = [this, viewer, generation](const KeyboardRouteControl &control) {
        const auto owner = control.binding.owner;
        return currentViewer(owner.session, owner.epoch, viewer, generation) && sendKeyboardRoute(control);
      };
      viewer->onError = [this, id, epoch = packet.epoch, viewer, generation](QString error) {
        if (currentViewer(id, epoch, viewer, generation))
          status(error);
      };
      viewer->onReturned = [this, id, epoch = packet.epoch, viewer, generation] {
        forgetViewer(id, epoch, viewer, generation);
      };
      connect(viewer, &QObject::destroyed, this, [this, id, epoch = packet.epoch, viewer, generation] {
        forgetViewer(id, epoch, viewer, generation);
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
    if (!m_established || !m_catalogReady || m_cleaning) {
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
        view.window->onSend = {};
        view.window->onKeyboardRouteSend = {};
        view.window->onError = {};
        view.window->disconnected();
        view.window->close();
      }
  }
  bool currentViewer(std::uint64_t session, std::uint64_t epoch, const Viewer *window, std::uint64_t generation) const
  {
    const auto found = m_viewers.find(session);
    return m_connectionGeneration == generation && found != m_viewers.end() && found->second.epoch == epoch &&
           found->second.identity == window;
  }
  void forgetViewer(std::uint64_t session, std::uint64_t epoch, const Viewer *window, std::uint64_t generation)
  {
    if (currentViewer(session, epoch, window, generation))
      m_viewers.erase(session);
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
  bool m_negotiating = false, m_reconnectEnabled = false, m_retryRequested = false, m_shuttingDown = false;
  Identity m_identity;
  QByteArray m_pin;
  int m_screen;
  std::uint32_t m_initialWindow;
  QLabel *m_status = nullptr;
  QListWidget *m_catalog = nullptr;
  QPushButton *m_open = nullptr, *m_refresh = nullptr, *m_retry = nullptr, *m_cancel = nullptr;
  Listener m_listener;
  QPointer<Channel> m_channel;
  QTimer m_keyframeRetry;
  QTimer m_reconnectTimer, m_stableTimer;
  QTimer m_keyboardTimeout;
  int m_keyboardTimeoutMs = 3000;
  bool m_keyboardCapable = false;
  std::uint64_t m_keyboardGeneration = 0;
  std::optional<KeyboardAttempt> m_keyboardRoute;
  std::optional<KeyboardRouteBinding> m_retiredKeyboardRoute;
  QString m_connectHost, m_disconnectReason;
  quint16 m_connectPort = 0;
  std::uint64_t m_connectionGeneration = 0;
  std::size_t m_retryStep = 0;
  std::array<int, 6> m_retryDelays{1000, 2000, 4000, 8000, 16000, 30000};
  int m_stableInterval = 30000;
  std::unique_ptr<NativeHost> m_native;
  std::map<std::uint64_t, std::uint64_t> m_hostSessions;
  std::map<std::uint64_t, std::uint64_t> m_pendingKeyframes;
  std::map<std::uint64_t, View> m_viewers;
  QJsonArray m_lastCatalog;
};
void configureApplicationLifetime(QApplication &application, Bridge &bridge, std::function<void()> quit)
{
  application.setQuitOnLastWindowClosed(false);
  bridge.onQuitRequested = std::move(quit);
}
} // namespace

#ifndef WINDOWSHARE_BRIDGE_TEST
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  app.setApplicationName("DeskflowWindowShare");
  app.setOrganizationName("Deskflow");
  app.setApplicationVersion("0.1-experimental");
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
    configureApplicationLifetime(app, bridge, [&app] { app.quit(); });
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
    return app.exec();
  } catch (const std::exception &error) {
    QTextStream(stderr) << "Window sharing: " << error.what() << '\n';
    return 1;
  }
}
#endif
