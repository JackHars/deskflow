/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "InputEvidenceRuntime.h"
#include "OriginalInputCapturedInbox.h"
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace deskflow::windowshare;
namespace deskflow::windowshare {
struct InputEvidenceTestAccess
{
  template <class F> static void collect(InputEvidenceRuntime &runtime, F &&action)
  {
    std::exception_ptr failure;
    QMetaObject::invokeMethod(
        &runtime.m_collector,
        [&] {
          try {
            action();
          } catch (...) {
            failure = std::current_exception();
          }
        },
        Qt::BlockingQueuedConnection
    );
    if (failure)
      std::rethrow_exception(failure);
  }
  static auto open(const QString &path, InputEvidenceRole role, std::uint64_t timeout = 180000, bool runTimer = false)
  {
    auto runtime = InputEvidenceRuntime::open(path, role, timeout);
    if (!runTimer)
      stopControlTimer(*runtime);
    return runtime;
  }
  static void preparationElapsed(InputEvidenceRuntime &runtime, std::uint64_t elapsedMs)
  {
    collect(runtime, [&] {
      runtime.m_created = inputEvidenceNow() - elapsedMs * 1000000;
      runtime.m_nextReadiness = 0;
      runtime.tick();
    });
  }
  static void tick(InputEvidenceRuntime &runtime)
  {
    collect(runtime, [&] { runtime.tick(); });
  }
  template <class F> static void firstSlice(InputEvidenceRuntime &runtime, F &&observe)
  {
    collect(runtime, [&] {
      runtime.drain();
      observe(runtime.m_rows, &runtime.m_collector);
    });
  }
  static void stopControlTimer(InputEvidenceRuntime &runtime)
  {
    collect(runtime, [&] { runtime.m_timer.stop(); });
  }
  static bool drained(InputEvidenceRuntime &runtime)
  {
    bool result = false;
    collect(runtime, [&] { result = runtime.m_recorder->drained(); });
    return result;
  }
  static void armWaitElapsed(InputEvidenceRuntime &runtime, std::uint64_t elapsedMs)
  {
    collect(runtime, [&] {
      runtime.m_armRequestedAt = inputEvidenceNow() - elapsedMs * 1000000;
      runtime.tick();
    });
  }
  template <class F> static void queueCollector(InputEvidenceRuntime &runtime, F &&action)
  {
    QMetaObject::invokeMethod(&runtime.m_collector, std::forward<F>(action), Qt::QueuedConnection);
  }
  static std::uint64_t depth(const InputEvidenceRecorder &recorder, InputEvidenceQueue queue)
  {
    return recorder.m_queues[static_cast<std::size_t>(queue)].depth.load();
  }
  static bool lockBoundary(InputEvidenceRecorder &recorder)
  {
    return recorder.lockCaptureBoundary();
  }
  static bool unlockBoundary(InputEvidenceRecorder &recorder)
  {
    return recorder.unlockCaptureBoundary();
  }
  static void append(InputEvidenceRecorder &recorder, const InputEvidenceRow &row)
  {
    recorder.append(row);
  }
  static auto reserveRow(InputEvidenceRecorder &recorder)
  {
    return recorder.reserveRow();
  }
  static void publishRow(InputEvidenceRecorder &recorder, std::uint64_t ticket, const InputEvidenceRow &row)
  {
    auto &slot = recorder.m_rows[ticket % InputEvidenceRecorder::capacity];
    slot.row = row;
    slot.ready.store(ticket + 1, std::memory_order_release);
  }
  static void eventCounts(InputEvidenceRecorder &recorder, InputEvidenceQueue queue, std::uint64_t counts)
  {
    recorder.m_queues[static_cast<std::size_t>(queue)].eventCounts.store(counts);
  }
};
} // namespace deskflow::windowshare
namespace {
// Keep the full contention workload without spinning between its phases.
// Avoid depending on library spin/backoff scheduling across hundreds of
// rendezvous on oversubscribed CI workers. The unchanged suite deadline still
// detects a recorder hang, with the same producer counts and phase workload.
class BlockingTestBarrier final
{
public:
  explicit BlockingTestBarrier(std::size_t participants) : m_participants(participants), m_remaining(participants)
  {
  }
  void arrive_and_wait()
  {
    std::unique_lock lock(m_mutex);
    const auto generation = m_generation;
    if (--m_remaining == 0) {
      m_remaining = m_participants;
      ++m_generation;
      lock.unlock();
      m_changed.notify_all();
    } else {
      m_changed.wait(lock, [&] { return m_generation != generation; });
    }
  }

private:
  const std::size_t m_participants;
  std::size_t m_remaining;
  std::uint64_t m_generation = 0;
  std::mutex m_mutex;
  std::condition_variable m_changed;
};

class QuitGuardApplication final : public QCoreApplication
{
public:
  using QCoreApplication::QCoreApplication;
  unsigned quitRequests = 0;

protected:
  bool event(QEvent *event) override
  {
    if (event->type() == QEvent::Quit) {
      ++quitRequests;
      return true; // Model the native fixture's deliberate manual-close veto.
    }
    return QCoreApplication::event(event);
  }
};

void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void runCase(const char *name, F &&function)
{
  std::fprintf(stderr, "[input-evidence] %s started\n", name);
  std::fflush(stderr);
  const auto start = std::chrono::steady_clock::now();
  try {
    function();
  } catch (...) {
    std::fprintf(stderr, "[input-evidence] %s failed\n", name);
    std::fflush(stderr);
    throw;
  }
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::fprintf(stderr, "[input-evidence] %s completed in %.3fs\n", name, elapsed);
  std::fflush(stderr);
}
template <class F> void denied(F &&function)
{
  bool caught = false;
  try {
    function();
  } catch (const std::exception &) {
    caught = true;
  }
  check(caught, "invalid private evidence configuration accepted");
}
InputEvidenceOwner owner()
{
  InputEvidenceOwner result;
  result.values.fill(1);
  result.values[4] = 5;
  result.identity.fill(7);
  return result;
}
std::unique_ptr<InputEvidenceRecorder> core(bool initialize = true)
{
  auto result = std::make_unique<InputEvidenceRecorder>(InputEvidenceRole::Core);
  if (initialize)
    for (std::size_t i = 0; i != InputEvidenceRecorder::queueCount; ++i)
      result->initialize(
          static_cast<InputEvidenceQueue>(i), result.get(),
          eventInputEvidenceQueue(static_cast<InputEvidenceQueue>(i)) ? 0 : 256
      );
  result->bindOwner(owner());
  return result;
}
OriginalInputRecord key(std::uint64_t sequence = 1)
{
  return {
      5, sequence, 1000 + sequence, 0,
      OriginalInputKey{42, 0, OriginalKeyKind::Down, OriginalKeyTransition::Down, false}
  };
}
void baselinesAndLifecycle()
{
  check(inputEvidence() == nullptr, "ordinary process unexpectedly has a recorder");
  auto missing = core(false);
  check(!missing->readyOwner() && !missing->arm(1000000000), "unobserved queue baseline armed");
  auto recorder = core();
  recorder->source(key());
  check(recorder->written() == 0, "payload retained before arm");
  check(recorder->beginMutation(InputEvidenceQueue::Capture, recorder.get()), "queue lock failed");
  recorder->endMutation(InputEvidenceQueue::Capture, 1, 0);
  check(!recorder->readyOwner(), "nonempty queue reported ready");
  check(recorder->beginMutation(InputEvidenceQueue::Capture, recorder.get()), "queue lock failed");
  recorder->endMutation(InputEvidenceQueue::Capture, 0, 1);
  check(recorder->readyOwner().has_value() && recorder->arm(180000000000ULL), "empty exact owner failed to arm");
  recorder->source(key());
  recorder->source(key(2));
  recorder->stop();
  recorder->finish();
  check(
      recorder->loss() == InputEvidenceLoss::None && recorder->state() == InputEvidenceState::Stopped,
      "clean stopped run lost evidence"
  );
  const auto written = recorder->written();
  recorder->source(key(3));
  check(recorder->written() == written && !recorder->arm(1), "stopped run recorded or rearmed");
  std::size_t sourceCount = 0, queueCount = 0;
  while (const auto row = recorder->pop()) {
    if (row->kind == InputEvidenceKind::Source) {
      ++sourceCount;
      check(
          row->sequence == sourceCount && row->raw.timestamp == 1000 + sourceCount,
          "source identity or native timestamp changed"
      );
    } else {
      ++queueCount;
      check(row->depth == 0 && row->enqueued == 0 && row->retired == 0, "pre-arm counts leaked into baseline");
    }
  }
  check(
      sourceCount == 2 && queueCount == 2 * InputEvidenceRecorder::queueCount && recorder->drained(),
      "incomplete source/queue boundaries"
  );
}
void readinessDiagnostics()
{
  const auto allQueues = [](const InputEvidenceReadiness &detail, std::uint8_t blocked) {
    return std::all_of(detail.queues.begin(), detail.queues.end(), [blocked](std::uint8_t value) {
      return value == blocked;
    });
  };
  InputEvidenceReadiness detail;
  auto missing = core(false);
  check(
      !missing->readyOwner(&detail) && detail.inspected && detail.ownerLive && detail.captureIdle &&
          allQueues(detail, 1),
      "missing queue readiness was not diagnosed"
  );
  auto recorder = core();
  recorder->revokeOwner();
  check(
      !recorder->readyOwner(&detail) && detail.inspected && !detail.ownerLive && detail.captureIdle &&
          allQueues(detail, 0),
      "missing live owner was not diagnosed"
  );
  recorder->bindOwner(owner());
  recorder->beginCaptureActivity();
  check(!recorder->readyOwner(&detail) && !detail.captureIdle, "capture activity was not diagnosed");
  recorder->endCaptureActivity();
  constexpr auto queue = InputEvidenceQueue::ServerPending;
  constexpr auto index = static_cast<std::size_t>(queue);
  check(recorder->beginQueueActivity(queue, recorder.get()), "queue activity failed");
  check(recorder->beginMutation(queue, recorder.get()), "queue lock failed");
  check(!recorder->readyOwner(&detail) && detail.queues[index] == 6, "queue activity and mutation were not diagnosed");
  recorder->endMutation(queue, 1, 0);
  recorder->endQueueActivity(queue);
  check(!recorder->readyOwner(&detail) && detail.queues[index] == 8, "nonempty queue was not diagnosed");
  check(recorder->beginMutation(queue, recorder.get()), "queue lock failed");
  recorder->endMutation(queue, 0, 1);
  check(
      recorder->readyOwner(&detail) && detail.inspected && detail.ownerLive && detail.captureIdle &&
          allQueues(detail, 0) && recorder->written() == 0 && recorder->loss() == InputEvidenceLoss::None,
      "diagnostics changed readiness or invented measured rows"
  );
  check(recorder->arm(1000000000), "diagnostic sampling prevented arm");
  check(!recorder->readyOwner(&detail) && !detail.inspected, "recording reused stale readiness diagnostics");
  recorder->stop();
  recorder->finish();
}
void concurrentEventPreparation()
{
  auto recorder = core();
  const auto &observer = recorder->eventObserver();
  using Action = deskflow::EventInputObservation;
  constexpr std::array eventQueues{
      InputEvidenceQueue::EventOutstanding, InputEvidenceQueue::EventDispatch, InputEvidenceQueue::EventAllOutstanding,
      InputEvidenceQueue::EventAllDispatch
  };
  constexpr unsigned workers = 8, rounds = 200;
  BlockingTestBarrier phase(workers + 1);
  const auto observe = [&](Action action) { observer.observe(observer.context, recorder.get(), {5, 1, 1}, action); };
  std::vector<std::thread> threads;
  for (unsigned worker = 0; worker < workers; ++worker)
    threads.emplace_back([&] {
      for (unsigned round = 0; round < rounds; ++round) {
        phase.arrive_and_wait();
        for (const auto action : {Action::AllAdmit, Action::Admit, Action::AllDispatch, Action::Dispatch})
          observe(action);
        phase.arrive_and_wait();
        phase.arrive_and_wait();
        for (const auto action : {Action::Return, Action::AllReturn, Action::Retire, Action::AllRetire})
          observe(action);
        phase.arrive_and_wait();
      }
    });
  bool exact = true;
  for (unsigned round = 0; round < rounds; ++round) {
    phase.arrive_and_wait();
    phase.arrive_and_wait();
    exact = exact && !recorder->readyOwner();
    for (const auto queue : eventQueues)
      exact = exact && InputEvidenceTestAccess::depth(*recorder, queue) == workers;
    phase.arrive_and_wait();
    phase.arrive_and_wait();
    exact = exact && recorder->readyOwner().has_value();
    for (const auto queue : eventQueues)
      exact = exact && InputEvidenceTestAccess::depth(*recorder, queue) == 0;
  }
  for (auto &thread : threads)
    thread.join();
  check(
      exact && recorder->loss() == InputEvidenceLoss::None && recorder->written() == 0,
      "concurrent pre-arm Event lifetimes lost exact depth or invented measured rows"
  );
  check(recorder->arm(1000000000), "quiescent concurrent Event preparation could not arm");
  for (const auto action :
       {Action::AllAdmit, Action::Admit, Action::AllDispatch, Action::Dispatch, Action::Return, Action::AllReturn,
        Action::Retire, Action::AllRetire})
    observe(action);
  recorder->stop();
  recorder->finish();
  unsigned peaks = 0;
  while (const auto row = recorder->pop())
    if (eventInputEvidenceQueue(row->queue)) {
      check(row->enqueued <= 1 && row->retired <= 1, "preparation counts leaked past the empty measurement boundary");
      peaks += row->depth == 1;
    }
  check(peaks == 4 && recorder->loss() == InputEvidenceLoss::None, "measured Event mutations lost their exact peaks");

  auto underflow = core();
  underflow->eventObserver().observe(underflow.get(), underflow.get(), {}, Action::AllRetire);
  check(
      underflow->loss() == InputEvidenceLoss::QueueAccounting && !underflow->arm(1000000000),
      "preparation Event underflow was accepted"
  );
  auto boundary = core();
  check(boundary->beginQueueActivity(InputEvidenceQueue::EventAllOutstanding, boundary.get()), "Event activity failed");
  check(!boundary->readyOwner() && !boundary->arm(1000000000), "unfinished Event observation crossed the arm boundary");
  boundary->endQueueActivity(InputEvidenceQueue::EventAllOutstanding);
  check(boundary->loss() == InputEvidenceLoss::Baseline, "racing Event preparation silently recovered");
}
void concurrentEventRecording()
{
  auto recorder = core();
  check(recorder->arm(180000000000ULL), "concurrent recording could not arm");
  const auto &observer = recorder->eventObserver();
  using Action = deskflow::EventInputObservation;
  constexpr unsigned workers = 4, rounds = 128;
  BlockingTestBarrier phase(workers);
  std::vector<std::thread> threads;
  for (unsigned worker = 0; worker != workers; ++worker)
    threads.emplace_back([&] {
      for (unsigned round = 0; round != rounds; ++round)
        for (const auto action :
             {Action::AllAdmit, Action::Admit, Action::AllDispatch, Action::Dispatch, Action::Return, Action::AllReturn,
              Action::Retire, Action::AllRetire}) {
          phase.arrive_and_wait();
          observer.observe(observer.context, recorder.get(), {5, 1, 1}, action);
        }
    });
  for (auto &thread : threads)
    thread.join();
  recorder->stop();
  recorder->finish();
  check(recorder->loss() == InputEvidenceLoss::None, "concurrent Event lifetimes lost measured counter updates");
  std::array<std::uint64_t, InputEvidenceRecorder::queueCount> enqueued{}, retired{}, times{}, rows{};
  while (const auto row = recorder->pop()) {
    if (!eventInputEvidenceQueue(row->queue))
      continue;
    const auto index = static_cast<std::size_t>(row->queue);
    check(
        row->enqueued >= enqueued[index] && row->retired >= retired[index] && row->enqueued >= row->retired &&
            row->depth == row->enqueued - row->retired && row->time >= times[index],
        "Event journal reordered counters or changed their observed time"
    );
    const auto delta = row->enqueued - enqueued[index] + row->retired - retired[index];
    check(
        delta == 1 || rows[index] == 0 || rows[index] == 2 * workers * rounds + 1,
        "Event journal aggregated or omitted an intermediate mutation"
    );
    enqueued[index] = row->enqueued;
    retired[index] = row->retired;
    times[index] = row->time;
    ++rows[index];
  }
  for (const auto queue :
       {InputEvidenceQueue::EventOutstanding, InputEvidenceQueue::EventDispatch,
        InputEvidenceQueue::EventAllOutstanding, InputEvidenceQueue::EventAllDispatch}) {
    const auto index = static_cast<std::size_t>(queue);
    check(
        enqueued[index] == workers * rounds && retired[index] == workers * rounds &&
            rows[index] == 2 * workers * rounds + 2,
        "Event journal lost a lifetime or its exact empty boundaries"
    );
  }
  check(recorder->drained() && recorder->loss() == InputEvidenceLoss::None, "Event journal did not drain cleanly");
}

void concurrentCaptureRecording(InputEvidenceQueue queue)
{
  auto recorder = core();
  inputEvidenceInstalled.store(recorder.get());
  struct Reset
  {
    ~Reset()
    {
      inputEvidenceInstalled.store(nullptr);
    }
  } reset;
  check(recorder->arm(180000000000ULL), "capture recording could not arm");
  // One published item allows both the SPSC producer and consumer to commit
  // together. Every round starts and ends at depth one; no capacity is added.
  {
    InputEvidenceMutation prime(queue, recorder.get(), 1, 0);
  }
  constexpr unsigned rounds = 1000;
  BlockingTestBarrier phase(2);
  std::thread producer([&] {
    for (unsigned round = 0; round != rounds; ++round) {
      phase.arrive_and_wait();
      {
        InputEvidenceMutation mutation(queue, recorder.get(), 1, 0);
      }
      phase.arrive_and_wait();
    }
  });
  for (unsigned round = 0; round != rounds; ++round) {
    phase.arrive_and_wait();
    {
      InputEvidenceMutation mutation(queue, recorder.get(), 0, 1);
    }
    phase.arrive_and_wait();
  }
  producer.join();
  {
    InputEvidenceMutation last(queue, recorder.get(), 0, 1);
  }
  recorder->stop();
  recorder->finish();
  check(recorder->loss() == InputEvidenceLoss::None, "concurrent capture updates lost diagnostic evidence");
  std::uint64_t enqueued = 0, retired = 0, time = 0, rows = 0;
  while (const auto row = recorder->pop()) {
    if (row->queue != queue)
      continue;
    check(
        row->enqueued >= enqueued && row->retired >= retired && row->enqueued >= row->retired &&
            row->depth == row->enqueued - row->retired && row->depth <= 2 && row->time >= time,
        "capture journal reordered counts or changed their actual observation time"
    );
    const auto delta = row->enqueued - enqueued + row->retired - retired;
    check(
        delta == 1 || rows == 0 || rows == 2 * rounds + 3,
        "capture journal omitted or combined an intermediate mutation"
    );
    enqueued = row->enqueued;
    retired = row->retired;
    time = row->time;
    ++rows;
  }
  check(
      enqueued == rounds + 1 && retired == rounds + 1 && rows == 2 * rounds + 4 && recorder->drained() &&
          recorder->state() == InputEvidenceState::Stopped && recorder->loss() == InputEvidenceLoss::None,
      "capture journal failed exact counts, empty boundaries or clean drain"
  );
}

void concurrentJournalPublicationOrder(InputEvidenceQueue queue)
{
  const auto row = [queue](std::uint64_t sequence) {
    return InputEvidenceRow{
        InputEvidenceKind::EventQueue, queue, sequence * 10, 0, sequence, 0, sequence, 0, sequence, 0, {}
    };
  };
  auto recorder = core();
  InputEvidenceTestAccess::append(*recorder, row(2));
  InputEvidenceRow source;
  source.kind = InputEvidenceKind::Source;
  source.time = 15;
  source.sequence = 77;
  InputEvidenceTestAccess::append(*recorder, source);
  InputEvidenceTestAccess::append(*recorder, row(1));
  auto actual = recorder->pop();
  check(
      actual && actual->kind == InputEvidenceKind::Source && actual->time == 15 && actual->sequence == 77,
      "pending Event publication blocked or changed independent source input"
  );
  for (std::uint64_t sequence : {1, 2}) {
    actual = recorder->pop();
    check(
        actual && actual->kind == InputEvidenceKind::Queue && actual->enqueued == sequence &&
            actual->time == sequence * 10,
        "late journal copy lost counter order or rewrote its captured timestamp"
    );
  }
  check(
      !recorder->pop() && recorder->drained() && recorder->loss() == InputEvidenceLoss::None,
      "ordered Event publications did not drain exactly once"
  );

  auto bounded = core();
  constexpr auto pendingCapacity = InputEvidenceRecorder::capacity / 7;
  for (std::uint64_t sequence = 2; sequence <= pendingCapacity + 2; ++sequence)
    InputEvidenceTestAccess::append(*bounded, row(sequence));
  check(
      !bounded->pop() && bounded->loss() == InputEvidenceLoss::Output && !bounded->drained(),
      "pending Event publications exceeded their fixed bound or invented a missing row"
  );
  bounded->stop();
  std::size_t count = 0;
  while ((actual = bounded->pop())) {
    check(
        actual->enqueued == count + 2 && actual->time == (count + 2) * 10,
        "failed pending journal overwrote retained diagnostic rows"
    );
    ++count;
  }
  check(
      count == pendingCapacity && bounded->drained() && bounded->loss() == InputEvidenceLoss::Output,
      "terminal gap hid its loss or left cleanup waiting for an impossible row"
  );

  auto saturated = core();
  check(saturated->arm(1000000000), "counter saturation fixture did not arm");
  InputEvidenceTestAccess::eventCounts(*saturated, queue, static_cast<std::uint64_t>(UINT32_MAX) << 32);
  if (nativeSpscInputEvidenceQueue(queue)) {
    check(saturated->beginMutation(queue, saturated.get()), "capture counter fixture did not admit mutation");
    saturated->endMutation(queue, 1, 0);
  } else {
    saturated->eventObserver().observe(saturated.get(), saturated.get(), {}, deskflow::EventInputObservation::AllAdmit);
  }
  check(saturated->loss() == InputEvidenceLoss::QueueAccounting, "packed Event counter silently wrapped");
}

void stickyFailures()
{
  auto epoch = core();
  check(epoch->arm(1000000000), "arm failed");
  auto record = key();
  record.epoch = 6;
  epoch->source(record);
  check(epoch->loss() == InputEvidenceLoss::CaptureChanged, "capture replacement accepted");
  epoch->fail(InputEvidenceLoss::Output);
  check(epoch->loss() == InputEvidenceLoss::CaptureChanged, "first loss overwritten");
  auto sequence = core();
  check(sequence->arm(1000000000), "arm failed");
  sequence->source(key(8));
  sequence->source(key(10));
  check(sequence->loss() == InputEvidenceLoss::Malformed, "source gap accepted");
  auto replaced = core();
  check(replaced->arm(1000000000), "arm failed");
  auto changed = owner();
  changed.values[7]++;
  replaced->bindOwner(changed);
  check(replaced->loss() == InputEvidenceLoss::OwnerChanged, "focus replacement retained evidence");
  auto concurrent = core();
  check(concurrent->beginMutation(InputEvidenceQueue::ServerPending, concurrent.get()), "queue lock failed");
  check(
      !concurrent->beginMutation(InputEvidenceQueue::ServerPending, concurrent.get()),
      "reentrant mutation waited or entered"
  );
  concurrent->endMutation(InputEvidenceQueue::ServerPending, 0, 0);
  check(concurrent->loss() == InputEvidenceLoss::Contention && !concurrent->arm(100), "contention silently recovered");
  check(
      concurrent->failureSite() == InputEvidenceFailureSite::QueueMutation &&
          concurrent->failureQueue() == InputEvidenceQueue::ServerPending,
      "first contention lost its exact failure context"
  );
  auto active = core();
  active->beginCaptureActivity();
  check(!active->arm(1000000000), "capture before first queue escaped arm baseline");
  active->endCaptureActivity();
  for (bool leaveBeforeUnlock : {false, true}) {
    auto racing = core();
    check(InputEvidenceTestAccess::lockBoundary(*racing), "empty capture boundary unavailable");
    // Reproduce entry after arm's empty check but before Recording: payload
    // would be skipped while Waiting, then queued after a false empty start.
    racing->beginCaptureActivity();
    racing->source(key());
    if (leaveBeforeUnlock)
      racing->endCaptureActivity();
    (void)InputEvidenceTestAccess::unlockBoundary(*racing);
    if (!leaveBeforeUnlock)
      racing->endCaptureActivity();
    check(
        racing->loss() == InputEvidenceLoss::Baseline && racing->written() == 0 && !racing->arm(1000000000),
        "source entering during arm escaped the nonwaiting transition gate"
    );
  }
  auto activeStop = core();
  check(activeStop->arm(1000000000), "arm failed");
  activeStop->beginCaptureActivity();
  activeStop->stop();
  activeStop->finish();
  check(activeStop->state() == InputEvidenceState::Draining, "in-flight source escaped terminal drain");
  activeStop->endCaptureActivity();
  activeStop->finish();
  check(
      activeStop->state() == InputEvidenceState::Stopped && activeStop->loss() == InputEvidenceLoss::None,
      "completed capture did not establish terminal baseline"
  );
  auto tail = core();
  check(tail->arm(1000000000), "arm failed");
  tail->stop();
  const auto boundary = tail->sourceEnd();
  check(
      boundary >= tail->start() && tail->state() == InputEvidenceState::Draining,
      "stop did not publish its source boundary before draining"
  );
  tail->hover(5, 10, 11);
  tail->stop();
  check(tail->sourceEnd() == boundary, "duplicate stop extended the boundary");
  tail->finish();
  bool sawHover = false;
  while (const auto row = tail->pop())
    if (row->kind == InputEvidenceKind::Hover)
      sawHover = true;
  check(sawHover && tail->loss() == InputEvidenceLoss::None, "tail compression was silently discarded");
  auto unknown = core(false);
  const auto invalidQueue = static_cast<InputEvidenceQueue>(255);
  unknown->initialize(invalidQueue, unknown.get(), 1);
  check(
      !unknown->expectedQueue(invalidQueue) && !unknown->beginMutation(invalidQueue, unknown.get()) &&
          unknown->loss() == InputEvidenceLoss::QueueIdentity,
      "unknown queue indexed recorder storage"
  );
  for (auto [added, removed] : {std::pair{1ULL, 1ULL}, {257ULL, 0ULL}, {0ULL, 1ULL}}) {
    auto bad = core();
    check(bad->beginMutation(InputEvidenceQueue::Capture, bad.get()), "queue lock failed");
    bad->endMutation(InputEvidenceQueue::Capture, added, removed);
    check(bad->loss() == InputEvidenceLoss::QueueAccounting, "unknown intermediate depth accepted");
  }
  auto full = core();
  check(full->arm(180000000000ULL), "arm failed");
  for (std::size_t i = 0; i <= InputEvidenceRecorder::capacity; ++i)
    full->source(key(i + 1));
  check(
      full->loss() == InputEvidenceLoss::BufferFull && full->written() == InputEvidenceRecorder::capacity,
      "bounded recorder overwrote or hid loss"
  );
}
void concurrentJournalWriters()
{
  // Independent queues legitimately publish at the same time. Exercise their
  // shared journal without introducing contention on an individual queue.
  auto recorder = core();
  constexpr std::size_t producers = 4, perProducer = 1024, rounds = 4;
  std::uint64_t total = 0;
  for (std::size_t round = 0; round != rounds; ++round) {
    BlockingTestBarrier boundary(producers);
    std::vector<std::thread> writers;
    for (std::size_t producer = 0; producer != producers; ++producer)
      writers.emplace_back([&, producer] {
        for (std::size_t index = 0; index != perProducer; ++index) {
          InputEvidenceRow row;
          row.sequence = producer * perProducer + index;
          row.otherSequence = round;
          boundary.arrive_and_wait();
          InputEvidenceTestAccess::append(*recorder, row);
        }
      });
    for (auto &writer : writers)
      writer.join();
    total += producers * perProducer;
    check(
        recorder->loss() == InputEvidenceLoss::None && recorder->written() == total,
        "independent concurrent writers lost journal entries"
    );
    std::array<bool, producers * perProducer> seen{};
    std::size_t count = 0;
    while (const auto row = recorder->pop()) {
      check(
          row->sequence < seen.size() && !seen[row->sequence] && row->otherSequence == round,
          "concurrent journal overwrote, duplicated or reused a stale entry"
      );
      seen[row->sequence] = true;
      ++count;
    }
    check(count == seen.size() && recorder->drained(), "concurrent journal did not drain exactly once");
  }
}
void unpublishedJournalReservation()
{
  auto recorder = core();
  const auto ticket = InputEvidenceTestAccess::reserveRow(*recorder);
  check(ticket && *ticket == 0 && !recorder->drained(), "unpublished writer was absent from the drain boundary");
  for (std::size_t index = 1; index != InputEvidenceRecorder::capacity; ++index) {
    InputEvidenceRow row;
    row.sequence = index;
    InputEvidenceTestAccess::append(*recorder, row);
  }
  check(
      recorder->loss() == InputEvidenceLoss::None && !recorder->pop(),
      "journal skipped an unfinished reservation or miscounted capacity"
  );
  InputEvidenceTestAccess::append(*recorder, {});
  check(
      recorder->loss() == InputEvidenceLoss::BufferFull && recorder->written() == InputEvidenceRecorder::capacity,
      "unpublished reservation did not retain its capacity credit"
  );
  InputEvidenceTestAccess::publishRow(*recorder, *ticket, {});
  std::size_t count = 0;
  while (const auto row = recorder->pop()) {
    check(row->sequence == count, "late publication reordered or overwrote a reserved row");
    ++count;
  }
  check(
      count == InputEvidenceRecorder::capacity && recorder->drained() &&
          recorder->loss() == InputEvidenceLoss::BufferFull,
      "late publication did not drain exactly once or erased the earlier overflow"
  );
}
void publicationBoundaries()
{
  auto recorder = core();
  inputEvidenceInstalled.store(recorder.get());
  struct Reset
  {
    ~Reset()
    {
      inputEvidenceInstalled.store(nullptr);
    }
  } reset;
  // Reproduce the legitimate SPSC overlap: after publishing one record, its
  // consumer can finish before the producer returns from its native scope.
  // The old scope-long counter lock denied the run at that exact boundary.
  for (const auto queue :
       {InputEvidenceQueue::Capture, InputEvidenceQueue::Translation, InputEvidenceQueue::Completed}) {
    {
      InputEvidenceMutation producer(queue, recorder.get(), 1, 0);
      producer.commit();
      bool consumerSawReady = true;
      std::thread consumer([&] {
        {
          InputEvidenceMutation retirement(queue, recorder.get(), 0, 1);
          retirement.commit();
        }
        consumerSawReady = recorder->readyOwner().has_value();
      });
      consumer.join();
      check(!consumerSawReady, "published-but-active producer escaped the empty start boundary");
      check(recorder->loss() == InputEvidenceLoss::None, "legal publication overlap poisoned preparation");
    }
    check(recorder->readyOwner().has_value(), "completed publication did not become ready");
  }
  check(recorder->arm(1000000000), "publication preparation did not arm");
  {
    InputEvidenceMutation producer(InputEvidenceQueue::Capture, recorder.get(), 1, 0);
    producer.commit();
    std::thread consumer([&] {
      InputEvidenceMutation retirement(InputEvidenceQueue::Capture, recorder.get(), 0, 1);
      retirement.commit();
    });
    consumer.join();
    recorder->stop();
    recorder->finish();
    check(recorder->state() == InputEvidenceState::Draining, "active publication escaped terminal drain");
  }
  recorder->finish();
  check(
      recorder->state() == InputEvidenceState::Stopped && recorder->loss() == InputEvidenceLoss::None,
      "completed concurrent publication did not drain cleanly"
  );
  std::vector<std::pair<std::uint64_t, std::uint64_t>> counts;
  while (const auto row = recorder->pop())
    if (row->queue == InputEvidenceQueue::Capture)
      counts.emplace_back(row->enqueued, row->retired);
  check(
      counts == std::vector<std::pair<std::uint64_t, std::uint64_t>>{{0, 0}, {1, 0}, {1, 1}, {1, 1}},
      "publication accounting omitted, reordered or aggregated an intermediate depth"
  );

  auto active = core();
  inputEvidenceInstalled.store(active.get());
  {
    InputEvidenceMutation unfinished(InputEvidenceQueue::Capture, active.get(), 0, 0);
    unfinished.commit();
    check(!active->arm(1000000000), "empty counters hid an unfinished native mutation at arm");
  }
  check(active->loss() == InputEvidenceLoss::Baseline, "unfinished mutation arm was not terminal");
}
void unboundedEventMetrics()
{
  for (const auto queue : {InputEvidenceQueue::EventOutstanding, InputEvidenceQueue::EventDispatch}) {
    auto fabricated = core(false);
    fabricated->initialize(queue, fabricated.get(), 256);
    check(fabricated->loss() == InputEvidenceLoss::QueueIdentity, "unbounded event metric accepted a fake capacity");
    auto recorder = core();
    check(recorder->arm(1000000000), "arm failed");
    // This is a counter fixture, not a queue allocation or claimed production
    // limit. Depth beyond the bounded-schema ceiling must remain representable.
    check(recorder->beginMutation(queue, recorder.get()), "event metric lock failed");
    recorder->endMutation(queue, 65537, 0);
    check(recorder->beginMutation(queue, recorder.get()), "event metric lock failed");
    recorder->endMutation(queue, 0, 65537);
    recorder->stop();
    recorder->finish();
    bool peak = false;
    while (const auto row = recorder->pop())
      if (row->queue == queue && row->depth == 65537)
        peak = row->capacity == 0;
    check(peak && recorder->loss() == InputEvidenceLoss::None, "unbounded accounting hid depth or invented a limit");
    auto overflow = core();
    check(overflow->beginMutation(queue, overflow.get()), "event metric lock failed");
    overflow->endMutation(queue, UINT64_MAX, 0);
    check(overflow->loss() == InputEvidenceLoss::None, "valid unbounded counter rejected");
    check(overflow->beginMutation(queue, overflow.get()), "event metric lock failed");
    overflow->endMutation(queue, 1, 0);
    check(overflow->loss() == InputEvidenceLoss::QueueAccounting, "event counter overflow wrapped silently");
    auto underflow = core();
    check(underflow->beginMutation(queue, underflow.get()), "event metric lock failed");
    underflow->endMutation(queue, 0, 1);
    check(underflow->loss() == InputEvidenceLoss::QueueAccounting, "unbounded retirement bypassed accounting");
  }
  auto bounded = core(false);
  bounded->initialize(InputEvidenceQueue::Capture, bounded.get(), 0);
  check(bounded->loss() == InputEvidenceLoss::QueueIdentity, "bounded capture accepted unknown capacity");
}
void actualPortableQueues()
{
  auto recorder = std::make_unique<InputEvidenceRecorder>(InputEvidenceRole::Core);
  inputEvidenceInstalled.store(recorder.get());
  struct Reset
  {
    ~Reset()
    {
      inputEvidenceInstalled.store(nullptr);
    }
  } reset;
  OriginalInputStream stream;
  OriginalInputCapturedInbox inbox(stream.epoch());
  for (std::size_t i = 0; i != InputEvidenceRecorder::queueCount; ++i) {
    const auto queue = static_cast<InputEvidenceQueue>(i);
    if (queue != InputEvidenceQueue::Capture && queue != InputEvidenceQueue::Completed)
      recorder->initialize(queue, recorder.get(), eventInputEvidenceQueue(queue) ? 0 : 256);
  }
  auto binding = owner();
  binding.values[4] = stream.epoch().id();
  recorder->bindOwner(binding);
  check(inbox.bind(1) && recorder->arm(180000000000ULL), "actual queue baseline failed");
  auto record = key();
  record.epoch = stream.epoch().id();
  recorder->source(record);
  check(stream.push(record), "pure capture push failed");
  const auto delivery = stream.pop();
  check(delivery.has_value(), "pure capture pop failed");
  check(inbox.publish({*delivery, {{record.epoch, record.sequence, 0}, 0, false}, {}}), "completed publish failed");
  check(inbox.pop().has_value(), "completed copy lost");
  recorder->stop();
  recorder->finish();
  std::array<unsigned, 2> peaks{};
  while (const auto row = recorder->pop())
    if (row->kind == InputEvidenceKind::Queue) {
      if (row->queue == InputEvidenceQueue::Capture && row->depth == 1)
        ++peaks[0];
      if (row->queue == InputEvidenceQueue::Completed && row->depth == 1)
        ++peaks[1];
    }
  check(
      peaks == std::array<unsigned, 2>{1, 1} && recorder->loss() == InputEvidenceLoss::None,
      "actual raw/inbox mutations omitted an intermediate queue peak"
  );
}
void revokeDuringDrain()
{
  auto recorder = std::make_unique<InputEvidenceRecorder>(InputEvidenceRole::Core);
  inputEvidenceInstalled.store(recorder.get());
  struct Reset
  {
    ~Reset()
    {
      inputEvidenceInstalled.store(nullptr);
    }
  } reset;
  OriginalInputStream stream;
  OriginalInputCapturedInbox inbox(stream.epoch());
  for (std::size_t i = 0; i != InputEvidenceRecorder::queueCount; ++i) {
    const auto queue = static_cast<InputEvidenceQueue>(i);
    if (queue != InputEvidenceQueue::Capture && queue != InputEvidenceQueue::Completed)
      recorder->initialize(queue, recorder.get(), eventInputEvidenceQueue(queue) ? 0 : 256);
  }
  auto binding = owner();
  binding.values[4] = stream.epoch().id();
  recorder->bindOwner(binding);
  check(recorder->arm(180000000000ULL), "arm failed");
  recorder->stop();
  inbox.revoke();
  check(recorder->loss() == InputEvidenceLoss::PipelineRevoked, "tail conduit revocation was hidden");
  recorder->finish();
}
void nativeFields()
{
  OriginalInputRecord record{
      5, 1, 9007199254740995ULL, 0x100000000ULL,
      OriginalInputScroll{
          {-2, 3}, {7, -9}, {65537, -131073}, OriginalScrollPhase::Changed, OriginalMomentumPhase::None, true
      }
  };
  const auto raw = inputEvidenceRaw(record);
  check(
      raw && raw->timestamp == record.sourceTimestampNs && raw->flags == record.flags && raw->fields[8] == -2 &&
          raw->fields[11] == -9 && raw->fields[12] == 65537 && raw->fields[13] == -131073,
      "scroll native representations narrowed or rewritten"
  );
  auto native = std::make_unique<InputEvidenceRecorder>(InputEvidenceRole::Native);
  InputEvidenceOwner binding;
  binding.values[0] = 44;
  binding.values[1] = 55;
  native->bindOwner(binding);
  check(native->arm(1000000000), "owned fixture failed to arm");
  native->native(*raw, 55, 88, 99);
  const auto receipt = native->pop();
  check(
      receipt && receipt->raw == *raw && receipt->sequence == 55 && receipt->epoch == 88 &&
          receipt->otherSequence == 99,
      "native observer lost timestamp/fields/destination/provenance"
  );
  native->stop();
}
void privateConfiguration()
{
  QTemporaryDir directory;
  check(directory.isValid(), "temporary directory failed");
  const auto path = QFileInfo(directory.filePath("config.json")).absoluteFilePath();
  const auto parent = QFileInfo(directory.path()).canonicalFilePath();
  const auto configPath = parent + "/config.json";
  const QJsonObject valid{
      {"version", 1},
      {"role", "native"},
      {"runId", "pure-fixture"},
      {"durationMs", 1000},
      {"output", parent + "/output.jsonl"},
      {"control", parent + "/control.json"}
  };
  const auto write = [&](const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "config fixture failed");
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    check(file.write(bytes) == bytes.size(), "config fixture write failed");
  };
  for (const auto &name : {"version", "role", "runId", "durationMs", "output", "control"}) {
    auto missing = valid;
    missing.remove(name);
    write(QJsonDocument(missing).toJson());
    denied([&] { InputEvidenceTestAccess::open(configPath, InputEvidenceRole::Native); });
  }
  for (const auto &duration : {QJsonValue(0), QJsonValue(180001), QJsonValue(1.5), QJsonValue("120000")}) {
    auto changed = valid;
    changed["durationMs"] = duration;
    write(QJsonDocument(changed).toJson());
    denied([&] { InputEvidenceTestAccess::open(configPath, InputEvidenceRole::Native); });
  }
  write(QJsonDocument(valid).toJson());
  denied([&] { InputEvidenceTestAccess::open(configPath, InputEvidenceRole::Core); });
  auto duplicate = QJsonDocument(valid).toJson(QJsonDocument::Compact);
  duplicate.insert(1, "\"version\":1,");
  write(duplicate);
  denied([&] { InputEvidenceTestAccess::open(configPath, InputEvidenceRole::Native); });
  write(QJsonDocument(valid).toJson());
  auto runtime = InputEvidenceTestAccess::open(configPath, InputEvidenceRole::Native);
  check(
      runtime->recorder().state() == InputEvidenceState::Waiting && runtime->recorder().written() == 0,
      "configuration implicitly armed recording"
  );
  denied([&] { InputEvidenceTestAccess::open(configPath, InputEvidenceRole::Native); });
  const auto recordPath = parent + "/record-request.json";
  check(runtime->requestRecord(recordPath), "explicit native record request failed");
  QFile recordRequest(recordPath);
  check(recordRequest.open(QIODevice::ReadOnly), "record request absent");
  const auto bytes = recordRequest.readAll();
  check(
      QJsonDocument::fromJson(bytes).object() ==
              QJsonObject{{"version", 1}, {"runId", "pure-fixture"}, {"command", "record"}} &&
          (recordRequest.permissions() &
           (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther)) == 0,
      "record request changed schema or privacy"
  );
  check(
      !runtime->requestRecord(parent + "/second-request.json") && !QFileInfo::exists(parent + "/second-request.json") &&
          runtime->recorder().state() == InputEvidenceState::Waiting && runtime->recorder().written() == 0,
      "Record implicitly armed or created another request"
  );
  // This test has no producers. Release the otherwise process-retained buffer
  // only after its runtime stops, so independent rejection cases stay pure.
  auto *storage = &runtime->recorder();
  runtime.reset();
  inputEvidenceInstalled.store(nullptr);
  delete storage;
  for (const auto &request : {recordPath, parent + "/control.json", parent + "/../record.json"}) {
    auto changed = valid;
    changed["output"] = parent + "/rejected-" + QString::number(qHash(request)) + ".jsonl";
    write(QJsonDocument(changed).toJson());
    runtime = InputEvidenceTestAccess::open(configPath, InputEvidenceRole::Native);
    check(
        !runtime->requestRecord(request) && runtime->recorder().loss() == InputEvidenceLoss::Output &&
            runtime->recorder().state() == InputEvidenceState::Waiting,
        "unsafe/existing Record request accepted or armed"
    );
    storage = &runtime->recorder();
    runtime.reset();
    inputEvidenceInstalled.store(nullptr);
    delete storage;
  }
  auto coreConfig = valid;
  coreConfig["role"] = "core";
  coreConfig["output"] = parent + "/core-output.jsonl";
  write(QJsonDocument(coreConfig).toJson());
  runtime = InputEvidenceTestAccess::open(configPath, InputEvidenceRole::Core);
  storage = &runtime->recorder();
  const auto *observer = deskflow::eventInputObserverInstalled.load();
  check(observer == &storage->eventObserver(), "Core runtime did not install its retained event observer");
  check(!storage->readyOwner(), "runtime invented an EventQueue baseline before construction");
  // Serialization fixture only. Actual queue initialization/lifetimes are
  // exercised by EventQueueTests and ServerTests with the real InputFilter.
  observer->initialize(observer->context, storage, 0, 0);
  for (std::size_t i = 0; i != InputEvidenceRecorder::queueCount; ++i) {
    const auto queue = static_cast<InputEvidenceQueue>(i);
    if (!eventInputEvidenceQueue(queue))
      storage->initialize(queue, storage, 256);
  }
  storage->bindOwner(owner());
  check(storage->arm(1000000000), "runtime serialization baseline failed");
  runtime.reset();
  check(
      inputEvidenceInstalled.load() == storage && deskflow::eventInputObserverInstalled.load() == observer,
      "runtime teardown freed or uninstalled a producer's retained observer"
  );
  QFile output(parent + "/core-output.jsonl");
  check(output.open(QIODevice::ReadOnly), "Core evidence output missing");
  std::size_t nullRows = 0, boundedRows = 0;
  while (!output.atEnd()) {
    const auto row = QJsonDocument::fromJson(output.readLine()).object();
    if (row["type"] == "header")
      check(row["queues"].toArray().size() == 17, "Core header lost its declared queue inventory");
    if (row["type"] != "queue")
      continue;
    if (row["queue"].toString().startsWith("event-")) {
      check(row.contains("capacity") && row["capacity"].isNull(), "unbounded capacity was not serialized as null");
      ++nullRows;
    } else {
      check(row["capacity"] == "256", "bounded capacity lost canonical integer representation");
      ++boundedRows;
    }
  }
  check(nullRows == 8 && boundedRows == 26, "runtime omitted initial/final queue boundaries");
  // No producers survive this unit fixture; production deliberately retains
  // both targets until process exit instead of attempting live reassignment.
  deskflow::eventInputObserverInstalled.store(nullptr);
  inputEvidenceInstalled.store(nullptr);
  delete storage;
}

