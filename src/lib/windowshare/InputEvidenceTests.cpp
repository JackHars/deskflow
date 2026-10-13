/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "InputEvidenceRuntime.h"
#include "OriginalInputCapturedInbox.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace deskflow::windowshare;
namespace deskflow::windowshare {
struct InputEvidenceTestAccess
{
  static bool lockBoundary(InputEvidenceRecorder &recorder)
  {
    return recorder.lockCaptureBoundary();
  }
  static bool unlockBoundary(InputEvidenceRecorder &recorder)
  {
    return recorder.unlockCaptureBoundary();
  }
};
} // namespace deskflow::windowshare
namespace {
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
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
      result->initialize(static_cast<InputEvidenceQueue>(i), result.get(), 256);
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
  check(concurrent->beginMutation(InputEvidenceQueue::Capture, concurrent.get()), "queue lock failed");
  check(
      !concurrent->beginMutation(InputEvidenceQueue::Capture, concurrent.get()), "reentrant mutation waited or entered"
  );
  concurrent->endMutation(InputEvidenceQueue::Capture, 0, 0);
  check(concurrent->loss() == InputEvidenceLoss::Contention && !concurrent->arm(100), "contention silently recovered");
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
      recorder->initialize(queue, recorder.get(), 256);
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
      recorder->initialize(queue, recorder.get(), 256);
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
    denied([&] { InputEvidenceRuntime::open(configPath, InputEvidenceRole::Native); });
  }
  for (const auto &duration : {QJsonValue(0), QJsonValue(180001), QJsonValue(1.5), QJsonValue("120000")}) {
    auto changed = valid;
    changed["durationMs"] = duration;
    write(QJsonDocument(changed).toJson());
    denied([&] { InputEvidenceRuntime::open(configPath, InputEvidenceRole::Native); });
  }
  write(QJsonDocument(valid).toJson());
  denied([&] { InputEvidenceRuntime::open(configPath, InputEvidenceRole::Core); });
  auto duplicate = QJsonDocument(valid).toJson(QJsonDocument::Compact);
  duplicate.insert(1, "\"version\":1,");
  write(duplicate);
  denied([&] { InputEvidenceRuntime::open(configPath, InputEvidenceRole::Native); });
  write(QJsonDocument(valid).toJson());
  auto runtime = InputEvidenceRuntime::open(configPath, InputEvidenceRole::Native);
  check(
      runtime->recorder().state() == InputEvidenceState::Waiting && runtime->recorder().written() == 0,
      "configuration implicitly armed recording"
  );
  denied([&] { InputEvidenceRuntime::open(configPath, InputEvidenceRole::Native); });
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
    runtime = InputEvidenceRuntime::open(configPath, InputEvidenceRole::Native);
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
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  try {
    baselinesAndLifecycle();
    stickyFailures();
    actualPortableQueues();
    revokeDuringDrain();
    nativeFields();
    privateConfiguration();
    std::puts("Bounded input evidence, actual portable queue mutations, native fields and strict opt-in config passed");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
