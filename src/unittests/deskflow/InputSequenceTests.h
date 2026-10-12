/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QTest>

class InputSequenceTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void wireLayoutPreservesFullWidthCapture();
  void framingRejectsTruncationControlAndHistoricalRepeat();
  void receiptRequiresConsistentTerminalOutcome();
  void scopeSeparatesSubmissionFromDelivery();
  void nestedAndExhaustedScopesFail();
  void originScopeDoesNotLeak();
};
