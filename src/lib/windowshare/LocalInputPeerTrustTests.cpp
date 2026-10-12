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
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>

using namespace deskflow::windowshare;
static_assert(!std::is_default_constructible_v<LocalInputRestrictedLibraryAdmissionSnapshot>);
static_assert(!std::is_convertible_v<LocalInputRestrictedLibraryAdmissionSnapshot, bool>);
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
std::optional<LocalInputRestrictedLibraryAdmissionSnapshot>
inspectAdmission(const LocalInputPeerTrust &trust, LocalInputProcess peer, LocalInputRole role, bool expectedAdmission)
{
  QString error = QStringLiteral("inspection did not replace the diagnostic");
  const auto started = std::chrono::steady_clock::now();
  auto snapshot = trust.inspectRestrictedLibraryAdmission(peer, &error);
  if (expectedAdmission && !snapshot)
    QTextStream(stderr) << "Restricted fixture: " << error << '\n';
  check(bool(snapshot) == expectedAdmission, "Unexpected restricted-library-admission result");
  if (snapshot) {
    const auto self = currentLocalInputProcess();
    check(error.isEmpty(), "Successful admission snapshot retained a failure diagnostic");
    check(
        self && snapshot->selfProcess() == *self && snapshot->peerProcess() == peer && snapshot->peerRole() == role &&
            snapshot->anchorProcess() == (role == LocalInputRole::Host ? *self : peer),
        "Admission snapshot does not bind both roles and the live bundle main"
    );
    check(
        snapshot->selfCodeHash().size() == 20 && snapshot->peerCodeHash().size() == 20 &&
            snapshot->anchorCodeHash().size() == 20 && snapshot->teamIdentifier().size() == 10,
        "Admission snapshot lacks exact code hashes or its established Team identity"
    );
    check(
        started <= snapshot->inspectionStartedAt() &&
            snapshot->inspectionStartedAt() <= snapshot->inspectionFinishedAt() &&
            snapshot->inspectionFinishedAt() <= std::chrono::steady_clock::now(),
        "Admission snapshot has invalid monotonic observation bounds"
    );
  } else {
    check(!error.isEmpty(), "Rejected admission has no diagnostic");
  }
  check(trust.verify(peer), "Optional stronger policy changed ordinary peer trust");
  return snapshot;
}
int hostFixture(const QString &mode)
{
  QString error;
  auto trust = LocalInputPeerTrust::forPackagedPeer(LocalInputRole::Core, &error);
  if (mode == "deny") {
    check(!trust && !error.isEmpty(), "Direct nested host launch unexpectedly acquired packaged trust");
    QTextStream(stdout) << "DENIED\n";
    return 0;
  }
  if (!trust)
    QTextStream(stderr) << error << '\n';
  check(bool(trust), "Packaged child cannot establish exact core trust");
  const auto parent = localInputProcess(getppid());
  check(parent && trust->verify(*parent), "Packaged child rejected its exact live core parent");
  inspectAdmission(*trust, *parent, LocalInputRole::Core, mode == "restricted-pass");
  auto wrong = *parent;
  ++wrong.startStamp;
  check(!trust->verify(wrong), "Packaged child accepted an incorrect parent birth identity");
  check(!trust->inspectRestrictedLibraryAdmission(wrong), "Admission accepted an incorrect parent birth identity");
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
  child.start(trust->executable(), {"--fixture-host", mode == "restricted-pass" ? mode : "restricted-deny"});
  const auto expected = localInputProcess(child.process.processId());
  check(expected && trust->verify(*expected), "Packaged core rejected its exact live child");
  const auto retainedAdmission = inspectAdmission(*trust, *expected, LocalInputRole::Host, mode == "restricted-pass");
  auto wrong = *expected;
  ++wrong.startStamp;
  check(!trust->verify(wrong), "Packaged core accepted a stale child birth identity");
  check(!trust->inspectRestrictedLibraryAdmission(wrong), "Admission accepted a stale child birth identity");
  check(!trust->verify(*currentLocalInputProcess()), "Core accepted its own PID as the host");
  if (mode == "tamper") {
    const auto resources = QFileInfo(QFileInfo(sibling).dir().path()).dir().filePath("Resources/proof.txt");
    write(resources, "altered-after-pin\n");
    check(!trust->verify(*expected), "Pinned trust ignored a changed sealed bundle resource");
    check(!trust->inspectRestrictedLibraryAdmission(*expected), "Admission ignored a changed sealed bundle resource");
    write(resources, "sealed-fixture\n");
    check(trust->verify(*expected), "Exact restored sealed bundle did not verify with fresh static objects");
  }
  if (mode == "wrong-path") {
    Child copied;
    copied.start(otherPeer, {"--fixture-hold"});
    const auto wrongPath = localInputProcess(copied.process.processId());
    check(wrongPath && !trust->verify(*wrongPath), "Same-user signed copied role at another path was accepted");
    check(!trust->inspectRestrictedLibraryAdmission(*wrongPath), "Admission accepted a copied role at another path");
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
  check(!trust->inspectRestrictedLibraryAdmission(*expected), "Exited peer received a new admission snapshot");
  if (retainedAdmission)
    check(retainedAdmission->peerProcess() == *expected, "Historical snapshot changed after peer exit");
  QTextStream(stdout) << "VERIFIED\n";
  return 0;
}
QString makeBundle(const QString &directory, const QString &name)
{
  const auto parent = QFileInfo(directory).canonicalFilePath();
  struct stat parentInfo{};
  check(
      !parent.isEmpty() && ::lstat(parent.toUtf8().constData(), &parentInfo) == 0 && S_ISDIR(parentInfo.st_mode) &&
          parentInfo.st_uid == geteuid() && (parentInfo.st_mode & 077) == 0 && !name.isEmpty() &&
          QFileInfo(name).fileName() == name && name != "." && name != "..",
      "Fixture bundle requires an existing private owned parent directory and a simple name"
  );
  const auto bundle = QDir(parent).filePath(name + ".app");
  check(
      !QFileInfo::exists(bundle) && !QFileInfo(bundle).isSymLink() && QDir().mkdir(bundle) &&
          ::chmod(bundle.toUtf8().constData(), 0700) == 0,
      "Fixture bundle must be a new private directory"
  );
  const auto contents = bundle + "/Contents";
  check(
      QDir().mkpath(contents + "/MacOS") && QDir().mkpath(contents + "/Resources"),
      "Cannot create signed fixture bundle"
  );
  const auto source = QCoreApplication::applicationFilePath();
  // The helper is a regular owned fixture executable for packaging closure;
  // no test invokes it or creates a display.
  for (const auto &role : {"deskflow-core", "deskflow-windowshare", "deskflow-holding-display-helper"}) {
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
  run("/usr/bin/codesign", {"--force", "--sign", "-", "--identifier", "org.deskflow.test.input-helper",
                            contents + "/MacOS/deskflow-holding-display-helper"});
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
    if (arguments.size() == 3 && arguments[1] == "--make-fixture-bundle") {
      const QFileInfo target(arguments[2]);
      check(
          target.isAbsolute() && target.fileName().endsWith(".app"), "New fixture path must be an absolute .app path"
      );
      QTextStream(stdout) << makeBundle(target.dir().path(), target.fileName().chopped(4)) << '\n';
      return 0;
    }
    if ((arguments.size() == 2 || arguments.size() == 3) && arguments[1] == "--fixture-host") {
      const auto mode = arguments.value(2, "restricted-deny");
      check(mode == "deny" || mode == "restricted-pass" || mode == "restricted-deny", "Invalid host fixture mode");
      return hostFixture(mode);
    }
    if ((arguments.size() == 3 || arguments.size() == 4) && arguments[1] == "--fixture-core") {
      check(
          QStringList{"deny", "pass", "tamper", "wrong-path", "reseal", "restricted-pass", "restricted-deny"}.contains(
              arguments[2]
          ),
          "Invalid core fixture mode"
      );
      return coreFixture(arguments[2], arguments.value(3));
    }
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
    check(
        run(core, {"--fixture-core", "restricted-deny"}) == "VERIFIED\n",
        "Ad-hoc signed peers acquired restricted library admission"
    );
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
                           "tampering, direct-launch denial and restricted-admission denial\n";
    return 0;
  } catch (const std::exception &failure) {
    QTextStream(stderr) << "FAIL: " << failure.what() << '\n';
    return 1;
  }
}
