/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2011 Nick Bolton
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXKeyStateTests.h"

#include "base/EventQueue.h"

#define SHIFT_ID_L kKeyShift_L
#define SHIFT_ID_R kKeyShift_R
#define SHIFT_BUTTON 57
#define A_CHAR_ID 0x00000061
#define A_CHAR_BUTTON 001

namespace {

// Keep the mapping regressions independent of the current keyboard layout and
// physical state. These tests never call updateKeyMap or any fakeKey method.
class FixedLayoutKeyState final : public OSXKeyState
{
public:
  using OSXKeyState::OSXKeyState;
  int32_t pollActiveGroup() const override
  {
    return 0;
  }
};

} // namespace

void OSXKeyStateTests::initTestCase()
{
  m_arch.init();
  m_log.setFilter(LogLevel::Level::Verbose);
}

void OSXKeyStateTests::mapModifiersFromOSX_OSXMask()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  KeyModifierMask outMask = 0;

  uint32_t shiftMask = 0 | kCGEventFlagMaskShift;
  outMask = keyState.mapModifiersFromOSX(shiftMask);
  QCOMPARE(outMask, KeyModifierShift);

  uint32_t ctrlMask = 0 | kCGEventFlagMaskControl;
  outMask = keyState.mapModifiersFromOSX(ctrlMask);
  QCOMPARE(outMask, KeyModifierControl);

  uint32_t altMask = 0 | kCGEventFlagMaskAlternate;
  outMask = keyState.mapModifiersFromOSX(altMask);
  QCOMPARE(outMask, KeyModifierAlt);

  uint32_t cmdMask = 0 | kCGEventFlagMaskCommand;
  outMask = keyState.mapModifiersFromOSX(cmdMask);
  QCOMPARE(outMask, KeyModifierSuper);

  uint32_t capsMask = 0 | kCGEventFlagMaskAlphaShift;
  outMask = keyState.mapModifiersFromOSX(capsMask);
  QCOMPARE(outMask, KeyModifierCapsLock);

  uint32_t numMask = 0 | kCGEventFlagMaskNumericPad;
  outMask = keyState.mapModifiersFromOSX(numMask);
  QCOMPARE(outMask, KeyModifierNumLock);
}

void OSXKeyStateTests::mapHotkeyModifiersToMac_data()
{
  QTest::addColumn<uint32_t>("key");
  QTest::addColumn<uint32_t>("virtualKey");
  QTest::addColumn<uint32_t>("deskflowMask");
  QTest::addColumn<uint32_t>("physicalFlags");
  QTest::addColumn<uint32_t>("carbonMask");

  QTest::newRow("alt-is-option") << uint32_t(kKeyRight) << uint32_t(kVK_RightArrow) << uint32_t(KeyModifierAlt)
                                 << uint32_t(kCGEventFlagMaskAlternate) << uint32_t(optionKey);
  QTest::newRow("super-is-command") << uint32_t(kKeyRight) << uint32_t(kVK_RightArrow) << uint32_t(KeyModifierSuper)
                                    << uint32_t(kCGEventFlagMaskCommand) << uint32_t(cmdKey);
  QTest::newRow("alt-and-super") << uint32_t(kKeyLeft) << uint32_t(kVK_LeftArrow)
                                 << uint32_t(KeyModifierAlt | KeyModifierSuper)
                                 << uint32_t(kCGEventFlagMaskAlternate | kCGEventFlagMaskCommand)
                                 << uint32_t(optionKey | cmdKey);
  QTest::newRow("control-shift-option-right")
      << uint32_t(kKeyRight) << uint32_t(kVK_RightArrow)
      << uint32_t(KeyModifierControl | KeyModifierShift | KeyModifierAlt)
      // Exact physical flags from the failed shortcut receipt, including device
      // side bits, numeric-pad and secondary-Fn flags that the matcher excludes.
      << uint32_t(0xae0123) << uint32_t(controlKey | shiftKey | optionKey);
  QTest::newRow("control-shift-option-left") << uint32_t(kKeyLeft) << uint32_t(kVK_LeftArrow)
                                             << uint32_t(KeyModifierControl | KeyModifierShift | KeyModifierAlt)
                                             << uint32_t(0xae0123) << uint32_t(controlKey | shiftKey | optionKey);
  QTest::newRow(
      "control-shift-command"
  ) << uint32_t(kKeyRight)
    << uint32_t(kVK_RightArrow) << uint32_t(KeyModifierControl | KeyModifierShift | KeyModifierSuper)
    << uint32_t(kCGEventFlagMaskControl | kCGEventFlagMaskShift | kCGEventFlagMaskCommand)
    << uint32_t(controlKey | shiftKey | cmdKey);
  QTest::newRow("control-shift-unchanged")
      << uint32_t(kKeyLeft) << uint32_t(kVK_LeftArrow) << uint32_t(KeyModifierControl | KeyModifierShift)
      << uint32_t(kCGEventFlagMaskControl | kCGEventFlagMaskShift) << uint32_t(controlKey | shiftKey);
}

