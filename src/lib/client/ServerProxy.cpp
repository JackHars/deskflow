/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016, 2026 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerProxy.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "client/Client.h"
#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/OptionTypes.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/StreamChunker.h"
#include "io/IStream.h"

#include <cstring>

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
#include "deskflow/InputSequence.h"
#include <QScopeGuard>
#include <array>
#include <limits>

namespace {

// One already validated legacy message. Reads never cross into the transport
// or a following message, and the existing version-specific parsers remain the
// only path that translates and executes input. Output always uses m_stream.
class InputSequencePayloadStream final : public deskflow::IStream
{
public:
  explicit InputSequencePayloadStream(std::string_view payload) : m_payload(payload)
  {
  }
  uint32_t read(void *buffer, uint32_t size) override
  {
    const auto count = std::min<std::size_t>(size, m_payload.size());
    if (buffer && count)
      std::memcpy(buffer, m_payload.data(), count);
    m_payload.remove_prefix(count);
    return static_cast<uint32_t>(count);
  }
  bool isReady() const override
  {
    return !m_payload.empty();
  }
  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(m_payload.size());
  }
  void *getEventTarget() const override
  {
    return nullptr;
  }
  void close() override
  {
    m_payload = {};
  }
  void write(const void *, uint32_t) override
  {
    throw BadClientException("Unexpected output to input sequence payload");
  }
  void flush() override
  {
  }
  void shutdownInput() override
  {
    close();
  }
  void shutdownOutput() override
  {
  }

private:
  std::string_view m_payload;
};

} // namespace
#endif

//
// ServerProxy
//

ServerProxy::ServerProxy(Client *client, deskflow::IStream *stream, IEventQueue *events)
    : m_client(client),
      m_stream(stream),
      m_events(events)
{
  assert(m_client != nullptr);
  assert(m_stream != nullptr);

  // initialize modifier translation table
  for (KeyModifierID id = 0; id < kKeyModifierIDLast; ++id)
    m_modifierTranslationTable[id] = id;

  // handle data on stream
  m_events->addHandler(EventTypes::StreamInputReady, m_stream->getEventTarget(), [this](const auto &) {
    handleData();
  });
  m_events->addHandler(EventTypes::ClipboardSending, this, [this](const auto &e) {
    ClipboardChunk::send(m_stream, e.getDataObject());
  });

  // send heartbeat
  setKeepAliveRate(kKeepAliveRate);
}

ServerProxy::~ServerProxy()
{
  setKeepAliveRate(-1.0);
  m_events->removeHandler(EventTypes::StreamInputReady, m_stream->getEventTarget());
  m_events->removeHandler(EventTypes::ClipboardSending, this);
}

void ServerProxy::resetKeepAliveAlarm()
{
  if (m_keepAliveAlarmTimer != nullptr) {
    m_events->removeHandler(EventTypes::Timer, m_keepAliveAlarmTimer);
    m_events->deleteTimer(m_keepAliveAlarmTimer);
    m_keepAliveAlarmTimer = nullptr;
  }
  if (m_keepAliveAlarm > 0.0) {
    m_keepAliveAlarmTimer = m_events->newOneShotTimer(m_keepAliveAlarm, nullptr);
    m_events->addHandler(EventTypes::Timer, m_keepAliveAlarmTimer, [this](const auto &) { handleKeepAliveAlarm(); });
  }
}

void ServerProxy::setKeepAliveRate(double rate)
{
  m_keepAliveAlarm = rate * kKeepAlivesUntilDeath;
  resetKeepAliveAlarm();
}

void ServerProxy::handleData()
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (m_sequenceProtocolFailed)
    return;
  if (m_sequenceDispatching) {
    // Do not let a nested event dispatch read transport bytes through the
    // temporary payload stream, or attribute its submissions to this input.
    deskflow::windowshare::InputSequenceSubmissionScope::fail();
    m_sequenceProtocolFailed = true;
    revokeInputSequence();
    return;
  }
  m_messageBytes = m_stream->getSize();
