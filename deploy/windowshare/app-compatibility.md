<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Application compatibility acceptance

This is the procedure for H08/H09, not a list of supported applications.
The scenarios below are **NOT_RUN on a final packaged candidate**. Component
tests of identity, packets and rollback do not establish native application
behavior. Run the physical cases only in the visible, bounded experimental
session after the user is ready; use disposable documents and text.

Record the candidate source/package hashes, macOS and Ubuntu versions, exact
app name/version/bundle ID, Linux session type, Qt/provider versions, physical
output layout and scaling. Also record whether holding display, original input
and focused text clipboard are enabled. A legacy-input pass cannot establish
original-input acceptance. Keep the source PID/birth/window ID and exported
session/epoch/focus/geometry alongside the native receipts and human verdict.

## Scenarios

Start with one native text document, one browser displaying a disposable local
page, and one Electron app using an unsaved scratch document. Choose installed
apps with the user; do not download software or open personal documents for
this test. Repeat the applicable rows separately for each app and each input
route. An app that has no corresponding UI records that row as not applicable,
with the missing facility identified.

| ID | Procedure | Required observations |
| --- | --- | --- |
| A01 | Export the disposable top-level window, activate its Viewer, type `aBc`, hold/release `x`, use arrows and select text with Shift. Return it to the Mac. | Matching painted geometry, exact focus acknowledgement and intended receiver; one down/up owner, no duplicate characters, no held key after return. First-click activation is recorded separately from an already-focused click. |
| A02 | In the browser, use page text controls and address bar separately. In Electron, use both editor and app-owned toolbar controls. Scroll vertically/horizontally, including trackpad momentum where available. | The intended control receives input once; parent-window content and native browser/app chrome are distinguished. A web page working does not establish native menu or popup support. |
| A03 | Export two top-level windows from the same process, then a third from another app. Overlap their Viewers and alternate focus; resize one, hide/minimize another, and close one. | Session/epoch and native window identity select each receiver. A shared PID/title is insufficient. Hidden/unpainted/stale Viewers have no current input/clipboard target; returning one does not move or focus another. |
| A04 | In the exported native app, open a disposable Save/Open sheet. In browser/Electron, exercise a native file chooser separately from an in-page dialog. Cancel locally after fallback. | For an exact AX Sheet/Drawer attached to the exported parent, remote admission closes and that parent returns to the Mac without a forced raise/refocus. The dialog stays locally usable. This outcome is **safe fallback**, not remote dialog support. |
| A05 | While one parent is exported, open a sheet belonging to a different same-process window. Then dismiss the exported parent's sheet before its queued return completes. | The unrelated sheet cannot return another parent. Once the exact parent return is latched, closing its child cannot revive the old export or accept delayed input/Ready/clipboard work. A new export uses a new session. |
| A06 | Exercise a context menu, application menu, tooltip, autocomplete list and detached popup individually. Record whether each is drawn inside the source surface or is a separate native window. | Do not infer child association or complete capture from shared PID, visual proximity or parent pixels. Unsupported separate children remain unaccepted; return the source before local interaction if their full geometry/receiver cannot be established. |
| A07 | Use ordinary shortcuts in a disposable document, including both physical modifier sides, key repeat and a held key during focus replacement. Do not use destructive shortcuts. | Correct source meaning, exact delivered modifier ownership and a single release. A focus change must not direct a retained parent's key-up into another app/window/sheet. A denied cleanup must be reported as such; it is not a successful release. |
| A08 | With clipboard policy absent on both peers, then present on only one, copy a disposable marker. Enable both policies in a fresh session and copy a different marker in each direction. | Default and one-sided policies produce no clipboard reads/writes. Both-enabled fresh plain text is shared only through exact focused owners. Pasting is a user action; no synthetic copy/paste shortcut is sent. |
| A09 | With both policies enabled, apply remote text, switch focus, and wait for delayed clipboard notifications. Copy a new marker only after the new focus has settled. Repeat with hide/minimize, resize, reconnect and return. | No synchronous or delayed echo loop, stale-focus packet apply, old-sequence replay or read merely on enable/focus gain. A changed native metadata sample requires the preceding retained focus observation to remain live. |
| A10 | Test empty text, ASCII, accented text and emoji through explicit copy/paste. Test direct Unicode key delivery separately. Use a file/image/HTML-only clipboard item and text above 8,192 UTF-8 bytes. | Accepted text round-trips once and respects the byte bound. Unsupported payloads do not cross the text protocol. Rich content containing an explicit plain-text representation may share only that representation; files/URLs are excluded. |
| A11 | Select an input method that produces preedit/candidate windows, then attempt composition in the Viewer. Also test switching input methods during an active route. | Viewer composition remains unsupported: `WA_InputMethodEnabled` is false and `QInputMethodEvent` preedit/commit is ignored. There is no candidate-window capture, composition-owner transfer or fallback text synthesis. Preserved raw Unicode fields do not prove an IME workflow. |
| A12 | Move the local source during capture preparation, then cancel or interrupt the preparation before source mutation/commit. Repeat after an owned relocation was attempted. | Untouched preparation closes its provisional share and leaves current local position/focus alone. Once a mutation was attempted, exact-source recovery remains required. A physical drag continuing locally must not be relabeled as a completed cross-machine handoff. |

