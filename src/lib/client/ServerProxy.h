/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016, 2026 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/KeyTypes.h"

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
#include "deskflow/OptionTypes.h"
#include "io/AuthenticatedPeerTicket.h"
#endif

class Client;
class ClientInfo;
class EventQueueTimer;
class IClipboard;
namespace deskflow {
class IStream;
}
class IEventQueue;

//! Proxy for server implementing protocol version 1.6
/*!
This class acts a proxy for the server, converting calls into messages
to the server and messages from the server to calls on the client.
It implements 1.6, the oldest version the client supports; later
versions subclass it and add their messages.
*/
class ServerProxy
{
public:
  /*!
  Process messages from the server on \p stream and forward to
  \p client.
  */
  ServerProxy(Client *client, deskflow::IStream *stream, IEventQueue *events);
  ServerProxy(ServerProxy const &) = delete;
  ServerProxy(ServerProxy &&) = delete;
  virtual ~ServerProxy();

  ServerProxy &operator=(ServerProxy const &) = delete;
  ServerProxy &operator=(ServerProxy &&) = delete;

  //! @name manipulators
  //@{

  void onInfoChanged();
  bool onGrabClipboard(ClipboardID);
  void onClipboardChanged(ClipboardID, const IClipboard *);

  //@}

protected:
  enum class ConnectionResult
  {
    Okay,
    Unknown,
    Disconnect
  };

  virtual ConnectionResult parseHandshakeMessage(const uint8_t *code);
  virtual ConnectionResult parseMessage(const uint8_t *code);
  void keyDown(uint16_t id, uint16_t mask, uint16_t button, const std::string &lang);
  void keyRepeat(uint16_t id, uint16_t mask, uint16_t count, uint16_t button, const std::string &lang);

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  virtual bool supportsInputSequence() const
  {
    return false;
  }
#endif

  deskflow::IStream *getStream() const
  {
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    if (m_sequencePayloadStream)
      return m_sequencePayloadStream;
#endif
    return m_stream;
  }

private:
  // if compressing mouse motion then send the last motion now
  void flushCompressedMouse();

  void sendInfo(const ClientInfo &);

  void resetKeepAliveAlarm();
  void setKeepAliveRate(double);

  // modifier key translation
  KeyID translateKey(KeyID) const;
  KeyModifierMask translateModifierMask(KeyModifierMask) const;

  // event handlers
  void handleData();
  ConnectionResult handleMessage(const uint8_t *code);
  void handleKeepAliveAlarm();
  void requestDisconnect(const char *message);
  void requestRefuseConnection(deskflow::core::ConnectionRefusal reason, const char *message);

  // message handlers
  void enter();
  void leave();
  void setClipboard();
  void grabClipboard();
  void keyUp();
  void mouseDown();
  void mouseUp();
  void mouseMove();
  void mouseRelativeMove();
  void mouseWheel();
  void screensaver();
  void resetOptions();
  void setOptions();
  void queryInfo();
  void infoAcknowledgment();

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  void inputSequence();
  void setInputSequenceOptions(const OptionsList &options);
  void revokeInputSequence();
  bool inputSequenceConnectionLive() const;
#endif

private:
  using MessageParser = ConnectionResult (ServerProxy::*)(const uint8_t *);

  Client *m_client = nullptr;
  deskflow::IStream *m_stream = nullptr;

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  deskflow::IStream *m_sequencePayloadStream = nullptr;
  std::optional<deskflow::AuthenticatedPeerTicket> m_sequencePeer;
  uint32_t m_sequenceGeneration = 0;
  uint32_t m_sequenceGenerationHighWater = 0;
  uint64_t m_sequenceNext = 0;
  uint32_t m_messageBytes = 0;
  bool m_sequenceDispatching = false;
  bool m_sequenceProtocolFailed = false;
#endif

  uint32_t m_seqNum = 0;

  bool m_compressMouse = false;
  bool m_compressMouseRelative = false;
  int32_t m_xMouse = 0;
  int32_t m_yMouse = 0;
  int32_t m_dxMouse = 0;
  int32_t m_dyMouse = 0;

  bool m_ignoreMouse = false;

  KeyModifierID m_modifierTranslationTable[kKeyModifierIDLast];

  double m_keepAliveAlarm = 0.0;
  EventQueueTimer *m_keepAliveAlarmTimer = nullptr;

  MessageParser m_parser = &ServerProxy::parseHandshakeMessage;
  IEventQueue *m_events = nullptr;
  std::string m_clipboardDataCached;
  ClipboardChunkAssemblyState m_clipboardChunkState;
};
