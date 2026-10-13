/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/InputCutoffProtocol.h"
#include "deskflow/InputSequence.h"
#include "io/AuthenticatedPeerTicket.h"
#include "server/BaseClientProxy.h"

#include <array>
#include <functional>
#include <memory>

namespace deskflow {
class IStream;
}

//! Generic proxy for client
class ClientProxy : public BaseClientProxy
{
public:
  /*!
  \c name is the name of the client.
  */
  ClientProxy(const std::string &name, deskflow::IStream *adoptedStream);
  ClientProxy(ClientProxy const &) = delete;
  ClientProxy(ClientProxy &&) = delete;
  ~ClientProxy() override;

  ClientProxy &operator=(ClientProxy const &) = delete;
  ClientProxy &operator=(ClientProxy &&) = delete;

  //! @name manipulators
  //@{

  //! Disconnect
  /*!
  Ask the client to disconnect, using \p msg as the reason.
  */
  void close(const char *msg);

  //@}
  //! @name accessors
  //@{

  //! Get stream
  /*!
  Returns the original stream passed to the c'tor.
  */
  deskflow::IStream *getStream() const override;

  std::optional<std::uint32_t> requestInputSequenceTrace() override;
  std::optional<std::uint32_t> requestInputCutoffCapability() override;
  // Event-thread, one bounded transaction. Existing physical owners must be
  // released first. Later legacy/control writes revoke this observation and
  // send exact Resume before their bytes; fallback waits for exact Resumed.
  std::optional<deskflow::windowshare::InputCutoffBinding> startInputCutoff(
      deskflow::windowshare::OriginalInputStamp, const deskflow::windowshare::KeyboardRouteBinding &, std::uint64_t,
      deskflow::windowshare::InputCutoffClock::time_point
  ) override;
  deskflow::windowshare::InputCutoffState inputCutoffState(const deskflow::windowshare::InputCutoffBinding &) override;
  bool resumeInputCutoff(const deskflow::windowshare::InputCutoffBinding &) override;

  struct InputSequenceStatistics
  {
    std::uint64_t enveloped = 0, receipts = 0, retiredReceipts = 0, unavailable = 0;
    std::size_t outstanding = 0;
  };
  InputSequenceStatistics inputSequenceStatistics() const noexcept;

  // Event-thread callback with the exact retained capture token. A receipt
  // describes backend API submission only, not application delivery. The
  // pending record is removed before calling out; reset/close/destruction from
  // the callback is supported. Retired-generation receipts do not call out.
  std::function<
      void(const deskflow::windowshare::InputSequenceToken &, const deskflow::windowshare::InputSequenceReceipt &)>
      onInputSequenceReceipt;

  //@}

  // IComputer
  void *getEventTarget() const override;
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
  void sendDragInfo(uint32_t fileCount, const char *info, size_t size) override = 0;
  void fileChunkSending(uint8_t mark, char *data, size_t dataSize) override = 0;
  void secureInputNotification(const std::string &app) const override = 0;

protected:
  virtual bool supportsInputSequenceTrace() const noexcept
  {
    return false;
  }
  deskflow::IStream *inputStream() const noexcept;
  OptionsList prepareInputSequenceOptions(const OptionsList &options);
  void retireInputSequenceTrace() noexcept;
  void stopInputSequenceTrace() noexcept;
  void receiveInputSequenceAck(std::uint32_t generation);
  void receiveInputSequenceReceipt(const deskflow::windowshare::InputSequenceReceipt &receipt);
  void receiveInputCutoffAck(std::uint32_t);
  void receiveInputCutoffReceipt(const deskflow::windowshare::InputCutoffReceipt &);
  void retireInputCutoffCapability();
  std::shared_ptr<const bool> proxyLifetime() const noexcept
  {
    return m_alive;
  }

private:
  friend class ClientProxyTests;
  class InputStream;
  void writeInputPacket(const void *buffer, std::uint32_t size);
  void writeControlPacket(const void *buffer, std::uint32_t size);
  bool inputSequenceTlsLive() const;
  bool inputCutoffTlsLive() const;

  deskflow::IStream *m_stream;
  std::unique_ptr<InputStream> m_inputStream;
  std::unique_ptr<InputStream> m_controlStream;
  std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
  std::optional<deskflow::AuthenticatedPeerTicket> m_inputSequenceTicket;
  std::uint32_t m_inputSequenceIssued = 0, m_inputSequenceRequested = 0, m_inputSequenceGeneration = 0;
  std::uint64_t m_inputSequenceWire = 0;
  bool m_inputSequenceAcknowledged = false, m_inputSequenceStopped = false;
  std::array<
      std::optional<deskflow::windowshare::InputSequenceToken>, deskflow::windowshare::inputSequenceMaximumOutstanding>
      m_inputSequencePending;
  InputSequenceStatistics m_inputSequenceStatistics;
  struct Cutoff
  {
    deskflow::windowshare::InputCutoffBinding binding;
    deskflow::windowshare::InputCutoffClock::time_point deadline;
    deskflow::windowshare::InputCutoffState state = deskflow::windowshare::InputCutoffState::Pending;
    bool resuming = false;
  };
  std::optional<Cutoff> m_cutoff;
  std::optional<deskflow::windowshare::InputCutoffBinding> m_retiredCutoff;
  std::optional<deskflow::AuthenticatedPeerTicket> m_cutoffTicket;
  std::uint32_t m_cutoffGeneration = 0, m_cutoffRequested = 0, m_cutoffIssued = 0;
  bool m_cutoffAcknowledged = false;
};
