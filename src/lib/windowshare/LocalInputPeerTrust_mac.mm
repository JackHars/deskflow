/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputPeerTrust.h"

#include <CoreFoundation/CoreFoundation.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QtEndian>
#include <Security/Security.h>
#include <cerrno>
#include <fcntl.h>
#include <libproc.h>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace deskflow::windowshare {
namespace {
// Code signing formats are documented by Apple TN3126 and the public Mach-O
// format. Parse only enough to authenticate the captured resource/Info bytes
// against the exact CodeDirectory of the running bundle-main anchor. The
// Security framework performs executable, requirement, and nested-code checks.
constexpr qsizetype maximumExecutable = 256 * 1024 * 1024;
constexpr qsizetype maximumResources = 8 * 1024 * 1024;
constexpr SecCSFlags strictFlags =
    kSecCSStrictValidate | kSecCSCheckAllArchitectures | kSecCSCheckNestedCode | kSecCSRestrictSymlinks;

void require(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
template <typename T> struct CF
{
  T value = nullptr;
  CF() = default;
  explicit CF(T pointer) : value(pointer)
  {
  }
  CF(const CF &) = delete;
  CF &operator=(const CF &) = delete;
  CF(CF &&other) noexcept : value(std::exchange(other.value, nullptr))
  {
  }
  CF &operator=(CF &&other) noexcept
  {
    if (value)
      CFRelease(value);
    value = std::exchange(other.value, nullptr);
    return *this;
  }
  ~CF()
  {
    if (value)
      CFRelease(value);
  }
};
CF<CFStringRef> cfString(const QString &value)
{
  const auto bytes = value.toUtf8();
  return CF<CFStringRef>(CFStringCreateWithBytes(
      nullptr, reinterpret_cast<const UInt8 *>(bytes.constData()), bytes.size(), kCFStringEncodingUTF8, false
  ));
}
QString string(CFTypeRef value)
{
  require(value && CFGetTypeID(value) == CFStringGetTypeID(), "Invalid signed string metadata");
  auto text = static_cast<CFStringRef>(value);
  const auto length = CFStringGetLength(text);
  require(length > 0 && length <= 8192, "Oversized signed string metadata");
  QByteArray bytes(length * 4 + 1, '\0');
  require(CFStringGetCString(text, bytes.data(), bytes.size(), kCFStringEncodingUTF8), "Invalid signed UTF-8 metadata");
  return QString::fromUtf8(bytes.constData());
}
QByteArray data(CFTypeRef value)
{
  require(value && CFGetTypeID(value) == CFDataGetTypeID(), "Missing exact signed code hash");
  auto bytes = static_cast<CFDataRef>(value);
  require(CFDataGetLength(bytes) == 20, "Unsupported signed code hash");
  return QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(bytes)), CFDataGetLength(bytes));
}
QString canonical(const QString &path)
{
  const auto clean = QDir::cleanPath(path);
  const auto resolved = QFileInfo(clean).canonicalFilePath();
  require(
      !resolved.isEmpty() && resolved == clean && QDir::isAbsolutePath(clean), "Symlink or missing packaged code path"
  );
  return resolved;
}
QString fileUrl(CFTypeRef value)
{
  require(value && CFGetTypeID(value) == CFURLGetTypeID(), "Missing signed executable path");
  CF<CFStringRef> path(CFURLCopyFileSystemPath(static_cast<CFURLRef>(value), kCFURLPOSIXPathStyle));
  return canonical(string(path.value));
}
struct File
{
  int descriptor = -1;
  ~File()
  {
    if (descriptor >= 0)
      ::close(descriptor);
  }
};
QByteArray readFile(const QString &path, qsizetype limit)
{
  canonical(path);
  const auto bytes = path.toUtf8();
  // Reject substituted special files without blocking before fstat can reject
  // them. Security.framework validation is synchronous and has its own costs.
  File file{::open(bytes.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
  struct stat before{}, after{}, named{};
  require(
      file.descriptor >= 0 && fstat(file.descriptor, &before) == 0 && S_ISREG(before.st_mode) &&
          !(before.st_mode & (S_IWGRP | S_IWOTH)) && before.st_size > 0 && before.st_size <= limit,
      "Cannot safely read packaged signature data"
  );
  QByteArray result(before.st_size, '\0');
  qsizetype offset = 0;
  while (offset < result.size()) {
    const auto count = ::pread(file.descriptor, result.data() + offset, result.size() - offset, offset);
    if (count < 0 && errno == EINTR)
      continue;
    require(count > 0, "Packaged signature data changed during read");
    offset += count;
  }
  require(
      fstat(file.descriptor, &after) == 0 && lstat(bytes.constData(), &named) == 0 && before.st_dev == after.st_dev &&
          before.st_ino == after.st_ino && before.st_size == after.st_size &&
          before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec &&
          before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec &&
          before.st_ctimespec.tv_sec == after.st_ctimespec.tv_sec &&
          before.st_ctimespec.tv_nsec == after.st_ctimespec.tv_nsec && named.st_dev == after.st_dev &&
          named.st_ino == after.st_ino && S_ISREG(named.st_mode),
      "Packaged signature data changed during read"
  );
  return result;
}
CF<CFDictionaryRef> plist(const QByteArray &bytes)
{
  CF<CFDataRef> input(CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(bytes.constData()), bytes.size()));
  CF<CFPropertyListRef> value(
      CFPropertyListCreateWithData(nullptr, input.value, kCFPropertyListImmutable, nullptr, nullptr)
  );
  require(value.value && CFGetTypeID(value.value) == CFDictionaryGetTypeID(), "Invalid sealed bundle property list");
  return CF<CFDictionaryRef>(static_cast<CFDictionaryRef>(CFRetain(value.value)));
}
quint64 number(const QByteArray &bytes, quint64 offset, unsigned width = 4, bool big = true)
{
  require(
      (width == 4 || width == 8) && offset <= quint64(bytes.size()) && width <= quint64(bytes.size()) - offset,
      "Truncated packaged Mach-O signature"
  );
  const auto *at = reinterpret_cast<const uchar *>(bytes.constData() + offset);
  if (width == 4)
    return big ? qFromBigEndian<quint32>(at) : qFromLittleEndian<quint32>(at);
  return big ? qFromBigEndian<quint64>(at) : qFromLittleEndian<quint64>(at);
}
QByteArray part(const QByteArray &bytes, quint64 offset, quint64 length)
{
  require(
      offset <= quint64(bytes.size()) && length <= quint64(bytes.size()) - offset, "Invalid Mach-O signature bounds"
  );
  return bytes.mid(offset, length);
}
QByteArray codeDirectory(const QByteArray &image, const QByteArray &wanted, bool allowFat = true)
{
  const auto magic = number(image, 0);
  if (magic == 0xcafebabe || magic == 0xbebafeca || magic == 0xcafebabf || magic == 0xbfbafeca) {
    require(allowFat, "Nested universal Mach-O image");
    const bool big = magic == 0xcafebabe || magic == 0xcafebabf;
    const bool wide = magic == 0xcafebabf || magic == 0xbfbafeca;
    const auto count = number(image, 4, 4, big);
    require(count > 0 && count <= 16, "Unsupported universal Mach-O architecture count");
    for (quint64 index = 0; index < count; ++index) {
      const auto at = 8 + index * (wide ? 32 : 20);
      const auto offset = number(image, at + 8, wide ? 8 : 4, big);
      const auto length = number(image, at + (wide ? 16 : 12), wide ? 8 : 4, big);
      const auto result = codeDirectory(part(image, offset, length), wanted, false);
      if (!result.isEmpty())
        return result;
    }
    return {};
  }
  const bool big = magic == 0xfeedface || magic == 0xfeedfacf;
  const bool wide = magic == 0xfeedfacf || magic == 0xcffaedfe;
  require(big || magic == 0xcefaedfe || magic == 0xcffaedfe, "Unsupported packaged executable format");
  const auto count = number(image, 16, 4, big), commands = number(image, 20, 4, big);
  quint64 at = wide ? 32 : 28;
  require(
      count <= 4096 && at <= quint64(image.size()) && commands <= quint64(image.size()) - at,
      "Invalid packaged Mach-O commands"
  );
  const auto end = at + commands;
  for (quint64 index = 0; index < count; ++index) {
    require(at + 8 <= end, "Truncated packaged Mach-O load command");
    const auto kind = number(image, at, 4, big), size = number(image, at + 4, 4, big);
    require(size >= 8 && size <= end - at, "Invalid packaged Mach-O load command");
    if (kind == 0x1d) { // LC_CODE_SIGNATURE
      require(size >= 16, "Truncated packaged code signature command");
      const auto signature = part(image, number(image, at + 8, 4, big), number(image, at + 12, 4, big));
      require(number(signature, 0) == 0xfade0cc0, "Missing embedded packaged code signature");
      const auto length = number(signature, 4), blobs = number(signature, 8);
      require(
          length <= quint64(signature.size()) && blobs <= 64 && 12 + blobs * 8 <= length,
          "Invalid embedded packaged code signature"
      );
      for (quint64 blob = 0; blob < blobs; ++blob) {
        const auto offset = number(signature, 16 + blob * 8);
        require(offset + 8 <= length, "Invalid packaged signature blob offset");
        const auto blobLength = number(signature, offset + 4);
        require(blobLength >= 8 && blobLength <= length - offset, "Invalid packaged signature blob size");
        if (number(signature, offset) != 0xfade0c02)
          continue;
        const auto directory = part(signature, offset, blobLength);
        require(directory.size() >= 44, "Truncated packaged CodeDirectory");
        const auto hashType = static_cast<unsigned char>(directory[37]);
        if (hashType != 2 && hashType != 3)
          continue; // Only SHA-256 and Apple's truncated SHA-256 are supported.
        if (QCryptographicHash::hash(directory, QCryptographicHash::Sha256).left(20) == wanted)
          return directory;
      }
    }
    at += size;
  }
  return {};
}
void sealedSlot(const QByteArray &directory, unsigned slot, const QByteArray &bytes)
{
  require(directory.size() >= 44, "Running anchor has no supported CodeDirectory");
  const auto offset = number(directory, 16), special = number(directory, 24);
  const auto size = static_cast<unsigned char>(directory[36]), type = static_cast<unsigned char>(directory[37]);
  require(
      special >= slot && ((type == 2 && size == 32) || (type == 3 && size == 20)) && offset >= slot * size,
      "Running anchor does not seal required bundle metadata"
  );
  require(
      part(directory, offset - slot * size, size) ==
          QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).left(size),
      "Bundle metadata differs from the running anchor's resource seal"
  );
}
struct Process
{
  LocalInputProcess identity;
  std::int64_t parent = 0;
  QString executable;
  bool operator==(const Process &) const = default;
};
Process process(std::int64_t pid)
{
  require(pid > 0 && pid <= std::numeric_limits<pid_t>::max(), "Invalid packaged peer process");
  proc_bsdinfo info{};
  char path[PROC_PIDPATHINFO_MAXSIZE]{};
  require(
      proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) == sizeof(info) && info.pbi_uid == geteuid() &&
          info.pbi_start_tvsec > 0 && proc_pidpath(pid, path, sizeof(path)) > 0,
      "Packaged peer process is unavailable or belongs to another user"
  );
  Process result{
      {pid, info.pbi_start_tvsec * 1000000 + info.pbi_start_tvusec}, info.pbi_ppid, canonical(QString::fromUtf8(path))
  };
  require(localInputProcess(pid) == result.identity, "Packaged peer process identity changed");
  return result;
}
struct Pin
{
  QString path, executable;
  QByteArray hash;
  CF<SecRequirementRef> exact, designated;
};
CF<SecStaticCodeRef> staticCode(const QString &path)
{
  const auto text = cfString(canonical(path));
  CF<CFURLRef> url(CFURLCreateWithFileSystemPath(nullptr, text.value, kCFURLPOSIXPathStyle, QFileInfo(path).isDir()));
  CF<SecStaticCodeRef> code;
  require(
      SecStaticCodeCreateWithPath(url.value, kSecCSDefaultFlags, &code.value) == errSecSuccess,
      "Cannot inspect packaged code signature"
  );
  return code;
}
CF<CFDictionaryRef> signingInformation(SecStaticCodeRef code)
{
  CF<CFDictionaryRef> info;
  require(
      SecCodeCopySigningInformation(code, kSecCSSigningInformation | kSecCSRequirementInformation, &info.value) ==
          errSecSuccess,
      "Cannot inspect packaged code identity"
  );
  return info;
}
Pin pin(const QString &path)
{
  const auto code = staticCode(path);
  require(
      SecStaticCodeCheckValidity(code.value, strictFlags, nullptr) == errSecSuccess,
      "Packaged code or sealed nested resources failed strict signature validation"
  );
  const auto info = signingInformation(code.value);
  Pin result;
  result.path = canonical(path);
  result.executable = fileUrl(CFDictionaryGetValue(info.value, kSecCodeInfoMainExecutable));
  result.hash = data(CFDictionaryGetValue(info.value, kSecCodeInfoUnique));
  require(
      SecCodeCopyDesignatedRequirement(code.value, kSecCSDefaultFlags, &result.designated.value) == errSecSuccess,
      "Packaged code has no designated requirement"
  );
  const auto expression = cfString(QStringLiteral("cdhash H\"%1\"").arg(QString::fromLatin1(result.hash.toHex())));
  require(
      SecRequirementCreateWithString(expression.value, kSecCSDefaultFlags, &result.exact.value) == errSecSuccess,
      "Cannot pin the exact packaged code requirement"
  );
  return result;
}
void checkStatic(const Pin &pin)
{
  const auto code = staticCode(pin.path); // Fresh object: never reuse a validation cache after file changes.
  require(
      SecStaticCodeCheckValidity(code.value, strictFlags, pin.exact.value) == errSecSuccess &&
          SecStaticCodeCheckValidity(code.value, kSecCSDefaultFlags, pin.designated.value) == errSecSuccess,
      "Packaged code changed after its exact identity was pinned"
  );
  const auto info = signingInformation(code.value);
  require(
      data(CFDictionaryGetValue(info.value, kSecCodeInfoUnique)) == pin.hash &&
          fileUrl(CFDictionaryGetValue(info.value, kSecCodeInfoMainExecutable)) == pin.executable,
      "Packaged code identity or executable path changed"
  );
}
void checkRunning(const Process &expected, const Pin &pin)
{
  require(
      process(expected.identity.pid) == expected && expected.executable == pin.executable,
      "Running packaged code has the wrong birth identity or executable path"
  );
  CF<SecCodeRef> code;
  if (expected.identity.pid == getpid()) {
    require(SecCodeCopySelf(kSecCSDefaultFlags, &code.value) == errSecSuccess, "Cannot inspect running self identity");
  } else {
    const auto pid = expected.identity.pid;
    CF<CFNumberRef> value(CFNumberCreate(nullptr, kCFNumberSInt64Type, &pid));
    const void *keys[] = {kSecGuestAttributePid};
    const void *values[] = {value.value};
    CF<CFDictionaryRef> attributes(
        CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks)
    );
    require(
        SecCodeCopyGuestWithAttributes(nullptr, attributes.value, kSecCSDefaultFlags, &code.value) == errSecSuccess,
        "Cannot inspect running packaged peer identity"
    );
  }
  require(
      SecCodeCheckValidity(code.value, kSecCSDefaultFlags, pin.exact.value) == errSecSuccess &&
          SecCodeCheckValidity(code.value, kSecCSDefaultFlags, pin.designated.value) == errSecSuccess,
      "Running code differs from the exact sealed packaged identity"
  );
  const auto info = signingInformation(code.value);
  require(
      data(CFDictionaryGetValue(info.value, kSecCodeInfoUnique)) == pin.hash &&
          fileUrl(CFDictionaryGetValue(info.value, kSecCodeInfoMainExecutable)) == pin.executable &&
          SecCodeCheckValidity(code.value, kSecCSDefaultFlags, pin.exact.value) == errSecSuccess &&
          process(expected.identity.pid) == expected,
      "Running packaged code changed during identity verification"
  );
}
QByteArray nestedHash(CFDictionaryRef resources, const QString &name)
{
  const auto files = CFDictionaryGetValue(resources, CFSTR("files2"));
  require(files && CFGetTypeID(files) == CFDictionaryGetTypeID(), "Bundle lacks a modern nested-code resource seal");
  const auto key = cfString("MacOS/" + name);
  const auto entry = CFDictionaryGetValue(static_cast<CFDictionaryRef>(files), key.value);
  require(entry && CFGetTypeID(entry) == CFDictionaryGetTypeID(), "Fixed peer is not sealed as nested bundle code");
  return data(CFDictionaryGetValue(static_cast<CFDictionaryRef>(entry), CFSTR("cdhash")));
}
} // namespace

