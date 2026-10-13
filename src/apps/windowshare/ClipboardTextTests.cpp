/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ClipboardText.h"
#include "KeyboardRouteProtocol.h"
#include "Transport.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTextStream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace deskflow::windowshare;
namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
template <typename Callback> void rejected(Callback callback, const char *message)
{
  bool threw = false;
  try {
    callback();
  } catch (const std::exception &) {
    threw = true;
  }
  check(threw, message);
}
ClipboardFocusBinding binding()
{
  return {{11, 12}, 13, 14, 15};
}
Packet packet(const ClipboardTextMessage &message)
{
  return {
      MessageType::ClipboardText, message.focus.owner.session, message.focus.owner.epoch, 1,
      jsonBytes(encodeClipboardText(message))
  };
}
struct Fixture
{
  ClipboardFocusBinding focus = binding();
  std::uint64_t incarnation = 1;
  bool focused = true;
  std::optional<QString> text = QStringLiteral("local text");
  unsigned focusReads = 0, reads = 0, writes = 0;
  std::vector<ClipboardTextMessage> sent;
  std::function<void()> onFocus, onRead, onWrite, onSend, onCurrent;
  std::unique_ptr<FocusedClipboard> clipboard;
  explicit Fixture(bool policy = true)
  {
    FocusedClipboard::Callbacks callbacks;
    callbacks.focus = [this]() -> std::optional<ClipboardFocusObservation> {
      ++focusReads;
      const auto callback = std::exchange(onFocus, {});
      if (callback)
        callback();
      if (!focused)
        return {};
      const auto expected = focus;
      const auto version = incarnation;
      return ClipboardFocusObservation{expected, [this, expected, version] {
                                         const auto callback = std::exchange(onCurrent, {});
                                         if (callback)
                                           callback();
                                         return focused && focus == expected && incarnation == version;
                                       }};
    };
    callbacks.readText = [this] {
      ++reads;
      const auto result = text;
      const auto callback = std::exchange(onRead, {});
      if (callback)
        callback();
      return result;
    };
    callbacks.writeText = [this](const QString &value) {
      ++writes;
      text = value;
      const auto callback = std::exchange(onWrite, {});
      if (callback)
        callback();
      return true;
    };
    callbacks.send = [this](const ClipboardTextMessage &message) {
      sent.push_back(message);
      const auto callback = std::exchange(onSend, {});
      if (callback)
        callback();
      return true;
    };
    clipboard = std::make_unique<FocusedClipboard>(policy, std::move(callbacks));
  }
  ClipboardTextMessage incoming(std::uint64_t sequence = 1, QString value = QStringLiteral("peer text")) const
  {
    return {focus, sequence, std::move(value)};
  }
  void negotiate()
  {
    clipboard->setNegotiated(true);
  }
};

struct MetadataFixture
{
  ClipboardFocusBinding focus = binding();
  std::uint64_t incarnation = 1;
  bool focused = true;
  std::optional<std::int64_t> revision = 10;
  unsigned focusReads = 0, metadataReads = 0, changes = 0;
  std::function<void()> onFocus, onCurrent, onMetadata, onChanged;
  std::unique_ptr<ClipboardChangeMonitor> monitor;
  MetadataFixture()
  {
    ClipboardChangeMonitor::Callbacks callbacks;
    callbacks.focus = [this]() -> std::optional<ClipboardFocusObservation> {
      ++focusReads;
      const auto callback = std::exchange(onFocus, {});
      if (callback)
        callback();
      if (!focused)
        return {};
      const auto expected = focus;
      const auto version = incarnation;
      return ClipboardFocusObservation{expected, [this, expected, version] {
                                         const auto callback = std::exchange(onCurrent, {});
                                         if (callback)
                                           callback();
                                         return focused && focus == expected && incarnation == version;
                                       }};
    };
    callbacks.readRevision = [this] {
      ++metadataReads;
      const auto result = revision;
      const auto callback = std::exchange(onMetadata, {});
      if (callback)
        callback();
      return result;
    };
    callbacks.changed = [this](const ClipboardFocusObservation &observation) {
      check(observation.binding == focus && observation.live(), "metadata notification retains its exact focus");
      ++changes;
      const auto callback = std::exchange(onChanged, {});
      if (callback)
        callback();
    };
    monitor = std::make_unique<ClipboardChangeMonitor>(std::move(callbacks));
  }
  void begin()
  {
    monitor->setEnabled(true);
    check(!monitor->poll() && !changes, "first metadata observation only establishes a baseline");
  }
};

