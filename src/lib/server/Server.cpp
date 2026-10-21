/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers.
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/Server.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/AppUtil.h"
#include "deskflow/Computer.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/IPlatformComputer.h"
#include "deskflow/OptionTypes.h"
#include "deskflow/PacketStreamFilter.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/StreamChunker.h"
#include "deskflow/ipc/CoreIpc.h"
#include "net/TCPSocket.h"
#include "server/ClientListener.h"
#include "server/ClientProxy.h"
#include "server/ClientProxyUnknown.h"
#include "server/PrimaryClient.h"

#ifdef _WIN32
#include <algorithm>
#include <array>
#endif
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

using namespace deskflow::server;

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
#include "windowshare/InputEvidence.h"
#include "windowshare/LifecycleDiagnostics.h"
using deskflow::windowshare::inputEvidenceApply;
using deskflow::windowshare::InputEvidenceMutation;
using deskflow::windowshare::InputEvidenceQueue;
namespace {
constexpr KeyModifierMask routedMomentaryModifiers =
    KeyModifierShift | KeyModifierControl | KeyModifierAlt | KeyModifierMeta | KeyModifierSuper | KeyModifierAltGr;
constexpr KeyModifierMask routedToggleModifiers = KeyModifierCapsLock | KeyModifierNumLock | KeyModifierScrollLock;
KeyModifierMask modifierForRoutedKey(KeyID id)
{
  switch (id) {
  case kKeyShift_L:
  case kKeyShift_R:
    return KeyModifierShift;
  case kKeyControl_L:
  case kKeyControl_R:
    return KeyModifierControl;
  case kKeyAlt_L:
  case kKeyAlt_R:
    return KeyModifierAlt;
  case kKeyMeta_L:
  case kKeyMeta_R:
    return KeyModifierMeta;
  case kKeySuper_L:
  case kKeySuper_R:
    return KeyModifierSuper;
  case kKeyAltGr:
    return KeyModifierAltGr;
  default:
    return 0;
  }
}
} // namespace

// The target has its own lifetime; a queued old wake cannot target a new
// Server at the same address. Close serializes with private-thread enqueue
// before either the Server or its event queue can be destroyed.
struct Server::OriginalInputWake : std::enable_shared_from_this<OriginalInputWake>
{
  struct Payload final : EventData
  {
    explicit Payload(std::shared_ptr<OriginalInputWake> value) : state(std::move(value))
    {
    }
    ~Payload() override
    {
      // Also retire a wake dropped by queue admission/platform shutdown.
      if (!handled)
        state->pending.store(false, std::memory_order_release);
    }
    std::shared_ptr<OriginalInputWake> state;
    mutable bool handled = false;
  };
  explicit OriginalInputWake(IEventQueue *value) : events(value)
  {
  }
  void notify() noexcept
  {
    std::lock_guard lock(mutex);
    if (!events || pending.exchange(true, std::memory_order_acq_rel))
      return;
    try {
      events->addEvent(Event(EventTypes::OriginalInputProgress, this, new Payload(shared_from_this())));
    } catch (...) {
      // No input/authority lives in this notification. The existing maintenance
      // timer still checks the same absolute deadline if scheduling fails.
      pending.store(false, std::memory_order_release);
    }
  }
  void close()
  {
    std::lock_guard lock(mutex);
    events = nullptr;
  }
  std::mutex mutex;
  IEventQueue *events;
  std::atomic_bool pending{false};
};

std::uint64_t Server::reserveServerIncarnation(std::atomic<std::uint64_t> &next) noexcept
{
  auto current = next.load(std::memory_order_relaxed);
  while (current != std::numeric_limits<std::uint64_t>::max()) {
    if (next.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
      return current + 1;
  }
  return 0; // Never wrap an incarnation into a previously issued value.
}

// Nested configuration/removal/saver operations must not publish an
// intermediate selection. Exceptions and failed switches leave no authority.
class Server::SelectionChange final
{
public:
  explicit SelectionChange(Server &server) noexcept
      : m_server(server),
        m_nested(server.m_selectionChanging),
        m_lifetime(server.m_originalLifetime),
        m_wasAlive(*m_lifetime)
  {
    if (auto *evidence = deskflow::windowshare::inputEvidence())
      evidence->revokeOwner();
    m_server.m_selectionChanging = true;
    m_server.invalidateWindowDragEdge(true);
    m_server.invalidateSelectedPeer();
    if (m_wasAlive)
      m_server.flushOriginalBeforeSelection();
  }
  ~SelectionChange()
  {
    if (!alive())
      return;
    m_server.m_selectionChanging = m_nested;
    if (m_complete && !m_nested)
      m_server.publishSelectedPeer();
  }
  void complete() noexcept
  {
    m_complete = true;
  }
  bool alive() const noexcept
  {
    // Destruction itself uses nested transition guards for ordinary cleanup.
    return !m_wasAlive || *m_lifetime;
  }

private:
  Server &m_server;
  bool m_nested, m_complete = false;
  std::shared_ptr<bool> m_lifetime;
  bool m_wasAlive;
};
#endif

//
// Server
//

Server::Server(ServerConfig &config, PrimaryClient *primaryClient, deskflow::Computer *computer, IEventQueue *events)
    : m_primaryClient(primaryClient),
      m_active(primaryClient),
      m_config(&config),
      m_inputFilter(config.getInputFilter()),
      m_computer(computer),
      m_events(events)
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  deskflow::windowshare::inputEvidenceInitialize(InputEvidenceQueue::ServerPending, this, originalPendingCapacity);
  deskflow::windowshare::inputEvidenceInitialize(InputEvidenceQueue::ServerCompleted, this, originalPendingCapacity);
  deskflow::windowshare::inputEvidenceInitialize(InputEvidenceQueue::ServerExecuting, this, originalPendingCapacity);
  // In-process snapshots outlive Server objects, but never survive this
  // process. Saturate rather than reuse an earlier Server incarnation.
  static std::atomic<std::uint64_t> nextServer{0};
  m_serverIncarnation = reserveServerIncarnation(nextServer);
  m_selectionShutdown = m_serverIncarnation == 0;
#endif
  // must have a primary client and it must have a canonical name
  assert(m_primaryClient != nullptr);
  assert(config.isComputer(primaryClient->getName()));
  assert(m_computer != nullptr);

  std::string primaryName = getName(primaryClient);

  // clear clipboards
  for (auto &clipboard : m_clipboards) {
    clipboard.m_clipboardOwner = primaryName;
    clipboard.m_clipboardSeqNum = m_seqNum;
    if (clipboard.m_clipboard.open(0)) {
      clipboard.m_clipboard.empty();
      clipboard.m_clipboard.close();
    }
    clipboard.m_clipboardData = clipboard.m_clipboard.marshall();
  }

  // install event handlers
  m_events->addHandler(EventTypes::Timer, this, [this](const auto &) { handleSwitchWaitTimeout(); });
  m_events->addHandler(EventTypes::KeyStateKeyDown, m_inputFilter, [this](const auto &e) { handleKeyDownEvent(e); });
  m_events->addHandler(EventTypes::KeyStateKeyUp, m_inputFilter, [this](const auto &e) { handleKeyUpEvent(e); });
  m_events->addHandler(EventTypes::KeyStateKeyRepeat, m_inputFilter, [this](const auto &e) {
    handleKeyRepeatEvent(e);
  });
  m_events->addHandler(EventTypes::PrimaryComputerButtonDown, m_inputFilter, [this](const auto &e) {
    handleButtonDownEvent(e);
  });
  m_events->addHandler(EventTypes::PrimaryComputerButtonUp, m_inputFilter, [this](const auto &e) {
    handleButtonUpEvent(e);
  });
  m_events->addHandler(
      EventTypes::PrimaryComputerMotionOnPrimary, m_primaryClient->getEventTarget(),
      [this](const auto &e) { handleMotionPrimaryEvent(e); }
  );
  m_events->addHandler(
      EventTypes::PrimaryComputerMotionOnSecondary, m_primaryClient->getEventTarget(),
      [this](const auto &e) { handleMotionSecondaryEvent(e); }
  );
  m_events->addHandler(EventTypes::PrimaryComputerWheel, m_primaryClient->getEventTarget(), [this](const auto &e) {
    handleWheelEvent(e);
  });
  m_events->addHandler(
      EventTypes::PrimaryScreenSaverActivated, m_primaryClient->getEventTarget(),
      [this](const auto &) { onScreensaver(true); }
  );
  m_events->addHandler(
      EventTypes::PrimaryScreenSaverDeactivated, m_primaryClient->getEventTarget(),
      [this](const auto &) { onScreensaver(false); }
  );
  m_events->addHandler(EventTypes::ServerSwitchToComputer, m_inputFilter, [this](const auto &e) {
    handleSwitchToComputerEvent(e);
  });
  m_events->addHandler(EventTypes::ServerSwitchInDirection, m_inputFilter, [this](const auto &e) {
    handleSwitchInDirectionEvent(e);
  });
  m_events->addHandler(EventTypes::ServerToggleComputer, m_inputFilter, [this](const auto &e) {
    handleToggleComputerEvent(e);
  });
  m_events->addHandler(EventTypes::ServerKeyboardBroadcast, m_inputFilter, [this](const auto &e) {
    handleKeyboardBroadcastEvent(e);
  });
  m_events->addHandler(EventTypes::ServerLockCursorToComputer, m_inputFilter, [this](const auto &e) {
    handleLockCursorToComputerEvent(e);
  });
  m_events->addHandler(EventTypes::PrimaryComputerFakeInputBegin, m_inputFilter, [this](const auto &) {
    m_primaryClient->fakeInputBegin();
  });
  m_events->addHandler(EventTypes::PrimaryComputerFakeInputEnd, m_inputFilter, [this](const auto &) {
    m_primaryClient->fakeInputEnd();
  });

  // add connection
  addClient(m_primaryClient);

  // set initial configuration
  setConfig(config);

  // If we are using libportal, we need to set the primary client first.
  // This enables the rules of the primary client. The hotkeys are then enabled in EiComputer.cpp after the rules
  // registered them.
#if WINAPI_LIBPORTAL
  // enable primary client
  m_inputFilter->setPrimaryClient(m_primaryClient);
  m_primaryClient->enable();
  // If we are on Mac, X11 or Windows, we need a key map, which is created in computer->enable().
  // So we need to enable the computer first, then enable the primary client.
#else
  m_primaryClient->enable();
  m_inputFilter->setPrimaryClient(m_primaryClient);
#endif
  // Determine if scroll lock is already set. If so, lock the cursor to the
  // primary computer (unless the user has disabled lock to computer in config)
  if (!m_disableLockToComputer && (m_primaryClient->getToggleMask() & KeyModifierScrollLock)) {
    LOG_INFO("scroll lock is on, locking cursor to computer");
    m_lockedToComputer = true;
  } else if (m_defaultLockToComputerState) {
    LOG_INFO("default computer lock is on, locking cursor to computer");
    m_lockedToComputer = true;
  }
}

Server::~Server()
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  *m_originalLifetime = false;
  if (m_originalWake) {
    m_originalWake->close();
    m_events->removeHandler(EventTypes::OriginalInputProgress, m_originalWake.get());
  }
  if (m_originalDispatcher)
    m_originalDispatcher->setProgressNotifier({});
  m_disconnectingInputClients.clear();
  revokeSelectedPeer();
  if (m_originalDispatchTimer)
    m_events->deleteTimer(m_originalDispatchTimer);
  m_events->removeHandler(EventTypes::Timer, &m_originalDispatchTimer);
  const auto dispatcher = m_originalDispatcher;
  auto pending = inputEvidenceApply(InputEvidenceQueue::ServerPending, this, 0, m_originalPending.size(), [&] {
    auto detached = std::move(m_originalPending);
    m_originalPending.clear();
    return detached;
  });
  if (dispatcher) {
    for (const auto &input : pending)
      if (input.preparation)
        dispatcher->cancelPreparation(input.preparation);
    if (!m_originalAbortSent) {
      m_originalAbortSent = true;
      dispatcher->abort();
    }
  }
  auto releases = std::move(m_originalReleases);
  m_originalReleases.clear();
  if (dispatcher)
    for (auto owner : releases)
      dispatcher->release(owner);
  m_inputFilter->setOriginalInputRoutes({});
#endif
  // remove event handlers and timers
  using enum EventTypes;
  m_events->removeHandler(KeyStateKeyDown, m_inputFilter);
  m_events->removeHandler(KeyStateKeyUp, m_inputFilter);
  m_events->removeHandler(KeyStateKeyRepeat, m_inputFilter);
  m_events->removeHandler(PrimaryComputerButtonDown, m_inputFilter);
  m_events->removeHandler(PrimaryComputerButtonUp, m_inputFilter);
  m_events->removeHandler(PrimaryComputerMotionOnPrimary, m_primaryClient->getEventTarget());
  m_events->removeHandler(PrimaryComputerMotionOnSecondary, m_primaryClient->getEventTarget());
  m_events->removeHandler(PrimaryComputerWheel, m_primaryClient->getEventTarget());
  m_events->removeHandler(PrimaryScreenSaverActivated, m_primaryClient->getEventTarget());
  m_events->removeHandler(PrimaryScreenSaverDeactivated, m_primaryClient->getEventTarget());
  m_events->removeHandler(PrimaryComputerFakeInputBegin, m_inputFilter);
  m_events->removeHandler(PrimaryComputerFakeInputEnd, m_inputFilter);
  m_events->removeHandler(Timer, this);
  stopSwitch();

  try {
    // force immediate disconnection of secondary clients
    disconnect();
  } catch (std::exception &e) { // NOSONAR
    LOG_ERR("failed to disconnect: %s", e.what());
  }

  for (auto index = m_oldClients.begin(); index != m_oldClients.end(); ++index) {
    BaseClientProxy *client = index->first;
    m_events->deleteTimer(index->second);
    m_events->removeHandler(Timer, client);
    m_events->removeHandler(ClientProxyDisconnected, client);
    delete client;
  }

  // See at Server::Server for why we do this in this order.
#if WINAPI_LIBPORTAL
  // disable and disconnect primary client
  m_primaryClient->disable();
  // remove input filter
  m_inputFilter->setPrimaryClient(nullptr);
#else
  m_inputFilter->setPrimaryClient(nullptr);
  m_primaryClient->disable();
#endif
  removeClient(m_primaryClient);
}

size_t Server::getMaximumClipboardSizeBytes() const
{
  return m_maximumClipboardSize * 1024;
}

bool Server::setConfig(const ServerConfig &config)
{
  // refuse configuration if it doesn't include the primary computer
  if (!config.isComputer(m_primaryClient->getName())) {
    return false;
  }

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  SelectionChange selection(*this);
  if (!selection.alive())
    return false;
#endif

  // close clients that are connected but being dropped from the
  // configuration.
  closeClients(config);

  // cut over
  processOptions();

  // add ScrollLock as a hotkey to lock to the computer.  this was a
  // built-in feature in earlier releases and is now supported via
  // the user configurable hotkey mechanism.  if the user has already
  // registered ScrollLock for something else then that will win but
  // we will unfortunately generate a warning.  if the user has
  // configured a LockCursorToComputerAction then we don't add
  // ScrollLock as a hotkey.
  if (!m_disableLockToComputer && !m_config->hasLockToComputerAction()) {
    IPlatformComputer::KeyInfo *key = IPlatformComputer::KeyInfo::alloc(kKeyScrollLock, 0, 0, 0);
    InputFilter::Rule rule(new InputFilter::KeystrokeCondition(m_events, key));
    rule.adoptAction(new InputFilter::LockCursorToComputerAction(m_events), true);
    m_inputFilter->addFilterRule(rule);
  }

  // tell primary computer about reconfiguration
  m_primaryClient->reconfigure(getActivePrimarySides());

  // tell all (connected) clients about current options
  for (ClientList::const_iterator index = m_clients.begin(); index != m_clients.end(); ++index) {
    BaseClientProxy *client = index->second;
    sendOptions(client);
  }

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  selection.complete();
#endif
  return true;
}

void Server::adoptClient(BaseClientProxy *client)
{
  assert(client != nullptr);

  // watch for client disconnection
  m_events->addHandler(EventTypes::ClientProxyDisconnected, client, [this, client](const auto &) {
    handleClientDisconnected(client);
  });

  // name must be in our configuration
  if (!m_config->isComputer(client->getName())) {
    LOG_WARN("unrecognised client name \"%s\", check server config", client->getName().c_str());
    ipcSendToClient("unrecognisedClient", QString::fromStdString(client->getName()));
    closeClient(client, kMsgEUnknown);
    return;
  }

  // add client to client list
  if (!addClient(client)) {
    // can only have one computer with a given name at any given time
    LOG_WARN("a client with name \"%s\" is already connected", getName(client).c_str());
    closeClient(client, kMsgEBusy);
    return;
  }
  LOG_DEBUG("client \"%s\" has connected", getName(client).c_str());
  ipcSendConnectionState(deskflow::core::ConnectionState::Connected);
  sendConnectedClientsIpc();

  // send configuration options to client
  sendOptions(client);

  // activate computer saver on new client if active on the primary computer
  if (m_activeSaver != nullptr) {
    client->screensaver(true);
  }

  // send notification
  auto *info = new Server::ComputerConnectedInfo(getName(client));
  m_events->addEvent(Event(EventTypes::ServerConnected, m_primaryClient->getEventTarget(), info));
}

void Server::disconnect()
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  SelectionChange selection(*this); // Never publish from nested client closes.
  if (!selection.alive())
    return;
#endif
  // close all secondary clients
  if (m_clients.size() > 1 || !m_oldClients.empty()) {
    Config emptyConfig(m_events);
    closeClients(emptyConfig);
  } else {
    m_events->addEvent(Event(EventTypes::ServerDisconnected, this));
  }
}

std::string Server::protocolString() const
{
  if (m_protocol == NetworkProtocol::Unknown)
    throw InvalidProtocolException();
  return networkProtocolToName(m_protocol).toStdString();
}

uint32_t Server::getNumClients() const
{
  return (int32_t)m_clients.size();
}

void Server::getClients(std::vector<std::string> &list) const
{
  list.clear();
  for (auto index = m_clients.begin(); index != m_clients.end(); ++index) {
    list.push_back(index->first);
  }
}

void Server::sendConnectedClientsIpc() const
{
  const auto primaryName = getName(m_primaryClient);
  QStringList clientList;
  for (const auto &[name, _] : m_clients) {
    if (name != primaryName) {
      clientList.append(QString::fromStdString(name));
    }
  }
  ipcSendToClient("connectedClients", clientList.join(","));
}

std::string Server::getName(const BaseClientProxy *client) const
{
  std::string name = m_config->getCanonicalName(client->getName());
  if (name.empty()) {
    name = client->getName();
  }
  return name;
}

