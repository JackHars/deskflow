/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QObject>

class ClientProxyTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void keyDown_data();
  void keyDown();
  void keyRepeat_data();
  void keyRepeat();
  void keyUp_data();
  void keyUp();
  void inputSequence_requiresAuthenticatedProtocol18();
  void inputSequence_wrapsAllInputWriters_data();
  void inputSequence_wrapsAllInputWriters();
  void inputSequence_preservesControlsAndUnstampedInput();
  void inputSequence_receiptRetainsExactOrigin();
  void inputSequence_resetAndDuplicateNegotiation();
  void inputSequence_rejectsMalformedAndUnsolicitedReplies_data();
  void inputSequence_rejectsMalformedAndUnsolicitedReplies();
  void inputSequence_packetBoundaries_data();
  void inputSequence_packetBoundaries();
  void inputSequence_boundsPreserveOrdinaryInput();
  void inputSequence_tlsRevocationAndReplacement();
  void inputSequence_counterExhaustion();
  void inputSequence_callbackMayResetOrDestroy();
  void inputCutoff_requiresNegotiationAndRetainsBinding();
  void inputCutoff_resumesBeforeEveryLegacyWriter();
  void inputCutoff_deadlineAndConnectionRevocation();
  void inputCutoff_rejectsWrongAndMalformedReceipts();
  void inputCutoff_callbackMayDestroyDuringOptions();
  void mappedCutoff_exactObservationAndRevocation();
  void mappedCutoff_rejectsSubstitutionAndUnnegotiatedResult();
  void sealedCutoff_refreshRetainsPrefixAndExactCleanup();
  void sealedCutoff_rejectsSubstitutionAndRevokesLifetime();
  void batchedCutoff_preservesEveryReceiptAndExactCleanup();
  void batchedCutoff_rejectsPartialChangedAndLateResults();
  void mappedCutoff_notificationMayRetireProxy();
  void preparation_negotiatesPromotesAndWaitsForCleanup();
  void preparation_notificationMayRetireProxy();
  void preparation_boundsCancellationAndPreservesAttemptIdentity();

private:
  Log m_log;
};