void cooperativeSerialization()
{
  for (const bool destroyPending : {false, true}) {
    QTemporaryDir directory;
    check(directory.isValid(), "serialization directory failed");
    const auto parent = QFileInfo(directory.path()).canonicalFilePath();
    const auto output = parent + "/evidence.jsonl", config = parent + "/config.json";
    {
      QFile file(config);
      check(file.open(QIODevice::WriteOnly), "serialization config failed");
      file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
      file.write(QJsonDocument(
                     QJsonObject{
                         {"version", 1},
                         {"role", "native"},
                         {"runId", "serialization"},
                         {"durationMs", 1000},
                         {"output", output},
                         {"control", parent + "/control.json"}
                     }
      )
                     .toJson());
    }
    auto runtime = InputEvidenceTestAccess::open(config, InputEvidenceRole::Native);
    auto *storage = &runtime->recorder();
    storage->bindOwner(owner());
    check(storage->arm(1000000000), "serialization owner could not arm");
    constexpr unsigned count = 2048;
    for (unsigned i = 0; i != count; ++i) {
      InputEvidenceRaw raw;
      raw.timestamp = 50000 + i;
      raw.flags = 0x20000000;
      raw.fields[0] = 22;
      raw.fields[11] = i;
      storage->native(raw, 55, 44, 123);
    }
    if (!destroyPending) {
      // Only the ordinary timer services control files. A serialization
      // continuation must not repeatedly resolve/read the same file, or carry
      // an implicit control/authority transition through the drain path.
      {
        QFile control(parent + "/control.json");
        check(control.open(QIODevice::WriteOnly), "serialization control failed");
        control.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        control.write(
            QJsonDocument(QJsonObject{{"version", 1}, {"runId", "serialization"}, {"command", "stop"}}).toJson()
        );
      }
    }
    std::atomic<bool> intervened{false};
    InputEvidenceTestAccess::firstSlice(*runtime, [&](auto serialized, QObject *collector) {
      check(
          !storage->drained() && serialized <= 129,
          "one evidence turn serialized the burst instead of yielding to controls"
      );
      if (!destroyPending)
        QMetaObject::invokeMethod(collector, [&] { intervened.store(!storage->drained()); }, Qt::QueuedConnection);
    });
    if (!destroyPending) {
      bool expired = false;
      QEventLoop loop;
      QTimer observe, timeout;
      QObject::connect(&observe, &QTimer::timeout, &loop, [&] {
        if (InputEvidenceTestAccess::drained(*runtime))
          loop.quit();
      });
      QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        expired = true;
        loop.quit();
      });
      observe.start(1);
      timeout.setSingleShot(true);
      timeout.start(1000);
      loop.exec();
      check(
          !expired && intervened.load() && InputEvidenceTestAccess::drained(*runtime) &&
              storage->loss() == InputEvidenceLoss::None,
          "cooperative evidence drain failed or lost its queued control turn"
      );
      check(
          storage->state() == InputEvidenceState::Recording,
          "serialization continuation reread the control file outside its timer"
      );
      InputEvidenceTestAccess::tick(*runtime);
      check(
          storage->state() == InputEvidenceState::Draining && storage->sourceEnd() != 0 &&
              storage->loss() == InputEvidenceLoss::None,
          "ordinary timer failed to stop the source while preserving the native receipt tail"
      );
    }
    // Teardown must finish an outstanding slice synchronously, cancel its
    // queued callback and leave the exact journal before its terminal row.
    runtime.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    inputEvidenceInstalled.store(nullptr);
    delete storage;
    QFile file(output);
    check(file.open(QIODevice::ReadOnly), "serialization output missing");
    unsigned received = 0, ends = 0;
    while (!file.atEnd()) {
      const auto row = QJsonDocument::fromJson(file.readLine()).object();
      if (row["type"] == "native") {
        check(
            ends == 0 && row["cgTimestampNs"] == QString::number(50000 + received) && row["flags"] == "536870912" &&
                row["raw"].toArray()[11] == QString::number(received),
            "serialization changed, reordered or moved an original row after the footer"
        );
        ++received;
      } else if (row["type"] == "end") {
        ++ends;
        check(row["rows"] == QString::number(count) && row["drained"].toBool(), "terminal row lost pending evidence");
      }
    }
    check(received == count && ends == 1, "serialization lost, duplicated or failed to close the burst");
  }
}