uint32_t Server::getActivePrimarySides() const
{
  using enum DirectionMask;
  using enum Direction;
  uint32_t sides = 0;
  if (!isLockedToComputerServer()) {
    if (hasAnyNeighbor(m_primaryClient, Left)) {
      sides |= static_cast<int>(LeftMask);
    }
    if (hasAnyNeighbor(m_primaryClient, Right)) {
      sides |= static_cast<int>(RightMask);
    }
    if (hasAnyNeighbor(m_primaryClient, Top)) {
      sides |= static_cast<int>(TopMask);
    }
    if (hasAnyNeighbor(m_primaryClient, Bottom)) {
      sides |= static_cast<int>(BottomMask);
    }
  }
  return sides;
}

bool Server::isLockedToComputerServer() const
{
  // locked if scroll-lock is toggled on
  return m_lockedToComputer;
}

bool Server::isLockedToComputer() const
{
  if (m_disableLockToComputer) {
    return false;
  }

  // locked if we say we're locked
  if (isLockedToComputerServer()) {
    if (!m_defaultLockToComputerState) {
      LOG_INFO("cursor is locked to computer, check scroll lock key");
    }
    return true;
  }

  // locked if primary says we're locked
  if (m_primaryClient->isLockedToComputer()) {
    return true;
  }

  // not locked
  return false;
}

int32_t Server::getJumpZoneSize(const BaseClientProxy *client) const
{
  if (client == m_primaryClient) {
    return m_primaryClient->getJumpZoneSize();
  } else {
    return 0;
  }
}

void Server::switchComputer(BaseClientProxy *dst, int32_t x, int32_t y, bool forScreensaver)
{
  assert(dst != nullptr);

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  const auto destination = m_inputRecipients.find(dst);
  const auto recipient = destination == m_inputRecipients.end() ? 0 : destination->second;
  SelectionChange selection(*this);
  if (!selection.alive())
    return;
  if (dst != m_primaryClient && (!recipient || inputRecipient(recipient) != dst))
    return;
#endif

  int32_t dx;
  int32_t dy;
  int32_t dw;
  int32_t dh;
  dst->getShape(dx, dy, dw, dh);

  // any of these conditions seem to trigger when the portal permission dialog
  // is visible on wayland. this was previously an assert, but that's pretty
  // annoying since it makes the mouse unusable on the server and you'll have to
  // ssh into your machine to kill it. better to just log a warning.
  if (x < dx) {
    LOG_WARN(
        "on switch, x (%d) is less than the left boundary dx (%d)", //
        x, dx
    );
  }
  if (y < dy) {
    LOG_WARN(
        "on switch, y (%d) is less than the top boundary dy (%d)", //
        y, dy
    );
  }
  if (x >= dx + dw) {
    LOG_WARN(
        "on switch, x (%d) exceeds the right boundary (dx + width = %d)", //
        x, dx + dw
    );
  }
  if (y >= dy + dh) {
    LOG_WARN(
        "on switch, y (%d) exceeds the bottom boundary (dy + height = %d)", //
        y, dy + dh
    );
  }

  assert(m_active != nullptr);

  LOG_INFO("switch from \"%s\" to \"%s\" at %d,%d", getName(m_active).c_str(), getName(dst).c_str(), x, y);

  // stop waiting to switch
  stopSwitch();

  // record new position
  m_x = x;
  m_y = y;
  m_xDelta = 0;
  m_yDelta = 0;
  m_xDelta2 = 0;
  m_yDelta2 = 0;

  // wrapping means leaving the active computer and entering it again.
  // since that's a waste of time we skip that and just warp the
  // mouse.
  if (m_active != dst) {
    // leave active computer
    if (!m_active->leave()) {
      // cannot leave computer
      LOG_WARN("can't leave computer");
      return;
    }

    // update the primary client's clipboards if we're leaving the
    // primary computer.
    if (m_active == m_primaryClient && m_enableClipboard) {
      for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
        const ClipboardInfo &clipboard = m_clipboards[id];
        if (clipboard.m_clipboardOwner == getName(m_primaryClient)) {
          onClipboardChanged(m_primaryClient, id, clipboard.m_clipboardSeqNum);
        }
      }
    }

#if defined(__APPLE__)
    if (dst != m_primaryClient) {
      std::string secureInputApplication = m_primaryClient->getSecureInputApp();
      if (secureInputApplication != "") {
        // display notification on the server
        m_primaryClient->secureInputNotification(secureInputApplication);
        // display notification on the client
        dst->secureInputNotification(secureInputApplication);
      }
    }
#endif

    // cut over
    m_active = dst;

    // increment enter sequence number
    ++m_seqNum;

    // enter new computer
    auto enterMask = m_primaryClient->getToggleMask();
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    // Legacy peers may synchronize this mask on entry. A locally held modifier
    // has no remote physical down owner yet; only synchronize toggle state.
    // Desired masks on subsequent key events are tracked separately below.
    if (m_active != m_primaryClient)
      enterMask &= routedToggleModifiers;
#endif
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    observeOriginalSend(m_active);
#endif
    m_active->enter(x, y, m_seqNum, enterMask, forScreensaver);

    if (m_enableClipboard) {
      // send the clipboard data to new active computer
      for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
        // Hackity hackity hack
        if (m_clipboards[id].m_clipboard.marshall().size() > (m_maximumClipboardSize * 1024)) {
          continue;
        }
        m_active->setClipboard(id, &m_clipboards[id].m_clipboard);
      }
    }

    auto *info = new Server::SwitchToComputerInfo(m_active->getName());
    m_events->addEvent(Event(EventTypes::ServerComputerSwitched, this, info));
  } else {
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    observeOriginalSend(m_active);
#endif
    m_active->mouseMove(x, y);
  }
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (!forScreensaver)
    selection.complete();
#endif
}

void Server::jumpToComputer(BaseClientProxy *newComputer)
{
  assert(newComputer != nullptr);

  // record the current cursor position on the active computer
  m_active->setJumpCursorPos(m_x, m_y);

  // get the last cursor position on the target computer
  int32_t x;
  int32_t y;
  newComputer->getJumpCursorPos(x, y);

  switchComputer(newComputer, x, y, false);
}

float Server::mapToFraction(const BaseClientProxy *client, Direction dir, int32_t x, int32_t y) const
{
  int32_t sx;
  int32_t sy;
  int32_t sw;
  int32_t sh;
  client->getShape(sx, sy, sw, sh);
  switch (dir) {
    using enum Direction;
  case Left:
  case Right:
    return (y - sy + 0.5f) / static_cast<float>(sh);

  case Top:
  case Bottom:
    return (x - sx + 0.5f) / static_cast<float>(sw);

  case NoDirection:
    assert(0 && "bad direction");
    break;
  }
  return 0.0f;
}

void Server::mapToPixel(const BaseClientProxy *client, Direction dir, float f, int32_t &x, int32_t &y) const
{
  int32_t sx;
  int32_t sy;
  int32_t sw;
  int32_t sh;
  client->getShape(sx, sy, sw, sh);
  switch (dir) {
    using enum Direction;
  case Left:
  case Right:
    y = static_cast<int32_t>(f * sh) + sy;
    break;

  case Top:
  case Bottom:
    x = static_cast<int32_t>(f * sw) + sx;
    break;

  case NoDirection:
    assert(0 && "bad direction");
    break;
  }
}

bool Server::hasAnyNeighbor(const BaseClientProxy *client, Direction dir) const
{
  assert(client != nullptr);

  return m_config->hasNeighbor(getName(client), dir);
}

BaseClientProxy *Server::getNeighbor(const BaseClientProxy *src, Direction dir, int32_t &x, int32_t &y) const
{
  // note -- must be locked on entry

  assert(src != nullptr);

  // get source computer name
  std::string srcName = getName(src);
  assert(!srcName.empty());
  LOG_VERBOSE("find neighbor on %s of \"%s\"", Config::dirName(dir), srcName.c_str());

  // convert position to fraction
  float t = mapToFraction(src, dir, x, y);

  // search for the closest neighbor that exists in direction dir
  float tTmp;
  for (;;) {
    std::string dstName(m_config->getNeighbor(srcName, dir, t, &tTmp));

    // if nothing in that direction then return nullptr. if the
    // destination is the source then we can make no more
    // progress in this direction.  since we haven't found a
    // connected neighbor we return nullptr.
    if (dstName.empty()) {
      LOG_VERBOSE("no neighbor on %s of \"%s\"", Config::dirName(dir), srcName.c_str());
      return nullptr;
    }

    // look up neighbor cell.  if the computer is connected and
    // ready then we can stop.
    if (ClientList::const_iterator index = m_clients.find(dstName); index != m_clients.end()) {
      LOG_VERBOSE("\"%s\" is on %s of \"%s\" at %f", dstName.c_str(), Config::dirName(dir), srcName.c_str(), t);
      mapToPixel(index->second, dir, tTmp, x, y);
      return index->second;
    }

    // skip over unconnected computer
    LOG_VERBOSE("ignored \"%s\" on %s of \"%s\"", dstName.c_str(), Config::dirName(dir), srcName.c_str());
    srcName = dstName;

    // use position on skipped computer
    t = tTmp;
  }
}

BaseClientProxy *Server::mapToNeighbor(BaseClientProxy *src, Direction srcSide, int32_t &x, int32_t &y) const
{
  // note -- must be locked on entry

  assert(src != nullptr);

  // get the first neighbor
  BaseClientProxy *dst = getNeighbor(src, srcSide, x, y);
  if (dst == nullptr) {
    return nullptr;
  }

  // get the source computer's size
  int32_t dx;
  int32_t dy;
  int32_t dw;
  int32_t dh;
  BaseClientProxy *lastGoodComputer = src;
  lastGoodComputer->getShape(dx, dy, dw, dh);

  // find destination computer, adjusting x or y (but not both).  the
  // searches are done in a sort of canonical computer space where
  // the upper-left corner is 0,0 for each computer.  we adjust from
  // actual to canonical position on entry to and from canonical to
  // actual on exit from the search.
  switch (srcSide) {
    using enum Direction;
  case Left:
    x -= dx;
    while (dst != nullptr) {
      lastGoodComputer = dst;
      lastGoodComputer->getShape(dx, dy, dw, dh);
      x += dw;
      if (x >= 0) {
        break;
      }
      LOG_VERBOSE("skipping over computer %s", getName(dst).c_str());
      dst = getNeighbor(lastGoodComputer, srcSide, x, y);
    }
    assert(lastGoodComputer != nullptr);
    x += dx;
    break;

  case Right:
    x -= dx;
    while (dst != nullptr) {
      x -= dw;
      lastGoodComputer = dst;
      lastGoodComputer->getShape(dx, dy, dw, dh);
      if (x < dw) {
        break;
      }
      LOG_VERBOSE("skipping over computer %s", getName(dst).c_str());
      dst = getNeighbor(lastGoodComputer, srcSide, x, y);
    }
    assert(lastGoodComputer != nullptr);
    x += dx;
    break;

  case Top:
    y -= dy;
    while (dst != nullptr) {
      lastGoodComputer = dst;
      lastGoodComputer->getShape(dx, dy, dw, dh);
      y += dh;
      if (y >= 0) {
        break;
      }
      LOG_VERBOSE("skipping over computer %s", getName(dst).c_str());
      dst = getNeighbor(lastGoodComputer, srcSide, x, y);
    }
    assert(lastGoodComputer != nullptr);
    y += dy;
    break;

  case Bottom:
    y -= dy;
    while (dst != nullptr) {
      y -= dh;
      lastGoodComputer = dst;
      lastGoodComputer->getShape(dx, dy, dw, dh);
      if (y < dh) {
        break;
      }
      LOG_VERBOSE("skipping over computer %s", getName(dst).c_str());
      dst = getNeighbor(lastGoodComputer, srcSide, x, y);
    }
    assert(lastGoodComputer != nullptr);
    y += dy;
    break;

  case NoDirection:
    assert(0 && "bad direction");
    return nullptr;
  }

  // save destination computer
  assert(lastGoodComputer != nullptr);
  dst = lastGoodComputer;

  // if entering primary computer then be sure to move in far enough
  // to avoid the jump zone.  if entering a side that doesn't have
  // a neighbor (i.e. an asymmetrical side) then we don't need to
  // move inwards because that side can't provoke a jump.
  avoidJumpZone(dst, srcSide, x, y);

  return dst;
}

void Server::avoidJumpZone(const BaseClientProxy *dst, Direction dir, int32_t &x, int32_t &y) const
{
  // we only need to avoid jump zones on the primary computer
  if (dst != m_primaryClient) {
    return;
  }

  const std::string dstName(getName(dst));
  int32_t dx;
  int32_t dy;
  int32_t dw;
  int32_t dh;
  dst->getShape(dx, dy, dw, dh);
  float t = mapToFraction(dst, dir, x, y);
  int32_t z = getJumpZoneSize(dst);

  // move in far enough to avoid the jump zone.  if entering a side
  // that doesn't have a neighbor (i.e. an asymmetrical side) then we
  // don't need to move inwards because that side can't provoke a jump.
  switch (dir) {
    using enum Direction;
  case Left:
    if (!m_config->getNeighbor(dstName, Right, t, nullptr).empty() && x > dx + dw - 1 - z)
      x = dx + dw - 1 - z;
    break;

  case Right:
    if (!m_config->getNeighbor(dstName, Left, t, nullptr).empty() && x < dx + z)
      x = dx + z;
    break;

  case Top:
    if (!m_config->getNeighbor(dstName, Bottom, t, nullptr).empty() && y > dy + dh - 1 - z)
      y = dy + dh - 1 - z;
    break;

  case Bottom:
    if (!m_config->getNeighbor(dstName, Top, t, nullptr).empty() && y < dy + z)
      y = dy + z;
    break;

  case NoDirection:
    assert(0 && "bad direction");
  }
}

bool Server::isSwitchOkay(
    BaseClientProxy *newComputer, Direction dir, int32_t x, int32_t y, int32_t xActive, int32_t yActive,
    double *dragWait
)
{
  if (dragWait)
    *dragWait = -1;
  LOG_VERBOSE("try to leave \"%s\" on %s", getName(m_active).c_str(), Config::dirName(dir));

  // is there a neighbor?
  if (newComputer == nullptr) {
    // there's no neighbor.  we don't want to switch and we don't
    // want to try to switch later.
    LOG_VERBOSE("no neighbor %s", Config::dirName(dir));
    stopSwitch();
    return false;
  }

  // should we switch or not?
  bool preventSwitch = false;
  bool allowSwitch = false;

  // note if the switch direction has changed.  save the new
  // direction and computer if so.
  bool isNewDirection = (dir != m_switchDir);
  if (isNewDirection || m_switchComputer == nullptr) {
    m_switchDir = dir;
    m_switchComputer = newComputer;
  }

  // is this a double tap and do we care?
  if (!allowSwitch && m_switchTwoTapDelay > 0.0) {
    if (isNewDirection || !isSwitchTwoTapStarted() || !shouldSwitchTwoTap()) {
      // tapping a different or new edge or second tap not
      // fast enough.  prepare for second tap.
      preventSwitch = true;
      startSwitchTwoTap();
    } else {
      // got second tap
      allowSwitch = true;
    }
  }

  bool edgePreventSwitch = preventSwitch;
  // if waiting before a switch then prepare to switch later
  if (!allowSwitch && m_switchWaitDelay > 0.0) {
    if (isNewDirection || !isSwitchWaitStarted()) {
      startSwitchWait(x, y);
    }
    preventSwitch = true;
  }

  // are we in a locked corner?  first check if computer has the option set
  // and, if not, check the global options.
  const Config::ScreenOptions *options = m_config->getOptions(getName(m_active));
  if (options == nullptr || !options->contains(kOptionComputerSwitchCorners)) {
    options = m_config->getOptions("");
  }
  if (options != nullptr && options->contains(kOptionComputerSwitchCorners)) {
    // get corner mask and size
    Config::ScreenOptions::const_iterator i = options->find(kOptionComputerSwitchCorners);
    auto corners = static_cast<uint32_t>(i->second);
    i = options->find(kOptionComputerSwitchCornerSize);
    int32_t size = 0;
    if (i != options->end()) {
      size = i->second;
    }
    // see if we're in a locked corner
    if ((getCorner(m_active, xActive, yActive, size) & corners) != 0) {
      // yep, no switching
      LOG_VERBOSE("locked in corner");
      preventSwitch = true;
      edgePreventSwitch = true;
      stopSwitch();
    }
  }

  // Observation of a held drag uses the same configured delays/corners, but
  // cannot treat the physical button lock as permission to leave the source.
  if (dragWait && !edgePreventSwitch && !isLockedToComputerServer())
    *dragWait = allowSwitch ? 0 : m_switchWaitDelay;

  // ignore if mouse is locked to computer and don't try to switch later
  if (!preventSwitch && isLockedToComputer()) {
    LOG_VERBOSE("locked to computer");
    preventSwitch = true;
    stopSwitch();
  }

  return !preventSwitch;
}

void Server::noSwitch(int32_t x, int32_t y)
{
  armSwitchTwoTap(x, y);
  stopSwitchWait();
}

void Server::stopSwitch()
{
  if (m_switchComputer != nullptr) {
    m_switchComputer = nullptr;
    m_switchDir = Direction::NoDirection;
    stopSwitchTwoTap();
    stopSwitchWait();
  }
}

void Server::startSwitchTwoTap()
{
  m_switchTwoTapEngaged = true;
  m_switchTwoTapArmed = false;
  m_switchTwoTapTimer.reset();
  LOG_VERBOSE("waiting for second tap");
}

void Server::armSwitchTwoTap(int32_t x, int32_t y)
{
  if (m_switchTwoTapEngaged) {
    if (m_switchTwoTapTimer.getTime() > m_switchTwoTapDelay) {
      // second tap took too long.  disengage.
      stopSwitchTwoTap();
    } else if (!m_switchTwoTapArmed) {
      // still time for a double tap.  see if we left the tap
      // zone and, if so, arm the two tap.
      int32_t ax;
      int32_t ay;
      int32_t aw;
      int32_t ah;
      m_active->getShape(ax, ay, aw, ah);
      int32_t tapZone = m_primaryClient->getJumpZoneSize();
      if (tapZone < m_switchTwoTapZone) {
        tapZone = m_switchTwoTapZone;
      }
      if (x >= ax + tapZone && x < ax + aw - tapZone && y >= ay + tapZone && y < ay + ah - tapZone) {
        // win32 can generate bogus mouse events that appear to
        // move in the opposite direction that the mouse actually
        // moved.  try to ignore that crap here.
        switch (m_switchDir) {
          using enum Direction;
        case Left:
          m_switchTwoTapArmed = (m_xDelta > 0 && m_xDelta2 > 0);
          break;

        case Right:
          m_switchTwoTapArmed = (m_xDelta < 0 && m_xDelta2 < 0);
          break;

        case Top:
          m_switchTwoTapArmed = (m_yDelta > 0 && m_yDelta2 > 0);
          break;

        case Bottom:
          m_switchTwoTapArmed = (m_yDelta < 0 && m_yDelta2 < 0);
          break;

        default:
          break;
        }
      }
    }
  }
}