#endif
  // handle messages until there are no more.  first read message code.
  uint8_t code[4];
  uint32_t n = m_stream->read(code, 4);
  while (n != 0) {
    // verify we got an entire code
    if (n != 4) {
      LOG_ERR("incomplete message from server: %d bytes", n);
      requestDisconnect("incomplete message from server");
      return;
    }

    // parse message
    LOG_VERBOSE("msg from server: %c%c%c%c", code[0], code[1], code[2], code[3]);
    try {
      switch ((this->*m_parser)(code)) {
        using enum ConnectionResult;
      case Okay:
        break;

      case Unknown:
        LOG_ERR("invalid message from server: %c%c%c%c", code[0], code[1], code[2], code[3]);
        // not possible to determine message boundaries
        // read the whole stream to discard unkonwn data
        while (m_stream->read(nullptr, 4))
          ;
        break;

      case Disconnect:
        return;
      }
    } catch (const BadClientException &e) {
      LOG_ERR("protocol error from server: %s", e.what());
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
      m_sequenceProtocolFailed = true;
      revokeInputSequence();
#endif
      ProtocolUtil::writef(m_stream, kMsgEBad);
      requestDisconnect("invalid message from server");
      return;
    }

    // next message
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    m_messageBytes = m_stream->getSize();
#endif
    n = m_stream->read(code, 4);
  }

  flushCompressedMouse();
}

ServerProxy::ConnectionResult ServerProxy::parseHandshakeMessage(const uint8_t *code)
{
  using enum ConnectionResult;
  using enum deskflow::core::ConnectionRefusal;

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (memcmp(code, kMsgDInputSequence, 4) == 0)
    throw BadClientException("Input sequence before handshake completion");
#endif

  if (memcmp(code, kMsgQInfo, 4) == 0) {
    queryInfo();
  }

  else if (memcmp(code, kMsgCInfoAck, 4) == 0) {
    infoAcknowledgment();
  }

  else if (memcmp(code, kMsgDSetOptions, 4) == 0) {
    setOptions();

    // handshake is complete
    m_parser = &ServerProxy::handleMessage;

    m_client->handshakeComplete();
  }

  else if (memcmp(code, kMsgCResetOptions, 4) == 0) {
    resetOptions();
  }

  else if (memcmp(code, kMsgCKeepAlive, 4) == 0) {
    // echo keep alives and reset alarm
    ProtocolUtil::writef(m_stream, kMsgCKeepAlive);
    resetKeepAliveAlarm();
  }

  else if (memcmp(code, kMsgCNoop, 4) == 0) {
    // accept and discard no-op
  }

  else if (memcmp(code, kMsgCClose, 4) == 0) {
    // server wants us to hangup
    LOG_VERBOSE("recv close");
    requestDisconnect(nullptr);
    return Disconnect;
  }

  else if (memcmp(code, kMsgEIncompatible, 4) == 0) {
    int32_t major;
    int32_t minor;
    ProtocolUtil::readf(m_stream, kMsgEIncompatible + 4, &major, &minor);
    LOG_ERR("server has incompatible version %d.%d", major, minor);
    requestRefuseConnection(IncompatibleVersion, "server has incompatible version");
    return Disconnect;
  }

  else if (memcmp(code, kMsgEBusy, 4) == 0) {
    LOG_ERR("server already has a connected client with name \"%s\"", m_client->getName().c_str());
    requestRefuseConnection(AlreadyConnected, "server already has a connected client with our name");
    return Disconnect;
  }

  else if (memcmp(code, kMsgEUnknown, 4) == 0) {
    LOG_ERR("server refused client with name \"%s\"", m_client->getName().c_str());
    requestRefuseConnection(UnknownClient, "server refused client with our name");
    return Disconnect;
  }

  else if (memcmp(code, kMsgEBad, 4) == 0) {
    LOG_ERR("server disconnected due to a protocol error");
    requestRefuseConnection(ProtocolError, "server reported a protocol error");
    return Disconnect;
  } else {
    return Unknown;
  }

  return Okay;
}

ServerProxy::ConnectionResult ServerProxy::handleMessage(const uint8_t *code)
{
  const auto result = parseMessage(code);
  if (result == ConnectionResult::Okay) {
    // send a reply.  this is intended to work around a delay when
    // running a linux server and an OS X (any BSD?) client.  the
    // client waits to send an ACK (if the system control flag
    // net.inet.tcp.delayed_ack is 1) in hopes of piggybacking it
    // on a data packet.  we provide that packet here.  i don't
    // know why a delayed ACK should cause the server to wait since
    // TCP_NODELAY is enabled.
    ProtocolUtil::writef(m_stream, kMsgCNoop);
  }
  return result;
}

