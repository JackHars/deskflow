/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ComputerInputSequenceTests.h"

#include "MockEventQueue.h"
#include "deskflow/Computer.h"
#include "deskflow/InputSequence.h"
#include "deskflow/PlatformComputer.h"

#include <array>
#include <limits>
#include <optional>
#include <stdexcept>

using deskflow::Computer;
using deskflow::windowshare::InputSequenceOutcome;
using deskflow::windowshare::InputSequenceReceipt;
using deskflow::windowshare::InputSequenceSubmissionScope;
using deskflow::windowshare::InputSequenceToken;

namespace {

constexpr InputSequenceToken token{5, 9, {13, 17, 1}};
enum class Input
{
  KeyDown,
  KeyRepeat,
  KeyUp,
  MouseDown,
  MouseUp,
  MouseMove,
  MouseRelativeMove,
  MouseWheel
};
constexpr std::array names{"key-down", "key-repeat", "key-up", "mouse-down", "mouse-up", "motion", "relative", "wheel"};

// Exercises the real Computer dispatch without constructing any OS platform,
// input device, compositor connection, key map, or display. The default base
// capability is deliberately retained to cover uninstrumented backends.
class QuietPlatform : public PlatformComputer
{
public:
  explicit QuietPlatform(IEventQueue *events) : PlatformComputer(events)
  {
  }
  void *getEventTarget() const override
  {
    return const_cast<QuietPlatform *>(this);
  }
  bool getClipboard(ClipboardID, IClipboard *) const override
  {
    return false;
  }
  void getShape(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const override
  {
    x = y = 0;
    width = height = 100;
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
    x = y = 0;
  }
  void getCursorCenter(int32_t &x, int32_t &y) const override
  {
    x = y = 0;
  }
  void reconfigure(uint32_t) override
  {
  }
  uint32_t activeSides() override
  {
    return 0;
  }
  void warpCursor(int32_t, int32_t) override
  {
  }
  uint32_t registerHotKey(KeyID, KeyModifierMask) override
  {
    return 0;
  }
  void unregisterHotKey(uint32_t) override
  {
  }
  void fakeInputBegin() override
  {
  }
  void fakeInputEnd() override
  {
  }
  int32_t getJumpZoneSize() const override
  {
    return 0;
  }
  bool isAnyMouseButtonDown(uint32_t &) const override
  {
    return false;
  }
  void enable() override
  {
  }
  void disable() override
  {
  }
  void enter() override
  {
  }
  bool canLeave() override
  {
    return true;
  }
  void leave() override
  {
  }
  bool setClipboard(ClipboardID, const IClipboard *) override
  {
    return true;
  }
  void checkClipboards() override
  {
  }
  void openScreensaver(bool) override
  {
  }
  void closeScreensaver() override
  {
  }
  void screensaver(bool) override
  {
  }
  void resetOptions() override
  {
  }
  void setOptions(const OptionsList &) override
  {
  }
  void setSequenceNumber(uint32_t) override
  {
  }
  std::string getSecureInputApp() const override
  {
    return {};
  }
  bool isPrimary() const override
  {
    return false;
  }

  void fakeKeyDown(KeyID id, KeyModifierMask mask, KeyButton button, const std::string &lang) override
  {
    key = id;
    modifiers = mask;
    keyButton = button;
    language = lang;
    record(Input::KeyDown);
  }
  bool fakeKeyRepeat(KeyID id, KeyModifierMask mask, int32_t repeat, KeyButton button, const std::string &lang) override
  {
    key = id;
    modifiers = mask;
    keyButton = button;
    language = lang;
    repeatCount = repeat;
    record(Input::KeyRepeat);
    return false;
  }
  bool fakeKeyUp(KeyButton button) override
  {
    keyButton = button;
    record(Input::KeyUp);
    return false;
  }
  bool fakeCtrlAltDel() override
  {
    ++secureAttentionCalls;
    return consumeSecureAttention;
  }
  void fakeMouseButton(ButtonID button, bool press) override
  {
    mouseButton = button;
    record(press ? Input::MouseDown : Input::MouseUp);
  }
  void fakeMouseMove(int32_t x, int32_t y) override
  {
    position = {x, y};
    record(Input::MouseMove);
  }
  void fakeMouseRelativeMove(int32_t x, int32_t y) const override
  {
    position = {x, y};
    record(Input::MouseRelativeMove);
  }
  void fakeMouseWheel(ScrollDelta delta) const override
  {
    position = {delta.x, delta.y};
    record(Input::MouseWheel);
  }

  unsigned submissionsPerCall = 0;
  bool throwAfterSubmissions = false;
  bool consumeSecureAttention = false;
  unsigned secureAttentionCalls = 0;
  mutable unsigned calls = 0;
  mutable Input lastInput = Input::KeyDown;
  mutable std::optional<InputSequenceToken> observedToken;
  mutable std::array<int32_t, 2> position{};
  KeyID key = 0;
  KeyModifierMask modifiers = 0;
  KeyButton keyButton = 0;
  ButtonID mouseButton = 0;
  int32_t repeatCount = 0;
  std::string language;

protected:
  void updateButtons() override
  {
  }
  IKeyState *getKeyState() const override
  {
    throw std::logic_error("The input-dispatch fixture must not construct a native key state");
  }
  void handleSystemEvent(const Event &) override
  {
  }

private:
  void record(Input input) const
  {
    ++calls;
    lastInput = input;
    observedToken = InputSequenceSubmissionScope::currentToken();
    if (supportsInputSequenceTrace()) {
      for (unsigned index = 0; index < submissionsPerCall; ++index)
        InputSequenceSubmissionScope::submitted();
    }
    if (throwAfterSubmissions)
      throw std::runtime_error("Controlled backend failure");
  }
};

class TraceablePlatform final : public QuietPlatform
{
public:
  using QuietPlatform::QuietPlatform;
  bool supportsInputSequenceTrace() const noexcept override
  {
    return true;
  }
};

void dispatch(Computer &computer, Input input)
{
  switch (input) {
  case Input::KeyDown:
    computer.keyDown('q', KeyModifierShift, 43, "en");
    break;
  case Input::KeyRepeat:
    computer.keyRepeat('q', KeyModifierShift, 7, 43, "en");
    break;
  case Input::KeyUp:
    computer.keyUp('q', KeyModifierShift, 43);
    break;
  case Input::MouseDown:
    computer.mouseDown(kButtonRight);
    break;
  case Input::MouseUp:
    computer.mouseUp(kButtonRight);
    break;
  case Input::MouseMove:
    computer.mouseMove(-17, 253);
    break;
  case Input::MouseRelativeMove:
    computer.mouseRelativeMove(-17, 253);
    break;
  case Input::MouseWheel:
    computer.mouseWheel(-17, 253);
    break;
  }
}

} // namespace

void ComputerInputSequenceTests::initTestCase()
{
  m_arch.init();
  m_log.setFilter(LogLevel::Level::Error);
}

void ComputerInputSequenceTests::inputDispatch_data()
{
  QTest::addColumn<int>("input");
  QTest::addColumn<bool>("supported");
  QTest::addColumn<unsigned>("submissions");
  for (std::size_t index = 0; index < names.size(); ++index) {
    QTest::newRow((std::string(names[index]) + "-unsupported").c_str()) << static_cast<int>(index) << false << 1u;
    for (unsigned submissions : {0u, 1u, 3u}) {
      QTest::newRow((std::string(names[index]) + "-" + std::to_string(submissions)).c_str())
          << static_cast<int>(index) << true << submissions;
    }
  }
}

void ComputerInputSequenceTests::inputDispatch()
{
  QFETCH(int, input);
  QFETCH(bool, supported);
  QFETCH(unsigned, submissions);
  MockEventQueue events;
  auto *platform = supported ? new TraceablePlatform(&events) : new QuietPlatform(&events);
  Computer computer(platform, &events);
  QCOMPARE(platform->supportsInputSequenceTrace(), supported);
  platform->submissionsPerCall = submissions;
  InputSequenceSubmissionScope scope(token);
  dispatch(computer, static_cast<Input>(input));
  QCOMPARE(platform->calls, 1u);
  QCOMPARE(platform->lastInput, static_cast<Input>(input));
  QVERIFY(platform->observedToken == token);
  const auto receipt = scope.receipt();
  const auto expected = !supported         ? InputSequenceOutcome::Unsupported
                        : !submissions     ? InputSequenceOutcome::Filtered
                        : submissions == 1 ? InputSequenceOutcome::Submitted
                                           : InputSequenceOutcome::Expanded;
  QCOMPARE(receipt.outcome, expected);
  QCOMPARE(receipt.submissions, supported ? submissions : 0u);
  QCOMPARE(receipt.generation, token.generation);
  QCOMPARE(receipt.wireSequence, token.wireSequence);
  switch (static_cast<Input>(input)) {
  case Input::KeyRepeat:
    QCOMPARE(platform->repeatCount, 7);
    [[fallthrough]];
  case Input::KeyDown:
    QCOMPARE(platform->key, static_cast<KeyID>('q'));
    QCOMPARE(platform->modifiers, KeyModifierShift);
    QCOMPARE(platform->language, std::string("en"));
    [[fallthrough]];
  case Input::KeyUp:
    QCOMPARE(platform->keyButton, 43);
    break;
  case Input::MouseDown:
  case Input::MouseUp:
    QCOMPARE(platform->mouseButton, kButtonRight);
    break;
  case Input::MouseMove:
  case Input::MouseRelativeMove:
  case Input::MouseWheel:
    QCOMPARE(platform->position, (std::array<int32_t, 2>{-17, 253}));
    break;
  }
}

void ComputerInputSequenceTests::secureAttention_data()
{
  QTest::addColumn<bool>("supported");
  QTest::addColumn<bool>("consumed");
  QTest::newRow("unsupported-consumed") << false << true;
  QTest::newRow("supported-consumed") << true << true;
  QTest::newRow("unsupported-normal-key") << false << false;
  QTest::newRow("supported-normal-key") << true << false;
}

void ComputerInputSequenceTests::secureAttention()
{
  QFETCH(bool, supported);
  QFETCH(bool, consumed);
  MockEventQueue events;
  auto *platform = supported ? new TraceablePlatform(&events) : new QuietPlatform(&events);
  Computer computer(platform, &events);
  platform->submissionsPerCall = 1;
  platform->consumeSecureAttention = consumed;
  InputSequenceSubmissionScope scope(token);
  computer.keyDown(kKeyDelete, KeyModifierControl | KeyModifierAlt, 43, "en");
  QCOMPARE(platform->secureAttentionCalls, 1u);
  QCOMPARE(platform->calls, consumed ? 0u : 1u);
  QCOMPARE(
      scope.receipt().outcome, !supported ? InputSequenceOutcome::Unsupported
                               : consumed ? InputSequenceOutcome::Failed
                                          : InputSequenceOutcome::Submitted
  );
  QCOMPARE(scope.receipt().submissions, supported && !consumed ? 1u : 0u);
}

void ComputerInputSequenceTests::exceptionDoesNotBecomeSuccessfulSubmission_data()
{
  QTest::addColumn<unsigned>("submissions");
  QTest::newRow("before-submission") << 0u;
  QTest::newRow("after-submission") << 1u;
}

void ComputerInputSequenceTests::exceptionDoesNotBecomeSuccessfulSubmission()
{
  QFETCH(unsigned, submissions);
  MockEventQueue events;
  auto *platform = new TraceablePlatform(&events);
  Computer computer(platform, &events);
  platform->submissionsPerCall = submissions;
  platform->throwAfterSubmissions = true;
  InputSequenceReceipt receipt;
  bool caught = false;
  try {
    InputSequenceSubmissionScope scope(token);
    struct ReadDuringUnwind
    {
      InputSequenceSubmissionScope &scope;
      InputSequenceReceipt &receipt;
      ~ReadDuringUnwind()
      {
        receipt = scope.receipt();
      }
    } readDuringUnwind{scope, receipt};
    computer.mouseMove(1, 2);
  } catch (const std::runtime_error &) {
    caught = true;
  }
  QVERIFY(caught);
  QCOMPARE(platform->calls, 1u);
  QCOMPARE(receipt.outcome, InputSequenceOutcome::Failed);
  QCOMPARE(receipt.submissions, submissions);
  QVERIFY(!InputSequenceSubmissionScope::currentToken());
}

void ComputerInputSequenceTests::overflowFailsWithoutWrapping()
{
  MockEventQueue events;
  auto *platform = new TraceablePlatform(&events);
  Computer computer(platform, &events);
  platform->submissionsPerCall = static_cast<unsigned>(std::numeric_limits<std::uint16_t>::max()) + 1;
  InputSequenceSubmissionScope scope(token);
  computer.keyRepeat('q', 0, 7, 43, "en");
  QCOMPARE(platform->calls, 1u);
  QCOMPARE(scope.receipt().outcome, InputSequenceOutcome::Failed);
  QCOMPARE(scope.receipt().submissions, std::numeric_limits<std::uint16_t>::max());
}

void ComputerInputSequenceTests::ordinaryUnscopedDispatchIsUnchanged()
{
  MockEventQueue events;
  auto *platform = new TraceablePlatform(&events);
  Computer computer(platform, &events);
  platform->submissionsPerCall = 3;
  for (std::size_t index = 0; index < names.size(); ++index) {
    dispatch(computer, static_cast<Input>(index));
    QVERIFY(!platform->observedToken);
    QCOMPARE(platform->lastInput, static_cast<Input>(index));
  }
  QCOMPARE(platform->calls, static_cast<unsigned>(names.size()));
  InputSequenceSubmissionScope scope(token);
  QCOMPARE(scope.receipt().outcome, InputSequenceOutcome::Unsupported);
  QCOMPARE(scope.receipt().submissions, 0);
}

QTEST_GUILESS_MAIN(ComputerInputSequenceTests)
