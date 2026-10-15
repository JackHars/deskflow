/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EventQueueTests.h"

#include "base/EventQueue.h"
#include "base/EventQueueWatchdog.h"
#include "base/SimpleEventQueueBuffer.h"
#include "windowshare/InputEvidence.h"
#ifdef __APPLE__
#include "platform/OSXEventQueueBuffer.h"
#endif

#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTest>

#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace deskflow::windowshare;

struct EventQueueTestAccess
{
  // Only skip loop's blocking wait when exercising actual save/remove paths.
  // No test copies the queue's admission, transfer or dispatch implementation.
  static void ready(EventQueue &events)
  {
    *events.m_readyCondVar = true;
  }
  static std::size_t pending(const EventQueue &events)
  {
    return events.m_pending.size();
  }
  static std::size_t saved(const EventQueue &events)
  {
    return events.m_events.size();
  }
  static void expire(Event &event)
  {
    event.m_queueAdmission.m_entered -= deskflow::EventQueueBudget::maximumAge + std::chrono::seconds(1);
  }
  static void expireSaved(EventQueue &events)
  {
    for (auto &[id, event] : events.m_events)
      expire(event);
  }
  static auto entered(const deskflow::EventQueueAdmission &admission)
  {
    return admission.m_entered;
  }
  static bool
  claimExpiredDispatch(deskflow::EventQueueBudget &budget, deskflow::EventQueueBudget::Clock::time_point now)
  {
    return budget.claimExpiredDispatch(now);
  }
};

namespace {
struct Evidence
{
  std::unique_ptr<InputEvidenceRecorder> recorder = std::make_unique<InputEvidenceRecorder>(InputEvidenceRole::Core);
  explicit Evidence(bool newProcess = true)
  {
    for (std::size_t i = 0; i != InputEvidenceRecorder::queueCount; ++i) {
      const auto queue = static_cast<InputEvidenceQueue>(i);
      if (!eventInputEvidenceQueue(queue))
        recorder->initialize(queue, this, 256);
    }
    InputEvidenceOwner owner;
    owner.values.fill(1);
    owner.values[4] = 7;
    owner.identity.fill(7);
    recorder->bindOwner(owner);
    // Simulate independent process launches only after previous queue/token
    // lifetimes have ended. Production never resets this monotonic marker.
    if (newProcess)
      deskflow::eventInputQueueConstructed.store(false);
    (void)deskflow::installEventInputObserver(recorder->eventObserver());
  }
  ~Evidence()
  {
    deskflow::eventInputObserverInstalled.store(nullptr);
  }
  Event key(void *target, std::uint64_t sequence = 1) const
  {
    return Event(EventTypes::KeyStateKeyDown, target, nullptr, Event::EventFlags::NoFlags, {7, sequence, 1});
  }
  struct Counts
  {
    std::uint64_t peak = 0, added = 0, retired = 0, depth = 0;
  };
  std::array<Counts, 2> counts()
  {
    std::array<Counts, 2> result{};
    while (const auto row = recorder->pop()) {
      if (row->kind != InputEvidenceKind::Queue ||
          (row->queue != InputEvidenceQueue::EventOutstanding && row->queue != InputEvidenceQueue::EventDispatch))
        continue;
      auto &count = result[row->queue == InputEvidenceQueue::EventDispatch];
      count.peak = std::max(count.peak, row->depth);
      count.added = row->enqueued;
      count.retired = row->retired;
      count.depth = row->depth;
      if (row->capacity != 0)
        throw std::runtime_error("EventQueue was assigned a fictitious capacity");
    }
    return result;
  }
};
struct RefusingBuffer : SimpleEventQueueBuffer
{
  bool addEvent(uint32_t) override
  {
    return false;
  }
};
struct CountedData : EventData
{
  explicit CountedData(unsigned &count) : destroyed(count)
  {
  }
  ~CountedData() override
  {
    ++destroyed;
  }
  unsigned &destroyed;
};
struct SpuriousBuffer : SimpleEventQueueBuffer
{
  Type getEvent(Event &event, std::uint32_t &id) override
  {
    if (remaining) {
      --remaining;
      return Type::Unknown;
    }
    return SimpleEventQueueBuffer::getEvent(event, id);
  }
  unsigned remaining = 10000;
};
} // namespace

void EventQueueTests::initTestCase()
{
  m_arch.init();
}