void Server::stopSwitchTwoTap()
{
  m_switchTwoTapEngaged = false;
  m_switchTwoTapArmed = false;
}

bool Server::isSwitchTwoTapStarted() const
{
  return m_switchTwoTapEngaged;
}

bool Server::shouldSwitchTwoTap() const
{
  // this is the second tap if two-tap is armed and this tap
  // came fast enough
  return (m_switchTwoTapArmed && m_switchTwoTapTimer.getTime() <= m_switchTwoTapDelay);
}

void Server::startSwitchWait(int32_t x, int32_t y)
{
  stopSwitchWait();
  m_switchWaitX = x;
  m_switchWaitY = y;
  m_switchWaitTimer = m_events->newOneShotTimer(m_switchWaitDelay, this);
  LOG_VERBOSE("waiting to switch");
}

void Server::stopSwitchWait()
{
  if (m_switchWaitTimer != nullptr) {
    m_events->deleteTimer(m_switchWaitTimer);
    m_switchWaitTimer = nullptr;
  }
}

bool Server::isSwitchWaitStarted() const
{
  return (m_switchWaitTimer != nullptr);
}

uint32_t Server::getCorner(const BaseClientProxy *client, int32_t x, int32_t y, int32_t size) const
{
  assert(client != nullptr);

  // get client computer shape
  int32_t ax;
  int32_t ay;
  int32_t aw;
  int32_t ah;
  client->getShape(ax, ay, aw, ah);

  // check for x,y on the left or right
  int32_t xSide;
  if (x <= ax) {
    xSide = -1;
  } else if (x >= ax + aw - 1) {
    xSide = 1;
  } else {
    xSide = 0;
  }

  // check for x,y on the top or bottom
  int32_t ySide;
  if (y <= ay) {
    ySide = -1;
  } else if (y >= ay + ah - 1) {
    ySide = 1;
  } else {
    ySide = 0;
  }

  // if against the left or right then check if y is within size
  if (xSide != 0) {
    if (y < ay + size) {
      return (xSide < 0) ? s_topLeftCornerMask : s_topRightCornerMask;
    } else if (y >= ay + ah - size) {
      return (xSide < 0) ? s_bottomLeftCornerMask : s_bottomRightCornerMask;
    }
  }

  // if against the left or right then check if y is within size
  if (ySide != 0) {
    if (x < ax + size) {
      return (ySide < 0) ? s_topLeftCornerMask : s_bottomLeftCornerMask;
    } else if (x >= ax + aw - size) {
      return (ySide < 0) ? s_topRightCornerMask : s_bottomRightCornerMask;
    }
  }

  return s_noCornerMask;
}

void Server::stopRelativeMoves()
{
  if (m_relativeMoves && m_active != m_primaryClient) {
    // warp to the center of the active client so we know where we are
    int32_t ax;
    int32_t ay;
    int32_t aw;
    int32_t ah;
    m_active->getShape(ax, ay, aw, ah);
    m_x = ax + (aw >> 1);
    m_y = ay + (ah >> 1);
    m_xDelta = 0;
    m_yDelta = 0;
    m_xDelta2 = 0;
    m_yDelta2 = 0;
    LOG_VERBOSE("synchronize move on %s by %d,%d", getName(m_active).c_str(), m_x, m_y);
    m_active->mouseMove(m_x, m_y);
  }
}

void Server::sendOptions(BaseClientProxy *client) const
{
  OptionsList optionsList;

  // look up options for client
  const Config::ScreenOptions *options = m_config->getOptions(getName(client));
  if (options != nullptr) {
    // convert options to a more convenient form for sending
    optionsList.reserve(2 * options->size());
    for (auto [optionId, optionValue] : *options) {
      if (optionId == kOptionInputSequenceTrace || optionId == kOptionInputCutoff ||
          optionId == kOptionMappedInputCutoff || optionId == kOptionSealedInputCutoff ||
          optionId == kOptionBatchedInputCutoff || optionId == kOptionInputPreparation)
        continue; // Reserved for the live capture route, never configuration.
      optionsList.push_back(optionId);
      optionsList.push_back(static_cast<uint32_t>(optionValue));
    }
  }

  // look up global options
  options = m_config->getOptions("");
  if (options != nullptr) {
    // convert options to a more convenient form for sending
    optionsList.reserve(optionsList.size() + 2 * options->size());
    for (auto [optionId, optionValue] : *options) {
      if (optionId == kOptionInputSequenceTrace || optionId == kOptionInputCutoff ||
          optionId == kOptionMappedInputCutoff || optionId == kOptionSealedInputCutoff ||
          optionId == kOptionBatchedInputCutoff || optionId == kOptionInputPreparation)
        continue;
      optionsList.push_back(optionId);
      optionsList.push_back(static_cast<uint32_t>(optionValue));
    }
  }

  // send the options
  client->resetOptions();
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (m_originalInputRoutes && m_originalInputRoutes->live()) {
    if (const auto generation = client->requestInputSequenceTrace()) {
      optionsList.push_back(kOptionInputSequenceTrace);
      optionsList.push_back(*generation);
    }
    if (const auto generation = client->requestBatchedInputCutoffCapability()) {
      optionsList.push_back(kOptionBatchedInputCutoff);
      optionsList.push_back(*generation);
    }
    if (const auto generation = client->requestInputPreparationCapability()) {
      optionsList.push_back(kOptionInputPreparation);
      optionsList.push_back(*generation);
    }
  }
#endif
  client->setOptions(optionsList);
}

void Server::processOptions()
{
  const Config::ScreenOptions *options = m_config->getOptions("");
  if (options == nullptr) {
    return;
  }

  bool newRelativeMoves = m_relativeMoves;
  for (auto [optionId, optionValue] : *options) {
    const OptionID id = optionId;
    const OptionValue value = optionValue;
    if (id == kOptionComputerSwitchDelay) {
      m_switchWaitDelay = 1.0e-3 * static_cast<double>(value);
      if (m_switchWaitDelay < 0.0) {
        m_switchWaitDelay = 0.0;
      }
      stopSwitchWait();
    } else if (id == kOptionComputerSwitchTwoTap) {
      m_switchTwoTapDelay = 1.0e-3 * static_cast<double>(value);
      if (m_switchTwoTapDelay < 0.0) {
        m_switchTwoTapDelay = 0.0;
      }
      stopSwitchTwoTap();
    } else if (id == kOptionRelativeMouseMoves) {
      newRelativeMoves = (value != 0);
    } else if (id == kOptionDefaultLockToComputerState) {
      m_defaultLockToComputerState = (value != 0);
    } else if (id == kOptionDisableLockToComputer) {
      m_disableLockToComputer = (value != 0);
    } else if (id == kOptionClipboardSharing) {
      m_enableClipboard = value;
      if (!m_enableClipboard) {
        LOG_INFO("clipboard sharing is disabled");
      }
    } else if (id == kOptionClipboardSharingSize) {
      if (value <= 0) {
        m_maximumClipboardSize = 0;
        LOG_INFO(
            "clipboard sharing is disabled because the "
            "maximum shared clipboard size is set to 0"
        );
      } else {
        m_maximumClipboardSize = static_cast<size_t>(value);
      }
    }
  }
  if (m_relativeMoves && !newRelativeMoves) {
    stopRelativeMoves();
  }
  m_relativeMoves = newRelativeMoves;
  m_protocol = Settings::networkProtocol();
}

void Server::handleShapeChanged(BaseClientProxy *client)
{
  if (!m_clientSet.contains(client)) {
    return;
  }
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  invalidateWindowDragEdge(true);
#endif

  LOG_DEBUG("computer \"%s\" shape changed", getName(client).c_str());

  // update jump coordinate
  int32_t x;
  int32_t y;
  client->getCursorPos(x, y);
  client->setJumpCursorPos(x, y);

  // update the mouse coordinates
  if (client == m_active) {
    m_x = x;
    m_y = y;
  }

  // handle resolution change to primary computer
  if (client == m_primaryClient) {
    if (client == m_active) {
      onMouseMovePrimary(m_x, m_y);
    } else {
      onMouseMoveSecondary(0, 0);
    }
  }
}

void Server::handleClipboardGrabbed(const Event &event, BaseClientProxy *grabber)
{
  if (!m_enableClipboard || (m_maximumClipboardSize == 0)) {
    return;
  }

  // ignore events from unknown clients
  if (!m_clientSet.contains(grabber)) {
    return;
  }
  const auto *info = static_cast<const IComputer::ClipboardInfo *>(event.getData());

  // ignore grab if sequence number is old.  always allow primary
  // computer to grab.
  ClipboardInfo &clipboard = m_clipboards[info->m_id];
  if (grabber != m_primaryClient && info->m_sequenceNumber < clipboard.m_clipboardSeqNum) {
    LOG_DEBUG("ignored computer \"%s\" grab of clipboard %d", getName(grabber).c_str(), info->m_id);
    return;
  }

  // mark computer as owning clipboard
  LOG_DEBUG(
      "computer \"%s\" grabbed clipboard %d from \"%s\"", getName(grabber).c_str(), info->m_id,
      clipboard.m_clipboardOwner.c_str()
  );
  clipboard.m_clipboardOwner = getName(grabber);
  clipboard.m_clipboardSeqNum = info->m_sequenceNumber;

  // clear the clipboard data (since it's not known at this point)
  if (clipboard.m_clipboard.open(0)) {
    clipboard.m_clipboard.empty();
    clipboard.m_clipboard.close();
  }
  clipboard.m_clipboardData = clipboard.m_clipboard.marshall();

  // tell all other computers to take ownership of clipboard.  tell the
  // grabber that it's clipboard isn't dirty.
  for (auto index = m_clients.begin(); index != m_clients.end(); ++index) {
    BaseClientProxy *client = index->second;
    if (client == grabber) {
      client->setClipboardDirty(info->m_id, false);
    } else {
      client->grabClipboard(info->m_id);
    }
  }

  if (grabber == m_primaryClient && m_active != m_primaryClient) {
    LOG_DEBUG("clipboard grabbed while active computer was changed, resending clipboard data");
    onClipboardChanged(m_primaryClient, info->m_id, clipboard.m_clipboardSeqNum);
  }
}

void Server::handleClipboardChanged(const Event &event, BaseClientProxy *client)
{
  // ignore events from unknown clients
  if (!m_clientSet.contains(client)) {
    return;
  }
  const auto *info = static_cast<const IComputer::ClipboardInfo *>(event.getData());
  onClipboardChanged(client, info->m_id, info->m_sequenceNumber);
}

void Server::handleKeyDownEvent(const Event &event)
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  OriginalRouteObservation observation(*this, event, deskflow::windowshare::OriginalInputRouteKind::Key);
#endif
  const auto *info = static_cast<IPlatformComputer::KeyInfo *>(event.getData());
  auto lang = AppUtil::instance().getCurrentLanguageCode();
  onKeyDown(info->m_key, info->m_mask, info->m_button, lang, info->m_computers.c_str());
}

void Server::handleKeyUpEvent(const Event &event)
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  OriginalRouteObservation observation(*this, event, deskflow::windowshare::OriginalInputRouteKind::Key);
#endif
  auto *info = static_cast<IPlatformComputer::KeyInfo *>(event.getData());
  onKeyUp(info->m_key, info->m_mask, info->m_button, info->m_computers.c_str());
}

void Server::handleKeyRepeatEvent(const Event &event)
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  OriginalRouteObservation observation(*this, event, deskflow::windowshare::OriginalInputRouteKind::Key);
#endif
  const auto *info = static_cast<IPlatformComputer::KeyInfo *>(event.getData());
  auto lang = AppUtil::instance().getCurrentLanguageCode();
  onKeyRepeat(info->m_key, info->m_mask, info->m_count, info->m_button, lang);
}

void Server::handleButtonDownEvent(const Event &event)
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  OriginalRouteObservation observation(*this, event, deskflow::windowshare::OriginalInputRouteKind::Button);
#endif
  const auto *info = static_cast<IPlatformComputer::ButtonInfo *>(event.getData());
  onMouseDown(info->m_button);
}

void Server::handleButtonUpEvent(const Event &event)
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  OriginalRouteObservation observation(*this, event, deskflow::windowshare::OriginalInputRouteKind::Button);
#endif
  const auto *info = static_cast<IPlatformComputer::ButtonInfo *>(event.getData());
  onMouseUp(info->m_button);
}

void Server::handleMotionPrimaryEvent(const Event &event)
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  OriginalRouteObservation observation(*this, event, deskflow::windowshare::OriginalInputRouteKind::Motion);
#endif
  const auto *info = static_cast<IPlatformComputer::MotionInfo *>(event.getData());
  onMouseMovePrimary(info->m_x, info->m_y);
}

void Server::handleMotionSecondaryEvent(const Event &event)
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  OriginalRouteObservation observation(*this, event, deskflow::windowshare::OriginalInputRouteKind::Motion);
#endif
  const auto *info = static_cast<IPlatformComputer::MotionInfo *>(event.getData());
  onMouseMoveSecondary(info->m_x, info->m_y);
}

void Server::handleWheelEvent(const Event &event)
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  OriginalRouteObservation observation(*this, event, deskflow::windowshare::OriginalInputRouteKind::Scroll);
#endif
  const auto *info = static_cast<IPlatformComputer::WheelInfo *>(event.getData());
  onMouseWheel(info->m_xDelta, info->m_yDelta);
}

void Server::handleSwitchWaitTimeout()
{
  // ignore if mouse is locked to computer
  if (isLockedToComputer()) {
    LOG_VERBOSE("locked to computer");
    stopSwitch();
    return;
  }

  // switch computer
  switchComputer(m_switchComputer, m_switchWaitX, m_switchWaitY, false);
}

void Server::handleClientDisconnected(BaseClientProxy *client)
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if ((!m_clientSet.contains(client) && !m_oldClients.contains(client)) ||
      !m_disconnectingInputClients.insert(client).second)
    return;
  forgetInputRecipient(client, false);
  SelectionChange selection(*this);
  if (!selection.alive())
    return;
#endif
  // client has disconnected.  it might be an old client or an
  // active client.  we don't care so just handle it both ways.
  removeActiveClient(client);
  removeOldClient(client);

  // m_clients always contains the primary (server) computer, so 1 means no remote clients.
  using enum deskflow::core::ConnectionState;
  ipcSendConnectionState(m_clients.size() <= 1 ? Listening : Connected);
  sendConnectedClientsIpc();

  delete client;
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (!selection.alive())
    return;
  m_disconnectingInputClients.erase(client);
  selection.complete();
#endif
}

void Server::handleClientCloseTimeout(BaseClientProxy *client)
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (m_disconnectingInputClients.contains(client) || !m_oldClients.contains(client))
    return;
#endif
  // client took too long to disconnect.  just dump it.
  LOG_INFO("forced disconnection of client \"%s\"", getName(client).c_str());
  removeOldClient(client);

  delete client;
}

void Server::handleSwitchToComputerEvent(const Event &event)
{
  const auto *info = static_cast<SwitchToComputerInfo *>(event.getDataObject());

  ClientList::const_iterator index = m_clients.find(info->m_computer);
  if (index == m_clients.end()) {
    LOG_VERBOSE("computer \"%s\" not active", info->m_computer.c_str());
  } else {
    jumpToComputer(index->second);
  }
}

void Server::handleSwitchInDirectionEvent(const Event &event)
{
  const auto *info = static_cast<SwitchInDirectionInfo *>(event.getDataObject());

  // jump to computer in chosen direction from center of this computer
  int32_t x = m_x;
  int32_t y = m_y;
  BaseClientProxy *newComputer = getNeighbor(m_active, info->m_direction, x, y);
  if (newComputer == nullptr) {
    LOG_VERBOSE("no neighbor %s", Config::dirName(info->m_direction));
  } else {
    jumpToComputer(newComputer);
  }
}

void Server::handleToggleComputerEvent(const Event &)
{
  // Get the list of connected computers in config order
  std::vector<std::string> computers;
  getClients(computers);

  if (computers.size() < 2) {
    LOG_ERR("not enough computers to toggle");
    return;
  }

  // Find the current active computer
  std::string currentComputer = getName(m_active);
  auto it = std::ranges::find(computers, currentComputer);
  if (it == computers.end()) {
    LOG_ERR("current computer not found in list");
    return;
  }

  // Find the next computer
  auto nextIt = it + 1;
  if (nextIt == computers.end()) {
    nextIt = computers.begin();
  }

  // Find the client for the next computer
  ClientList::const_iterator clientIt = m_clients.find(*nextIt);
  if (clientIt == m_clients.end()) {
    LOG_ERR("next computer not active");
    return;
  }

  jumpToComputer(clientIt->second);
}

void Server::handleKeyboardBroadcastEvent(const Event &event)
{
  const auto *info = static_cast<KeyboardBroadcastInfo *>(event.getDataObject());

  // choose new state
  bool newState;
  switch (info->m_state) {
  default:
  case KeyboardBroadcastInfo::kOn:
    newState = true;
    break;

  case KeyboardBroadcastInfo::kOff:
    newState = false;
    break;

  case KeyboardBroadcastInfo::kToggle:
    newState = !m_keyboardBroadcasting;
    break;
  }

  // enter new state
  if (newState != m_keyboardBroadcasting || info->m_computers != m_keyboardBroadcastingComputers) {
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    SelectionChange selection(*this);
    if (!selection.alive())
      return;
#endif
    m_keyboardBroadcasting = newState;
    m_keyboardBroadcastingComputers = info->m_computers;
    LOG(
        (CLOG_DEBUG "keyboard broadcasting %s: %s", m_keyboardBroadcasting ? "on" : "off",
         m_keyboardBroadcastingComputers.c_str())
    );
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    selection.complete();
#endif
  }
}

void Server::handleLockCursorToComputerEvent(const Event &event)
{
  const auto *info = static_cast<LockCursorToComputerInfo *>(event.getDataObject());

  // choose new state
  bool newState;
  switch (info->m_state) {
  default:
  case LockCursorToComputerInfo::kOn:
    newState = true;
    break;

  case LockCursorToComputerInfo::kOff:
    newState = false;
    break;

  case LockCursorToComputerInfo::kToggle:
    newState = !m_lockedToComputer;
    break;
  }

  // enter new state
  if (newState != m_lockedToComputer) {
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    invalidateWindowDragEdge(true);
#endif
    m_lockedToComputer = newState;
    LOG_INFO("cursor %s current computer", m_lockedToComputer ? "locked to" : "unlocked from");

    m_primaryClient->reconfigure(getActivePrimarySides());
    if (!isLockedToComputerServer()) {
      stopRelativeMoves();
    }
  }
}

