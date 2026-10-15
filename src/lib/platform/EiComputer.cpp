/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2024, 2026 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2022, 2026 Red Hat, Inc.
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/EiComputer.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "common/Constants.h"
#include "common/ExitCodes.h"
#include "common/Settings.h"
#include "deskflow/App.h"
#include "deskflow/IComputer.h"
#include "deskflow/OptionTypes.h"
#include "platform/EiClipboard.h"
#include "platform/EiEventQueueBuffer.h"
#include "platform/EiKeyState.h"
#include "platform/PortalInputCapture.h"
#include "platform/PortalRemoteDesktop.h"
#ifdef BUILD_WINDOWSHARE
#include "windowshare/GnomeProviderClient.h"
#endif
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
#include "deskflow/InputSequence.h"
#endif

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <utility>
#include <vector>

// Values are in fractional wheel-click units (1.0 == one full 120-unit click) for trackpad
// and pixels in scroll wheel events.
struct ScrollRemainder
{
  double x;
  double y;
};

namespace deskflow {

#ifdef DESKFLOW_GNOME_EI_PING
struct EiComputer::GnomePreparation
{
  windowshare::InputPreparationBinding binding;
  windowshare::InputCutoffPoint point;
  windowshare::InputCutoffClock::time_point deadline, cleanupDeadline{};
  windowshare::InputPreparationState state = windowshare::InputPreparationState::Preparing;
  windowshare::InputPreparationOutcome outcome = windowshare::InputPreparationOutcome::None;
  std::uint64_t providerRequest = 0;
  bool prefixObserved = false;
  std::optional<windowshare::GnomeProviderClient::PreparedInputFocus> focus;
  std::optional<windowshare::InputCutoffBinding> promotion;
};
struct EiComputer::GnomeMappedCutoff
{
  windowshare::InputCutoffPoint point;
  windowshare::InputCutoffClock::time_point deadline;
  std::uint64_t providerRequest = 0;
  std::optional<windowshare::GnomeProviderClient::InputDrainSegment> segment;
  std::optional<windowshare::GnomeProviderClient::InputDrainSeal> seal;
  windowshare::InputCutoffBinding binding;
  bool retainPrefix = false, refreshing = false;
  std::vector<windowshare::InputCutoffBatchItem> batch;
  std::vector<windowshare::GnomeProviderClient::InputDrainSegment> batchSegments;
};
#endif

EiComputer::EiComputer(bool isPrimary, IEventQueue *events, bool usePortal)
#ifdef BUILD_WINDOWSHARE
    : EiComputer(isPrimary, events, usePortal, nullptr)
{
}

EiComputer::EiComputer(
    bool isPrimary, IEventQueue *events, bool usePortal, const windowshare::GnomeProviderConfig *configuration
)
#endif
    : PlatformComputer{events},
      m_isPrimary{isPrimary},
      m_events{events},
      m_w{1},
      m_h{1},
      m_isOnComputer{isPrimary}
{
#ifdef BUILD_WINDOWSHARE
  if (configuration && (isPrimary || !usePortal))
    throw std::runtime_error("GNOME provider requires an experimental Wayland client");
#endif
  initEi();
  m_keyState = new EiKeyState(this, events);
  // install event handlers
  m_events->addHandler(EventTypes::System, m_events->getSystemTarget(), [this](const auto &e) {
    handleSystemEvent(e);
  });

#ifdef BUILD_WINDOWSHARE
  if (configuration) {
    m_gnomeProviderEnabled = true;
    m_events->addHandler(EventTypes::EIGnomeProviderChanged, getEventTarget(), [this](const auto &) {
      handleGnomeProviderChanged();
    });
    try {
      m_clipboard = new EiClipboard(kClipboardClipboard);
      // Start from the queue's first timer dispatch. Its pre-loop pending
      // queue is not safe for worker publication during this constructor.
      m_gnomeProviderStartTimer = m_events->newOneShotTimer(0.001, nullptr);
      m_events->addHandler(EventTypes::Timer, m_gnomeProviderStartTimer, [this, config = *configuration](const auto &) {
        m_events->removeHandler(EventTypes::Timer, m_gnomeProviderStartTimer);
        m_events->deleteTimer(std::exchange(m_gnomeProviderStartTimer, nullptr));
        try {
          // The worker only wakes the consumer; it never borrows platform
          // state, passes raw descriptors in events, or changes libei.
          auto wake = [events = m_events, target = getEventTarget()] {
            events->addEvent(Event(EventTypes::EIGnomeProviderChanged, target));
          };
          m_gnomeProvider = std::make_unique<windowshare::GnomeProviderClient>(config, std::move(wake));
        } catch (const std::exception &error) {
          stopGnomeProvider(error.what());
        }
      });
    } catch (...) {
      if (m_gnomeProviderStartTimer) {
        m_events->removeHandler(EventTypes::Timer, m_gnomeProviderStartTimer);
        m_events->deleteTimer(std::exchange(m_gnomeProviderStartTimer, nullptr));
      }
      m_events->removeHandler(EventTypes::EIGnomeProviderChanged, getEventTarget());
      m_events->removeHandler(EventTypes::System, m_events->getSystemTarget());
      m_events->adoptBuffer(nullptr);
      cleanupEi();
      delete m_keyState;
      delete m_clipboard;
      throw;
    }
  } else
#endif
      if (usePortal) {
    m_events->addHandler(EventTypes::EIConnected, getEventTarget(), [this](const auto &e) {
      handleConnectedToEisEvent(e);
    });
    if (isPrimary) {
      // Portal input capture manages its own clipboard
      m_portalInputCapture = new PortalInputCapture(this, m_events);
#ifdef HAVE_LIBPORTAL_SHORTCUTS
      m_portalGlobalShortcuts = new PortalGlobalShortcuts(this, m_events);
#endif
    } else {
      m_events->addHandler(EventTypes::EISessionClosed, getEventTarget(), [this](const auto &) {
        handlePortalSessionClosed();
      });
      m_portalRemoteDesktop = new PortalRemoteDesktop(this, m_events);
      // Create clipboard for remote desktop (secondary computer)
      m_clipboard = new EiClipboard(kClipboardClipboard);
    }
  } else {
    // Note: socket backend does not support reconnections
    if (auto rc = ei_setup_backend_socket(m_ei, nullptr); rc != 0) {
      LOG_ERR("ei init error: %s", strerror(-rc));
      throw std::runtime_error("failed to init ei context");
    }
    // Create clipboard for socket backend
    m_clipboard = new EiClipboard(kClipboardClipboard);
  }

  // disable sleep if the flag is set
  if (Settings::value(Settings::Core::PreventSleep).toBool()) {
    m_powerManager.disableSleep();
  }
}

EiComputer::~EiComputer()
{
#ifdef BUILD_WINDOWSHARE
  if (m_gnomeProviderStartTimer) {
    m_events->removeHandler(EventTypes::Timer, m_gnomeProviderStartTimer);
    m_events->deleteTimer(std::exchange(m_gnomeProviderStartTimer, nullptr));
  }
  // Join the sole producer before removing its target or destroying libei.
  m_gnomeProvider.reset();
  m_events->removeHandler(EventTypes::EIGnomeProviderChanged, getEventTarget());
#endif
  m_events->adoptBuffer(nullptr);
  m_events->removeHandler(EventTypes::System, m_events->getSystemTarget());

  cancelIdleEmulationTimer();

  cleanupEi();

  delete m_keyState;
  delete m_clipboard;

#ifdef HAVE_LIBPORTAL_SHORTCUTS
  delete m_portalGlobalShortcuts;
#endif
  delete m_portalInputCapture;
  delete m_portalRemoteDesktop;
}

void EiComputer::eiLogEvent(ei_log_priority priority, const char *message) const
{
  switch (priority) {
  case EI_LOG_PRIORITY_DEBUG:
    LOG_VERBOSE("ei: %s", message);
    break;
  case EI_LOG_PRIORITY_INFO:
    LOG_INFO("ei: %s", message);
    break;
  case EI_LOG_PRIORITY_WARNING:
    LOG_WARN("ei: %s", message);
    break;
  case EI_LOG_PRIORITY_ERROR:
    LOG_ERR("ei: %s", message);
    break;
  default:
    LOG_PRINT("ei: %s", message);
    break;
  }
}

void EiComputer::initEi()
{
  if (m_isPrimary) {
    m_ei = ei_new_receiver(nullptr); // we receive from the display server
  } else {
    m_ei = ei_new_sender(nullptr); // we send to the display server
  }
  ei_set_user_data(m_ei, this);
  ei_log_set_priority(m_ei, EI_LOG_PRIORITY_DEBUG);
  ei_log_set_handler(m_ei, handleEiLogEvent);
  std::string configName = kAppId;
  ei_configure_name(m_ei, configName.append(" client").c_str());

  // install the platform event queue
  m_events->adoptBuffer(nullptr);
  m_events->adoptBuffer(new EiEventQueueBuffer(m_ei, m_events));
}

void EiComputer::cleanupEi()
{
#ifdef DESKFLOW_GNOME_EI_PING
  cancelGnomeEiPrefixObservation();
  // A new EI context has a different native lifetime. Retained proof from
  // a prior connection must not become eligible on a new context incarnation.
  m_gnomePrefixProbe.resetAfterConnectionTeardown();
  m_gnomePrefixRequest = 0;
  if (m_gnomePreparation && m_gnomePreparation->state != windowshare::InputPreparationState::Released) {
    m_gnomePreparation->state = windowshare::InputPreparationState::Failed;
    m_gnomePreparation->outcome = windowshare::InputPreparationOutcome::Failed;
  }
  m_gnomePrefixBinding.reset();
  m_gnomeMappedCutoff.reset();
  m_gnomeSubmittedKeys.reset();
  m_gnomeSubmittedButtons.reset();
  m_gnomeUntrackedInput = false;
#endif
  for (auto device : m_eiDevices) {
    delete static_cast<ScrollRemainder *>(ei_device_get_user_data(device));
    ei_device_set_user_data(device, nullptr);
  }

  if (m_eiPointer) {
    m_eiPointer = ei_device_unref(m_eiPointer);
  }
  if (m_eiKeyboard) {
    m_eiKeyboard = ei_device_unref(m_eiKeyboard);
  }
  if (m_eiAbs) {
    m_eiAbs = ei_device_unref(m_eiAbs);
  }
  m_eiSeat = ei_seat_unref(m_eiSeat);
  for (auto device : m_eiDevices) {
    ei_device_unref(device);
  }
  m_eiDevices.clear();
  m_ei = ei_unref(m_ei);
}

void *EiComputer::getEventTarget() const
{
  return const_cast<void *>(static_cast<const void *>(this));
}

bool EiComputer::getClipboard(ClipboardID id, IClipboard *clipboard) const
{
  // If using portal input capture, get clipboard from there
  if (m_portalInputCapture) {
    const auto sourceClipboard = m_portalInputCapture->getClipboard(id);
    if (!sourceClipboard) {
      return false;
    }
    return IClipboard::copy(clipboard, sourceClipboard);
  }

  // Otherwise use our own clipboard
  if (!m_clipboard) {
    return false;
  }

  return IClipboard::copy(clipboard, m_clipboard);
}

void EiComputer::getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const
{
  x = m_x;
  y = m_y;
  w = m_w;
  h = m_h;
}

void EiComputer::getCursorPos(int32_t &x, int32_t &y) const
{
  x = m_cursorX;
  y = m_cursorY;
}

void EiComputer::reconfigure(uint32_t activeSides)
{
  const static auto sidesText = sidesMaskToString(activeSides);
  LOG_DEBUG("active sides: %s (0x%02x)", sidesText.c_str(), activeSides);
  m_activeSides = activeSides;
}

std::uint32_t EiComputer::activeSides()
{
  return m_activeSides;
}

void EiComputer::warpCursor(int32_t x, int32_t y)
{
  m_cursorX = x;
  m_cursorY = y;
}

std::uint32_t EiComputer::registerHotKey(KeyID key, KeyModifierMask mask)
{
  static std::uint32_t next_id;
  std::uint32_t id = std::max(++next_id, 1u);

  // Bug: id rollover means duplicate hotkey ids. Oh well.

  auto set = m_hotkeys.find(key);
  if (set == m_hotkeys.end()) {
    m_hotkeys.try_emplace(key, HotKeySet{key});
    set = m_hotkeys.find(key);
  }
  set->second.addItem(HotKeyItem(mask, id));

  updatePortalGlobalShortcuts();

  return id;
}

void EiComputer::unregisterHotKey(uint32_t id)
{
  for (auto it = m_hotkeys.begin(); it != m_hotkeys.end(); ++it) {
    if (it->second.removeById(id)) {
      if (it->second.empty())
        m_hotkeys.erase(it);
      updatePortalGlobalShortcuts();
      break;
    }
  }
}

void EiComputer::updatePortalGlobalShortcuts()
{
#ifdef HAVE_LIBPORTAL_SHORTCUTS
  if (!m_portalGlobalShortcuts) {
    return;
  }

  if (!m_activated) {
    return;
  }

  std::vector<PortalGlobalShortcuts::HotKey> hotkeys;

  for (const auto &[key, set] : m_hotkeys) {
    (void)key;

    const auto setHotKeys = set.getPortalHotKeys();
    hotkeys.insert(hotkeys.end(), setHotKeys.begin(), setHotKeys.end());
  }

  m_portalGlobalShortcuts->setHotKeys(std::move(hotkeys));
#endif
}

void EiComputer::fakeInputBegin()
{
  // FIXME -- not implemented
}

void EiComputer::fakeInputEnd()
{
  // FIXME -- not implemented
}

std::int32_t EiComputer::getJumpZoneSize() const
{
  return 1;
}

bool EiComputer::isAnyMouseButtonDown(uint32_t &buttonID) const
{
  if (m_buttons.none())
    return false;

  buttonID = std::countr_zero(m_buttons.to_ulong());
  return true;
}

void EiComputer::getCursorCenter(int32_t &x, int32_t &y) const
{
  x = m_x + m_w / 2;
  y = m_y + m_h / 2;
}

void EiComputer::fakeMouseButton(ButtonID button, bool press)
{
  if (!allowLegacyInput())
    return;
  uint32_t code;

  if (!m_eiPointer)
    return;

  switch (button) {
  case kButtonLeft:
    code = 0x110; // BTN_LEFT
    break;
  case kButtonMiddle:
    code = 0x112; // BTN_MIDDLE
    break;
  case kButtonRight:
    code = 0x111; // BTN_RIGHT
    break;
  default:
    code = 0x110 + (button - 1);
    break;
  }

  ensureEmulating();
  ei_device_button_button(m_eiPointer, code, press);
  ei_device_frame(m_eiPointer, ei_now(m_ei));
#ifdef DESKFLOW_GNOME_EI_PING
  m_gnomeSubmittedButtons.set(button, press);
#endif
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  windowshare::InputSequenceSubmissionScope::submitted();
#endif
}

void EiComputer::fakeMouseMove(int32_t x, int32_t y)
{
  if (!allowLegacyInput())
    return;
  // We get one motion event before enter() with the target position
  if (!m_isOnComputer) {
    m_cursorX = x;
    m_cursorY = y;
    return;
  }

  if (!m_eiAbs)
    return;

  ensureEmulating();
  ei_device_pointer_motion_absolute(m_eiAbs, x, y);
  ei_device_frame(m_eiAbs, ei_now(m_ei));
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  windowshare::InputSequenceSubmissionScope::submitted();
#endif
#ifdef BUILD_WINDOWSHARE
  // Observe the production viewer through the same owned EIS session. This
  // asynchronous request is not an EIS fence or attribution of this motion;
  // it leaves ordinary injection and original-input delivery unchanged.
  if (m_gnomeProvider)
    m_gnomeProvider->requestViewportObservation();
#endif
}

void EiComputer::fakeMouseRelativeMove(int32_t dx, int32_t dy) const
{
  if (!allowLegacyInput())
    return;
  if (!m_eiPointer)
    return;

  ensureEmulating();
  ei_device_pointer_motion(m_eiPointer, dx, dy);
  ei_device_frame(m_eiPointer, ei_now(m_ei));
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  windowshare::InputSequenceSubmissionScope::submitted();
#endif
}

void EiComputer::fakeMouseWheel(ScrollDelta delta) const
{
  if (!allowLegacyInput())
    return;
  if (!m_eiPointer)
    return;

  delta = applyScrollModifier(delta);
  // libei and deskflow seem to use opposite directions, so we have
  // to send EI the opposite of the value received if we want to remain
  // compatible with other platforms (including X11).
  ensureEmulating();
  ei_device_scroll_discrete(m_eiPointer, -delta.x, -delta.y);
  ei_device_frame(m_eiPointer, ei_now(m_ei));
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  windowshare::InputSequenceSubmissionScope::submitted();
#endif
}

void EiComputer::fakeKey(uint32_t keycode, bool isDown) const
{
  if (!allowLegacyInput())
    return;
  if (!m_eiKeyboard)
    return;

  auto xkbKeycode = keycode + 8;
  m_keyState->updateXkbState(xkbKeycode, isDown);
  ensureEmulating();
  ei_device_keyboard_key(m_eiKeyboard, keycode, isDown);
  ei_device_frame(m_eiKeyboard, ei_now(m_ei));
#ifdef DESKFLOW_GNOME_EI_PING
  if (keycode < m_gnomeSubmittedKeys.size())
    m_gnomeSubmittedKeys.set(keycode, isDown);
  else
    m_gnomeUntrackedInput = true;
#endif
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  windowshare::InputSequenceSubmissionScope::submitted();
#endif
}

void EiComputer::enable()
{
  m_activated = true;
  updatePortalGlobalShortcuts();
}

void EiComputer::disable()
{
  m_activated = false;
}

void EiComputer::cancelIdleEmulationTimer() const
{
  if (m_idleEmulationTimer) {
    m_events->removeHandler(EventTypes::Timer, m_idleEmulationTimer);
    m_events->deleteTimer(m_idleEmulationTimer);
    m_idleEmulationTimer = nullptr;
  }
}

bool EiComputer::inputSessionLive() const noexcept
{
#ifdef BUILD_WINDOWSHARE
  return !m_gnomeProviderEnabled || (!m_gnomeProviderStopped && m_gnomeProvider && m_gnomeProvider->live());
#else
  return true;
#endif
}

bool EiComputer::allowLegacyInput() const noexcept
{
#ifdef DESKFLOW_GNOME_EI_PING
  if (m_gnomePrefixBinding ||
      (m_gnomePreparation && m_gnomePreparation->state != windowshare::InputPreparationState::Released)) {
    // Transport normally rejects this before key translation or compression.
    // A direct backend caller still cannot submit across the retained cutoff.
    invalidateGnomeInputCutoff();
    windowshare::InputSequenceSubmissionScope::fail();
    return false;
  }
#endif
  return inputSessionLive();
}

void EiComputer::ensureEmulating() const
{
  if (!inputSessionLive() || m_isPrimary || !m_isOnComputer)
    return;

  if (!m_isEmulating) {
    ++m_sequenceNumber;
    if (m_eiPointer)
      ei_device_start_emulating(m_eiPointer, m_sequenceNumber);
    if (m_eiKeyboard)
      ei_device_start_emulating(m_eiKeyboard, m_sequenceNumber);
    if (m_eiAbs)
      ei_device_start_emulating(m_eiAbs, m_sequenceNumber);
    m_isEmulating = true;
  }

  cancelIdleEmulationTimer();
  if (Settings::value(Settings::Core::PreventSleep).toBool())
    return;

  m_idleEmulationTimer = m_events->newOneShotTimer(s_idleEmulationTimeout, nullptr);
  m_events->addHandler(EventTypes::Timer, m_idleEmulationTimer, [this](const auto &) {
#ifdef DESKFLOW_GNOME_EI_PING
    if (m_gnomeProviderEnabled && m_isEmulating)
      LOG_DEBUG("GNOME EI idle stop generation=%u", m_sequenceNumber);
#endif
    stopEmulating();
  });
}

void EiComputer::stopEmulating() const
{
#ifdef DESKFLOW_GNOME_EI_PING
  invalidateGnomeInputCutoff();
#endif
  cancelIdleEmulationTimer();
  if (!m_isEmulating)
    return;
  if (m_eiPointer)
    ei_device_stop_emulating(m_eiPointer);
  if (m_eiKeyboard)
    ei_device_stop_emulating(m_eiKeyboard);
  if (m_eiAbs)
    ei_device_stop_emulating(m_eiAbs);
  m_isEmulating = false;
}

void EiComputer::enter()
{
  m_isOnComputer = true;
  if (!m_isPrimary && m_eiAbs) {
    // Emulation is started lazily by ensureEmulating() on the first injected
    // input and released again after a short idle, so this computer can DPMS-sleep
    // while the cursor sits here with no relayed activity.
    fakeMouseMove(m_cursorX, m_cursorY);
  } else if (m_isPrimary) {
    LOG_DEBUG("releasing input capture at x=%i y=%i", m_cursorX, m_cursorY);
    m_portalInputCapture->release(m_cursorX, m_cursorY);
    // no more button events once capture is released, so drop any held state
    updateButtons();
  }
}

bool EiComputer::canLeave()
{
  return true;
}

void EiComputer::leave()
{
  if (!m_isPrimary) {
    stopEmulating();
  }

  m_isOnComputer = false;
}

bool EiComputer::setClipboard(ClipboardID id, const IClipboard *clipboard)
{
  if (!clipboard) {
    return false;
  }

  // If using portal input capture, set clipboard there
  if (m_portalInputCapture) {
    IClipboard *targetClipboard = m_portalInputCapture->getClipboard(id);
    if (!targetClipboard) {
      return false;
    }
    return IClipboard::copy(targetClipboard, clipboard);
  }

  // Otherwise use our own clipboard
  if (!m_clipboard) {
    return false;
  }

  bool ok = IClipboard::copy(m_clipboard, clipboard);

  if (ok && m_portalRemoteDesktop && id == kClipboardClipboard) {
    m_portalRemoteDesktop->claimClipboard();
  }

  return ok;
}

void EiComputer::checkClipboards()
{
  // For portal-based input capture, clipboard changes come via portal events
  // For socket-based, clipboard is passive and changes are sent explicitly
  // Nothing to do here
}

void EiComputer::openScreensaver(bool notify)
{
  // FIXME
}

void EiComputer::closeScreensaver()
{
  // FIXME
}

void EiComputer::screensaver(bool activate)
{
  // FIXME
}

void EiComputer::resetOptions()
{
  // Should reset options to neutral, see setOptions().
  // We don't have ei-specific options, nothing to do here
}

void EiComputer::setOptions(const OptionsList &options)
{
  for (auto it = options.begin(); it != options.end(); ++it) {
    if (*it == kOptionClipboardSharingSize) {
      ++it;
      if (it == options.end())
        break;
      m_maximumClipboardSize = *it;
      LOG_DEBUG("computer received clipboard size limit: %zu KB", m_maximumClipboardSize);
    }
  }
}

bool EiComputer::isPrimary() const
{
  return m_isPrimary;
}

void EiComputer::updateShape()
{
  std::uint32_t newW = 1;
  std::uint32_t newH = 1;
  std::uint32_t newX = std::numeric_limits<uint32_t>::max();
  std::uint32_t newY = std::numeric_limits<uint32_t>::max();
  bool foundRegion = false;
  for (auto it = m_eiDevices.begin(); it != m_eiDevices.end(); it++) {
    auto idx = 0;
    struct ei_region *r;
    while ((r = ei_device_get_region(*it, idx++)) != nullptr) {
      foundRegion = true;
      newX = std::min(ei_region_get_x(r), newX);
      newY = std::min(ei_region_get_y(r), newY);
      newW = std::max(ei_region_get_x(r) + ei_region_get_width(r), newW);
      newH = std::max(ei_region_get_y(r) + ei_region_get_height(r), newH);
    }
  }

  if (!foundRegion) {
    LOG_DEBUG("logical output size: unchanged (no region-reporting device present)");
    return;
  }

  LOG_DEBUG("logical output size: %dx%d@%d.%d", newW, newH, newX, newY);

  const bool changed = newX != m_x || newY != m_y || newW != m_w || newH != m_h;

  if (!m_isShapeInitialized) {
    m_cursorX = newX + newW / 2;
    m_cursorY = newY + newH / 2;
    m_isShapeInitialized = true;
  } else if (changed) {
    m_cursorX = std::clamp(m_cursorX, static_cast<int32_t>(newX), static_cast<int32_t>(newX + newW - 1));
    m_cursorY = std::clamp(m_cursorY, static_cast<int32_t>(newY), static_cast<int32_t>(newY + newH - 1));
  }

  m_x = newX;
  m_y = newY;
  m_w = newW;
  m_h = newH;

  if (changed) {
    sendEvent(EventTypes::ComputerShapeChanged, nullptr);
  }
}

void EiComputer::addDevice(struct ei_device *device)
{
  LOG_DEBUG("adding device %s", ei_device_get_name(device));

  // Noteworthy: EI in principle supports multiple devices with multiple
  // capabilities, so there may be more than one logical pointer (or even
  // multiple seats). Supporting this is ... tricky so for now we go the easy
  // route: one device for each capability. Note this may be the same device
  // if the first device comes with multiple capabilities.

  if (!m_eiPointer && ei_device_has_capability(device, EI_DEVICE_CAP_POINTER) &&
      ei_device_has_capability(device, EI_DEVICE_CAP_BUTTON) &&
      ei_device_has_capability(device, EI_DEVICE_CAP_SCROLL)) {
    m_eiPointer = ei_device_ref(device);
  }

  if (!m_eiKeyboard && ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD)) {
    m_eiKeyboard = ei_device_ref(device);

    if (auto keymap = ei_device_keyboard_get_keymap(device);
        keymap && ei_keymap_get_type(keymap) == EI_KEYMAP_TYPE_XKB) {
      int fd = ei_keymap_get_fd(keymap);
      size_t len = ei_keymap_get_size(keymap);
      m_keyState->init(fd, len);
    } else {
      // We rely on the EIS implementation to give us a keymap, otherwise we
      // really have no idea what a keycode means (other than it's linux/input.h
      // code) Where the EIS implementation does not tell us, we just default to
      // whatever libxkbcommon thinks is default. At least this way we can
      // influence with env vars what we get
      LOG_WARN("keyboard device %s does not have a keymap, we are guessing", ei_device_get_name(device));
      m_keyState->initDefaultKeymap();
    }
    m_keyState->updateKeyMap();
  }