void stalledCollectorKeepsApplicationResponsive()
{
  for (const bool overflow : {false, true}) {
    QTemporaryDir directory;
    check(directory.isValid(), "collector directory failed");
    const auto parent = QFileInfo(directory.path()).canonicalFilePath();
    const auto config = parent + "/config.json", output = parent + "/evidence.jsonl";
    {
      QFile file(config);
      check(file.open(QIODevice::WriteOnly), "collector config failed");
      file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
      file.write(QJsonDocument(
                     QJsonObject{
                         {"version", 1},
                         {"role", "native"},
                         {"runId", "stalled-collector"},
                         {"durationMs", 10000},
                         {"output", output},
                         {"control", parent + "/control.json"}
                     }
      )
                     .toJson());
    }
    auto runtime = InputEvidenceTestAccess::open(config, InputEvidenceRole::Native);
    auto *storage = &runtime->recorder();
    storage->bindOwner(owner());
    check(storage->arm(10000000000ULL), "collector fixture could not arm");
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false, workerExpired = false, separateThread = false;
    const auto applicationThread = QThread::currentThread();
    // Model a stalled control read or output write on the actual collector
    // executor. No sleeps, global hooks, input devices or external windows.
    InputEvidenceTestAccess::queueCollector(*runtime, [&] {
      std::unique_lock lock(mutex);
      separateThread = QThread::currentThread() != applicationThread;
      entered = true;
      changed.notify_all();
      workerExpired = !changed.wait_for(lock, std::chrono::seconds(2), [&] { return released; });
    });
    bool ready;
    {
      std::unique_lock lock(mutex);
      ready = changed.wait_for(lock, std::chrono::seconds(1), [&] { return entered; });
      if (!ready)
        released = true;
    }
    changed.notify_all();
    if (!ready) {
      runtime.reset();
      inputEvidenceInstalled.store(nullptr);
      delete storage;
      check(false, "collector did not enter its bounded fixture");
    }
    const auto count = overflow ? InputEvidenceRecorder::capacity + 1 : 64U;
    bool applicationRan = false, expired = false;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
      expired = true;
      loop.quit();
    });
    QTimer::singleShot(0, &loop, [&] {
      // The application's native observer can still append to its existing
      // bounded storage while all collector I/O is stalled.
      for (unsigned i = 0; i != count; ++i) {
        InputEvidenceRaw raw;
        raw.timestamp = 90000 + i;
        raw.fields[0] = 22;
        raw.fields[11] = i;
        storage->native(raw, 55, 44, 123);
      }
      applicationRan = true;
      loop.quit();
    });
    deadline.start(1000);
    loop.exec();
    {
      std::lock_guard lock(mutex);
      released = true;
    }
    changed.notify_all();
    InputEvidenceTestAccess::collect(*runtime, [] {});
    const auto loss = storage->loss();
    runtime.reset(); // Joins the consumer and drains exact retained rows.
    inputEvidenceInstalled.store(nullptr);
    delete storage;
    check(
        separateThread && applicationRan && !expired && !workerExpired,
        "stalled evidence collector blocked the application thread"
    );
    check(
        loss == (overflow ? InputEvidenceLoss::BufferFull : InputEvidenceLoss::None),
        "collector stall concealed overflow or invalidated a bounded producer"
    );
    QFile file(output);
    check(file.open(QIODevice::ReadOnly), "collector output missing");
    unsigned rows = 0, ends = 0;
    while (!file.atEnd()) {
      const auto row = QJsonDocument::fromJson(file.readLine()).object();
      if (row["type"] == "native") {
        check(
            !ends && row["cgTimestampNs"] == QString::number(90000 + rows) &&
                row["raw"].toArray()[11] == QString::number(rows),
            "collector stall changed or reordered retained input"
        );
        ++rows;
      } else if (row["type"] == "end") {
        ++ends;
        check(row["loss"].toInt() == int(loss) && row["drained"].toBool(), "collector footer concealed loss");
      }
    }
    check(
        rows == (overflow ? InputEvidenceRecorder::capacity : count) && ends == 1,
        "collector teardown lost retained rows or emitted duplicate terminal evidence"
    );
  }
}

