/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_0.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

#include <cstring>

//
// ClientProxy1_0
//

ClientProxy1_0::ClientProxy1_0(const std::string &name, deskflow::IStream *stream, IEventQueue *events)
    : ClientProxy(name, stream),
      m_events(events)
{
  // install event handlers
  m_events->addHandler(EventTypes::StreamInputReady, stream->getEventTarget(), [this](const auto &) { handleData(); });
  m_events->addHandler(EventTypes::StreamOutputError, stream->getEventTarget(), [this](const auto &) {
    handleWriteError();
  });
  m_events->addHandler(EventTypes::StreamInputShutdown, stream->getEventTarget(), [this](const auto &) {
    handleDisconnect();
  });
  m_events->addHandler(EventTypes::StreamInputFormatError, stream->getEventTarget(), [this](const auto &) {
    handleDisconnect();
  });
  m_events->addHandler(EventTypes::StreamOutputShutdown, stream->getEventTarget(), [this](const auto &) {
    handleWriteError();
  });
  m_events->addHandler(EventTypes::Timer, this, [this](const auto &) { handleFlatline(); });

  setHeartbeatRate(kHeartRate, kHeartRate * kHeartBeatsUntilDeath);

  LOG_VERBOSE("querying client \"%s\" info", getName().c_str());
  ProtocolUtil::writef(getStream(), kMsgQInfo);
}

ClientProxy1_0::~ClientProxy1_0()
{
  removeHandlers();
}

void ClientProxy1_0::disconnect()
{
  if (m_disconnected)
    return;
  m_disconnected = true;
  stopInputSequenceTrace();
  removeHandlers();
  getStream()->close();
  m_events->addEvent(Event(EventTypes::ClientProxyDisconnected, getEventTarget()));
}

void ClientProxy1_0::removeHandlers()
{
  using enum EventTypes;
  // uninstall event handlers
  m_events->removeHandler(StreamInputReady, getStream()->getEventTarget());
  m_events->removeHandler(StreamOutputError, getStream()->getEventTarget());
  m_events->removeHandler(StreamInputShutdown, getStream()->getEventTarget());
  m_events->removeHandler(StreamOutputShutdown, getStream()->getEventTarget());
  m_events->removeHandler(StreamInputFormatError, getStream()->getEventTarget());
  m_events->removeHandler(Timer, this);

  // remove timer
  removeHeartbeatTimer();
}

void ClientProxy1_0::addHeartbeatTimer()
{
  if (m_heartbeatAlarm > 0.0) {
    m_heartbeatTimer = m_events->newOneShotTimer(m_heartbeatAlarm, this);
  }
}

void ClientProxy1_0::removeHeartbeatTimer()
{
  if (m_heartbeatTimer != nullptr) {
    m_events->deleteTimer(m_heartbeatTimer);
    m_heartbeatTimer = nullptr;
  }
}

void ClientProxy1_0::resetHeartbeatTimer()
{
  // reset the alarm
  removeHeartbeatTimer();
  addHeartbeatTimer();
}

void ClientProxy1_0::resetHeartbeatRate()
{
  setHeartbeatRate(kHeartRate, kHeartRate * kHeartBeatsUntilDeath);
}

void ClientProxy1_0::setHeartbeatRate(double, double alarm)
{
  m_heartbeatAlarm = alarm;
}

void ClientProxy1_0::handleData()
{
  if (m_disconnected)
    return;
  const auto alive = proxyLifetime();
  // handle messages until there are no more.  first read message code.
  uint8_t code[4];
  m_messagePacketSize = getStream()->getSize();
  uint32_t n = getStream()->read(code, 4);
  while (n != 0) {
    // verify we got an entire code
    if (n != 4) {
      LOG_ERR("incomplete message from \"%s\": %d bytes", getName().c_str(), n);
      disconnect();
      return;
    }

    // parse message
    try {
      LOG_VERBOSE("msg from \"%s\": %c%c%c%c", getName().c_str(), code[0], code[1], code[2], code[3]);
      const bool accepted = (this->*m_parser)(code);
      if (!*alive || m_disconnected)
        return;
      if (!accepted) {
        LOG(
            (CLOG_ERR "invalid message from client \"%s\": %c%c%c%c", getName().c_str(), code[0], code[1], code[2],
             code[3])
        );
        // not possible to determine message boundaries
        // read the whole stream to discard unkonwn data
        while (getStream()->read(nullptr, 4))
          ;
      }
    } catch (const BadClientException &e) {
      if (!*alive)
        return;
      LOG_ERR("protocol error from client \"%s\": %s", getName().c_str(), e.what());
      disconnect();
      return;
    }

    // next message
    m_messagePacketSize = getStream()->getSize();
    n = getStream()->read(code, 4);
  }

  // restart heartbeat timer
  resetHeartbeatTimer();
}