  if (!m_eiAbs && ei_device_has_capability(device, EI_DEVICE_CAP_POINTER_ABSOLUTE) &&
      ei_device_has_capability(device, EI_DEVICE_CAP_BUTTON) &&
      ei_device_has_capability(device, EI_DEVICE_CAP_SCROLL)) {
    m_eiAbs = ei_device_ref(device);
  }

  m_eiDevices.emplace_back(ei_device_ref(device));

  updateShape();
}

void EiComputer::removeDevice(struct ei_device *device)
{
  LOG_DEBUG("removing device %s", ei_device_get_name(device));

  bool wasTracked = false;
  if (device == m_eiPointer) {
#ifdef DESKFLOW_GNOME_EI_PING
    m_gnomeSubmittedButtons.reset();
#endif
    m_eiPointer = ei_device_unref(m_eiPointer);
    wasTracked = true;
  }
  if (device == m_eiKeyboard) {
#ifdef DESKFLOW_GNOME_EI_PING
    m_gnomeSubmittedKeys.reset();
#endif
    m_eiKeyboard = ei_device_unref(m_eiKeyboard);
    wasTracked = true;
  }
  if (device == m_eiAbs) {
    m_eiAbs = ei_device_unref(m_eiAbs);
    wasTracked = true;
  }

  if (wasTracked) {
    m_isEmulating = false;
    cancelIdleEmulationTimer();
    // same as for a paused device: no release events follow a removal
    updateButtons();
  }

  delete static_cast<ScrollRemainder *>(ei_device_get_user_data(device));
  ei_device_set_user_data(device, nullptr);

  for (auto it = m_eiDevices.begin(); it != m_eiDevices.end();) {
    if (*it == device) {
      it = m_eiDevices.erase(it);
      ei_device_unref(device);
    } else {
      ++it;
    }
  }

  updateShape();
}