void armAfterBackgroundOutputDrains()
{
  QTemporaryDir directory;
  check(directory.isValid(), "arm fixture directory failed");
  const auto parent = QFileInfo(directory.path()).canonicalFilePath();
  const auto write = [&](const QString &path, const QJsonObject &value) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "arm fixture write failed");
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = QJsonDocument(value).toJson();
    check(file.write(bytes) == bytes.size(), "arm fixture short write");
  };
  const auto output = parent + "/evidence.jsonl", control = parent + "/control.json";
  const auto config = parent + "/config.json";
  write(
      config, {{"version", 1},
               {"role", "host"},
               {"runId", "pending-arm"},
               {"durationMs", 1000},
               {"output", output},
               {"control", control}}
  );
  auto runtime = InputEvidenceTestAccess::open(config, InputEvidenceRole::Host);
  auto *storage = &runtime->recorder();
  for (const auto queue :
       {InputEvidenceQueue::PrivateOutput, InputEvidenceQueue::PrivateBufferedInput,
        InputEvidenceQueue::PrivateIncoming, InputEvidenceQueue::PrivateDispatch})
    storage->initialize(queue, storage, 256);
  storage->bindOwner(owner());
  check(storage->readyOwner().has_value(), "empty Host could not publish readiness");
  // The remote coordinator sees readiness, then a heartbeat queues output
  // before this process observes its arm request. No measurement has started.
  check(storage->beginMutation(InputEvidenceQueue::PrivateOutput, storage), "output mutation failed");
  storage->endMutation(InputEvidenceQueue::PrivateOutput, 1, 0);
  write(control, {{"version", 1}, {"runId", "pending-arm"}, {"command", "arm"}});
  InputEvidenceTestAccess::tick(*runtime);
  check(
      storage->state() == InputEvidenceState::Waiting && storage->loss() == InputEvidenceLoss::None &&
          storage->written() == 0 && storage->start() == 0,
      "background output turned a pending arm into a failed or premature measurement"
  );
  check(storage->beginMutation(InputEvidenceQueue::PrivateOutput, storage), "output retirement failed");
  storage->endMutation(InputEvidenceQueue::PrivateOutput, 0, 1);
  InputEvidenceTestAccess::tick(*runtime);
  check(
      storage->state() == InputEvidenceState::Recording && storage->loss() == InputEvidenceLoss::None &&
          storage->start() != 0,
      "empty live Host did not accept the pending arm"
  );
  runtime.reset();
  inputEvidenceInstalled.store(nullptr);
  delete storage;
  QFile file(output);
  check(file.open(QIODevice::ReadOnly), "arm output missing");
  std::size_t arms = 0;
  while (!file.atEnd()) {
    const auto row = QJsonDocument::fromJson(file.readLine()).object();
    if (row["type"] == "arm") {
      ++arms;
      check(row["accepted"].toBool(), "pending arm wrote a rejected acknowledgement");
    }
  }
  check(arms == 1, "pending arm was acknowledged more than once");
}

