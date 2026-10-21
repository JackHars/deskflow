/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once
#include "windowshare/InputCutoff.h"
#include "windowshare/InputPreparation.h"

#include "deskflow/IClient.h"

#include <functional>
#include <optional>

namespace deskflow {
class IStream;
}

//! Generic proxy for client or primary
class BaseClientProxy : public IClient
{
public:
  /*!
  \c name is the name of the client.
  */
  explicit BaseClientProxy(const std::string &name);
  ~BaseClientProxy() override = default;

  //! @name manipulators
  //@{

  //! Save cursor position
  /*!
  Save the position of the cursor when jumping from client.
  */
  void setJumpCursorPos(int32_t x, int32_t y);

  // Core-thread scheduling only. A validated map completion can wake the
  // original-input consumer, but never authorizes delivery by itself.
  void setInputMappingNotifier(std::function<void()> notifier) noexcept;

  //@}
  //! @name accessors
  //@{

  //! Get cursor position
  /*!
  Get the position of the cursor when last jumping from client.
  */
  void getJumpCursorPos(int32_t &x, int32_t &y) const;

  //! Get cursor position
  /*!
  Return if this proxy is for client or primary.
  */
  virtual bool isPrimary() const
  {
    return false;
  }

  // A programmatic opt-in, never a configured computer name or an input grant.
  // Only the current protocol's authenticated remote proxy implements it.
  virtual bool inputPreparationCapabilityReady() const
  {
    return false;
  }
  virtual std::optional<std::uint32_t> requestInputPreparationCapability()
  {
    return std::nullopt;
  }
  virtual std::optional<deskflow::windowshare::InputPreparationBinding> startInputPreparation(
      deskflow::windowshare::OriginalInputStamp, deskflow::windowshare::InputCutoffPoint,
      deskflow::windowshare::InputCutoffClock::time_point
  )
  {
    return std::nullopt;
  }
  virtual std::optional<deskflow::windowshare::PreparedInputFocus>
  preparedInputFocus(const deskflow::windowshare::InputPreparationBinding &)
  {
    return std::nullopt;
  }
  virtual deskflow::windowshare::InputPreparationState
  inputPreparationState(const deskflow::windowshare::InputPreparationBinding &)
  {
    return deskflow::windowshare::InputPreparationState::Failed;
  }
  virtual deskflow::windowshare::InputPreparationOutcome
  inputPreparationOutcome(const deskflow::windowshare::InputPreparationBinding &)
  {
    return deskflow::windowshare::InputPreparationOutcome::Failed;
  }
  virtual std::optional<deskflow::windowshare::InputCutoffBinding> promoteInputPreparation(
      const deskflow::windowshare::PreparedInputFocus &, const deskflow::windowshare::KeyboardRouteBinding &,
      deskflow::windowshare::InputCutoffClock::time_point
  )
  {
    return std::nullopt;
  }
  virtual bool cancelInputPreparation(
      const deskflow::windowshare::InputPreparationBinding &, deskflow::windowshare::InputCutoffClock::time_point
  )
  {
    return false;
  }