ServerProxy::ConnectionResult ServerProxy::parseMessage(const uint8_t *code)
{
  using enum ConnectionResult;

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (memcmp(code, kMsgDInputSequence, 4) == 0) {
    inputSequence();
    return Okay;
  }
#endif

  if (memcmp(code, kMsgDMouseMove, 4) == 0) {
    mouseMove();
  }

  else if (memcmp(code, kMsgDMouseRelMove, 4) == 0) {
    mouseRelativeMove();
  }

  else if (memcmp(code, kMsgDMouseWheel, 4) == 0) {
    mouseWheel();
  }

  else if (memcmp(code, kMsgDKeyDown1_1, 4) == 0) {
    uint16_t id = 0;
    uint16_t mask = 0;
    uint16_t button = 0;
    ProtocolUtil::readf(getStream(), kMsgDKeyDown1_1 + 4, &id, &mask, &button);
    LOG_VERBOSE("recv key down id=0x%08x, mask=0x%04x, button=0x%04x", id, mask, button);

    keyDown(id, mask, button, "");
  }

  else if (memcmp(code, kMsgDKeyUp, 4) == 0) {
    keyUp();
  }

  else if (memcmp(code, kMsgDMouseDown, 4) == 0) {
    mouseDown();
  }

  else if (memcmp(code, kMsgDMouseUp, 4) == 0) {
    mouseUp();
  }

  else if (memcmp(code, kMsgDKeyRepeat1_1, 4) == 0) {
    uint16_t id = 0;
    uint16_t mask = 0;
    uint16_t count = 0;
    uint16_t button = 0;
    ProtocolUtil::readf(getStream(), kMsgDKeyRepeat1_1 + 4, &id, &mask, &count, &button);
    LOG_VERBOSE("recv key repeat id=0x%08x, mask=0x%04x, count=%d, button=0x%04x", id, mask, count, button);

    keyRepeat(id, mask, count, button, "");
  }

  else if (memcmp(code, kMsgCKeepAlive, 4) == 0) {
    // echo keep alives and reset alarm
    ProtocolUtil::writef(m_stream, kMsgCKeepAlive);
    resetKeepAliveAlarm();
  }

  else if (memcmp(code, kMsgCNoop, 4) == 0) {
    // accept and discard no-op
  }

  else if (memcmp(code, kMsgCEnter, 4) == 0) {
    enter();
  }

  else if (memcmp(code, kMsgCLeave, 4) == 0) {
    leave();
  }

  else if (memcmp(code, kMsgCClipboard, 4) == 0) {
    grabClipboard();
  }

  else if (memcmp(code, kMsgCScreenSaver, 4) == 0) {
    screensaver();
  }

  else if (memcmp(code, kMsgQInfo, 4) == 0) {
    queryInfo();
  }

  else if (memcmp(code, kMsgCInfoAck, 4) == 0) {
    infoAcknowledgment();
  }

  else if (memcmp(code, kMsgDClipboard, 4) == 0) {
    setClipboard();
  }

  else if (memcmp(code, kMsgCResetOptions, 4) == 0) {
    resetOptions();
  }

  else if (memcmp(code, kMsgDSetOptions, 4) == 0) {
    setOptions();
  }

  else if (memcmp(code, kMsgCClose, 4) == 0) {
    // server wants us to hangup
    LOG_VERBOSE("recv close");
    requestDisconnect(nullptr);
    return Disconnect;
  } else if (memcmp(code, kMsgEBad, 4) == 0) {
    LOG_ERR("server disconnected due to a protocol error");
    requestDisconnect("server reported a protocol error");
    return Disconnect;
  } else {
    return Unknown;
  }

  return Okay;
}

void ServerProxy::handleKeepAliveAlarm()
{
  LOG_INFO("server is dead");
  requestDisconnect("server is not responding");
}

void ServerProxy::requestDisconnect(const char *message)
{
  m_events->addEvent(Event(
      EventTypes::ClientDisconnectRequested, m_stream->getEventTarget(),
      new Client::DisconnectRequest(Client::DisconnectRequest::Kind::Disconnect, message)
  ));
}