void pendingArmDeadlineAndStop()
{
  for (const bool cancel : {false, true}) {
    QTemporaryDir directory;
    check(directory.isValid(), "pending arm directory failed");
    const auto parent = QFileInfo(directory.path()).canonicalFilePath();
    const auto write = [&](const QString &path, const QJsonObject &value) {
      QFile file(path);
      check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "pending arm write failed");
      file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
      const auto bytes = QJsonDocument(value).toJson();
      check(file.write(bytes) == bytes.size(), "pending arm short write");
    };
    const auto output = parent + "/evidence.jsonl", control = parent + "/control.json";
    const auto config = parent + "/config.json";
    write(
        config, {{"version", 1},
                 {"role", "native"},
                 {"runId", "pending-arm"},
                 {"durationMs", 1000},
                 {"output", output},
                 {"control", control}}
    );
    auto runtime = InputEvidenceTestAccess::open(config, InputEvidenceRole::Native);
    auto *storage = &runtime->recorder();
    const auto command = [&](const char *action) {
      write(control, {{"version", 1}, {"runId", "pending-arm"}, {"command", action}});
      InputEvidenceTestAccess::tick(*runtime);
    };
    command("arm"); // No owned native window is ready yet.
    InputEvidenceTestAccess::armWaitElapsed(*runtime, 1000);
    check(
        storage->state() == InputEvidenceState::Waiting && storage->loss() == InputEvidenceLoss::None &&
            storage->written() == 0,
        "pending request recorded without an owner or failed before its deadline"
    );
    if (cancel)
      command("stop");
    else
      InputEvidenceTestAccess::armWaitElapsed(*runtime, 2100);
    check(
        storage->state() == InputEvidenceState::Denied && storage->written() == 0 &&
            storage->loss() == (cancel ? InputEvidenceLoss::None : InputEvidenceLoss::Deadline),
        "pending arm ignored Stop, exceeded its budget or invented recording"
    );
    storage->bindOwner(owner());
    command("arm");
    check(storage->state() == InputEvidenceState::Denied, "late readiness resurrected an ended arm request");
    runtime.reset();
    inputEvidenceInstalled.store(nullptr);
    delete storage;
    QFile file(output);
    check(file.open(QIODevice::ReadOnly), "pending arm output missing");
    std::size_t arms = 0, ends = 0;
    while (!file.atEnd()) {
      const auto row = QJsonDocument::fromJson(file.readLine()).object();
      if (row["type"] == "arm") {
        ++arms;
        check(!row["accepted"].toBool(true), "expired arm was accepted");
      }
      if (row["type"] == "end")
        ++ends;
    }
    check(arms == (cancel ? 0U : 1U) && ends == 1, "pending request lost or repeated its terminal acknowledgement");
  }
}