void EventQueueTests::dispatchEvent_noHandler_returnsFalse()
{
  EventQueue events;

  QVERIFY(!events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
}

void EventQueueTests::dispatchEvent_noTypeHandler_dispatchesUnknownHandler()
{
  EventQueue events;
  bool fallbackCalled = false;
  events.addHandler(EventTypes::Unknown, this, [&fallbackCalled](const Event &) { fallbackCalled = true; });

  QVERIFY(events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
  QVERIFY(fallbackCalled);
}

void EventQueueTests::dispatchEvent_handlerRemovesItself_keepsHandlerAliveUntilReturn()
{
  EventQueue events;
  auto handlerLifetime = std::make_shared<int>(1);
  std::weak_ptr<int> handlerLifetimeObserver = handlerLifetime;
  bool handlerAliveAfterRemoval = false;

  events.addHandler(
      EventTypes::ClientDisconnected, this,
      [this, &events, &handlerLifetimeObserver, &handlerAliveAfterRemoval, handlerLifetime](const Event &) {
        events.removeHandler(EventTypes::ClientDisconnected, this);
        handlerAliveAfterRemoval = handlerLifetime != nullptr && !handlerLifetimeObserver.expired();
      }
  );
  handlerLifetime.reset();

  QVERIFY(events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
  QVERIFY(handlerAliveAfterRemoval);
  QVERIFY(handlerLifetimeObserver.expired());
}

void EventQueueTests::inputObservation_pendingSavedRetrievedAndMoves()
{
  {
    Evidence evidence;
    EventQueue events;
    QVERIFY(evidence.recorder->arm(180000000000ULL));
    unsigned calls = 0;
    events.addHandler(EventTypes::KeyStateKeyDown, this, [&](const Event &event) {
      ++calls;
      QCOMPARE(event.originalInputStamp(), (OriginalInputStamp{7, 1, 1}));
      QCOMPARE(EventQueueTestAccess::pending(events), std::size_t{0});
      // The remaining saved item is Quit; this input has already been removed.
      QCOMPARE(EventQueueTestAccess::saved(events), std::size_t{1});
      evidence.recorder->stop();
      evidence.recorder->finish();
      QCOMPARE(evidence.recorder->state(), InputEvidenceState::Draining);
    });
    events.addEvent(evidence.key(this));
    events.addEvent(Event(EventTypes::Quit));
    QCOMPARE(EventQueueTestAccess::pending(events), std::size_t{2});
    QCOMPARE(EventQueueTestAccess::saved(events), std::size_t{0});
    QCOMPARE(events.loop(), 0);
    QCOMPARE(calls, 1U);
    evidence.recorder->finish();
    QCOMPARE(evidence.recorder->state(), InputEvidenceState::Stopped);
    QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::None);
    for (const auto &count : evidence.counts()) {
      QCOMPARE(count.peak, std::uint64_t{1});
      QCOMPARE(count.added, std::uint64_t{1});
      QCOMPARE(count.retired, std::uint64_t{1});
      QCOMPARE(count.depth, std::uint64_t{0});
    }
  }
  {
    Evidence evidence;
    EventQueue events;
    EventQueueTestAccess::ready(events);
    QVERIFY(evidence.recorder->arm(180000000000ULL));
    events.addHandler(EventTypes::KeyStateKeyDown, this, [](const Event &) {});
    events.addEvent(evidence.key(this));
    QCOMPARE(EventQueueTestAccess::saved(events), std::size_t{1});
    Event retrieved;
    QVERIFY(events.getEvent(retrieved, 0));
    QCOMPARE(EventQueueTestAccess::saved(events), std::size_t{0});
    Event moved(std::move(retrieved));
    Event assigned;
    assigned = std::move(moved);
    evidence.recorder->stop();
    evidence.recorder->finish();
    QCOMPARE(evidence.recorder->state(), InputEvidenceState::Draining);
    QVERIFY(events.dispatchEvent(assigned));
    evidence.recorder->finish();
    QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::None);
    QCOMPARE(evidence.recorder->state(), InputEvidenceState::Stopped);
    for (const auto &count : evidence.counts()) {
      QCOMPARE(count.added, std::uint64_t{1});
      QCOMPARE(count.retired, std::uint64_t{1});
      QCOMPARE(count.peak, std::uint64_t{1});
    }
  }
}

void EventQueueTests::inputObservation_dispatchAndArmBoundaries()
{
  // Retrieval is not retirement. An event waiting in caller-owned storage
  // still prevents the empty baseline, even with no saved IDs in the queue.
  {
    Evidence evidence;
    EventQueue events;
    EventQueueTestAccess::ready(events);
    events.addHandler(EventTypes::KeyStateKeyDown, this, [](const Event &) {});
    events.addEvent(evidence.key(this));
    Event retrieved;
    QVERIFY(events.getEvent(retrieved, 0));
    QVERIFY(!evidence.recorder->readyOwner());
    QVERIFY(!evidence.recorder->arm(1000000000));
    QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::Baseline);
    QVERIFY(events.dispatchEvent(retrieved));
  }
  {
    Evidence evidence;
    EventQueue events;
    std::latch entered{1}, leave{1};
    events.addHandler(EventTypes::KeyStateKeyDown, this, [&](const Event &) {
      entered.count_down();
      leave.wait();
    });
    bool handled = false;
    std::thread worker([&] { handled = events.dispatchEvent(evidence.key(this)); });
    entered.wait();
    const bool ready = evidence.recorder->readyOwner().has_value();
    const bool armed = evidence.recorder->arm(1000000000);
    leave.count_down();
    worker.join();
    QVERIFY(handled);
    QVERIFY(!ready && !armed);
    QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::Baseline);
  }
  // A mutation already owned by arm never blocks the producer; it invalidates
  // the diagnostic run and ordinary dispatch still completes.
  {
    Evidence evidence;
    EventQueue events;
    bool called = false;
    events.addHandler(EventTypes::KeyStateKeyDown, this, [&](const Event &) { called = true; });
    QVERIFY(evidence.recorder->beginMutation(InputEvidenceQueue::EventOutstanding, &events));
    QVERIFY(events.dispatchEvent(evidence.key(this)));
    evidence.recorder->endMutation(InputEvidenceQueue::EventOutstanding, 0, 0);
    QVERIFY(called);
    QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::Contention);
  }
}

