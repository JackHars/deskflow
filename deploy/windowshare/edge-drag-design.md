<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Edge-drag preparation and rollback boundary

P5/H07 is unfinished. WindowShare currently has explicit export, painted-frame
readiness, optional holding relocation and exact-source return. Deskflow's
Server separately maps cursor crossings through its configured neighbors.
There is no production window-edge trigger, native-grab handoff packet,
cross-machine drag commit, or drag-back transaction. An ordinary export/return
must not be presented as natural bidirectional window dragging.

The user confirmed on 2026-10-14 that normal application titlebar dragging is
required. A selected-window thumbnail interaction does not satisfy this
requirement. The preview-and-drop research below refers to a provisional
rendering during that same ordinary drag, not a replacement interaction.

## What the APIs establish

AppKit's `NSWindow::performWindowDragWithEvent:` accepts an original mouse-down
for that app's window and delegates movement to WindowServer. The call returns
immediately, and the application may not receive mouse-up. Neither an AppKit
mouse-up receipt nor the annotation on a PID-targeted CGEvent acknowledges
release of an arbitrary existing WindowServer drag.
[Apple API](https://developer.apple.com/documentation/appkit/nswindow/performdrag(with:))
The exact primary SDK declaration is in `AppKit/NSWindow.h`, under Window
Dragging. `kAXWindowMovedNotification` in `AXNotificationConstants.h` is an
end-of-move notification, not a live drag-ownership token.

Qt's `QWindow::startSystemMove()` starts a platform-supported move of the
owned QWindow and reports whether the operation is supported. It does not
transfer the source machine's grab. QtWayland 6.10.2 forwards through its
current input seat and shell surface; an arbitrary remote mouse-down cannot
be treated as that seat's valid native move request.
[Qt API](https://doc.qt.io/qt-6/qwindow.html#startSystemMove)

The inspected Mutter 50.1 implementation has exact-`MetaWindow` frame movement
and compositor drag operations. A GNOME provider could own a Viewer placement
transaction after verifying that same Viewer/native role. That is Linux-side
window management only. It supplies no authority over the Mac source drag and
is not presently wired into this provider. Runtime/API version checks and
native acceptance remain necessary for any future use.

## Alternative: preview while owned, commit on the real drop

A stronger direction is to keep the original native grab and its real physical
up on the original machine, show only a provisional remote preview, and commit
ownership after the actual drop completes. This removes the need to cancel or
transfer a WindowServer grab using synthetic input. Preview must not activate
the destination, start a second native grab, or redirect the held press. The
Server must retain the source's local route through the up rather than allowing
its ordinary cursor edge switch to consume that up on the peer.

For Linux → Mac, the owned provider can observe `grab-op-begin`/`grab-op-end`
for the exact Viewer `MetaWindow` and operation. The inspected Mutter 50.1
`meta_window_drag_end` dismisses the Clutter grab before emitting `grab-op-end`
with the exact effective window and operation. **That signal alone is not a
successful physical drop:** window unmanagement also invokes that same end
path. Require the matching genuine release, still-live exact Viewer/native
role, final frame/topology and no intervening cancellation before returning
the retained Mac source. This source contract still needs runtime capability
checks, connected protocol changes and real native acceptance.

For arbitrary Mac → Linux, `AXUIElementCopyElementAtPosition` is documented
as a z-order hit test at the time of the query. `AXWindowMoved` identifies the
moved AX window and is documented at the end of movement, but carries no
initiating press/drag incarnation or distinction between a user drag and an
application-driven move. The public CG event window-under-pointer fields do
not document ownership of the active WindowServer grab. Current
`OriginalInputRecord` retains raw button/motion semantics, but not either
window-under-pointer field. Adding a later AX hit test to that record would
not repair the missing event-time grab identity.

An explicitly preselected exact source could support a narrower, declared
"preview and drop this selected window" interaction: the user has already
named the source, so it need not be inferred from foreground PID or a moved
frame. Such a UX still needs uninterrupted source-local real down/up delivery,
same-window end-of-move observation, exact lifetime/geometry/layout checks and
bounded cancellation. It must not be advertised as discovering any arbitrary
native titlebar drag. For a fixture-owned AppKit window, the actual call to
`performWindowDragWithEvent:` with its original down supplies exact initiation
evidence that an external AX observer lacks. This distinction is a feasible
research boundary, not a completed P5 claim or the requested product behavior.

Primary declarations inspected: SDK `AXUIElement.h` hit-testing documentation;
`AXNotificationConstants.h` window-move notification; `CGEventTypes.h` fields
91/92; `AppKit/NSWindow.h` original-down drag API. The inspected Mutter source
is `src/compositor/meta-window-drag.c`, `meta_window_drag_end` and
`on_grab_window_unmanaged`, in the retained 50.1-0ubuntu2.4 source audit. The
existing [owned probe](native-drag-probe.md) observes only its own window and
does not grant arbitrary-application authority.

## Connected preparation rollback

The Host now uses an [exact private AX/native window join](native-window-identity.md)
for export and recovery. It distinguishes same-title/same-frame windows and
rejects stale replacements. This supplies a stronger source-identity primitive;
it does not establish which window owns an ongoing WindowServer drag. Two
physical owned-probe runs on 2026-10-14 observed continued native movement after
the PID-targeted up reached the app. That release method cannot authorize the
requested continuous drag handoff.

`NativeSourceTransaction` is recovery bookkeeping used by the real NativeHost
return path. New Preparing exports write `restoreRequired:false` because
starting capture does not take ownership of the source's placement or focus.
Before a source mutation is attempted or a remote session commits, the host
durably writes `restoreRequired:true`. Legacy records without the field are
conservatively restored; malformed markers are retained and denied.

In-process cancellation of an untouched preparation fences admission, releases
only that export's input owner, stops capture and closes the provisional share.
It does not query, move, unminimize or raise the source. A source the user moved
during preparation stays at the user's current local location. An attempted
native placement, including a partially failed placement, or a committed
remote session still requires the existing exact-identity restoration. A
crash after durable intent but before the native call is conservatively treated
as possibly mutated. No cleanup result grants drag ownership.

The native recovery test uses this same return procedure with injected native
callbacks, including callback replacement/reentry and failed persistence. It
also executes actual private-journal recovery for untouched and malformed
records. These tests do not create a window or input event. This is a useful
P5 prerequisite, not completion of H07.

## Required future transaction

| Boundary | Mac → Linux requirement | Linux → Mac requirement |
| --- | --- | --- |
| Candidate | Fresh non-synthetic press, exact retained dragged source window and source geometry, physical-output identity and configured Deskflow edge/neighbor. AX movement or foreground PID alone is insufficient. | Exact owned Viewer/native role plus actual compositor move operation and button/seat identity; selected peer and current topology. Content drag inside the Viewer is distinct from moving its native frame. |
| Prepare | Start bounded capture without moving the source or redirecting its still-owned press. Reserve the exact remote Viewer and wait for its painted generation. A distinct preparation operation is needed: current Ready can commit and must not be reused as a held-drag handoff. | Keep the existing exported source recoverable and the Viewer visible until the requested physical Mac destination and current source identity are verified. No fabricated native down on the Mac. |
| Commit | Need independently supported source-grab completion/transfer and exact destination move ownership, with one designated physical-up destination. A timer, quiet queue, app-up receipt or changed window frame is insufficient. | Need completion of the exact Linux move/grab before source return and supported continuation on the Mac. A Qt move API result cannot establish native WindowServer ownership. |
| Cancel before mutation | Close only the provisional remote share; leave the existing local source drag and physical up with their original native owner. | Keep the existing source/export ownership; cancel only owned provisional placement. Preserve the still-current Viewer grab unless its compositor owner explicitly ends it. |
| Cancel after mutation | Fence both new acquisitions, revoke exact delivered owners and restore the retained source through its journal. Do not synthesize a new down or route an old up to a replacement window. | Same ownership rule; restore only the retained source and retire only the named Viewer/transaction. Missing cleanup proof keeps admission closed. |

Every continuation must retain the original absolute deadline, exact source
and Viewer incarnations, connection generation, geometry and physical-layout
identity. Late acknowledgements, replacement windows, source process death,
layout changes, helper loss, disconnect, emergency return and sleep must cancel
the named transaction without moving an unrelated window. Cursor topology
selection must not overtake an older press's release decision.

The [owned native probe](native-drag-probe.md) can gather evidence about a
targeted-up submission to a fixture-owned AppKit drag. It always reports
`serverReleaseProven:false` and `productionDragAuthorized:false`. Stable frames
with later physical pointer movement are an observation, not a documented
transfer primitive. Production handoff stays unavailable until the missing
native contract is established and tested in both directions.
