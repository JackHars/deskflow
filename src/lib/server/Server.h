/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Event.h"
#include "base/Stopwatch.h"
#include "common/NetworkProtocol.h"
#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/KeyTypes.h"
#include "deskflow/MouseTypes.h"
#include "server/Config.h"
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
#include "deskflow/InputSequence.h"
#include "windowshare/InputRouteState.h"
#include "windowshare/OriginalInputBackend.h"
#include "windowshare/OriginalInputCapturedInbox.h"
#include "windowshare/OriginalInputDispatch.h"
#include "windowshare/OriginalInputRoute.h"
#include "windowshare/SelectedPeer.h"
#include "windowshare/WindowDragEdge.h"
#endif

#include <climits>
#include <deque>
#include <map>
#include <set>
#include <vector>

class BaseClientProxy;
class EventQueueTimer;
class PrimaryClient;
class InputFilter;
namespace deskflow {
class Computer;
}
class IEventQueue;
class Thread;
class ClientListener;

//! Deskflow server
/*!
This class implements the top-level server algorithms for deskflow.
*/
class Server
{
  using ServerConfig = deskflow::server::Config;
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  friend class ServerTests;
#endif

public:
  //! Lock cursor to computer data
  class LockCursorToComputerInfo : public EventData
  {
  public:
    enum State
    {
      kOff,
      kOn,
      kToggle
    };

    explicit LockCursorToComputerInfo(State state = kToggle) : m_state(state)
    {
      // do nothing
    }
    ~LockCursorToComputerInfo() override = default; // do nothing

  public:
    State m_state;
  };

  //! Switch to computer data
  class SwitchToComputerInfo : public EventData
  {
  public:
    explicit SwitchToComputerInfo(const std::string &computer) : m_computer(computer)
    {
      // do nothing
    }
    ~SwitchToComputerInfo() override = default; // do nothing

  public:
    std::string m_computer;
  };

  //! Switch in direction data
  class SwitchInDirectionInfo : public EventData
  {
  public:
    explicit SwitchInDirectionInfo(Direction direction) : m_direction(direction)
    {
      // do nothing
    }
    ~SwitchInDirectionInfo() override = default; // do nothing

  public:
    Direction m_direction;
  };

  //! Computer connected data
  class ComputerConnectedInfo : public EventData
  {
  public:
    explicit ComputerConnectedInfo(std::string computer) : m_computer(computer)
    {
      // do nothing
    }

  public:
    std::string m_computer;
  };

  //! Keyboard broadcast data
  class KeyboardBroadcastInfo : public EventData
  {
  public:
    enum State
    {
      kOff,
      kOn,
      kToggle
    };

    explicit KeyboardBroadcastInfo(State state = kToggle) : m_state(state)
    {
      // do nothing
    }
    KeyboardBroadcastInfo(State state, const std::string &computers) : m_state(state), m_computers(computers)
    {
      // do nothing
    }
    ~KeyboardBroadcastInfo() override = default; // do nothing

  public:
    State m_state;
    std::string m_computers;
  };

  /*!
  Start the server with the configuration \p config and the primary
  client (local computer) \p primaryClient.  The client retains
  ownership of \p primaryClient.
  */
  Server(ServerConfig &config, PrimaryClient *primaryClient, deskflow::Computer *computer, IEventQueue *events);
  Server(Server const &) = delete;
  Server(Server &&) = delete;
  ~Server();

  Server &operator=(Server const &) = delete;
  Server &operator=(Server &&) = delete;

  //! @name manipulators
  //@{

  //! Set configuration
  /*!
  Change the server's configuration.  Returns true iff the new
  configuration was accepted (it must include the server's name).
  This will disconnect any clients no longer in the configuration.
  */
  bool setConfig(const ServerConfig &);

  //! Add a client
  /*!
  Adds \p client to the server.  The client is adopted and will be
  destroyed when the client disconnects or is disconnected.
  */
  void adoptClient(BaseClientProxy *client);

