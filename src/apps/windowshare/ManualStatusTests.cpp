/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ManualStatus.h"

#include <QCoreApplication>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

using namespace deskflow::windowshare;
using Kind = ManualStatusRead::Kind;

namespace {
void check(bool okay, const char *message)
{
  if (!okay)
    throw std::runtime_error(message);
}
struct Fixture
{
  QTemporaryDir temporary;
  int directory = -1;
  Fixture()
  {
    check(temporary.isValid(), "create isolated directory");
    directory = open(QFile::encodeName(temporary.path()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    check(directory >= 0, "open isolated directory");
  }
  ~Fixture()
  {
    close(directory);
  }
  QString path() const
  {
    return temporary.filePath("status.json");
  }
  void bytes(const QByteArray &value)
  {
    QSaveFile file(path());
    check(file.open(QIODevice::WriteOnly), "open publication");
    check(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner), "private publication");
    check(file.write(value) == value.size() && file.commit(), "publish replacement");
  }
  void publish(const char *state = "ready", int remaining = 300)
  {
    bytes(QJsonDocument(
              QJsonObject{{"version", 1}, {"state", state}, {"remainingSeconds", remaining}}
    ).toJson(QJsonDocument::Compact));
  }
  ManualStatusRead read()
  {
    return readManualStatus(directory, "status.json", 120);
  }
};

void replacementRace()
{
  for (auto stage : {ManualStatusReadStage::Opened, ManualStatusReadStage::Read}) {
    Fixture f;
    f.publish();
    ManualStatusHeartbeat heartbeat;
    check(!heartbeat.observe(f.read(), 0), "first old publication cannot enable Start");
    f.publish();
    check(heartbeat.observe(f.read(), 250), "a fresh replacement establishes heartbeat");
    auto raced = readManualStatus(f.directory, "status.json", 120, [&](ManualStatusReadStage at) {
      if (stage == at)
        f.publish("active", 120);
    });
    check(raced.kind == Kind::Replaced && !raced.status, "actual rename detected without accepting unlinked bytes");
    check(heartbeat.observe(raced, 350), "concurrent replacement preserves an unexpired lease");
    check(heartbeat.changedAt() == 250 && heartbeat.last()->state == "ready", "race cannot renew or activate");
    check(heartbeat.observe(raced, 2249), "old lease is valid before its fixed boundary");
    check(!heartbeat.observe(raced, 2250), "repeated collisions cannot extend the two-second lease");
    auto current = f.read();
    check(current.kind == Kind::Accepted && current.status->state == "active", "next poll reads whole new state");
    check(heartbeat.observe(current, 2300), "independent successful fresh read renews heartbeat");
    check(!heartbeat.observe(current, 4300), "unchanged file never renews heartbeat");
  }
}

void strictPublications()
{
  Fixture f;
  for (auto value :
       {QByteArray("{\"version\":1,\"state\":\"ready\",\"state\":\"active\",\"remainingSeconds\":20}"),
        QByteArray("{\"version\":1,\"state\":\"active\",\"remainingSeconds\":0}"),
        QByteArray("{\"version\":1,\"state\":\"active\",\"remainingSeconds\":121}"),
        QByteArray("{\"version\":1,\"state\":\"ready\",\"remainingSeconds\":301}"),
        QByteArray("{\"version\":1,\"state\":\"stopped\",\"remainingSeconds\":1}"),
        QByteArray("{\"version\":1,\"state\":\"active\",\"remainingSeconds\":0.5}"), QByteArray("broken"),
        QByteArray(513, 'x'), QByteArray()}) {
    f.bytes(value);
    check(f.read().kind == Kind::Invalid, "malformed publication fails closed");
  }
  f.publish();
  check(chmod(QFile::encodeName(f.path()).constData(), 0644) == 0, "make unsafe permissions");
  check(f.read().kind == Kind::Invalid, "publicly readable status rejected");
  f.publish();
  const auto alias = QFile::encodeName(f.temporary.filePath("alias"));
  check(link(QFile::encodeName(f.path()).constData(), alias.constData()) == 0, "create hardlink");
  check(f.read().kind == Kind::Invalid, "multiply linked publication rejected");
  check(unlink(alias.constData()) == 0, "remove hardlink");
  check(QFile::rename(f.path(), QString::fromUtf8(alias)), "move target");
  check(symlink(alias.constData(), QFile::encodeName(f.path()).constData()) == 0, "create symlink");
  check(f.read().kind == Kind::Invalid, "symlink publication rejected");
  check(QFile::remove(f.path()), "remove symlink");
  check(f.read().kind == Kind::Invalid, "missing publication rejected");
  check(readManualStatus(f.directory, "../status.json", 120).kind == Kind::Invalid, "path traversal rejected");
}

void unsafeInterleaving()
{
  Fixture f;
  f.publish();
  auto changed = readManualStatus(f.directory, "status.json", 120, [&](ManualStatusReadStage at) {
    if (at == ManualStatusReadStage::Read) {
      QFile file(f.path());
      check(file.open(QIODevice::WriteOnly | QIODevice::Append), "in-place mutation");
      check(file.write(" ") == 1, "change size during read");
    }
  });
  check(changed.kind == Kind::Invalid, "in-place mutation is not an atomic publication");
  for (bool unsafeReplacement : {false, true}) {
    f.publish();
    auto removed = readManualStatus(f.directory, "status.json", 120, [&](ManualStatusReadStage at) {
      if (at == ManualStatusReadStage::Read) {
        if (unsafeReplacement) {
          f.publish();
          check(chmod(QFile::encodeName(f.path()).constData(), 0644) == 0, "unsafe replacement");
        } else {
          check(QFile::remove(f.path()), "delete publication");
        }
      }
    });
    check(removed.kind == Kind::Invalid, "unlink alone or unsafe replacement cannot preserve a lease");
  }
  ManualStatusHeartbeat heartbeat;
  f.publish();
  heartbeat.observe(f.read(), 0);
  f.publish();
  check(heartbeat.observe(f.read(), 250), "establish heartbeat before unsafe read");
  check(!heartbeat.observe(changed, 251), "invalid publication fails immediately within lease");
  check(heartbeat.changedAt() == 250, "invalid publication cannot renew lease");
  f.publish("stopped", 0);
  check(heartbeat.observe(f.read(), 300) && heartbeat.last()->state == "stopped", "fresh stop observed immediately");
  ManualStatusHeartbeat uninitialized;
  check(!uninitialized.observe({Kind::Replaced, {}, "race"}, 1), "race cannot establish initial heartbeat");
}
} // namespace

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  try {
    for (auto test :
         {std::pair{"atomic replacement and fixed heartbeat lease", &replacementRace},
          std::pair{"strict publication validation", &strictPublications},
          std::pair{"unsafe interleavings", &unsafeInterleaving}}) {
      std::fprintf(stderr, "START %s\n", test.first);
      test.second();
      std::fprintf(stderr, "PASS %s\n", test.first);
    }
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL %s\n", error.what());
    return 1;
  }
}
