/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerTests.h"

#include "server/Server.h"

#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
#include "../deskflow/MockEventQueue.h"
#include "../deskflow/MockKeyState.h"
#include "deskflow/Computer.h"
#include "deskflow/PlatformComputer.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include "io/IStream.h"
#include "server/ClientProxy1_1.h"
#include "server/PrimaryClient.h"

#include <QTemporaryDir>
#include <memory>
#include <utility>

namespace {

// Exercise the real Computer/PrimaryClient lifecycle without opening a display,
// registering system hotkeys, or synthesizing input on the test machine.
class RoutingPlatform : public PlatformComputer
{
public:
  explicit RoutingPlatform(IEventQueue *events) : PlatformComputer(events), m_keys(*events)
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<RoutingPlatform *>(this);
  }
  bool getClipboard(ClipboardID, IClipboard *) const override
  {
    return false;
  }
  void getShape(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const override
  {
    x = y = 0;
    width = height = 1000;
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
    x = y = 500;
  }
  void getCursorCenter(int32_t &x, int32_t &y) const override
  {
    x = y = 500;
  }
  void reconfigure(uint32_t) override
  {
  }
  uint32_t activeSides() override
  {
    return 0;
  }
  void warpCursor(int32_t, int32_t) override
  {
  }
  uint32_t registerHotKey(KeyID, KeyModifierMask) override
  {
    return 1;
  }
  void unregisterHotKey(uint32_t) override
  {
  }
  void fakeInputBegin() override
  {
  }
  void fakeInputEnd() override
  {
  }
  int32_t getJumpZoneSize() const override
  {
    return 1;
  }
  bool isAnyMouseButtonDown(uint32_t &) const override
  {
    return false;
  }
  void fakeMouseButton(ButtonID, bool) override
  {
  }
  void fakeMouseMove(int32_t, int32_t) override
  {
  }
  void fakeMouseRelativeMove(int32_t, int32_t) const override
  {
  }
  void fakeMouseWheel(ScrollDelta) const override
  {
  }
  void updateKeyMap() override
  {
  }
  void updateKeyState() override
  {
  }
  KeyModifierMask pollActiveModifiers() const override
  {
    return modifiers;
  }
  void enable() override
  {
  }
  void disable() override
  {
  }
  void enter() override
  {
  }
  bool canLeave() override
  {
    return true;
  }
  void leave() override
  {
  }
  bool setClipboard(ClipboardID, const IClipboard *) override
  {
    return true;
  }
  void checkClipboards() override
  {
  }
  void openScreensaver(bool) override
  {
  }
  void closeScreensaver() override
  {
  }
  void screensaver(bool) override
  {
  }
  void resetOptions() override
  {
  }
  void setOptions(const OptionsList &) override
  {
  }
  void setSequenceNumber(uint32_t) override
  {
  }
  std::string getSecureInputApp() const override
  {
    return {};
  }
  bool isPrimary() const override
  {
    return true;
  }

  KeyModifierMask modifiers = 0;

protected:
  void updateButtons() override
  {
  }
  IKeyState *getKeyState() const override
  {
    return &m_keys;
  }
  void handleSystemEvent(const Event &) override
  {
  }

private:
  mutable MockKeyState m_keys;
};

struct WireCapture
{
  QByteArray bytes;
  bool destroyed = false;
  QByteArray take()
  {
    return std::exchange(bytes, {});
  }
};

class RoutingStream : public deskflow::IStream
{
public:
  explicit RoutingStream(std::shared_ptr<WireCapture> capture) : m_capture(std::move(capture))
  {
  }
  ~RoutingStream() override
  {
    m_capture->destroyed = true;
  }
  void write(const void *buffer, uint32_t n) override
  {
    m_capture->bytes.append(static_cast<const char *>(buffer), n);
  }
  void close() override
  {
  }
  uint32_t read(void *, uint32_t) override
  {
    return 0;
  }
  void flush() override
  {
  }
  void shutdownInput() override
  {
  }
  void shutdownOutput() override
  {
  }
  void *getEventTarget() const override
  {
    return const_cast<RoutingStream *>(this);
  }
  bool isReady() const override
  {
    return false;
  }
  uint32_t getSize() const override
  {
    return 0;
  }

private:
  std::shared_ptr<WireCapture> m_capture;
};

class RoutingClient : public ClientProxy1_1
{
public:
  RoutingClient(const char *name, IEventQueue *events, const std::shared_ptr<WireCapture> &capture)
      : ClientProxy1_1(name, new RoutingStream(capture), events)
  {
  }