void explicitNativeClose(QuitGuardApplication &app)
{
  QTemporaryDir directory;
  check(directory.isValid(), "close fixture directory failed");
  const auto parent = QFileInfo(directory.path()).canonicalFilePath();
  const auto write = [&](const QString &path, const QJsonObject &value) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "close fixture write failed");
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = QJsonDocument(value).toJson();
    check(file.write(bytes) == bytes.size(), "close fixture short write");
  };
  const auto output = parent + "/evidence.jsonl", control = parent + "/control.json";
  const auto config = parent + "/config.json";
  write(
      config, {{"version", 1},
               {"role", "native"},
               {"runId", "close-fixture"},
               {"durationMs", 1000},
               {"output", output},
               {"control", control}}
  );
  auto runtime = InputEvidenceTestAccess::open(config, InputEvidenceRole::Native, 180000, true);
  auto *storage = &runtime->recorder();
  const auto command = [&](const char *action) {
    write(control, {{"version", 1}, {"runId", "close-fixture"}, {"command", action}});
  };
  bool endObserved = false;
  QTimer observe;
  QObject::connect(&observe, &QTimer::timeout, &app, [&] {
    QFile file(output);
    if (!file.open(QIODevice::ReadOnly))
      return;
    const auto rows = file.readAll();
    if (!rows.contains("\"type\":\"end\""))
      return;
    endObserved = true;
    observe.stop();
    command("close");
  });
  observe.start(25);
  QTimer::singleShot(0, &app, [&] {
    // An ordinary quit must still be vetoed; the explicit close below is only
    // legal after the unarmed recorder has emitted its terminal evidence.
    QCoreApplication::quit();
    command("stop");
  });
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &app, [] { QCoreApplication::exit(91); });
  deadline.start(2000);
  const auto result = app.exec();
  check(result == 0 && endObserved, "explicit terminal native close was blocked by a manual quit veto");
  check(app.quitRequests == 1, "explicit close reused the ordinary vetoable quit request");
  check(
      storage->state() == InputEvidenceState::Denied && storage->loss() == InputEvidenceLoss::None &&
          storage->written() == 0 && storage->drained(),
      "unarmed native cleanup invented recording or lost terminal evidence"
  );
  runtime.reset();
  inputEvidenceInstalled.store(nullptr);
  delete storage;
}

