/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "PhysicalTestReport.h"
#include <QApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

using namespace deskflow::windowshare;
namespace {
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
QByteArray row(int sequence, const QString &step, const QString &state, const QString &message)
{
  return QJsonDocument(
             QJsonObject{
                 {"schema", "deskflow-physical-diagnostic-v1"},
                 {"sequence", sequence},
                 {"step", step},
                 {"state", state},
                 {"message", message},
                 {"utc", "2026-10-15T23:52:10.636Z"},
                 {"elapsedMs", sequence * 10},
                 {"details", QJsonObject{}}
             }
         ).toJson(QJsonDocument::Compact) +
         '\n';
}
void append(const QString &path, const QByteArray &data)
{
  QFile file(path);
  check(file.open(QIODevice::Append), "cannot open diagnostic fixture");
  check(file.write(data) == data.size(), "cannot write diagnostic fixture");
}
} // namespace

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  try {
    QTemporaryDir directory;
    const auto path = directory.filePath("diagnostics.jsonl");
    append(path, row(1, "Hidden display", "started", "Creating display"));
    PhysicalTestReport report(path);
    report.show(); // CTest forces offscreen; never a native physical window.
    check(report.sequence() == 1 && report.firstFailure().isEmpty(), "initial stage missing");
    const auto failure = row(2, "Hidden display", "failed", "Geometry rejected <plain text>");
    append(path, failure.left(failure.size() / 2));
    report.refresh();
    check(report.sequence() == 1, "partial publication was accepted");
    append(path, failure.mid(failure.size() / 2));
    report.refresh();
    const auto first = report.firstFailure();
    check(first.contains("Geometry rejected <plain text>"), "first failure was not retained as plain text");
    append(path, row(3, "Companion", "failed", "Channel lost"));
    append(path, row(4, "Cleanup", "completed", "Owned input stopped"));
    append(path, row(5, "finished", "observed", "Test finished"));
    report.refresh();
    QCoreApplication::processEvents();
    check(report.sequence() == 5 && report.firstFailure() == first, "cleanup overwrote the initial failure");
    check(
        report.isVisible() && report.timeline().contains("Channel lost") &&
            report.timeline().contains("Owned input stopped"),
        "report closed or lost later diagnostics"
    );
    QFile::remove(path);
    report.refresh();
    check(report.isVisible() && report.firstFailure() == first, "missing file erased the retained failure");
    report.close();
    check(!report.isVisible(), "explicit dismissal did not close the report");
    std::puts("Persistent failure, partial lines, complete cleanup timeline and explicit dismissal passed offscreen");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
