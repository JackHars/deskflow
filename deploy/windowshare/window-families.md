<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Attached window families (P6)

`window-family-v1` is negotiated on the existing authenticated connection.
Supported attached surfaces get their own capture, session, painted geometry,
focus acknowledgement and input owner. A child never inherits its parent's
input lease. H08/H09 still require physical acceptance in named applications;
a passing protocol or private compositor test is not an app-compatibility claim.

## Source association and lifecycle

The Mac follows the focused Accessibility element's actual ancestry and a
bounded set of immediate children. It recognizes Sheet, Drawer, dialog Window
subroles, Menu, Popover and HelpTag. Sheet/Drawer `AXWindow` must equal the
retained parent; other roles require an exact AX parent chain. All candidates
must have the parent's process identity and process birth. Titles, shared PIDs,
visual proximity, hit testing and peer-provided native IDs cannot create links.

An independently captured child needs a distinct private AX/CG window ID, an
on-screen WindowServer record, a matching ScreenCaptureKit window and matching
complete AX/CG/capture bounds. An element rendered inside the parent's native
window stays in that window's existing stream. Ambiguous or unavailable separate
surfaces do not acquire remote input. A discovered attached surface that cannot
be captured safely returns its complete family to the Mac. Older peers keep the
existing exact Sheet/Drawer local-return behavior.

Discovery fences input before asynchronous enumeration. Enumeration has a
three-second deadline, total windows remain bounded to 16, and every callback
rechecks the parent/session/candidate lifetime. Each child waits for an actual
painted first frame before committing. No independent AX move or recovery
record is created for a sheet or popup: `prepareAttached`/`commitAttached`
require a committed root with durable recovery. Returning any member restores
that root and retires descendants first. Native dismissal retires only that
child subtree. Replaced native IDs are never used for cleanup or restoration.

Modal sheets, dialogs, menus and popovers block every ancestor. Drawers and
help tags do not themselves block their parent; help tags accept no input.
Keyboard/clipboard admission rechecks exact focused AX ancestry, including
cases where AXFocusedWindow still names a modal child's parent. Existing owned
releases retain their original recipient. If that release cannot be submitted
to the exact original focus owner, admission stays closed; there is no forced
refocus or redirect into a new dialog. This case must be exercised in H09.

A real outside press in a blocked parent can request dismissal of its attached
menu/popover. The initiating press/release is consumed. NativeHost validates the
current child geometry/relationship and invokes only that AX object's advertised
Cancel action. If exact cancellation is unavailable, it returns the family.
There is no synthetic Escape or click into the parent. Input preparation and
original-input fences also block this dismissal path.

## Wire identity

A child's Opened metadata adds exactly these fields inside `family`:

```json
{"parentSession": 91, "parentEpoch": 2, "relation": 1,
 "role": "sheet", "x": 30, "y": 40}
```

IDs are positive JSON-safe integers. Roles are `sheet`, `drawer`, `dialog`,
`menu`, `popover`, `tooltip`. Offsets are finite source points relative to the
parent's frame and bounded to ±65536. Parent owner, relation and role are
immutable; only placement may update. Parent creation must precede child
creation. Relations increase across a connection and cannot be reused after
retirement. Self-links, missing/stale parents, reparenting and graph overflow
are protocol errors. Parent close removes descendants before callbacks can
reenter or delayed destruction can affect a replacement Viewer.

## Linux adapters

Each child is a separate frameless Qt top-level with the exact native transient
parent. QObject ownership is separate so Bridge can revoke routes before
teardown. A separate Viewer lifetime token closes before QWidget destruction;
QPointer alone does not protect access to already-destroyed derived members.

X11 places the child using the parent's painted rectangle and source scale.
Its actual child XID, viewport and focus own coordinate observations. Parent
hide/destruction or changed transient parent closes input and clipboard access;
retained viewport receipts are invalidated on parent changes.

Wayland uses owned **xdg_toplevel transients**, with compositor-managed placement.
It does not invent global coordinates or advertise xdg_popup/subsurface/grab
support. In particular, native source-relative popup positioning is not yet
preserved under GNOME; record usability/placement separately in H08. Qt retains
the exact parent role/surface association. GNOME accepts a transient only when
all ancestors are uniquely associated visible top-levels owned by the same
retained Meta.WaylandClient, with bounded acyclic ancestry. Native reparenting,
parent disappearance, ambiguous association and foreign parents are denied.
Existing single-surface, focus, no-grab and queue-drain requirements still apply.
P5 ordinary titlebar watchers remain restricted to independent normal windows.

## Verification

- `windowshare-family`: strict metadata, stale/replayed relationships, bounded
  ancestry, modal ancestors, subtree retirement and shared durable recovery.
- Viewer/Bridge tests: real offscreen decode/paint, child focus/input, modal
  parent denial, outside-click suppression, input fences, parent destruction,
  negotiated capability, delayed callbacks and replacement epochs.
- `windowshare-x11-viewport-1` and `-1.25`: actual private Xvfb child XID,
  viewport/focus ownership and revocation after reparenting.
- GNOME policy tests: exact owned ancestry, foreign/hidden/duplicate/cyclic
  parents and retained grab/focus/surface exclusions.
- `gnome-window-family-tests.py`: production Viewers in private headless GNOME,
  real parent/sheet/menu mappings, dismissal, Qt parent-change denial and an
  independent native parent mutation. It does not claim Mac app input authority.

Qt 6.10 sets the native xdg parent when constructing its top-level; changing
`QWindow::transientParent` later only changes Qt state. The isolated fixture
therefore checks the Qt fence first, then sends a real `xdg_toplevel.set_parent`
on its own role to exercise the compositor fence. See the primary Qt sources:
[QWindow](https://github.com/qt/qtbase/blob/6.10/src/gui/kernel/qwindow.cpp) and
[QWaylandXdgSurface](https://github.com/qt/qtbase/blob/6.10/src/plugins/platforms/wayland/plugins/shellintegration/xdg-shell/qwaylandxdgshell.cpp).

Run physical app cases from [app-compatibility.md](app-compatibility.md) only
when the user requests them. Separate OS dialogs without an exact AX parent,
global application menus, compositor grabs, IME candidates and arbitrary popup
implementations are not blanket-supported. Never mark H08/H09 passed from the
headless fixtures or from a safe local-return fallback.