void EiComputer::sendEvent(EventTypes type, void *data)
{
  m_events->addEvent(Event(type, getEventTarget(), data));
}

void EiComputer::sendClipboardEvent(EventTypes type, ClipboardID id) const
{
  auto *info = static_cast<ClipboardInfo *>(malloc(sizeof(ClipboardInfo)));
  if (info == nullptr) {
    LOG_ERR("malloc failed for ClipboardInfo");
    return;
  }
  info->m_id = id;
  info->m_sequenceNumber = m_sequenceNumber;

  // Use const_cast to call non-const sendEvent from const method
  const_cast<EiComputer *>(this)->sendEvent(type, info);
}

void EiComputer::setSequenceNumber(uint32_t seqNum)
{
  m_sequenceNumber = seqNum;
}

ButtonID EiComputer::mapButtonFromEvdev(ei_event *event) const
{
  switch (ei_event_button_get_button(event)) {
  case 0x110:
    return kButtonLeft;
  case 0x111:
    return kButtonRight;
  case 0x112:
    return kButtonMiddle;
  case 0x113:
    return kButtonExtra0;
  case 0x114:
    return kButtonExtra1;
  default:
    return kButtonNone;
  }

  return kButtonNone;
}

bool EiComputer::onHotkey(KeyID keyid, bool isPressed, KeyModifierMask mask)
{
  // Check if the keyid is registered as a hotkey
  // This is the implementation if InputCapture is active. If InputCapture is not active,
  // the hotkey is handled by the portal and we don't get here.

  auto it = m_hotkeys.find(keyid);

  if (it == m_hotkeys.end()) {
    return false;
  }

  // Note: our mask (see on_key_event) only contains some modifiers
  // but we don't put a limitation on modifiers in the hotkeys. So some
  // key combinations may not work correctly, more effort is needed here.
  if (auto id = it->second.findByMask(mask); id != 0) {
    EventTypes type = isPressed ? EventTypes::PrimaryComputerHotkeyDown : EventTypes::PrimaryComputerHotkeyUp;
    sendEvent(type, HotKeyInfo::alloc(id));
    return true;
  }
  return false;
}

void EiComputer::onKeyEvent(ei_event *event)
{
  auto keycode = ei_event_keyboard_get_key(event);
  uint32_t keyval = keycode + 8;
  bool pressed = ei_event_keyboard_get_key_is_press(event);
  KeyID keyid = m_keyState->mapKeyFromKeyval(keyval);
  auto keybutton = static_cast<KeyButton>(keyval);
  bool repeat;

  m_keyState->updateXkbState(keyval, pressed);
  KeyModifierMask mask = m_keyState->pollActiveModifiers();

  repeat = pressed && m_lastPressed == keyid && keyid != kKeyNone;

  m_lastPressed = pressed ? keyid : kKeyNone;

  LOG_VERBOSE(
      "event: key %s%s keycode=%d keyid=%d mask=0x%x", pressed ? "press" : "release", repeat ? " (repeat)" : "",
      keycode, keyid, mask
  );

  if (m_isPrimary && onHotkey(keyid, pressed, mask)) {
    return;
  }

  if (keyid != kKeyNone) {
    m_keyState->sendKeyEvent(getEventTarget(), pressed, repeat, keyid, mask, 1, keybutton);
  }
}