void EventQueueTests::inputObservation_concurrentPreparation()
{
  Evidence evidence;
  EventQueue events;
  constexpr unsigned workers = 8, rounds = 100;
  std::barrier inside(workers);
  events.addHandler(EventTypes::KeyStateKeyDown, this, [&](const Event &) { inside.arrive_and_wait(); });
  std::atomic<unsigned> handled{0};
  std::vector<std::thread> threads;
  for (unsigned worker = 0; worker < workers; ++worker)
    threads.emplace_back([&] {
      for (unsigned round = 0; round < rounds; ++round)
        handled += events.dispatchEvent(evidence.key(this));
    });
  for (auto &thread : threads)
    thread.join();
  QCOMPARE(handled.load(), workers * rounds);
  QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::None);
  QCOMPARE(evidence.recorder->written(), std::uint64_t{0});
  QVERIFY(evidence.recorder->readyOwner());
  QVERIFY(evidence.recorder->arm(1000000000));
  evidence.recorder->stop();
  evidence.recorder->finish();
  QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::None);
  QCOMPARE(evidence.recorder->state(), InputEvidenceState::Stopped);
}

void EventQueueTests::inputObservation_nestedAndDestructiveDispatch()
{
  {
    Evidence evidence;
    EventQueue events;
    QVERIFY(evidence.recorder->arm(180000000000ULL));
    unsigned depth = 0;
    events.addHandler(EventTypes::KeyStateKeyDown, this, [&](const Event &event) {
      if (++depth == 1)
        QVERIFY(events.dispatchEvent(event));
    });
    QVERIFY(events.dispatchEvent(evidence.key(this)));
    QCOMPARE(depth, 2U);
    evidence.recorder->stop();
    evidence.recorder->finish();
    QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::None);
    for (const auto &count : evidence.counts()) {
      QCOMPARE(count.peak, std::uint64_t{2});
      QCOMPARE(count.added, std::uint64_t{2});
      QCOMPARE(count.retired, std::uint64_t{2});
    }
  }
  {
    Evidence evidence;
    auto events = std::make_unique<EventQueue>();
    auto event = std::make_unique<Event>(evidence.key(this));
    QVERIFY(evidence.recorder->arm(180000000000ULL));
    events->addHandler(EventTypes::KeyStateKeyDown, this, [&](const Event &) {
      event.reset();
      events.reset();
    });
    QVERIFY(events->dispatchEvent(*event));
    QVERIFY(!events && !event);
    evidence.recorder->stop();
    evidence.recorder->finish();
    QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::None);
    for (const auto &count : evidence.counts())
      QCOMPARE(count.depth, std::uint64_t{0});
  }
}

void EventQueueTests::inputObservation_dropThrowAndAdoptBuffer()
{
  for (int kind = 0; kind != 7; ++kind) {
    Evidence evidence;
    auto events = std::make_unique<EventQueue>();
    QVERIFY(evidence.recorder->arm(180000000000ULL));
    if (kind == 0) {
      QVERIFY(!events->dispatchEvent(evidence.key(this)));
    } else if (kind == 1) {
      events->addHandler(EventTypes::KeyStateKeyDown, this, [](const Event &) { throw std::runtime_error("fixture"); });
      QVERIFY_THROWS_EXCEPTION(std::runtime_error, events->dispatchEvent(evidence.key(this)));
    } else if (kind == 2) {
      events->addEvent(evidence.key(this)); // pending destruction
      events.reset();
    } else {
      EventQueueTestAccess::ready(*events);
      if (kind == 3)
        events->adoptBuffer(new RefusingBuffer);
      events->addEvent(evidence.key(this));
      if (kind == 4)
        events->adoptBuffer(nullptr); // saved destruction
      if (kind == 5 || kind == 6) {
        Event retrieved;
        QVERIFY(events->getEvent(retrieved, 0));
        if (kind == 6)
          retrieved = Event(); // move assignment drops the owned lifetime
      }
    }
    QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::PipelineRevoked);
    evidence.recorder->stop();
    evidence.recorder->finish();
    QCOMPARE(evidence.recorder->state(), InputEvidenceState::Stopped);
    for (const auto &count : evidence.counts()) {
      QCOMPARE(count.depth, std::uint64_t{0});
      QCOMPARE(count.added, count.retired);
    }
    QVERIFY(evidence.recorder->drained());
  }
}