  //! Disconnect clients
  /*!
  Disconnect clients.  This tells them to disconnect but does not wait
  for them to actually do so.  The server sends the disconnected event
  when they're all disconnected (or immediately if none are connected).
  The caller can also just destroy this object to force the disconnection.
  */
  void disconnect();

  //! Store ClientListener pointer
  void setListener(ClientListener *p)
  {
    m_clientListener = p;
  }

  //@}
  //! @name accessors
  //@{

  //! Get the network protocol
  /*!
  Returns the network protocol used by the server.
  */
  std::string protocolString() const;

  //! Get number of connected clients
  /*!
  Returns the number of connected clients, including the server itself.
  */
  uint32_t getNumClients() const;

  //! Get the list of connected clients
  /*!
  Set the \c list to the names of the currently connected clients.
  */
  void getClients(std::vector<std::string> &list) const;
  void sendConnectedClientsIpc() const;
  size_t getMaximumClipboardSizeBytes() const;

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  // Core event-queue thread only. The copied value contains no client/stream
  // pointers and remains safe to inspect after this Server is destroyed.
  std::optional<deskflow::windowshare::SelectedPeer> selectedPeer() const;
  bool setWindowDragEdgeMailbox(std::shared_ptr<deskflow::windowshare::WindowDragEdgeMailbox> mailbox);

  // Terminal shutdown fence. Call before entering any shutdown nested loop.
  // A subsequent client/configuration event cannot publish new authority.
  void revokeSelectedPeer() noexcept;
  // Bind once on the core thread before dispatch; failure denies observation
  // without changing ordinary routing. Server shutdown revokes retained joins.
  bool setOriginalInputRoutes(std::shared_ptr<deskflow::windowshare::OriginalInputRoutes> routes) noexcept;
  bool setOriginalInputCapturedInbox(std::shared_ptr<deskflow::windowshare::OriginalInputCapturedInbox> inbox);

  using OriginalInputDispatcher = deskflow::windowshare::OriginalInputDispatcher;
  // One-shot registration of a trusted provider. Inbox registration alone
  // changes no legacy packets or ownership. Production must establish native
  // prerequisites independently before installing this provider.
  bool setOriginalInputDispatcher(std::shared_ptr<OriginalInputDispatcher> dispatcher) noexcept;
  deskflow::windowshare::OriginalInputBackend originalInputBackend();
#endif

  //@}

private:
  // get canonical name of client
  std::string getName(const BaseClientProxy *) const;

  // get the sides of the primary computer that have neighbors
  uint32_t getActivePrimarySides() const;

  // returns true iff mouse should be locked to the current computer
  // according to this object only, ignoring what the primary client
  // says.
  bool isLockedToComputerServer() const;

  // returns true iff mouse should be locked to the current m_computer
  // according to this object or the primary client.
  bool isLockedToComputer() const;

  // returns the jump zone of the client
  int32_t getJumpZoneSize(const BaseClientProxy *) const;

  // change the active m_computer
  void switchComputer(BaseClientProxy *, int32_t x, int32_t y, bool forScreenSaver);

  // jump to computer
  void jumpToComputer(BaseClientProxy *);

  // convert pixel position to fraction, using x or y depending on the
  // direction.
  float mapToFraction(const BaseClientProxy *, Direction, int32_t x, int32_t y) const;

  // convert fraction to pixel position, writing only x or y depending
  // on the direction.
  void mapToPixel(const BaseClientProxy *, Direction, float f, int32_t &x, int32_t &y) const;

  // returns true if the client has a neighbor anywhere along the edge
  // indicated by the direction.
  bool hasAnyNeighbor(const BaseClientProxy *, Direction) const;

  // lookup neighboring computer, mapping the coordinate independent of
  // the direction to the neighbor's coordinate space.
  BaseClientProxy *getNeighbor(const BaseClientProxy *, Direction, int32_t &x, int32_t &y) const;

  // lookup neighboring computer.  given a position relative to the
  // source computer, find the computer we should move onto and where.
  // if the position is sufficiently far from the source then we
  // cross multiple computers.  if there is no suitable computer then
  // return nullptr and x,y are not modified.
  BaseClientProxy *mapToNeighbor(BaseClientProxy *, Direction, int32_t &x, int32_t &y) const;

