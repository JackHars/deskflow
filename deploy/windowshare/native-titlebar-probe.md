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
window-under-pointer fields, native event number and reported source PID/state.
`OriginalPointerObservation` is local metadata beside the input record, bound
to its epoch, sequence and source timestamp. The existing completed-capture
queue and Session/Server copy conduit carry it; there is no additional raw
consumer or wire-protocol change. Values are captured before legacy translation
can mutate the CGEvent. Checkpoints and keys cannot inherit pointer metadata.

Zero window IDs remain unavailable. These fields do not identify a native grab
owner, establish a titlebar hit or authenticate a physical device. The optional
observation cannot authorize export, input redirection or a synthetic release.

## Physical procedure

Build the target, then obtain fresh readiness for this specific 30-second test.
Existing Input Monitoring permission is required; the probe never prompts for
permission. Freeze the source and binary hashes before launch.

```sh
cmake --build build/mac-default --target windowshare-native-titlebar-probe
build/mac-default/bin/windowshare-native-titlebar-probe --interactive > titlebar.jsonl 2> titlebar.stderr
```

Drag the **top title bar** of **Deskflow normal titlebar test - drag this title
bar** for about three seconds, then release. The test only observes; there is
no synthetic stop marker. Its content area does not initiate a drag. It closes
after release or its fixed 30-second deadline. A separate watchdog exits even
if the main run loop stalls. The default invocation prints usage and exits
without a window or tap; unattended CTest does not run the interactive mode.

One listen-only HID-head tap feeds the real capture adapter. One main-thread
consumer drains its bounded queue. The observer queries only its own retained
native window ID and PID, at 50 Hz, and uses that window's actual titlebar
geometry to distinguish the test press from content/control clicks. This known
fixture geometry is not an arbitrary-application titlebar detector.

Preserve stdout, stderr, exit status, OS version, source/binary hashes and the
user's observation. `titlebar-down` reports both event-time native IDs and
whether they equal the owned window. Missing IDs must not be filled from the
fixture, foreground app or a later hit test. `owned-frame` records independent
frame changes, and `physical-up` retains the later unmarked input stamp.
Other input, a second down, source loss or a deadline ends the observation.

## Acceptance boundary

Constructed-event regressions cover metadata copying, missing/invalid fields,
synthetic filtering, mutation after capture, exact stamp matching and queue
lifetime. They do not establish what a physical titlebar drag supplies.

The physical normal-titlebar run is **NOT_RUN** as of this implementation
checkpoint; a readiness request is pending. P5/H07 remains open. Even matching
native IDs plus observed frame movement would be evidence for further candidate
detection work, not a native-grab completion or cross-machine transfer receipt.