bool ClientProxy1_0::parseHandshakeMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgCInputSequenceAck, 4) == 0 || memcmp(code, kMsgCInputSequenceReceipt, 4) == 0)
    throw BadClientException("Input sequence reply before client handshake");
  if (memcmp(code, kMsgCNoop, 4) == 0) {
    // discard no-ops
    LOG_VERBOSE("no-op from", getName().c_str());
    return true;
  } else if (memcmp(code, kMsgDInfo, 4) == 0) {
    // future messages get parsed by parseMessage
    m_parser = &ClientProxy1_0::parseMessage;
    if (recvInfo()) {
      m_events->addEvent(Event(EventTypes::ClientProxyReady, getEventTarget()));
      addHeartbeatTimer();
      return true;
    }
  }
  return false;
}

bool ClientProxy1_0::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgCInputPreparationAck, 4) == 0) {
    if (m_messagePacketSize != 8)
      throw BadClientException("Incorrect preparation ack size");
    std::array<char, 4> body{};
    if (getStream()->read(body.data(), body.size()) != body.size())
      throw BadClientException("Incomplete preparation ack");
    std::size_t offset = 0;
    receiveInputPreparationAck(
        static_cast<std::uint32_t>(
            deskflow::windowshare::inputSequenceDetail::read(std::string_view(body.data(), body.size()), offset, 4)
        )
    );
    return true;
  }
  if (memcmp(code, kMsgCPreparedInputFocus, 4) == 0 || memcmp(code, kMsgCInputPreparationReceipt, 4) == 0) {
    using namespace deskflow::windowshare;
    const bool focus = memcmp(code, kMsgCPreparedInputFocus, 4) == 0;
    const auto size = focus ? inputPreparationResultBytes : inputPreparationReceiptBytes;
    if (m_messagePacketSize != size + 4)
      throw BadClientException("Incorrect preparation reply size");
    std::string body(size, '\0');
    if (getStream()->read(body.data(), body.size()) != body.size())
      throw BadClientException("Incomplete preparation reply");
    if (focus) {
      const auto value = decodePreparedInputFocusResult(body);
      if (!value)
        throw BadClientException("Malformed prepared focus");
      receivePreparedInputFocus(*value);
    } else {
      const auto value = decodeInputPreparationReceipt(body);
      if (!value)
        throw BadClientException("Malformed preparation receipt");
      receiveInputPreparationReceipt(*value);
    }
    return true;
  }
  if (memcmp(code, kMsgCInputCutoffAck, 4) == 0 || memcmp(code, kMsgCMappedInputCutoffAck, 4) == 0 ||
      memcmp(code, kMsgCSealedInputCutoffAck, 4) == 0 || memcmp(code, kMsgCBatchedInputCutoffAck, 4) == 0) {
    if (m_messagePacketSize != 8)
      throw BadClientException("Incorrect input cutoff acknowledgement size");
    std::array<char, 4> body{};
    if (getStream()->read(body.data(), body.size()) != body.size())
      throw BadClientException("Incomplete input cutoff acknowledgement");
    std::size_t offset = 0;
    receiveInputCutoffAck(
        static_cast<std::uint32_t>(
            deskflow::windowshare::inputSequenceDetail::read(std::string_view(body.data(), body.size()), offset, 4)
        ),
        memcmp(code, kMsgCInputCutoffAck, 4) != 0,
        memcmp(code, kMsgCSealedInputCutoffAck, 4) == 0 || memcmp(code, kMsgCBatchedInputCutoffAck, 4) == 0,
        memcmp(code, kMsgCBatchedInputCutoffAck, 4) == 0
    );
    return true;
  } else if (memcmp(code, kMsgCInputCutoffBatchResult, 4) == 0) {
    using namespace deskflow::windowshare;
    if (m_messagePacketSize < 4 + inputCutoffBatchHeaderBytes ||
        m_messagePacketSize > 4 + inputCutoffBatchResultMaximumBytes)
      throw BadClientException("Incorrect batch cutoff result size");
    std::string body(m_messagePacketSize - 4, '\0');
    if (getStream()->read(body.data(), body.size()) != body.size())
      throw BadClientException("Incomplete batch cutoff result");
    const auto result = decodeInputCutoffBatchResult(body);
    if (!result)
      throw BadClientException("Malformed batch cutoff result");
    receiveInputCutoffBatchResult(*result);
    return true;
  } else if (memcmp(code, kMsgCSealedInputCutoffResult, 4) == 0) {
    using namespace deskflow::windowshare;
    if (m_messagePacketSize != 4 + inputCutoffSealedResultBytes)
      throw BadClientException("Incorrect sealed cutoff result size");
    std::array<char, inputCutoffSealedResultBytes> body{};
    if (getStream()->read(body.data(), body.size()) != body.size())
      throw BadClientException("Incomplete sealed cutoff result");
    const auto result = decodeInputCutoffSealedResult(std::string_view(body.data(), body.size()));
    if (!result)
      throw BadClientException("Malformed sealed cutoff result");
    receiveSealedInputCutoffResult(*result);
    return true;
  } else if (memcmp(code, kMsgCMappedInputCutoffResult, 4) == 0) {
    using namespace deskflow::windowshare;
    if (m_messagePacketSize != 4 + inputCutoffMappedResultBytes)
      throw BadClientException("Incorrect mapped cutoff result size");
    std::array<char, inputCutoffMappedResultBytes> body{};
    if (getStream()->read(body.data(), body.size()) != body.size())
      throw BadClientException("Incomplete mapped cutoff result");
    const auto result = decodeInputCutoffMappedResult(std::string_view(body.data(), body.size()));
    if (!result)
      throw BadClientException("Malformed mapped cutoff result");
    receiveMappedInputCutoffResult(*result);
    return true;
  } else if (memcmp(code, kMsgCInputCutoffReceipt, 4) == 0) {
    using namespace deskflow::windowshare;
    if (m_messagePacketSize != 4 + inputCutoffReceiptBytes)
      throw BadClientException("Incorrect input cutoff receipt size");
    std::array<char, inputCutoffReceiptBytes> body{};
    if (getStream()->read(body.data(), body.size()) != body.size())
      throw BadClientException("Incomplete input cutoff receipt");
    const auto receipt = decodeInputCutoffReceipt(std::string_view(body.data(), body.size()));
    if (!receipt)
      throw BadClientException("Malformed input cutoff receipt");
    receiveInputCutoffReceipt(*receipt);
    return true;
  } else if (memcmp(code, kMsgCInputSequenceAck, 4) == 0) {
    // Save packet size before reading its code: after its last byte the packet
    // filter may already expose the NEXT packet's size. Never borrow its body.
    if (m_messagePacketSize != 8)
      throw BadClientException("Incorrect input sequence acknowledgement packet size");
    std::array<char, 4> body{};
    if (getStream()->read(body.data(), body.size()) != body.size())
      throw BadClientException("Incomplete input sequence acknowledgement");
    std::size_t offset = 0;
    receiveInputSequenceAck(
        static_cast<std::uint32_t>(
            deskflow::windowshare::inputSequenceDetail::read(std::string_view(body.data(), body.size()), offset, 4)
        )
    );
    return true;
  } else if (memcmp(code, kMsgCInputSequenceReceipt, 4) == 0) {
    using namespace deskflow::windowshare;
    if (m_messagePacketSize != 4 + inputSequenceReceiptBodyBytes)
      throw BadClientException("Incorrect input sequence receipt packet size");
    std::array<char, inputSequenceReceiptBodyBytes> body{};
    if (getStream()->read(body.data(), body.size()) != body.size())
      throw BadClientException("Incomplete input sequence receipt");
    const auto receipt = decodeInputSequenceReceipt(std::string_view(body.data(), body.size()));
    if (!receipt)
      throw BadClientException("Malformed input sequence receipt");
    receiveInputSequenceReceipt(*receipt);
    return true;
  } else if (memcmp(code, kMsgDInfo, 4) == 0) {
    if (recvInfo()) {
      m_events->addEvent(Event(EventTypes::ComputerShapeChanged, getEventTarget()));
      return true;
    }
    return false;
  } else if (memcmp(code, kMsgCNoop, 4) == 0) {
    // discard no-ops
    LOG_VERBOSE("no-op from", getName().c_str());
    return true;
  } else if (memcmp(code, kMsgCClipboard, 4) == 0) {
    return recvGrabClipboard();
  } else if (memcmp(code, kMsgDClipboard, 4) == 0) {
    return recvClipboard();
  }
  return false;
}

