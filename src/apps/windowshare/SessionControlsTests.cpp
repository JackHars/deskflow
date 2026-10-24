/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "SessionControls.h"

#include <QApplication>
#include <QDir>
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
 root.mkdir();(root/'logs').mkdir();(root/'profile.json').write_text(json.dumps({'name':'MacBook','role':'mac','paired':False,'packageReady':True}))
 print('{}')
elif operation=='pair':
 value=json.loads((root/'profile.json').read_text());value.update(paired=True,peer='JackUbuntu',options={'macAddress':'192.168.1.101','screen':0,'clipboardText':False})
 (root/'profile.json').write_text(json.dumps(value));print('{}')
elif operation=='inspect':
 value=json.loads((root/'profile.json').read_text());value.update(pairingFile=str(root/'pairing.json'),pairing={'name':'MacBook','role':'mac'})
 if (root/'requires-update').exists(): value.update(packageReady=False,packageError='A new package is available')
 print(json.dumps(value))
elif operation=='provider':
 action=sys.argv[sys.argv.index('--operation')+1]
 value=json.loads((root/'support.json').read_text())
 if action=='install':
  assert value['action']=='install'
  value.update(code='disabled',action='enable',ready=False,detail='Enable Ubuntu support')
  (root/'support.json').write_text(json.dumps(value))
  print(json.dumps(dict(value,action='',detail='Sign out and back in')))
 elif action=='enable':
  assert value['action']=='enable'
  value.update(code='ready',action='',ready=True,detail='Ubuntu support ready')
  (root/'support.json').write_text(json.dumps(value))
  print(json.dumps(dict(value,ready=False,detail='Enable requested; check again')))
 else: print(json.dumps(value))
elif operation=='permissions':
 action=sys.argv[sys.argv.index('--operation')+1]
 with (root/'permission-commands').open('a') as log: log.write(action+'\n')
 path=root/'permissions.json'
 value=json.loads(path.read_text()) if path.exists() else {'ready':True,'action':'','detail':'Mac permissions are ready.','inputStarted':False}
 if action=='request':
  assert value['action']=='request'
  value.update(ready=True,action='',detail='Mac permissions are ready.')
  path.write_text(json.dumps(value))
  print(json.dumps(dict(value,ready=False,detail='Click Check Mac permissions')))
 else: print(json.dumps(value))
elif operation=='update':
 if (root/'reject-update').exists():
  print('Window sharing: Cannot update this saved setup');sys.exit(1)
 value=json.loads((root/'profile.json').read_text())
 if value['paired']:
  value['options'].update(macAddress=sys.argv[sys.argv.index('--mac-address')+1],screen=int(sys.argv[sys.argv.index('--screen')+1]),clipboardText='--clipboard-text' in sys.argv)
 (root/'profile.json').write_text(json.dumps(value));(root/'requires-update').unlink(missing_ok=True);print('{}')
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

  child<QPushButton>(window, "toggleSetup")->click();
  child<QLineEdit>(window, "macAddress")->setText("192.168.1.102");
  check(!start->isEnabled(), "unsaved connection changes cannot start with stale options");
  auto *update = child<QPushButton>(window, "updateProfile");
  check(update->isEnabled() && update->text() == "Save changes", "paired setup can save connection changes");
  update->click();
  until([&] { return status->text() == "Ready to start" && start->isEnabled(); });
  check(!QFileInfo::exists(root + "/started"), "saving changes cannot start sharing");

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

  check(QFile::remove(root + "/started"), "clear owned Start receipt before upgrade");
  write(root + "/requires-update", "yes");
  SessionControls upgradeWindow({QStandardPaths::findExecutable("python3"), script, core, core, root, "mac"});
  upgradeWindow.show();
  auto *upgradeStatus = child<QLabel>(upgradeWindow, "sessionStatus");
  until([&] { return upgradeStatus->text() == "Update saved setup"; });
  auto *upgrade = child<QPushButton>(upgradeWindow, "updateProfile");
  check(!child<QPushButton>(upgradeWindow, "startStop")->isEnabled(), "package update requires explicit approval");
  check(upgrade->isVisible() && upgrade->isEnabled(), "the saved setup exposes an update action");
  if (!screenshot.isEmpty())
    check(upgradeWindow.grab().save(screenshot + ".upgrade.png"), "save offscreen update controls preview");
  write(root + "/reject-update", "yes");
  upgrade->click();
  until([&] { return upgradeStatus->text() == "Window sharing could not continue"; });
  check(!child<QPushButton>(upgradeWindow, "startStop")->isEnabled(), "failed update does not approve Start");
  check(QFile::remove(root + "/reject-update"), "clear owned update failure");
  upgrade->click();
  until([&] { return upgradeStatus->text() == "Ready to start"; });
  check(child<QPushButton>(upgradeWindow, "startStop")->isEnabled(), "explicit update makes the saved setup ready");
  check(!QFileInfo::exists(root + "/started"), "explicit update does not start sharing");
  upgradeWindow.close();
}