void Server::onClipboardChanged(const BaseClientProxy *sender, ClipboardID id, uint32_t seqNum)
{
  ClipboardInfo &clipboard = m_clipboards[id];

  // ignore update if sequence number is old
  if (seqNum < clipboard.m_clipboardSeqNum) {
    LOG_INFO("ignored computer \"%s\" update of clipboard %d (mis-sequenced)", getName(sender).c_str(), id);
    return;
  }

  // should be the expected client
  assert(sender == m_clients.find(clipboard.m_clipboardOwner)->second);

  // get data
  sender->getClipboard(id, &clipboard.m_clipboard);

  std::string data = clipboard.m_clipboard.marshall();
  if (data.size() > m_maximumClipboardSize * 1024) {
    LOG_WARN("not sending clipboard data, exceeds limit: %i KB", m_maximumClipboardSize);
    return;
  }

  // ignore if data hasn't changed
  if (data == clipboard.m_clipboardData) {
    LOG_DEBUG("ignored computer \"%s\" update of clipboard %d (unchanged)", clipboard.m_clipboardOwner.c_str(), id);
    return;
  }

  // got new data
  LOG_INFO("computer \"%s\" updated clipboard %d", clipboard.m_clipboardOwner.c_str(), id);
  clipboard.m_clipboardData = data;

  // tell all clients except the sender that the clipboard is dirty
  for (ClientList::const_iterator index = m_clients.begin(); index != m_clients.end(); ++index) {
    BaseClientProxy *client = index->second;
    client->setClipboardDirty(id, client != sender);
  }

  // send the new clipboard to the active computer
  m_active->setClipboard(id, &clipboard.m_clipboard);
}

void Server::onScreensaver(bool activated)
{
  LOG_DEBUG("onScreenSaver %s", activated ? "activated" : "deactivated");

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  SelectionChange selection(*this);
  if (!selection.alive())
    return;
  // m_activeSaver may be cleared when that client disconnects. Keep an
  // independent guard until the actual screensaver-deactivation event.
  m_selectionScreensaver = activated;
#endif

  if (activated) {
    // save current computer and position
    m_activeSaver = m_active;
    m_xSaver = m_x;
    m_ySaver = m_y;

    // jump to primary computer
    if (m_active != m_primaryClient) {
      switchComputer(m_primaryClient, 0, 0, true);
    }
  } else {
    // jump back to previous computer and position.  we must check
    // that the position is still valid since the computer may have
    // changed resolutions while the computer saver was running.
    if (m_activeSaver != nullptr && m_activeSaver != m_primaryClient) {
      // check position
      BaseClientProxy *computer = m_activeSaver;
      int32_t x;
      int32_t y;
      int32_t w;
      int32_t h;
      computer->getShape(x, y, w, h);
      int32_t zoneSize = getJumpZoneSize(computer);
      if (m_xSaver < x + zoneSize) {
        m_xSaver = x + zoneSize;
      } else if (m_xSaver >= x + w - zoneSize) {
        m_xSaver = x + w - zoneSize - 1;
      }
      if (m_ySaver < y + zoneSize) {
        m_ySaver = y + zoneSize;
      } else if (m_ySaver >= y + h - zoneSize) {
        m_ySaver = y + h - zoneSize - 1;
      }

      // jump
      switchComputer(computer, m_xSaver, m_ySaver, false);
    }

    // reset state
    m_activeSaver = nullptr;
  }

  // send message to all clients
  for (ClientList::const_iterator index = m_clients.begin(); index != m_clients.end(); ++index) {
    BaseClientProxy *client = index->second;
    client->screensaver(activated);
  }
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  selection.complete();
#endif
}

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
bool Server::setOriginalInputRoutes(std::shared_ptr<deskflow::windowshare::OriginalInputRoutes> routes) noexcept
{
  if (!routes || m_originalInputRoutes || m_selectionShutdown || !routes->bind(m_serverIncarnation))
    return false;
  m_originalInputRoutes = std::move(routes);
  m_inputFilter->setOriginalInputRoutes(m_originalInputRoutes);
  return true;
}

bool Server::setOriginalInputCapturedInbox(std::shared_ptr<deskflow::windowshare::OriginalInputCapturedInbox> inbox)
{
  if (!inbox || m_originalInputCaptured || m_selectionShutdown || !inbox->bind(m_serverIncarnation))
    return false;
  m_originalInputCaptured = std::move(inbox);
  m_events->addHandler(EventTypes::Timer, &m_originalDispatchTimer, [this](const auto &) { drainOriginalInput(); });
  m_originalDispatchTimer = m_events->newTimer(0.005, &m_originalDispatchTimer);
  if (!m_originalDispatchTimer) {
    m_originalInputCaptured->revoke();
    m_events->removeHandler(EventTypes::Timer, &m_originalDispatchTimer);
    return false;
  }
  return true;
}

bool Server::setOriginalInputDispatcher(std::shared_ptr<OriginalInputDispatcher> dispatcher) noexcept
{
  if (!dispatcher || m_originalDispatcher || !m_originalInputCaptured || !m_originalInputCaptured->live() ||
      !m_originalInputRoutes || !m_originalInputRoutes->live() || m_selectionShutdown || m_originalDispatchTerminal)
    return false;
  try {
    m_originalWake = std::make_shared<OriginalInputWake>(m_events);
    m_events->addHandler(EventTypes::OriginalInputProgress, m_originalWake.get(), [this](const Event &event) {
      const auto *payload = dynamic_cast<const OriginalInputWake::Payload *>(event.getDataObject());
      if (!payload || payload->state != m_originalWake)
        return;
      // Clear before draining, so a completion racing the drain schedules the
      // next turn. The notification never extends a deadline or grants a receipt.
      payload->handled = true;
      m_originalWake->pending.store(false, std::memory_order_release);
      drainOriginalInput();
    });
    dispatcher->setProgressNotifier([weak = std::weak_ptr(m_originalWake)] {
      if (const auto state = weak.lock())
        state->notify();
    });
  } catch (...) {
    if (m_originalWake) {
      m_originalWake->close();
      m_events->removeHandler(EventTypes::OriginalInputProgress, m_originalWake.get());
      m_originalWake.reset();
    }
    return false;
  }
  m_originalDispatcher = std::move(dispatcher);
  return true;
}

deskflow::windowshare::OriginalInputBackend Server::originalInputBackend()
{
  using namespace deskflow::windowshare;
  const auto lifetime = m_originalLifetime;
  const auto recipient = [this, lifetime](const SelectedPeer &selected) -> BaseClientProxy * {
    if (!*lifetime || m_selectionShutdown || m_originalDispatchTerminal || !selected.live() || !m_selectedPeer ||
        !m_selectedPeer->sameSelection(selected))
      return nullptr;
    return inputRecipient(selected.recipient());
  };
  const auto cleanupRecipient = [this, lifetime](const SelectedPeer &selected) -> BaseClientProxy * {
    // Cleanup survives a selection change, but can only address the exact
    // retained TLS connection. A reused recipient or a new connection cannot
    // receive the old owner's resume.
    if (!*lifetime || !selected.ticket().live())
      return nullptr;
    auto *client = inputRecipient(selected.recipient());
    const auto *stream = client ? client->getStream() : nullptr;
    const auto ticket = stream ? stream->authenticatedPeerTicket() : std::nullopt;
    return ticket && ticket->live() && ticket->incarnation() == selected.ticket().incarnation() &&
                   ticket->fingerprint() == selected.ticket().fingerprint()
               ? client
               : nullptr;
  };
  return {
      [recipient](const SelectedPeer &selected) -> std::optional<std::uint32_t> {
        auto *client = recipient(selected);
        return client && client->inputPreparationCapabilityReady() ? client->requestInputPreparationCapability()
                                                                   : std::nullopt;
      },
      [recipient](
          const SelectedPeer &selected, OriginalInputStamp stamp, InputCutoffPoint point,
          InputCutoffClock::time_point deadline
      ) {
        auto *client = recipient(selected);
        return client ? client->startInputPreparation(stamp, point, deadline) : std::nullopt;
      },
      [recipient](const SelectedPeer &selected, InputPreparationBinding binding) {
        auto *client = recipient(selected);
        return client ? client->preparedInputFocus(binding) : std::nullopt;
      },
      [cleanupRecipient](const SelectedPeer &selected, InputPreparationBinding binding) {
        auto *client = cleanupRecipient(selected);
        return client ? client->inputPreparationState(binding) : InputPreparationState::Failed;
      },
      [cleanupRecipient](const SelectedPeer &selected, InputPreparationBinding binding) {
        auto *client = cleanupRecipient(selected);
        return client ? client->inputPreparationOutcome(binding) : InputPreparationOutcome::Failed;
      },
      [recipient](
          const SelectedPeer &selected, const PreparedInputFocus &prepared, const KeyboardRouteBinding &forwarding,
          InputCutoffClock::time_point deadline
      ) {
        auto *client = recipient(selected);
        return client ? client->promoteInputPreparation(prepared, forwarding, deadline) : std::nullopt;
      },
      [cleanupRecipient](
          const SelectedPeer &selected, InputPreparationBinding binding, InputCutoffClock::time_point deadline
      ) {
        auto *client = cleanupRecipient(selected);
        return client && client->cancelInputPreparation(binding, deadline);
      },
      [this, lifetime, recipient](const SelectedPeer &selected, const OriginalInputRouteDecision &route, bool release) {
        if (!recipient(selected) || !route.pointKnown || route.recipient != selected.recipient() ||
            route.server != selected.serverIncarnation())
          return false;
        if (release) {
          // Retire ownership before any callback. A racing physical repeat/up
          // sees a tombstone; it cannot move to the newly prepared Mac owner.
          auto detached = m_inputRoutes.releaseRecipient(selected.recipient());
          for (const auto &press : detached.presses) {
            auto *client = recipient(selected);
            if (!client)
              return false;
            switch (press.control.kind) {
            case InputRoutes::Kind::Key:
              client->keyUp(press.key, 0, static_cast<KeyButton>(press.button));
              break;
            case InputRoutes::Kind::Button:
              client->mouseUp(static_cast<ButtonID>(press.button));
              break;
            case InputRoutes::Kind::ModifierCommand:
              detached.modifiers |= press.mask;
              break;
            }
            if (!*lifetime)
              return false;
          }
          if (detached.modifiers) {
            auto *client = recipient(selected);
            if (!client)
              return false;
            client->keyDown(kKeyClearModifiers, detached.modifiers, 0, {});
          }
        }
        auto *client = recipient(selected);
        if (!client)
          return false;
        if (route.kind == OriginalInputRouteKind::Motion)
          client->mouseMove(route.x, route.y);
        return recipient(selected) != nullptr;
      },
      [recipient](
          const SelectedPeer &selected, OriginalInputStamp stamp, KeyboardRouteBinding binding, std::uint64_t viewer,
          InputCutoffPoint point, InputCutoffClock::time_point deadline
      ) {
        auto *client = recipient(selected);
        return client ? client->startSealedInputCutoff(stamp, binding, viewer, point, deadline) : std::nullopt;
      },
      [recipient](
          const SelectedPeer &selected, const InputCutoffSeal &seal, OriginalInputStamp stamp, InputCutoffPoint point,
          InputCutoffClock::time_point deadline, bool presentCursor
      ) {
        auto *client = recipient(selected);
        return client ? client->refreshMappedInputCutoff(seal, stamp, point, deadline, presentCursor) : std::nullopt;
      },
      [recipient](
          const SelectedPeer &selected, const InputCutoffSeal &seal, const std::vector<InputCutoffMapItem> &items
      ) {
        auto *client = recipient(selected);
        return client ? client->startMappedInputBatch(seal, items) : std::nullopt;
      },
      [recipient](const SelectedPeer &selected, InputCutoffBinding binding) {
        auto *client = recipient(selected);
        return client ? client->inputCutoffObservation(binding) : std::nullopt;
      },
      [cleanupRecipient](const SelectedPeer &selected, InputCutoffBinding binding) {
        auto *client = cleanupRecipient(selected);
        return client ? client->inputCutoffState(binding) : InputCutoffState::Denied;
      },
      [cleanupRecipient](const SelectedPeer &selected, InputCutoffBinding binding) {
        auto *client = cleanupRecipient(selected);
        return client && client->resumeInputCutoff(binding);
      }
  };
}

deskflow::windowshare::OriginalInputRouteDecision
Server::originalDispatchRoute(const InputRoutes::Recipients &recipients, bool relative) const
{
  using namespace deskflow::windowshare;
  OriginalInputRouteDecision route;
  if (m_originalRouteObservation)
    route = m_originalRouteObservation->decision;
  route.server = m_serverIncarnation;
  if (recipients.empty()) {
    route.outcome = OriginalInputRouteOutcome::Local;
    return route;
  }
  if (recipients.size() != 1) {
    route.outcome = OriginalInputRouteOutcome::Ambiguous;
    return route;
  }
  route.recipient = recipients.front();
  auto *client = inputRecipient(route.recipient);
  if (!client)
    return route;
  route.pointKnown = client == m_active && !relative && !(m_relativeMoves && isLockedToComputerServer());
  if (route.pointKnown) {
    route.x = m_x;
    route.y = m_y;
  }
  try {
    const auto *stream = client->getStream();
    const auto ticket = stream ? stream->authenticatedPeerTicket() : std::nullopt;
    if (ticket && ticket->live()) {
      route.ticket = *ticket;
      route.outcome = relative ? OriginalInputRouteOutcome::Relative : OriginalInputRouteOutcome::Dispatched;
    } else {
      route.outcome = OriginalInputRouteOutcome::Unauthenticated;
    }
  } catch (...) {
    route.outcome = OriginalInputRouteOutcome::Failed;
  }
  return route;
}

bool Server::deferOriginalInput(PendingOperation operation, InputRoutes::Recipients recipients)
{
  if (!m_originalDispatcher || (m_originalExecuting && m_originalRouteObservation == m_originalExecutingObservation))
    return false;
  const auto *motion = std::get_if<PendingMotion>(&operation);
  PendingInput pending{
      std::move(operation), std::move(recipients), {}, selectedPeer(), OriginalClock::now() + originalDispatchTimeout
  };
  pending.route = originalDispatchRoute(pending.recipients, motion && motion->relative);
  if (m_originalRouteObservation)
    m_originalRouteObservation->deferred = true;
  if (pending.route.stamp && (!m_originalInputRoutes || !m_originalInputRoutes->defer(pending.route.stamp)))
    cancelOriginalDispatch();
  // A detached reserve/poll call still occupies one pending slot. Reentry may
  // enqueue behind it but cannot force an asynchronous requeue to slot 257.
  if (m_originalPending.size() + (m_originalReservationCall && !m_originalReservationCall->cancelled ? 1 : 0) >=
      originalPendingCapacity) {
    cancelOriginalDispatch();
    // Reentrant producers cannot grow the queue. Existing presses still own
    // their releases; pending acquisitions become tombstones before draining.
    for (const auto &queued : m_originalPending) {
      if (const auto *key = std::get_if<PendingKey>(&queued.operation); key && key->phase == PendingKey::Phase::Down)
        m_inputRoutes.down(InputRoutes::keyControl(key->id, key->button), key->id, key->mask, key->button, {});
      if (const auto *button = std::get_if<PendingButton>(&queued.operation); button && button->down)
        m_inputRoutes.down({InputRoutes::Kind::Button, button->id}, 0, 0, button->id, {});
    }
    if (const auto *key = std::get_if<PendingKey>(&pending.operation); key && key->phase == PendingKey::Phase::Down)
      m_inputRoutes.down(InputRoutes::keyControl(key->id, key->button), key->id, key->mask, key->button, {});
    if (const auto *button = std::get_if<PendingButton>(&pending.operation); button && button->down)
      m_inputRoutes.down({InputRoutes::Kind::Button, button->id}, 0, 0, button->id, {});
    if (m_originalDraining) {
      const auto lifetime = m_originalLifetime;
      cancelInFlightOriginal();
      if (!*lifetime)
        return true;
      while (!m_originalPending.empty()) {
        auto queued = std::move(m_originalPending.front());
        inputEvidenceApply(InputEvidenceQueue::ServerPending, this, 0, 1, [&] { m_originalPending.pop_front(); });
        executeOriginalInput(queued, nullptr);
        if (!*lifetime)
          return true;
      }
      executeOriginalInput(pending, nullptr);
      return true;
    }
    const auto lifetime = m_originalLifetime;
    drainOriginalInput();
    if (!*lifetime)
      return true;
  }
  inputEvidenceApply(InputEvidenceQueue::ServerPending, this, 1, 0, [&] {
    m_originalPending.push_back(std::move(pending));
  });
  drainOriginalInput();
  return true;
}

void Server::revokeOriginalOwner(std::uint64_t owner) noexcept
{
  if (!owner)
    return;
  m_inputRoutes.revokeOriginal(owner);
  m_originalOwners.erase(owner);
  // Distinct lease lifetimes cannot accumulate an unbounded deny list. Once
  // the bounded history fills, this dispatcher cannot acquire again.
  if (m_originalOwners.size() + m_originalRevokedOwners.size() < originalPendingCapacity)
    m_originalRevokedOwners.insert(owner);
  else
    m_originalDispatchTerminal = true;
  m_originalReleases.insert(owner);
}

Server::OriginalReservationScope::OriginalReservationScope(Server &server, OriginalReservationCall &call) noexcept
    : m_server(server),
      m_lifetime(server.m_originalLifetime),
      m_previous(server.m_originalReservationCall)
{
  m_server.m_originalReservationCall = &call;
}
Server::OriginalReservationScope::~OriginalReservationScope()
{
  if (*m_lifetime)
    m_server.m_originalReservationCall = m_previous;
}

void Server::suppressOriginalInput(
    const PendingInput &pending, const deskflow::windowshare::OriginalInputCaptured *captured
)
{
  using namespace deskflow::windowshare;
  const auto lifetime = m_originalLifetime;
  const auto &operation = pending.operation;
  const auto *key = std::get_if<PendingKey>(&operation);
  const auto *button = std::get_if<PendingButton>(&operation);
  std::optional<InputRoutes::Control> control;
  if (key && key->id != kKeySetModifiers && key->id != kKeyClearModifiers)
    control = InputRoutes::keyControl(key->id, key->button);
  else if (button)
    control = InputRoutes::Control{InputRoutes::Kind::Button, button->id};
  const bool down = (key && key->phase == PendingKey::Phase::Down) || (button && button->down);
  const bool up = (key && key->phase == PendingKey::Phase::Up) || (button && !button->down);
  auto held = control ? m_inputRoutes.repeat(*control) : std::nullopt;
  if (held && held->originalOwner)
    revokeOriginalOwner(held->originalOwner);
  if (control && down && !held)
    held = m_inputRoutes.down(*control, key ? key->id : 0, key ? key->mask : 0, key ? key->button : button->id, {});
  // Pending/failed acquisition tombstones need the same physical-up join as
  // delivered originals. An untranslated raw up must be able to retire them.
  if (control && down && held && captured) {
    if (key) {
      const auto *raw = std::get_if<OriginalInputKey>(&captured->input.record.payload);
      if (raw && raw->transition == OriginalKeyTransition::Down &&
          m_originalKeyControls.size() < InputRoutes::maxPresses)
        m_originalKeyControls.insert_or_assign(raw->code, OriginalPhysicalPress{*control, held->incarnation});
    } else {
      const auto *raw = std::get_if<OriginalInputButton>(&captured->input.record.payload);
      if (raw && raw->down)
        m_originalButtonControls.insert_or_assign(raw->button, OriginalPhysicalPress{*control, held->incarnation});
    }
  }
  if (control && up)
    takeInputPress(*control);
  if (key && up)
    releaseRoutedModifiers(held ? held->key : key->id, key->mask);
  if (!*lifetime)
    return;
  if (std::holds_alternative<PendingMotion>(operation)) {
    // Ordinary motion writes implicitly resume a Core cutoff. Only the
    // dispatcher's explicit mapped presentation may cross a live All gate.
    auto presentation = pending;
    presentation.recipients.clear();
    const auto dispatcher = m_originalDispatcher;
    for (const auto recipient : pending.recipients) {
      const bool ready = !dispatcher || dispatcher->legacyAcquisitionReady(recipient);
      if (!*lifetime)
        return;
      if (ready)
        presentation.recipients.push_back(recipient);
    }
    if (!presentation.recipients.empty())
      executeLegacyInput(presentation);
  }
}

