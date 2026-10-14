/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EiComputerTests.h"

#include "base/EventQueue.h"
#include "common/Settings.h"
#include "deskflow/AppUtil.h"
#include "platform/EiComputer.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QTemporaryDir>

#include <array>
#include <cstddef>
#include <memory>

std::array<std::size_t, 2> eiComputerPlatformLayout() noexcept;

namespace {
class TestAppUtil final : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }
  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }
  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};
} // namespace

void EiComputerTests::initTestCase()
{
  m_arch.init();
  m_log.setFilter(LogLevel::Level::Verbose);
}

void EiComputerTests::producerAndConsumerAgreeOnLayout()
{
  const auto producer = eiComputerPlatformLayout();
  QCOMPARE(sizeof(deskflow::EiComputer), producer[0]);
  QCOMPARE(alignof(deskflow::EiComputer), producer[1]);
}

void EiComputerTests::freshSenderProcessesFirstBatchWithoutCutoff()
{
  // Check before allocation so a missing transitive compile definition fails
  // deterministically, rather than corrupting the test process's heap.
  QCOMPARE(sizeof(deskflow::EiComputer), eiComputerPlatformLayout()[0]);
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  Settings::setSettingsFile(directory.filePath("core.ini"));
  Settings::setValue(Settings::Core::PreventSleep, false);
  Settings::setValue(Settings::Client::LanguageSync, false);

  // The actual sender connects only to this owned Unix listener. No desktop,
  // portal, provider, device, or native input authority is involved. An empty
  // first libei batch still reaches the production cutoff post-batch checks.
  // Deliberately leave the handshake unfinished through destruction: quitting
  // during startup must also be safe. Unpatched libei before 1.5 crashes here;
  // the experimental build rejects those releases instead of skipping this.
  QLocalServer listener;
  const auto path = directory.filePath("eis.sock");
  QVERIFY(listener.listen(path));
  const bool hadSocket = qEnvironmentVariableIsSet("LIBEI_SOCKET");
  const auto previousSocket = qgetenv("LIBEI_SOCKET");
  const auto restoreSocket = qScopeGuard([&] {
    if (hadSocket)
      qputenv("LIBEI_SOCKET", previousSocket);
    else
      qunsetenv("LIBEI_SOCKET");
  });
  QVERIFY(qputenv("LIBEI_SOCKET", path.toLocal8Bit()));

  TestAppUtil appUtil;
  EventQueue events;
  auto computer = std::make_unique<deskflow::EiComputer>(false, &events, false);
  QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), 1000);
  std::unique_ptr<QLocalSocket> connection(listener.nextPendingConnection());
  QVERIFY(connection);
  QVERIFY(events.dispatchEvent(Event(EventTypes::System, events.getSystemTarget())));
  QVERIFY(events.dispatchEvent(Event(EventTypes::System, events.getSystemTarget())));
  std::int32_t x = -1, y = -1, width = -1, height = -1;
  computer->getShape(x, y, width, height);
  QCOMPARE(x, 0);
  QCOMPARE(y, 0);
  QCOMPARE(width, 1);
  QCOMPARE(height, 1);
  computer.reset();
  QVERIFY(!events.dispatchEvent(Event(EventTypes::System, events.getSystemTarget())));
}

QTEST_MAIN(EiComputerTests)