  // adjusts x and y or neither to avoid ending up in a jump zone
  // after entering the client in the given direction.
  void avoidJumpZone(const BaseClientProxy *, Direction, int32_t &x, int32_t &y) const;

  // test if a switch is permitted.  this includes testing user
  // options like switch delay and tracking any state required to
  // implement them.  returns true iff a switch is permitted.
  bool isSwitchOkay(
      BaseClientProxy *dst, Direction, int32_t x, int32_t y, int32_t xActive, int32_t yActive,
      double *dragWait = nullptr
  );

  // update switch state due to a mouse move at \p x, \p y that
  // doesn't switch computers.
  void noSwitch(int32_t x, int32_t y);

  // stop switch timers
  void stopSwitch();

  // start two tap switch timer
  void startSwitchTwoTap();

  // arm the two tap switch timer if \p x, \p y is outside the tap zone
  void armSwitchTwoTap(int32_t x, int32_t y);

  // stop the two tap switch timer
  void stopSwitchTwoTap();

  // returns true iff the two tap switch timer is started
  bool isSwitchTwoTapStarted() const;

  // returns true iff should switch because of two tap
  bool shouldSwitchTwoTap() const;

  // start delay switch timer
  void startSwitchWait(int32_t x, int32_t y);

  // stop delay switch timer
  void stopSwitchWait();

  // returns true iff the delay switch timer is started
  bool isSwitchWaitStarted() const;

  // returns the corner (ScreenSwitchCornerMasks) where x,y is on the
  // given client.  corners have the given size.
  uint32_t getCorner(const BaseClientProxy *, int32_t x, int32_t y, int32_t size) const;

  // stop relative mouse moves
  void stopRelativeMoves();

  // send computer options to \c client
  void sendOptions(BaseClientProxy *client) const;

  // process options from configuration
  void processOptions();

  // event handlers
  void handleShapeChanged(BaseClientProxy *client);
  void handleClipboardGrabbed(const Event &event, BaseClientProxy *client);
  void handleClipboardChanged(const Event &event, BaseClientProxy *client);
  void handleKeyDownEvent(const Event &event);
  void handleKeyUpEvent(const Event &event);
  void handleKeyRepeatEvent(const Event &event);
  void handleButtonDownEvent(const Event &event);
  void handleButtonUpEvent(const Event &event);
  void handleMotionPrimaryEvent(const Event &event);
  void handleMotionSecondaryEvent(const Event &event);
  void handleWheelEvent(const Event &event);
  void handleSwitchWaitTimeout();
  void handleClientDisconnected(BaseClientProxy *client);
  void handleClientCloseTimeout(BaseClientProxy *client);
  void handleSwitchToComputerEvent(const Event &event);
  void handleSwitchInDirectionEvent(const Event &event);
  void handleToggleComputerEvent(const Event &);
  void handleKeyboardBroadcastEvent(const Event &event);
  void handleLockCursorToComputerEvent(const Event &event);

  // event processing
  void onClipboardChanged(const BaseClientProxy *sender, ClipboardID id, uint32_t seqNum);
  void onScreensaver(bool activated);
  void onKeyDown(KeyID, KeyModifierMask, KeyButton, const std::string &, const char *computers);
  void onKeyUp(KeyID, KeyModifierMask, KeyButton, const char *computers);
  void onKeyRepeat(KeyID, KeyModifierMask, int32_t, KeyButton, const std::string &);
  void onMouseDown(ButtonID);
  void onMouseUp(ButtonID);
  bool onMouseMovePrimary(int32_t x, int32_t y);
  void onMouseMoveSecondary(int32_t dx, int32_t dy);
  void onMouseWheel(int32_t xDelta, int32_t yDelta);

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  using InputRoutes = deskflow::windowshare::InputRouteState;
  InputRoutes::Recipients inputRecipients(const char *computers) const;
  BaseClientProxy *inputRecipient(InputRoutes::Recipient recipient) const;
  void forgetInputRecipient(BaseClientProxy *client, bool sendReleases);
  void routeModifierCommand(KeyID id, KeyModifierMask mask, const std::string &lang, const char *computers);
  class SelectionChange;
  static std::uint64_t reserveServerIncarnation(std::atomic<std::uint64_t> &next) noexcept;
  void invalidateSelectedPeer() noexcept;
  void publishSelectedPeer() noexcept;
  void invalidateWindowDragEdge(bool forgetPress = false) noexcept;
  void drainWindowDragTransfer();
  bool observeWindowDragEdge(
      BaseClientProxy *destination, Direction direction, int32_t x, int32_t y, int32_t sourceX, int32_t sourceY,
      double waitSeconds
  );
#endif

