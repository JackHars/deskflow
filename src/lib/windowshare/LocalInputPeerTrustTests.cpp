/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputPeerTrust.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTextStream>
#include <stdexcept>
#include <unistd.h>

using namespace deskflow::windowshare;
namespace {
void check(bool value, const char *reason)
{
  if (!value)
    throw std::runtime_error(reason);
}
void write(const QString &path, const QByteArray &bytes)
{
  QFile file(path);
  check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot write peer trust fixture");
  check(file.write(bytes) == bytes.size(), "Cannot complete peer trust fixture write");
}
QByteArray run(const QString &program, const QStringList &arguments, int expectedExit = 0)
{
  QProcess process;
  process.start(program, arguments);
  check(process.waitForStarted(5000) && process.waitForFinished(30000), "Peer trust fixture process timed out");
  const auto output = process.readAllStandardOutput(), errors = process.readAllStandardError();
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != expectedExit) {
    QTextStream(stderr) << errors << output;
    check(false, "Peer trust fixture process failed");
  }
  return output;
}
struct Child
{
  QProcess process;
  ~Child()
  {
    if (process.state() != QProcess::NotRunning) {
      process.kill();
      process.waitForFinished(5000);
    }
  }
  void start(const QString &executable, const QStringList &arguments = {"--fixture-host"})
  {
    process.start(executable, arguments);
    check(process.waitForStarted(5000), "Cannot launch fixed packaged peer fixture");
    QByteArray ready;
    for (int attempt = 0; attempt < 30 && !ready.contains('\n'); ++attempt) {
      process.waitForReadyRead(1000);
      ready += process.readAllStandardOutput();
      if (process.state() == QProcess::NotRunning)
        break;
    }
    if (ready != "READY\n") {
      QTextStream(stderr) << ready << process.readAllStandardError();
      check(false, "Packaged child did not independently verify its parent");
    }
  }
  void finish()
  {
    process.write("done\n");
    check(process.waitForBytesWritten(5000) && process.waitForFinished(5000), "Cannot finish packaged peer fixture");
    check(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, "Packaged child exited incorrectly");
  }
};
void hold()
{
  QTextStream ready(stdout);
  ready << "READY\n";
  ready.flush();
  QFile input;
  check(input.open(stdin, QIODevice::ReadOnly), "Cannot wait for fixture completion");
  check(input.readLine() == "done\n", "Unexpected fixture completion token");
}
int hostFixture(bool expectDenial)
{
  QString error;
  auto trust = LocalInputPeerTrust::forPackagedPeer(LocalInputRole::Core, &error);
  if (expectDenial) {
    check(!trust && !error.isEmpty(), "Direct nested host launch unexpectedly acquired packaged trust");
    QTextStream(stdout) << "DENIED\n";
    return 0;
  }
  if (!trust)
    QTextStream(stderr) << error << '\n';
  check(bool(trust), "Packaged child cannot establish exact core trust");
  const auto parent = localInputProcess(getppid());
  check(parent && trust->verify(*parent), "Packaged child rejected its exact live core parent");
  auto wrong = *parent;
  ++wrong.startStamp;
  check(!trust->verify(wrong), "Packaged child accepted an incorrect parent birth identity");
  check(!trust->verify(*currentLocalInputProcess()), "Packaged child accepted itself as core parent");
  hold();
  return 0;
}
int coreFixture(const QString &mode, const QString &otherPeer)
{
  QString error;
  auto trust = LocalInputPeerTrust::forPackagedPeer(LocalInputRole::Host, &error);
  if (mode == "deny") {
    check(!trust && !error.isEmpty(), "Broken sealed bundle unexpectedly acquired trust");
    QTextStream(stdout) << "DENIED\n";
    return 0;
  }
  if (!trust)
    QTextStream(stderr) << error << '\n';
  check(bool(trust), "Packaged core cannot establish exact host trust");
  check(!LocalInputPeerTrust::forPackagedPeer(LocalInputRole::Core), "Core acquired the reversed packaged role");
  const auto sibling = QFileInfo(QCoreApplication::applicationFilePath()).dir().filePath("deskflow-windowshare");
  check(trust->executable() == QFileInfo(sibling).canonicalFilePath(), "Trusted executable is not the fixed sibling");
  Child child;
  child.start(trust->executable());
  const auto expected = localInputProcess(child.process.processId());
  check(expected && trust->verify(*expected), "Packaged core rejected its exact live child");
  auto wrong = *expected;
  ++wrong.startStamp;
  check(!trust->verify(wrong), "Packaged core accepted a stale child birth identity");
  check(!trust->verify(*currentLocalInputProcess()), "Core accepted its own PID as the host");
  if (mode == "tamper") {
    const auto resources = QFileInfo(QFileInfo(sibling).dir().path()).dir().filePath("Resources/proof.txt");
    write(resources, "altered-after-pin\n");
    check(!trust->verify(*expected), "Pinned trust ignored a changed sealed bundle resource");
    write(resources, "sealed-fixture\n");
    check(trust->verify(*expected), "Exact restored sealed bundle did not verify with fresh static objects");
  }
  if (mode == "wrong-path") {
    Child copied;
    copied.start(otherPeer, {"--fixture-hold"});
    const auto wrongPath = localInputProcess(copied.process.processId());
    check(wrongPath && !trust->verify(*wrongPath), "Same-user signed copied role at another path was accepted");
    copied.finish();
  }
  if (mode == "reseal") {
    const auto contents = QFileInfo(QFileInfo(sibling).dir().path()).dir();
    const auto bundle = QFileInfo(contents.path()).dir().path();
    QTemporaryDir replacement(QFileInfo(bundle).dir().filePath("replacement-XXXXXX"));
    check(replacement.isValid(), "Cannot create independent resealed bundle fixture");
    const auto clone = replacement.filePath("New.app"), retired = bundle + ".retired";
    run("/usr/bin/ditto", {bundle, clone});
    write(clone + "/Contents/Resources/proof.txt", "independently-resealed\n");
    run("/usr/bin/codesign", {"--force", "--sign", "-", clone});
    run("/usr/bin/codesign", {"--verify", "--strict", "--deep", clone});
    check(QDir().rename(bundle, retired) && QDir().rename(clone, bundle), "Cannot replace fixture bundle atomically");
    const bool acceptedReplacement = trust->verify(*expected);
    const bool acceptedNewAnchor = bool(LocalInputPeerTrust::forPackagedPeer(LocalInputRole::Host));
    check(
        QDir().rename(bundle, clone) && QDir().rename(retired, bundle), "Cannot restore original running fixture bundle"
    );
    check(!acceptedReplacement, "Resealed disk bundle replaced the already-running bundle anchor");
    check(!acceptedNewAnchor, "Factory accepted a newly resealed disk bundle as the live process anchor");
    check(trust->verify(*expected), "Restored exact live bundle anchor did not verify");
  }
  child.finish();
  check(!trust->verify(*expected), "Exited packaged peer retained process trust");
  QTextStream(stdout) << "VERIFIED\n";
  return 0;
}
QString makeBundle(const QString &directory, const QString &name)
{
  const auto bundle = QDir(directory).filePath(name + ".app");
  const auto contents = bundle + "/Contents";
  check(
      QDir().mkpath(contents + "/MacOS") && QDir().mkpath(contents + "/Resources"),
      "Cannot create signed fixture bundle"
  );
  const auto source = QCoreApplication::applicationFilePath();
  for (const auto &role : {"deskflow-core", "deskflow-windowshare"}) {
    const auto path = contents + "/MacOS/" + role;
    check(QFile::copy(source, path), "Cannot copy peer trust fixture executable");
    check(
        QFile::setPermissions(
            path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                      QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther
        ),
        "Cannot make peer fixture executable"
    );
  }
  write(contents + "/Info.plist", R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>org.deskflow.test.inputpeers</string>
<key>CFBundleExecutable</key><string>deskflow-core</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1</string>
</dict></plist>)");
  write(contents + "/Resources/proof.txt", "sealed-fixture\n");
  run("/usr/bin/codesign", {"--force", "--sign", "-", "--identifier", "org.deskflow.test.input-host",
                            contents + "/MacOS/deskflow-windowshare"});
  run("/usr/bin/codesign", {"--force", "--sign", "-", bundle});
  run("/usr/bin/codesign", {"--verify", "--strict", "--deep", bundle});
  return bundle;
}
} // namespace

