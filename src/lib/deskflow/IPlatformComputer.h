/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardTypes.h"
#include "deskflow/IComputer.h"
#include "deskflow/IKeyState.h"
#include "deskflow/IPrimaryComputer.h"
#include "deskflow/ISecondaryComputer.h"
#include "deskflow/OptionTypes.h"
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
#include "windowshare/InputCutoff.h"
#include "windowshare/InputPreparation.h"
#endif

class IClipboard;

//! Computer interface
/*!
This interface defines the methods common to all platform dependent
computer implementations that are used by both primary and secondary
computers.
*/
class IPlatformComputer : public IComputer, public IPrimaryComputer, public ISecondaryComputer, public IKeyState
{
public:
  //! @name manipulators
  //@{

  explicit IPlatformComputer(const IEventQueue *events) : IKeyState(events)
  {
    // do nothing
  }

  //! Enable computer
  /*!
  Enable the computer, preparing it to report system and user events.
  For a secondary computer it also means preparing to synthesize events
  and hiding the cursor.
  */
  virtual void enable() = 0;

  //! Disable computer
  /*!
  Undoes the operations in enable() and events should no longer
  be reported.
  */
  virtual void disable() = 0;

  //! Enter computer
  /*!
  Called when the user navigates to this computer.
  */
  virtual void enter() = 0;

  //! Leave computer
  /*!
  Called when the user navigates off the computer.  Returns true if
  the leave can proceed.  A typical reason for failure is being
  unable to install the keyboard and mouse snoopers on a primary
  computer.  Secondary computers should not fail.
  */
  virtual bool canLeave() = 0;

  //! Leave computer
  /*!
  Called when the user navigates off the computer.  Should be gated
  by canLeave().
  */
  virtual void leave() = 0;

  //! Set clipboard
  /*!
  Set the contents of the system clipboard indicated by \c id.
  */
  virtual bool setClipboard(ClipboardID id, const IClipboard *) = 0;

  //! Check clipboard owner
  /*!
  Check ownership of all clipboards and post grab events for any that
  have changed.  This is used as a backup in case the system doesn't
  reliably report clipboard ownership changes.
  */
  virtual void checkClipboards() = 0;

  //! Open screen saver
  /*!
  Open the screen saver.  If \c notify is true then this object must
  send events when the screen saver activates or deactivates until
  \c closeScreensaver() is called.  If \c notify is false then the
  screen saver is disabled and restored on \c closeScreensaver().
  */
  virtual void openScreensaver(bool notify) = 0;

  //! Close screen saver
  /*!
  Close the screen saver.  Stop reporting screen saver activation
  and deactivation and, if the screen saver was disabled by
  openScreensaver(), enable the screen saver.
  */
  virtual void closeScreensaver() = 0;

  //! Activate/deactivate screen saver
  /*!
  Forcibly activate the screen saver if \c activate is true otherwise
  forcibly deactivate it.
  */
  virtual void screensaver(bool activate) = 0;

  //! Notify of options changes
  /*!
  Reset all options to their default values.
  */
  virtual void resetOptions() = 0;

  //! Notify of options changes
  /*!
  Set options to given values.  Ignore unknown options and don't
  modify options that aren't given in \c options.
  */
  virtual void setOptions(const OptionsList &options) = 0;

  //! Set clipboard sequence number
  /*!
  Sets the sequence number to use in subsequent clipboard events.
  */
  virtual void setSequenceNumber(uint32_t) = 0;

  //! Determine the name of the app causing a secure input state
  /*!
  On MacOS check which app causes a secure input state to be enabled. No
  alternative on other platforms
  */
  virtual std::string getSecureInputApp() const = 0;

  //@}
  //! @name accessors
  //@{