void Server::cancelInFlightOriginal()
{
  auto *flight = m_originalReservationCall;
  if (!flight || flight->cancelled)
    return;
  flight->cancelled = true; // Fence before any release/packet callback can reenter.
  suppressOriginalInput(flight->pending, flight->captured);
}

void Server::flushOriginalBeforeSelection()
{
  if (!m_originalDispatcher)
    return;
  const auto lifetime = m_originalLifetime;
  const bool draining = m_originalDraining;
  m_originalDraining = true;
  try {
    cancelInFlightOriginal();
  } catch (...) {
    if (!*lifetime)
      return;
    cancelOriginalDispatch();
  }
  if (!*lifetime)
    return;
  // Leave, enter and same-screen warp packets must follow older queued input.
  // The revoked selection cannot authorize original delivery, but an ordinary
  // press still reaches its frozen legacy recipient before that client leaves.
  while (!m_originalPending.empty()) {
    auto pending = std::move(m_originalPending.front());
    inputEvidenceApply(InputEvidenceQueue::ServerPending, this, 0, 1, [&] { m_originalPending.pop_front(); });
    try {
      executeOriginalInput(pending, nullptr);
    } catch (...) {
      if (!*lifetime)
        return;
      cancelOriginalDispatch();
    }
    if (!*lifetime)
      return;
  }
  m_originalDraining = draining;
}

void Server::cancelOriginalDispatch(const char *reason) noexcept
{
  if (!m_originalDispatchTerminal) {
    deskflow::windowshare::lifecycleDiagnostic(
        "Core input", "dispatch.stopped", "failed", reason,
        {{"pending", qint64(m_originalPending.size())},
         {"completed", qint64(m_originalCompleted.size())},
         {"captureLive", m_originalInputCaptured && m_originalInputCaptured->live()},
         {"routesLive", m_originalInputRoutes && m_originalInputRoutes->live()}}
    );
    LOG_WARN("original input dispatch stopped: %s", reason);
    if (m_originalInputCaptured)
      LOG_WARN(
          "original input stop context: captureLive=%d routesLive=%d epochReason=%d inboxFailure=%s",
          m_originalInputCaptured->live(), m_originalInputRoutes && m_originalInputRoutes->live(),
          int(m_originalInputCaptured->epoch().reason()), m_originalInputCaptured->failureName()
      );
  }
  deskflow::windowshare::inputEvidenceInvalidate(deskflow::windowshare::InputEvidenceLoss::PipelineRevoked);
  m_originalDispatchTerminal = true;
  const auto owners = m_originalOwners;
  for (auto owner : owners)
    revokeOriginalOwner(owner);
  // Keep completed physical ups for terminal cleanup. Discarding the cache
  // here would leave an already-released original press as a live tombstone.
}

void Server::retireOriginalCapture(const deskflow::windowshare::OriginalInputCaptured &captured)
{
  using namespace deskflow::windowshare;
  std::optional<OriginalPhysicalPress> physical;
  if (const auto *key = std::get_if<OriginalInputKey>(&captured.input.record.payload);
      key && key->transition == OriginalKeyTransition::Up) {
    const auto found = m_originalKeyControls.find(key->code);
    if (found != m_originalKeyControls.end()) {
      physical = found->second;
      m_originalKeyControls.erase(found);
    }
  } else if (
      const auto *button = std::get_if<OriginalInputButton>(&captured.input.record.payload); button && !button->down
  ) {
    const auto found = m_originalButtonControls.find(button->button);
    if (found != m_originalButtonControls.end()) {
      physical = found->second;
      m_originalButtonControls.erase(found);
    }
  }
  if (!physical)
    return;
  const auto held = m_inputRoutes.repeat(physical->control);
  if (!held || held->incarnation != physical->incarnation)
    return;
  if (held && held->originalOwner)
    revokeOriginalOwner(held->originalOwner);
  takeInputPress(physical->control);
  if (physical->control.kind == InputRoutes::Kind::Key) {
    const auto flags = captured.input.record.flags;
    KeyModifierMask mask = 0;
    if (flags & originalKey::shiftFlag)
      mask |= KeyModifierShift;
    if (flags & originalKey::controlFlag)
      mask |= KeyModifierControl;
    if (flags & originalKey::optionFlag)
      mask |= KeyModifierAlt;
    if (flags & originalKey::commandFlag)
      mask |= KeyModifierSuper;
    releaseRoutedModifiers(held->key, mask);
  }
}

std::optional<Server::InputRoutes::Press> Server::takeInputPress(InputRoutes::Control control)
{
  auto press = m_inputRoutes.up(control);
  if (!press)
    return press;
  // An up retires the exact acquisition on every success/failure path. A
  // retained raw map cannot later erase a fresh legacy press on that control.
  std::erase_if(m_originalKeyControls, [&](const auto &entry) {
    return entry.second.incarnation == press->incarnation;
  });
  std::erase_if(m_originalButtonControls, [&](const auto &entry) {
    return entry.second.incarnation == press->incarnation;
  });
  return press;
}

void Server::drainOriginalInput(OriginalClock::time_point now)
{
  using namespace deskflow::windowshare;
  if (m_originalDraining || !m_originalInputCaptured)
    return;
  m_originalDraining = true;
  const auto lifetime = m_originalLifetime;
  const auto dispatcher = m_originalDispatcher;
  if (dispatcher)
    dispatcher->maintain();
  if (!*lifetime)
    return;
  if (m_selectedPeer && !m_selectedPeer->live())
    invalidateSelectedPeer();
  if (!*lifetime)
    return;
  if (!m_originalInputCaptured->live() || (m_originalInputRoutes && !m_originalInputRoutes->live()))
    cancelOriginalDispatch("capture or route inbox revoked");
  while (auto captured = m_originalInputCaptured->pop()) {
    if (!dispatcher)
      continue;
    while (m_originalCompleted.size() >= originalPendingCapacity) {
      const auto first = m_originalCompleted.begin();
      const auto &stamp = first->second.captured.translation.stamp;
      const bool awaited = std::any_of(m_originalPending.begin(), m_originalPending.end(), [&](const auto &pending) {
        return pending.route.stamp.epoch == stamp.epoch && pending.route.stamp.sequence == stamp.sequence;
      });
      if (awaited) {
        cancelOriginalDispatch();
        flushOriginalBeforeSelection();
        if (!*lifetime)
          return;
        continue;
      }
      auto retired = std::move(first->second);
      inputEvidenceApply(InputEvidenceQueue::ServerCompleted, this, 0, 1, [&] { m_originalCompleted.erase(first); });
      if (!retired.upResolved)
        retireOriginalCapture(retired.captured);
      if (!*lifetime)
        return;
    }
    {
      const auto &record = captured->input.record;
      InputEvidenceMutation evidence(InputEvidenceQueue::ServerCompleted, this, 1, 0);
      m_originalCompleted.emplace(
          std::pair{record.epoch, record.sequence},
          CompletedInput{std::move(*captured), now + originalDispatchTimeout, now}
      );
    }
  }
  if (dispatcher) {
    const auto owners = m_originalOwners;
    for (auto owner : owners) {
      const bool live = dispatcher->live(owner);
      if (!*lifetime)
        return;
      if (!live)
        revokeOriginalOwner(owner);
    }
  }
  // Keep hover latency bounded without changing any reserved native event or
  // press/drag/scroll sequence. Each absolute Server point already includes
  // every translated delta. Only the final captured record is delivered;
  // superseded records keep an explicit diagnostic outcome and raw history.
  const bool coalesceHover = dispatcher && m_inputRoutes.size() == 0 && dispatcher->canCoalesceHoverMotion();
  if (!*lifetime)
    return;
  if (coalesceHover) {
    const auto hover = [&](const PendingInput &pending) -> const OriginalInputCaptured * {
      const auto *motion = std::get_if<PendingMotion>(&pending.operation);
      if (!motion || motion->relative || pending.preparation || pending.captureResolved || !pending.selected ||
          !pending.selected->live() || !pending.route.pointKnown ||
          pending.route.outcome != OriginalInputRouteOutcome::Dispatched || pending.route.stamp.part != 1)
        return nullptr;
      const auto found = m_originalCompleted.find({pending.route.stamp.epoch, pending.route.stamp.sequence});
      if (found == m_originalCompleted.end() || found->second.receivedAt > pending.deadline)
        return nullptr;
      const auto &captured = found->second.captured;
      const auto *raw = std::get_if<OriginalInputMotion>(&captured.input.record.payload);
      return captured.input.live() && captured.translation.parts == 1 && !captured.translation.uncertain && raw &&
                     raw->dragButton == originalInputButtonCount
                 ? &captured
                 : nullptr;
    };
    for (auto first = m_originalPending.begin(); first != m_originalPending.end();) {
      const auto next = std::next(first);
      if (next == m_originalPending.end())
        break;
      const auto *a = hover(*first), *b = hover(*next);
      if (a && b && first->selected->sameSelection(*next->selected) && a->input.epoch.sameEpoch(b->input.epoch) &&
          a->input.record.sequence != UINT64_MAX && b->input.record.sequence == a->input.record.sequence + 1 &&
          a->input.record.flags == b->input.record.flags) {
        auto decision = first->route;
        decision.outcome = OriginalInputRouteOutcome::Coalesced;
        if (!m_originalInputRoutes->publish(std::move(decision))) {
          cancelOriginalDispatch();
          break;
        }
        if (auto *evidence = inputEvidence())
          evidence->hover(a->input.record.epoch, a->input.record.sequence, b->input.record.sequence);
        first = inputEvidenceApply(InputEvidenceQueue::ServerPending, this, 0, 1, [&] {
          return m_originalPending.erase(first);
        });
      } else {
        ++first;
      }
    }
  }
  if (dispatcher && !m_originalDispatchTerminal) {
    std::vector<OriginalInputDispatcher::Preview> preview;
    preview.reserve(inputCutoffMaximumBatch);
    for (const auto &pending : m_originalPending) {
      if (preview.size() == inputCutoffMaximumBatch || !pending.selected || !pending.selected->live() ||
          !pending.route.pointKnown || pending.route.outcome != OriginalInputRouteOutcome::Dispatched ||
          pending.route.stamp.part != 1)
        break;
      const auto found = m_originalCompleted.find({pending.route.stamp.epoch, pending.route.stamp.sequence});
      const auto *captured = pending.captured ? &*pending.captured
                             : found != m_originalCompleted.end() && found->second.receivedAt <= pending.deadline
                                 ? &found->second.captured
                                 : nullptr;
      if (!captured || !captured->input.live() || captured->translation.parts != 1 || captured->translation.uncertain)
        break;
      preview.push_back(
          {*captured, pending.route, *pending.selected,
           pending.preparation ? pending.preparationDeadline : now + originalPreparationTimeout}
      );
    }
    try {
      dispatcher->preview(preview);
    } catch (...) {
      if (!*lifetime)
        return;
      cancelOriginalDispatch();
    }
    if (!*lifetime)
      return;
  }
  while (!m_originalPending.empty()) {
    const auto stamp = m_originalPending.front().route.stamp;
    // The core event queue preserves native part order. A completed older up
    // with no operation before this newer part was filtered (or translated to
    // zero parts). Retire its original owner before a new press can reuse it.
    while (!m_originalCompleted.empty() && stamp &&
           m_originalCompleted.begin()->first < std::pair{stamp.epoch, stamp.sequence}) {
      auto retired = std::move(m_originalCompleted.begin()->second);
      inputEvidenceApply(InputEvidenceQueue::ServerCompleted, this, 0, 1, [&] {
        m_originalCompleted.erase(m_originalCompleted.begin());
      });
      if (!retired.upResolved)
        retireOriginalCapture(retired.captured);
      if (!*lifetime)
        return;
    }
    if (m_originalPending.empty())
      break;
    if (m_originalPending.front().route.stamp != stamp)
      continue;
    auto &front = m_originalPending.front();
    if (!front.captureResolved) {
      const auto found = m_originalCompleted.find({front.route.stamp.epoch, front.route.stamp.sequence});
      if (!m_originalDispatchTerminal && front.route.stamp && found == m_originalCompleted.end() &&
          now < front.deadline)
        break;
      if (!m_originalDispatchTerminal && found == m_originalCompleted.end() && now >= front.deadline)
        cancelOriginalDispatch("translated input has no completed capture before its deadline");
      if (!m_originalDispatchTerminal && found != m_originalCompleted.end() &&
          found->second.receivedAt > front.deadline)
        cancelOriginalDispatch("completed capture arrived after its translated input deadline");
      front.captureResolved = true;
      if (!m_originalDispatchTerminal && found != m_originalCompleted.end()) {
        const auto part = front.route.stamp.part;
        if (part && part <= maximumOriginalInputParts && !(found->second.parts & (1u << part))) {
          front.captured = found->second.captured;
          found->second.parts |= 1u << part;
        }
        const auto *key = std::get_if<PendingKey>(&front.operation);
        const auto *button = std::get_if<PendingButton>(&front.operation);
        found->second.upResolved |= (key && key->phase == PendingKey::Phase::Up) || (button && !button->down);
      }
    }
    auto pending = std::move(m_originalPending.front());
    inputEvidenceApply(InputEvidenceQueue::ServerPending, this, 0, 1, [&] { m_originalPending.pop_front(); });
    bool complete = true;
    try {
      complete = executeOriginalInput(pending, pending.captured ? &*pending.captured : nullptr, now);
    } catch (...) {
      if (!*lifetime)
        return;
      cancelOriginalDispatch();
    }
    if (!*lifetime)
      return;
    if (!complete) {
      inputEvidenceApply(InputEvidenceQueue::ServerPending, this, 1, 0, [&] {
        m_originalPending.push_front(std::move(pending));
      });
      break; // No later operation, including a release, can pass this frozen input.
    }
  }
  while (true) {
    const auto found = std::find_if(m_originalCompleted.begin(), m_originalCompleted.end(), [&](const auto &entry) {
      return (m_originalDispatchTerminal || now >= entry.second.deadline || !entry.second.captured.translation.parts) &&
             (m_originalPending.empty() || entry.first < std::pair{
                                                             m_originalPending.front().route.stamp.epoch,
                                                             m_originalPending.front().route.stamp.sequence
                                                         });
    });
    if (found == m_originalCompleted.end())
      break;
    auto retired = std::move(found->second);
    inputEvidenceApply(InputEvidenceQueue::ServerCompleted, this, 0, 1, [&] { m_originalCompleted.erase(found); });
    if (!retired.upResolved)
      retireOriginalCapture(retired.captured);
    if (!*lifetime)
      return;
  }
  // Remove ownership before external cleanup so reentry cannot resurrect it.
  auto releases = std::move(m_originalReleases);
  m_originalReleases.clear();
  if (dispatcher)
    for (auto owner : releases) {
      dispatcher->release(owner);
      if (!*lifetime)
        return;
    }
  if (dispatcher && m_originalDispatchTerminal && !m_originalAbortSent) {
    m_originalAbortSent = true;
    dispatcher->abort();
    if (!*lifetime)
      return;
  }
  m_originalDraining = false;
  drainWindowDragTransfer();
}

void Server::executeLegacyInput(const PendingInput &pending)
{
  const auto lifetime = m_originalLifetime;
  const auto *previous = m_originalExecuting;
  const auto *previousObservation = m_originalExecutingObservation;
  m_originalExecuting = &pending;
  m_originalExecutingObservation = m_originalRouteObservation;
  try {
    std::visit(
        [&](const auto &operation) {
          using T = std::decay_t<decltype(operation)>;
          if constexpr (std::is_same_v<T, PendingKey>) {
            switch (operation.phase) {
            case PendingKey::Phase::Down:
              onKeyDown(operation.id, operation.mask, operation.button, operation.language, nullptr);
              break;
            case PendingKey::Phase::Up:
              onKeyUp(operation.id, operation.mask, operation.button, nullptr);
              break;
            case PendingKey::Phase::Repeat:
              onKeyRepeat(operation.id, operation.mask, operation.count, operation.button, operation.language);
              break;
            }
          } else if constexpr (std::is_same_v<T, PendingButton>) {
            if (operation.down)
              onMouseDown(operation.id);
            else
              onMouseUp(operation.id);
          } else {
            for (auto recipient : pending.recipients)
              if (auto *client = inputRecipient(recipient)) {
                if constexpr (std::is_same_v<T, PendingMotion>) {
                  observeOriginalSend(client, operation.relative);
                  if (operation.relative)
                    client->mouseRelativeMove(operation.x, operation.y);
                  else
                    client->mouseMove(operation.x, operation.y);
                } else {
                  observeOriginalSend(client);
                  client->mouseWheel(operation.x, operation.y);
                }
                if (!*lifetime)
                  return;
              }
          }
        },
        pending.operation
    );
  } catch (...) {
    if (*lifetime)
      m_originalExecuting = previous;
    if (*lifetime)
      m_originalExecutingObservation = previousObservation;
    throw;
  }
  if (*lifetime)
    m_originalExecuting = previous;
  if (*lifetime)
    m_originalExecutingObservation = previousObservation;
}