void nativePreparationDeadline()
{
  QTemporaryDir directory;
  check(directory.isValid(), "preparation fixture directory failed");
  const auto parent = QFileInfo(directory.path()).canonicalFilePath();
  const auto config = parent + "/config.json";
  for (const std::uint64_t budget : {180000, 600000}) {
    const auto output = parent + "/output-" + QString::number(budget) + ".jsonl";
    {
      QFile file(config);
      check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "preparation config write failed");
      file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
      file.write(QJsonDocument(
                     QJsonObject{
                         {"version", 1},
                         {"role", "native"},
                         {"runId", "waiting-fixture"},
                         {"durationMs", 1000},
                         {"output", output},
                         {"control", parent + "/control.json"}
                     }
      )
                     .toJson());
    }
    for (const auto invalid : std::array<std::uint64_t, 3>{0, 600001, UINT64_MAX})
      denied([&] { InputEvidenceTestAccess::open(config, InputEvidenceRole::Native, invalid); });
    for (const auto role : {InputEvidenceRole::Core, InputEvidenceRole::Host})
      denied([&] { InputEvidenceTestAccess::open(config, role, 600000); });
    auto runtime = budget == 180000 ? InputEvidenceTestAccess::open(config, InputEvidenceRole::Native)
                                    : InputEvidenceTestAccess::open(config, InputEvidenceRole::Native, budget);
    auto *storage = &runtime->recorder();
    InputEvidenceOwner binding;
    binding.values[0] = 44;
    binding.values[1] = 55;
    storage->bindOwner(binding);
    if (budget == 600000) {
      for (const std::uint64_t elapsed : {180001, 300001, 599000}) {
        InputEvidenceTestAccess::preparationElapsed(*runtime, elapsed);
        check(
            storage->state() == InputEvidenceState::Waiting && storage->loss() == InputEvidenceLoss::None &&
                storage->written() == 0 && storage->readyOwner(),
            "visible Start wait expired early or implicitly armed recording"
        );
      }
    }
    InputEvidenceTestAccess::preparationElapsed(*runtime, budget);
    check(
        storage->state() == InputEvidenceState::Denied && storage->loss() == InputEvidenceLoss::Deadline &&
            storage->written() == 0,
        "native preparation exceeded its one-shot bound"
    );
    runtime.reset();
    inputEvidenceInstalled.store(nullptr);
    delete storage;
    QFile file(output);
    check(file.open(QIODevice::ReadOnly), "preparation evidence absent");
    const auto rows = file.readAll();
    check(
        rows.contains("\"type\":\"end\"") && !rows.contains("\"type\":\"arm\""),
        "preparation expiry omitted terminal evidence or invented an arm"
    );
  }
}
} // namespace
int main(int argc, char **argv)
{
  std::fputs("[input-evidence] process entered main; constructing Qt application\n", stderr);
  std::fflush(stderr);
  QuitGuardApplication app(argc, argv);
  try {
    runCase("baselinesAndLifecycle", baselinesAndLifecycle);
    runCase("readinessDiagnostics", readinessDiagnostics);
    runCase("concurrentEventPreparation", concurrentEventPreparation);
    runCase("concurrentEventRecording", concurrentEventRecording);
    runCase("concurrentCaptureRecording", [] {
      for (const auto queue :
           {InputEvidenceQueue::Capture, InputEvidenceQueue::Translation, InputEvidenceQueue::Completed})
        concurrentCaptureRecording(queue);
    });
    runCase("concurrentJournalPublicationOrder", [] {
      concurrentJournalPublicationOrder(InputEvidenceQueue::EventAllOutstanding);
      for (const auto queue :
           {InputEvidenceQueue::Capture, InputEvidenceQueue::Translation, InputEvidenceQueue::Completed})
        concurrentJournalPublicationOrder(queue);
    });
    runCase("stickyFailures", stickyFailures);
    runCase("concurrentJournalWriters", concurrentJournalWriters);
    runCase("unpublishedJournalReservation", unpublishedJournalReservation);
    runCase("publicationBoundaries", publicationBoundaries);
    runCase("unboundedEventMetrics", unboundedEventMetrics);
    runCase("actualPortableQueues", actualPortableQueues);
    runCase("revokeDuringDrain", revokeDuringDrain);
    runCase("nativeFields", nativeFields);
    runCase("privateConfiguration", privateConfiguration);
    runCase("cooperativeSerialization", cooperativeSerialization);
    runCase("stalledCollectorKeepsApplicationResponsive", stalledCollectorKeepsApplicationResponsive);
    runCase("armAfterBackgroundOutputDrains", armAfterBackgroundOutputDrains);
    runCase("pendingArmDeadlineAndStop", pendingArmDeadlineAndStop);
    runCase("nativePreparationDeadline", nativePreparationDeadline);
    runCase("explicitNativeClose", [&] { explicitNativeClose(app); });
    std::puts("Bounded input evidence, actual portable queue mutations, native fields and strict opt-in config passed");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
