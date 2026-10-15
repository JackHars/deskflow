/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputCodeAdmission.h"
#include "LocalInputPeerTrust.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>

using namespace deskflow::windowshare;
#if defined(DESKFLOW_LOCAL_INPUT_PEER_TRUST_TESTING)
namespace deskflow::windowshare {
bool testLocalInputExactImage(const QByteArray &image, QString *error) noexcept;
}
#endif
static_assert(!std::is_default_constructible_v<LocalInputRestrictedLibraryAdmissionSnapshot>);
static_assert(!std::is_convertible_v<LocalInputRestrictedLibraryAdmissionSnapshot, bool>);
static_assert(!std::is_default_constructible_v<LocalInputExactPermittedLibrariesSnapshot>);
static_assert(!std::is_convertible_v<LocalInputExactPermittedLibrariesSnapshot, bool>);
namespace {
QByteArray run(const QString &program, const QStringList &arguments, int expectedExit);
void check(bool value, const char *reason)
{
  if (!value)
    throw std::runtime_error(reason);
}
#if defined(DESKFLOW_LOCAL_INPUT_PEER_TRUST_TESTING)
void put32(QByteArray &bytes, qsizetype offset, quint32 value, bool big = true)
{
  check(offset >= 0 && offset + 4 <= bytes.size(), "Invalid reader test field offset");
  auto *destination = reinterpret_cast<uchar *>(bytes.data() + offset);
  if (big)
    qToBigEndian(value, destination);
  else
    qToLittleEndian(value, destination);
}
void put64(QByteArray &bytes, qsizetype offset, quint64 value)
{
  check(offset >= 0 && offset + 8 <= bytes.size(), "Invalid reader test wide field offset");
  qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}
QByteArray readerDirectory(unsigned hashType = 2)
{
  QByteArray result(44, '\0');
  put32(result, 0, 0xfade0c02);
  put32(result, 4, result.size());
  result[36] = hashType == 2 ? 32 : 20;
  result[37] = hashType;
  return result;
}
QByteArray readerThin(quint32 cpu = 0x01000007, bool alternate = false)
{
  // Minimal container fixtures exercise structure only, never signature trust.
  // The production caller separately requires Security.framework validation.
  const auto directory = readerDirectory();
  const auto second = alternate ? readerDirectory(3) : QByteArray::fromHex("fade818100000008");
  QByteArray result(48 + 28, '\0');
  put32(result, 0, 0xfeedfacf, false);
  put32(result, 4, cpu, false);
  put32(result, 8, 3, false);
  put32(result, 12, 2, false);
  put32(result, 16, 1, false);
  put32(result, 20, 16, false);
  put32(result, 32, 0x1d, false);
  put32(result, 36, 16, false);
  put32(result, 40, 48, false);
  put32(result, 44, 28 + directory.size() + second.size(), false);
  put32(result, 48, 0xfade0cc0);
  put32(result, 52, 28 + directory.size() + second.size());
  put32(result, 56, 2);
  put32(result, 60, 0);
  put32(result, 64, 28);
  put32(result, 68, alternate ? 0x1000 : 11);
  put32(result, 72, 28 + directory.size());
  result += directory;
  result += second;
  return result;
}
QByteArray readerFat(const QByteArray &first, const QByteArray &second, bool wide = false)
{
  const qsizetype entry = wide ? 32 : 20, end = 8 + 2 * entry;
  QByteArray result(end, '\0');
  put32(result, 0, wide ? 0xcafebabf : 0xcafebabe);
  put32(result, 4, 2);
  qsizetype index = 0;
  for (const auto *slice : {&first, &second}) {
    const auto at = 8 + index * entry;
    put32(result, at, qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(slice->constData() + 4)));
    put32(result, at + 4, qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(slice->constData() + 8)));
    const auto offset = index ? end + first.size() : end;
    if (wide) {
      put64(result, at + 8, offset);
      put64(result, at + 16, slice->size());
    } else {
      put32(result, at + 8, offset);
      put32(result, at + 12, slice->size());
    }
    ++index;
  }
  result += first;
  result += second;
  return result;
}
void exactImageReaderTests()
{
  const auto accept = [](const QByteArray &image) {
    QString error;
    const bool accepted = testLocalInputExactImage(image, &error);
    if (!accepted)
      QTextStream(stderr) << error << '\n';
    check(accepted && error.isEmpty(), "Reader rejected a valid container fixture");
  };
  const auto reject = [](const QByteArray &image, const char *message) {
    QString error;
    check(!testLocalInputExactImage(image, &error), "Reader accepted a malformed container fixture");
    if (!error.contains(QString::fromLatin1(message))) {
      QTextStream(stderr) << "Unexpected reader rejection: " << error << '\n';
      check(false, "Malformed container did not exercise its intended validation");
    }
  };
  QTemporaryDir signedImage;
  check(signedImage.isValid(), "Cannot create isolated signed container fixture");
  const auto signedPath = signedImage.filePath("reader-executable");
  check(QFile::copy(QCoreApplication::applicationFilePath(), signedPath), "Cannot copy actual test executable");
  run("/usr/bin/codesign", {"--force", "--sign", "-", signedPath}, 0);
  QFile executable(signedPath);
  check(executable.open(QIODevice::ReadOnly), "Cannot read actual signed test executable");
  accept(executable.readAll());
  const auto thin = readerThin(), other = readerThin(0x0100000c);
  accept(thin);
  accept(readerThin(0x01000007, true));
  for (qsizetype size = 0; size < thin.size(); ++size) {
    QString error;
    check(!testLocalInputExactImage(thin.left(size), &error), "Truncated container was accepted");
  }
  auto image = thin;
  put32(image, 16, 4097, false);
  reject(image, "Mach-O commands");
  image = thin;
  put32(image, 36, 7, false);
  reject(image, "command size");
  image = thin;
  put32(image, 40, 47, false);
  reject(image, "signature range");
  image = thin;
  put32(image, 44, 0xffffffff, false);
  reject(image, "signature bounds");
  image = thin;
  image.insert(48, thin.mid(32, 16));
  put32(image, 16, 2, false);
  put32(image, 20, 32, false);
  put32(image, 40, 64, false);
  put32(image, 56, 64, false);
  reject(image, "Duplicate or malformed exact-library signature command");
  image = thin;
  put32(image, 56, 65);
  reject(image, "signature superblob");
  image = thin;
  put32(image, 68, 0);
  reject(image, "signature slot");
  image = thin;
  put32(image, 72, 28);
  reject(image, "Overlapping exact-library signature blobs");
  image = thin;
  put32(image, 72, 0xffffffff);
  reject(image, "signature slot");
  image = thin;
  put32(image, 80, 0xffffffff);
  reject(image, "signature blob range");
  image = thin;
  image[76 + 37] = 1;
  reject(image, "SHA-256 CodeDirectory");
  image = thin;
  image[76 + 36] = 20;
  reject(image, "SHA-256 CodeDirectory");
  image = thin;
  put32(image, 60, 0x2000);
  reject(image, "CodeDirectory occupies an invalid");
  image = readerThin(0x01000007, true);
  image.replace(120, 44, image.mid(76, 44));
  reject(image, "Duplicate exact-library CodeDirectory identity");
  image = thin;
  put32(image, 120, 0xfade0000);
  reject(image, "constraint blob");
  const auto fat = readerFat(thin, other);
  accept(fat);
  accept(readerFat(thin, other, true));
  reject(readerFat(thin, thin), "duplicate exact-library architecture");
  image = fat;
  put32(image, 8, 0x0100000c);
  reject(image, "Inconsistent or duplicate exact-library architecture");
  image = fat;
  put32(image, 20, thin.size() + 1);
  reject(image, "Overlapping exact-library architecture images");
  image = fat;
  put32(image, 16, 47);
  reject(image, "architecture range");
  image = fat;
  put32(image, 40, 0xffffffff);
  reject(image, "signature bounds");
  image = fat;
  put32(image, 4, 17);
  reject(image, "architecture count");
  image = readerFat(thin, other, true);
  put64(image, 48, std::numeric_limits<quint64>::max());
  reject(image, "signature bounds");
  image = fat;
  put32(image, 48, 0xcafebabe);
  reject(image, "Nested universal exact-library image");
}
#endif
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
std::optional<LocalInputExactPermittedLibrariesSnapshot> inspectExact(
    const LocalInputPeerTrust &trust, LocalInputProcess peer, LocalInputRole role, bool expectedAdmission,
    const QString &expectedFailure = {}
)
{
  QString error = QStringLiteral("inspection did not replace the diagnostic");
  const auto started = std::chrono::steady_clock::now();
  auto snapshot = trust.inspectExactPermittedLibraries(peer, &error);
  if (expectedAdmission && !snapshot)
    QTextStream(stderr) << "Exact-library fixture: " << error << '\n';
  check(bool(snapshot) == expectedAdmission, "Unexpected exact permitted-library inspection result");
  if (snapshot) {
    const auto &restricted = snapshot->restrictedAdmission();
    const auto self = currentLocalInputProcess();
    check(error.isEmpty(), "Successful exact-library snapshot retained a failure diagnostic");
    check(
        self && restricted.selfProcess() == *self && restricted.peerProcess() == peer &&
            restricted.peerRole() == role &&
            restricted.anchorProcess() == (role == LocalInputRole::Host ? *self : peer),
        "Exact-library snapshot does not bind both roles and their live anchor"
    );
    const auto &hashes = snapshot->permittedCodeHashes();
    check(
        snapshot->manifestHash().size() == 32 && !hashes.empty() && hashes.size() <= 1024 &&
            std::is_sorted(hashes.begin(), hashes.end()) &&
            std::adjacent_find(hashes.begin(), hashes.end()) == hashes.end() &&
            std::all_of(hashes.begin(), hashes.end(), [](const auto &hash) { return hash.size() == 20; }) &&
            snapshot->manifestRecordCount() >= hashes.size() && snapshot->manifestRecordCount() <= 1024 &&
            snapshot->librarySliceCount() > 0 && snapshot->librarySliceCount() <= snapshot->manifestRecordCount(),
        "Exact-library snapshot has invalid identities or record/slice counts"
    );
    check(
        started <= snapshot->inspectionStartedAt() &&
            snapshot->inspectionStartedAt() <= restricted.inspectionStartedAt() &&
            restricted.inspectionFinishedAt() <= snapshot->inspectionFinishedAt() &&
            snapshot->inspectionFinishedAt() <= std::chrono::steady_clock::now(),
        "Exact-library snapshot has invalid monotonic observation bounds"
    );
  } else {
    check(!error.isEmpty(), "Rejected exact-library inspection has no diagnostic");
    if (!expectedFailure.isEmpty() && !error.contains(expectedFailure)) {
      QTextStream(stderr) << "Exact-library rejection: " << error << '\n';
      check(false, "Exact-library fixture failed for an unintended reason");
    }
  }
  check(trust.verify(peer), "Optional exact-library inspection changed ordinary peer trust");
  return snapshot;
}
bool expectsRestricted(const QString &mode)
{
  return mode == "restricted-pass" || mode.startsWith("exact-");
}
bool expectsExact(const QString &mode)
{
  return mode == "exact-pass" || mode == "exact-tamper" || mode == "exact-reseal";
}
int hostFixture(const QString &mode, const QString &expectedFailure)
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
  inspectAdmission(*trust, *parent, LocalInputRole::Core, expectsRestricted(mode));
  inspectExact(*trust, *parent, LocalInputRole::Core, expectsExact(mode), expectedFailure);
  auto wrong = *parent;
  ++wrong.startStamp;
  check(!trust->verify(wrong), "Packaged child accepted an incorrect parent birth identity");
  check(!trust->inspectRestrictedLibraryAdmission(wrong), "Admission accepted an incorrect parent birth identity");
  check(!trust->inspectExactPermittedLibraries(wrong), "Exact-library inspection accepted a stale parent identity");
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
  QStringList childArguments{"--fixture-host", expectsRestricted(mode) ? mode : "restricted-deny"};
  if (mode == "exact-deny" && !otherPeer.isEmpty())
    childArguments.push_back(otherPeer);
  child.start(trust->executable(), childArguments);
  const auto expected = localInputProcess(child.process.processId());
  check(expected && trust->verify(*expected), "Packaged core rejected its exact live child");
  const auto retainedAdmission = inspectAdmission(*trust, *expected, LocalInputRole::Host, expectsRestricted(mode));
  const auto retainedExact = inspectExact(
      *trust, *expected, LocalInputRole::Host, expectsExact(mode), mode == "exact-deny" ? otherPeer : QString{}
  );
  auto wrong = *expected;
  ++wrong.startStamp;
  check(!trust->verify(wrong), "Packaged core accepted a stale child birth identity");
  check(!trust->inspectRestrictedLibraryAdmission(wrong), "Admission accepted a stale child birth identity");
  check(!trust->inspectExactPermittedLibraries(wrong), "Exact-library inspection accepted a stale child identity");
  check(!trust->verify(*currentLocalInputProcess()), "Core accepted its own PID as the host");
  if (mode == "tamper") {
    const auto resources = QFileInfo(QFileInfo(sibling).dir().path()).dir().filePath("Resources/proof.txt");
    write(resources, "altered-after-pin\n");
    check(!trust->verify(*expected), "Pinned trust ignored a changed sealed bundle resource");
    check(!trust->inspectRestrictedLibraryAdmission(*expected), "Admission ignored a changed sealed bundle resource");
    check(!trust->inspectExactPermittedLibraries(*expected), "Exact-library inspection ignored resource tampering");
    write(resources, "sealed-fixture\n");
    check(trust->verify(*expected), "Exact restored sealed bundle did not verify with fresh static objects");
  }
  if (mode == "exact-tamper") {
    const auto manifest =
        QFileInfo(QFileInfo(sibling).dir().path()).dir().filePath("Resources/deskflow-exact-libraries.json");
    QFile original(manifest);
    check(original.open(QIODevice::ReadOnly), "Cannot read exact-library fixture manifest");
    const auto bytes = original.readAll();
    original.close();
    write(manifest, "{}");
    check(!trust->inspectExactPermittedLibraries(*expected), "Exact-library inspection accepted altered sealed policy");
    write(manifest, bytes);
    check(bool(trust->inspectExactPermittedLibraries(*expected)), "Restored exact-library policy did not verify");
  }
  if (mode == "wrong-path") {
    Child copied;
    copied.start(otherPeer, {"--fixture-hold"});
    const auto wrongPath = localInputProcess(copied.process.processId());
    check(wrongPath && !trust->verify(*wrongPath), "Same-user signed copied role at another path was accepted");
    check(!trust->inspectRestrictedLibraryAdmission(*wrongPath), "Admission accepted a copied role at another path");
    check(!trust->inspectExactPermittedLibraries(*wrongPath), "Exact-library inspection accepted a copied role");
    copied.finish();
  }
  if (mode == "reseal" || mode == "exact-reseal") {
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
    const bool acceptedExactReplacement = bool(trust->inspectExactPermittedLibraries(*expected));
    const bool acceptedNewAnchor = bool(LocalInputPeerTrust::forPackagedPeer(LocalInputRole::Host));
    check(
        QDir().rename(bundle, clone) && QDir().rename(retired, bundle), "Cannot restore original running fixture bundle"
    );
    check(!acceptedReplacement, "Resealed disk bundle replaced the already-running bundle anchor");
    check(!acceptedExactReplacement, "Resealed disk policy replaced the live exact-library anchor");
    check(!acceptedNewAnchor, "Factory accepted a newly resealed disk bundle as the live process anchor");
    check(trust->verify(*expected), "Restored exact live bundle anchor did not verify");
  }
  child.finish();
  check(!trust->verify(*expected), "Exited packaged peer retained process trust");
  check(!trust->inspectRestrictedLibraryAdmission(*expected), "Exited peer received a new admission snapshot");
  check(!trust->inspectExactPermittedLibraries(*expected), "Exited peer received a new exact-library snapshot");
  if (retainedAdmission)
    check(retainedAdmission->peerProcess() == *expected, "Historical snapshot changed after peer exit");
  if (retainedExact)
    check(
        retainedExact->restrictedAdmission().peerProcess() == *expected, "Historical exact snapshot changed after exit"
    );
  QTextStream(stdout) << "VERIFIED\n";
  return 0;
}
bool admissionUntil(const std::function<bool()> &condition, int timeoutMs = 20000)
{
  if (condition())
    return true;
  QEventLoop loop;
  QTimer poll, timeout;
  poll.setInterval(10);
  timeout.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (condition())
      loop.quit();
  });
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start();
  timeout.start(timeoutMs);
  loop.exec();
  return condition();
}
int admissionHostFixture(QCoreApplication &application, int descriptor, bool expectedAdmission, int budgetMs)
{
  QString error;
  const auto parent = localInputProcess(getppid());
  const auto trust = LocalInputPeerTrust::forPackagedPeer(LocalInputRole::Core, &error);
  check(trust && parent && trust->verify(*parent), qPrintable(error));
  auto channel = LocalInputChannel::adoptHost(descriptor, *parent);
  std::unique_ptr<LocalInputCodeAdmission> guard;
  std::optional<LocalInputCodeAdmissionProof> retained;
  bool announced = false, closed = false, revokedAtClose = false;
  channel->onAuthenticated = [&] { guard = LocalInputCodeAdmission::request(*channel, &error, budgetMs); };
  channel->onClosed = [&](QString) {
    closed = true;
    revokedAtClose = !retained || !retained->live();
    application.quit();
  };
  QTimer poll;
  poll.setInterval(10);
  QObject::connect(&poll, &QTimer::timeout, &application, [&] {
    if (announced || !guard || guard->state() == LocalInputCodeAdmission::State::Pending)
      return;
    retained = guard->proof();
    announced = true;
    if (bool(retained) != expectedAdmission) {
      QTextStream(stderr) << "Host admission failed: " << guard->error() << '\n';
      application.exit(1);
      return;
    }
    QTextStream output(stdout);
    output << (expectedAdmission ? "ADMISSION_READY\n" : "ADMISSION_DENIED\n");
    output.flush();
  });
  poll.start();
  QTimer::singleShot(budgetMs + 10000, &application, [&] { application.exit(1); });
  const auto result = application.exec();
  check(result == 0 && announced && closed && revokedAtClose, "Real host admission/close lifecycle failed");
  return 0;
}
int admissionCoreFixture(bool expectedAdmission, int budgetMs)
{
  QString error;
  const auto trust = LocalInputPeerTrust::forPackagedPeer(LocalInputRole::Host, &error);
  check(bool(trust), qPrintable(error));
  auto pair = LocalInputSocketPair::create();
  Child child;
  pair.configureChild(child.process);
  child.process.start(
      trust->executable(), {"--admission-host", QString::number(pair.childDescriptor()),
                            expectedAdmission ? "allow" : "deny", QString::number(budgetMs)}
  );
  check(child.process.waitForStarted(5000), "Cannot launch real admission fixture host");
  const auto peer = localInputProcess(child.process.processId());
  check(peer && trust->verify(*peer), "Real admission fixture peer identity failed");
  auto channel = pair.adoptCore(*peer);
  check(admissionUntil([&] { return channel->authenticated(); }), "Real admission private handshake failed");
  QElapsedTimer elapsed;
  elapsed.start();
  auto guard = LocalInputCodeAdmission::request(*channel, &error, budgetMs);
  check(bool(guard), qPrintable(error));
  QByteArray hostResult;
  check(
      admissionUntil(
          [&] {
            hostResult += child.process.readAllStandardOutput();
            return (guard->state() != LocalInputCodeAdmission::State::Pending && hostResult.contains('\n')) ||
                   child.process.state() == QProcess::NotRunning;
          },
          budgetMs + 5000
      ),
      "Real asynchronous code admission timed out"
  );
  auto proof = guard->proof();
  if (budgetMs != LocalInputCodeAdmission::defaultTimeoutMs)
    QTextStream(stderr) << "Admission fixture: elapsed=" << elapsed.elapsed() << "ms budget=" << budgetMs << "ms\n";
  if (bool(proof) != expectedAdmission || child.process.state() == QProcess::NotRunning)
    QTextStream(stderr) << "Core admission: " << guard->error() << '\n' << child.process.readAllStandardError();
  check(bool(proof) == expectedAdmission, "Real core code-admission result differs from policy");
  check(
      hostResult == (expectedAdmission ? "ADMISSION_READY\n" : "ADMISSION_DENIED\n"),
      "Real host code-admission result differs from policy"
  );
  if (proof) {
    check(proof->live(), "Real core proof was already revoked");
    const auto &restricted = proof->inspection().restrictedAdmission();
    check(
        restricted.anchorProcess() == proof->connection().self && restricted.peerProcess() == *peer,
        "Real asynchronous proof lost its core-main anchor or peer"
    );
  }
  channel->close();
  check(!proof || !proof->live(), "Real channel close left retained authority");
  check(
      admissionUntil([&] { return child.process.state() == QProcess::NotRunning; }, 5000),
      "Real admission child did not observe channel closure"
  );
  if (child.process.exitCode() != 0)
    QTextStream(stderr) << child.process.readAllStandardError();
  check(
      child.process.exitStatus() == QProcess::NormalExit && child.process.exitCode() == 0, "Real admission host failed"
  );
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
    const auto admissionBudget = [&](qsizetype index) {
      if (index == arguments.size())
        return LocalInputCodeAdmission::defaultTimeoutMs;
      bool ok = false;
      const auto budget = arguments[index].toInt(&ok);
      check(ok && budget >= 100 && budget <= 30000, "Invalid fixture admission budget");
      return budget;
    };
    if ((arguments.size() == 3 || arguments.size() == 4) && arguments[1] == "--admission-core") {
      check(arguments[2] == "allow" || arguments[2] == "deny", "Invalid code-admission expectation");
      return admissionCoreFixture(arguments[2] == "allow", admissionBudget(3));
    }
    if ((arguments.size() == 4 || arguments.size() == 5) && arguments[1] == "--admission-host") {
      bool ok = false;
      const auto descriptor = arguments[2].toInt(&ok);
      check(ok && descriptor >= 3 && protectLocalInputDescriptor(descriptor), "Invalid fixture private descriptor");
      check(arguments[3] == "allow" || arguments[3] == "deny", "Invalid host code-admission expectation");
      return admissionHostFixture(application, descriptor, arguments[3] == "allow", admissionBudget(4));
    }
#if defined(DESKFLOW_LOCAL_INPUT_PEER_TRUST_TESTING)
    if (arguments.size() == 2 && arguments[1] == "--reader-only") {
      exactImageReaderTests();
      QTextStream(stdout) << "PASS: bounded exact-library Mach-O and signature container reader\n";
      return 0;
    }
#endif
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
    if (arguments.size() >= 2 && arguments.size() <= 4 && arguments[1] == "--fixture-host") {
      const auto mode = arguments.value(2, "restricted-deny");
      check(
          QStringList{
              "deny", "restricted-pass", "restricted-deny", "exact-pass", "exact-deny", "exact-tamper", "exact-reseal"
          }
              .contains(mode),
          "Invalid host fixture mode"
      );
      check(arguments.size() < 4 || mode == "exact-deny", "Unexpected host fixture diagnostic argument");
      return hostFixture(mode, arguments.value(3));
    }
    if ((arguments.size() == 3 || arguments.size() == 4) && arguments[1] == "--fixture-core") {
      check(
          QStringList{
              "deny", "pass", "tamper", "wrong-path", "reseal", "restricted-pass", "restricted-deny", "exact-pass",
              "exact-deny", "exact-tamper", "exact-reseal"
          }
              .contains(arguments[2]),
          "Invalid core fixture mode"
      );
      return coreFixture(arguments[2], arguments.value(3));
    }
    check(arguments.size() == 1, "Unexpected peer trust fixture arguments");
#if defined(DESKFLOW_LOCAL_INPUT_PEER_TRUST_TESTING)
    exactImageReaderTests();
#endif
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
        run(core, {"--admission-core", "deny"}) == "VERIFIED\n",
        "Ad-hoc fixture acquired retained code-admission authority"
    );
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