void EiComputer::onButtonEvent(ei_event *event)
{
  assert(m_isPrimary);

  auto buttonID = mapButtonFromEvdev(event);
  bool pressed = ei_event_button_get_is_press(event);
  KeyModifierMask mask = m_keyState->pollActiveModifiers();

  LOG_VERBOSE("event: button %s button=%d mask=0x%x", pressed ? "press" : "release", buttonID, mask);

  if (buttonID == kButtonNone) {
    LOG_DEBUG("event: button not recognized");
    return;
  }

  m_buttons.set(buttonID, pressed);

  auto eventType = pressed ? EventTypes::PrimaryComputerButtonDown : EventTypes::PrimaryComputerButtonUp;

  sendEvent(eventType, ButtonInfo::alloc(buttonID, mask));
}

void EiComputer::onPointerScrollEvent(ei_event *event)
{
  // Smooth scroll deltas are in pixels. We accumulate them as fractional
  // wheel-click units and only send full wheel clicks (120 units each)
  // to the client. Sub-120 fractional clicks are silently ignored by
  // compositors on the receiving end, so accumulating full clicks avoids
  // flooding the network with events that the client drops anyway.
  static const double s_wheelClicksPerPixel = 0.1; // 10 pixels == 1 full wheel click

  assert(m_isPrimary);

  auto dx = ei_event_scroll_get_dx(event);
  auto dy = ei_event_scroll_get_dy(event);
  struct ei_device *device = ei_event_get_device(event);

  LOG_VERBOSE("event: scroll (%.2f, %.2f)", dx, dy);

  auto *remainder = static_cast<struct ScrollRemainder *>(ei_device_get_user_data(device));
  if (!remainder) {
    remainder = new ScrollRemainder();
    ei_device_set_user_data(device, remainder);
  }

  // Accumulate smooth scroll as fractional wheel clicks (1.0 == 120 units)
  double accX = remainder->x + dx * s_wheelClicksPerPixel;
  double accY = remainder->y + dy * s_wheelClicksPerPixel;

  // Only dispatch full wheel clicks. Use trunc (toward zero) not floor,
  // because floor(-0.3) == -1 which would fire a spurious click.
  double fullClicksX = std::trunc(accX);
  double fullClicksY = std::trunc(accY);

  // libei and deskflow seem to use opposite directions, so we have
  // to send the opposite of the value reported by EI if we want to
  // remain compatible with other platforms (including X11).
  if (fullClicksX != 0 || fullClicksY != 0) {
    sendEvent(
        EventTypes::PrimaryComputerWheel,
        WheelInfo::alloc(
            static_cast<int32_t>(-fullClicksX) * s_scrollDelta, static_cast<int32_t>(-fullClicksY) * s_scrollDelta
        )
    );
    accX -= fullClicksX;
    accY -= fullClicksY;
  }

  remainder->x = accX;
  remainder->y = accY;
}

void EiComputer::onPointerScrollDiscreteEvent(ei_event *event)
{
  // both libei and deskflow use multiples of 120 to represent
  // one scroll wheel click event

  assert(m_isPrimary);

  auto dx = ei_event_scroll_get_discrete_dx(event);
  auto dy = ei_event_scroll_get_discrete_dy(event);

  LOG_VERBOSE("event: scroll discrete (%d, %d)", dx, dy);

  // accumulate fractional ticks, then emit 120 units at a time
  struct ei_device *device = ei_event_get_device(event);
  auto *remainder = static_cast<struct ScrollRemainder *>(ei_device_get_user_data(device));
  if (!remainder) {
    remainder = new ScrollRemainder();
    ei_device_set_user_data(device, remainder);
  }

  double ax = remainder->x + dx;
  double ay = remainder->y + dy;
  auto cx = static_cast<int32_t>(ax / s_scrollDelta) * s_scrollDelta;
  auto cy = static_cast<int32_t>(ay / s_scrollDelta) * s_scrollDelta;
  remainder->x = ax - cx;
  remainder->y = ay - cy;

  // libei and deskflow seem to use opposite directions, so we have
  // to send the opposite of the value reported by EI if we want to
  // remain compatible with other platforms (including X11).
  if (cx != 0 || cy != 0)
    sendEvent(EventTypes::PrimaryComputerWheel, WheelInfo::alloc(-cx, -cy));
}

void EiComputer::onMotionEvent(ei_event *event)
{
  assert(m_isPrimary);

  auto dx = ei_event_pointer_get_dx(event);
  auto dy = ei_event_pointer_get_dy(event);
  if (!std::isfinite(dx) || !std::isfinite(dy)) {
    LOG_WARN("dropping pointer motion with non-finite delta: %g,%g", dx, dy);
    return;
  }

  if (m_isOnComputer) {
    LOG_DEBUG("event: motion on primary x=%i y=%i", m_cursorX, m_cursorY);
    sendEvent(EventTypes::PrimaryComputerMotionOnPrimary, MotionInfo::alloc(m_cursorX, m_cursorY));
    if (m_portalInputCapture->isActive()) {
      m_portalInputCapture->release();
    }
  } else {
    m_bufferDX += dx;
    m_bufferDY += dy;
    auto pixelDx = static_cast<std::int32_t>(m_bufferDX);
    auto pixelDy = static_cast<std::int32_t>(m_bufferDY);
    if (pixelDx || pixelDy) {
      LOG_VERBOSE("event: motion on secondary x=%d y=%d", pixelDx, pixelDy);
      sendEvent(EventTypes::PrimaryComputerMotionOnSecondary, MotionInfo::alloc(pixelDx, pixelDy));
      m_bufferDX -= pixelDx;
      m_bufferDY -= pixelDy;
    }
  }
}

void EiComputer::onAbsMotionEvent(const ei_event *) const
{
  assert(m_isPrimary);
}

void EiComputer::handleConnectedToEisEvent(const Event &event)
{
  int fd = static_cast<EiConnectInfo *>(event.getData())->m_fd;
  LOG_DEBUG("eis connection established, fd=%d", fd);

  auto rc = ei_setup_backend_fd(m_ei, fd);
  if (rc != 0) {
    LOG_WARN("failed to set up ei: %s", strerror(-rc));
  }
}

void EiComputer::handlePortalSessionClosed()
{
  // Portal may or may not EI_EVENT_DISCONNECT us before sending the DBus Closed
  // signal. Let's clean up either way.
  LOG_DEBUG("eis handling portal session closed");
  cleanupEi();
  initEi();
}

#ifdef BUILD_WINDOWSHARE
void EiComputer::handleGnomeProviderChanged()
{
  const std::scoped_lock lock(m_mutex);
  if (!m_gnomeProvider)
    return;
  const auto update = m_gnomeProvider->takeUpdate();
  if (update.closed || !update.ready || !m_gnomeProvider->live() || m_gnomeProviderStopped) {
    if (update.descriptor >= 0)
      ::close(update.descriptor);
    stopGnomeProvider(update.error.empty() ? "GNOME input session ended" : update.error.c_str());
    return;
  }
  if (update.descriptor >= 0) {
    // libei takes ownership even on failure and closes at context teardown.
    const auto result = ei_setup_backend_fd(m_ei, update.descriptor);
    if (result != 0) {
      LOG_ERR("failed to adopt GNOME EIS session: %s", strerror(-result));
      stopGnomeProvider("Cannot adopt GNOME EIS session");
      return;
    }
    LOG_INFO("connected experimental GNOME input session and owned Wayland viewer");
  }
  const auto viewport = m_gnomeProvider->takeViewportUpdate();
  if (viewport && viewport->observation && viewport->observation->live() && !m_gnomeViewportReported) {
    const auto &state = viewport->observation->snapshot();
    LOG_DEBUG(
        "GNOME viewer painted viewport observed: surface %.3f,%.3f -> source %.3f,%.3f (observation only)",
        state.surfaceX, state.surfaceY, state.sourceX, state.sourceY
    );
    m_gnomeViewportReported = true;
  }
}

void EiComputer::stopGnomeProvider(const char *reason)
{
#ifdef DESKFLOW_GNOME_EI_PING
  cancelGnomeEiPrefixObservation();
#endif
  if (m_gnomeProviderStopped)
    return;
  m_gnomeProviderStopped = true;
  if (m_gnomeProvider)
    m_gnomeProvider->stop();
  stopEmulating();
  LOG_ERR("experimental GNOME session stopped: %s", reason);
  // A replacement portal/session would silently replace the retained native
  // identity. Exit this explicit client and let normal teardown release libei.
  m_events->addEvent(Event(EventTypes::Quit, nullptr, new ExitEventData(s_exitFailed)));
}
#endif

#ifdef DESKFLOW_GNOME_EI_PING
void EiComputer::retireGnomeEiPing()
{
  if (m_gnomePrefixTimer) {
    m_events->removeHandler(EventTypes::Timer, m_gnomePrefixTimer);
    m_events->deleteTimer(std::exchange(m_gnomePrefixTimer, nullptr));
  }
  if (m_gnomePrefixPing)
    m_gnomePrefixPing = ei_ping_unref(m_gnomePrefixPing);
}

void EiComputer::cancelGnomeEiPrefixObservation()
{
  invalidateGnomeInputCutoff();
  if (m_gnomeMappedCutoff) {
    if (m_gnomeProvider && m_gnomeMappedCutoff->providerRequest)
      m_gnomeProvider->cancelInputDrain(m_gnomeMappedCutoff->providerRequest);
    m_gnomeMappedCutoff->segment.reset();
    m_gnomeMappedCutoff->seal.reset();
    m_gnomeMappedCutoff->batchSegments.clear();
  }
  retireGnomeEiPing();
}

void EiComputer::invalidateGnomeInputCutoff() const noexcept
{
  m_gnomePrefixProbe.invalidate();
  m_gnomeCutoffInvalidated = true;
}

bool EiComputer::beginGnomeEiPrefixObservation(
    const windowshare::InputCutoffBinding &binding, windowshare::InputCutoffClock::time_point deadline
)
{
  if (!windowshare::validInputCutoffBinding(binding) || !beginGnomeEiPrefixObservation(binding.request, deadline))
    return false;
  m_gnomePrefixBinding = binding;
  return true;
}

