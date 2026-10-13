<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Owned native window drag observation

`windowshare-native-drag-probe` investigates a prerequisite for natural
cross-desktop window dragging: whether a targeted mouse-up actually stops
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
