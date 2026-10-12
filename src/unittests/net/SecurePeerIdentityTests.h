/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "arch/Arch.h"
#include "base/Log.h"
#include <QTemporaryDir>
#include <QTest>

class SecurePeerIdentityTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void authenticatedIdentity_data();
  void authenticatedIdentity();
  void activeClose();
  void plainStreamHasNoIdentity();

private:
  Arch m_arch;
  Log m_log;
  QTemporaryDir m_directory;
  QString m_hostCertificate;
  QString m_peerCertificate;
};
