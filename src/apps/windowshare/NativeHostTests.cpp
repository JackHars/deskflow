/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeHost.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <unistd.h>

// Internal filesystem primitive; exercising it never enumerates native windows.
namespace deskflow::windowshare::native {
bool persistJournal(const QString &path, const QJsonArray &records, QString *diagnostic);
}

using deskflow::windowshare::NativeHost;
namespace {
int failures = 0;
void check(bool result, const char *message)
{
  if (!result) {
    ++failures;
    QTextStream(stderr) << "FAIL: " << message << '\n';
  }
}
void write(const QString &path, const QByteArray &bytes)
{
  QFile file(path);
  check(file.open(QIODevice::WriteOnly), "create fixture journal");
  check(file.write(bytes) == bytes.size(), "write fixture journal");
}
QByteArray read(const QString &path)
{
  QFile file(path);
  file.open(QIODevice::ReadOnly);
  return file.readAll();
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  QTemporaryDir directory;
  check(directory.isValid(), "temporary journal directory");
  if (!directory.isValid())
    return 1;
  const auto path = directory.filePath("restore.json");
  {
    NativeHost host(path);
    // This deliberately never starts the event loop: --recover has the same
    // lifecycle, and must not depend on the deferred constructor callback.
    check(host.recover(), "synchronous recovery accepts a missing journal");
    check(!QFile::exists(path), "probe recovery does not create an unnecessary journal");
  }
  const QByteArray malformed("{partial power-loss data");
  write(path, malformed);
  {
    NativeHost host(path);
    bool reported = false;
    host.onError = [&](QString) { reported = true; };
    check(!host.recover(), "corrupt recovery journal fails closed");
    check(reported, "synchronous recovery reports corruption before exit");
    host.restoreAll();
  }
  check(read(path) == malformed, "corrupt journal survives failed recovery and destruction");
  const QByteArray oversized(1024 * 1024 + 1, 'x');
  write(path, oversized);
  {
    NativeHost host(path);
    check(!host.recover(), "oversized recovery journal rejected before parse");
  }
  check(read(path) == oversized, "oversized journal remains available for manual recovery");
  write(path, R"({"version":42,"windows":[]})");
  {
    NativeHost host(path);
    check(!host.recover(), "unknown journal version is never silently discarded");
  }
  check(read(path) == QByteArray(R"({"version":42,"windows":[]})"), "unknown journal schema preserved");
  write(path, R"({"version":1,"windows":[]})");
  {
    NativeHost host(path);
    check(host.recover(), "empty journal recovers without OS permissions");
    check(host.recover(), "successful synchronous recovery is idempotent");
  }
  const auto document = QJsonDocument::fromJson(read(path));
  check(
      document.isObject() && document["version"].toInt() == 1 && document["windows"].isArray(),
      "successful recovery leaves valid journal"
  );
  const auto mode = QFile::permissions(path);
  check(
      !(mode & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther)),
      "journal excludes group and other readers"
  );
  {
    const auto boundedPath = directory.filePath("bounded.json");
    const QString fullTitle(700, QChar(u'\u20ac'));
    QJsonObject record{
        {"windowId", 123},      {"pid", 456},
        {"launch", 1234567890}, {"bundle", "org.deskflow.test"},
        {"title", fullTitle},   {"original", QJsonObject{{"x", 50}, {"y", 50}, {"width", 640}, {"height", 480}}},
        {"session", 123456},    {"epoch", 1}
    };
    QString diagnostic;
    check(
        deskflow::windowshare::native::persistJournal(boundedPath, {record}, &diagnostic),
        "persist complete long native identity"
    );
    check(
        QJsonDocument::fromJson(read(boundedPath))["windows"].toArray().at(0).toObject() == record,
        "journal preserves the complete title and native identity"
    );
    record["title"] = QString();
    const auto overhead = QJsonDocument(QJsonObject{{"version", 1}, {"windows", QJsonArray{record}}})
                              .toJson(QJsonDocument::Compact)
                              .size();
    record["title"] = QString(1024 * 1024 - overhead, QChar(u'x'));
    check(
        deskflow::windowshare::native::persistJournal(boundedPath, {record}, &diagnostic),
        "journal accepts the exact recovery byte limit"
    );
    const auto atLimit = read(boundedPath);
    check(atLimit.size() == 1024 * 1024, "boundary journal occupies exactly the supported bytes");
    record["title"] = record["title"].toString() + QChar(u'x');
    check(
        !deskflow::windowshare::native::persistJournal(boundedPath, {record}, &diagnostic),
        "journal rejects one byte over the recovery limit"
    );
    check(read(boundedPath) == atLimit, "oversized replacement leaves the prior recovery journal intact");
    record["title"] = QString(512 * 1024, QChar(u'\u20ac'));
    check(
        !deskflow::windowshare::native::persistJournal(boundedPath, {record}, &diagnostic),
        "journal limit measures serialized UTF-8 bytes, not characters"
    );
    check(read(boundedPath) == atLimit, "UTF-8 overflow preserves the existing journal");
    record["title"] = QString(512 * 1024, QChar(u'"'));
    check(
        !deskflow::windowshare::native::persistJournal(boundedPath, {record}, &diagnostic),
        "journal limit accounts for JSON escaping"
    );
    check(read(boundedPath) == atLimit, "escaped overflow preserves the existing journal");
  }
  if (geteuid() != 0) {
    QTemporaryDir restricted;
    check(restricted.isValid(), "restricted journal directory");
    const auto restrictedPath = restricted.filePath("restore.json");
    const QByteArray empty(R"({"version":1,"windows":[]})");
    write(restrictedPath, empty);
    check(
        QFile::setPermissions(restricted.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner),
        "restrict journal directory writes"
    );
    {
      NativeHost host(restrictedPath);
      check(!host.recover(), "recovery fails when the completed journal cannot be persisted");
      check(read(restrictedPath) == empty, "failed durable rewrite preserves the prior journal");
      check(
          QFile::setPermissions(
              restricted.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
          ),
          "restore journal directory writes"
      );
      check(host.recover(), "failed durable recovery can be retried on the same host");
    }
  }
  const auto permissions = NativeHost::permissions();
  check(
      permissions["screenCapture"].isBool() && permissions["accessibility"].isBool(),
      "permission probe is explicit and does not request permission"
  );
  QTextStream(stdout) << (failures ? "FAIL" : "PASS") << ": native recovery journal regression tests\n";
  return failures ? 1 : 0;
}
