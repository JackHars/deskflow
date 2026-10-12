/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeHost.h"
#include "NativeKeyboardAdmission.h"
#include <CoreGraphics/CoreGraphics.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <dlfcn.h>
#include <unistd.h>

// Internal filesystem primitive; exercising it never enumerates native windows.
namespace deskflow::windowshare::native {
bool persistJournal(const QString &path, const QJsonArray &records, QString *diagnostic);
bool targetedPointerAvailable();
CGEventRef targetedPointerEvent(
    CGEventSourceRef source, CGWindowID window, Rect frame, Point point, CGEventType type, std::uint32_t button,
    CGEventFlags modifiers, std::int64_t eventNumber, int clicks
);
} // namespace deskflow::windowshare::native

using deskflow::windowshare::FocusLease;
using deskflow::windowshare::ForwardingScope;
using deskflow::windowshare::InputLedger;
using deskflow::windowshare::KeyboardRouteBinding;
using deskflow::windowshare::NativeHost;
using deskflow::windowshare::NativeKeyboardAdmission;
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
void targetedPointerConstruction()
{
  using namespace deskflow::windowshare;
  using native::targetedPointerEvent;
  const deskflow::windowshare::Rect frame{-400, 180, 640, 448};
  using InputPoint = deskflow::windowshare::Point;
  const InputPoint point{100.25, 140.5};
  constexpr CGWindowID window = 987654; // Construction only: no real window or posting.
  const auto source = CGEventSourceCreate(kCGEventSourceStatePrivate);
  check(source != nullptr, "create an independent private event source");
  if (!source)
    return;
  const auto make = [&](CGEventType type, std::uint32_t button) {
    return targetedPointerEvent(source, window, frame, point, type, button, kCGEventFlagMaskShift, 42, 2);
  };
  const auto getLocal = reinterpret_cast<CGPoint (*)(CGEventRef)>(dlsym(RTLD_DEFAULT, "CGEventGetWindowLocation"));
  const bool supported = native::targetedPointerAvailable();
  const auto permissions = NativeHost::permissions();
  check(permissions["targetedPointer"].toBool() == supported, "probe reports the actual targeted-pointer capability");
  check(permissions["targetedPointerApi"].toString().contains("private"), "probe discloses the private API dependency");
  for (const auto type :
       {kCGEventMouseMoved, kCGEventLeftMouseDown, kCGEventLeftMouseUp, kCGEventLeftMouseDragged,
        kCGEventRightMouseDown, kCGEventRightMouseUp, kCGEventRightMouseDragged, kCGEventOtherMouseDown,
        kCGEventOtherMouseUp, kCGEventOtherMouseDragged, kCGEventScrollWheel}) {
    const auto button =
        type == kCGEventRightMouseDown || type == kCGEventRightMouseUp || type == kCGEventRightMouseDragged   ? 1u
        : type == kCGEventOtherMouseDown || type == kCGEventOtherMouseUp || type == kCGEventOtherMouseDragged ? 2u
                                                                                                              : 0u;
    const auto event = make(type, button);
    check(bool(event) == supported, "all pointer families fail closed without the private annotation API");
    if (!event)
      continue;
    check(getLocal != nullptr, "private annotation getter is available for independent readback");
    check(CGEventGetType(event) == type, "pointer construction preserves its event family");
    check(
        CGPointEqualToPoint(CGEventGetLocation(event), CGPointMake(frame.x + point.x, frame.y + point.y)),
        "targeted pointer keeps correct global coordinates with a negative screen origin"
    );
    check(
        getLocal && CGPointEqualToPoint(getLocal(event), CGPointMake(point.x, point.y)),
        "global-location assignment cannot erase or flip the independent window-local coordinates"
    );
    // CG's mouse-only fields are not defined for the scroll-wheel type. Its
    // AppKit window binding and local/global coordinates are checked above.
    if (type != kCGEventScrollWheel) {
      check(
          CGEventGetIntegerValueField(event, kCGMouseEventWindowUnderMousePointer) == window &&
              CGEventGetIntegerValueField(event, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent) == window,
          "both public window annotations identify the same selected window"
      );
      check(
          CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber) == button,
          "button identity survives construction"
      );
      check(
          CGEventGetIntegerValueField(event, kCGMouseEventNumber) == 42, "pointer event number survives construction"
      );
      check(CGEventGetIntegerValueField(event, kCGMouseEventClickState) == 2, "click count survives construction");
    }
    check(CGEventGetFlags(event) == kCGEventFlagMaskShift, "no artificial Command modifier is added");
    check(
        CGEventGetIntegerValueField(event, kCGEventSourceUserData) == syntheticEventTag &&
            CGEventGetIntegerValueField(event, kCGEventSourceStateID) == CGEventSourceGetSourceStateID(source),
        "constructed events retain the private source and synthetic loop marker"
    );
    CFRelease(event);
  }
  if (supported) {
    const auto fractional = targetedPointerEvent(source, window, frame, {100.1, 140.3}, kCGEventMouseMoved, 0, 0, 0, 0);
    check(fractional != nullptr, "fractional remote coordinates retain their independent annotation");
    if (fractional)
      CFRelease(fractional);
  }
  check(!make(kCGEventKeyDown, 0), "pointer constructor rejects keyboard events");
  check(!make(kCGEventMouseMoved, 32), "pointer constructor rejects an unsupported button");
  check(!targetedPointerEvent(nullptr, window, frame, point, kCGEventMouseMoved, 0, 0, 0, 0), "null sources denied");
  check(!targetedPointerEvent(source, 0, frame, point, kCGEventMouseMoved, 0, 0, 0, 0), "unbound window denied");
  for (const auto invalid :
       {InputPoint{-1, 1}, InputPoint{1, -1}, InputPoint{640, 1}, InputPoint{1, 448}, InputPoint{NAN, 1}})
    check(
        !targetedPointerEvent(source, window, frame, invalid, kCGEventMouseMoved, 0, 0, 0, 0),
        "outside or nonfinite local point denied"
    );
  for (const auto state : {kCGEventSourceStateCombinedSessionState, kCGEventSourceStateHIDSystemState}) {
    const auto shared = CGEventSourceCreate(state);
    check(shared != nullptr, "create shared-source rejection fixture");
    check(
        !targetedPointerEvent(shared, window, frame, point, kCGEventMouseMoved, 0, 0, 0, 0),
        "shared physical/session event sources denied"
    );
    if (shared)
      CFRelease(shared);
  }
  CFRelease(source);
}
KeyboardRouteBinding routeBinding()
{
  return {{11, 12}, 13, 14, 1, 16};
}
void keyboardAdmission()
{
  const auto binding = routeBinding();
  NativeKeyboardAdmission gate;
  InputLedger ledger;
  FocusLease focus;
  const auto revision = focus.request(binding.owner);
  check(focus.acknowledge(binding.owner, revision), "fixture focus lease acknowledges");
  check(revision == binding.focusRevision, "fixture route uses the acknowledged focus revision");
  check(!gate.blocksKeys() && !gate.blocksPointers(), "network keyboard and pointer begin admitted");
  check(
      !gate.quiesce(binding, std::nullopt, true), "viewer acknowledgment without a current native candidate is denied"
  );
  auto invalid = binding;
  invalid.generation = 0;
  check(!gate.quiesce(invalid, invalid, true), "zero route generation cannot fence input");
  invalid.generation = std::uint64_t{1} << 53;
  check(!gate.quiesce(invalid, invalid, true), "inexact JSON route generation cannot fence input");

  std::vector<KeyboardRouteBinding> changed;
  auto other = binding;
  ++other.owner.session;
  changed.push_back(other);
  other = binding;
  ++other.owner.epoch;
  changed.push_back(other);
  for (const auto member :
       {&KeyboardRouteBinding::generation, &KeyboardRouteBinding::focusRequest, &KeyboardRouteBinding::focusRevision,
        &KeyboardRouteBinding::geometry}) {
    other = binding;
    ++(other.*member);
    changed.push_back(other);
  }
  for (const auto &mismatch : changed)
    check(!gate.quiesce(binding, mismatch, true), "every candidate binding field must match exactly");
  check(!gate.blocksKeys(), "denied admission attempts do not alter network forwarding");

  check(ledger.down(true, 0x01000020, binding.owner), "fixture holds Qt Shift");
  check(!gate.quiesce(binding, binding, ledger.size() == 0), "held Qt key denies quiescence");
  check(ledger.ownerOf(true, 0x01000020) == binding.owner, "failed quiescence preserves the Qt key owner");
  check(ledger.up(true, 0x01000020, binding.owner) == binding.owner, "Qt up releases its existing owner");
  check(ledger.down(false, 0, binding.owner), "fixture holds left button");
  check(!gate.quiesce(binding, binding, ledger.size() == 0), "held mouse button denies quiescence");
  check(ledger.ownerOf(false, 0) == binding.owner, "failed handoff never synthesizes a pointer release");
  check(ledger.up(false, 0, binding.owner) == binding.owner, "physical pointer up retains its owner");

  check(gate.quiesce(binding, binding, ledger.size() == 0), "neutral exact candidate closes keyboard admission");
  check(gate.blocksKeys(), "network key down and repeat admission is blocked while fenced");
  check(!gate.blocksPointers(), "a legacy keyboard-only fence leaves pointer forwarding admitted");
  check(gate.quiesce(binding, binding, true), "exact repeated acknowledgment is idempotent while valid");
  check(gate.quiesced(binding, binding, true), "quiescence verifies the exact valid gate");
  check(focus.permits(binding.owner, binding.focusRevision), "quiescence preserves the existing focus lease");
  check(!ledger.up(true, 0x01000020, binding.owner), "late Qt up cannot invent a delivered key owner");
  check(gate.blocksKeys(), "late Qt up cannot reopen network admission");
  check(ledger.down(false, 0, binding.owner), "pointer input continues while keyboard is fenced");
  check(!gate.quiesced(binding, binding, ledger.size() == 0), "held pointer denies a later neutral admission check");
  check(
      gate.blocksKeys() && focus.permits(binding.owner, binding.focusRevision), "held pointer preserves gate and focus"
  );
  check(ledger.up(false, 0, binding.owner) == binding.owner, "pointer up is unaffected by the keyboard gate");
  check(gate.quiesced(binding, binding, ledger.size() == 0), "pointer release restores the neutral admission boundary");

  for (const auto &mismatch : changed) {
    check(!gate.quiesced(binding, mismatch, true), "changed current facts deny quiescence immediately");
    check(!gate.quiesce(mismatch, mismatch, true), "another binding cannot replace an outstanding gate");
    check(!gate.resume(mismatch), "mismatched resume cannot reopen admission");
    check(gate.blocksKeys(), "mismatched operations preserve blocking state");
  }
  check(!gate.quiesced(binding, std::nullopt, true), "hidden or returned source cannot remain admissible");
  check(gate.blocksKeys(), "loss of native candidate alone never reopens keyboard input");
  check(!gate.invalidate(deskflow::windowshare::Owner{99, 12}), "another owner's change leaves this gate intact");
  check(gate.quiesced(binding, binding, true), "unrelated owner invalidation is inert");
  const auto revoked = gate.invalidate(binding.owner);
  check(revoked == binding, "invalidation identifies the exact fenced owner");
  check(!gate.quiesced(binding, binding, true) && gate.blocksKeys(), "invalidation marks a blocking tombstone first");
  check(!gate.blocksPointers(), "invalidating a legacy keyboard fence cannot widen its pointer scope");
  check(!gate.quiesce(binding, binding, true), "fresh acknowledgment cannot revive an invalidated gate");
  check(!gate.invalidate(), "reentrant invalidation emits only one notification");
  auto newer = binding;
  ++newer.generation;
  check(!gate.quiesce(newer, newer, true), "reentrant replacement is denied before exact resume");
  check(gate.resume(*revoked), "exact owner resume clears its tombstone after authority revocation");
  check(!gate.blocksKeys(), "exact resume reopens network admission");
  check(!gate.quiesce(binding, binding, true), "retired generation cannot be fenced again");
  check(gate.quiesce(newer, newer, true), "a newer route can fence the same still-focused source");
  check(!gate.resume(binding), "late old resume cannot clear the newer route");
  check(gate.quiesced(newer, newer, true), "newer route survives old callback completion");
  check(gate.invalidate() == newer, "global lifecycle invalidation preserves the latest exact owner");
  check(!gate.quiesced(newer, std::nullopt, true) && gate.blocksKeys(), "returned entry leaves a blocking tombstone");
  check(gate.resume(newer), "returned entry is not required to release an exact tombstone");
  other = binding;
  ++other.owner.session;
  other.generation = 1;
  check(gate.quiesce(other, other, true), "a fresh window incarnation may start its own generation sequence");
  check(!gate.resume(newer) && gate.blocksKeys(), "prior incarnation cannot reopen a new owner's keyboard route");
  check(gate.resume(other), "new owner's exact resume succeeds");
}
void fullInputAdmission()
{
  auto binding = routeBinding();
  binding.scope = ForwardingScope::All;
  auto keyboard = binding;
  keyboard.scope = ForwardingScope::Keyboard;
  check(binding != keyboard, "a full-input binding differs from the otherwise identical keyboard binding");
  NativeKeyboardAdmission gate;
  InputLedger ledger;
  check(!gate.blocksKeys() && !gate.blocksPointers(), "full-input fixture starts with both paths admitted");
  auto invalid = binding;
  invalid.scope = static_cast<ForwardingScope>(123);
  check(!gate.quiesce(invalid, invalid, true), "unknown scope cannot fence native forwarding");
  check(!gate.quiesce(binding, std::nullopt, true), "full-input fence requires an observed native candidate");
  check(!gate.quiesce(binding, keyboard, true), "keyboard-only candidate cannot acknowledge full-input quiescence");
  check(!gate.quiesce(keyboard, binding, true), "full-input candidate cannot be silently downgraded to keyboard");
  check(!gate.blocksKeys() && !gate.blocksPointers(), "failed full-input admission does not change either route");

  for (const auto isKey : {true, false}) {
    const auto code = isKey ? 0x01000020u : 0u;
    check(ledger.down(isKey, code, binding.owner), "fixture retains physical key or pointer ownership");
    check(!gate.quiesce(binding, binding, ledger.size() == 0), "held input denies full-input cutover");
    check(
        ledger.ownerOf(isKey, code) == binding.owner && !gate.blocksKeys() && !gate.blocksPointers(),
        "denied full-input cutover preserves ownership and the still-needed release route"
    );
    check(ledger.up(isKey, code, binding.owner) == binding.owner, "the prior owner receives its matching release");
  }
  check(gate.quiesce(binding, binding, ledger.size() == 0), "neutral exact full-input candidate fences both paths");
  check(gate.blocksKeys() && gate.blocksPointers(), "full-input fence blocks keyboard and pointer forwarding");
  check(gate.quiesce(binding, binding, true), "repeated exact full-input acknowledgement is idempotent");
  check(gate.quiesced(binding, binding, true), "full-input quiescence retains all current facts");
  check(!gate.quiesced(binding, binding, false), "full-input quiescence must remain neutral at every query");
  check(!gate.quiesced(binding, std::nullopt, true), "missing native candidate cannot retain admissible quiescence");
  check(gate.blocksKeys() && gate.blocksPointers(), "loss of candidate or neutrality never reopens either path");

  std::vector<KeyboardRouteBinding> changed{keyboard, invalid};
  auto other = binding;
  ++other.owner.session;
  changed.push_back(other);
  other = binding;
  ++other.owner.epoch;
  changed.push_back(other);
  for (const auto member :
       {&KeyboardRouteBinding::generation, &KeyboardRouteBinding::focusRequest, &KeyboardRouteBinding::focusRevision,
        &KeyboardRouteBinding::geometry}) {
    other = binding;
    ++(other.*member);
    changed.push_back(other);
  }
  for (const auto &mismatch : changed) {
    check(!gate.quiesced(binding, mismatch, true), "every current binding field participates in full-input admission");
    check(!gate.quiesced(mismatch, binding, true), "every queried binding field participates in full-input admission");
    check(!gate.quiesce(mismatch, mismatch, true), "a live full-input gate cannot be replaced or downgraded");
    check(!gate.resume(mismatch), "wrong-scope or stale completion cannot reopen a live full-input gate");
    check(gate.blocksKeys() && gate.blocksPointers(), "mismatched operations preserve both forwarding fences");
  }
  check(!gate.invalidate(deskflow::windowshare::Owner{99, 12}), "an unrelated source cannot invalidate this gate");
  check(gate.quiesced(binding, binding, true), "unrelated invalidation preserves exact full-input readiness");
  check(gate.invalidate(binding.owner) == binding, "invalidation returns the complete original scope and binding");
  check(
      !gate.quiesced(binding, binding, true) && gate.blocksKeys() && gate.blocksPointers(),
      "invalidated full-input tombstone keeps both forwarding paths fenced"
  );
  check(!gate.invalidate(), "full-input lifecycle invalidation is delivered only once");
  check(!gate.quiesce(binding, binding, true), "a repeated full-input acknowledgement cannot revive invalidation");
  for (const auto &mismatch : changed) {
    check(!gate.quiesce(mismatch, mismatch, true), "invalidated full-input fence cannot be replaced or downgraded");
    check(!gate.resume(mismatch), "tombstone cannot be resumed by a different scope or binding");
    check(gate.blocksKeys() && gate.blocksPointers(), "invalidated route retains both fences until exact resume");
  }
  check(gate.resume(binding), "exact full-input resume clears the tombstone after authority revocation");
  check(!gate.blocksKeys() && !gate.blocksPointers(), "exact resume reopens keyboard and pointer together");
  check(!gate.resume(binding), "duplicate full-input resume cannot affect another route");
  check(!gate.quiesce(binding, binding, true), "retired full-input generation cannot be revived");
  check(!gate.quiesce(keyboard, keyboard, true), "changing scope cannot replay a retired generation");
  auto older = binding;
  --older.generation;
  check(!gate.quiesce(older, older, true), "older full-input generations remain retired");
  older.scope = ForwardingScope::Keyboard;
  check(!gate.quiesce(older, older, true), "scope downgrade cannot bypass generation retirement");

  auto newerKeyboard = keyboard;
  ++newerKeyboard.generation;
  check(gate.quiesce(newerKeyboard, newerKeyboard, true), "newer negotiated keyboard-only route remains supported");
  check(gate.blocksKeys() && !gate.blocksPointers(), "new keyboard-only generation does not inherit old pointer fence");
  auto newerAll = newerKeyboard;
  newerAll.scope = ForwardingScope::All;
  check(!gate.quiesce(newerAll, newerAll, true), "a live keyboard gate cannot be silently widened");
  check(!gate.resume(newerAll), "full-input completion cannot resume a keyboard-only gate");
  check(gate.invalidate() == newerKeyboard, "keyboard-only invalidation preserves its scope after full-input use");
  check(gate.blocksKeys() && !gate.blocksPointers(), "keyboard-only tombstone does not acquire pointer ownership");
  check(gate.resume(newerKeyboard), "exact newer keyboard resume succeeds");
  check(!gate.quiesce(newerAll, newerAll, true), "scope upgrade cannot reuse a retired keyboard generation");
  ++newerAll.generation;
  check(gate.quiesce(newerAll, newerAll, true), "next full-input generation can establish a fresh fence");
  check(!gate.resume(binding) && !gate.resume(newerKeyboard), "old completions cannot clear newer full-input fences");
  check(gate.blocksKeys() && gate.blocksPointers(), "new full-input route survives both kinds of stale completion");
  check(gate.resume(newerAll), "exact newest full-input resume succeeds");

  other = binding;
  ++other.owner.epoch;
  other.generation = 1;
  check(gate.quiesce(other, other, true), "a new source epoch may begin its own full-input generation sequence");
  check(!gate.resume(newerAll), "prior source epoch cannot reopen new full-input ownership");
  check(gate.invalidate() == other && gate.blocksPointers(), "global invalidation preserves the new epoch scope");
  check(gate.resume(other), "new source epoch resumes only its exact tombstone");
  auto newWindow = other;
  ++newWindow.owner.session;
  check(gate.quiesce(newWindow, newWindow, true), "another source window can begin its own full-input sequence");
  check(!gate.resume(other) && gate.blocksKeys() && gate.blocksPointers(), "prior window cannot reopen new ownership");
  check(gate.resume(newWindow), "new window resumes its own full-input fence");
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  targetedPointerConstruction();
  keyboardAdmission();
  fullInputAdmission();
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
    const auto binding = routeBinding();
    int invalidations = 0;
    host.onKeyboardRouteInvalidated = [&](KeyboardRouteBinding, deskflow::windowshare::KeyboardRouteReason) {
      ++invalidations;
    };
    check(
        !host.keyboardRouteCandidate(binding.owner.session, binding.owner.epoch, binding.generation),
        "nonexistent native entry cannot produce a route candidate"
    );
    check(
        !host.quiesceKeyboard(binding) && !host.keyboardRouteQuiesced(binding),
        "an acknowledgment cannot admit a route without an exported native entry"
    );
    check(!host.resumeKeyboard(binding), "a route without a gate cannot be resumed");
    auto all = binding;
    all.scope = ForwardingScope::All;
    check(
        !host.keyboardRouteCandidate(all.owner.session, all.owner.epoch, all.generation, all.scope),
        "nonexistent native entry cannot produce a full-input route candidate"
    );
    check(
        !host.quiesceKeyboard(all) && !host.keyboardRouteQuiesced(all) && !host.resumeKeyboard(all),
        "full-input acknowledgement cannot create native ownership without an exported entry"
    );
    host.releaseAll();
    check(invalidations == 0, "release without a gate never fabricates an invalidation");
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
  QTextStream(stdout) << (failures ? "FAIL" : "PASS")
                      << ": native recovery journal and forwarding admission regressions\n";
  return failures ? 1 : 0;
}