bool EiComputer::beginGnomeEiPrefixObservation(
    std::uint64_t request, windowshare::InputCutoffClock::time_point deadline
)
{
  // Only the explicitly owned GNOME provider's actual libei sender. No
  // borrowed device or dispatch pointer escapes the core event queue.
  if (m_gnomePreparation && m_gnomePreparation->state == windowshare::InputPreparationState::Released)
    m_gnomePreparation.reset();
  if (!m_gnomeProviderEnabled || m_gnomeProviderStopped || !m_gnomeProvider || !m_gnomeProvider->live() ||
      m_isPrimary || !m_activated || !m_isOnComputer || !m_ei || !m_eiPointer || !m_eiKeyboard || !m_eiAbs ||
      m_gnomeSubmittedKeys.any() || m_gnomeSubmittedButtons.any() || m_gnomeUntrackedInput ||
      std::this_thread::get_id() != m_gnomePrefixThread || !request || !ei_is_sender(m_ei) || m_gnomePrefixBinding ||
      m_gnomePreparation)
    return false;
  auto *const provider = m_gnomeProvider.get();
  auto *const context = m_ei;
  auto *const pointer = m_eiPointer;
  auto *const keyboard = m_eiKeyboard;
  auto *const absolute = m_eiAbs;
  const auto previousGeneration = m_sequenceNumber;
  const bool restart = !m_isEmulating;
  if (restart) {
    if (previousGeneration == std::numeric_limits<std::uint32_t>::max())
      return false;
    // A fresh transaction may start the idle sender before its requested
    // ping. This path is unavailable to retained seals/maps, and its PONG
    // must still finish the entire libei batch and the native/Qt drain.
    ensureEmulating();
  }
  if (m_gnomeProvider.get() != provider || !inputSessionLive() || m_ei != context || m_eiPointer != pointer ||
      m_eiKeyboard != keyboard || m_eiAbs != absolute || !m_isEmulating || !m_activated || !m_isOnComputer ||
      m_sequenceNumber != previousGeneration + std::uint32_t(restart) || m_gnomeSubmittedKeys.any() ||
      m_gnomeSubmittedButtons.any() || m_gnomeUntrackedInput || m_gnomePrefixBinding || m_gnomePreparation)
    return false;
  if (restart)
    LOG_DEBUG("GNOME EI cutoff idle restart generation=%u", m_sequenceNumber);
  struct ei_ping *ping = ei_new_ping(m_ei);
  if (!ping)
    return false;
  const auto id = ei_ping_get_id(ping);
  const auto now = windowshare::EiPrefixProbe::Clock::now();
  if (!m_gnomePrefixProbe.begin(request, id, now, deadline, true)) {
    ei_ping_unref(ping);
    return false;
  }
  m_gnomePrefixRequest = request;
  m_gnomePrefixEmulationGeneration = m_sequenceNumber;
  m_gnomeCutoffInvalidated = false;
  m_gnomePrefixPing = ping;
  try {
    // The 250ms absolute timer retires the ping even if no further native
    // event arrives. This never extends Server capture/preparation deadlines.
    m_gnomePrefixTimer = m_events->newOneShotTimer(std::chrono::duration<double>(deadline - now).count(), nullptr);
    m_events->addHandler(EventTypes::Timer, m_gnomePrefixTimer, [this](const auto &) {
      m_gnomePrefixProbe.finishEventBatch(windowshare::EiPrefixProbe::Clock::now());
      if (m_gnomePrefixProbe.state() != windowshare::EiPrefixProbe::State::Observed) {
        LOG_DEBUG("GNOME EI sender-prefix observation expired (not native input authority)");
        cancelGnomeEiPrefixObservation();
      } else
        retireGnomeEiPing();
    });
    ei_ping(ping);
  } catch (...) {
    cancelGnomeEiPrefixObservation();
    m_gnomePrefixProbe.retire(request);
    m_gnomePrefixRequest = 0;
    m_gnomePrefixBinding.reset();
    return false;
  }
  return true;
}
#endif

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
bool EiComputer::beginInputPreparation(
    const windowshare::InputPreparationBinding &binding, windowshare::InputCutoffPoint point,
    windowshare::InputCutoffClock::time_point deadline
)
{
#ifdef DESKFLOW_GNOME_EI_PING
  using namespace windowshare;
  const auto now = InputCutoffClock::now();
  if (!validInputPreparationBinding(binding) || !validInputCutoffPoint(point) || deadline <= now ||
      deadline > now + inputPreparationMaximumLifetime)
    return false;
  if (m_gnomePreparation && m_gnomePreparation->state == InputPreparationState::Released)
    m_gnomePreparation.reset();
  if (!beginGnomeEiPrefixObservation(binding.request, std::min(deadline, now + inputCutoffMaximumLifetime)))
    return false;
  m_gnomePreparation = std::make_unique<GnomePreparation>();
  m_gnomePreparation->binding = binding;
  m_gnomePreparation->point = point;
  m_gnomePreparation->deadline = deadline;
  cancelIdleEmulationTimer();
  return true;
#else
  return false;
#endif
}

windowshare::InputPreparationState
EiComputer::inputPreparationState(const windowshare::InputPreparationBinding &binding)
{
#ifdef DESKFLOW_GNOME_EI_PING
  using namespace windowshare;
  if (std::this_thread::get_id() != m_gnomePrefixThread || !m_gnomePreparation ||
      m_gnomePreparation->binding != binding)
    return InputPreparationState::Failed;
  auto &preparation = *m_gnomePreparation;
  if (preparation.state == InputPreparationState::Released || preparation.state == InputPreparationState::Failed)
    return preparation.state;
  const auto now = InputCutoffClock::now();
  if (preparation.state == InputPreparationState::Canceling) {
    if (now >= preparation.cleanupDeadline || !m_gnomeProvider || !m_gnomeProvider->live()) {
      abortInputPreparation(binding);
      return InputPreparationState::Failed;
    }
    if (const auto update = m_gnomeProvider->takeInputPreparationCleanupUpdate()) {
      if (update->request != preparation.providerRequest || !update->released || !update->error.empty()) {
        abortInputPreparation(binding);
        return InputPreparationState::Failed;
      }
      // Only the provider's exact native+Qt drain and preparation gate ACK
      // permits legacy submissions. A TLS loss or timeout never reaches here.
      retireGnomeEiPing();
      m_gnomePrefixProbe.retire(m_gnomePrefixRequest);
      m_gnomePrefixRequest = 0;
      m_gnomePrefixBinding.reset();
      m_gnomeMappedCutoff.reset();
      preparation.state = InputPreparationState::Released;
      preparation.focus.reset();
      if (m_isEmulating && inputSessionLive())
        ensureEmulating();
    }
    return preparation.state;
  }
  if (!inputSessionLive() || !m_activated || !m_isOnComputer || !m_isEmulating || !m_eiPointer || !m_eiKeyboard ||
      !m_eiAbs || m_sequenceNumber != m_gnomePrefixEmulationGeneration || m_gnomeCutoffInvalidated ||
      (preparation.state != InputPreparationState::Sealed && now >= preparation.deadline)) {
    preparation.state = InputPreparationState::Failed;
    preparation.outcome = InputPreparationOutcome::Failed;
    return preparation.state;
  }
  if (preparation.state == InputPreparationState::Sealed) {
    if (!preparation.promotion || !inputCutoffSealLive(*preparation.promotion)) {
      preparation.state = InputPreparationState::Failed;
      preparation.outcome = InputPreparationOutcome::Failed;
    }
    return preparation.state;
  }
  if (preparation.state == InputPreparationState::Promoting)
    return preparation.state;
  if (preparation.state == InputPreparationState::Prepared) {
    if (!preparation.focus || !preparation.focus->live()) {
      preparation.state = InputPreparationState::Failed;
      preparation.outcome = InputPreparationOutcome::Failed;
    }
    return preparation.state;
  }
  if (!preparation.prefixObserved) {
    if (!m_gnomePrefixProbe.observed(now)) {
      if (m_gnomePrefixProbe.state() == EiPrefixProbe::State::Denied) {
        preparation.state = InputPreparationState::Failed;
        preparation.outcome = InputPreparationOutcome::Failed;
      }
      return preparation.state;
    }
    // Historical prefix completion is retained only under this continuously
    // paused sender. It is never a point receipt or native input permission.
    preparation.prefixObserved = true;
    retireGnomeEiPing();
    preparation.providerRequest = m_gnomeProvider->requestInputPreparation(
        {binding,
         {binding.capture.epoch, binding.capture.sequence, double(preparation.point.x), double(preparation.point.y)},
         preparation.deadline}
    );
    if (!preparation.providerRequest) {
      preparation.state = InputPreparationState::Failed;
      preparation.outcome = InputPreparationOutcome::Unsupported;
      return preparation.state;
    }
  }
  if (const auto update = m_gnomeProvider->takeInputPreparationUpdate()) {
    if (update->request != preparation.providerRequest || !update->focus || !update->focus->live() ||
        update->focus->binding() != binding || update->focus->contract().empty() || !update->error.empty()) {
      preparation.state = InputPreparationState::Failed;
      preparation.outcome =
          update->outcome == InputPreparationOutcome::None ? InputPreparationOutcome::Failed : update->outcome;
    } else {
      const auto &snapshot = update->focus->snapshot();
      if (!snapshot.routedPoint || snapshot.routedPoint->captureEpoch != binding.capture.epoch ||
          snapshot.routedPoint->sequence != binding.capture.sequence ||
          snapshot.routedPoint->desktopX != preparation.point.x ||
          snapshot.routedPoint->desktopY != preparation.point.y) {
        preparation.state = InputPreparationState::Failed;
        preparation.outcome = InputPreparationOutcome::Failed;
      } else {
        preparation.focus = update->focus;
        preparation.state = InputPreparationState::Prepared;
      }
    }
  }
  return preparation.state;
#else
  return windowshare::InputPreparationState::Failed;
#endif
}

windowshare::InputPreparationOutcome
EiComputer::inputPreparationOutcome(const windowshare::InputPreparationBinding &binding)
{
#ifdef DESKFLOW_GNOME_EI_PING
  return m_gnomePreparation && m_gnomePreparation->binding == binding ? m_gnomePreparation->outcome
                                                                      : windowshare::InputPreparationOutcome::Failed;
#else
  return windowshare::InputPreparationOutcome::Unsupported;
#endif
}

std::optional<windowshare::PreparedInputFocusResult>
EiComputer::preparedInputFocusResult(const windowshare::InputPreparationBinding &binding)
{
#ifdef DESKFLOW_GNOME_EI_PING
  using namespace windowshare;
  if (inputPreparationState(binding) != InputPreparationState::Prepared || !m_gnomePreparation->focus)
    return std::nullopt;
  const auto &paint = m_gnomePreparation->focus->snapshot().paint;
  PreparedInputFocusResult result{
      binding,        m_gnomePreparation->point, paint.connection,   {paint.session, paint.epoch},
      paint.geometry, paint.focusRequest,        paint.focusRevision
  };
  const auto fingerprint = QByteArray::fromHex(paint.peerFingerprint.toLatin1());
  if (fingerprint.size() != result.viewerPeerFingerprint.size())
    return std::nullopt;
  std::copy(fingerprint.begin(), fingerprint.end(), result.viewerPeerFingerprint.begin());
  return validPreparedInputFocusResult(result) ? std::optional(result) : std::nullopt;
#else
  return std::nullopt;
#endif
}

