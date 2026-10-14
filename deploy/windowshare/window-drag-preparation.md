<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Ordinary drag preparation

The Core can now prepare a provisional remote window from the exact original
titlebar press and configured Deskflow edge. This is the preparation portion of
P5. Continuous cursor/drag cutover, remote placement/drop and ordinary drag-back
are still implementation work. A painted preview is not a completed transfer.

The user deferred physical testing on 2026-10-14. Automated tests must use fake
native callbacks or the offscreen Qt platform. Do not launch a physical fixture
or enable input routing while the user is away.

## Production path

1. The existing sole raw consumer joins the annotated press to the Server's
   configured boundary. `LocalWindowDragCore` accepts only a fresh candidate,
   live capture/press/Core TLS continuity, verified companion code admission and
   the immutable explicit Core-pin → WindowShare-pin policy.
2. Private protocol **v7** negotiates `localInputWindowDragCapability` (16), which
   requires pointer observation capability (8). Message **20** is exactly **208
   bytes**, Core → Host only. Begin, Motion, Hold and Cancel have independent
   replay state; none prepares an input lease or advances input capture counters.
3. The Host matches the prior joined down, both locally configured pins and its
   independently observed current WindowShare TLS connection. The native binding
   uses that Host connection incarnation. Names, addresses and the remote peer's
   assertions do not establish the pairing.
4. Bridge also requires negotiated `window-drag-preview-v1`. NativeHost performs
   its existing exact AX/native window, process-birth, titlebar, movement and
   physical-display checks before starting capture. The Viewer paints with
   `WA_ShowWithoutActivating`, `WindowDoesNotAcceptFocus`, and no local input
   admission until an explicit committed acknowledgement.
5. Core checks retained continuity every 25 ms and sends a Hold at most every
   100 ms. A Hold cannot change coordinates or the original sequence. Host
   silence of 350 ms cancels; total preparation is limited to 30 seconds. A
   stationary press may retain an already-started preview, but an expired motion
   cannot start a new one. Later real motion retains the same boundary identity.
6. Real up, another press, missing pointer identity, Escape, capture loss,
   topology/edge change, either TLS loss, private-channel loss, admission loss,
   cancellation or shutdown cancels the exact preparation. Native callbacks are
   rechecked after returning, including callbacks that stop/destroy the owner.
   Cleanup also covers a late-created preview returned after cancellation.

`NativeHost::observeWindowPointer` no longer automatically turns a matching up
into `releaseWindowDrag`. Observing that up before the source application's
native grab has finished cannot establish cutover. The explicit release API and
painted-geometry transaction remain available for the future cutover owner.
Current production preparation always cancels on up; it does not relocate the
source, suppress input, synthesize a button release or switch the active Core peer.

Motion metadata reaches the Host coordinator, but the production Bridge has no
preview-placement callback yet. In particular, Qt global positioning is not a
replacement for an owned GNOME compositor placement receipt on Wayland.

## Automated verification

Build `windowshare-drag-coordinator-tests`, `windowshare-local-input-tests`,
`windowshare-window-drag-edge-tests`, `windowshare-companion-tests`,
`windowshare-viewer-tests`, `windowshare-bridge-tests` and `windowshare-native-tests`.
Run the corresponding CTests from `build/mac-default/src/unittests`.
Viewer/Bridge tests must use `QT_QPA_PLATFORM=offscreen`.

The coordinator suite launches a private child process and exercises the actual
authenticated socketpair and sender/receiver coordinators with fake native
callbacks. It covers wrong policy, missing press, denied code admission,
missing/replaced/revoked connections, native denial/throw/slow return/destruction,
raw press/capture/topology loss, private disconnect, stop, stationary holds and
Host timeout while the Core event loop is blocked. No desktop event is posted.

The wire suite independently injects malformed, truncated, oversized, stale,
wrong-role and unsupported-capability messages. It verifies signed coordinates,
the fixed byte layout, exact cancellation, Hold immutability and callback deletion.
The Viewer suite checks painted readiness without focus or input, explicit commit
transition, and immutable drag identity. Bridge tests check actual negotiated
TLS connection matching and propagation of native denial.

## Remaining implementation and deferred acceptance

- [ ] Native ordinary-titlebar eligibility acceptance on real macOS apps.
- [ ] Cutover owner with an actual native-grab completion/ownership boundary,
      virtual pointer motion, cancellation and exactly one real button-up owner.
- [ ] Bind the existing Host motion callback to provisional network placement;
      obtain exact owned GNOME MetaWindow placement and painted readiness.
- [ ] Permit drop only with that cutover evidence, current peer/layout and
      current produced/painted geometry; retain durable source recovery.
- [ ] Detect the exact owned Viewer ordinary move grab and implement drag-back,
      source position/focus restoration and remote-session retirement.
- [ ] Connected fault tests at each new cutover/drop/return transition.
- [ ] H07 physical Mac → Ubuntu → Mac, cancellation, reconnect, output changes,
      delayed first frame and held-button behavior. Record source/binary hashes
      and original down/up counts. Automated preparation tests do not satisfy H07.
