/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/Log.h"

#include <QObject>

class ServerProxyTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void handleKeepAliveAlarm_timeout_queuesDisconnectRequest();
  void handleData_incompleteMessage_queuesDisconnectRequest();
  void parseHandshakeMessage_protocolError_queuesRefusalRequest();
  void legacyInput_preservesCompressionLanguageAndModifiers();
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  void inputSequence_onlyAuthenticatedLatestProxyAcknowledges();
  void inputSequence_generationResetAndWithdrawal();
  void inputSequence_mixedMotionKeepsExactScopesAndReceipts();
  void inputSequence_languageAndModifierSemantics();
  void inputSequence_malformedPacketsHaveNoSideEffects_data();
  void inputSequence_malformedPacketsHaveNoSideEffects();
  void inputSequence_noncontiguousOrStaleMetadataDisconnects_data();
  void inputSequence_noncontiguousOrStaleMetadataDisconnects();
  void inputSequence_backendOutcomesAndFailure();
  void inputSequence_revocationAndReentryCannotEmitSuccess();
  void inputSequence_realPacketFilterPreservesEnvelopeBoundary();
  void inputSequence_malformedOptionsCannotNegotiate();
  void inputSequence_optionsCannotCrossPacketBoundary();
  void inputCutoff_flushesMotionDelegatesAndResumes();
  void inputCutoff_requiresNegotiationAndExactBoundedPackets();
  void inputCutoff_freezeRejectsLaterInputAndWrongResume();
  void inputCutoff_deadlineInvalidationAndDestruction();
  void mappedCutoff_delegatesExactPointAndWaitsForDrain();
  void mappedCutoff_rejectsMissingProofAndStaleCompletion();
  void sealedCutoff_refreshesWithoutReopeningLegacy();
  void sealedCutoff_rejectsStaleOverlappingAndInvalidatedMaps();
  void batchedCutoff_delegatesAtomicallyWithoutNewPrefix();
  void batchedCutoff_rejectsOverlapLossAndSubstitution();
  void preparation_ordersFocusPromotionAndLinkedCleanup();
  void preparation_rejectsSubstitutionAndNeverInfersRelease();
#endif

private:
  Arch m_arch;
  Log m_log;
};