For every failure, retain the exact action and observed receiver, not only a
clean shutdown. If an unexpected app receives input, stop the experimental
session and record the failure before another case. Human visual confirmation
and native event receipts are separate evidence. Do not mark H08/H09 complete
from a subset of apps or from the safe-fallback rows alone.

## Exact attached-child boundary

The host follows `AXFocusedUIElement` to `AXTopLevelUIElement`, accepts only
Sheet/Drawer roles, and requires that child's `AXWindow` to equal the retained
exported parent. It verifies process birth and the parent's CG window identity
around the AX queries. The return is latched before release callbacks, so a
queued cleanup cannot reopen an old export. Recovery moves only that parent;
it deliberately does not raise it or activate its application.

Apple specifies that `AXWindow` names the attached Window for a Sheet/Drawer,
whereas `AXTopLevelUIElement` may name the Sheet/Drawer itself. See
[AXWindow](https://developer.apple.com/documentation/applicationservices/kaxwindowattribute)
and [AXTopLevelUIElement](https://developer.apple.com/documentation/applicationservices/kaxtopleveluielementattribute).
The matching primary SDK declarations are `AXAttributeConstants.h`, definitions
of `kAXWindowAttribute` and `kAXTopLevelUIElementAttribute`.

This does not assign an arbitrary popup to its parent. True remote child
support still needs exact child AX/CG identity, complete pixel extents and
origin, a new painted geometry, stacking, focus, and an independently correct
input/release destination. Apple's `includeChildWindows` declaration describes
display-bound window/application sharing; it does not establish those
properties for the host's desktop-independent single-window filter.
[ScreenCaptureKit property](https://developer.apple.com/documentation/screencapturekit/scstreamconfiguration/includechildwindows)

The remaining work is not just enabling that property (the host already sets
it). A display-bound parent-plus-child capture path could render the bounded
union. The Host now uses an [exact private AX/native window join](native-window-identity.md)
for initial export and recovery, replacing title/geometry matching. Its owned
native regression covers AX windows, rename and replacement. It does not yet
establish that an attached sheet, drawer or popup has its own capturable native
window: the SPI can identify the containing parent instead. Each supported
child role still needs that distinction, separate retained child geometry,
painted acknowledgement, stacking, keyboard focus and release ownership.
A mouse hit-test supplies no AX association, and a child preview alone would
not complete remote dialog support. The local-return fallback remains explicit.

## Clipboard observation limit

[Focused clipboard policy](focused-clipboard.md) is optional and independent of
input authority. Its binding authorizes access at an observed exact focus; it
does not identify which process wrote the system pasteboard. The native sampler
uses change-count metadata and retained live focus observations, with new
baselines for known release/focus/reconnect transitions. It does not read text
to establish that baseline. The controller rechecks the sampled and current
owners before/after a later text read.

There is no continuous AX focus notification stream in this implementation.
An external away-and-back entirely between observations is not disproved by
matching endpoints. Clipboard managers and other background writers are also
possible. Therefore report **observed-focus text sharing**, never exclusive
source-app content provenance. The Cocoa activation-driven Qt clipboard signal
is not used as a native-host publication trigger.

## Deterministic evidence versus application evidence

`windowshare-clipboard`, Bridge and Viewer suites cover schema/policy, exact
bindings, sampled metadata, echo/sequence, invalidation and callback lifetime
using private strings. Native original-input tests cover exact Sheet/Drawer
return accounting without querying AX or opening windows. Native recovery tests
exercise the production return procedure with injected callbacks and private
journals. These establish component behavior only; all app-visible rows above
still require their recorded native acceptance run.
