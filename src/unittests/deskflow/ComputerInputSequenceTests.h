/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "arch/Arch.h"
#include "base/Log.h"

#include <QTest>

class ComputerInputSequenceTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void inputDispatch_data();
  void inputDispatch();
  void secureAttention_data();
  void secureAttention();
  void exceptionDoesNotBecomeSuccessfulSubmission_data();
  void exceptionDoesNotBecomeSuccessfulSubmission();
  void overflowFailsWithoutWrapping();
  void ordinaryUnscopedDispatchIsUnchanged();

private:
  Arch m_arch;
  Log m_log;
};
