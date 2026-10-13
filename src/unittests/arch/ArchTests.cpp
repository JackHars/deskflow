/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "arch/Arch.h"

#include <QTest>

class ArchTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
#if !defined(Q_OS_WIN)
  void mainThreadOwner_repeatedConstructionReleasesItsReference()
  {
    // No worker or signal thread is started. LeakSanitizer also verifies that
    // each constructor's otherwise-unobservable initial reference is freed.
    for (int attempt = 0; attempt < 32; ++attempt) {
      QVERIFY(!ArchMultithreadPosix::getInstance());
      {
        ArchMultithreadPosix threads;
        QCOMPARE(ArchMultithreadPosix::getInstance(), &threads);
        const auto current = threads.newCurrentThread();
        const auto copy = threads.copyThread(current);
        QVERIFY(threads.isSameThread(current, copy));
        threads.closeThread(copy);
        threads.closeThread(current);
      }
      QVERIFY(!ArchMultithreadPosix::getInstance());
    }
  }
#endif
  void time_preservesFractionalSeconds()
  {
    const double t1 = Arch::time();
    QTest::qWait(20);
    const double t2 = Arch::time();
    const double elapsed = t2 - t1;

    // The old implementation could only advance in whole seconds.
    QVERIFY(elapsed > 0.0);
    QVERIFY(elapsed < 1.0);
  }
};

QTEST_GUILESS_MAIN(ArchTests)
#include "ArchTests.moc"
