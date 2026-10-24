/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "SessionControls.h"

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <stdexcept>

using namespace deskflow::windowshare;
namespace {
void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
void write(const QString &path, const QByteArray &bytes)
{
  QFile file(path);
  check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write owned fixture");
}
void until(const std::function<bool()> &condition)
{
  QEventLoop loop;
  QTimer deadline, observe;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  QObject::connect(&observe, &QTimer::timeout, &loop, [&] {
    if (condition())
      loop.quit();
  });
  deadline.start(10000);
  observe.start(10);
  if (!condition())
    loop.exec();
  check(condition(), "UI operation did not finish within its deadline");
}
template <class T> T *child(SessionControls &window, const char *name)
{
  auto *value = window.findChild<T *>(QLatin1String(name));
  check(value != nullptr, "named control exists");
  return value;
}
const char *backend = R"PY(
import sys,json
from pathlib import Path
operation=sys.argv[1]
root=Path(sys.argv[sys.argv.index('--profile')+1])
if operation=='init':
 root.mkdir();(root/'logs').mkdir();(root/'profile.json').write_text(json.dumps({'name':'MacBook','role':'mac','paired':False}))
 print('{}')
elif operation=='pair':
 value=json.loads((root/'profile.json').read_text());value.update(paired=True,peer='JackUbuntu',options={'macAddress':'192.168.1.101','screen':0,'clipboardText':False})
 (root/'profile.json').write_text(json.dumps(value));print('{}')
elif operation=='inspect':
 value=json.loads((root/'profile.json').read_text());value.update(pairingFile=str(root/'pairing.json'),pairing={'name':'MacBook','role':'mac'})
 print(json.dumps(value))
elif operation=='run':
 assert '--control-stdin' in sys.argv
 if (root/'reject-run').exists():
  print('Window sharing: Stop the regular Deskflow connection before starting window sharing');sys.exit(1)
 (root/'started').write_text('explicit-start')
 print('Window-sharing session starting.',flush=True)
 if (root/'flood').exists(): print('x'*70000,flush=True)
 sys.stdin.buffer.read()
 (root/'stopped').write_text('controller-eof')
 print('{}')
else: sys.exit(2)
)PY";

void lifecycle(const QString &screenshot)
{
  QTemporaryDir temporary;
  check(temporary.isValid(), "isolated fixture directory");
  const auto root = temporary.filePath("profile");
  const auto script = temporary.filePath("owned-helper.py"), core = temporary.filePath("unused-core");
  write(script, backend);
  write(core, "#!/bin/sh\nexit 99\n");
  check(
      QFile::setPermissions(core, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner),
      "fixture mode"
  );
  SessionControls window({QStandardPaths::findExecutable("python3"), script, core, core, root, "mac"});
  window.show();
  auto *start = child<QPushButton>(window, "startStop");
  auto *status = child<QLabel>(window, "sessionStatus");
  check(!start->isEnabled() && !QFileInfo::exists(root), "opening controls cannot create or run a session");
  child<QPushButton>(window, "createProfile")->click();
  until([&] { return status->text() == "Ready to pair"; });
  check(!start->isEnabled(), "unpaired setup cannot start");
  child<QLineEdit>(window, "peerFile")->setText(temporary.filePath("peer.json"));
  child<QLineEdit>(window, "macAddress")->setText("192.168.1.101");
  child<QPushButton>(window, "pairComputers")->click();
  until([&] { return status->text() == "Ready to start"; });
  check(start->isEnabled() && !QFileInfo::exists(root + "/started"), "pairing alone cannot run input");
  QCoreApplication::processEvents();
  if (!screenshot.isEmpty())
    check(window.grab().save(screenshot), "save offscreen controls preview");

  write(root + "/reject-run", "yes");
  start->click();
  until([&] { return status->text() == "Window sharing could not continue"; });
  check(child<QLabel>(window, "sessionDetail")->text().contains("Stop the regular Deskflow"), "conflict is visible");
  check(start->isEnabled() && !QFileInfo::exists(root + "/started"), "refused launch leaves Start available");
  QFile log(root + "/logs/controls.log");
  check(
      log.open(QIODevice::ReadOnly) && log.readAll().contains("Stop the regular Deskflow"), "startup refusal is saved"
  );
  log.close();
  check(QFile::remove(root + "/reject-run"), "clear only owned failure marker");

  start->click();
  until([&] { return status->text() == "Session started"; });
  check(start->text() == "Stop sharing", "live session exposes Stop");
  start->click();
  until([&] { return status->text() == "Sharing stopped"; });
  check(QFileInfo::exists(root + "/stopped"), "Stop closes the owned lifetime pipe");

  write(root + "/flood", "yes");
  start->click();
  until([&] { return status->text() == "Window sharing could not continue"; });
  check(start->isEnabled(), "oversized helper output stops without wedging controls");
  check(QFile::remove(root + "/flood"), "clear owned flood fixture");
  check(QFile::remove(root + "/stopped"), "clear owned Stop receipt");
  start->click();
  until([&] { return status->text() == "Session started"; });
  window.close();
  until([&] { return !window.isVisible(); });
  check(QFileInfo::exists(root + "/stopped"), "window close waits for the owned session to stop");

  check(QFile::remove(root + "/stopped"), "clear owned Quit receipt");
  SessionControls quitWindow({QStandardPaths::findExecutable("python3"), script, core, core, root, "mac"});
  quitWindow.show();
  auto *quitStatus = child<QLabel>(quitWindow, "sessionStatus");
  until([&] { return quitStatus->text() == "Ready to start"; });
  child<QPushButton>(quitWindow, "startStop")->click();
  until([&] { return quitStatus->text() == "Session started"; });
  QEvent quit(QEvent::Quit);
  QCoreApplication::sendEvent(QCoreApplication::instance(), &quit);
  until([&] { return !quitWindow.isVisible(); });
  check(QFileInfo::exists(root + "/stopped"), "application Quit drains the session before closing");
}
} // namespace

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  try {
    lifecycle(argc == 2 ? QString::fromLocal8Bit(argv[1]) : QString());
    std::puts("PASS: input-free controls creation, pairing, conflict, Start/Stop, output bounds and close lifecycle");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