bool EiComputer::promoteInputPreparation(
    const windowshare::InputPreparationBinding &preparation, const windowshare::InputCutoffBinding &binding,
    windowshare::InputCutoffClock::time_point deadline
)
{
#ifdef DESKFLOW_GNOME_EI_PING
  using namespace windowshare;
  const auto prepared = preparedInputFocusResult(preparation);
  const auto now = InputCutoffClock::now();
  if (!prepared || !matchesInputPreparationPromotion(*prepared, binding) || deadline <= now ||
      deadline > now + inputCutoffMaximumLifetime || deadline > m_gnomePreparation->deadline || m_gnomeMappedCutoff ||
      m_gnomePrefixBinding)
    return false;
  const auto request = m_gnomeProvider->promoteInputPreparation(
      *m_gnomePreparation->focus,
      {binding,
       {binding.capture.epoch, binding.capture.sequence, double(prepared->point.x), double(prepared->point.y)},
       deadline,
       true}
  );
  if (!request)
    return false;
  m_gnomePreparation->state = InputPreparationState::Promoting;
  m_gnomePreparation->promotion = binding;
  m_gnomePrefixBinding = binding;
  m_gnomeMappedCutoff = std::make_unique<GnomeMappedCutoff>();
  m_gnomeMappedCutoff->point = prepared->point;
  m_gnomeMappedCutoff->binding = binding;
  m_gnomeMappedCutoff->deadline = deadline;
  m_gnomeMappedCutoff->providerRequest = request;
  m_gnomeMappedCutoff->retainPrefix = true;
  return true;
#else
  return false;
#endif
}

bool EiComputer::cancelInputPreparation(
    const windowshare::InputPreparationBinding &binding, windowshare::InputCutoffClock::time_point deadline
)
{
#ifdef DESKFLOW_GNOME_EI_PING
  using namespace windowshare;
  if (std::this_thread::get_id() != m_gnomePrefixThread || !m_gnomePreparation ||
      m_gnomePreparation->binding != binding)
    return false;
  auto &preparation = *m_gnomePreparation;
  if (preparation.state == InputPreparationState::Released)
    return true;
  if (preparation.state == InputPreparationState::Canceling)
    return InputCutoffClock::now() < preparation.cleanupDeadline;
  const auto now = InputCutoffClock::now();
  if (deadline <= now || deadline > now + inputCutoffMaximumLifetime)
    return false;
  preparation.state = InputPreparationState::Canceling;
  preparation.cleanupDeadline = deadline;
  preparation.focus.reset();
  invalidateGnomeInputCutoff();
  retireGnomeEiPing();
  if (preparation.providerRequest) {
    if (!m_gnomeProvider || !m_gnomeProvider->cancelInputPreparation(preparation.providerRequest, deadline))
      return false;
    return true;
  }
  // No provider gate was created. The only backend action was the bounded
  // ping, so there is no native authority or forwarded input to retire.
  m_gnomePrefixProbe.retire(m_gnomePrefixRequest);
  m_gnomePrefixRequest = 0;
  m_gnomePrefixBinding.reset();
  m_gnomeMappedCutoff.reset();
  preparation.state = InputPreparationState::Released;
  if (m_isEmulating && inputSessionLive())
    ensureEmulating();
  return true;
#else
  return false;
#endif
}

void EiComputer::abortInputPreparation(const windowshare::InputPreparationBinding &binding) noexcept
{
#ifdef DESKFLOW_GNOME_EI_PING
  if (!m_gnomePreparation || m_gnomePreparation->binding != binding ||
      m_gnomePreparation->state == windowshare::InputPreparationState::Released)
    return;
  m_gnomePreparation->state = windowshare::InputPreparationState::Failed;
  m_gnomePreparation->outcome = windowshare::InputPreparationOutcome::Failed;
  m_gnomePreparation->focus.reset();
  try {
    stopGnomeProvider("Input preparation abandoned before exact cleanup");
  } catch (...) {
  }
#endif
}