struct LocalInputPeerTrust::Impl
{
  LocalInputRole role;
  Pin root, self, peer;
  std::vector<Process> ancestry;
  QString infoPath, resourcesPath;
  QByteArray infoBytes, resourceBytes;

  void verifyPackage() const
  {
    for (const auto &item : ancestry)
      require(process(item.identity.pid) == item, "Live packaged bundle ancestry changed");
    checkRunning(ancestry.back(), root);
    checkRunning(ancestry.front(), self);
    checkStatic(root);
    checkStatic(self);
    checkStatic(peer);
    require(
        readFile(infoPath, maximumResources) == infoBytes && readFile(resourcesPath, maximumResources) == resourceBytes,
        "Pinned packaged resource bytes changed"
    );
    checkRunning(ancestry.back(), root);
    for (const auto &item : ancestry)
      require(process(item.identity.pid) == item, "Live packaged bundle ancestry changed during verification");
  }
};

LocalInputPeerTrust::LocalInputPeerTrust(std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
{
}
LocalInputPeerTrust::~LocalInputPeerTrust() = default;

std::unique_ptr<LocalInputPeerTrust> LocalInputPeerTrust::forPackagedPeer(LocalInputRole expectedPeer, QString *error)
{
  if (error)
    error->clear();
  try {
    require(expectedPeer == LocalInputRole::Core || expectedPeer == LocalInputRole::Host, "Invalid packaged peer role");
    auto result = std::make_unique<Impl>();
    result->role = expectedPeer;
    const auto self = process(getpid());
    const QString selfName = expectedPeer == LocalInputRole::Host ? "deskflow-core" : "deskflow-windowshare";
    const QString peerName = expectedPeer == LocalInputRole::Host ? "deskflow-windowshare" : "deskflow-core";
    require(QFileInfo(self.executable).fileName() == selfName, "Current executable is not the fixed packaged role");
    const auto macos = QFileInfo(self.executable).dir();
    const auto contents = QFileInfo(macos.path()).dir();
    const auto bundle = QFileInfo(contents.path()).dir().path();
    require(
        macos.dirName() == "MacOS" && contents.dirName() == "Contents" && bundle.endsWith(".app"),
        "Private original input requires a signed app bundle"
    );
    result->root = pin(bundle);
    result->infoPath = contents.filePath("Info.plist");
    result->resourcesPath = contents.filePath("_CodeSignature/CodeResources");
    result->infoBytes = readFile(result->infoPath, maximumResources);
    result->resourceBytes = readFile(result->resourcesPath, maximumResources);
    const auto info = plist(result->infoBytes);
    const auto mainName = string(CFDictionaryGetValue(info.value, CFSTR("CFBundleExecutable")));
    require(
        QFileInfo(mainName).fileName() == mainName && mainName != "." && mainName != ".." &&
            result->root.executable == canonical(macos.filePath(mainName)),
        "Bundle main executable does not match its sealed identity"
    );
    result->ancestry.push_back(self);
    while (result->ancestry.back().executable != result->root.executable) {
      require(
          result->ancestry.size() < 16 && result->ancestry.back().parent > 1,
          "No live signed bundle-main ancestor; launch the packaged core app executable"
      );
      const auto parent = process(result->ancestry.back().parent);
      require(
          parent.executable.startsWith(macos.path() + "/"),
          "Direct nested-tool launch has no trusted bundle-main anchor"
      );
      result->ancestry.push_back(parent);
    }
    checkRunning(result->ancestry.back(), result->root);
    const auto directory = codeDirectory(readFile(result->root.executable, maximumExecutable), result->root.hash);
    sealedSlot(directory, 1, result->infoBytes);     // CSSLOT_INFOSLOT
    sealedSlot(directory, 3, result->resourceBytes); // CSSLOT_RESOURCEDIR
    const auto resources = plist(result->resourceBytes);
    const auto rolePin = [&](const QString &name) {
      const auto path = canonical(macos.filePath(name));
      auto value = pin(path == result->root.executable ? bundle : path);
      require(value.executable == path, "Fixed role resolves to a different executable");
      if (path != result->root.executable)
        require(
            value.hash == nestedHash(resources.value, name),
            "Fixed role differs from the bundle's exact nested-code seal"
        );
      return value;
    };
    result->self = rolePin(selfName);
    result->peer = rolePin(peerName);
    if (expectedPeer == LocalInputRole::Core)
      require(
          self.parent > 1 && process(self.parent).executable == result->peer.executable,
          "Window host was not launched directly by its fixed packaged core"
      );
    result->verifyPackage();
    return std::unique_ptr<LocalInputPeerTrust>(new LocalInputPeerTrust(std::move(result)));
  } catch (const std::exception &failure) {
    if (error)
      *error = QString::fromUtf8(failure.what());
    return nullptr;
  }
}
QString LocalInputPeerTrust::executable() const
{
  return m_impl->peer.executable;
}
bool LocalInputPeerTrust::verify(LocalInputProcess expected) const
{
  try {
    const auto before = process(expected.pid);
    require(before.identity == expected, "Packaged peer birth identity mismatch");
    if (m_impl->role == LocalInputRole::Host)
      require(before.parent == getpid(), "Packaged host is not this core's direct child");
    else
      require(
          m_impl->ancestry.front().parent == expected.pid && getppid() == expected.pid,
          "Packaged core is not this host's live parent"
      );
    m_impl->verifyPackage();
    checkRunning(before, m_impl->peer);
    m_impl->verifyPackage();
    require(process(expected.pid) == before, "Packaged peer changed during verification");
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace deskflow::windowshare
