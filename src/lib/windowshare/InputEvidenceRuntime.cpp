/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "InputEvidenceRuntime.h"
#include "UniqueJson.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace deskflow::windowshare {
namespace {
constexpr const char *queueNames[]{
    "capture",
    "translation",
    "completed",
    "server-pending",
    "server-completed",
    "server-executing",
    "coordinator-operations",
    "coordinator-batch",
    "coordinator-preview",
    "event-outstanding",
    "event-dispatch",
    "event-all-outstanding",
    "event-all-dispatch",
    "private-output",
    "private-buffered-input",
    "private-incoming",
    "private-dispatch"
};
static_assert(sizeof(queueNames) / sizeof(*queueNames) == InputEvidenceRecorder::queueCount);
const char *roleName(InputEvidenceRole role)
{
  return role == InputEvidenceRole::Core ? "core" : role == InputEvidenceRole::Host ? "host" : "native";
}
QJsonObject failureDetails(const InputEvidenceRecorder &recorder)
{
  constexpr const char *sites[]{
      "unspecified",        "owner-bind",      "owner-revoke",           "queue-initialize",
      "queue-mutation",     "journal-reserve", "private-output-written", "private-buffer-observed",
      "private-buffer-read"
  };
  const auto queue = static_cast<std::size_t>(recorder.failureQueue());
  return {
      {"site", sites[static_cast<std::size_t>(recorder.failureSite())]},
      {"queue", queue < InputEvidenceRecorder::queueCount ? QJsonValue(queueNames[queue]) : QJsonValue()}
  };
}
void require(bool condition, const char *reason)
{
  if (!condition)
    throw std::runtime_error(reason);
}
bool sameFile(const struct stat &a, const struct stat &b)
{
#ifdef Q_OS_MACOS
  const auto am = a.st_mtimespec, bm = b.st_mtimespec, ac = a.st_ctimespec, bc = b.st_ctimespec;
#else
  const auto am = a.st_mtim, bm = b.st_mtim, ac = a.st_ctim, bc = b.st_ctim;
#endif
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size && a.st_mode == b.st_mode &&
         a.st_uid == b.st_uid && a.st_nlink == b.st_nlink && am.tv_sec == bm.tv_sec && am.tv_nsec == bm.tv_nsec &&
         ac.tv_sec == bc.tv_sec && ac.tv_nsec == bc.tv_nsec;
}
QString privatePath(const QString &path, bool existing)
{
  require(
      !path.isEmpty() && path.size() <= 4096 && !path.contains(QChar(0)) && QFileInfo(path).isAbsolute() &&
          QDir::cleanPath(path) == path,
      "Evidence paths must be canonical absolute paths"
  );
  const QFileInfo parent(QFileInfo(path).absolutePath());
  require(parent.canonicalFilePath() == parent.absoluteFilePath(), "Evidence directory must not contain symlinks");
  struct stat status{};
  const auto directory = QFile::encodeName(parent.absoluteFilePath());
  require(
      lstat(directory.constData(), &status) == 0 && S_ISDIR(status.st_mode) && status.st_uid == geteuid() &&
          !(status.st_mode & 0077),
      "Evidence requires a private owned directory"
  );
  if (existing)
    require(QFileInfo(path).canonicalFilePath() == path, "Evidence file must not be a symlink");
  return path;
}
QJsonObject readPrivate(const QString &path, qsizetype maximum)
{
  privatePath(path, true);
  const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
  require(fd >= 0, "Cannot open private evidence file");
  QFile file;
  if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
    ::close(fd);
    throw std::runtime_error("Cannot read evidence file");
  }
  struct stat before{}, after{};
  require(
      fstat(fd, &before) == 0 && S_ISREG(before.st_mode) && before.st_uid == geteuid() && before.st_nlink == 1 &&
          !(before.st_mode & 0077) && before.st_size > 0 && before.st_size <= maximum,
      "Invalid private evidence file"
  );
  const auto bytes = file.read(maximum + 1);
  require(
      fstat(fd, &after) == 0 && before.st_size == bytes.size() && sameFile(before, after),
      "Evidence file changed while reading"
  );
  requireUniqueJsonKeys(bytes);
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid evidence JSON");
  return document.object();
}
QString string(const QJsonObject &object, const char *name)
{
  require(object[name].isString(), "Missing evidence string");
  return object[name].toString();
}
QJsonArray numbers(const auto &values)
{
  QJsonArray array;
  for (auto value : values)
    array.append(QString::number(value));
  return array;
}
} // namespace
std::unique_ptr<InputEvidenceRuntime>
InputEvidenceRuntime::open(const QString &path, InputEvidenceRole role, std::uint64_t preparationTimeoutMs)
{
  require(
      preparationTimeoutMs > 0 && preparationTimeoutMs <= 600000 &&
          (role == InputEvidenceRole::Native || preparationTimeoutMs == 180000),
      "Only the native fixture may change its bounded preparation wait"
  );
  const auto config = readPrivate(path, 8192);
  require(
      config.size() == 6 && config["version"].isDouble() && config["version"].toDouble() == 1 &&
          string(config, "role") == QLatin1String(roleName(role)),
      "Invalid evidence configuration or role"
  );
  const auto run = string(config, "runId");
  require(QRegularExpression("^[A-Za-z0-9_-]{1,64}$").match(run).hasMatch(), "Invalid evidence run identity");
  const auto duration = config["durationMs"].toDouble(-1);
  require(
      config["durationMs"].isDouble() && std::isfinite(duration) && std::floor(duration) == duration && duration >= 1 &&
          duration <= 180000,
      "Evidence duration must be 1..180000 ms"
  );
  const auto output = privatePath(string(config, "output"), false);
  const auto control = privatePath(string(config, "control"), false);
  require(output != control && output != path && control != path, "Evidence paths must be distinct");
  auto result = std::unique_ptr<InputEvidenceRuntime>(new InputEvidenceRuntime);
  const int fd =
      ::open(QFile::encodeName(output).constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  require(fd >= 0, "Evidence output must be a new private file");
  if (!result->m_output.open(fd, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
    ::close(fd);
    throw std::runtime_error("Cannot open evidence output");
  }
  // Intentional process-lifetime storage: source callbacks never acquire a
  // shared_ptr, and shutdown cannot free a buffer beneath an in-flight copy.
  auto *recorder = new InputEvidenceRecorder(role);
  InputEvidenceRecorder *expected = nullptr;
  if (!inputEvidenceInstalled.compare_exchange_strong(expected, recorder, std::memory_order_acq_rel)) {
    delete recorder;
    throw std::runtime_error("Evidence recorder is one-shot per process");
  }
  result->m_recorder = recorder;
  if (role == InputEvidenceRole::Core) {
    require(
        deskflow::installEventInputObserver(recorder->eventObserver()),
        "Core event observer is one-shot and must precede EventQueue construction"
    );
  }
  result->m_run = run;
  result->m_control = control;
  result->m_duration = static_cast<std::uint64_t>(duration) * 1000000;
  result->m_created = inputEvidenceNow();
  result->m_preparationTimeout = preparationTimeoutMs * 1000000;
  QJsonArray queues;
  for (std::size_t i = 0; i != InputEvidenceRecorder::queueCount; ++i)
    if (recorder->expectedQueue(static_cast<InputEvidenceQueue>(i)))
      queues.append(queueNames[i]);
  result->write(
      {{"type", "header"},
       {"schema", "deskflow-input-evidence-v2"},
       {"runId", run},
       {"role", roleName(role)},
       {"pid", QString::number(getpid())},
       {"clockId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
       {"queues", queues},
       {"nativeFieldPreservation", "NOT_RUN"},
       {"observerProvenance", "NOT_RUN"}}
  );
  QObject::connect(&result->m_timer, &QTimer::timeout, result.get(), [self = result.get()] { self->tick(); });
  result->m_timer.start(10);
  return result;
}
void InputEvidenceRuntime::write(const QJsonObject &value)
{
  const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
  if (++m_rows > 2000000 || m_bytes + bytes.size() > 512ULL * 1024 * 1024 || m_output.write(bytes) != bytes.size()) {
    m_recorder->fail(InputEvidenceLoss::Output);
    return;
  }
  m_bytes += bytes.size();
}
bool InputEvidenceRuntime::requestRecord(const QString &privateNewPath)
{
  if (m_recordRequested || m_recorder->role() != InputEvidenceRole::Native ||
      m_recorder->state() != InputEvidenceState::Waiting)
    return false;
  m_recordRequested = true;
  try {
    const auto path = privatePath(privateNewPath, false);
    require(path != m_control, "Record request must be separate from recorder commands");
    const int fd =
        ::open(QFile::encodeName(path).constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    require(fd >= 0, "Record request must be a new private file");
    QFile file;
    if (!file.open(fd, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
      ::close(fd);
      throw std::runtime_error("Cannot write record request");
    }
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"runId", m_run}, {"command", "record"}})
                           .toJson(QJsonDocument::Compact);
    require(file.write(bytes) == bytes.size() && file.flush(), "Cannot complete record request");
    return true;
  } catch (...) {
    m_recorder->fail(InputEvidenceLoss::Output);
    return false;
  }
}
void InputEvidenceRuntime::drain()
{
  // Serialization belongs to this timer, never to the event tap or native
  // filter. Bound each turn so an evidence sink cannot monopolize input work.
  for (unsigned i = 0; i != InputEvidenceRecorder::capacity; ++i) {
    const auto row = m_recorder->pop();
    if (!row)
      break;
    QJsonObject object{{"timeNs", QString::number(row->time)}};
    if (row->kind == InputEvidenceKind::Queue) {
      object["type"] = "queue";
      object["queue"] = queueNames[static_cast<std::size_t>(row->queue)];
      object["enqueued"] = QString::number(row->enqueued);
      object["retired"] = QString::number(row->retired);
      object["depth"] = QString::number(row->depth);
      object["capacity"] = !row->capacity ? QJsonValue(QJsonValue::Null) : QJsonValue(QString::number(row->capacity));
    } else {
      object["type"] = row->kind == InputEvidenceKind::Source   ? "source"
                       : row->kind == InputEvidenceKind::Native ? "native"
                                                                : "hover";
      if (row->kind == InputEvidenceKind::Native) {
        object["sourcePid"] = QString::number(row->epoch);
        object["window"] = QString::number(row->sequence);
        object["tag"] = QString::number(row->otherSequence);
      } else {
        object["epoch"] = QString::number(row->epoch);
        object["sequence"] = QString::number(row->sequence);
      }
      if (row->kind == InputEvidenceKind::Source) {
        constexpr const char *effects[]{"key_down", "key_up", "key_repeat", "button_down", "button_up",
                                        "drag",     "scroll", "hover",      "unsupported"};
        object["effect"] = effects[std::min<std::uint64_t>(8, row->otherSequence)];
      }
      if (row->kind == InputEvidenceKind::Hover)
        object["retainedSequence"] = QString::number(row->otherSequence);
      else {
        object["cgTimestampNs"] = QString::number(row->raw.timestamp);
        object["flags"] = QString::number(row->raw.flags);
        object["raw"] = numbers(row->raw.fields);
      }
    }
    write(object);
  }
  if (!m_output.flush())
    m_recorder->fail(InputEvidenceLoss::Output);
}
void InputEvidenceRuntime::tick()
{
  const auto now = inputEvidenceNow();
  if (!m_armSeen && now >= m_nextReadiness && m_recorder->state() == InputEvidenceState::Waiting) {
    m_nextReadiness = now + 100000000;
    InputEvidenceReadiness readiness;
    const auto ready = m_recorder->readyOwner(&readiness);
    QJsonObject status{
        {"type", "readiness"},
        {"timeNs", QString::number(now)},
        {"readyCandidate", ready.has_value()},
        {"loss", int(m_recorder->loss())}
    };
    if (ready) {
      status["owner"] = numbers(ready->values);
      status["identity"] =
          QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(ready->identity.data()), 96).toHex());
    } else {
      QJsonArray queues;
      for (std::size_t i = 0; i != readiness.queues.size(); ++i)
        if (const auto blocked = readiness.queues[i])
          queues.append(
              QJsonObject{
                  {"queue", queueNames[i]},
                  {"uninitialized", bool(blocked & 1)},
                  {"mutationBusy", bool(blocked & 2)},
                  {"activityInFlight", bool(blocked & 4)},
                  {"nonempty", bool(blocked & 8)}
              }
          );
      status["preparation"] = QJsonObject{
          {"snapshotAvailable", readiness.inspected},
          {"ownerLive", readiness.ownerLive},
          {"captureIdle", readiness.captureIdle},
          {"blockedQueues", queues}
      };
    }
    if (m_recorder->loss() != InputEvidenceLoss::None)
      status["failure"] = failureDetails(*m_recorder);
    write(status);
    if (now - m_created >= m_preparationTimeout) {
      m_recorder->fail(InputEvidenceLoss::Deadline);
      m_recorder->stop();
    }
  }
  try {
    if (QFileInfo::exists(m_control)) {
      const auto command = readPrivate(m_control, 512);
      require(
          command.size() == 3 && command["version"].isDouble() && command["version"].toDouble() == 1 &&
              string(command, "runId") == m_run,
          "Invalid evidence control"
      );
      const auto action = string(command, "command");
      require(
          action == "arm" || action == "stop" ||
              (action == "close" && m_recorder->role() == InputEvidenceRole::Native && m_endWritten),
          "Unknown or premature evidence command"
      );
      if (action == "arm" && !m_armSeen && !m_stopSeen) {
        m_armSeen = true;
        const bool armed = m_recorder->arm(m_duration);
        const auto owner = m_recorder->ownerForRuntime();
        write(
            {{"type", "arm"},
             {"accepted", armed},
             {"timeNs", QString::number(m_recorder->start())},
             {"owner", numbers(owner.values)},
             {"identity",
              QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(owner.identity.data()), 96).toHex())}}
        );
      } else if (action == "stop" && !m_stopSeen) {
        m_stopSeen = true;
        m_recorder->stop();
      } else if (action == "close" && !m_closeSeen) {
        m_closeSeen = true;
        // Only the native fixture accepts this command, after its terminal
        // evidence is written and the coordinator has finished restoration.
        // quit() asks the fixture's manual-close guard, which deliberately
        // vetoes ordinary window closure while that coordinator is alive.
        // This timer runs on the application thread; exit() ends its event
        // loop directly, then main still flushes receipts and destroys Qt.
        QCoreApplication::exit(0);
      }
    }
  } catch (...) {
    m_recorder->fail(InputEvidenceLoss::Malformed);
    m_recorder->stop();
  }
  (void)m_recorder->recording();
  m_recorder->finish();
  drain();
  if ((m_recorder->state() == InputEvidenceState::Stopped || m_recorder->state() == InputEvidenceState::Denied) &&
      m_recorder->drained())
    endJournal();
}
void InputEvidenceRuntime::endJournal()
{
  if (m_endWritten)
    return;
  m_endWritten = true;
  // finish() froze payload collection only after its actual queue/active-copy
  // boundary. A failed or forced boundary carries sticky loss. Storage remains
  // installed for process lifetime, so late producer calls cannot use freed RAM.
  QJsonObject terminal{
      {"type", "end"},
      {"timeNs", QString::number(m_recorder->stoppedAt())},
      {"sourceEndNs", QString::number(m_recorder->sourceEnd())},
      {"state", int(m_recorder->state())},
      {"loss", int(m_recorder->loss())},
      {"rows", QString::number(m_recorder->written())},
      {"drained", m_recorder->drained()}
  };
  if (m_recorder->loss() != InputEvidenceLoss::None)
    terminal["failure"] = failureDetails(*m_recorder);
  write(terminal);
  m_output.flush();
  m_timer.setInterval(100); // Native fixture may receive its explicit clean-close command.
}
InputEvidenceRuntime::~InputEvidenceRuntime()
{
  m_timer.stop();
  if (!m_recorder || m_endWritten)
    return;
  m_recorder->stop();
  m_recorder->finish(true);
  drain();
  endJournal();
}
} // namespace deskflow::windowshare