  // add client to list and attach event handlers for client
  bool addClient(BaseClientProxy *);

  // remove client from list and detach event handlers for client
  bool removeClient(BaseClientProxy *);

  // close a client
  void closeClient(BaseClientProxy *, const char *msg);

  // close clients not in \p config
  void closeClients(const ServerConfig &config);

  // close all clients whether they've completed the handshake or not,
  // except the primary client
  void closeAllClients();

  // remove clients from internal state
  void removeActiveClient(BaseClientProxy *);
  void removeOldClient(BaseClientProxy *);

  // force the cursor off of \p client
  void forceLeaveClient(const BaseClientProxy *client);

private:
  class ClipboardInfo
  {
  public:
    ClipboardInfo() = default;

  public:
    Clipboard m_clipboard;
    std::string m_clipboardData;
    std::string m_clipboardOwner;
    uint32_t m_clipboardSeqNum = 0;
  };
  // Order suggested by clang

  // the Primary Computer Client
  PrimaryClient *m_primaryClient = nullptr;

  // the client with focus
  BaseClientProxy *m_active = nullptr;

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  class OriginalRouteObservation
  {
  public:
    OriginalRouteObservation(Server &server, const Event &event, deskflow::windowshare::OriginalInputRouteKind kind);
    ~OriginalRouteObservation();
    OriginalRouteObservation(const OriginalRouteObservation &) = delete;
    OriginalRouteObservation &operator=(const OriginalRouteObservation &) = delete;
    Server &server;
    OriginalRouteObservation *previous;
    deskflow::windowshare::OriginalInputRouteDecision decision;
    deskflow::windowshare::InputSequenceOriginScope inputSequenceOrigin;
    unsigned sends = 0;
    int exceptions;
    bool deferred = false;
    std::shared_ptr<bool> lifetime;
  };
  void observeOriginalSend(BaseClientProxy *client, bool relative = false) noexcept;
  using OriginalClock = std::chrono::steady_clock;
  struct PendingKey
  {
    enum class Phase
    {
      Down,
      Up,
      Repeat
    } phase;
    KeyID id;
    KeyModifierMask mask;
    KeyButton button;
    std::int32_t count = 0;
    std::string language;
  };
  struct PendingButton
  {
    ButtonID id;
    bool down;
  };
  struct PendingMotion
  {
    std::int32_t x, y;
    bool relative = false;
  };
  struct PendingScroll
  {
    std::int32_t x, y;
  };
  using PendingOperation = std::variant<PendingKey, PendingButton, PendingMotion, PendingScroll>;
  struct PendingInput
  {
    PendingOperation operation;
    InputRoutes::Recipients recipients;
    deskflow::windowshare::OriginalInputRouteDecision route;
    std::optional<deskflow::windowshare::SelectedPeer> selected;
    OriginalClock::time_point deadline; // Capture-arrival deadline, not time spent waiting behind other input.
    std::optional<deskflow::windowshare::OriginalInputCaptured> captured{};
    bool captureResolved = false;
    std::uint64_t preparation = 0;
    OriginalClock::time_point preparationDeadline{};
  };
  struct CompletedInput
  {
    deskflow::windowshare::OriginalInputCaptured captured;
    OriginalClock::time_point deadline, receivedAt;
    std::uint32_t parts = 0;
    bool upResolved = false;
  };
  static constexpr std::size_t originalPendingCapacity = 256;
  static constexpr auto originalDispatchTimeout = std::chrono::milliseconds(100);
  // Native preparation has a separate, explicit budget. The 100 ms capture
  // deadline remains unchanged; a timely capture can wait behind preparation.
  static constexpr auto originalPreparationTimeout = std::chrono::milliseconds(500);
  bool deferOriginalInput(PendingOperation operation, InputRoutes::Recipients recipients);
  void drainOriginalInput(OriginalClock::time_point now = OriginalClock::now());
  // False retains this exact operation at the queue head until preparation is
  // resolved. Its diagnostic decision remains deferred, not an early denial.
  bool executeOriginalInput(
      PendingInput &pending, const deskflow::windowshare::OriginalInputCaptured *captured,
      OriginalClock::time_point now = OriginalClock::now()
  );
  void suppressOriginalInput(const PendingInput &pending, const deskflow::windowshare::OriginalInputCaptured *captured);
  void executeLegacyInput(const PendingInput &pending);
  void cancelOriginalDispatch(const char *reason = "capture or routing authority ended") noexcept;
  void cancelInFlightOriginal();
  void flushOriginalBeforeSelection();
  void retireOriginalCapture(const deskflow::windowshare::OriginalInputCaptured &captured);
  std::optional<InputRoutes::Press> takeInputPress(InputRoutes::Control control);
  void revokeOriginalOwner(std::uint64_t owner) noexcept;
  void releaseRoutedModifiers(KeyID id, KeyModifierMask mask);
  deskflow::windowshare::OriginalInputRouteDecision
  originalDispatchRoute(const InputRoutes::Recipients &recipients, bool relative) const;
  std::shared_ptr<bool> m_originalLifetime = std::make_shared<bool>(true);
  std::shared_ptr<deskflow::windowshare::OriginalInputCapturedInbox> m_originalInputCaptured;
  std::shared_ptr<OriginalInputDispatcher> m_originalDispatcher;
  EventQueueTimer *m_originalDispatchTimer = nullptr;
  std::deque<PendingInput> m_originalPending;
  std::map<std::pair<std::uint64_t, std::uint64_t>, CompletedInput> m_originalCompleted;
  std::set<std::uint64_t> m_originalReleases;
  std::set<std::uint64_t> m_originalOwners;
  std::set<std::uint64_t> m_originalRevokedOwners;
  struct OriginalPhysicalPress
  {
    InputRoutes::Control control;
    std::uint64_t incarnation;
  };
  std::map<std::uint8_t, OriginalPhysicalPress> m_originalButtonControls;
  std::map<std::uint16_t, OriginalPhysicalPress> m_originalKeyControls;
  struct OriginalReservationCall
  {
    const PendingInput &pending;
    const deskflow::windowshare::OriginalInputCaptured *captured = nullptr;
    bool cancelled = false;
  };
  class OriginalReservationScope
  {
  public:
    OriginalReservationScope(Server &server, OriginalReservationCall &call) noexcept;
    ~OriginalReservationScope();
    OriginalReservationScope(const OriginalReservationScope &) = delete;
    OriginalReservationScope &operator=(const OriginalReservationScope &) = delete;

