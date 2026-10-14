/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EventQueueTests.h"

#include "base/EventQueue.h"
#include "base/SimpleEventQueueBuffer.h"
#include "windowshare/InputEvidence.h"

#include <QTest>

#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>

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
};

namespace {
struct Evidence
{
  std::unique_ptr<InputEvidenceRecorder> recorder = std::make_unique<InputEvidenceRecorder>(InputEvidenceRole::Core);
  explicit Evidence(bool newProcess = true)
  {
    for (std::size_t i = 0; i != InputEvidenceRecorder::queueCount; ++i) {
      const auto queue = static_cast<InputEvidenceQueue>(i);
      if (!unboundedInputEvidenceQueue(queue))
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
      if (row->kind != InputEvidenceKind::Queue || !unboundedInputEvidenceQueue(row->queue))
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

QTEST_MAIN(EventQueueTests)