void ClientProxy1_0::handleDisconnect()
{
  LOG_DEBUG("client \"%s\" has disconnected", getName().c_str());
  disconnect();
}

void ClientProxy1_0::handleWriteError()
{
  LOG_WARN("error writing to client \"%s\"", getName().c_str());
  disconnect();
}

void ClientProxy1_0::handleFlatline()
{
  // didn't get a heartbeat fast enough.  assume client is dead.
  LOG_DEBUG("client \"%s\" is dead", getName().c_str());
  disconnect();
}

bool ClientProxy1_0::getClipboard(ClipboardID id, IClipboard *clipboard) const
{
  Clipboard::copy(clipboard, &m_clipboard[id].m_clipboard);
  return true;
}

void ClientProxy1_0::getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const
{
  x = m_info.m_x;
  y = m_info.m_y;
  w = m_info.m_w;
  h = m_info.m_h;
}

void ClientProxy1_0::getCursorPos(int32_t &x, int32_t &y) const
{
  // note -- this returns the cursor pos from when we last got client info
  x = m_info.m_mx;
  y = m_info.m_my;
}

void ClientProxy1_0::enter(int32_t xAbs, int32_t yAbs, uint32_t seqNum, KeyModifierMask mask, bool)
{
  LOG_VERBOSE("send enter to \"%s\", %d,%d %d %04x", getName().c_str(), xAbs, yAbs, seqNum, mask);
  ProtocolUtil::writef(getStream(), kMsgCEnter, xAbs, yAbs, seqNum, mask);
}

