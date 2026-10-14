<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Owned native window drag observation

`windowshare-native-drag-probe` investigates a prerequisite for natural
cross-desktop window dragging: whether a submitted mouse-up actually stops
WindowServer's native window movement. Receiving a mouse-up in an application
does not establish that fact. This tool cannot authorize a production handoff.

The default invocation runs deterministic accounting tests only. It creates no
application window, event tap, capture session or input event:

```sh
cmake --build build/app --target windowshare-native-drag-probe
build/app/windowshare-native-drag-probe
```

Those paths describe the standalone application build. In the repository's
integrated build, use `cmake --build build/mac-default --target
windowshare-native-drag-probe` and `build/mac-default/bin/windowshare-native-drag-probe`.

Interactive use requires a ready user at an unlocked Mac, existing Accessibility
and Input Monitoring permission, and the supported targeted-pointer API. The
probe checks those permissions without prompting or changing them. Start it
only after the user agrees to this specific physical test:

```sh
build/app/windowshare-native-drag-probe --interactive > owned-drag-probe.jsonl 2> owned-drag-probe.stderr
```

In **Deskflow owned WindowServer drag probe**, hold the left button in the blue
area and move the window. Keep holding and moving after the targeted-up marker,
then physically release. The tool observes pointer motion through a bounded
listen-only tap. After real movement and at least 500 ms, it submits at most
one synthetic up to its own retained PID/window using the production event
constructor. It does not inject a down, suppress physical input, target another
application, export a window or start ordinary Deskflow. A 30-second watchdog
ends even a hung tracking loop. Closing the owned window also ends the test.
The probe also closes immediately after the physical release; keep holding and
moving for several seconds after the title changes before releasing.

On 2026-10-14 the user ran this probe twice on macOS 26.1. Both runs received
the targeted app-up and then observed further native window movement before
the real release. They recorded 41/74 later pointer motions and 33/62 later
frame observations respectively. The targeted PID post did **not** end the
WindowServer drag. Evidence is under
`build/roadmap-completion-20261014/owned-drag-physical-{1,2}/`.
Neither run authorizes production handoff or completes H07.

## Separate system-stream experiment

`--interactive-hid` is a different, explicitly selected experiment. It uses
`CGEventPost(kCGHIDEventTap, ...)` to send one marked mouse-up into the system
input stream. Apple's [tap-location documentation](https://developer.apple.com/documentation/coregraphics/cgeventtaplocation/cghideventtap)
places this entry point at WindowServer. This is a global-stream operation,
not a PID-targeted post or a production Host fallback. No natural-drag support
is enabled by building or running it.

Obtain fresh readiness for this specific test before launching it. The window
is titled **Deskflow owned system-release drag probe**. Keep other keys/buttons
released, drag in its blue area, then keep holding and moving for three seconds
after **System up submitted** before physically releasing. It closes on that
real release or after the same 30-second watchdog.

```sh
build/mac-default/bin/windowshare-native-drag-probe --interactive-hid > owned-hid-drag.jsonl 2> owned-hid-drag.stderr
```

Before the one allowed submission, the probe requires its own active, visible,
key window, the original live native ID, the current pointer within that
window, and only the physical left button held with all keyboard keys released.
Other observed keys/buttons, observation loss, an early release or an expired
deadline cancel the experiment. It never posts a down, suppresses real input,
or changes another application's window. The event uses a private source and
the current pointer position; no cursor warp or new drag is requested.

Output names `postRoute` (`targeted-pid` or `hid-windowserver`) and separately
records whether the marked up was observed in the session tap. The original
down's pointer-window fields are diagnostic samples, not grab-ownership
certificates. Neither marked-event observation nor an app-up receipt proves
that WindowServer released the grab. `serverReleaseProven` and
`productionDragAuthorized` remain false in every mode.

Retain stdout, stderr, exit status, binary/source hashes, macOS version and the
user's observation. The output distinguishes an app-up receipt, later physical
motion and independent native frame movement. A frame still moving after the
submission demonstrates that this submission did not stop that native drag.
Stable frames during later physical motion remain an observation, not a
documented grab-release acknowledgement or a transferable permission. Physical
release, stale callbacks, missing motion, identity loss and timeout cannot be
reported as a successful synthetic release.

Natural arbitrary-application edge drag remains unavailable until exact native
drag ownership, cancellation/transfer and a single button-up destination can
be established on both platforms. Ordinary Viewer open/close is not this test
and does not satisfy H07.
