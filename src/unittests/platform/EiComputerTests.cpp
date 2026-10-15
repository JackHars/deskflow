/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EiComputerTests.h"

#include "base/EventQueue.h"
#include "common/Settings.h"
#include "deskflow/AppUtil.h"
#include "platform/EiComputer.h"

#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QTemporaryDir>

#include <libeis.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <thread>

namespace deskflow {
struct EiComputerTestAccess
{
  static ei *context(EiComputer &computer)
  {
    return computer.m_ei;
  }
};
} // namespace deskflow

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

struct NativeBatchObservation
{
  unsigned seats = 0;
  bool connected = false, delay = false;
  static void log(ei *context, ei_log_priority, const char *message, ei_log_context *)
  {
    auto &self = *static_cast<NativeBatchObservation *>(ei_get_user_data(context));
    if (std::strstr(message, "queuing event type EI_EVENT_CONNECT"))
      self.connected = true;
    if (std::strstr(message, "queuing event type EI_EVENT_SEAT_ADDED")) {
      ++self.seats;
      if (self.delay) {
        self.delay = false;
        // Delay inside the actual library dispatch, before Core can consume
        // its newly queued event. No input device or compositor is involved.
        std::this_thread::sleep_for(deskflow::EventQueueBudget::maximumAge + std::chrono::milliseconds(100));
      }
    }
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

void EiComputerTests::freshSenderProcessesFirstBatchWithoutCutoff_data()
{
  QTest::addColumn<bool>("bounded");
  QTest::newRow("ordinary") << false;
  QTest::newRow("experimental-backlog") << true;
}

void EiComputerTests::freshSenderProcessesFirstBatchWithoutCutoff()
{
  QFETCH(bool, bounded);
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
  const auto budget = bounded ? std::make_shared<deskflow::EventQueueBudget>() : nullptr;
  EventQueue events(budget);
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
  if (bounded) {
    unsigned calls = 0;
    events.addHandler(EventTypes::ClientDisconnected, this, [&](const Event &) {
      ++calls;
      // This callback owns one slot; the actual EI posted-ID queue can
      // receive only the remaining slots before admission becomes terminal.
      for (std::uint64_t i = 1; i != deskflow::EventQueueBudget::capacity; ++i)
        events.addEvent(Event(EventTypes::ClientDisconnected, this));
      events.addEvent(Event(EventTypes::KeyStateKeyUp, this));
    });
    events.addEvent(Event(EventTypes::ClientDisconnected, this));
    QCOMPARE(events.loop(), 1);
    QCOMPARE(calls, 1U);
    QCOMPARE(budget->snapshot().peak, deskflow::EventQueueBudget::capacity);
    QCOMPARE(budget->snapshot().failure, deskflow::EventQueueBudget::Failure::Capacity);
  }
  computer.reset();
  QVERIFY(!events.dispatchEvent(Event(EventTypes::System, events.getSystemTarget())));
  if (bounded) {
    QCOMPARE(budget->snapshot().outstanding, std::uint64_t{0});
    QCOMPARE(budget->snapshot().admitted, budget->snapshot().retired);
  }
}

void EiComputerTests::nativeBatchBudget_data()
{
  QTest::addColumn<bool>("bounded");
  QTest::addColumn<unsigned>("count");
  QTest::addColumn<bool>("delay");
  const auto limit = unsigned(deskflow::EventQueueBudget::nativeBatchCapacity);
  QTest::newRow("ordinary-large-native-batch") << false << limit + 2 << false;
  QTest::newRow("experimental-at-limit") << true << limit << false;
  QTest::newRow("experimental-over-limit") << true << limit + 2 << false;
  QTest::newRow("expired-inside-native-dispatch") << true << 1U << true;
}

void EiComputerTests::nativeBatchBudget()
{
  QFETCH(bool, bounded);
  QFETCH(unsigned, count);
  QFETCH(bool, delay);
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  Settings::setSettingsFile(directory.filePath("core.ini"));
  Settings::setValue(Settings::Core::PreventSleep, false);
  Settings::setValue(Settings::Client::LanguageSync, false);
  std::unique_ptr<eis, decltype(&eis_unref)> server(eis_new(nullptr), eis_unref);
  QVERIFY(server);
  const auto path = directory.filePath("batch.sock").toLocal8Bit();
  QCOMPARE(eis_setup_backend_socket(server.get(), path.constData()), 0);
  const auto previousSocket = qgetenv("LIBEI_SOCKET");
  const bool hadSocket = qEnvironmentVariableIsSet("LIBEI_SOCKET");
  const auto restoreSocket = qScopeGuard([&] {
    if (hadSocket)
      qputenv("LIBEI_SOCKET", previousSocket);
    else
      qunsetenv("LIBEI_SOCKET");
  });
  QVERIFY(qputenv("LIBEI_SOCKET", path));
  std::unique_ptr<eis_client, decltype(&eis_client_unref)> client(nullptr, eis_client_unref);
  TestAppUtil appUtil;
  NativeBatchObservation observed;
  const auto budget = bounded ? std::make_shared<deskflow::EventQueueBudget>() : nullptr;
  EventQueue events(budget);
  auto computer = std::make_unique<deskflow::EiComputer>(false, &events, false);
  auto *context = deskflow::EiComputerTestAccess::context(*computer);
  ei_set_user_data(context, &observed);
  ei_log_set_priority(context, EI_LOG_PRIORITY_DEBUG);
  ei_log_set_handler(context, NativeBatchObservation::log);
  QElapsedTimer timeout;
  timeout.start();
  while (!observed.connected && timeout.elapsed() < 2000) {
    eis_dispatch(server.get());
    while (auto *event = eis_get_event(server.get())) {
      if (eis_event_get_type(event) == EIS_EVENT_CLIENT_CONNECT) {
        client.reset(eis_client_ref(eis_event_get_client(event)));
        eis_client_connect(client.get());
      }
      eis_event_unref(event);
    }
    QVERIFY(events.dispatchEvent(Event(EventTypes::System, events.getSystemTarget())));
    QTest::qWait(1);
  }
  QVERIFY(observed.connected);
  QVERIFY(client);
  // Queue real native objects through the owned EIS protocol. There are no
  // devices, input events, display access, or test-only production queues.
  for (unsigned i = 0; i < count; ++i) {
    const auto name = QByteArray("batch-seat-") + QByteArray::number(i);
    auto *seat = eis_client_new_seat(client.get(), name.constData());
    QVERIFY(seat);
    eis_seat_configure_capability(seat, EIS_DEVICE_CAP_POINTER);
    eis_seat_add(seat);
    eis_seat_unref(seat);
  }
  observed.delay = delay;
  if (!delay) {
    timeout.restart();
    while (observed.seats < count && timeout.elapsed() < 2000) {
      eis_dispatch(server.get());
      ei_dispatch(context);
      QTest::qWait(1);
    }
    // Debug observations establish the fixture's actual prefilled population;
    // they are not production queue accounting or an input-delivery receipt.
    QCOMPARE(observed.seats, count);
  }
  QVERIFY(events.dispatchEvent(Event(EventTypes::System, events.getSystemTarget())));
  auto *remaining = ei_peek_event(context);
  const bool nativeRemaining = remaining != nullptr;
  if (remaining)
    ei_event_unref(remaining);
  const bool overflow = bounded && count > deskflow::EventQueueBudget::nativeBatchCapacity;
  QCOMPARE(nativeRemaining, overflow || delay);
  if (budget) {
    using Failure = deskflow::EventQueueBudget::Failure;
    QCOMPARE(
        budget->snapshot().failure, delay      ? Failure::Deadline
                                    : overflow ? Failure::NativeBatchCapacity
                                               : Failure::Healthy
    );
    if (overflow || delay)
      QCOMPARE(events.loop(), 1);
  }
  // Destruction frees any unconsumed native tail. The failed session never
  // drains only part of a batch and then resumes forwarding or prefix ACKs.
  computer.reset();
  if (budget)
    QCOMPARE(budget->snapshot().outstanding, std::uint64_t{0});
}

QTEST_MAIN(EiComputerTests)