void ServerProxy::requestRefuseConnection(deskflow::core::ConnectionRefusal reason, const char *message)
{
  m_events->addEvent(Event(
      EventTypes::ClientDisconnectRequested, m_stream->getEventTarget(), new Client::DisconnectRequest(reason, message)
  ));
}

void ServerProxy::onInfoChanged()
{
  // ignore mouse motion until we receive acknowledgment of our info
  // change message.
  m_ignoreMouse = true;

  // send info update
  queryInfo();
}

bool ServerProxy::onGrabClipboard(ClipboardID id)
{
  LOG_VERBOSE("sending clipboard %d changed", id);
  ProtocolUtil::writef(m_stream, kMsgCClipboard, id, m_seqNum);
  return true;
}

void ServerProxy::onClipboardChanged(ClipboardID id, const IClipboard *clipboard)
{
  std::string data = IClipboard::marshall(clipboard);
  LOG_DEBUG("sending clipboard %d seqnum=%d", id, m_seqNum);

  StreamChunker::sendClipboard(data, data.size(), id, m_seqNum, m_events, this);
}

void ServerProxy::flushCompressedMouse()
{
  if (m_compressMouse) {
    m_compressMouse = false;
    m_client->mouseMove(m_xMouse, m_yMouse);
  }
  if (m_compressMouseRelative) {
    m_compressMouseRelative = false;
    m_client->mouseRelativeMove(m_dxMouse, m_dyMouse);
    m_dxMouse = 0;
    m_dyMouse = 0;
  }
}

void ServerProxy::sendInfo(const ClientInfo &info)
{
  LOG_VERBOSE("sending info shape=%d,%d %dx%d", info.m_x, info.m_y, info.m_w, info.m_h);
  ProtocolUtil::writef(m_stream, kMsgDInfo, info.m_x, info.m_y, info.m_w, info.m_h, 0, info.m_mx, info.m_my);
}

KeyID ServerProxy::translateKey(KeyID id) const
{
  static const KeyID s_translationTable[kKeyModifierIDLast][2] = {
      {kKeyNone, kKeyNone},     {kKeyShift_L, kKeyShift_R}, {kKeyControl_L, kKeyControl_R}, {kKeyAlt_L, kKeyAlt_R},
      {kKeyMeta_L, kKeyMeta_R}, {kKeySuper_L, kKeySuper_R}, {kKeyAltGr, kKeyAltGr}
  };

  KeyModifierID id2 = kKeyModifierIDNull;
  uint32_t side = 0;
  switch (id) {
  case kKeyShift_L:
    id2 = kKeyModifierIDShift;
    side = 0;
    break;

  case kKeyShift_R:
    id2 = kKeyModifierIDShift;
    side = 1;
    break;

  case kKeyControl_L:
    id2 = kKeyModifierIDControl;
    side = 0;
    break;

  case kKeyControl_R:
    id2 = kKeyModifierIDControl;
    side = 1;
    break;

  case kKeyAlt_L:
    id2 = kKeyModifierIDAlt;
    side = 0;
    break;

  case kKeyAlt_R:
    id2 = kKeyModifierIDAlt;
    side = 1;
    break;

  case kKeyAltGr:
    id2 = kKeyModifierIDAltGr;
    side = 1; // there is only one alt gr key on the right side
    break;

  case kKeyMeta_L:
    id2 = kKeyModifierIDMeta;
    side = 0;
    break;

  case kKeyMeta_R:
    id2 = kKeyModifierIDMeta;
    side = 1;
    break;

  case kKeySuper_L:
    id2 = kKeyModifierIDSuper;
    side = 0;
    break;

  case kKeySuper_R:
    id2 = kKeyModifierIDSuper;
    side = 1;
    break;

  default:
    break;
  }

  if (id2 != kKeyModifierIDNull) {
    return s_translationTable[m_modifierTranslationTable[id2]][side];
  } else {
    return id;
  }
}

