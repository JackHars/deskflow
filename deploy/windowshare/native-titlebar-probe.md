<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Normal titlebar observation

`windowshare-native-titlebar-probe` observes an ordinary AppKit titlebar drag
through the production `OriginalInputCaptureSource`. It is a separate test from
the [owned release experiment](native-drag-probe.md), whose content view calls
`performWindowDragWithEvent:` explicitly. This probe posts and suppresses no
input and never invokes that drag API.

The production capture adapter now retains event-time position, both Quartz
window-under-pointer fields, native event number, mouse subtype and reported
source PID/state.
`OriginalPointerObservation` is local metadata beside the input record, bound
to its epoch, sequence and source timestamp. The existing completed-capture
queue and Session/Server copy conduit carry it; there is no additional raw
consumer. Private v7 retains v6's joined window observations separately from input
leases; the network input protocol is unchanged. Values are captured before legacy translation
can mutate the CGEvent. Checkpoints and keys cannot inherit pointer metadata.

Zero window IDs remain unavailable. These fields do not identify a native grab
owner, establish a titlebar hit or authenticate a physical device. The optional
observation cannot authorize export, input redirection or a synthetic release.

## Physical procedure

Build the target, then obtain fresh readiness for this specific 90-second test.
Existing Input Monitoring permission is required; the probe never prompts for
permission. Freeze the source and binary hashes before launch.

```sh
cmake --build build/mac-default --target windowshare-native-titlebar-probe
build/mac-default/bin/windowshare-native-titlebar-probe --interactive > titlebar.jsonl 2> titlebar.stderr
```

Drag the **top title bar** of **Deskflow normal titlebar test - drag this title
bar** for about three seconds, then release. The test only observes; there is
no synthetic stop marker. Its content area does not initiate a drag. It closes
after release or its fixed 90-second deadline. Separate watchdogs exit after
a five-second cleanup grace even if a run loop stalls. The default invocation prints usage and exits
without a window or tap; unattended CTest does not run the interactive mode.

One listen-only HID-head tap feeds the real capture adapter. One main-thread
consumer drains its bounded queue. The observer queries only its own retained
native window ID and PID, at 50 Hz, and uses that window's actual titlebar
geometry to distinguish the test press from content/control clicks. This known
fixture geometry is not an arbitrary-application titlebar detector.