bool Server::executeOriginalInput(
    PendingInput &pending, const deskflow::windowshare::OriginalInputCaptured *captured, OriginalClock::time_point now
)
{
  using namespace deskflow::windowshare;
  InputEvidenceExecution evidence(this);
  using State = OriginalInputDispatcher::State;
  const auto lifetime = m_originalLifetime;
  const auto dispatcher = m_originalDispatcher;
  Event event(EventTypes::Unknown, nullptr, nullptr, Event::EventFlags::DontFreeData, pending.route.stamp);
  OriginalRouteObservation observation(*this, event, pending.route.kind);
  observation.decision = pending.route;
  observation.decision.outcome = OriginalInputRouteOutcome::NoSend;
  const auto *key = std::get_if<PendingKey>(&pending.operation);
  const auto *button = std::get_if<PendingButton>(&pending.operation);
  const auto *motion = std::get_if<PendingMotion>(&pending.operation);
  const bool modifierCommand = key && (key->id == kKeySetModifiers || key->id == kKeyClearModifiers);
  std::optional<InputRoutes::Control> control;
  if (key && !modifierCommand)
    control = InputRoutes::keyControl(key->id, key->button);
  else if (button)
    control = InputRoutes::Control{InputRoutes::Kind::Button, button->id};
  const auto held = control ? m_inputRoutes.repeat(*control) : std::nullopt;
  const bool down = (key && key->phase == PendingKey::Phase::Down) || (button && button->down);
  const bool up = (key && key->phase == PendingKey::Phase::Up) || (button && !button->down);
  auto existingOwner = held ? held->originalOwner : 0;
  bool eligible = captured && captured->input.live() && captured->translation.parts == 1 &&
                  !captured->translation.uncertain && pending.route.stamp.part == 1 && !modifierCommand &&
                  !m_originalDispatchTerminal && m_originalInputCaptured->live() && pending.selected &&
                  pending.selected->live() && m_selectedPeer && m_selectedPeer->sameSelection(*pending.selected) &&
                  pending.route.recipient == pending.selected->recipient() &&
                  pending.route.outcome == OriginalInputRouteOutcome::Dispatched &&
                  (!control || (held ? existingOwner != 0 : down));
  if (eligible) {
    const auto &payload = captured->input.record.payload;
    if (key) {
      const auto *raw = std::get_if<OriginalInputKey>(&payload);
      const auto expected = up                                        ? OriginalKeyTransition::Up
                            : key->phase == PendingKey::Phase::Repeat ? OriginalKeyTransition::Repeat
                                                                      : OriginalKeyTransition::Down;
      eligible =
          raw && raw->transition == expected && (key->phase == PendingKey::Phase::Repeat ? raw->repeat : !raw->repeat);
    } else if (button) {
      const auto *raw = std::get_if<OriginalInputButton>(&payload);
      eligible = raw && raw->down == button->down;
    } else if (motion) {
      const auto *raw = std::get_if<OriginalInputMotion>(&payload);
      eligible = raw && !motion->relative && pending.route.pointKnown;
      if (eligible && raw->dragButton != originalInputButtonCount) {
        const auto mapped = m_originalButtonControls.find(raw->dragButton);
        const auto press =
            mapped == m_originalButtonControls.end() ? std::nullopt : m_inputRoutes.repeat(mapped->second.control);
        eligible = press && press->originalOwner && press->incarnation == mapped->second.incarnation;
        if (eligible)
          existingOwner = press->originalOwner;
      }
    } else {
      eligible = std::holds_alternative<OriginalInputScroll>(payload) && pending.route.pointKnown;
    }
  }
  if (inputEvidence()) {
    // These observations explain refusals before reserve() can report them.
    // At most one line per boundary per Server, outside the native event tap;
    // no key code, text, clipboard value or native payload is retained here.
    const unsigned boundary = eligible                                                               ? 14
                              : !captured                                                            ? 0
                              : !captured->input.live()                                              ? 1
                              : captured->translation.parts != 1                                     ? 2
                              : captured->translation.uncertain                                      ? 3
                              : pending.route.stamp.part != 1                                        ? 4
                              : modifierCommand                                                      ? 5
                              : m_originalDispatchTerminal                                           ? 6
                              : !m_originalInputCaptured->live()                                     ? 7
                              : !pending.selected || !pending.selected->live()                       ? 8
                              : !m_selectedPeer || !m_selectedPeer->sameSelection(*pending.selected) ? 9
                              : pending.route.recipient != pending.selected->recipient()             ? 10
                              : pending.route.outcome != OriginalInputRouteOutcome::Dispatched       ? 11
                              : control && (held ? !existingOwner : !down)                           ? 12
                                                                                                     : 13;
    const bool repeat = key && key->phase == PendingKey::Phase::Repeat;
    const auto bit = std::uint32_t{1} << (boundary + (repeat ? 16 : 0));
    if (!(m_originalEligibilityLogged & bit)) {
      m_originalEligibilityLogged |= bit;
      constexpr const char *reasons[]{"missing-completed-capture", "capture-not-live",
                                      "translation-count",         "translation-uncertain",
                                      "translation-part",          "modifier-command",
                                      "dispatch-terminal",         "capture-inbox-not-live",
                                      "selection-not-live",        "selection-changed",
                                      "recipient-changed",         "route-not-dispatched",
                                      "control-not-acquirable",    "payload-or-point-not-eligible",
                                      "eligible-for-reservation"};
      lifecycleDiagnostic(
          "Core input", "dispatch.eligibility", "observed", reasons[boundary],
          {{"eligible", eligible},
           {"repeat", repeat},
           {"epoch", QString::number(pending.route.stamp.epoch)},
           {"sequence", QString::number(pending.route.stamp.sequence)},
           {"part", int(pending.route.stamp.part)},
           {"translatedParts", captured ? int(captured->translation.parts) : -1},
           {"uncertain", captured && captured->translation.uncertain},
           {"routeKind", int(pending.route.kind)},
           {"routeOutcome", int(pending.route.outcome)},
           {"pointKnown", pending.route.pointKnown},
           {"controlHeld", held.has_value()},
           {"originalOwnerHeld", existingOwner != 0}}
      );
    }
  }
  const auto ticket = pending.preparation;
  const auto deadline = ticket ? pending.preparationDeadline : now + originalPreparationTimeout;
  OriginalReservationCall flight{pending, captured ? captured : pending.captured ? &*pending.captured : nullptr};
  // Suppress before entering a provider cleanup callback. The in-flight marker
  // also orders any reentrant selection flush before later queued operations.
  const auto suppress = [&] {
    if (!*lifetime || flight.cancelled)
      return;
    OriginalReservationScope inFlight(*this, flight);
    cancelInFlightOriginal();
  };
  if (ticket && (!eligible || now >= deadline)) {
    pending.preparation = 0;
    if (now >= deadline)
      cancelOriginalDispatch("input handoff preparation expired");
    suppress();
    dispatcher->cancelPreparation(ticket);
    observation.decision.outcome = OriginalInputRouteOutcome::Failed;
    return true;
  }
  OriginalInputDispatcher::Result result;
  if (eligible) {
    try {
      OriginalReservationScope inFlight(*this, flight);
      result = ticket ? dispatcher->poll(ticket)
                      : dispatcher->reserve(*captured, pending.route, *pending.selected, existingOwner, deadline);
    } catch (...) {
      // A throwing provider may have partially acquired authority. Never turn
      // an exceptional/unfinished handoff into a fresh legacy acquisition.
      pending.preparation = 0;
      if (*lifetime) {
        cancelOriginalDispatch();
        suppress();
      }
      if (ticket)
        dispatcher->cancelPreparation(ticket);
      if (!*lifetime)
        dispatcher->abort();
      observation.decision.outcome = OriginalInputRouteOutcome::Failed;
      return true;
    }
    // Discard uses only retained provider/stack values. In particular, a poll
    // callback can destroy Server after the request left its pending deque.
    const auto discard = [&] {
      if (result.reservation)
        dispatcher->cancel(*result.reservation);
      if (ticket)
        dispatcher->cancelPreparation(ticket);
      else if (result.state == State::Pending && result.ticket)
        dispatcher->cancelPreparation(result.ticket);
    };
    if (!*lifetime) {
      discard();
      return true;
    }
    if (flight.cancelled) {
      pending.preparation = 0;
      if (result.reservation)
        revokeOriginalOwner(result.reservation->owner);
      discard();
      observation.decision.outcome = OriginalInputRouteOutcome::Failed;
      return true;
    }
    const bool identity = ticket                           ? result.ticket == ticket
                          : result.state == State::Pending ? result.ticket > m_originalLastPreparation
                                                           : result.ticket == 0;
    if (!result.wellFormed() || !identity || std::max(now, OriginalClock::now()) >= deadline) {
      pending.preparation = 0;
      cancelOriginalDispatch(
          !result.wellFormed() ? "input dispatcher returned an invalid result"
          : !identity          ? "input dispatcher changed its preparation identity"
                               : "input dispatcher returned after its preparation deadline"
      );
      suppress();
      discard();
      observation.decision.outcome = OriginalInputRouteOutcome::Failed;
      return true;
    }
    if (result.state == State::Pending) {
      if (!ticket) {
        m_originalLastPreparation = result.ticket;
        pending.preparation = result.ticket;
        pending.preparationDeadline = deadline;
      }
      observation.deferred = true;
      return false;
    }
    pending.preparation = 0;
    if (result.state == State::Revoked) {
      if (existingOwner)
        revokeOriginalOwner(existingOwner);
      suppress();
      observation.decision.outcome = OriginalInputRouteOutcome::Failed;
      return true;
    }
  }
  const auto reservation = result.reservation;
  if (!reservation) {
    // A terminal Refused explicitly certifies that its handoff has completed
    // cleanup. Ineligible records never went through that protocol: missing,
    // expanded, configured, relative or uncertain input cannot silently reopen
    // a still-owned recipient's independent All forwarding gate.
    const bool releasedFallback = eligible && result.state == State::Refused;
    const bool legacyRelease =
        (up && held && !held->originalOwner) || (modifierCommand && key->id == kKeyClearModifiers);
    if (!releasedFallback && !legacyRelease && dispatcher) {
      const auto &recipients = held && !held->originalOwner ? held->recipients : pending.recipients;
      InputRoutes::Recipients available;
      bool blocked = false;
      for (const auto recipient : recipients) {
        bool ready;
        {
          OriginalReservationScope inFlight(*this, flight);
          ready = dispatcher->legacyAcquisitionReady(recipient);
        }
        if (!*lifetime)
          return true;
        if (flight.cancelled) {
          observation.decision.outcome = OriginalInputRouteOutcome::Failed;
          return true;
        }
        if (ready)
          available.push_back(recipient);
        else
          blocked = true;
      }
      if (blocked) {
        const auto owners = m_originalOwners;
        for (const auto owner : owners)
          revokeOriginalOwner(owner);
        if (!available.empty() && !held) {
          // Preserve unrelated recipients of an explicit broadcast. The
          // resulting press owns only the recipients that actually received it.
          auto legacy = pending;
          legacy.recipients = std::move(available);
          executeLegacyInput(legacy);
        } else {
          suppress(); // New controls become tombstones until physical up.
        }
        observation.decision.outcome = OriginalInputRouteOutcome::Failed;
        return true;
      }
    }
    if (existingOwner)
      revokeOriginalOwner(existingOwner);
    executeLegacyInput(pending);
    if (*lifetime && up && captured)
      retireOriginalCapture(*captured);
    return true;
  }
  const auto owner = reservation->owner;
  if (!m_originalOwners.contains(owner) && !m_originalRevokedOwners.contains(owner) &&
      m_originalOwners.size() + m_originalRevokedOwners.size() >= originalPendingCapacity)
    cancelOriginalDispatch();
  // No callback below an accepted reservation may cause a legacy acquisition.
  bool valid = !m_originalRevokedOwners.contains(owner) && (!existingOwner || existingOwner == owner) &&
               !m_originalDispatchTerminal && captured->input.live() && m_originalInputCaptured->live() &&
               pending.selected->live() && m_selectedPeer && m_selectedPeer->sameSelection(*pending.selected) &&
               inputRecipient(pending.route.recipient);
  if (valid) {
    {
      OriginalReservationScope inFlight(*this, flight);
      valid = dispatcher->live(owner);
    }
    if (!*lifetime) {
      dispatcher->cancel(*reservation);
      return true;
    }
    valid = valid && !flight.cancelled && pending.selected->live() && m_selectedPeer &&
            m_selectedPeer->sameSelection(*pending.selected) && !m_originalDispatchTerminal &&
            m_originalInputCaptured->live();
  }
  if (valid && control && down && !held) {
    if (key && m_originalKeyControls.size() >= InputRoutes::maxPresses)
      valid = false;
  }
  if (valid && control && down && !held) {
    const auto acquired = m_inputRoutes.downOriginal(
        *control, key ? key->id : 0, key ? key->mask : 0, key ? key->button : button->id, owner
    );
    valid = acquired && acquired->originalOwner == owner;
  }
  if (!valid) {
    revokeOriginalOwner(owner);
    if (existingOwner && existingOwner != owner)
      revokeOriginalOwner(existingOwner);
    suppress();
    dispatcher->cancel(*reservation);
    observation.decision.outcome = OriginalInputRouteOutcome::Failed;
    return true;
  }
  m_originalOwners.insert(owner);
  if (button && down)
    m_originalButtonControls.insert_or_assign(
        std::get<OriginalInputButton>(captured->input.record.payload).button,
        OriginalPhysicalPress{*control, m_inputRoutes.repeat(*control)->incarnation}
    );
  if (key && down)
    m_originalKeyControls.insert_or_assign(
        std::get<OriginalInputKey>(captured->input.record.payload).code,
        OriginalPhysicalPress{*control, m_inputRoutes.repeat(*control)->incarnation}
    );
  if (up && control)
    takeInputPress(*control);
  if (key && up)
    m_originalKeyControls.erase(std::get<OriginalInputKey>(captured->input.record.payload).code);
  if (button && up)
    m_originalButtonControls.erase(std::get<OriginalInputButton>(captured->input.record.payload).button);
  bool committed = false;
  try {
    // The reserved forwarding fence suppresses app forwarding while the real
    // Linux cursor still receives its frozen absolute desktop position.
    if (motion && !dispatcher->presentsCursor())
      executeLegacyInput(pending);
    if (!*lifetime) {
      dispatcher->cancel(*reservation);
      return true;
    }
    const bool outputLive = dispatcher->live(owner);
    if (!*lifetime) {
      dispatcher->cancel(*reservation);
      return true;
    }
    if (outputLive && captured->input.live() && m_originalInputCaptured->live() && pending.selected->live() &&
        m_selectedPeer && m_selectedPeer->sameSelection(*pending.selected) && m_originalOwners.contains(owner) &&
        !m_originalDispatchTerminal)
      committed = dispatcher->commit(*reservation);
  } catch (...) {
    committed = false;
  }
  if (!*lifetime)
    return true;
  if (!committed || !pending.selected->live() || !captured->input.live())
    revokeOriginalOwner(owner);
  if (!committed) {
    dispatcher->cancel(*reservation);
    if (!*lifetime)
      return true;
  }
  if (key && up)
    releaseRoutedModifiers(held ? held->key : key->id, key->mask);
  // Synthetic legacy modifier cleanup cannot rewrite original ownership.
  observation.decision = pending.route;
  observation.decision.outcome = committed ? OriginalInputRouteOutcome::Original : OriginalInputRouteOutcome::Failed;
  return true;
}
Server::OriginalRouteObservation::OriginalRouteObservation(
    Server &owner, const Event &event, deskflow::windowshare::OriginalInputRouteKind kind
)
    : server(owner),
      previous(owner.m_originalRouteObservation),
      inputSequenceOrigin(
          owner.m_originalInputRoutes && owner.m_originalInputRoutes->live()
              ? event.originalInputStamp()
              : deskflow::windowshare::OriginalInputStamp{}
      ),
      exceptions(std::uncaught_exceptions()),
      lifetime(owner.m_originalLifetime)
{
  decision.stamp = event.originalInputStamp();
  decision.kind = kind;
  decision.server = server.m_serverIncarnation;
  if (server.m_active == server.m_primaryClient)
    decision.outcome = deskflow::windowshare::OriginalInputRouteOutcome::Local;
  server.m_originalRouteObservation = this;
}
Server::OriginalRouteObservation::~OriginalRouteObservation()
{
  if (!*lifetime)
    return;
  server.m_originalRouteObservation = previous;
  if (deferred || !decision.stamp || !server.m_originalInputRoutes)
    return;
  if (std::uncaught_exceptions() != exceptions)
    decision.outcome = deskflow::windowshare::OriginalInputRouteOutcome::Failed;
  server.m_originalInputRoutes->publish(std::move(decision));
}
void Server::observeOriginalSend(BaseClientProxy *client, bool relative) noexcept
{
  using namespace deskflow::windowshare;
  auto *observation = m_originalRouteObservation;
  if (!observation || !observation->decision.stamp || !m_originalInputRoutes)
    return;
  auto &decision = observation->decision;
  if (++observation->sends != 1) {
    decision.outcome = OriginalInputRouteOutcome::Ambiguous;
    decision.pointKnown = false;
    return;
  }
  if (client == m_primaryClient) {
    decision.outcome = OriginalInputRouteOutcome::Local;
    return;
  }
  const auto recipient = m_inputRecipients.find(client);
  if (recipient == m_inputRecipients.end()) {
    decision.outcome = OriginalInputRouteOutcome::Uncorrelated;
    return;
  }
  decision.recipient = recipient->second;
  if (m_originalExecuting && m_originalRouteObservation == m_originalExecutingObservation &&
      m_originalExecuting->route.recipient == decision.recipient) {
    const auto &frozen = m_originalExecuting->route;
    decision.pointKnown = frozen.pointKnown;
    decision.x = frozen.x;
    decision.y = frozen.y;
    decision.ticket = frozen.ticket;
    decision.outcome = frozen.outcome;
    if (!frozen.ticket.live())
      decision.outcome = OriginalInputRouteOutcome::Unauthenticated;
    return;
  }
  decision.pointKnown = client == m_active && !relative && !(m_relativeMoves && isLockedToComputerServer());
  if (decision.pointKnown) {
    decision.x = m_x;
    decision.y = m_y;
  }
  try {
    const auto *stream = client->getStream();
    const auto ticket = stream ? stream->authenticatedPeerTicket() : std::nullopt;
    if (!ticket || !ticket->live()) {
      decision.outcome = OriginalInputRouteOutcome::Unauthenticated;
      return;
    }
    decision.ticket = *ticket;
    decision.outcome = relative ? OriginalInputRouteOutcome::Relative : OriginalInputRouteOutcome::Dispatched;
    if (!decision.pointKnown && decision.kind != OriginalInputRouteKind::Key)
      decision.outcome = relative ? OriginalInputRouteOutcome::Relative : OriginalInputRouteOutcome::Uncorrelated;
  } catch (...) {
    decision.outcome = OriginalInputRouteOutcome::Failed;
  }
}

bool Server::setWindowDragEdgeMailbox(std::shared_ptr<deskflow::windowshare::WindowDragEdgeMailbox> mailbox)
{
  if (!mailbox || m_windowDragEdgeMailbox || m_selectionShutdown || !mailbox->bind(m_serverIncarnation))
    return false;
  m_windowDragEdgeMailbox = std::move(mailbox);
  return true;
}

