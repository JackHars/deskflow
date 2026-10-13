<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Bounded viewer media recovery and hidden-window lifecycle

## Implemented runtime behavior

The optional authenticated `media-recovery-v1` capability permits a Viewer to
recover from its bounded decoder queue filling without returning the source
window immediately. Legacy peers retain their existing fail-closed overflow
behavior. Media controls are not native input authority.

An active overflow releases the exact delivered inputs, suppresses new input,
invalidates the old decoder generation, and drains already-accounted worker
completions. Only then does it send one recovery request bound to the existing
TLS connection, session, epoch and geometry. The host rejects stale/replayed
requests and injected fields and schedules a keyframe through its bounded
per-session backpressure path. The decoder accepts an ordered SPS/PPS/IDR access
unit, resets on its worker thread, and keeps input blocked until that revision
has actually painted. There is one nonrenewable five-second recovery deadline;
a second overload or deadline failure returns the source instead of retrying
indefinitely. Recovery does not silently reacquire keyboard focus.

After this lifecycle change, hiding/minimizing a committed negotiated Viewer
also invalidates queued decoding, releases held input and drops subsequent
video without admitting it to FFmpeg. A hidden viewer has no running recovery
deadline. Showing it again starts a new bounded drain/keyframe transaction for
its current geometry. Repeated visibility events cannot duplicate an active
request. An old geometry's frame cannot finish recovery or reopen input.

## Reproducible checks (C06/C09; not physical acceptance)

On a configured application build:

```sh
cmake --build build/app --parallel 3
QT_QPA_PLATFORM=offscreen ctest --test-dir build/app \
  -R '^(windowshare-viewer|windowshare-bridge)$' --output-on-failure
```

`ViewerTests` exercises overload, held-key cleanup, a single drain-triggered
request, dependent-frame refusal, recovery paint, fresh-focus intent, hidden
bursts, hidden geometry changes and visibility restoration. `BridgeTests`
exercises negotiated controls over actual TLS, exact native-stream selection,
replay/foreign-owner rejection and malformed-field connection teardown.

Local portable foundations on the source snapshot for this change:
GCC 14.2 and Clang sanitizer builds, `WINDOWSHARE_SANITIZERS=ON`, each passed
11/11 CTest suites with ASan leak detection and UBSan halt-on-error enabled.
Those suites validate portable contracts, not Qt visibility or native painting.
The new Qt visibility cases require the matching hosted application CI result;
no native result is claimed merely from the portable suite.

Physical H06/H11: **NOT_RUN**. No measurements of hidden-source rendering,
GPU load, physical monitor scaling, end-to-end latency or hour-long soak have
been made here. Use `resource-soak.md` for passive resource evidence and the
root `TESTING.md` procedures for the separate physical verdicts. This is a P7
lifecycle/backpressure slice, not completion of the full P1-P8 handoff.