void OSXKeyStateTests::mapHotkeyModifiersToMac()
{
  QFETCH(uint32_t, key);
  QFETCH(uint32_t, virtualKey);
  QFETCH(uint32_t, deskflowMask);
  QFETCH(uint32_t, physicalFlags);
  QFETCH(uint32_t, carbonMask);

  deskflow::KeyMap keyMap;
  deskflow::KeyMap::KeyItem item;
  item.m_id = key;
  item.m_button = static_cast<KeyButton>(virtualKey + 1);
  keyMap.addKeyEntry(item);
  keyMap.finish();
  EventQueue eventQueue;
  FixedLayoutKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  uint32_t mappedKey = 0;
  uint32_t registeredMask = 0;
  QVERIFY(keyState.mapDeskflowHotKeyToMac(key, deskflowMask, mappedKey, registeredMask));
  QCOMPARE(mappedKey, virtualKey);
  QCOMPARE(registeredMask, carbonMask);
  // This is the actual tap-side hotkey comparison. Registration must agree
  // with the physical event's Carbon mask, rather than swapping Alt/Super.
  QCOMPARE(registeredMask, keyState.mapModifiersToCarbon(physicalFlags) & 0xff00u);
}

void OSXKeyStateTests::fakePollShift()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  keyState.fakeKeyDown(SHIFT_ID_L, 0, 1, "en");
  QVERIFY(isKeyPressed(keyState, SHIFT_BUTTON));

  keyState.fakeKeyUp(1);
  QVERIFY(!isKeyPressed(keyState, SHIFT_BUTTON));

  keyState.fakeKeyDown(SHIFT_ID_R, 0, 2, "en");
  QVERIFY(isKeyPressed(keyState, SHIFT_BUTTON));

  keyState.fakeKeyUp(2);
  QVERIFY(!isKeyPressed(keyState, SHIFT_BUTTON));
}

void OSXKeyStateTests::fakePollChar()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  keyState.fakeKeyDown(A_CHAR_ID, 0, 1, "en");
  QVERIFY(isKeyPressed(keyState, A_CHAR_BUTTON));

  keyState.fakeKeyUp(1);
  QVERIFY(!isKeyPressed(keyState, A_CHAR_BUTTON));

  // HACK: delete the key in case it was typed into a text editor.
  // we should really set focus to an invisible window.
  keyState.fakeKeyDown(kKeyBackSpace, 0, 2, "en");
  keyState.fakeKeyUp(2);
}

void OSXKeyStateTests::fakePollCharWithModifier()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  keyState.fakeKeyDown(A_CHAR_ID, KeyModifierShift, 1, "en");
  QVERIFY(isKeyPressed(keyState, A_CHAR_BUTTON));

  keyState.fakeKeyUp(1);
  QVERIFY(!isKeyPressed(keyState, A_CHAR_BUTTON));

  // HACK: delete the key in case it was typed into a text editor.
  // we should really set focus to an invisible window.
  keyState.fakeKeyDown(kKeyBackSpace, 0, 2, "en");
  keyState.fakeKeyUp(2);
}

bool OSXKeyStateTests::isKeyPressed(const OSXKeyState &keyState, KeyButton button)
{
  // HACK: allow os to realize key state changes.
  Arch::sleep(.2);

  IKeyState::KeyButtonSet pressed;
  keyState.pollPressedKeys(pressed);

  IKeyState::KeyButtonSet::const_iterator it;
  for (it = pressed.begin(); it != pressed.end(); ++it) {
    LOG_DEBUG("checking key %d", *it);
    if (*it == button) {
      return true;
    }
  }
  return false;
}

QTEST_MAIN(OSXKeyStateTests)