void Server::invalidateWindowDragEdge(bool forgetPress) noexcept
{
  m_windowDragEdgePending.reset();
  if (m_windowDragEdgeMailbox)
    m_windowDragEdgeMailbox->clear();
  if (forgetPress) {
    m_windowDragPress = {};
    m_windowDragPressIncarnation = 0;
  }
}

void Server::drainWindowDragTransfer()
{
  using namespace deskflow::windowshare;
  const auto mailbox = m_windowDragEdgeMailbox;
  const auto request = mailbox ? mailbox->takeTransfer() : std::nullopt;
  if (!request)
    return;
  const auto lifetime = m_originalLifetime;
  const auto &release = request->release;
  const auto &edge = release.edge();
  auto *remote = inputRecipient(edge.recipient);
  auto *destination = edge.returning ? m_primaryClient : remote;
  auto *sourceClient = edge.returning ? remote : m_primaryClient;
  bool switched = false;
  if (release.live() && edge.server == m_serverIncarnation && !m_selectionShutdown && !m_selectionChanging &&
      !m_selectionScreensaver && !m_activeSaver && !m_keyboardBroadcasting && !isLockedToComputerServer() &&
      m_active == sourceClient && m_inputRoutes.size() == 0 && m_originalPending.empty() && remote &&
      remote != m_primaryClient && m_clientSet.contains(remote) && m_originalInputRoutes &&
      m_originalInputRoutes->live() && m_originalInputCaptured && m_originalInputCaptured->live()) {
    WindowDragScreen source, target;
    sourceClient->getShape(source.x, source.y, source.width, source.height);
    destination->getShape(target.x, target.y, target.width, target.height);
    const auto *stream = remote->getStream();
    const auto peer = stream ? stream->authenticatedPeerTicket() : std::nullopt;
    if (source == edge.source && target == edge.destination && target.contains(request->x, request->y) && peer &&
        peer->live() && peer->incarnation() == edge.peer.incarnation() &&
        peer->fingerprint() == edge.peer.fingerprint() && release.live()) {
      try {
        // No held press changes owner. SelectionChange invalidates the release
        // at this reservation; completion uses the separately retained request.
        switchComputer(destination, request->x, request->y, false);
        switched = *lifetime && m_active == destination &&
                   (edge.returning
                        ? !m_selectedPeer
                        : (m_selectedPeer && m_selectedPeer->live() && m_selectedPeer->recipient() == edge.recipient));
      } catch (...) {
        switched = false;
      }
    }
  }
  mailbox->completeTransfer(*request, switched);
}

bool Server::observeWindowDragEdge(
    BaseClientProxy *destination, Direction direction, int32_t x, int32_t y, int32_t sourceX, int32_t sourceY,
    double waitSeconds
)
{
  using namespace deskflow::windowshare;
  const bool returning = m_active != m_primaryClient && destination == m_primaryClient;
  auto *remote = returning ? m_active : destination;
  if (!m_windowDragEdgeMailbox || !m_windowDragPress || !m_windowDragPressIncarnation || m_selectionShutdown ||
      m_selectionChanging || m_selectionScreensaver || m_activeSaver || m_keyboardBroadcasting ||
      isLockedToComputerServer() || !destination || !remote || remote == m_primaryClient ||
      (!returning && m_active != m_primaryClient) || !m_clientSet.contains(remote) || !m_originalRouteObservation ||
      !m_originalInputRoutes || !m_originalInputRoutes->live())
    return false;
  const auto press = m_inputRoutes.repeat({InputRoutes::Kind::Button, 1});
  const auto recipient = m_inputRecipients.find(remote);
  const auto &motion = m_originalRouteObservation->decision;
  if (!press || press->incarnation != m_windowDragPressIncarnation || press->originalOwner ||
      (returning
           ? (recipient == m_inputRecipients.end() || press->recipients != InputRoutes::Recipients{recipient->second})
           : !press->recipients.empty()) ||
      recipient == m_inputRecipients.end() || motion.kind != OriginalInputRouteKind::Motion ||
      motion.stamp.epoch != m_windowDragPress.epoch || motion.stamp.sequence <= m_windowDragPress.sequence ||
      motion.stamp.part != 1)
    return false;
  const auto *stream = remote->getStream();
  const auto ticket = stream ? stream->authenticatedPeerTicket() : std::nullopt;
  if (!ticket || !ticket->live())
    return false;
  WindowDragEdge edge;
  edge.returning = returning;
  switch (direction) {
  case Direction::Left:
    edge.side = WindowDragEdgeSide::Left;
    break;
  case Direction::Right:
    edge.side = WindowDragEdgeSide::Right;
    break;
  case Direction::Top:
    edge.side = WindowDragEdgeSide::Top;
    break;
  case Direction::Bottom:
    edge.side = WindowDragEdgeSide::Bottom;
    break;
  default:
    return false;
  }
  edge.server = m_serverIncarnation;
  edge.recipient = recipient->second;
  edge.press = m_windowDragPress;
  edge.motion = motion.stamp;
  edge.sourceX = sourceX;
  edge.sourceY = sourceY;
  edge.destinationX = x;
  edge.destinationY = y;
  edge.peer = *ticket;
  m_active->getShape(edge.source.x, edge.source.y, edge.source.width, edge.source.height);
  destination->getShape(edge.destination.x, edge.destination.y, edge.destination.width, edge.destination.height);
  if (!edge.valid() || !std::isfinite(waitSeconds) || waitSeconds < 0)
    return false;
  const auto now = WindowDragEdgeMailbox::Clock::now();
  if (!m_windowDragEdgePending || !m_windowDragEdgePending->sameBoundary(edge)) {
    m_windowDragEdgeMailbox->clear();
    m_windowDragEdgePending = edge;
    m_windowDragEdgeEntered = now;
  }
  // Only an actual later stamped motion can finish this dwell. The ordinary
  // switch timer cannot manufacture drag motion or bypass the button owner.
  if (std::chrono::duration<double>(now - m_windowDragEdgeEntered).count() < waitSeconds)
    return true;
  return m_windowDragEdgeMailbox->publish(std::move(edge));
}

std::optional<deskflow::windowshare::SelectedPeer> Server::selectedPeer() const
{
  // No lazy publication: a failed switch or an in-progress transition must
  // stay ineligible even when m_active still points to its previous client.
  return m_selectedPeer && m_selectedPeer->live() ? m_selectedPeer : std::nullopt;
}

void Server::invalidateSelectedPeer() noexcept
{
  if (m_selectionRevoked)
    m_selectionRevoked->store(true, std::memory_order_release);
  m_selectionRevoked.reset();
  m_selectedPeer.reset();
  const auto owners = m_originalOwners;
  for (auto owner : owners)
    revokeOriginalOwner(owner);
}

void Server::revokeSelectedPeer() noexcept
{
  m_selectionShutdown = true;
  invalidateWindowDragEdge(true);
  if (m_windowDragEdgeMailbox)
    m_windowDragEdgeMailbox->close();
  if (m_originalInputRoutes)
    m_originalInputRoutes->revoke();
  if (m_originalInputCaptured)
    m_originalInputCaptured->revoke();
  cancelOriginalDispatch();
  invalidateSelectedPeer();
}

void Server::publishSelectedPeer() noexcept
{
  invalidateSelectedPeer();
  if (m_selectionShutdown || m_selectionChanging || !m_serverIncarnation || m_selectionScreensaver || m_activeSaver ||
      m_keyboardBroadcasting || !m_active || m_active == m_primaryClient || !m_clientSet.contains(m_active))
    return;
  const auto recipient = m_inputRecipients.find(m_active);
  if (recipient == m_inputRecipients.end() || !recipient->second)
    return;
  if (m_selectionGeneration == std::numeric_limits<std::uint64_t>::max()) {
    revokeSelectedPeer();
    return;
  }
  try {
    const auto *stream = m_active->getStream();
    const auto ticket = stream ? stream->authenticatedPeerTicket() : std::nullopt;
    if (!ticket || !ticket->live())
      return;
    auto revoked = std::make_shared<std::atomic_bool>(false);
    deskflow::windowshare::SelectedPeer selected{
        m_serverIncarnation, recipient->second, ++m_selectionGeneration, *ticket, revoked
    };
    m_selectionRevoked = std::move(revoked);
    m_selectedPeer = std::move(selected);
  } catch (...) {
    // Credential/allocator failures cannot leave an old selection valid or
    // escape a transition guard's destructor. Restart the Server to recover.
    revokeSelectedPeer();
  }
}

Server::InputRoutes::Recipients Server::inputRecipients(const char *computers) const
{
  if (m_originalExecuting && m_originalRouteObservation == m_originalExecutingObservation)
    return m_originalExecuting->recipients;
  InputRoutes::Recipients recipients;
  if (!m_keyboardBroadcasting && IKeyState::KeyInfo::isDefault(computers)) {
    const auto active = m_inputRecipients.find(m_active);
    if (active != m_inputRecipients.end())
      recipients.push_back(active->second);
    return recipients;
  }
  if (m_keyboardBroadcasting && IKeyState::KeyInfo::isDefault(computers)) {
    computers = m_keyboardBroadcastingComputers.c_str();
    if (IKeyState::KeyInfo::isDefault(computers))
      computers = "*";
  }
  for (const auto &[name, client] : m_clients) {
    if (!IKeyState::KeyInfo::contains(computers, name))
      continue;
    const auto recipient = m_inputRecipients.find(client);
    if (recipient != m_inputRecipients.end())
      recipients.push_back(recipient->second);
  }
  // The primary is deliberately absent: its hardware event already passed to
  // the OS. Synthesizing it again would duplicate local input or form a loop.
  return recipients;
}

BaseClientProxy *Server::inputRecipient(InputRoutes::Recipient recipient) const
{
  const auto found = m_inputClients.find(recipient);
  if (found == m_inputClients.end() || m_clientSet.find(found->second) == m_clientSet.end())
    return nullptr;
  return found->second;
}

void Server::forgetInputRecipient(BaseClientProxy *client, bool sendReleases)
{
  const auto lifetime = m_originalLifetime;
  const bool wasAlive = *lifetime;
  const auto found = m_inputRecipients.find(client);
  if (found == m_inputRecipients.end())
    return;
  const auto recipient = found->second;
  auto detached = m_inputRoutes.detach(recipient);
  m_inputRecipients.erase(found);
  m_inputClients.erase(recipient);
  if (!sendReleases)
    return;
  try {
    for (const auto &press : detached.presses) {
      switch (press.control.kind) {
      case InputRoutes::Kind::Key:
        client->keyUp(press.key, 0, static_cast<KeyButton>(press.button));
        break;
      case InputRoutes::Kind::Button:
        client->mouseUp(static_cast<ButtonID>(press.button));
        break;
      case InputRoutes::Kind::ModifierCommand:
        detached.modifiers |= press.mask;
        break;
      }
      if (wasAlive && !*lifetime)
        return;
    }
    if (detached.modifiers)
      client->keyDown(kKeyClearModifiers, detached.modifiers, 0, {});
  } catch (const std::exception &error) {
    // A peer can disappear during an orderly close. Ownership is already
    // detached; proceed with closing instead of reviving it on another client.
    LOG_WARN("input release failed while closing client: %s", error.what());
  }
}

void Server::routeModifierCommand(KeyID id, KeyModifierMask mask, const std::string &lang, const char *computers)
{
  const auto lifetime = m_originalLifetime;
  const auto recipients = inputRecipients(computers);
  std::map<InputRoutes::Recipient, KeyModifierMask> masks;
  // Configured mouse actions use paired Set/Clear commands, both delivered as
  // key-down events. Give each bit its own ownership instead of treating the
  // command as a physical key that will eventually receive a normal key-up.
  for (unsigned bit = 0; bit < 32; ++bit) {
    const KeyModifierMask flag = KeyModifierMask{1} << bit;
    if (!(mask & flag))
      continue;
    const InputRoutes::Control control{InputRoutes::Kind::ModifierCommand, bit};
    const auto route =
        id == kKeySetModifiers ? m_inputRoutes.down(control, id, flag, 0, recipients) : takeInputPress(control);
    if (route)
      for (auto recipient : route->recipients)
        masks[recipient] |= flag;
  }
  for (const auto &[recipient, routedMask] : masks)
    if (auto *client = inputRecipient(recipient)) {
      if (id == kKeySetModifiers)
        m_inputRoutes.observeModifiers(recipient, routedMask & routedMomentaryModifiers);
      else
        m_inputRoutes.forgetModifiers(recipient, routedMask);
      observeOriginalSend(client);
      client->keyDown(id, routedMask, 0, lang);
      if (!*lifetime)
        return;
    }
}

void Server::releaseRoutedModifiers(KeyID id, KeyModifierMask mask)
{
  const auto lifetime = m_originalLifetime;
  const auto releasedModifier = modifierForRoutedKey(id) & ~mask;
  for (const auto &release : m_inputRoutes.releaseModifiers(releasedModifier))
    if (auto *client = inputRecipient(release.recipient)) {
      observeOriginalSend(client);
      client->keyDown(kKeyClearModifiers, release.mask, 0, {});
      if (!*lifetime)
        return;
    }
}
#endif

void Server::onKeyDown(KeyID id, KeyModifierMask mask, KeyButton button, const std::string &lang, const char *computers)
{
  LOG_VERBOSE("onKeyDown id=%d mask=0x%04x button=0x%04x lang=%s", id, mask, button, lang.c_str());
  assert(m_active != nullptr);

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  const auto lifetime = m_originalLifetime;
  if (id == kKeyEscape)
    invalidateWindowDragEdge(true);
  if (deferOriginalInput(PendingKey{PendingKey::Phase::Down, id, mask, button, 0, lang}, inputRecipients(computers)))
    return;
  if (id == kKeySetModifiers || id == kKeyClearModifiers) {
    routeModifierCommand(id, mask, lang, computers);
    return;
  }
  const auto route =
      m_inputRoutes.down(InputRoutes::keyControl(id, button), id, mask, button, inputRecipients(computers));
  if (route)
    for (auto recipient : route->recipients)
      if (auto *client = inputRecipient(recipient)) {
        m_inputRoutes.observeModifiers(recipient, mask & routedMomentaryModifiers);
        // Repeated downs retain the original recipient. Some keyboard layouts send
        // multiple symbols per physical down; the client already handles these.
        observeOriginalSend(client);
        client->keyDown(id, mask, button, lang);
        if (!*lifetime)
          return;
      }
#else
  // relay
  if (!m_keyboardBroadcasting && IKeyState::KeyInfo::isDefault(computers)) {
    m_active->keyDown(id, mask, button, lang);
  } else {
    if (!computers && m_keyboardBroadcasting) {
      computers = m_keyboardBroadcastingComputers.c_str();
      if (IKeyState::KeyInfo::isDefault(computers)) {
        computers = "*";
      }
    }
    for (ClientList::const_iterator index = m_clients.begin(); index != m_clients.end(); ++index) {
      if (IKeyState::KeyInfo::contains(computers, index->first)) {
        index->second->keyDown(id, mask, button, lang);
      }
    }
  }
#endif
}

void Server::onKeyUp(KeyID id, KeyModifierMask mask, KeyButton button, const char *computers)
{
  LOG_VERBOSE("onKeyUp id=%d mask=0x%04x button=0x%04x", id, mask, button);
  assert(m_active != nullptr);

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  const auto lifetime = m_originalLifetime;
  if (deferOriginalInput(PendingKey{PendingKey::Phase::Up, id, mask, button}, inputRecipients(computers)))
    return;
  (void)computers; // Release destinations are the press snapshot, not new rules.
  const auto route = takeInputPress(InputRoutes::keyControl(id, button));
  if (route)
    for (auto recipient : route->recipients)
      if (auto *client = inputRecipient(recipient)) {
        observeOriginalSend(client);
        client->keyUp(id, mask, button);
        if (!*lifetime)
          return;
      }
  releaseRoutedModifiers(route ? route->key : id, mask);
#else
  // relay
  if (!m_keyboardBroadcasting && IKeyState::KeyInfo::isDefault(computers)) {
    m_active->keyUp(id, mask, button);
  } else {
    if (!computers && m_keyboardBroadcasting) {
      computers = m_keyboardBroadcastingComputers.c_str();
      if (IKeyState::KeyInfo::isDefault(computers)) {
        computers = "*";
      }
    }
    for (ClientList::const_iterator index = m_clients.begin(); index != m_clients.end(); ++index) {
      if (IKeyState::KeyInfo::contains(computers, index->first)) {
        index->second->keyUp(id, mask, button);
      }
    }
  }
#endif
}

void Server::onKeyRepeat(KeyID id, KeyModifierMask mask, int32_t count, KeyButton button, const std::string &lang)
{
  LOG(
      (CLOG_VERBOSE "onKeyRepeat id=%d mask=0x%04x count=%d button=0x%04x lang=\"%s\"", id, mask, count, button,
       lang.c_str())
  );
  assert(m_active != nullptr);

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  const auto lifetime = m_originalLifetime;
  if (deferOriginalInput(
          PendingKey{PendingKey::Phase::Repeat, id, mask, button, count, lang}, inputRecipients(nullptr)
      ))
    return;
  const auto route = m_inputRoutes.repeat(InputRoutes::keyControl(id, button));
  if (route)
    for (auto recipient : route->recipients)
      if (auto *client = inputRecipient(recipient)) {
        m_inputRoutes.observeModifiers(recipient, mask & routedMomentaryModifiers);
        observeOriginalSend(client);
        client->keyRepeat(id, mask, count, button, lang);
        if (!*lifetime)
          return;
      }
#else
  m_active->keyRepeat(id, mask, count, button, lang);
#endif
}

void Server::onMouseDown(ButtonID id)
{
  LOG_VERBOSE("onMouseDown id=%d", id);
  assert(m_active != nullptr);

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  const auto lifetime = m_originalLifetime;
  // Mouse buttons do not participate in keyboard broadcasting.
  InputRoutes::Recipients recipients;
  const auto active = m_inputRecipients.find(m_active);
  if (m_originalExecuting && m_originalRouteObservation == m_originalExecutingObservation)
    recipients = m_originalExecuting->recipients;
  else if (active != m_inputRecipients.end())
    recipients.push_back(active->second);
  if (deferOriginalInput(PendingButton{id, true}, recipients))
    return;
  invalidateWindowDragEdge(true);
  if (m_inputRoutes.repeat({InputRoutes::Kind::Button, id}))
    return;
  const auto route = m_inputRoutes.down({InputRoutes::Kind::Button, id}, 0, 0, id, std::move(recipients));
  if (id == 1 && route && !route->originalOwner && m_inputRoutes.size() == 1 &&
      ((m_active == m_primaryClient && route->recipients.empty()) ||
       (active != m_inputRecipients.end() && route->recipients == InputRoutes::Recipients{active->second})) &&
      m_originalRouteObservation && m_originalInputRoutes && m_originalInputRoutes->live()) {
    const auto stamp = m_originalRouteObservation->decision.stamp;
    if (stamp && stamp.part == 1) {
      m_windowDragPress = stamp;
      m_windowDragPressIncarnation = route->incarnation;
    }
  }
  if (route)
    for (auto recipient : route->recipients)
      if (auto *client = inputRecipient(recipient)) {
        observeOriginalSend(client);
        client->mouseDown(id);
        if (!*lifetime)
          return;
      }
#else
  m_active->mouseDown(id);
#endif
}