bool ClientProxy1_0::leave()
{
  LOG_VERBOSE("send leave to \"%s\"", getName().c_str());
  ProtocolUtil::writef(getStream(), kMsgCLeave);

  // we can never prevent the user from leaving
  return true;
}

void ClientProxy1_0::setClipboard(ClipboardID id, const IClipboard *clipboard)
{
  // ignore -- deprecated in protocol 1.0
}

void ClientProxy1_0::grabClipboard(ClipboardID id)
{
  LOG_DEBUG("send grab clipboard %d to \"%s\"", id, getName().c_str());
  ProtocolUtil::writef(getStream(), kMsgCClipboard, id, 0);

  // this clipboard is now dirty
  m_clipboard[id].m_dirty = true;
}

void ClientProxy1_0::setClipboardDirty(ClipboardID id, bool dirty)
{
  m_clipboard[id].m_dirty = dirty;
}

void ClientProxy1_0::keyDown(KeyID key, KeyModifierMask mask, KeyButton, const std::string &)
{
  LOG_VERBOSE("send key down to \"%s\" id=%d, mask=0x%04x", getName().c_str(), key, mask);
  ProtocolUtil::writef(inputStream(), kMsgDKeyDown1_0, key, mask);
}

void ClientProxy1_0::keyRepeat(KeyID key, KeyModifierMask mask, int32_t count, KeyButton, const std::string &)
{
  LOG_VERBOSE("send key repeat to \"%s\" id=%d, mask=0x%04x, count=%d", getName().c_str(), key, mask, count);
  ProtocolUtil::writef(inputStream(), kMsgDKeyRepeat1_0, key, mask, count);
}

void ClientProxy1_0::keyUp(KeyID key, KeyModifierMask mask, KeyButton)
{
  LOG_VERBOSE("send key up to \"%s\" id=%d, mask=0x%04x", getName().c_str(), key, mask);
  ProtocolUtil::writef(inputStream(), kMsgDKeyUp1_0, key, mask);
}

void ClientProxy1_0::mouseDown(ButtonID button)
{
  LOG_VERBOSE("send mouse down to \"%s\" id=%d", getName().c_str(), button);
  ProtocolUtil::writef(inputStream(), kMsgDMouseDown, button);
}

void ClientProxy1_0::mouseUp(ButtonID button)
{
  LOG_VERBOSE("send mouse up to \"%s\" id=%d", getName().c_str(), button);
  ProtocolUtil::writef(inputStream(), kMsgDMouseUp, button);
}

void ClientProxy1_0::mouseMove(int32_t xAbs, int32_t yAbs)
{
  LOG_VERBOSE("send mouse move to \"%s\" %d,%d", getName().c_str(), xAbs, yAbs);
  ProtocolUtil::writef(inputStream(), kMsgDMouseMove, xAbs, yAbs);
}

void ClientProxy1_0::mouseRelativeMove(int32_t, int32_t)
{
  // ignore -- not supported in protocol 1.0
}

void ClientProxy1_0::mouseWheel(int32_t, int32_t yDelta)
{
  // clients prior to 1.3 only support the y axis
  LOG_VERBOSE("send mouse wheel to \"%s\" %+d", getName().c_str(), yDelta);
  ProtocolUtil::writef(inputStream(), kMsgDMouseWheel1_0, yDelta);
}