void EventQueueTests::inputObservation_stampMutationAndInstallation()
{
  for (bool duringCallback : {false, true}) {
    Evidence evidence;
    EventQueue events;
    EventQueueTestAccess::ready(events);
    QVERIFY(evidence.recorder->arm(180000000000ULL));
    events.addEvent(evidence.key(this));
    Event retrieved;
    QVERIFY(events.getEvent(retrieved, 0));
    events.addHandler(EventTypes::KeyStateKeyDown, this, [&](const Event &) {
      if (duringCallback)
        retrieved.setOriginalInputStamp({7, 2, 1});
    });
    if (!duringCallback)
      retrieved.setOriginalInputStamp({7, 2, 1});
    QVERIFY(events.dispatchEvent(retrieved));
    QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::PipelineRevoked);
    evidence.recorder->stop();
    evidence.recorder->finish();
    QCOMPARE(evidence.recorder->state(), InputEvidenceState::Stopped);
    for (const auto &count : evidence.counts()) {
      QCOMPARE(count.depth, std::uint64_t{0});
      QCOMPARE(count.added, count.retired);
    }
    QVERIFY(evidence.recorder->drained());
  }
  {
    EventQueue events;
    Evidence late(false);
    EventQueue afterInstallation;
    // Even a newly initialized queue cannot conceal the old unobserved one.
    QVERIFY(!late.recorder->readyOwner());
    QVERIFY(!late.recorder->arm(1000000000));
    events.addHandler(EventTypes::KeyStateKeyDown, this, [](const Event &) {});
    QVERIFY(events.dispatchEvent(late.key(this)));
    QCOMPARE(late.recorder->loss(), InputEvidenceLoss::QueueIdentity);
  }
  {
    Evidence first;
    EventQueue events;
    Evidence replaced(false);
    EventQueue afterReplacement;
    events.addHandler(EventTypes::KeyStateKeyDown, this, [](const Event &) {});
    QVERIFY(events.dispatchEvent(first.key(this)));
    QCOMPARE(first.recorder->loss(), InputEvidenceLoss::QueueIdentity);
    QCOMPARE(replaced.recorder->loss(), InputEvidenceLoss::QueueIdentity);
    deskflow::eventInputObserverInstalled.store(&first.recorder->eventObserver());
  }
  {
    Event unobserved;
    {
      EventQueue previous;
      EventQueueTestAccess::ready(previous);
      previous.addEvent(Event(EventTypes::KeyStateKeyDown, this, nullptr, 0, {7, 1, 1}));
      QVERIFY(previous.getEvent(unobserved, 0));
    }
    Evidence late(false);
    EventQueue replacement;
    QVERIFY(!late.recorder->readyOwner());
    QVERIFY(!late.recorder->arm(1000000000));
    QCOMPARE(late.recorder->loss(), InputEvidenceLoss::QueueIdentity);
  }
}

void EventQueueTests::inputObservation_disabledAndUnstamped()
{
  QVERIFY(!deskflow::eventInputObserverInstalled.load());
  {
    EventQueue events;
    unsigned calls = 0;
    events.addHandler(EventTypes::KeyStateKeyDown, this, [&](const Event &) { ++calls; });
    QVERIFY(events.dispatchEvent(Event(EventTypes::KeyStateKeyDown, this, nullptr, 0, {7, 1, 1})));
    QCOMPARE(calls, 1U);
  }
  Evidence evidence;
  EventQueue events;
  QVERIFY(evidence.recorder->arm(180000000000ULL));
  events.addHandler(EventTypes::KeyStateKeyDown, this, [](const Event &) {});
  events.addHandler(EventTypes::ClientDisconnected, this, [](const Event &) {});
  QVERIFY(events.dispatchEvent(Event(EventTypes::KeyStateKeyDown, this)));
  QVERIFY(events.dispatchEvent(Event(EventTypes::ClientDisconnected, this, nullptr, 0, {7, 1, 1})));
  evidence.recorder->stop();
  evidence.recorder->finish();
  QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::None);
  for (const auto &count : evidence.counts()) {
    QCOMPARE(count.added, std::uint64_t{0});
    QCOMPARE(count.retired, std::uint64_t{0});
  }
}