  virtual std::optional<std::uint32_t> requestInputSequenceTrace()
  {
    return std::nullopt;
  }
  virtual std::optional<std::uint32_t> requestInputCutoffCapability()
  {
    return std::nullopt;
  }
  virtual std::optional<std::uint32_t> requestMappedInputCutoffCapability()
  {
    return std::nullopt;
  }
  virtual std::optional<std::uint32_t> requestBatchedInputCutoffCapability()
  {
    return std::nullopt;
  }
  virtual std::optional<std::vector<deskflow::windowshare::InputCutoffBinding>> startMappedInputBatch(
      const deskflow::windowshare::InputCutoffSeal &, const std::vector<deskflow::windowshare::InputCutoffMapItem> &
  )
  {
    return std::nullopt;
  }
  virtual std::optional<std::uint32_t> requestSealedInputCutoffCapability()
  {
    return std::nullopt;
  }
  virtual std::optional<deskflow::windowshare::InputCutoffBinding> startSealedInputCutoff(
      deskflow::windowshare::OriginalInputStamp, const deskflow::windowshare::KeyboardRouteBinding &, std::uint64_t,
      deskflow::windowshare::InputCutoffPoint, deskflow::windowshare::InputCutoffClock::time_point
  )
  {
    return std::nullopt;
  }
  virtual std::optional<deskflow::windowshare::InputCutoffBinding> refreshMappedInputCutoff(
      const deskflow::windowshare::InputCutoffSeal &, deskflow::windowshare::OriginalInputStamp,
      deskflow::windowshare::InputCutoffPoint, deskflow::windowshare::InputCutoffClock::time_point, bool
  )
  {
    return std::nullopt;
  }
  virtual std::optional<deskflow::windowshare::InputCutoffSeal>
  inputCutoffSeal(const deskflow::windowshare::InputCutoffBinding &)
  {
    return std::nullopt;
  }
  virtual std::optional<deskflow::windowshare::InputCutoffBinding> startMappedInputCutoff(
      deskflow::windowshare::OriginalInputStamp, const deskflow::windowshare::KeyboardRouteBinding &, std::uint64_t,
      deskflow::windowshare::InputCutoffPoint, deskflow::windowshare::InputCutoffClock::time_point
  )
  {
    return std::nullopt;
  }
  virtual std::optional<deskflow::windowshare::InputCutoffObservation>
  inputCutoffObservation(const deskflow::windowshare::InputCutoffBinding &)
  {
    return std::nullopt;
  }
  virtual std::optional<deskflow::windowshare::InputCutoffBinding> startInputCutoff(
      deskflow::windowshare::OriginalInputStamp, const deskflow::windowshare::KeyboardRouteBinding &, std::uint64_t,
      deskflow::windowshare::InputCutoffClock::time_point
  )
  {
    return std::nullopt;
  }
  virtual deskflow::windowshare::InputCutoffState inputCutoffState(const deskflow::windowshare::InputCutoffBinding &)
  {
    return deskflow::windowshare::InputCutoffState::Denied;
  }
  virtual bool resumeInputCutoff(const deskflow::windowshare::InputCutoffBinding &)
  {
    return false;
  }

  //@}

  // IComputer
  void *getEventTarget() const override = 0;
  bool getClipboard(ClipboardID id, IClipboard *) const override = 0;
  void getShape(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const override = 0;
  void getCursorPos(int32_t &x, int32_t &y) const override = 0;

  // IClient overrides
  void enter(int32_t xAbs, int32_t yAbs, uint32_t seqNum, KeyModifierMask mask, bool forScreensaver) override = 0;
  bool leave() override = 0;
  void setClipboard(ClipboardID, const IClipboard *) override = 0;
  void grabClipboard(ClipboardID) override = 0;
  void setClipboardDirty(ClipboardID, bool) override = 0;
  void keyDown(KeyID, KeyModifierMask, KeyButton, const std::string &) override = 0;
  void keyRepeat(KeyID, KeyModifierMask, int32_t count, KeyButton, const std::string &lang) override = 0;
  void keyUp(KeyID, KeyModifierMask, KeyButton) override = 0;
  void mouseDown(ButtonID) override = 0;
  void mouseUp(ButtonID) override = 0;
  void mouseMove(int32_t xAbs, int32_t yAbs) override = 0;
  void mouseRelativeMove(int32_t xRel, int32_t yRel) override = 0;
  void mouseWheel(int32_t xDelta, int32_t yDelta) override = 0;
  void screensaver(bool activate) override = 0;
  void resetOptions() override = 0;
  void setOptions(const OptionsList &options) override = 0;
  virtual void sendDragInfo(uint32_t fileCount, const char *info, size_t size) = 0;
  virtual void fileChunkSending(uint8_t mark, char *data, size_t dataSize) = 0;
  virtual std::string getSecureInputApp() const = 0;
  virtual void secureInputNotification(const std::string &app) const = 0;
  std::string getName() const override;
  virtual deskflow::IStream *getStream() const = 0;

protected:
  void notifyInputMappingProgress() const;

private:
  std::function<void()> m_inputMappingNotifier;
  std::string m_name;
  int32_t m_x = 0;
  int32_t m_y = 0;
};