  private:
    Server &m_server;
    std::shared_ptr<bool> m_lifetime;
    OriginalReservationCall *m_previous;
  };
  OriginalReservationCall *m_originalReservationCall = nullptr;
  const PendingInput *m_originalExecuting = nullptr;
  const OriginalRouteObservation *m_originalExecutingObservation = nullptr;
  std::uint64_t m_originalLastPreparation = 0;
  bool m_originalDraining = false, m_originalDispatchTerminal = false, m_originalAbortSent = false;
  std::uint32_t m_originalEligibilityLogged = 0;
  OriginalRouteObservation *m_originalRouteObservation = nullptr;
  std::shared_ptr<deskflow::windowshare::OriginalInputRoutes> m_originalInputRoutes;
  InputRoutes m_inputRoutes;
  std::map<BaseClientProxy *, InputRoutes::Recipient> m_inputRecipients;
  std::map<InputRoutes::Recipient, BaseClientProxy *> m_inputClients;
  std::set<BaseClientProxy *> m_disconnectingInputClients;
  std::uint64_t m_serverIncarnation = 0;
  std::uint64_t m_selectionGeneration = 0;
  std::shared_ptr<std::atomic_bool> m_selectionRevoked;
  std::optional<deskflow::windowshare::SelectedPeer> m_selectedPeer;
  std::shared_ptr<deskflow::windowshare::WindowDragEdgeMailbox> m_windowDragEdgeMailbox;
  deskflow::windowshare::OriginalInputStamp m_windowDragPress;
  std::uint64_t m_windowDragPressIncarnation = 0;
  std::optional<deskflow::windowshare::WindowDragEdge> m_windowDragEdgePending;
  std::chrono::steady_clock::time_point m_windowDragEdgeEntered;
  bool m_selectionShutdown = false;
  bool m_selectionScreensaver = false;
  bool m_selectionChanging = false;
#endif