void EventQueueTests::backlog_pendingCapacityRevokesAndDisposes()
{
  unsigned revoked = 0, destroyed = 0;
  const auto budget = std::make_shared<deskflow::EventQueueBudget>([&] { ++revoked; });
  {
    EventQueue events(budget);
    for (std::uint64_t i = 0; i != deskflow::EventQueueBudget::capacity; ++i)
      events.addEvent(Event(EventTypes::ClientDisconnected, this, new CountedData(destroyed)));
    QCOMPARE(EventQueueTestAccess::pending(events), std::size_t(deskflow::EventQueueBudget::capacity));
    QVERIFY(budget->live());
    // Even a release cannot be silently dropped while continuing the session.
    events.addEvent(Event(EventTypes::KeyStateKeyUp, this, new CountedData(destroyed)));
    QCOMPARE(revoked, 1U);
    QCOMPARE(destroyed, 1U);
    QCOMPARE(budget->snapshot().failure, deskflow::EventQueueBudget::Failure::Capacity);
    bool routed = false;
    events.addHandler(EventTypes::ClientDisconnected, this, [&](const Event &) { routed = true; });
    QCOMPARE(events.loop(), 1);
    QVERIFY(!routed);
    // Teardown's nested loop cannot clear the fault and resume old input.
    QCOMPARE(events.loop(), 1);
  }
  const auto state = budget->snapshot();
  QCOMPARE(revoked, 1U);
  QCOMPARE(destroyed, unsigned(deskflow::EventQueueBudget::capacity + 1));
  QCOMPARE(state.peak, deskflow::EventQueueBudget::capacity);
  QCOMPARE(state.outstanding, std::uint64_t{0});
  QCOMPARE(state.admitted, state.retired);
}

void EventQueueTests::backlog_retrievedLifetimeStillConsumesCapacity()
{
  const auto budget = std::make_shared<deskflow::EventQueueBudget>();
  Event retrieved;
  {
    EventQueue events(budget);
    EventQueueTestAccess::ready(events);
    events.addEvent(Event(EventTypes::PrimaryComputerButtonDown, this));
    QVERIFY(events.getEvent(retrieved, 0));
    QCOMPARE(EventQueueTestAccess::saved(events), std::size_t{0});
    QCOMPARE(budget->snapshot().outstanding, std::uint64_t{1});
    Event moved(std::move(retrieved));
    retrieved = std::move(moved);
    for (std::uint64_t i = 1; i != deskflow::EventQueueBudget::capacity; ++i)
      events.addEvent(Event(EventTypes::ClientDisconnected, this));
    events.addEvent(Event(EventTypes::PrimaryComputerButtonUp, this));
    QVERIFY(!budget->live());
    QVERIFY(!events.dispatchEvent(retrieved));
  }
  QCOMPARE(budget->snapshot().outstanding, std::uint64_t{1});
  retrieved = Event();
  QCOMPARE(budget->snapshot().outstanding, std::uint64_t{0});
  QCOMPARE(budget->snapshot().admitted, budget->snapshot().retired);
}

void EventQueueTests::backlog_nestedAndDestructiveDispatch()
{
  const auto budget = std::make_shared<deskflow::EventQueueBudget>();
  auto events = std::make_unique<EventQueue>(budget);
  auto event = std::make_unique<Event>(EventTypes::KeyStateKeyDown, this);
  unsigned calls = 0;
  events->addHandler(EventTypes::KeyStateKeyDown, this, [&](const Event &borrowed) {
    if (++calls == 1) {
      QVERIFY(events->dispatchEvent(borrowed));
      QCOMPARE(budget->snapshot().outstanding, std::uint64_t{1});
    } else {
      QCOMPARE(budget->snapshot().outstanding, std::uint64_t{2});
      event.reset();
      events.reset();
    }
  });
  QVERIFY(events->dispatchEvent(*event));
  QCOMPARE(calls, 2U);
  QCOMPARE(budget->snapshot().outstanding, std::uint64_t{0});
  QCOMPARE(budget->snapshot().peak, std::uint64_t{2});
  QVERIFY(budget->live());
}

void EventQueueTests::backlog_expiredAndForeignEventsAreTerminal()
{
  for (bool foreign : {false, true}) {
    const auto budget = std::make_shared<deskflow::EventQueueBudget>();
    const auto other = std::make_shared<deskflow::EventQueueBudget>();
    EventQueue events(budget), destination(other);
    EventQueueTestAccess::ready(events);
    events.addEvent(Event(EventTypes::KeyStateKeyDown, this));
    Event retrieved;
    QVERIFY(events.getEvent(retrieved, 0));
    bool called = false;
    events.addHandler(EventTypes::KeyStateKeyDown, this, [&](const Event &) { called = true; });
    destination.addHandler(EventTypes::KeyStateKeyDown, this, [&](const Event &) { called = true; });
    if (foreign) {
      QVERIFY(!destination.dispatchEvent(retrieved));
      QCOMPARE(other->snapshot().failure, deskflow::EventQueueBudget::Failure::ForeignQueue);
    } else {
      EventQueueTestAccess::expire(retrieved);
      QVERIFY(!events.dispatchEvent(retrieved));
    }
    QVERIFY(!called);
    QCOMPARE(
        budget->snapshot().failure,
        foreign ? deskflow::EventQueueBudget::Failure::ForeignQueue : deskflow::EventQueueBudget::Failure::Deadline
    );
    retrieved = Event();
    QCOMPARE(budget->snapshot().outstanding, std::uint64_t{0});
    QCOMPARE(events.loop(), 1);
  }
}

