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
  void connectionEvent_destroysOwnedPayload();
  void SwitchToComputerInfo_alloc_computer();
  void KeyboardBroadcastInfo_alloc_stateAndComputers();
#if defined(BUILD_WINDOWSHARE) && BUILD_WINDOWSHARE
  void initTestCase();
  void inputFilter_ownedActionsAndConnectionLifetime();
  void inputFilter_actualQueueObservedBorrowedDispatch();
  void originalCapture_legacyKeyExpansion();
  void originalCapture_exactDispatchAndOwnership();
  void originalCapture_motionCrossingUsesEnterDecision();
  void originalCapture_filterAndAmbiguityDenials();
  void originalCapture_negotiatedPacketsRetainRecipientAndStamp();
  void originalDispatch_captureRaceAndPackets();
  void originalDispatch_denialExpansionAndFiltered();
  void originalDispatch_ownersFocusAndReconnect();
  void originalDispatch_mixedOrderingAndCoordinates();
  void originalDispatch_timeoutOverflowAndFeatureDefault();
  void originalDispatch_reentrantFailureAndDestruction();
  void originalDispatch_pendingOrderingAndCaptureArrival();
  void originalDispatch_hoverCompressionPreservesBarriersAndHistory();
  void originalDispatch_inlineTerminalReservationContract();
  void originalDispatch_retainedLookaheadPreventsHoverReplacement();
  void originalDispatch_ineligibleInputWaitsForExactCleanup();
  void originalDispatch_legacyReleaseAndOtherRecipientsSurviveGate();
  void originalDispatch_closedGateSurvivesFlushAndTerminal();
  void originalDispatch_pendingRefusalAndRawRelease();
  void originalDispatch_pendingCancellationLifetime();
  void originalDispatch_pendingMalformedRepliesAndDeadline();
  void originalDispatch_pendingReentryAndLimit();
  void originalDispatch_pendingFrozenPointAndTicketReuse();
  void originalDispatch_pendingDragRevocation();
  void originalDispatch_missingUpsAndDragOwner();
  void originalDispatch_transitionAndReentrantOrdering();
  void originalDispatch_terminalAndPartialReleaseAccounting();
  void originalDispatch_cleanupCannotEraseNewLegacyOwner();
  void originalDispatch_flushDestructionAndDisconnect();
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
  void windowDragEdge_configuredMappingsKeepLocalOwner();
  void windowDragEdge_revokesOnReleaseAndTopology();
  void windowDragEdge_honorsDelaysCornersAndOriginalIdentity();
  void windowDragDrop_realReleaseBeforeCursorTransfer();

private:
  Arch m_arch;
  Log m_log;
#endif
};