KeyModifierMask ServerProxy::translateModifierMask(KeyModifierMask mask) const
{
  static const KeyModifierMask s_masks[kKeyModifierIDLast] = {0x0000,          KeyModifierShift, KeyModifierControl,
                                                              KeyModifierAlt,  KeyModifierMeta,  KeyModifierSuper,
                                                              KeyModifierAltGr};

  KeyModifierMask newMask = mask & ~(KeyModifierShift | KeyModifierControl | KeyModifierAlt | KeyModifierMeta |
                                     KeyModifierSuper | KeyModifierAltGr);
  if ((mask & KeyModifierShift) != 0) {
    newMask |= s_masks[m_modifierTranslationTable[kKeyModifierIDShift]];
  }
  if ((mask & KeyModifierControl) != 0) {
    newMask |= s_masks[m_modifierTranslationTable[kKeyModifierIDControl]];
  }
  if ((mask & KeyModifierAlt) != 0) {
    newMask |= s_masks[m_modifierTranslationTable[kKeyModifierIDAlt]];
  }
  if ((mask & KeyModifierAltGr) != 0) {
    newMask |= s_masks[m_modifierTranslationTable[kKeyModifierIDAltGr]];
  }
  if ((mask & KeyModifierMeta) != 0) {
    newMask |= s_masks[m_modifierTranslationTable[kKeyModifierIDMeta]];
  }
  if ((mask & KeyModifierSuper) != 0) {
    newMask |= s_masks[m_modifierTranslationTable[kKeyModifierIDSuper]];
  }
  return newMask;
}

void ServerProxy::enter()
{
  // parse
  int16_t x;
  int16_t y;
  uint16_t mask;
  uint32_t seqNum;
  ProtocolUtil::readf(m_stream, kMsgCEnter + 4, &x, &y, &seqNum, &mask);
  LOG_VERBOSE("recv enter, %d,%d %d %04x", x, y, seqNum, mask);

  // discard old compressed mouse motion, if any
  m_compressMouse = false;
  m_compressMouseRelative = false;
  m_dxMouse = 0;
  m_dyMouse = 0;
  m_seqNum = seqNum;

  // forward
  m_client->enter(x, y, seqNum, static_cast<KeyModifierMask>(mask), false);
}

void ServerProxy::leave()
{
  // parse
  LOG_VERBOSE("recv leave");

  // send last mouse motion
  flushCompressedMouse();

  // forward
  m_client->leave();
}

void ServerProxy::setClipboard()
{
  // parse
  ClipboardID id;
  uint32_t seq;

  auto r = ClipboardChunk::assemble(
      m_stream, m_clipboardDataCached, id, seq, m_clipboardChunkState, m_client->getMaximumClipboardReceiveSizeBytes()
  );

  if (r == TransferState::Started) {
    size_t size = ClipboardChunk::getExpectedSize(m_clipboardChunkState);
    LOG_DEBUG("receiving clipboard %d size=%zu", id, size);
  } else if (r == TransferState::Finished) {
    LOG_DEBUG("received clipboard %d size=%zu", id, m_clipboardDataCached.size());

    // forward
    Clipboard clipboard;
    clipboard.unmarshall(m_clipboardDataCached, 0);
    m_client->setClipboard(id, &clipboard);
    m_clipboardDataCached.clear();
    m_clipboardDataCached.shrink_to_fit();

    LOG_INFO("clipboard was updated");
  } else if (r == TransferState::Error) {
    requestDisconnect("invalid clipboard data from server");
  }
}

void ServerProxy::grabClipboard()
{
  // parse
  ClipboardID id;
  uint32_t seqNum;
  ProtocolUtil::readf(m_stream, kMsgCClipboard + 4, &id, &seqNum);
  LOG_DEBUG("recv grab clipboard %d", id);

  // validate
  if (id >= kClipboardEnd) {
    return;
  }

  // forward
  m_client->grabClipboard(id);
}

void ServerProxy::keyDown(uint16_t id, uint16_t mask, uint16_t button, const std::string &lang)
{
  flushCompressedMouse();

  KeyID id2 = translateKey(static_cast<KeyID>(id));
  KeyModifierMask mask2 = translateModifierMask(static_cast<KeyModifierMask>(mask));
  if (id2 != static_cast<KeyID>(id) || mask2 != static_cast<KeyModifierMask>(mask))
    LOG_VERBOSE("key down translated to id=0x%08x, mask=0x%04x", id2, mask2);

  m_client->keyDown(id2, mask2, button, lang);
}