void Server::onMouseUp(ButtonID id)
{
  LOG_VERBOSE("onMouseUp id=%d", id);
  assert(m_active != nullptr);

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  const auto lifetime = m_originalLifetime;
  InputRoutes::Recipients recipients;
  const auto active = m_inputRecipients.find(m_active);
  if (active != m_inputRecipients.end())
    recipients.push_back(active->second);
  if (deferOriginalInput(PendingButton{id, false}, std::move(recipients)))
    return;
  const auto route = takeInputPress({InputRoutes::Kind::Button, id});
  if (route)
    for (auto recipient : route->recipients)
      if (auto *client = inputRecipient(recipient)) {
        observeOriginalSend(client);
        client->mouseUp(id);
        if (!*lifetime)
          return;
      }
  if (id == 1) {
    const bool local = route && route->recipients.empty() && m_active == m_primaryClient;
    const bool remote =
        route && active != m_inputRecipients.end() && route->recipients == InputRoutes::Recipients{active->second};
    const bool exactRelease = route && route->incarnation == m_windowDragPressIncarnation && !route->originalOwner &&
                              (local || remote) && m_inputRoutes.size() == 0 && m_originalRouteObservation &&
                              m_originalRouteObservation->decision.outcome ==
                                  (local ? deskflow::windowshare::OriginalInputRouteOutcome::Local
                                         : deskflow::windowshare::OriginalInputRouteOutcome::Dispatched);
    if (!exactRelease || !m_windowDragEdgeMailbox ||
        !m_windowDragEdgeMailbox->release(m_windowDragPress, m_originalRouteObservation->decision.stamp))
      invalidateWindowDragEdge(true);
    else {
      m_windowDragEdgePending.reset();
      m_windowDragPress = {};
      m_windowDragPressIncarnation = 0;
    }
  } else
    invalidateWindowDragEdge(true);
#else
  m_active->mouseUp(id);
#endif
}

bool Server::onMouseMovePrimary(int32_t x, int32_t y)
{
  LOG_VERBOSE("onMouseMovePrimary %d,%d", x, y);

  // mouse move on primary (server's) computer
  if (m_active != m_primaryClient) {
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    invalidateWindowDragEdge(true);
#endif
    // stale event -- we're actually on a secondary computer
    return false;
  }

  // save last delta
  m_xDelta2 = m_xDelta;
  m_yDelta2 = m_yDelta;

  // save current delta
  m_xDelta = x - m_x;
  m_yDelta = y - m_y;

  // save position
  m_x = x;
  m_y = y;

  // get computer shape
  int32_t ax;
  int32_t ay;
  int32_t aw;
  int32_t ah;
  m_active->getShape(ax, ay, aw, ah);
  int32_t zoneSize = getJumpZoneSize(m_active);

  // clamp position to computer
  int32_t xc = x;
  int32_t yc = y;
  if (xc < ax + zoneSize) {
    xc = ax;
  } else if (xc >= ax + aw - zoneSize) {
    xc = ax + aw - 1;
  }
  if (yc < ay + zoneSize) {
    yc = ay;
  } else if (yc >= ay + ah - zoneSize) {
    yc = ay + ah - 1;
  }

  // see if we should change computers
  // when the cursor is in a corner, there may be a computer either
  // horizontally or vertically.  check both directions.
  using enum Direction;
  auto dirh = NoDirection;
  auto dirv = NoDirection;
  int32_t xh = x;
  int32_t yv = y;
  if (x < ax + zoneSize) {
    xh -= zoneSize;
    dirh = Left;
  } else if (x >= ax + aw - zoneSize) {
    xh += zoneSize;
    dirh = Right;
  }
  if (y < ay + zoneSize) {
    yv -= zoneSize;
    dirv = Top;
  } else if (y >= ay + ah - zoneSize) {
    yv += zoneSize;
    dirv = Bottom;
  }
  if (dirh == NoDirection && dirv == NoDirection) {
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    invalidateWindowDragEdge();
#endif
    // still on local computer
    noSwitch(x, y);
    return false;
  }

  // check both horizontally and vertically
  std::array<Direction, 2> dirs = {dirh, dirv};
  std::array<int32_t, 2> xs = {xh, x};
  std::array<int32_t, 2> ys = {y, yv};
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  bool edgeObserved = false;
#endif
  for (int i = 0; i < 2; ++i) {
    Direction dir = dirs.at(i);
    if (dir == NoDirection) {
      continue;
    }
    x = xs.at(i);
    y = ys.at(i);
    // get jump destination
    BaseClientProxy *newComputer = mapToNeighbor(m_active, dir, x, y);

    // should we switch or not?
    double dragWait = -1;
    const bool switchOkay = isSwitchOkay(newComputer, dir, x, y, xc, yc, &dragWait);
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    if (!edgeObserved && dragWait >= 0)
      edgeObserved = observeWindowDragEdge(newComputer, dir, x, y, xc, yc, dragWait);
#endif
    if (switchOkay) {
      // switch computer
      switchComputer(newComputer, x, y, false);
      return true;
    }
  }

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (!edgeObserved)
    invalidateWindowDragEdge();
#endif
  return false;
}

void Server::onMouseMoveSecondary(int32_t dx, int32_t dy)
{
  LOG_VERBOSE("mouse move on secondary: %+d,%+d", dx, dy);

  // TODO: move this to client side and use a qt setting or cli arg instead of env var.
  const static auto adjustEnv = "DESKFLOW_MOUSE_ADJUSTMENT";
  if (const char *envVal = std::getenv(adjustEnv); envVal) {
    try {
      double multiplier = std::stod(envVal);
      dx = static_cast<int32_t>(std::round(dx * multiplier));
      dy = static_cast<int32_t>(std::round(dy * multiplier));
      LOG_VERBOSE("adjusted mouse x %.2f: %+d,%+d", multiplier, dx, dy);
    } catch (const std::exception &e) {
      LOG_ERR("invalid %s value: %s", adjustEnv, e.what());
    }
  }

  // mouse move on secondary (client's) computer
  assert(m_active != nullptr);
  if (m_active == m_primaryClient) {
    // stale event -- we're actually on the primary computer
    return;
  }

  // if doing relative motion on secondary computers and we're locked
  // to the computer (which activates relative moves) then send a
  // relative mouse motion.  when we're doing this we pretend as if
  // the mouse isn't actually moving because we're expecting some
  // program on the secondary computer to warp the mouse on us, so we
  // have no idea where it really is.
  if (m_relativeMoves && isLockedToComputerServer()) {
    LOG_VERBOSE("relative move on %s by %d,%d", getName(m_active).c_str(), dx, dy);
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    const auto active = m_inputRecipients.find(m_active);
    if (active != m_inputRecipients.end() && deferOriginalInput(PendingMotion{dx, dy, true}, {active->second}))
      return;
    observeOriginalSend(m_active, true);
#endif
    m_active->mouseRelativeMove(dx, dy);
    return;
  }

  // save old position
  const int32_t xOld = m_x;
  const int32_t yOld = m_y;

  // save last delta
  m_xDelta2 = m_xDelta;
  m_yDelta2 = m_yDelta;

  // save current delta
  m_xDelta = dx;
  m_yDelta = dy;

  // accumulate motion
  m_x += dx;
  m_y += dy;

  // get computer shape
  int32_t ax;
  int32_t ay;
  int32_t aw;
  int32_t ah;
  m_active->getShape(ax, ay, aw, ah);

  // find direction of neighbor and get the neighbor
  bool jump = true;
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  bool edgeObserved = false;
#endif
  BaseClientProxy *newComputer;
  do {
    // clamp position to computer
    int32_t xc = m_x;
    int32_t yc = m_y;
    if (xc < ax) {
      xc = ax;
    } else if (xc >= ax + aw) {
      xc = ax + aw - 1;
    }
    if (yc < ay) {
      yc = ay;
    } else if (yc >= ay + ah) {
      yc = ay + ah - 1;
    }

    Direction dir;
    using enum Direction;
    if (m_x < ax) {
      dir = Left;
    } else if (m_x > ax + aw - 1) {
      dir = Right;
    } else if (m_y < ay) {
      dir = Top;
    } else if (m_y > ay + ah - 1) {
      dir = Bottom;
    } else {
      // we haven't left the computer
      newComputer = m_active;
      jump = false;

      // if waiting and mouse is not on the border we're waiting
      // on then stop waiting.  also if it's not on the border
      // then arm the double tap.
      if (m_switchComputer != nullptr) {
        bool clearWait;
        int32_t zoneSize = m_primaryClient->getJumpZoneSize();
        switch (m_switchDir) {
        case Left:
          clearWait = (m_x >= ax + zoneSize);
          break;

        case Right:
          clearWait = (m_x <= ax + aw - 1 - zoneSize);
          break;

        case Top:
          clearWait = (m_y >= ay + zoneSize);
          break;

        case Bottom:
          clearWait = (m_y <= ay + ah - 1 + zoneSize);
          break;

        default:
          clearWait = false;
          break;
        }
        if (clearWait) {
          // still on local computer
          noSwitch(m_x, m_y);
        }
      }

      // skip rest of block
      break;
    }

    // try to switch computer.  get the neighbor.
    newComputer = mapToNeighbor(m_active, dir, m_x, m_y);

    // see if we should switch
    double dragWait = -1;
    const bool switchOkay = isSwitchOkay(newComputer, dir, m_x, m_y, xc, yc, &dragWait);
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    if (dragWait >= 0)
      edgeObserved = observeWindowDragEdge(newComputer, dir, m_x, m_y, xc, yc, dragWait);
#endif
    if (!switchOkay) {
      newComputer = m_active;
      jump = false;
    }
  } while (false);

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (!edgeObserved)
    invalidateWindowDragEdge();
#endif
  if (jump) {
    int32_t newX = m_x;
    int32_t newY = m_y;

    // switch computers
    switchComputer(newComputer, newX, newY, false);
  } else {
    // same computer.  clamp mouse to edge.
    m_x = xOld + dx;
    m_y = yOld + dy;
    if (m_x < ax) {
      m_x = ax;
      LOG_VERBOSE("clamp to left of \"%s\"", getName(m_active).c_str());
    } else if (m_x > ax + aw - 1) {
      m_x = ax + aw - 1;
      LOG_VERBOSE("clamp to right of \"%s\"", getName(m_active).c_str());
    }
    if (m_y < ay) {
      m_y = ay;
      LOG_VERBOSE("clamp to top of \"%s\"", getName(m_active).c_str());
    } else if (m_y > ay + ah - 1) {
      m_y = ay + ah - 1;
      LOG_VERBOSE("clamp to bottom of \"%s\"", getName(m_active).c_str());
    }

    // warp cursor if it moved.
    if (m_x != xOld || m_y != yOld) {
      LOG_VERBOSE("move on %s to %d,%d", getName(m_active).c_str(), m_x, m_y);
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
      const auto active = m_inputRecipients.find(m_active);
      if (active != m_inputRecipients.end() && deferOriginalInput(PendingMotion{m_x, m_y}, {active->second}))
        return;
      observeOriginalSend(m_active);
#endif
      m_active->mouseMove(m_x, m_y);
    }
  }
}

void Server::onMouseWheel(int32_t xDelta, int32_t yDelta)
{
  LOG_VERBOSE("onMouseWheel %+d,%+d", xDelta, yDelta);
  assert(m_active != nullptr);

  // relay
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  const auto active = m_inputRecipients.find(m_active);
  if (active != m_inputRecipients.end() && deferOriginalInput(PendingScroll{xDelta, yDelta}, {active->second}))
    return;
  observeOriginalSend(m_active);
#endif
  m_active->mouseWheel(xDelta, yDelta);
}

bool Server::addClient(BaseClientProxy *client)
{
  std::string name = getName(client);
  if (m_clients.contains(name)) {
    return false;
  }

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  InputRoutes::Recipient recipient = 0;
  if (client != m_primaryClient) {
    try {
      recipient = m_inputRoutes.attach();
    } catch (const std::overflow_error &) {
      revokeSelectedPeer();
      LOG_ERR("input recipient sequence exhausted; refusing new client");
      return false;
    }
  }
#endif

  // add event handlers
  m_events->addHandler(EventTypes::ComputerShapeChanged, client->getEventTarget(), [this, client](const auto &) {
    handleShapeChanged(client);
  });
  m_events->addHandler(EventTypes::ClipboardGrabbed, client->getEventTarget(), [this, client](const auto &e) {
    handleClipboardGrabbed(e, client);
  });
  m_events->addHandler(EventTypes::ClipboardChanged, client->getEventTarget(), [this, client](const auto &e) {
    handleClipboardChanged(e, client);
  });

  // add to list
  m_clientSet.insert(client);
  m_clients.try_emplace(name, client);

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (client != m_primaryClient) {
    m_inputRecipients.emplace(client, recipient);
    m_inputClients.emplace(recipient, client);
  }
#endif

  // initialize client data
  int32_t x;
  int32_t y;
  client->getCursorPos(x, y);
  client->setJumpCursorPos(x, y);

  // tell primary client about the active sides
  m_primaryClient->reconfigure(getActivePrimarySides());

  return true;
}

bool Server::removeClient(BaseClientProxy *client)
{
  using enum EventTypes;
  // return false if not in list
  ClientSet::iterator i = m_clientSet.find(client);
  if (i == m_clientSet.end()) {
    return false;
  }

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  // This path also runs after an unexpected disconnect. Never write releases
  // through a dead proxy or redirect them to the new active computer.
  forgetInputRecipient(client, false);
  SelectionChange selection(*this);
  if (!selection.alive())
    return false;
  i = m_clientSet.find(client);
  if (i == m_clientSet.end())
    return false;
#endif

  // remove event handlers
  m_events->removeHandler(ComputerShapeChanged, client->getEventTarget());
  m_events->removeHandler(ClipboardGrabbed, client->getEventTarget());
  m_events->removeHandler(ClipboardChanged, client->getEventTarget());

  // remove from list
  m_clients.erase(getName(client));
  m_clientSet.erase(i);

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  selection.complete();
#endif
  return true;
}

void Server::closeClient(BaseClientProxy *client, const char *msg)
{
  assert(client != m_primaryClient);
  assert(msg != nullptr);

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (m_disconnectingInputClients.contains(client))
    return;
  const bool known = m_clientSet.contains(client) || m_oldClients.contains(client);
  SelectionChange selection(*this);
  if (!selection.alive() || (known && !m_clientSet.contains(client) && !m_oldClients.contains(client)))
    return;
#endif

  // send message to client.  this message should cause the client
  // to disconnect.  we add this client to the closed client list
  // and install a timer to remove the client if it doesn't respond
  // quickly enough.  we also remove the client from the active
  // client list since we're not going to listen to it anymore.
  // note that this method also works on clients that are not in
  // the m_clients list.  adoptClient() may call us with such a
  // client.
  LOG_INFO("disconnecting client \"%s\"", getName(client).c_str());

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  // Queue owned releases while the connection still accepts protocol writes.
  forgetInputRecipient(client, true);
  if (!selection.alive())
    return;
#endif

  // send message
  // FIXME -- avoid type cast (kinda hard, though)
  auto clientProxy = static_cast<ClientProxy *>(client);
  clientProxy->close(msg);

  // install timer.  wait timeout seconds for client to close.
  double timeout = 5.0;
  EventQueueTimer *timer = m_events->newOneShotTimer(timeout, nullptr);
  m_events->addHandler(EventTypes::Timer, timer, [this, client](const auto &) { handleClientCloseTimeout(client); });

  // move client to closing list
  removeClient(client);

  m_oldClients.try_emplace(client, timer);

  // if this client is the active computer then we have to
  // jump off of it
  forceLeaveClient(client);
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  selection.complete();
#endif
}

void Server::closeClients(const ServerConfig &config)
{
  // collect the clients that are connected but are being dropped
  // from the configuration (or who's canonical name is changing).
  using RemovedClients = std::set<BaseClientProxy *>;
  RemovedClients removed;
  for (auto index = m_clients.begin(); index != m_clients.end(); ++index) {
    if (!config.isCanonicalName(index->first)) {
      removed.insert(index->second);
    }
  }

  // don't close the primary client
  removed.erase(m_primaryClient);

  // now close them.  we collect the list then close in two steps
  // because closeClient() modifies the collection we iterate over.
  for (auto &client : removed) {
    closeClient(client, kMsgCClose);
  }
}

void Server::removeActiveClient(BaseClientProxy *client)
{
  if (removeClient(client)) {
    forceLeaveClient(client);
    m_events->removeHandler(EventTypes::ClientProxyDisconnected, client);
    if (m_clients.size() == 1 && m_oldClients.empty()) {
      m_events->addEvent(Event(EventTypes::ServerDisconnected, this));
    }
  }
}

void Server::removeOldClient(BaseClientProxy *client)
{
  using enum EventTypes;
  OldClients::iterator i = m_oldClients.find(client);
  if (i != m_oldClients.end()) {
    m_events->removeHandler(ClientProxyDisconnected, client);
    m_events->removeHandler(Timer, i->second);
    m_events->deleteTimer(i->second);
    m_oldClients.erase(i);
    if (m_clients.size() == 1 && m_oldClients.empty()) {
      m_events->addEvent(Event(ServerDisconnected, this));
    }
  }
}

void Server::forceLeaveClient(const BaseClientProxy *client)
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  SelectionChange selection(*this);
  if (!selection.alive())
    return;
#endif
  if (const auto *active = (m_activeSaver != nullptr) ? m_activeSaver : m_active; active == client) {
    // record new position (center of primary computer)
    m_primaryClient->getCursorCenter(m_x, m_y);

    // stop waiting to switch to this client
    if (active == m_switchComputer) {
      stopSwitch();
    }

    // don't notify active computer since it has probably already
    // disconnected.
    LOG(
        (CLOG_INFO "jump from \"%s\" to \"%s\" at %d,%d", getName(active).c_str(), getName(m_primaryClient).c_str(),
         m_x, m_y)
    );

    // cut over
    m_active = m_primaryClient;

    // enter new computer (unless we already have because of the
    // screen saver)
    if (m_activeSaver == nullptr) {
      m_primaryClient->enter(m_x, m_y, m_seqNum, m_primaryClient->getToggleMask(), false);
    }
  }

  // if this computer had the cursor when the screen saver activated
  // then we can't switch back to it when the screen saver
  // deactivates.
  if (m_activeSaver == client) {
    m_activeSaver = nullptr;
  }

  // tell primary client about the active sides
  m_primaryClient->reconfigure(getActivePrimarySides());
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  selection.complete();
#endif
}
