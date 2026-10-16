/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "../../lib/platform/HotKeyCapture.h"
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
} // namespace
int main()
{
  try {
    HotKeyCapture capture;
    auto down = capture.observe(124, true, false, 7);
    check(down.consumed && down.hotkey == 7 && down.down, "shortcut down consumed before asynchronous switch");
    for (int i = 0; i < 10; ++i) {
      const auto repeat = capture.observe(124, true, true, i < 5 ? 7 : 0);
      check(repeat.consumed && !repeat.hotkey, "shortcut repeat stays consumed after modifiers release");
    }
    auto up = capture.observe(124, false, false, 0);
    check(up.consumed && up.hotkey == 7 && !up.down, "release remains paired with original shortcut");
    check(!capture.observe(124, false, false, 0).consumed, "duplicate release cannot resurrect shortcut");
    check(!capture.observe(124, true, false, 0).consumed, "next ordinary arrow works");
    check(!capture.observe(124, true, true, 7).consumed, "adding modifiers cannot steal an ordinary held key");
    check(!capture.observe(124, false, false, 7).consumed, "ordinary release remains ordinary");
    auto orphan = capture.observe(124, true, true, 7);
    check(orphan.consumed && !orphan.hotkey, "orphan shortcut repeat is swallowed without fabricating press");
    up = capture.observe(124, false, false, 0);
    check(up.consumed && !up.hotkey, "orphan release does not fabricate a shortcut notification");
    check(capture.observe(123, true, false, 8).hotkey == 8, "other shortcut starts independently");
    check(capture.observe(124, true, false, 7).hotkey == 7, "multiple physical shortcut keys are independent");
    check(capture.observe(123, false, false, 9).hotkey == 8, "changed registration cannot retarget held release");
    check(!capture.observe(0, true, false, 0).consumed, "ordinary A key is unaffected");
    check(!capture.observe(65535, true, false, 7).consumed, "invalid virtual key cannot address storage");
    std::puts("PASS shortcut press, repeat, modifier changes, release and ordinary input");
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL %s\n", error.what());
    return 1;
  }
}