void ServerProxy::keyRepeat(uint16_t id, uint16_t mask, uint16_t count, uint16_t button, const std::string &lang)
{
  flushCompressedMouse();

  KeyID id2 = translateKey(static_cast<KeyID>(id));
  KeyModifierMask mask2 = translateModifierMask(static_cast<KeyModifierMask>(mask));
  if (id2 != static_cast<KeyID>(id) || mask2 != static_cast<KeyModifierMask>(mask))
    LOG_VERBOSE("key repeat translated to id=0x%08x, mask=0x%04x", id2, mask2);

  m_client->keyRepeat(id2, mask2, count, button, lang);
}

void ServerProxy::keyUp()
{
  // get mouse up to date
  flushCompressedMouse();

  // parse
  uint16_t id;
  uint16_t mask;
  uint16_t button;
  ProtocolUtil::readf(getStream(), kMsgDKeyUp + 4, &id, &mask, &button);
  LOG_VERBOSE("recv key up id=0x%08x, mask=0x%04x, button=0x%04x", id, mask, button);

  // translate
  KeyID id2 = translateKey(static_cast<KeyID>(id));
  KeyModifierMask mask2 = translateModifierMask(static_cast<KeyModifierMask>(mask));
  if (id2 != static_cast<KeyID>(id) || mask2 != static_cast<KeyModifierMask>(mask))
    LOG_VERBOSE("key up translated to id=0x%08x, mask=0x%04x", id2, mask2);

  // forward
  m_client->keyUp(id2, mask2, button);
}

void ServerProxy::mouseDown()
{
  // get mouse up to date
  flushCompressedMouse();

  // parse
  int8_t id;
  ProtocolUtil::readf(getStream(), kMsgDMouseDown + 4, &id);
  LOG_VERBOSE("recv mouse down id=%d", id);

  // forward
  m_client->mouseDown(static_cast<ButtonID>(id));
}

void ServerProxy::mouseUp()
{
  // get mouse up to date
  flushCompressedMouse();

  // parse
  int8_t id;
  ProtocolUtil::readf(getStream(), kMsgDMouseUp + 4, &id);
  LOG_VERBOSE("recv mouse up id=%d", id);

  // forward
  m_client->mouseUp(static_cast<ButtonID>(id));
}

void ServerProxy::mouseMove()
{
  // parse
  bool ignore;
  int16_t x;
  int16_t y;
  ProtocolUtil::readf(getStream(), kMsgDMouseMove + 4, &x, &y);

  // note if we should ignore the move
  ignore = m_ignoreMouse;
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (ignore)
    deskflow::windowshare::InputSequenceSubmissionScope::filtered();
#endif

  // compress mouse motion events if more input follows
  if (!ignore && !m_compressMouse && getStream()->isReady()) {
    m_compressMouse = true;
  }

  // if compressing then ignore the motion but record it
  if (m_compressMouse) {
    m_compressMouseRelative = false;
    ignore = true;
    m_xMouse = x;
    m_yMouse = y;
    m_dxMouse = 0;
    m_dyMouse = 0;
  }
  LOG_VERBOSE("recv mouse move %d,%d", x, y);

  // forward
  if (!ignore) {
    m_client->mouseMove(x, y);
  }
}

void ServerProxy::mouseRelativeMove()
{
  // parse
  bool ignore;
  int16_t dx;
  int16_t dy;
  ProtocolUtil::readf(getStream(), kMsgDMouseRelMove + 4, &dx, &dy);

  // note if we should ignore the move
  ignore = m_ignoreMouse;
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  if (ignore)
    deskflow::windowshare::InputSequenceSubmissionScope::filtered();
#endif

  // compress mouse motion events if more input follows
  if (!ignore && !m_compressMouseRelative && getStream()->isReady()) {
    m_compressMouseRelative = true;
  }

  // if compressing then ignore the motion but record it
  if (m_compressMouseRelative) {
    ignore = true;
    m_dxMouse += dx;
    m_dyMouse += dy;
  }
  LOG_VERBOSE("recv mouse relative move %d,%d", dx, dy);

  // forward
  if (!ignore) {
    m_client->mouseRelativeMove(dx, dy);
  }
}

