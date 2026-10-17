/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "base/Log.h"
#include <QObject>

class OriginalInputCoordinatorTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void unknownAcquisitionDoesNotStartHandoff();
  void emptyHostDeclinePreservesOrdinaryInput();
  void lateTargetAfterCancellationPreservesNextHandoff();
  void firstClickWaitsForFocusAndExactHostBeforeDelivery();
  void committedInputWakesWithoutPollingTimer_data();
  void committedInputWakesWithoutPollingTimer();
  void firstClickRejectsHostSubstitution_data();
  void firstClickRejectsHostSubstitution();
  void firstClickCancellationRetainsOneCleanupOwner_data();
  void firstClickCancellationRetainsOneCleanupOwner();
  void firstClickNoTargetRequiresExactRelease();
  void firstClickLostTransportCannotAcknowledgeRelease();
  void firstClickOlderPeerFallsBackBeforeAnyGate();
  void consecutiveMappedRecordsRetainSealUntilOwnerRelease();
  void mixedInputBatchesKeepEveryRecordAndDrainInOrder();
  void rejectsMappedSubstitution_data();
  void rejectsMappedSubstitution();
  void pongAndExpiredObservationCannotPrepare();
  void cancellationDuringBackendCallback_data();
  void cancellationDuringBackendCallback();
  void stopMayDestroyCoordinatorFromPriorRevocationCallback();

private:
  Log m_log;
};