  void getShape(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const override
  {
    x = y = 0;
    width = height = 1000;
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
    x = y = 500;
  }
};

struct RoutingFixture
{
  MockEventQueue events;
  deskflow::server::Config config{&events};
  RoutingPlatform *platform = new RoutingPlatform(&events);
  deskflow::Computer computer{platform, &events};
  PrimaryClient primary{"primary", &computer};
  std::unique_ptr<Server> server;
  std::shared_ptr<WireCapture> first = std::make_shared<WireCapture>();
  std::shared_ptr<WireCapture> second = std::make_shared<WireCapture>();
  RoutingClient *firstClient = nullptr;
  RoutingClient *secondClient = nullptr;

  RoutingFixture()
  {
    config.addComputer("primary");
    config.addComputer("first");
    config.addComputer("second");
    config.addOption("", kOptionClipboardSharing, 0);
    config.addOption("", kOptionDisableLockToComputer, 1);
    server = std::make_unique<Server>(config, &primary, &computer, &events);
    firstClient = new RoutingClient("first", &events, first);
    secondClient = new RoutingClient("second", &events, second);
    server->adoptClient(firstClient);
    server->adoptClient(secondClient);
    clear();
  }

  void clear()
  {
    first->take();
    second->take();
  }
};

} // namespace

void ServerTests::initTestCase()
{
  m_arch.init();
  // Config and Computer consult Settings. Give this process disposable files
  // before constructing either, so tests never run configured enter/exit commands.
  static QTemporaryDir settings;
  QVERIFY(settings.isValid());
  qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
  qputenv("XDG_STATE_HOME", settings.path().toUtf8());
  QDir(settings.path()).mkpath(kAppName);
  Settings::setSettingsFile(settings.filePath("routing.conf"));
  Settings::setStateFile(settings.filePath("routing.state"));
  // Core IPC receives notifications but never listens on a socket in these tests.
  new deskflow::core::ipc::CoreIpcServer(this);
}

void ServerTests::inputRoutes_keepPressRecipientAcrossSwitch()
{
  RoutingFixture f;
  auto &server = *f.server;
  server.switchComputer(f.firstClient, 100, 100, false);
  f.clear();
  server.onKeyDown('a', 0, 0x1e, "en", "");
  server.onMouseDown(1);
  QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("0061 0000 001e") + "DMDN" + QByteArray(1, '\1'));

  server.switchComputer(f.secondClient, 100, 100, false);
  f.clear();
  server.onKeyRepeat('a', 0, 3, 0x1e, "en");
  // macOS key-up may omit the symbol; physical button still identifies owner.
  server.onKeyUp(kKeyNone, 0, 0x1e, "");
  server.onMouseUp(1);
  QCOMPARE(
      f.first->take(), "DKRP" + QByteArray::fromHex("0061 0000 0003 001e") + "DKUP" +
                           QByteArray::fromHex("0000 0000 001e") + "DMUP" + QByteArray(1, '\1')
  );
  QVERIFY(f.second->take().isEmpty());
}