bool EiComputer::beginInputCutoff(
    const windowshare::InputCutoffBinding &binding, windowshare::InputCutoffClock::time_point deadline
)
{
#ifdef DESKFLOW_GNOME_EI_PING
  return beginGnomeEiPrefixObservation(binding, deadline);
#else
  return false;
#endif
}
bool EiComputer::beginMappedInputCutoff(
    const windowshare::InputCutoffBinding &binding, windowshare::InputCutoffPoint point,
    windowshare::InputCutoffClock::time_point deadline
)
{
#ifdef DESKFLOW_GNOME_EI_PING
  if (!windowshare::validInputCutoffPoint(point) || !beginGnomeEiPrefixObservation(binding, deadline))
    return false;
  m_gnomeMappedCutoff = std::make_unique<GnomeMappedCutoff>();
  m_gnomeMappedCutoff->point = point;
  m_gnomeMappedCutoff->binding = binding;
  m_gnomeMappedCutoff->deadline = deadline;
  return true;
#else
  return false;
#endif
}
bool EiComputer::beginSealedInputCutoff(
    const windowshare::InputCutoffBinding &binding, windowshare::InputCutoffPoint point,
    windowshare::InputCutoffClock::time_point deadline
)
{
#ifdef DESKFLOW_GNOME_EI_PING
  if (!beginMappedInputCutoff(binding, point, deadline))
    return false;
  m_gnomeMappedCutoff->retainPrefix = true;
  return true;
#else
  return false;
#endif
}
bool EiComputer::inputCutoffSealLive(const windowshare::InputCutoffBinding &binding)
{
#ifdef DESKFLOW_GNOME_EI_PING
  return std::this_thread::get_id() == m_gnomePrefixThread && m_gnomePrefixBinding == binding &&
         !m_gnomeCutoffInvalidated && m_sequenceNumber == m_gnomePrefixEmulationGeneration && inputSessionLive() &&
         m_activated && m_isOnComputer && m_isEmulating && m_eiPointer && m_eiKeyboard && m_eiAbs &&
         m_gnomeMappedCutoff && m_gnomeMappedCutoff->seal && m_gnomeMappedCutoff->seal->binding() == binding &&
         m_gnomeMappedCutoff->seal->live();
#else
  return false;
#endif
}
bool EiComputer::refreshMappedInputCutoff(
    const windowshare::InputCutoffBinding &seal, const windowshare::InputCutoffBinding &binding,
    windowshare::InputCutoffPoint point, windowshare::InputCutoffClock::time_point deadline, bool presentCursor
)
{
#ifdef DESKFLOW_GNOME_EI_PING
  using namespace windowshare;
  const auto now = InputCutoffClock::now();
  if (!inputCutoffSealLive(seal) || !continuesInputCutoffSeal(m_gnomeMappedCutoff->binding, binding) ||
      !validInputCutoffPoint(point) || deadline <= now || deadline > now + inputCutoffMaximumLifetime ||
      m_gnomeMappedCutoff->refreshing)
    return false;
  auto &mapped = *m_gnomeMappedCutoff;
  const auto request = m_gnomeProvider->requestInputMap(
      *mapped.seal,
      {binding, {binding.capture.epoch, binding.capture.sequence, double(point.x), double(point.y)}, deadline, false}
  );
  if (!request)
    return false;
  mapped.binding = binding;
  mapped.point = point;
  mapped.deadline = deadline;
  mapped.providerRequest = request;
  mapped.segment.reset();
  mapped.batch.clear();
  mapped.batchSegments.clear();
  mapped.refreshing = true;
  // A retained owner may hold a stationary physical control. Ordinary idle
  // timeout must not release its backend session; exact resume restores it.
  cancelIdleEmulationTimer();
  if (presentCursor) {
    // Only cursor presentation is permitted across this continuously closed
    // All gate. No key/button/scroll or emulation restart can use this path.
    // Native hit testing names the frozen point, not the sampled cursor.
    ei_device_pointer_motion_absolute(m_eiAbs, point.x, point.y);
    ei_device_frame(m_eiAbs, ei_now(m_ei));
  }
  return inputCutoffSealLive(seal);
#else
  return false;
#endif
}
bool EiComputer::beginMappedInputBatch(
    const windowshare::InputCutoffBinding &seal, const std::vector<windowshare::InputCutoffBatchItem> &items
)
{
#ifdef DESKFLOW_GNOME_EI_PING
  using namespace windowshare;
  if (!inputCutoffSealLive(seal) || m_gnomeMappedCutoff->refreshing ||
      !validInputCutoffBatch(m_gnomeMappedCutoff->binding, items))
    return false;
  auto &mapped = *m_gnomeMappedCutoff;
  std::vector<GnomeProviderClient::InputDrainRequest> requests;
  for (const auto &item : items)
    requests.push_back(
        {item.binding,
         {item.binding.capture.epoch, item.binding.capture.sequence, double(item.point.x), double(item.point.y)},
         item.deadline,
         false}
    );
  const auto request = m_gnomeProvider->requestInputMapBatch(*mapped.seal, std::move(requests));
  if (!request)
    return false;
  mapped.binding = items.back().binding;
  mapped.deadline = std::min_element(items.begin(), items.end(), [](const auto &a, const auto &b) {
                      return a.deadline < b.deadline;
                    })->deadline;
  mapped.providerRequest = request;
  mapped.segment.reset();
  mapped.batch = items;
  mapped.batchSegments.clear();
  mapped.refreshing = true;
  cancelIdleEmulationTimer(); // Retained ownership survives stationary holds.
  for (const auto &item : items) {
    if (item.presentCursor) {
      // Ordered cursor presentation under the retained All gate only. No
      // application key/button/scroll path is opened by a batch.
      ei_device_pointer_motion_absolute(m_eiAbs, item.point.x, item.point.y);
      ei_device_frame(m_eiAbs, ei_now(m_ei));
    }
  }
  return inputCutoffSealLive(seal);
#else
  return false;
#endif
}
windowshare::InputCutoffState EiComputer::inputCutoffState(const windowshare::InputCutoffBinding &binding)
{
#ifdef DESKFLOW_GNOME_EI_PING
  if (std::this_thread::get_id() != m_gnomePrefixThread || !m_gnomePrefixBinding ||
      (m_gnomeMappedCutoff ? (m_gnomeMappedCutoff->binding != binding &&
                              std::none_of(
                                  m_gnomeMappedCutoff->batch.begin(), m_gnomeMappedCutoff->batch.end(),
                                  [&](const auto &item) { return item.binding == binding; }
                              ))
                           : m_gnomePrefixBinding != binding))
    return windowshare::InputCutoffState::Denied;
  if (!inputSessionLive() || !m_activated || !m_isOnComputer || !m_isEmulating || !m_eiPointer || !m_eiKeyboard ||
      !m_eiAbs || m_sequenceNumber != m_gnomePrefixEmulationGeneration)
    invalidateGnomeInputCutoff();
  if (m_gnomeMappedCutoff && m_gnomeMappedCutoff->seal) {
    auto &mapped = *m_gnomeMappedCutoff;
    if (!inputCutoffSealLive(*m_gnomePrefixBinding)) {
      cancelGnomeEiPrefixObservation();
      return windowshare::InputCutoffState::Denied;
    }
    if (!mapped.batch.empty()) {
      if (mapped.refreshing) {
        if (const auto update = m_gnomeProvider->takeInputMapBatchUpdate()) {
          bool valid = update->request == mapped.providerRequest && update->error.empty() &&
                       update->segments.size() == mapped.batch.size();
          for (std::size_t index = 0; valid && index < mapped.batch.size(); ++index) {
            const auto &segment = update->segments[index];
            valid = segment.binding() == mapped.batch[index].binding && segment.live() && segment.seal() &&
                    segment.seal()->binding() == *m_gnomePrefixBinding && segment.seal()->live();
          }
          if (!valid) {
            cancelGnomeEiPrefixObservation();
            return windowshare::InputCutoffState::Denied;
          }
          mapped.batchSegments = update->segments;
          mapped.refreshing = false;
        }
      }
      if (mapped.refreshing) {
        if (windowshare::InputCutoffClock::now() >= mapped.deadline) {
          cancelGnomeEiPrefixObservation();
          return windowshare::InputCutoffState::Denied;
        }
        return windowshare::InputCutoffState::Pending;
      }
      const auto item = std::find_if(mapped.batch.begin(), mapped.batch.end(), [&](const auto &value) {
        return value.binding == binding;
      });
      if (item == mapped.batch.end())
        return windowshare::InputCutoffState::Denied;
      const auto index = static_cast<std::size_t>(item - mapped.batch.begin());
      if (windowshare::InputCutoffClock::now() >= item->deadline)
        return windowshare::InputCutoffState::Sealed;
      if (index >= mapped.batchSegments.size() || !mapped.batchSegments[index].live()) {
        cancelGnomeEiPrefixObservation();
        return windowshare::InputCutoffState::Denied;
      }
      return windowshare::InputCutoffState::DrainObserved;
    }
    if (mapped.refreshing) {
      if (const auto update = m_gnomeProvider->takeInputMapUpdate()) {
        if (update->request != mapped.providerRequest || !update->segment || update->segment->binding() != binding ||
            !update->segment->live() || !update->segment->seal() ||
            update->segment->seal()->binding() != *m_gnomePrefixBinding || !update->segment->seal()->live()) {
          cancelGnomeEiPrefixObservation();
          return windowshare::InputCutoffState::Denied;
        }
        mapped.segment = update->segment;
        mapped.refreshing = false;
      }
    }
    if (windowshare::InputCutoffClock::now() >= mapped.deadline) {
      if (mapped.refreshing) {
        cancelGnomeEiPrefixObservation();
        return windowshare::InputCutoffState::Denied;
      }
      return windowshare::InputCutoffState::Sealed;
    }
    if (!mapped.refreshing && (!mapped.segment || !mapped.segment->live())) {
      cancelGnomeEiPrefixObservation();
      return windowshare::InputCutoffState::Denied;
    }
    return mapped.refreshing ? windowshare::InputCutoffState::Pending : windowshare::InputCutoffState::DrainObserved;
  }
  if (!m_gnomeCutoffInvalidated &&
      ((m_gnomePreparation && m_gnomePreparation->prefixObserved && m_gnomePreparation->promotion == binding &&
        (m_gnomePreparation->state == windowshare::InputPreparationState::Promoting ||
         m_gnomePreparation->state == windowshare::InputPreparationState::Sealed)) ||
       m_gnomePrefixProbe.observed(windowshare::InputCutoffClock::now()))) {
    if (!m_gnomeMappedCutoff)
      return windowshare::InputCutoffState::BackendObserved;
    auto &mapped = *m_gnomeMappedCutoff;
    if (!mapped.providerRequest) {
      // A matching PONG is necessary but insufficient. The provider performs
      // the supported native-thread, compositor and exact All-fenced Qt join
      // at this retained point under the original absolute deadline.
      mapped.providerRequest = m_gnomeProvider->requestInputDrain(
          {binding,
           {binding.capture.epoch, binding.capture.sequence, static_cast<double>(mapped.point.x),
            static_cast<double>(mapped.point.y)},
           mapped.deadline,
           mapped.retainPrefix}
      );
      if (!mapped.providerRequest) {
        cancelGnomeEiPrefixObservation();
        return windowshare::InputCutoffState::Denied;
      }
    }
    if (const auto update = m_gnomeProvider->takeInputDrainUpdate()) {
      if (update->request != mapped.providerRequest || !update->segment || update->segment->binding() != binding ||
          !update->segment->live() || !update->segment->senderOrdering() ||
          !update->segment->senderOrdering()->live()) {
        cancelGnomeEiPrefixObservation();
        return windowshare::InputCutoffState::Denied;
      }
      mapped.segment = update->segment;
      if (mapped.retainPrefix) {
        if (!mapped.segment->seal() || !mapped.segment->seal()->live() ||
            mapped.segment->seal()->binding() != binding) {
          cancelGnomeEiPrefixObservation();
          return windowshare::InputCutoffState::Denied;
        }
        mapped.seal = mapped.segment->seal();
        cancelIdleEmulationTimer();
        // The bounded ping has done its job. Do not extend its deadline;
        // retain a separate continuously closed, independently revocable seal.
        retireGnomeEiPing();
      }
    }
    if (mapped.segment) {
      if (!mapped.segment->live() || !mapped.segment->senderOrdering() || !mapped.segment->senderOrdering()->live()) {
        cancelGnomeEiPrefixObservation();
        return windowshare::InputCutoffState::Denied;
      }
      if (m_gnomePreparation && m_gnomePreparation->promotion == binding)
        m_gnomePreparation->state = windowshare::InputPreparationState::Sealed;
      return windowshare::InputCutoffState::DrainObserved;
    }
    return windowshare::InputCutoffState::Pending;
  }
  if (m_gnomePrefixProbe.state() == windowshare::EiPrefixProbe::State::Denied)
    cancelGnomeEiPrefixObservation();
  return m_gnomePrefixProbe.state() == windowshare::EiPrefixProbe::State::Denied
             ? windowshare::InputCutoffState::Denied
             : windowshare::InputCutoffState::Pending;
#else
  return windowshare::InputCutoffState::Denied;
#endif
}
std::optional<windowshare::InputCutoffMappedResult>
EiComputer::mappedInputCutoffResult(const windowshare::InputCutoffBinding &binding)
{
#ifdef DESKFLOW_GNOME_EI_PING
  using namespace windowshare;
  if (inputCutoffState(binding) != InputCutoffState::DrainObserved || !m_gnomeMappedCutoff)
    return std::nullopt;
  const auto &mapped = *m_gnomeMappedCutoff;
  const auto *selected = mapped.segment ? &*mapped.segment : nullptr;
  auto point = mapped.point;
  if (!mapped.batch.empty()) {
    const auto item = std::find_if(mapped.batch.begin(), mapped.batch.end(), [&](const auto &value) {
      return value.binding == binding;
    });
    if (item == mapped.batch.end())
      return std::nullopt;
    const auto index = static_cast<std::size_t>(item - mapped.batch.begin());
    if (index >= mapped.batchSegments.size())
      return std::nullopt;
    selected = &mapped.batchSegments[index];
    point = item->point;
  }
  if (!selected)
    return std::nullopt;
  const auto &segment = *selected;
  const auto &snapshot = segment.snapshot();
  const auto &paint = snapshot.paint;
  if (!snapshot.routedPoint || snapshot.routedPoint->captureEpoch != binding.capture.epoch ||
      snapshot.routedPoint->sequence != binding.capture.sequence || snapshot.routedPoint->desktopX != point.x ||
      snapshot.routedPoint->desktopY != point.y || paint.connection != binding.viewerConnection ||
      paint.session != binding.forwarding.owner.session || paint.epoch != binding.forwarding.owner.epoch ||
      paint.geometry != binding.forwarding.geometry || paint.focusRequest != binding.forwarding.focusRequest ||
      paint.focusRevision != binding.forwarding.focusRevision)
    return std::nullopt;
  InputCutoffMappedResult result;
  result.binding = binding;
  result.point = point;
  result.sourceX = snapshot.sourceX;
  result.sourceY = snapshot.sourceY;
  result.sourceWidth = paint.sourceWidth;
  result.sourceHeight = paint.sourceHeight;
  result.surfaceIncarnation = paint.surfaceIncarnation;
  result.viewportRevision = paint.viewportRevision;
  result.nativeSurface = paint.nativeSurface;
  result.providerGeneration = snapshot.generation;
  result.nativeRevision = snapshot.nativeRevision;
  const auto fingerprint = QByteArray::fromHex(paint.peerFingerprint.toLatin1());
  if (fingerprint.size() != result.viewerPeerFingerprint.size())
    return std::nullopt;
  std::copy(fingerprint.begin(), fingerprint.end(), result.viewerPeerFingerprint.begin());
  const bool ordered =
      segment.seal() ? segment.seal()->live() : segment.senderOrdering() && segment.senderOrdering()->live();
  if (!ordered)
    return std::nullopt;
  result.orderingContract = segment.seal() ? segment.seal()->contract() : segment.senderOrdering()->contract();
  return segment.live() && validInputCutoffMappedResult(result) ? std::optional(result) : std::nullopt;
#else
  return std::nullopt;
#endif
}
bool EiComputer::resumeInputCutoff(const windowshare::InputCutoffBinding &binding)
{
#ifdef DESKFLOW_GNOME_EI_PING
  if (std::this_thread::get_id() != m_gnomePrefixThread || m_gnomePrefixBinding != binding)
    return false;
  if (m_gnomePreparation && m_gnomePreparation->promotion == binding &&
      m_gnomePreparation->state != windowshare::InputPreparationState::Released) {
    // Linked preparation cleanup is asynchronous; direct resume must never
    // reopen the sender while its distinct Qt gate remains owned.
    cancelInputPreparation(
        m_gnomePreparation->binding, windowshare::InputCutoffClock::now() + windowshare::inputCutoffMaximumLifetime
    );
    return false;
  }
  cancelGnomeEiPrefixObservation();
  m_gnomePrefixBinding.reset();
  m_gnomeMappedCutoff.reset();
  const bool retired = m_gnomePrefixProbe.retire(m_gnomePrefixRequest);
  m_gnomePrefixRequest = 0;
  if (m_gnomePreparation && m_gnomePreparation->promotion == binding)
    m_gnomePreparation.reset();
  // Restore ordinary inactivity only for this still-running session. Resume
  // never revives a stopped/emulation-replaced backend.
  if (m_isEmulating && inputSessionLive())
    ensureEmulating();
  return retired;
#else
  return false;
#endif
}
#endif

