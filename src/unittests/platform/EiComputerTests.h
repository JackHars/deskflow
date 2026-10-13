/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/Log.h"

#include <QTest>

class EiComputerTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void producerAndConsumerAgreeOnLayout();
  void freshSenderProcessesFirstBatchWithoutCutoff();

private:
  Arch m_arch;
  Log m_log;
};