void ServerProxy::mouseWheel()
{
  // get mouse up to date
  flushCompressedMouse();

  // parse
  int16_t xDelta;
  int16_t yDelta;
  ProtocolUtil::readf(getStream(), kMsgDMouseWheel + 4, &xDelta, &yDelta);
  LOG_VERBOSE("recv mouse wheel %+d,%+d", xDelta, yDelta);

  // forward
  m_client->mouseWheel(xDelta, yDelta);
}

void ServerProxy::screensaver()
{
  // parse
  int8_t on;
  ProtocolUtil::readf(m_stream, kMsgCScreenSaver + 4, &on);
  LOG_VERBOSE("recv screen saver on=%d", on);

  // forward
  m_client->screensaver(on != 0);
}

void ServerProxy::resetOptions()
{
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  revokeInputSequence();
#endif
  // parse
  LOG_VERBOSE("recv reset options");

  // forward
  m_client->resetOptions();

  // reset keep alive
  setKeepAliveRate(kKeepAliveRate);

  // reset modifier translation table
  for (KeyModifierID id = 0; id < kKeyModifierIDLast; ++id) {
    m_modifierTranslationTable[id] = id;
  }
}

void ServerProxy::setOptions()
{
  // parse
  OptionsList options;
  if (!ProtocolUtil::readf(m_stream, kMsgDSetOptions + 4, &options)) {
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    if (supportsInputSequence())
      throw BadClientException("Incomplete options for input sequence negotiation");
#endif
  }
  LOG_VERBOSE("recv set options size=%d", options.size());

  if (options.size() % 2 != 0) {
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
    if (supportsInputSequence())
      throw BadClientException("Malformed options for input sequence negotiation");
#endif
    LOG_ERR("options are the incorrect size, can not process them");
    return;
  }

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  setInputSequenceOptions(options);
#endif

  // forward
  m_client->setOptions(options);

  // update modifier table
  for (uint32_t i = 0, n = (uint32_t)options.size(); i < n; i += 2) {
    KeyModifierID id = kKeyModifierIDNull;
    if (options[i] == kOptionModifierMapForShift) {
      id = kKeyModifierIDShift;
    } else if (options[i] == kOptionModifierMapForControl) {
      id = kKeyModifierIDControl;
    } else if (options[i] == kOptionModifierMapForAlt) {
      id = kKeyModifierIDAlt;
    } else if (options[i] == kOptionModifierMapForAltGr) {
      id = kKeyModifierIDAltGr;
    } else if (options[i] == kOptionModifierMapForMeta) {
      id = kKeyModifierIDMeta;
    } else if (options[i] == kOptionModifierMapForSuper) {
      id = kKeyModifierIDSuper;
    } else if (options[i] == kOptionHeartbeat) {
      // update keep alive
      setKeepAliveRate(1.0e-3 * static_cast<double>(options[i + 1]));
    }

    if (id != kKeyModifierIDNull) {
      m_modifierTranslationTable[id] = std::clamp<KeyModifierMask>(options[i + 1], 0, kKeyModifierIDLast - 1);
      LOG_VERBOSE("modifier %d mapped to %d", id, m_modifierTranslationTable[id]);
    }
  }
}

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
void ServerProxy::revokeInputSequence()
{
  m_sequenceGeneration = 0;
  m_sequenceNext = 0;
  m_sequencePeer.reset();
}

bool ServerProxy::inputSequenceConnectionLive() const
{
  const auto current = m_stream->authenticatedPeerTicket();
  return m_sequencePeer && m_sequencePeer->live() && current && current->live() &&
         current->incarnation() == m_sequencePeer->incarnation();
}

void ServerProxy::setInputSequenceOptions(const OptionsList &options)
{
  if (!supportsInputSequence())
    return;
  std::optional<uint32_t> advertised;
  for (std::size_t index = 0; index < options.size(); index += 2) {
    if (options[index] != kOptionInputSequenceTrace)
      continue;
    if (advertised)
      throw BadClientException("Duplicate input sequence option");
    advertised = options[index + 1];
  }
  if (!advertised)
    return;
  if (*advertised == 0) {
    revokeInputSequence();
    return;
  }
  if (*advertised == m_sequenceGeneration && inputSequenceConnectionLive()) {
    // Retransmission is idempotent; it must not rewind the expected sequence.
    ProtocolUtil::writef(m_stream, kMsgCInputSequenceAck, *advertised);
    return;
  }
  if (*advertised <= m_sequenceGenerationHighWater)
    throw BadClientException("Retired input sequence generation");
  revokeInputSequence();
  m_sequenceGenerationHighWater = *advertised;
  const auto peer = m_stream->authenticatedPeerTicket();
  if (!peer || !peer->live())
    return;
  m_sequencePeer = peer;
  m_sequenceGeneration = *advertised;
  m_sequenceNext = 1;
  ProtocolUtil::writef(m_stream, kMsgCInputSequenceAck, *advertised);
}

