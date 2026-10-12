/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
#include "arch/Arch.h"
#include "base/Log.h"
#endif

class ServerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void SwitchToComputerInfo_alloc_computer();
  void KeyboardBroadcastInfo_alloc_stateAndComputers();
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  void initTestCase();
  void inputRoutes_keepPressRecipientAcrossSwitch();
  void inputRoutes_localPressDoesNotAcquireRemoteRecipient();
  void inputRoutes_disconnectDoesNotRetargetHeldInput();
  void inputRoutes_orderlyCloseReleasesBeforeClosing();
  void selectedPeer_requiresCurrentAuthenticatedRemote();
  void selectedPeer_revokesBeforeSwitchAndFailedLeave();
  void selectedPeer_reconnectDoesNotReviveCachedSelection();
  void selectedPeer_revokesBeforeRemoveCloseAndForceLeave();
  void selectedPeer_configBroadcastAndScreensaverFence();
  void selectedPeer_shutdownAndOverflowAreTerminal();

private:
  Arch m_arch;
  Log m_log;
#endif
};
