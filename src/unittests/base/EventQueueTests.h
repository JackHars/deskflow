/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/Log.h"

#include <QObject>

class EventQueueTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void dispatchEvent_noHandler_returnsFalse();
  void dispatchEvent_noTypeHandler_dispatchesUnknownHandler();
  void dispatchEvent_handlerRemovesItself_keepsHandlerAliveUntilReturn();
  void inputObservation_pendingSavedRetrievedAndMoves();
  void inputObservation_dispatchAndArmBoundaries();
  void inputObservation_nestedAndDestructiveDispatch();
  void inputObservation_dropThrowAndAdoptBuffer();
  void inputObservation_stampMutationAndInstallation();
  void inputObservation_disabledAndUnstamped();
  void backlog_pendingCapacityRevokesAndDisposes();
  void backlog_retrievedLifetimeStillConsumesCapacity();
  void backlog_nestedAndDestructiveDispatch();
  void backlog_expiredAndForeignEventsAreTerminal();
  void backlog_platformRefusalAndConcurrentProducers();
  void backlog_controlAndConfiguredEventsAreMeasured();
  void backlog_timersAndSpuriousWakeupsMakeProgress();
  void backlog_ordinaryQueueKeepsAdmissionPolicy();
  void backlog_recursiveDispatchHasItsOwnLimit();
  void backlog_expiredRetrievalDisposesPayload();
  void backlog_dispatchDeadlinesFollowAdmissionAndMoves();
  void watchdog_processDeadlines_data();
  void watchdog_processDeadlines();
#ifdef __APPLE__
  void backlog_nativeMacBufferRetainsTheBound();
#endif

private:
  Arch m_arch;
  Log m_log;
};