void EiComputer::handleSystemEvent(const Event &)
{
  std::scoped_lock lock{m_mutex};

  const auto budget = m_events->queueBudget();
  const auto started = EventQueueBudget::Clock::now();
  const auto live = [&] {
    if (!budget)
      return true;
    if (EventQueueBudget::Clock::now() - started > EventQueueBudget::maximumAge)
      budget->fail(EventQueueBudget::Failure::Deadline);
    if (budget->live())
      return true;
#ifdef DESKFLOW_GNOME_EI_PING
    // Never publish a prefix observation from a partial native batch. The
    // terminal Core budget causes the enclosing loop to exit and disconnect.
    invalidateGnomeInputCutoff();
#endif
    return false;
  };
  if (!live())
    return;

  // Only one ei_dispatch per system event, see the comment in
  // EiEventQueueBuffer::addEvent
  ei_dispatch(m_ei);
  // A library call cannot be preempted by this check. Once it returns, do not
  // consume or acknowledge an expired batch, even when it produced no events.
  if (!live())
    return;
  struct ei_event *event;
  std::uint64_t consumed = 0;

  while ((event = ei_get_event(m_ei)) != nullptr) {
    if (budget && consumed == EventQueueBudget::nativeBatchCapacity)
      budget->fail(EventQueueBudget::Failure::NativeBatchCapacity);
    if (!live()) {
      ei_event_unref(event);
      return;
    }
    ++consumed;
    auto type = ei_event_get_type(event);
    auto seat = ei_event_get_seat(event);
    auto device = ei_event_get_device(event);

    switch (type) {
    case EI_EVENT_CONNECT:
      LOG_DEBUG("connected to eis");
      break;
#ifdef DESKFLOW_GNOME_EI_PING
    case EI_EVENT_PONG:
      if (m_gnomePrefixPing) {
        auto *answer = ei_event_pong_get_ping(event);
        m_gnomePrefixProbe.pong(
            answer == m_gnomePrefixPing ? ei_ping_get_id(answer) : 0, windowshare::EiPrefixProbe::Clock::now()
        );
      }
      break;
#endif
    case EI_EVENT_SEAT_ADDED:
      if (!m_eiSeat) {
        m_eiSeat = ei_seat_ref(seat);
        ei_seat_bind_capabilities(
            m_eiSeat, EI_DEVICE_CAP_POINTER, EI_DEVICE_CAP_POINTER_ABSOLUTE, EI_DEVICE_CAP_KEYBOARD,
            EI_DEVICE_CAP_BUTTON, EI_DEVICE_CAP_SCROLL, nullptr
        );
        LOG_DEBUG("ei: using seat %s", ei_seat_get_name(m_eiSeat));
        // we don't care about touch
      }
      break;
    case EI_EVENT_DEVICE_ADDED:
      if (seat == m_eiSeat) {
        addDevice(device);
      } else {
        LOG_INFO("seat %s is ignored", ei_seat_get_name(m_eiSeat));
      }
      break;
    case EI_EVENT_DEVICE_REMOVED:
#ifdef DESKFLOW_GNOME_EI_PING
      if (m_gnomeProviderEnabled)
        invalidateGnomeInputCutoff();
#endif
      removeDevice(device);
      break;
    case EI_EVENT_SEAT_REMOVED:
#ifdef DESKFLOW_GNOME_EI_PING
      if (m_gnomeProviderEnabled)
        invalidateGnomeInputCutoff();
#endif
      if (seat == m_eiSeat) {
        m_eiSeat = ei_seat_unref(m_eiSeat);
      }
      break;
    case EI_EVENT_DISCONNECT:
#ifdef DESKFLOW_GNOME_EI_PING
      if (m_gnomeProviderEnabled)
        invalidateGnomeInputCutoff();
#endif
#ifdef BUILD_WINDOWSHARE
      if (m_gnomeProvider) {
        stopGnomeProvider("EIS disconnected");
        break;
      }
#endif
      // We're using libei which emulates the various seat/device remove events
      // so by the time we get here our EiComputer should be in a neutral state.
      //
      // We must release the xdg-portal InputCapture in case it is still active
      // so that the cursor is usable and not stuck on the deskflow server.
      LOG_WARN("disconnected from eis, will afterwards commence attempt to reconnect");
      if (m_isPrimary) {
        LOG_DEBUG("re-allocating portal input capture connection and releasing active captures");
        if (m_portalInputCapture) {
          if (m_portalInputCapture->isActive()) {
            m_portalInputCapture->release();
          }
          delete m_portalInputCapture;
          m_portalInputCapture = new PortalInputCapture(this, this->m_events);
        }
      }
      this->handlePortalSessionClosed();
      break;
    case EI_EVENT_DEVICE_PAUSED:
#ifdef DESKFLOW_GNOME_EI_PING
      if (m_gnomeProviderEnabled) {
        invalidateGnomeInputCutoff();
        if (device == m_eiKeyboard)
          m_gnomeSubmittedKeys.reset();
        if (device == m_eiPointer)
          m_gnomeSubmittedButtons.reset();
      }
#endif
      LOG_DEBUG("device %s is paused", ei_device_get_name(device));
      m_isEmulating = false;
      // a paused device is reset to neutral by the EIS side and sends no
      // further events, so the releases for held buttons never arrive
      updateButtons();
      cancelIdleEmulationTimer();
      break;
    case EI_EVENT_DEVICE_RESUMED:
      LOG_DEBUG("device %s is resumed", ei_device_get_name(device));
      if (!m_isPrimary && m_isOnComputer) {
        ensureEmulating();
      }
      break;
    case EI_EVENT_KEYBOARD_MODIFIERS:
      LOG_VERBOSE("event: keyboard modifiers locked=0x%x", ei_event_keyboard_get_xkb_mods_locked(event));
      m_keyState->updateLockedModifiers(ei_event_keyboard_get_xkb_mods_locked(event));
      break;

    // events below are for a receiver context (barriers)
    case EI_EVENT_FRAME:
      break;
    case EI_EVENT_DEVICE_START_EMULATING:
      LOG_DEBUG("device %s started emulating", ei_device_get_name(device));
      break;
    case EI_EVENT_DEVICE_STOP_EMULATING:
      LOG_DEBUG("device %s stopped emulating", ei_device_get_name(device));
      break;
    case EI_EVENT_KEYBOARD_KEY:
      onKeyEvent(event);
      break;
    case EI_EVENT_BUTTON_BUTTON:
      onButtonEvent(event);
      break;
    case EI_EVENT_POINTER_MOTION:
      onMotionEvent(event);
      break;
    case EI_EVENT_POINTER_MOTION_ABSOLUTE:
      onAbsMotionEvent(event);
      break;
    case EI_EVENT_TOUCH_UP:
      break;
    case EI_EVENT_TOUCH_MOTION:
      break;
    case EI_EVENT_TOUCH_DOWN:
      break;
    case EI_EVENT_SCROLL_DELTA:
      onPointerScrollEvent(event);
      break;
    case EI_EVENT_SCROLL_DISCRETE:
      onPointerScrollDiscreteEvent(event);
      break;
    case EI_EVENT_SCROLL_STOP:
    case EI_EVENT_SCROLL_CANCEL:
      break;
    default:
      break;
    }
    ei_event_unref(event);
    if (!live())
      return;
  }
#ifdef DESKFLOW_GNOME_EI_PING
  // libei explicitly permits a synthetic PONG immediately before disconnect.
  // Do not acknowledge the PONG inside the switch or before the full event
  // batch has been consumed, and do not mix observations across EI contexts.
  if ((!m_gnomeMappedCutoff || !m_gnomeMappedCutoff->seal) &&
      (!m_gnomePreparation || !m_gnomePreparation->prefixObserved))
    m_gnomePrefixProbe.finishEventBatch(windowshare::EiPrefixProbe::Clock::now());
  if (m_gnomePrefixPing && !m_gnomePrefixProbe.awaitingPong()) {
    if (m_gnomePrefixProbe.observed(windowshare::EiPrefixProbe::Clock::now()))
      LOG_DEBUG("GNOME EI sender prefix observed after complete event batch; Clutter/Qt drain NOT proven");
    retireGnomeEiPing();
  }
#endif
}

void EiComputer::updateButtons()
{
  // libei relies on the EIS implementation to keep our button count correct,
  // and the held buttons cannot be polled, so resyncing means assuming that
  // everything is released.
  m_buttons.reset();
}

IKeyState *EiComputer::getKeyState() const
{
  return m_keyState;
}

std::string EiComputer::getSecureInputApp() const
{
  throw std::runtime_error("get security input app not implemented");
}

EiComputer::HotKeyItem::HotKeyItem(std::uint32_t mask, std::uint32_t id) : mask(mask), id(id)
{
  // Todo: Implement
}

EiComputer::HotKeySet::HotKeySet(KeyID key) : m_id(key)
{
  // Todo: Implement
}

bool EiComputer::HotKeySet::removeById(std::uint32_t id)
{
  for (auto it = m_set.begin(); it != m_set.end(); ++it) {
    if (it->id == id) {
      m_set.erase(it);
      return true;
    }
  }
  return false;
}

void EiComputer::HotKeySet::addItem(HotKeyItem item)
{
  m_set.push_back(item);
}

std::uint32_t EiComputer::HotKeySet::findByMask(std::uint32_t mask) const
{
  for (const auto &item : m_set) {
    if (item.mask == mask) {
      return item.id;
    }
  }
  return 0;
}

#ifdef HAVE_LIBPORTAL_SHORTCUTS
const std::vector<PortalGlobalShortcuts::HotKey> EiComputer::HotKeySet::getPortalHotKeys() const
{
  std::vector<PortalGlobalShortcuts::HotKey> portalHotKeys;
  portalHotKeys.reserve(m_set.size());

  for (const auto &item : m_set) {
    portalHotKeys.push_back({
        .id = item.id,
        .key = m_id,
        .mask = item.mask,
    });
  }
  return portalHotKeys;
}
#endif

} // namespace deskflow