void ClientProxy1_0::sendDragInfo(uint32_t, const char *, size_t)
{
  // ignore -- not supported in protocol 1.0
  LOG_DEBUG("draggingInfoSending not supported");
}

void ClientProxy1_0::fileChunkSending(uint8_t, char *, size_t)
{
  // ignore -- not supported in protocol 1.0
  LOG_DEBUG("fileChunkSending not supported");
}

std::string ClientProxy1_0::getSecureInputApp() const
{
  // ignore -- not supported on clients
  LOG_DEBUG("getSecureInputApp not supported");
  return "";
}

void ClientProxy1_0::secureInputNotification(const std::string &) const
{
  // ignore -- not supported in protocol 1.0
  LOG_DEBUG("secureInputNotification not supported");
}

void ClientProxy1_0::screensaver(bool on)
{
  LOG_VERBOSE("send screen saver to \"%s\" on=%d", getName().c_str(), on ? 1 : 0);
  ProtocolUtil::writef(getStream(), kMsgCScreenSaver, on ? 1 : 0);
}

void ClientProxy1_0::resetOptions()
{
  const auto alive = proxyLifetime();
  retireInputCutoffCapability();
  if (!*alive)
    return;
  retireInputSequenceTrace();
  LOG_VERBOSE("send reset options to \"%s\"", getName().c_str());
  ProtocolUtil::writef(getStream(), kMsgCResetOptions);
  if (!*alive)
    return;

  // reset heart rate and death
  resetHeartbeatRate();
  removeHeartbeatTimer();
  addHeartbeatTimer();
}

void ClientProxy1_0::setOptions(const OptionsList &options)
{
  const auto alive = proxyLifetime();
  LOG_VERBOSE("send set options to \"%s\" size=%d", getName().c_str(), options.size());
  if (options.size() % 2 != 0) {
    LOG_ERR("options are the incorrect size, not sending");
    return;
  }

  const auto outgoing = prepareInputSequenceOptions(options);
  if (!*alive)
    return;
  ProtocolUtil::writef(getStream(), kMsgDSetOptions, &outgoing);
  if (!*alive)
    return;

  // check options
  for (uint32_t i = 0, n = (uint32_t)options.size(); i < n; i += 2) {
    if (options[i] == kOptionHeartbeat) {
      double rate = 1.0e-3 * static_cast<double>(options[i + 1]);
      if (rate <= 0.0) {
        rate = -1.0;
      }
      setHeartbeatRate(rate, rate * kHeartBeatsUntilDeath);
      removeHeartbeatTimer();
      addHeartbeatTimer();
    }
  }
}

bool ClientProxy1_0::recvInfo()
{
  // parse the message
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  int16_t dummy1;
  int16_t mx;
  int16_t my;
  if (!ProtocolUtil::readf(getStream(), kMsgDInfo + 4, &x, &y, &w, &h, &dummy1, &mx, &my)) {
    return false;
  }
  LOG_DEBUG("received client \"%s\" info shape=%d,%d %dx%d at %d,%d", getName().c_str(), x, y, w, h, mx, my);

  // validate
  if (w <= 0 || h <= 0) {
    return false;
  }
  if (mx < x || mx >= x + w || my < y || my >= y + h) {
    mx = x + w / 2;
    my = y + h / 2;
  }

  // save
  m_info.m_x = x;
  m_info.m_y = y;
  m_info.m_w = w;
  m_info.m_h = h;
  m_info.m_mx = mx;
  m_info.m_my = my;

  // acknowledge receipt
  LOG_VERBOSE("send info ack to \"%s\"", getName().c_str());
  ProtocolUtil::writef(getStream(), kMsgCInfoAck);
  return true;
}

bool ClientProxy1_0::recvClipboard()
{
  // deprecated in protocol 1.0
  return false;
}

bool ClientProxy1_0::recvGrabClipboard()
{
  // parse message
  ClipboardID id;
  uint32_t seqNum;
  if (!ProtocolUtil::readf(getStream(), kMsgCClipboard + 4, &id, &seqNum)) {
    return false;
  }
  LOG_DEBUG("received client \"%s\" grabbed clipboard %d seqnum=%d", getName().c_str(), id, seqNum);

  // validate
  if (id >= kClipboardEnd) {
    return false;
  }

  // notify
  auto *info = new ClipboardInfo;
  info->m_id = id;
  info->m_sequenceNumber = seqNum;
  m_events->addEvent(Event(EventTypes::ClipboardGrabbed, getEventTarget(), info));

  return true;
}