void ubuntuSupport(const QString &screenshot)
{
  QTemporaryDir temporary;
  const auto root = temporary.filePath("profile"), script = temporary.filePath("helper.py");
  QDir().mkpath(root + "/logs");
  write(script, backend);
  const auto core = temporary.filePath("unused-core");
  write(core, "#!/bin/sh\nexit 99\n");
  QFile::setPermissions(core, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  write(
      root + "/profile.json",
      R"({"name":"Ubuntu","role":"ubuntu","paired":true,"packageReady":true,"peer":"MacBook","options":{"macAddress":"192.168.1.101","screen":0,"clipboardText":false}})"
  );
  write(
      root + "/support.json",
      R"({"ready":false,"code":"missing","action":"install","detail":"Install Ubuntu support","inputStarted":false})"
  );
  SessionControls window({QStandardPaths::findExecutable("python3"), script, core, core, root, "ubuntu"});
  window.show();
  auto *status = child<QLabel>(window, "sessionStatus"), *detail = child<QLabel>(window, "supportDetail");
  auto *start = child<QPushButton>(window, "startStop"), *setup = child<QPushButton>(window, "setupSupport");
  auto *refresh = child<QPushButton>(window, "checkSupport");
  until([&] { return status->text() == "Ubuntu support needs attention"; });
  check(
      !start->isEnabled() && setup->isEnabled() && setup->text() == "Install support", "missing support cannot start"
  );
  if (!screenshot.isEmpty())
    check(window.grab().save(screenshot + ".ubuntu.png"), "save offscreen Ubuntu setup preview");
  setup->click();
  until([&] { return detail->text() == "Sign out and back in"; });
  check(!start->isEnabled() && !QFileInfo::exists(root + "/started"), "installation cannot start input");
  refresh->click();
  until([&] { return setup->isEnabled() && setup->text() == "Enable support"; });
  setup->click();
  until([&] { return detail->text() == "Enable requested; check again"; });
  check(!start->isEnabled(), "enable request alone is not readiness");
  refresh->click();
  until([&] { return start->isEnabled(); });
  check(!QFileInfo::exists(root + "/started"), "readiness never starts input");
  write(
      root + "/support.json",
      R"({"ready":false,"code":"restart-needed","action":"","detail":"Sign out and back in","inputStarted":false})"
  );
  refresh->click();
  check(!start->isEnabled(), "refresh immediately revokes old readiness");
  until([&] { return status->text() == "Ubuntu support needs attention"; });
  check(!start->isEnabled() && !setup->isVisible(), "stale loaded support is not ready or automatically reenabled");
  window.close();
}
void macPermissions(const QString &screenshot)
{
  QTemporaryDir temporary;
  const auto root = temporary.filePath("profile");
  QDir().mkpath(root + "/logs");
  const auto script = temporary.filePath("owned-helper.py"), core = temporary.filePath("unused-core");
  write(script, backend);
  write(core, "#!/bin/sh\nexit 99\n");
  QFile::setPermissions(core, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  write(
      root + "/profile.json",
      R"({"name":"MacBook","role":"mac","paired":true,"packageReady":true,"peer":"JackUbuntu","options":{"macAddress":"192.168.1.101","screen":0,"clipboardText":false}})"
  );
  write(
      root + "/permissions.json",
      R"({"ready":false,"action":"request","detail":"Allow Accessibility and Screen Recording","inputStarted":false})"
  );
  SessionControls window({QStandardPaths::findExecutable("python3"), script, core, core, root, "mac"});
  window.show();
  auto *status = child<QLabel>(window, "sessionStatus"), *detail = child<QLabel>(window, "supportDetail");
  auto *start = child<QPushButton>(window, "startStop"), *request = child<QPushButton>(window, "setupSupport");
  auto *refresh = child<QPushButton>(window, "checkSupport");
  until([&] { return status->text() == "Mac permissions need attention"; });
  check(
      !start->isEnabled() && request->isEnabled() && request->text() == "Request permissions",
      "missing Mac permissions cannot start"
  );
  QFile commands(root + "/permission-commands");
  check(commands.open(QIODevice::ReadOnly) && commands.readAll() == "status\n", "opening controls requested access");
  commands.close();
  if (!screenshot.isEmpty())
    check(window.grab().save(screenshot + ".mac-permissions.png"), "save offscreen Mac permission preview");
  request->click();
  until([&] { return detail->text() == "Click Check Mac permissions"; });
  check(!start->isEnabled() && !QFileInfo::exists(root + "/started"), "permission request cannot start input");
  refresh->click();
  until([&] { return start->isEnabled(); });
  check(!QFileInfo::exists(root + "/started"), "readiness alone started sharing");
  write(root + "/permissions.json", R"({"ready":true,"action":"","detail":"invalid","inputStarted":true})");
  refresh->click();
  check(!start->isEnabled(), "permission refresh retained stale readiness");
  until([&] { return status->text() == "Setup check returned an invalid result"; });
  check(!start->isEnabled(), "invalid permission output enabled sharing");
  window.close();
}
} // namespace

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  try {
    lifecycle(argc == 2 ? QString::fromLocal8Bit(argv[1]) : QString());
    ubuntuSupport(argc == 2 ? QString::fromLocal8Bit(argv[1]) : QString());
    macPermissions(argc == 2 ? QString::fromLocal8Bit(argv[1]) : QString());
    std::puts("PASS: input-free controls creation, pairing, conflict, Start/Stop, output bounds and close lifecycle");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