void ServerTests::inputRoutes_localPressDoesNotAcquireRemoteRecipient()
{
  RoutingFixture f;
  auto &server = *f.server;
  server.onKeyDown('a', 0, 0x1e, "en", "");
  server.onKeyDown(kKeyShift_L, KeyModifierShift, 0x2a, "en", "");
  server.onMouseDown(1);
  f.platform->modifiers = KeyModifierShift | KeyModifierCapsLock;
  server.switchComputer(f.firstClient, 100, 100, false);
  // Enter must synchronize toggles, without importing a local held Shift.
  QCOMPARE(f.first->take(), "CINN" + QByteArray::fromHex("0064 0064 00000001 1000"));
  server.onKeyRepeat('a', 0, 2, 0x1e, "en");
  server.onKeyDown('a', 0, 0x1e, "en", "");
  server.onKeyUp('a', 0, 0x1e, "");
  server.onMouseUp(1);
  QVERIFY(f.first->take().isEmpty());

  // A new remote key can legitimately synthesize the desired held modifier.
  // Its eventual local physical release must clear only the recipient that saw it.
  server.onKeyDown('b', KeyModifierShift, 0x30, "en", "");
  server.onKeyUp('b', KeyModifierShift, 0x30, "");
  QCOMPARE(
      f.first->take(), "DKDN" + QByteArray::fromHex("0062 0001 0030") + "DKUP" + QByteArray::fromHex("0062 0001 0030")
  );
  server.onKeyUp(kKeyShift_L, 0, 0x2a, "");
  QCOMPARE(f.first->take(), "DKDN" + QByteArray::fromHex("ee07 0001 0000"));
  QVERIFY(f.second->take().isEmpty());
}

void ServerTests::inputRoutes_disconnectDoesNotRetargetHeldInput()
{
  RoutingFixture f;
  auto &server = *f.server;
  server.switchComputer(f.firstClient, 100, 100, false);
  server.onKeyDown('a', 0, 0x1e, "en", "");
  server.onMouseDown(1);
  server.switchComputer(f.secondClient, 100, 100, false);
  f.clear();
  server.handleClientDisconnected(f.firstClient);
  QVERIFY(f.first->destroyed);
  QVERIFY(f.first->take().isEmpty()); // no write through the disconnected proxy

  auto replacement = std::make_shared<WireCapture>();
  auto *replacementClient = new RoutingClient("first", &f.events, replacement);
  server.adoptClient(replacementClient);
  server.switchComputer(replacementClient, 100, 100, false);
  replacement->take();
  f.clear();
  server.onKeyRepeat('a', 0, 1, 0x1e, "en");
  server.onKeyDown('a', 0, 0x1e, "en", "");
  server.onMouseDown(1);
  server.onKeyUp('a', 0, 0x1e, "");
  server.onMouseUp(1);
  QVERIFY(replacement->take().isEmpty());
  QVERIFY(f.second->take().isEmpty());
  server.onKeyDown('a', 0, 0x1e, "en", "");
  server.onKeyUp('a', 0, 0x1e, "");
  QCOMPARE(
      replacement->take(),
      "DKDN" + QByteArray::fromHex("0061 0000 001e") + "DKUP" + QByteArray::fromHex("0061 0000 001e")
  );
}

void ServerTests::inputRoutes_orderlyCloseReleasesBeforeClosing()
{
  RoutingFixture f;
  auto &server = *f.server;
  server.switchComputer(f.firstClient, 100, 100, false);
  server.onKeyDown('a', KeyModifierShift, 0x1e, "en", "");
  server.onMouseDown(1);
  server.switchComputer(f.secondClient, 100, 100, false);
  f.clear();
  server.closeClient(f.firstClient, kMsgCClose);
  QCOMPARE(
      f.first->take(), "DKUP" + QByteArray::fromHex("0061 0000 001e") + "DMUP" + QByteArray(1, '\1') + "DKDN" +
                           QByteArray::fromHex("ee07 0001 0000") + "CBYE"
  );
  QVERIFY(f.second->take().isEmpty());
  server.onKeyUp('a', 0, 0x1e, "");
  server.onMouseUp(1);
  QVERIFY(f.first->take().isEmpty());
  QVERIFY(f.second->take().isEmpty());
}
#endif

void ServerTests::SwitchToComputerInfo_alloc_computer()
{
  auto actual = new Server::SwitchToComputerInfo("test");
  QCOMPARE(actual->m_computer, "test");
  delete actual;
}

void ServerTests::KeyboardBroadcastInfo_alloc_stateAndComputers()
{
  auto info = new Server::KeyboardBroadcastInfo(Server::KeyboardBroadcastInfo::State::kOn, "test");
  QCOMPARE(info->m_state, Server::KeyboardBroadcastInfo::State::kOn);
  QCOMPARE(info->m_computers, "test");
  delete info;
}

QTEST_MAIN(ServerTests)