void protocol()
{
  for (const auto &text :
       {QString{}, QStringLiteral("hello\n\t\"world\" — café 😀"), QString(maximumClipboardTextBytes, QChar('a'))}) {
    const ClipboardTextMessage message{binding(), maximumClipboardSequence, text};
    const auto original = packet(message);
    Decoder decoder;
    const auto wire = encode(original);
    std::vector<Packet> decoded;
    decoder.feed(wire, [&](Packet item) { decoded.push_back(std::move(item)); });
    check(
        decoded.size() == 1 && parseClipboardText(decoded.front()) == message,
        "focused text roundtrips through the actual transport envelope and strict codec"
    );
  }
  const ClipboardTextMessage message{binding(), 1, QStringLiteral("hello")};
  const auto good = encodeClipboardText(message);
  for (const auto *name : {"focusRequest", "focusRevision", "geometry", "sequence"}) {
    for (const auto invalid :
         {QJsonValue{}, QJsonValue(0), QJsonValue(-1), QJsonValue(1.5), QJsonValue(QStringLiteral("1")),
          QJsonValue(9007199254740992.0)}) {
      auto object = good;
      object.insert(QLatin1String(name), invalid);
      rejected(
          [&] { parseClipboardText({MessageType::ClipboardText, 11, 12, 1, jsonBytes(object)}); },
          "missing, fractional, textual, zero and oversized clipboard revisions are denied"
      );
    }
  }
  auto extra = good;
  extra.insert("html", "<b>hidden</b>");
  rejected(
      [&] { parseClipboardText({MessageType::ClipboardText, 11, 12, 1, jsonBytes(extra)}); },
      "HTML and any unnegotiated fields cannot cross the text schema"
  );
  auto wrong = packet(message);
  wrong.type = MessageType::Input;
  rejected([&] { parseClipboardText(wrong); }, "clipboard cannot arrive as input authority");
  wrong = packet(message);
  wrong.session = 0;
  rejected([&] { parseClipboardText(wrong); }, "clipboard must name an exact window session");
  wrong = packet(message);
  wrong.payload.resize(maxControlPayload + 1);
  rejected([&] { parseClipboardText(wrong); }, "oversized text packets are denied before JSON parse");
  check(!validClipboardText(QString(maximumClipboardTextBytes + 1, QChar('a'))), "text bound is enforced");
  check(!validClipboardText(QString(maximumClipboardTextBytes, QChar(0x00e9))), "UTF-8 byte bound is enforced");
  check(
      !validClipboardText(QString(QChar(0xd800))) && !validClipboardText(QString(QChar('\0'))),
      "lossy UTF-16 and embedded null text are denied"
  );
  const auto escaped = encodeClipboardText({binding(), 1, QString(maximumClipboardTextBytes, QChar('\n'))});
  check(
      jsonBytes(escaped).size() < maxControlPayload, "worst-case escaped allowed text fits one bounded control packet"
  );

  check(!peerSupportsClipboardText({}), "legacy peers have no implicit clipboard permission");
  check(
      !peerSupportsClipboardText({{"capabilities", clipboardTextCapabilities(keyboardRouteCapabilities(), false)}}),
      "default capabilities do not opt into clipboard"
  );
  const auto capabilities = clipboardTextCapabilities(keyboardRouteCapabilities(), true);
  check(peerSupportsClipboardText({{"capabilities", capabilities}}), "explicit clipboard capability negotiates");
  auto repeated = capabilities;
  repeated.append(QLatin1String(clipboardTextCapability));
  rejected([&] { peerSupportsClipboardText({{"capabilities", repeated}}); }, "duplicate clipboard capabilities deny");
}
void policyAndFocus()
{
  Fixture disabled(false);
  disabled.negotiate();
  check(
      !disabled.clipboard->localChanged() && !disabled.clipboard->receive(disabled.incoming()) &&
          !disabled.clipboard->enabled() && !disabled.focusReads && !disabled.reads && !disabled.writes,
      "one-sided negotiation cannot access even clipboard focus, contents or writes"
  );
  Fixture fixture;
  check(
      !fixture.clipboard->localChanged() && !fixture.clipboard->receive(fixture.incoming()) && !fixture.reads,
      "local opt-in alone is insufficient"
  );
  fixture.negotiate();
  check(
      fixture.clipboard->enabled() && !fixture.reads && !fixture.writes && fixture.sent.empty(),
      "negotiation does not publish preexisting clipboard contents"
  );
  fixture.focused = false;
  check(!fixture.clipboard->localChanged() && !fixture.reads, "no focused export means no clipboard read");
  fixture.focused = true;
  check(
      fixture.clipboard->localChanged() && fixture.sent.size() == 1 && fixture.sent[0].focus == fixture.focus &&
          fixture.sent[0].sequence == 1 && fixture.sent[0].text == fixture.text,
      "fresh local text sends once with exact focus and sequence"
  );
  check(!fixture.clipboard->localChanged() && fixture.sent.size() == 1, "duplicate local notifications do not resend");
  fixture.text = QStringLiteral("new local text");
  check(
      fixture.clipboard->localChanged() && fixture.sent.back().sequence == 2,
      "a new text change retains monotonic sequence"
  );
  fixture.text.reset();
  check(!fixture.clipboard->localChanged() && fixture.sent.size() == 2, "non-text clipboard produces no packet");
  fixture.clipboard->setNegotiated(false);
  const auto reads = fixture.reads;
  check(
      !fixture.clipboard->localChanged() && fixture.reads == reads, "disconnect immediately prevents clipboard reads"
  );
}
void exactReceiveAndEcho()
{
  for (int field = 0; field < 5; ++field) {
    Fixture fixture;
    fixture.negotiate();
    auto stale = fixture.incoming();
    if (field == 0)
      ++stale.focus.owner.session;
    if (field == 1)
      ++stale.focus.owner.epoch;
    if (field == 2)
      ++stale.focus.focusRequest;
    if (field == 3)
      ++stale.focus.focusRevision;
    if (field == 4)
      ++stale.focus.geometry;
    check(!fixture.clipboard->receive(stale) && !fixture.writes, "every exact binding field participates in admission");
    fixture.focus = stale.focus;
    check(
        !fixture.clipboard->receive(stale) && !fixture.writes, "stale packet cannot revive after later focus matches"
    );
    stale.sequence = 2;
    check(fixture.clipboard->receive(stale) && fixture.writes == 1, "fresh sequence can use the new exact focus");
  }
  Fixture fixture;
  fixture.negotiate();
  fixture.onWrite = [&] {
    check(!fixture.clipboard->localChanged(), "synchronous clipboard signals cannot echo an in-flight receive");
    check(
        !fixture.clipboard->receive(fixture.incoming(2)), "nested remote delivery is consumed without recursive writes"
    );
  };
  const auto message = fixture.incoming();
  check(
      fixture.clipboard->receive(message) && fixture.writes == 1 && fixture.sent.empty(), "exact receive applies once"
  );
  check(!fixture.clipboard->localChanged() && fixture.sent.empty(), "delayed remote echo is suppressed");
  ++fixture.focus.focusRevision;
  ++fixture.incarnation;
  check(!fixture.clipboard->localChanged() && fixture.sent.empty(), "focus replacement cannot relabel a delayed echo");
  fixture.clipboard->setNegotiated(false);
  fixture.negotiate();
  check(!fixture.clipboard->localChanged() && fixture.sent.empty(), "connection replacement cannot relabel an echo");
  fixture.text = QStringLiteral("fresh user text");
  check(
      fixture.clipboard->localChanged() && fixture.sent.size() == 1,
      "fresh user text after remote application is still shared"
  );
  auto latest = fixture.incoming(maximumClipboardSequence, QStringLiteral("last sequence"));
  check(
      fixture.clipboard->receive(latest) && !fixture.clipboard->receive(latest) &&
          !fixture.clipboard->receive(fixture.incoming()),
      "duplicate, old and exhausted receive sequence never writes again"
  );
}
void reentryAndLifetime()
{
  for (int change = 0; change < 3; ++change) {
    Fixture fixture;
    fixture.negotiate();
    fixture.onRead = [&] {
      if (change == 0)
        ++fixture.incarnation;
      if (change == 1)
        ++fixture.focus.focusRevision;
      if (change == 2)
        fixture.clipboard->setNegotiated(false);
    };
    check(
        !fixture.clipboard->localChanged() && fixture.sent.empty(),
        "lifetime/focus/connection invalidation during read denies publication"
    );
  }
  Fixture replaced;
  replaced.negotiate();
  replaced.onWrite = [&] {
    ++replaced.incarnation;
    ++replaced.focus.focusRevision;
  };
  check(
      !replaced.clipboard->receive(replaced.incoming()) && replaced.writes == 1,
      "a write that loses exact focus during callback reports no current completion"
  );
  check(!replaced.clipboard->localChanged() && replaced.sent.empty(), "uncertain completion still suppresses its echo");

  for (int boundary = 0; boundary < 5; ++boundary) {
    Fixture fixture;
    fixture.negotiate();
    auto destroy = [&] { fixture.clipboard.reset(); };
    if (boundary == 0)
      fixture.onFocus = destroy;
    if (boundary == 1)
      fixture.onCurrent = destroy;
    if (boundary == 2)
      fixture.onRead = destroy;
    if (boundary == 3)
      fixture.onWrite = destroy;
    if (boundary == 4)
      fixture.onSend = destroy;
    auto *controller = fixture.clipboard.get();
    const bool result = boundary == 3 ? controller->receive(fixture.incoming()) : controller->localChanged();
    check(!result && !fixture.clipboard, "destruction at every borrowed callback boundary is safe and returns failure");
  }
  Fixture fixture;
  fixture.negotiate();
  bool read = true, received = true, enabled = true;
  std::thread worker([&] {
    enabled = fixture.clipboard->enabled();
    read = fixture.clipboard->localChanged();
    received = fixture.clipboard->receive(fixture.incoming());
    fixture.clipboard->setNegotiated(false);
  });
  worker.join();
  check(
      !enabled && !read && !received && !fixture.reads && !fixture.writes && fixture.clipboard->enabled(),
      "worker-thread calls cannot observe or mutate GUI-owned clipboard state"
  );
}
void metadataFocusContinuity()
{
  MetadataFixture fixture;
  check(!fixture.monitor->poll() && !fixture.focusReads && !fixture.metadataReads, "disabled sampler reads nothing");
  fixture.monitor->setEnabled(true);
  check(!fixture.focusReads && !fixture.metadataReads, "enabling the sampler does not inspect a clipboard");
  fixture.begin();
  check(!fixture.monitor->poll(), "unchanged metadata does not notify");
  ++*fixture.revision;
  check(fixture.monitor->poll() && fixture.changes == 1, "a later change under retained live focus notifies once");
  fixture.focused = false;
  ++*fixture.revision;
  check(!fixture.monitor->poll(), "unfocused changes only rebaseline metadata");
  fixture.focused = true;
  ++*fixture.revision;
  check(!fixture.monitor->poll(), "a change first noticed on focus acquisition cannot expose earlier text");
  ++*fixture.revision;
  check(fixture.monitor->poll() && fixture.changes == 2, "a subsequent focused change is eligible");

  for (int field = 0; field < 6; ++field) {
    MetadataFixture replacement;
    replacement.begin();
    if (field == 0)
      ++replacement.focus.owner.session;
    if (field == 1)
      ++replacement.focus.owner.epoch;
    if (field == 2)
      ++replacement.focus.focusRequest;
    if (field == 3)
      ++replacement.focus.focusRevision;
    if (field == 4)
      ++replacement.focus.geometry;
    if (field == 5)
      ++replacement.incarnation;
    ++*replacement.revision;
    check(
        !replacement.monitor->poll() && !replacement.changes,
        "every focus/lifetime replacement establishes a new baseline"
    );
    ++*replacement.revision;
    check(replacement.monitor->poll() && replacement.changes == 1, "fresh changes remain usable after rebaseline");
  }
  fixture.monitor->resetBaseline();
  ++*fixture.revision;
  check(
      !fixture.monitor->poll() && fixture.changes == 2, "explicit focus transitions rebaseline even an equal binding"
  );
  fixture.revision.reset();
  check(!fixture.monitor->poll(), "inaccessible metadata invalidates continuity");
  fixture.revision = 20;
  check(!fixture.monitor->poll(), "metadata recovery cannot publish old data");
  ++*fixture.revision;
  check(fixture.monitor->poll(), "fresh metadata after recovery remains eligible");
  fixture.monitor->setEnabled(false);
  ++*fixture.revision;
  fixture.monitor->setEnabled(true);
  check(!fixture.monitor->poll(), "reconnection starts a new metadata baseline");
}
void metadataReentryAndLifetime()
{
  MetadataFixture replacement;
  replacement.begin();
  ++*replacement.revision;
  replacement.onMetadata = [&] { ++replacement.incarnation; };
  check(
      !replacement.monitor->poll() && !replacement.changes, "focus destruction during metadata read denies notification"
  );
  check(!replacement.monitor->poll(), "new observation after interrupted read only rebaselines");
  ++*replacement.revision;
  replacement.onMetadata = [&] {
    check(!replacement.monitor->poll(), "nested sampler cannot publish or overwrite the outer sample");
    replacement.monitor->resetBaseline();
  };
  check(!replacement.monitor->poll() && !replacement.changes, "reentrant reset cancels the entire outer observation");
  check(!replacement.monitor->poll(), "cancelled sample cannot reinstall its old baseline");
  ++*replacement.revision;
  check(replacement.monitor->poll() && replacement.changes == 1, "later fresh samples recover after reset");

  for (int boundary = 0; boundary < 4; ++boundary) {
    MetadataFixture fixture;
    fixture.begin();
    ++*fixture.revision;
    const auto destroy = [&] { fixture.monitor.reset(); };
    if (boundary == 0)
      fixture.onFocus = destroy;
    if (boundary == 1)
      fixture.onCurrent = destroy;
    if (boundary == 2)
      fixture.onMetadata = destroy;
    if (boundary == 3)
      fixture.onChanged = destroy;
    auto *monitor = fixture.monitor.get();
    check(!monitor->poll() && !fixture.monitor, "destruction at every metadata callback boundary is safe");
  }
  MetadataFixture threaded;
  threaded.begin();
  const auto reads = threaded.metadataReads;
  ++*threaded.revision;
  bool notified = true;
  std::thread worker([&] {
    notified = threaded.monitor->poll();
    threaded.monitor->resetBaseline();
    threaded.monitor->setEnabled(false);
  });
  worker.join();
  check(
      !notified && threaded.metadataReads == reads && threaded.monitor->poll(),
      "worker calls cannot inspect or reset GUI metadata continuity"
  );

  Fixture delivery;
  delivery.negotiate();
  bool retainedLive = true;
  ClipboardFocusObservation change{delivery.focus, [&] { return retainedLive; }};
  ++change.binding.focusRevision;
  check(
      !delivery.clipboard->localChanged(change) && !delivery.reads, "sampled focus cannot be relabeled to current focus"
  );
  change.binding = delivery.focus;
  delivery.onRead = [&] { retainedLive = false; };
  check(
      !delivery.clipboard->localChanged(change) && delivery.sent.empty(),
      "sampled focus must remain live through actual text read"
  );
}
} // namespace

int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  try {
    protocol();
    policyAndFocus();
    exactReceiveAndEcho();
    reentryAndLifetime();
    metadataFocusContinuity();
    metadataReentryAndLifetime();
    QTextStream(
        stdout
    ) << "PASS: focused clipboard schema, policy, exact ownership, metadata continuity, echo, sequence and reentry\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