  //! Test if is primary computer
  /*!
  Return true iff this computer is a primary computer.
  */
  virtual bool isPrimary() const = 0;

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  // Whether this backend reports actual API submissions inside the current
  // legacy-input sequence scope. This is not proof of native input delivery.
  virtual bool supportsInputSequenceTrace() const noexcept
  {
    return false;
  }
  // Owner-thread asynchronous sender-prefix observation. Default unsupported;
  // no compositor/application drain or original-input authority is returned.
  virtual bool beginInputPreparation(
      const deskflow::windowshare::InputPreparationBinding &, deskflow::windowshare::InputCutoffPoint,
      deskflow::windowshare::InputCutoffClock::time_point
  )
  {
    return false;
  }
  virtual deskflow::windowshare::InputPreparationState
  inputPreparationState(const deskflow::windowshare::InputPreparationBinding &)
  {
    return deskflow::windowshare::InputPreparationState::Failed;
  }
  virtual deskflow::windowshare::InputPreparationOutcome
  inputPreparationOutcome(const deskflow::windowshare::InputPreparationBinding &)
  {
    return deskflow::windowshare::InputPreparationOutcome::Unsupported;
  }
  virtual std::optional<deskflow::windowshare::PreparedInputFocusResult>
  preparedInputFocusResult(const deskflow::windowshare::InputPreparationBinding &)
  {
    return std::nullopt;
  }
  virtual bool promoteInputPreparation(
      const deskflow::windowshare::InputPreparationBinding &, const deskflow::windowshare::InputCutoffBinding &,
      deskflow::windowshare::InputCutoffClock::time_point
  )
  {
    return false;
  }
  virtual bool cancelInputPreparation(
      const deskflow::windowshare::InputPreparationBinding &, deskflow::windowshare::InputCutoffClock::time_point
  )
  {
    return false;
  }
  // Transport teardown cannot claim a drain. Permanently disable this owned
  // backend session if asynchronous preparation cleanup cannot be retained.
  virtual void abortInputPreparation(const deskflow::windowshare::InputPreparationBinding &) noexcept
  {
  }

  virtual bool beginInputCutoff(
      const deskflow::windowshare::InputCutoffBinding &, deskflow::windowshare::InputCutoffClock::time_point
  )
  {
    return false;
  }
  virtual bool beginMappedInputCutoff(
      const deskflow::windowshare::InputCutoffBinding &, deskflow::windowshare::InputCutoffPoint,
      deskflow::windowshare::InputCutoffClock::time_point
  )
  {
    return false;
  }
  virtual std::optional<deskflow::windowshare::InputCutoffMappedResult>
  mappedInputCutoffResult(const deskflow::windowshare::InputCutoffBinding &)
  {
    return std::nullopt;
  }
  virtual bool beginSealedInputCutoff(
      const deskflow::windowshare::InputCutoffBinding &, deskflow::windowshare::InputCutoffPoint,
      deskflow::windowshare::InputCutoffClock::time_point
  )
  {
    return false;
  }
  virtual bool refreshMappedInputCutoff(
      const deskflow::windowshare::InputCutoffBinding &, const deskflow::windowshare::InputCutoffBinding &,
      deskflow::windowshare::InputCutoffPoint, deskflow::windowshare::InputCutoffClock::time_point, bool
  )
  {
    return false;
  }
  virtual bool beginMappedInputBatch(
      const deskflow::windowshare::InputCutoffBinding &,
      const std::vector<deskflow::windowshare::InputCutoffBatchItem> &
  )
  {
    return false;
  }
  virtual bool inputCutoffSealLive(const deskflow::windowshare::InputCutoffBinding &)
  {
    return false;
  }
  virtual deskflow::windowshare::InputCutoffState inputCutoffState(const deskflow::windowshare::InputCutoffBinding &)
  {
    return deskflow::windowshare::InputCutoffState::Denied;
  }
  virtual bool resumeInputCutoff(const deskflow::windowshare::InputCutoffBinding &)
  {
    return false;
  }
#endif

  //@}
  // IKeyState overrides
  void fakeKeyDown(KeyID id, KeyModifierMask mask, KeyButton button, const std::string &lang) override = 0;
  bool
  fakeKeyRepeat(KeyID id, KeyModifierMask mask, int32_t count, KeyButton button, const std::string &lang) override = 0;
  bool fakeMediaKey(KeyID) override
  {
    return false;
  }

protected:
  //! Handle system event
  /*!
  A platform computer is expected to install a handler for system
  events in its c'tor like so:
  \code
  m_events->addHandler(EventTypes::System,
                        m_events->getSystemTarget(),
                        [this] (const auto &e) {handleSystemEvent(e);});
  \endcode
  It should remove the handler in its d'tor.  Override the
  \c handleSystemEvent() method to process system events.
  It should post the events \c IComputer as appropriate.

  A primary computer has further responsibilities.  It should post
  the events in \c IPrimaryComputer as appropriate.  It should also
  call \c onKey() on its \c KeyState whenever a key is pressed
  or released (but not for key repeats).  And it should call
  \c updateKeyMap() on its \c KeyState if necessary when the keyboard
  mapping changes.

  The target of all events should be the value returned by
  \c getEventTarget().
  */
  virtual void handleSystemEvent(const Event &event) = 0;
};