  // current configuration
  ServerConfig *m_config = nullptr;

  // input filter (from m_config);
  InputFilter *m_inputFilter = nullptr;

  // state saved when screen saver activates
  BaseClientProxy *m_activeSaver = nullptr;

  BaseClientProxy *m_switchComputer = nullptr;
  double m_switchWaitDelay = 0.0;
  EventQueueTimer *m_switchWaitTimer = nullptr;

  // delay for double-tap computer switching
  double m_switchTwoTapDelay = 0.0;

  // server computer
  deskflow::Computer *m_computer;

  IEventQueue *m_events = nullptr;
  size_t m_maximumClipboardSize = INT_MAX;
  ClientListener *m_clientListener = nullptr;
  Stopwatch m_switchTwoTapTimer;

  // Name of computer broadcasting the keyboard events
  std::string m_keyboardBroadcastingComputers;

  // all clients (including the primary client) indexed by name
  using ClientList = std::map<std::string, BaseClientProxy *>;
  using ClientSet = std::set<BaseClientProxy *>;
  ClientList m_clients;
  ClientSet m_clientSet;

  // all old connections that we're waiting to hangup
  using OldClients = std::map<BaseClientProxy *, EventQueueTimer *>;
  OldClients m_oldClients;

  // clipboard cache
  ClipboardInfo m_clipboards[kClipboardEnd];

  // used in hello message sent to the client
  NetworkProtocol m_protocol = NetworkProtocol::Barrier;

  // the sequence number of enter messages
  uint32_t m_seqNum = 0;

  // current mouse position (in absolute computer coordinates) on
  // whichever computer is active
  int32_t m_x;
  int32_t m_y;

  // last mouse deltas.  this is needed to smooth out double tap
  // on win32 which reports bogus mouse motion at the edge of
  // the computer when using low level hooks, synthesizing motion
  // in the opposite direction the mouse actually moved.
  int32_t m_xDelta = 0;
  int32_t m_yDelta = 0;
  int32_t m_xDelta2 = 0;
  int32_t m_yDelta2 = 0;

  int32_t m_xSaver;
  int32_t m_ySaver;

  // state for delayed computer switching
  int32_t m_switchWaitX;
  int32_t m_switchWaitY;

  int32_t m_switchTwoTapZone = 3;

  // common state for computer switch tests.  all tests are always
  // trying to reach the same computer in the same direction.
  Direction m_switchDir = Direction::NoDirection;

  bool m_switchTwoTapEngaged = false;
  bool m_switchTwoTapArmed = false;

  // relative mouse move option
  bool m_relativeMoves = false;

  // flag whether or not we have broadcasting enabled and the computers to
  // which we should send broadcasted keys.
  bool m_keyboardBroadcasting = false;

  // computer locking (former scroll lock)
  bool m_lockedToComputer = false;

  bool m_defaultLockToComputerState = false;
  bool m_disableLockToComputer = false;
  bool m_enableClipboard = true;
};