void ServerProxy::inputSequence()
{
  using namespace deskflow::windowshare;
  m_sequenceDispatching = true;
  const auto dispatchGuard = qScopeGuard([this] { m_sequenceDispatching = false; });
  if (!supportsInputSequence() || !m_sequenceGeneration || !inputSequenceConnectionLive()) {
    revokeInputSequence();
    throw BadClientException("Unnegotiated input sequence");
  }
  // m_messageBytes was captured before reading the message code. A four-byte
  // malformed packet must not borrow its body from the next packet.
  if (m_messageBytes < 6 + inputSequenceHeaderBytes + 4 || m_messageBytes > 6 + inputSequenceMaximumBody)
    throw BadClientException("Invalid input sequence packet size");
  std::array<char, 2 + inputSequenceMaximumBody> bytes{};
  const auto remaining = m_messageBytes - 4;
  if (m_stream->read(bytes.data(), remaining) != remaining)
    throw BadClientException("Incomplete input sequence packet");
  const auto bodySize =
      static_cast<uint32_t>((static_cast<unsigned char>(bytes[0]) << 8) | static_cast<unsigned char>(bytes[1]));
  if (bodySize != remaining - 2)
    throw BadClientException("Input sequence packet length mismatch");
  const auto envelope = decodeInputSequenceEnvelope(std::string_view(bytes.data() + 2, bodySize));
  if (!envelope || envelope->token.generation != m_sequenceGeneration || !m_sequenceNext ||
      envelope->token.wireSequence != m_sequenceNext)
    throw BadClientException("Invalid input sequence metadata or payload");

  // Previous unstamped motion belongs to its own dispatch, never this token.
  flushCompressedMouse();
  if (m_sequenceProtocolFailed || !inputSequenceConnectionLive()) {
    revokeInputSequence();
    throw BadClientException("Input sequence connection revoked");
  }
  m_sequenceNext = m_sequenceNext == std::numeric_limits<uint64_t>::max() ? 0 : m_sequenceNext + 1;
  InputSequenceReceipt receipt;
  bool dispatchFailed = false;
  {
    InputSequenceSubmissionScope scope(envelope->token);
    InputSequencePayloadStream payload(std::string_view(envelope->payload).substr(4));
    m_sequencePayloadStream = &payload;
    const auto restore = qScopeGuard([this] { m_sequencePayloadStream = nullptr; });
    try {
      const auto result = parseMessage(reinterpret_cast<const uint8_t *>(envelope->payload.data()));
      if (result != ConnectionResult::Okay || payload.isReady())
        throw BadClientException("Input sequence payload not completely handled");
    } catch (...) {
      InputSequenceSubmissionScope::fail();
      dispatchFailed = true;
    }
    receipt = scope.receipt();
  }
  if (m_sequenceProtocolFailed || !inputSequenceConnectionLive()) {
    revokeInputSequence();
    throw BadClientException("Input sequence connection or dispatch revoked");
  }
  const auto frame = encodeInputSequenceReceipt(receipt);
  m_stream->write(frame.data(), static_cast<uint32_t>(frame.size()));
  if (dispatchFailed)
    throw BadClientException("Input sequence backend dispatch failed");
}
#endif

void ServerProxy::queryInfo()
{
  ClientInfo info;
  m_client->getShape(info.m_x, info.m_y, info.m_w, info.m_h);
  m_client->getCursorPos(info.m_mx, info.m_my);
  sendInfo(info);
}

void ServerProxy::infoAcknowledgment()
{
  LOG_VERBOSE("recv info acknowledgment");
  m_ignoreMouse = false;
}