void EventQueueTests::backlog_platformRefusalAndConcurrentProducers()
{
  {
    const auto budget = std::make_shared<deskflow::EventQueueBudget>();
    EventQueue events(budget);
    EventQueueTestAccess::ready(events);
    events.adoptBuffer(new RefusingBuffer);
    events.addEvent(Event(EventTypes::KeyStateKeyUp, this));
    QCOMPARE(budget->snapshot().failure, deskflow::EventQueueBudget::Failure::PlatformBuffer);
    QCOMPARE(budget->snapshot().outstanding, std::uint64_t{0});
    QCOMPARE(events.loop(), 1);
  }
  std::atomic<unsigned> revoked{0};
  const auto budget = std::make_shared<deskflow::EventQueueBudget>([&] { ++revoked; });
  {
    EventQueue events(budget);
    std::vector<std::thread> producers;
    for (int i = 0; i != 4; ++i)
      producers.emplace_back([&] {
        for (std::uint64_t n = 0; n != deskflow::EventQueueBudget::capacity; ++n)
          events.addEvent(Event(EventTypes::ClientDisconnected, this));
      });
    for (auto &producer : producers)
      producer.join();
    QVERIFY(!budget->live());
    QVERIFY(budget->snapshot().peak <= deskflow::EventQueueBudget::capacity);
    QCOMPARE(revoked.load(), 1U);
    QCOMPARE(events.loop(), 1);
  }
  QCOMPARE(budget->snapshot().outstanding, std::uint64_t{0});
  QCOMPARE(budget->snapshot().admitted, budget->snapshot().retired);
}