The separate `--interactive-annotated` mode uses one listen-only head tap at
`kCGAnnotatedSessionEventTap` instead. The installed SDK and
[Apple's documentation](https://developer.apple.com/documentation/coregraphics/cgeventtaplocation/cgannotatedsessioneventtap)
describe this as the stage where events have been annotated to flow to an application.
This research mode tests whether native window fields become available there;
it does not move production Core's HID tap or add a second raw consumer. Obtain
fresh readiness for this separate run. Preserve the reported `observationTap`
with its result: an annotated-stage observation cannot be claimed as HID
capture continuity or a correlation between the two stages. The fixed deadline
still closes the window if WindowServer consumes drag/release events before
they reach this later tap.

`--interactive-joined` keeps the production HID capture and adds a passive
annotation-only tap for left down/up. The same sole consumer correlates both
observations using source time, event number, direction and the remaining
scalar fields. Its 32 slots and absolute 50ms deadline cannot delay ordinary
input. Duplicate, inconsistent or overflowing metadata revokes optional window
identity; late/missing annotations cannot manufacture an ID or revive it.
`pointer-window-join` logs the raw stamp and the independently annotated ID.
The owned physical run on 2026-10-14 passes both joins, 213 drags and the real
release (see `TESTING.md`); no input is posted or suppressed.

`--interactive-eligible` additionally runs `NativeTitlebarObservation`, the
read-only observer used by NativeHost. It launches one separately owned source
application with a normal AppKit titlebar. The source reports only its own ID,
PID and titlebar height, and has an independent 95-second watchdog. The observer
uses the joined event-time ID to retain an exact AX window/process birth, hit
test the native titlebar, exclude controls/content, and observe movement with
unchanged size. Fixture geometry is used only to grade the test, not as the
production eligibility decision. `ready.sourcePid` identifies the source;
`ready.pid` identifies the observer. `native-titlebar-candidate`,
`native-titlebar-moved` and `complete.eligibleSourceDrag` record the result.
On completion, the observer terminates only its own fixture and reports
`owned-fixture-exit`. Existing Accessibility permission is also required.
The owned physical run `20261014T194753Z` passes this check; obtain fresh readiness
before another launch. The harness waits at most one second for its exact owned
WindowServer frame before installing either tap, and reports startup failures
by stage. This addresses a fixture-publication race without changing production
identity admission.

The first eligible drags exposed a separate production observation bug:
WindowServer and AX reported different origins during movement of the same
native window. The diagnostic run retained AX `400,138` and CG `402,143`, both
`640x368`. Subsequent observation rechecks the retained AX object, native ID and
process birth, requires both native sizes to match the original extent, and
uses WindowServer's current position. It does not compare independently sampled
origins during movement. Initial exact titlebar hit/frame admission remains
unchanged, and identity loss, either resize, invalid geometry and minimization
still retire the observer permanently. The physical repeat records 201 drags,
131 frame changes, exact joined down/up on window 10482 and real release after
3.472907914 seconds, with owned cleanup and no posted/suppressed input.

The source preparation/drop gate and authenticated configured-edge preparation
controller exist. Native drag/cursor cutover, remote preview motion/drop and
GNOME drag-back are not wired. A painted preview, a timer, a source frame query or an eligibility
observation is not a native grab-completion receipt. Keep P5/H07 open until
continuous transfer and return are implemented and tested.

Preserve stdout, stderr, exit status, OS version, source/binary hashes and the
user's observation. `titlebar-down` reports both event-time native IDs and
whether they equal the owned window. Missing IDs must not be filled from the
fixture, foreground app or a later hit test. `owned-frame` records independent
frame changes, and `physical-up` retains the later unmarked input stamp.
Other input, a second down, source loss or a deadline ends the observation.
On capture loss, `capture-rejection` preserves the first rejecting event's
scalar fields and the retained epoch's reason. This diagnostic performs no
logging or native query in the tap, stores no typed text and never restarts
the failed capture stream. A normal process exit alone is not a successful test.

## Acceptance boundary

Constructed-event regressions cover metadata copying, missing/invalid fields,
synthetic filtering, mutation after capture, exact stamp matching and queue
lifetime. They do not establish what a physical titlebar drag supplies.

The first two physical attempts on 2026-10-14 failed before recording a
titlebar-down. The first (`owned-titlebar-physical-20261014T165243Z`) reported
only `capture-revoked`. The diagnostic repeat (`20261014T165717Z`) identifies
the first event as a valid touch-origin mouse move (type 5, subtype 3, flags 0,
zero deltas). The old adapter rejected every nondefault mouse subtype, so an
ordinary trackpad move revoked capture before the drag could begin. These
failures remain recorded under `build/roadmap-completion-20261014/`.

The adapter now accepts the documented touch-mouse subtype as mouse-button and
movement semantics, while retaining its subtype in local observations. Tablet
and unknown subtypes still fail. This does not implement raw touch contacts,
pressure or gestures. The recorded mouse sample reproduces the old rejection
in a constructed-only regression.

The next fresh run (`owned-titlebar-physical-20261014T170310Z`) records the
ordinary titlebar down, 244 drag events and 164 independent owned-frame changes.
It then rejects the native left-up: touch-mouse subtype 3, click count **0**.
That run remains failed at physical release. Both event-time window IDs at its
down are **zero**, despite the known fixture being dragged. HID-head window
fields alone cannot identify an arbitrary source on this tested Mac.

The release correction permits zero only on up and preserves it through the
portable record, local IPC and native receiver preparation. It does not copy
the down's count onto the captured up. The receiver still requires the exact
retained button press, route, capture epoch and forward sequence/time; nonzero
counts must match the press. Regressions cover capture continuity, private
event reconstruction, source transition evidence, press ownership and IPC.
The fresh physical repeat with both corrections
(`owned-titlebar-physical-20261014T171313Z`) passes: normal titlebar down, 226
drag events, 150 independent frame changes and the real release 4.022145439
seconds later. It completes with `physical-release`, no capture rejection,
exit 0 and no surviving probe process. No input is posted or suppressed.
Source/binary hashes are bound in `titlebar-zero-up-evidence.json`; the run's
`analysis.json` verifies raw evidence hashes and ordering. Both event-time
window IDs remain zero, so only owned normal-titlebar capture passes here.

The separately selected annotated-stage run
(`owned-titlebar-annotated-physical-20261014T171931Z`) supplies both native IDs
as **10359**, equal to the independently retained owned window ID. It records
167 drags, 114 frame changes and the real zero-click/subtype-3 up 3.078813456
seconds later. Down/up event number is 515; completion is `physical-release`,
exit 0, with no capture rejection or surviving process. No input is posted or
suppressed. `titlebar-annotated-evidence.json` freezes the tested source/binary;
the run's `analysis.json` verifies raw hashes and exact owned-window equality.
This confirms a usable event-time window-ID source in this fixture at the
later stage. A separate single-tap run does not prove same-event correlation
with HID capture. Preserve that distinction when implementing the bounded
join to the original HID press; do not move production capture to this stage
or infer native grab ownership from the window-under-pointer fields alone.

P5/H07 remains open. Even matching native IDs plus observed frame movement
would be evidence for further candidate detection work, not a native-grab
completion or cross-machine transfer receipt.
