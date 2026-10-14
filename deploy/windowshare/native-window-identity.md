<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Exact Mac window identity

The experimental Host joins an Accessibility window to its native `CGWindowID`
with `_AXUIElementGetWindow`, resolved only from the system HIServices framework.
This is a **private SPI**, not a supported public Apple API. The declaration is
also used by [Rectangle](https://github.com/rxhanson/Rectangle/blob/main/Rectangle/Rectangle-Bridging-Header.h).
The lookup is tested on macOS 26.1; future OS releases still require validation.

Initial export and journal recovery use this lookup. The Host no longer chooses
a window because its title or geometry resembles the selected capture. The
query requires one matching native ID in the exact process's AX window list,
checks candidate process IDs and the selected object's window role, and revalidates it after
enumeration. Missing APIs, unreadable/incomplete lists, duplicates, timeouts and
stale identities fail closed. Recovery preserves unresolved journal records.
The Host rechecks the native process birth identity after AX IPC. Permission
diagnostics expose `exactWindowIdentity` and disclose the SPI dependency.

Queries run on the control thread, never inside the input tap. The initial AX
list request has a one-second messaging timeout. Enumeration is limited to
256 windows and a separate one-second deadline, with 100 ms per-element
messaging timeouts. This is not a guarantee about third-party API scheduling.

The join establishes which native window contains an AX element. It does not
prove an active native drag, transfer WindowServer ownership, establish input
focus, or authorize remote child-window rendering. Those gates and roadmap
items remain separate. An attached sheet may be part of its parent's native
window rather than a separately capturable child surface.

## Native regression

Build and run explicitly on the actual Mac with existing Accessibility
permission:

```sh
cmake --build build/mac-default --target windowshare-native-identity-tests
build/mac-default/bin/windowshare-native-identity-tests --native
```

This creates two owned AppKit test windows with identical titles and frames,
which the previous geometry/title matching could not distinguish. The parent
invokes the same lookup used by the Host and checks:

- Each window resolves to its own independently reported native ID.
- A title change preserves the retained AX object's identity.
- Closing and replacing a window with the same title/frame does not revive
  the old identity or change the retained stale object into its replacement.

The fixture exits on its private control pipe closing or within 15 seconds.
The parent checks that its child exited normally and returns a structured
result. It never posts input, activates its application, captures the screen,
or accesses unrelated applications. Missing permission/API returns 77 with
`NOT_RUN`; it is not a pass. Default automated recovery tests also exercise
malformed identity rejection without native window queries.

Retain the JSON, stderr, exit status, binary/source hashes and OS version.
This test is an identity regression, not H07 edge-drag or H08 app acceptance.

## Attached-sheet observation

`windowshare-native-identity-tests --native-child-probe` additionally asks its
owned child process to attach an AppKit sheet. It inspects only the retained
parent's direct AX children, compares `AXWindow` with that exact parent, and
compares the SPI result with the fixture's independently reported sheet window
number. It does not post input or activate the application. Windows are ordered
behind the current desktop windows.

On macOS 26.1, the fixture's `AXSheet` had its own distinct native window ID and
an exact attached-parent link. The result is recorded as an observation with
`childRenderingAuthorized:false`, not remote-dialog acceptance. It does not
establish behavior for other apps, drawers, menus, popovers or IME surfaces.
The production Host still returns an unsupported attached sheet's parent
locally. Capture geometry, painted acknowledgements, stacking, modal focus and
release ownership must be implemented before remote interaction can be enabled.