void EventQueueTests::backlog_controlAndConfiguredEventsAreMeasured()
{
  Evidence evidence;
  const auto budget = std::make_shared<deskflow::EventQueueBudget>();
  EventQueue events(budget);
  QVERIFY(evidence.recorder->arm(180000000000ULL));
  events.addHandler(EventTypes::KeyStateKeyDown, this, [](const Event &) {});
  events.addHandler(EventTypes::ClientDisconnected, this, [](const Event &) {});
  QVERIFY(events.dispatchEvent(Event(EventTypes::KeyStateKeyDown, this)));
  QVERIFY(events.dispatchEvent(evidence.key(this)));
  QVERIFY(events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
  evidence.recorder->stop();
  evidence.recorder->finish();
  QCOMPARE(evidence.recorder->loss(), InputEvidenceLoss::None);
  std::array<std::uint64_t, 4> added{};
  while (const auto row = evidence.recorder->pop()) {
    if (row->kind == InputEvidenceKind::Queue && eventInputEvidenceQueue(row->queue)) {
      const auto dispatch =
          row->queue == InputEvidenceQueue::EventDispatch || row->queue == InputEvidenceQueue::EventAllDispatch;
      QCOMPARE(
          row->capacity, dispatch ? deskflow::EventQueueBudget::dispatchCapacity : deskflow::EventQueueBudget::capacity
      );
      const auto index = std::size_t(row->queue) - std::size_t(InputEvidenceQueue::EventOutstanding);
      added[index] = row->enqueued;
    }
  }
  QCOMPARE(added, (std::array<std::uint64_t, 4>{1, 1, 3, 3}));
}

void EventQueueTests::backlog_timersAndSpuriousWakeupsMakeProgress()
{
  {
    EventQueue events(std::make_shared<deskflow::EventQueueBudget>());
    const auto timer = events.newOneShotTimer(0.01, this);
    events.addHandler(EventTypes::Timer, this, [&](const Event &) { events.addEvent(Event(EventTypes::Quit)); });
    events.addHandler(EventTypes::ClientDisconnected, this, [&](const Event &) {
      events.addEvent(Event(EventTypes::ClientDisconnected, this));
    });
    events.addEvent(Event(EventTypes::ClientDisconnected, this));
    QCOMPARE(events.loop(), 0);
    events.deleteTimer(timer);
    QVERIFY(events.queueBudget()->live());
  }
  {
    EventQueue events(std::make_shared<deskflow::EventQueueBudget>());
    events.adoptBuffer(new SpuriousBuffer);
    events.addEvent(Event(EventTypes::Quit));
    QCOMPARE(events.loop(), 0);
    QVERIFY(events.queueBudget()->live());
  }
}

void EventQueueTests::backlog_ordinaryQueueKeepsAdmissionPolicy()
{
  EventQueue events;
  QVERIFY(!events.queueBudget());
  for (std::uint64_t i = 0; i != deskflow::EventQueueBudget::capacity + 1; ++i)
    events.addEvent(Event(EventTypes::ClientDisconnected, this));
  QCOMPARE(EventQueueTestAccess::pending(events), std::size_t(deskflow::EventQueueBudget::capacity + 1));
}

void EventQueueTests::backlog_recursiveDispatchHasItsOwnLimit()
{
  const auto budget = std::make_shared<deskflow::EventQueueBudget>();
  EventQueue events(budget);
  unsigned called = 0;
  events.addHandler(EventTypes::KeyStateKeyDown, this, [&](const Event &event) {
    ++called;
    (void)events.dispatchEvent(event);
  });
  QVERIFY(events.dispatchEvent(Event(EventTypes::KeyStateKeyDown, this)));
  QCOMPARE(called, unsigned(deskflow::EventQueueBudget::dispatchCapacity));
  QCOMPARE(budget->snapshot().failure, deskflow::EventQueueBudget::Failure::DispatchCapacity);
  QCOMPARE(budget->snapshot().dispatchPeak, deskflow::EventQueueBudget::dispatchCapacity);
  QCOMPARE(budget->snapshot().dispatching, std::uint64_t{0});
  QCOMPARE(budget->snapshot().outstanding, std::uint64_t{0});
  QCOMPARE(budget->snapshot().admitted, budget->snapshot().retired);
}

void EventQueueTests::backlog_expiredRetrievalDisposesPayload()
{
  const auto budget = std::make_shared<deskflow::EventQueueBudget>();
  unsigned destroyed = 0;
  EventQueue events(budget);
  EventQueueTestAccess::ready(events);
  events.addEvent(Event(EventTypes::KeyStateKeyUp, this, new CountedData(destroyed)));
  EventQueueTestAccess::expireSaved(events);
  Event received;
  QVERIFY(events.getEvent(received, 0));
  QCOMPARE(received.getType(), EventTypes::Quit);
  QCOMPARE(destroyed, 1U);
  QCOMPARE(budget->snapshot().failure, deskflow::EventQueueBudget::Failure::Deadline);
  QCOMPARE(budget->snapshot().outstanding, std::uint64_t{0});
}

void EventQueueTests::backlog_dispatchDeadlinesFollowAdmissionAndMoves()
{
  using Budget = deskflow::EventQueueBudget;
  const auto budget = std::make_shared<Budget>();
  deskflow::EventQueueAdmission first;
  QVERIFY(first.admit(budget));
  const auto firstDeadline = EventQueueTestAccess::entered(first) + Budget::maximumAge;
  QVERIFY(first.beginDispatch());
  QVERIFY(!EventQueueTestAccess::claimExpiredDispatch(*budget, firstDeadline));
  auto moved = std::move(first);
  QVERIFY(!EventQueueTestAccess::claimExpiredDispatch(*budget, firstDeadline));
  moved = {}; // Retire the first slot before reusing it with a later admission.
  QTest::qSleep(2);
  deskflow::EventQueueAdmission next;
  QVERIFY(next.admit(budget));
  QVERIFY(next.beginDispatch());
  QVERIFY(!EventQueueTestAccess::claimExpiredDispatch(*budget, firstDeadline + Budget::Clock::duration{1}));
  // Expiration uses admission time, including any wait before beginDispatch.
  QVERIFY(
      EventQueueTestAccess::claimExpiredDispatch(
          *budget, EventQueueTestAccess::entered(next) + Budget::maximumAge + Budget::Clock::duration{1}
      )
  );
  next = {};
  QVERIFY(!EventQueueTestAccess::claimExpiredDispatch(*budget, Budget::Clock::now() + std::chrono::hours(1)));
  QCOMPARE(budget->snapshot().outstanding, std::uint64_t{0});
  QCOMPARE(budget->snapshot().dispatching, std::uint64_t{0});
  QCOMPARE(budget->snapshot().failure, Budget::Failure::Healthy);
}

namespace {
// Only called in this test executable's directly owned QProcess. It never
// starts Core, a Host, an input source, a native window or a desktop connection.
int runWatchdogFixture(const QString &mode)
{
  using Budget = deskflow::EventQueueBudget;
  const auto block = [] {
    for (;;)
      std::this_thread::sleep_for(std::chrono::seconds(1));
  };
  const bool ordinary = mode == QStringLiteral("ordinary");
  const auto budget =
      ordinary ? nullptr
               : std::make_shared<Budget>(mode == QStringLiteral("blocked") ? block : std::function<void()>{});
  auto watchdog = std::make_unique<deskflow::EventQueueWatchdog>(budget);
  auto events = std::make_unique<EventQueue>(budget);
  int outer = 0, inner = 0;
  const auto dispatch = [&](int *target) {
    return events->dispatchEvent(Event(EventTypes::ClientDisconnected, target));
  };
  events->addHandler(EventTypes::ClientDisconnected, &inner, [](const Event &) {});
  const auto churn = [&] {
    for (;;) {
      if (!dispatch(&inner))
        std::_Exit(10);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  events->addHandler(EventTypes::ClientDisconnected, &outer, [&](const Event &) {
    std::puts("watchdog-fixture-entered");
    std::fflush(stdout);
    if (mode == QStringLiteral("blocked") || mode == QStringLiteral("concurrent"))
      block();
    else if (mode == QStringLiteral("nested"))
      churn();
    else {
      if (mode == QStringLiteral("cancelled"))
        watchdog.reset();
      std::this_thread::sleep_for(Budget::maximumAge + std::chrono::milliseconds(200));
    }
  });
  if (mode == QStringLiteral("concurrent")) {
    std::thread stalled([&] { (void)dispatch(&outer); });
    churn();
    stalled.join();
  } else if (mode == QStringLiteral("healthy")) {
    // An idle queue and repeated new callbacks may each live longer than an
    // individual lease. Exceptions/queue destruction must leave no stale lease.
    std::this_thread::sleep_for(Budget::maximumAge + std::chrono::milliseconds(200));
    const auto end = Budget::Clock::now() + Budget::maximumAge + std::chrono::milliseconds(200);
    do {
      if (!dispatch(&inner))
        return 11;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (Budget::Clock::now() < end);
    events->addHandler(EventTypes::ClientDisconnected, &inner, [](const Event &) {
      throw std::runtime_error("fixture");
    });
    try {
      (void)dispatch(&inner);
      return 12;
    } catch (const std::runtime_error &) {
    }
    events->addHandler(EventTypes::ClientDisconnected, &inner, [&](const Event &) { events.reset(); });
    if (!dispatch(&inner))
      return 13;
    std::this_thread::sleep_for(Budget::maximumAge + std::chrono::milliseconds(200));
    const auto counts = budget->snapshot();
    if (!budget->live() || counts.outstanding || counts.dispatching || counts.admitted != counts.retired)
      return 14;
  } else if (!dispatch(&outer)) {
    return 15;
  }
  if (mode == QStringLiteral("cancelled") && budget->snapshot().failure != Budget::Failure::Deadline)
    return 16;
  return 0;
}
} // namespace

void EventQueueTests::watchdog_processDeadlines_data()
{
  QTest::addColumn<QString>("mode");
  QTest::addColumn<int>("expectedExit");
  for (const auto *mode : {"blocked", "nested", "concurrent"})
    QTest::newRow(mode) << QString::fromLatin1(mode) << deskflow::EventQueueWatchdog::timeoutExitCode;
  for (const auto *mode : {"healthy", "cancelled", "ordinary"})
    QTest::newRow(mode) << QString::fromLatin1(mode) << 0;
}

void EventQueueTests::watchdog_processDeadlines()
{
  QFETCH(QString, mode);
  QFETCH(int, expectedExit);
  if (qEnvironmentVariable("DESKFLOW_TEST_EVENT_WATCHDOG_CHILD") == QStringLiteral("1"))
    std::_Exit(runWatchdogFixture(mode));
  QProcess child;
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("DESKFLOW_TEST_EVENT_WATCHDOG_CHILD"), QStringLiteral("1"));
  child.setProcessEnvironment(environment);
  child.setProcessChannelMode(QProcess::MergedChannels);
  QElapsedTimer elapsed;
  elapsed.start();
  child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("watchdog_processDeadlines:") + mode});
  QVERIFY2(child.waitForStarted(5000), qPrintable(child.errorString()));
  // QProcess owns and kills only this child on an assertion/timeout. Every
  // expiration case uses the real production two-second residence deadline.
  const auto finished = child.waitForFinished(12000);
  const auto output = child.readAll();
  QVERIFY2(finished, output.constData());
  QCOMPARE(child.exitStatus(), QProcess::NormalExit);
  QVERIFY2(child.exitCode() == expectedExit, output.constData());
  if (expectedExit == deskflow::EventQueueWatchdog::timeoutExitCode) {
    QVERIFY(output.contains("watchdog-fixture-entered"));
    QVERIFY(elapsed.elapsed() >= 2000);
  }
}

#ifdef __APPLE__
void EventQueueTests::backlog_nativeMacBufferRetainsTheBound()
{
  const auto budget = std::make_shared<deskflow::EventQueueBudget>();
  EventQueue events(budget);
  auto *buffer = new OSXEventQueueBuffer(&events);
  events.adoptBuffer(buffer);
  EventQueueTestAccess::ready(events);
  events.addHandler(EventTypes::ClientDisconnected, this, [](const Event &) {});
  for (unsigned i = 0; i != 16; ++i) {
    events.addEvent(Event(EventTypes::ClientDisconnected, this));
    Event received;
    QVERIFY(events.getEvent(received, 0));
    QVERIFY(events.dispatchEvent(received));
  }
  QVERIFY(buffer->isEmpty());
  for (std::uint64_t i = 0; i != deskflow::EventQueueBudget::capacity; ++i)
    events.addEvent(Event(EventTypes::ClientDisconnected, this));
  events.addEvent(Event(EventTypes::KeyStateKeyUp, this));
  QCOMPARE(budget->snapshot().failure, deskflow::EventQueueBudget::Failure::Capacity);
  // Inspect the actual posted-ID queue after terminal failure. Destruction
  // still owns the corresponding saved Events and retires their permits.
  unsigned posted = 0;
  while (!buffer->isEmpty()) {
    Event unused;
    std::uint32_t id = 0;
    QCOMPARE(buffer->getEvent(unused, id), IEventQueueBuffer::Type::User);
    ++posted;
  }
  QCOMPARE(posted, unsigned(deskflow::EventQueueBudget::capacity));
  QCOMPARE(events.loop(), 1);
}
#endif

QTEST_MAIN(EventQueueTests)