int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  try {
    const auto arguments = application.arguments();
    if (arguments.size() == 2 && arguments[1] == "--fixture-hold") {
      hold();
      return 0;
    }
    if (arguments.size() >= 2 && arguments[1] == "--fixture-host")
      return hostFixture(arguments.size() == 3 && arguments[2] == "deny");
    if ((arguments.size() == 3 || arguments.size() == 4) && arguments[1] == "--fixture-core")
      return coreFixture(arguments[2], arguments.value(3));
    check(arguments.size() == 1, "Unexpected peer trust fixture arguments");
    QString error;
    check(
        !LocalInputPeerTrust::forPackagedPeer(LocalInputRole::Host, &error) && !error.isEmpty(),
        "Unpackaged process acquired private peer trust"
    );
    QTemporaryDir temporary;
    check(temporary.isValid(), "Cannot create isolated peer trust fixtures");
    const auto unpackaged = temporary.filePath("deskflow-core");
    check(QFile::copy(application.applicationFilePath(), unpackaged), "Cannot copy unpackaged role fixture");
    check(
        run(unpackaged, {"--fixture-core", "deny"}) == "DENIED\n", "Correct basename bypassed signed-bundle requirement"
    );
    const auto bundle = makeBundle(temporary.path(), "InputPeers");
    const auto core = bundle + "/Contents/MacOS/deskflow-core";
    const auto host = bundle + "/Contents/MacOS/deskflow-windowshare";
    check(run(core, {"--fixture-core", "pass"}) == "VERIFIED\n", "Signed peer mutual verification failed");
    check(run(core, {"--fixture-core", "tamper"}) == "VERIFIED\n", "Post-pin resource tamper was not rejected");
    check(run(core, {"--fixture-core", "reseal"}) == "VERIFIED\n", "Resealed bundle replaced live process authority");
    const auto copiedBundle = makeBundle(temporary.path(), "CopiedPeers");
    check(
        run(core, {"--fixture-core", "wrong-path", copiedBundle + "/Contents/MacOS/deskflow-windowshare"}) ==
            "VERIFIED\n",
        "Signed copied peer from another bundle was accepted"
    );
    check(run(host, {"--fixture-host", "deny"}) == "DENIED\n", "Unrelated direct host launch was accepted");
    write(bundle + "/Contents/Resources/proof.txt", "broken-before-start\n");
    check(run(core, {"--fixture-core", "deny"}) == "DENIED\n", "Invalid sealed resources were accepted");
    write(bundle + "/Contents/Resources/proof.txt", "sealed-fixture\n");
    const auto saved = temporary.filePath("original-host");
    check(QFile::rename(host, saved) && QFile::link(saved, host), "Cannot create sibling escape fixture");
    check(run(core, {"--fixture-core", "deny"}) == "DENIED\n", "Symlink peer escape was accepted");
    check(QFile::remove(host) && QFile::rename(saved, host), "Cannot restore isolated sibling fixture");
    run("/usr/bin/codesign", {"--force", "--sign", "-", "--identifier", "org.deskflow.test.unrelated", host});
    check(
        run(core, {"--fixture-core", "deny"}) == "DENIED\n",
        "Unrelated same-user ad-hoc signed replacement was accepted"
    );
    QTextStream(stdout) << "PASS: signed packaged peer identity, live anchor, exact roles, birth checks, resource "
                           "tampering and direct-launch denial\n";
    return 0;
  } catch (const std::exception &failure) {
    QTextStream(stderr) << "FAIL: " << failure.what() << '\n';
    return 1;
  }
}
