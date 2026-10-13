<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Deskflow Seamless Windows: cloud implementation handoff

Prepared 2026-10-12. Read this file first, then `TESTING.md`.

**Local work resumed on 2026-10-13.** This document retains the cloud assignment
and roadmap; the newest entries in `WORK_STATE.md` and `TESTING.md` identify the
current implementation and evidence. The cloud has implemented private protocol
v4 with explicit full-input scope and asynchronous Server preparation. Neither
those contracts nor the GNOME/Qt observations enable original-input delivery.

## Assignment and division of work

Complete the remaining Seamless Windows code in a **cloud coding environment**.
You will not have the user's Mac, Ubuntu desktop, WiFiman tunnel, physical input,
monitors, SSH key, private certificates or installed experimental applications.
That is an expected limitation, not a reason to stop implementing the roadmap.

The user will return the resulting branch and `TESTING.md` to the original local
chat. That chat will build/install the final revision on the real machines and
perform hardware verification. Local work was paused during the original
handoff and has since resumed. Cloud runs must not create automations or attempt
to control the user's machines.

Your deliverables are:

1. Integrated production code for the remaining roadmap, in reviewable commits.
2. Meaningful automated regressions and any reproducible test harnesses needed
   by the local verifier. Run the subset supported by the cloud environment.
3. A maintained root **`TESTING.md`** containing exact commands, setup, expected
   observations, cleanup, evidence locations, known limitations and results.
4. Updated `WORK_STATE.md` and `docs/seamless-windows.md`, distinguishing code
   implemented, cloud checks passed, native headless checks passed, and physical
   acceptance still pending.
5. A final implementation commit SHA/branch and an explicit list of anything
   that could not actually be implemented or compiled.

Do not end after writing another plan or adding disconnected abstractions.
Connect the real runtime paths. Do not mark an unrun test as passed or claim the
whole product is accepted merely because the code compiles. When native testing
is unavailable, implement the code and executable test procedure, record
`NOT_RUN — requires local hardware` in `TESTING.md`, and continue other work.
If an OS API truly cannot support a requirement, record the precise unsupported
capability and evidence, deny that path at runtime, and finish independent work.
Do not substitute a success-returning stub for a missing platform capability.

## Repository and starting point

- Repository: `https://github.com/JackHars/deskflow`
- Branch: **`feature/seamless-windows`**. Continue this branch or a clearly named
  descendant if your cloud workflow requires one. Never modify `master` or
  `main`, force-push, or merge into the default branch for this assignment.
- Latest implementation base: **`ef51456659edae1acf06a6c03b730bcaf66f2f8b`**.
  The handoff/documentation commit may be newer; start from the branch tip.
- Prior implementation: `eda21b39b19004af9f4abfaacb707bb9b380f6f8` adds Server
  ownership before legacy dispatch; `ef5145665` fixes portable core linkage.
- Original local checkout, useful only to the returning local chat:
  `/Users/jackharsfai/tweakapp/deskflow`.
- `build/`, `deps/` and `.venv/` contain local/ignored material. They are not
  prerequisites available in a fresh cloud clone. Use the tracked source,
  workflow dependency lists and test scripts to reproduce checks.
- Read any applicable `AGENTS.md` in your checkout. Inspect status before edits
  and preserve unrelated changes. Follow existing formatting and REUSE headers.

At handoff the project is approximately **50% complete overall**. This is an
engineering estimate, not a count of passing tests. The capture/viewer and many
input primitives work; the production original-input controller, seamless edge
drag and broader acceptance are unfinished.

## Verified baseline — historical, not results for your new code

The final baseline code `ef5145665` passes all six jobs in
[CI run 38323926279](https://github.com/JackHars/deskflow/actions/runs/38323926279):
GCC and Clang sanitizer foundations, transport, Linux application/core, Mac
application/core, and native headless GNOME integration.

The same source built on the user's Mac and home Ubuntu. Five core suites pass
161 Qt entries on each, including Qt setup/teardown entries. Ubuntu additionally
passes three original-input/route suites. A separate actual Ubuntu core build
with `BUILD_WINDOWSHARE=OFF` passes three compatibility suites: 30 Qt entries,
zero failures and 29 intentional skips of disabled trace cases. Nine targeted
Mac suites and five sanitizer suites passed before the linkage-only follow-up;
the actual Mac core and five core suites passed again afterward. Scoped Server
sanitizers instrument changed code, not every prebuilt dependency archive.

Earlier actual Mac-capture → private Ubuntu GNOME/Qt → Mac native-receiver
synthetic tests passed at Qt scale factors 1 and 1.25. These were not physical
keyboard/trackpad tests, physical compositor monitor-scaling tests, holding-
display interaction tests, or two-monitor/edge-drag acceptance.

The user confirmed the disposable Mac test window was visible and typed `ok`.
That confirms local window visibility only. A complete physical input round trip
has **not** passed. One physical Ubuntu monitor is currently active. Local
experimental Mac package updating awaits the previously requested macOS App
Management permission; fresh user readiness is required before another physical
input session. These are local verification tasks, not cloud coding blockers.

For the returning local chat, baseline evidence is retained under
`build/pre-dispatch-20261012/`, especially `mac/`, `mac/linkage-followup/` and
`ubuntu/candidate-2/`. Earlier cross-machine evidence is indexed in
`WORK_STATE.md`. Do not require these ignored files in cloud tests.

## Read in this order

1. This file and `TESTING.md`.
2. The top of `WORK_STATE.md` and roadmap/current behavior in
   `docs/seamless-windows.md`.
3. `deploy/windowshare/original-input-dispatch.md`,
   `deploy/windowshare/forwarding-fence.md` and
   `deploy/windowshare/input-sequence-trace.md`.
4. `deploy/windowshare/gnome-provider/README.md` and its `interface.xml`.
5. `docs/seamless-windows-coordinate-plan.md` for the X11 witness design.
6. Physical/cross-machine procedures under `deploy/windowshare/`.

Older sections describe earlier states. In particular, the coordinate plan's
claim that Server pre-dispatch ownership is missing predates `eda21b39b`; the
current Server implementation and full-input forwarding gate already exist.
Use current code and the newest work-state entry to resolve historical wording.

## Code map and current boundaries

| Area | Main files | Current state |
| --- | --- | --- |
| Server ownership | `src/lib/server/Server.{h,cpp}`, `src/lib/windowshare/InputRouteState.h`, `src/unittests/server/ServerTests.cpp` | Bounded pre-dispatch queue, exact owner ledger and trusted dispatcher interface exist. |
| Production registration | `src/lib/deskflow/ServerApp.cpp` | Registers routes and capture inbox on Mac; does **not** install `OriginalInputDispatcher`. |
| Capture | `src/lib/platform/OriginalInputCaptureSource_mac.mm`, `src/lib/windowshare/OriginalInput.{h,cpp}`, `OriginalInputCapturedInbox.h`, `OriginalInputRoute.{h,cpp}`, `LocalInputSession.cpp` | One raw consumer; separate completed-capture copies reach Server. `onRoutedInput` is historical observation, not a delivery callback. |
| Private Mac handoff | `src/lib/windowshare/LocalInputChannel.{h,cpp}`, `LocalInputPreflight.{h,cpp}`, `LocalInputSession.{h,cpp}` | Version 4 explicitly retains keyboard or full-input scope; the production coordinator remains absent. |
| Host/viewer | `src/apps/windowshare/main.cpp` (`Bridge`), `NativeHost_mac.mm`, `Viewer.cpp`, `KeyboardRouteProtocol.cpp`; `src/lib/windowshare/KeyboardRoute.h` | Negotiated `input-route-v1` gates key/button/move/scroll forwarding. It is not a native queue drain or permission to post original input. |
| GNOME | `src/lib/platform/EiComputer.{h,cpp}`, `src/lib/windowshare/GnomeProviderClient.{h,cpp}`, `GnomeViewportProtocol.{h,cpp}`, `deploy/windowshare/gnome-provider/` | Owns an EIS/viewer lifetime and joins native hit observations to last-painted Qt geometry. No original-delivery permit. |
| Qt native viewport | `src/apps/windowshare/WaylandViewport.{h,cpp}`, `ViewerViewportEndpoint.{h,cpp}` | Actual QtWayland surface and private inherited observation channel. Preserve exact-version/private-API checks. |
| X11 coordinates | `src/lib/windowshare/X11DisplayObserver.{h,cpp}`, `ViewportLease.{h,cpp}`, `src/lib/platform/XWindowsComputer.cpp` | Observation/geometry foundations exist; cross-connection namespace witness and production authority remain to be connected. |
| Holding/restore/media | `src/apps/windowshare/HoldingDisplay*`, `NativeHost_mac.mm` (capture/encoding), `VideoDecoder*`, `src/lib/windowshare/Core.{h,cpp}` | Holding helper, capture, H.264, journal and normal restoration exist. Full lifecycle/compatibility/performance work remains. |
| Build/tests | `src/apps/windowshare/CMakeLists.txt`, `src/unittests/windowshare/`, `.github/workflows/windowshare-foundation.yml` | Portable original-input code is in `windowshare-original-input`; Server links it when enabled. Preserve ON and OFF core builds. |

## Invariants to preserve while integrating

- Ordinary Deskflow remains the default. Window sharing is explicit opt-in and
  must not change the user's ordinary installation, settings or trust store.
- Original input is authorized by exact retained session, connection, selection,
  focus, geometry and native-lifetime evidence. Names, PIDs supplied by a peer,
  equal dimensions, current cursor position and matching timestamps are not
  substitutes for that evidence.
- A backend submission receipt, EIS PONG, forwarding acknowledgment, native
  dispatch receipt, Qt consumption and Mac application receipt are different
  facts. Model and report them separately.
- The Mac event tap never waits. Server work runs on the core event thread;
  private IPC/preflight and Viewer operations run on their Qt owner thread;
  GNOME D-Bus work uses its owned context. Exchange bounded immutable values and
  revocable lifetimes; no borrowed proxy/native pointers across threads.
- Keep `LocalInputCoreSession` the only raw capture consumer. Use the existing
  completed-capture inbox; do not add a second consumer to the raw SPSC stream.
- A press chooses one owner. Repeats, drag and release retain that owner. Once
  an original reservation is accepted, failure cannot create a legacy down or
  retarget cleanup to a newly focused window. Retain suppressed presses through
  physical ups, including untranslated ups.
- Preserve current bounded behavior: 256 pending/captured records, 100 ms
  capture wait, bounded owner history and the shared 1,024-press cap. If new
  asynchronous preparation needs a different budget, make it explicit, bounded
  and tested; do not silently extend existing deadlines.
- Revoke first, release the exact owner, then resume ordinary forwarding only
  after the required drain. Emergency handling remains independent of capture
  overflow or diagnostic-join failure.
- Do not weaken authentication, code admission, private framing or native
  capability checks to obtain a green test. No new untrusted raw-native-input
  endpoint is needed. Keep the full route disabled when its prerequisites fail.

## Implementation sequence

Implement connected vertical slices in the order below. Add/update the matching
`TESTING.md` test IDs in each commit. Source locations are navigation hints;
verify the current interfaces instead of relying on old line numbers.

### P1 — Full-input private lease and asynchronous preparation

- [x] Carry full input scope through `LocalInputLease`, private framing,
  preflight, host reservation/activation and release. Protocol v4 has a 192-byte
  lease with explicit scope and strict version/capability decoding; old or
  mismatched scope cannot silently become a keyboard lease.
- [x] Preserve independent code-admission, delivery-ready and neutral-boundary
  checks. Activation requires physical neutrality; active use may retain that
  owner's held input. Release must be synchronous at the owner boundary or
  remain an explicit unfinished release with forwarding closed.
- [x] Add bounded asynchronous Server preparation with explicit pending,
  refused, accepted and revoked results, exact nonreused tickets and a separate
  nonrenewable 500 ms preparation budget. Actual native validation remains P2;
  an accepted test fixture is not a production permit.
- [x] Preserve source order, exact recipient/selection and output capacity
  through preparation. Ensure selection/disconnect/destruction cancels safely.
- [x] Retain the exact quiesced private lease during asynchronous preparation,
  using one queued continuation and the unchanged private deadline. The hook
  does not replace admission, readiness or neutrality checks and cannot extend
  the Server's earlier deadline. The actual native cutoff remains P2.

Acceptance in cloud: malformed/old-version/scope mismatch, missing providers,
late/duplicate replies, queue limits, timeout, callback reentry, destruction,
press ownership and ON/OFF compatibility tests. No production enablement from
fixture-only readiness. See tests C03/C04/C05 and later H03/H04.

### P2 — Native old-input drain and exact routed-point validation

- [ ] Introduce negotiated, bounded request/receipt types for an exact old-input
  cutoff and for `ValidateRoutedPoint`. Bind them to the actual authenticated
  Core connection, capture stamp, native session/device/layout lifetime, Viewer,
  full forwarding binding and one monotonic request deadline.
- [ ] Fence new legacy key/button/wheel acquisition, explicitly release old
  owners, and establish backend → compositor → Qt consumption ordering while
  the full forwarding gate remains closed. Keep explicitly permitted cursor
  presentation distinct from input authority.
- [ ] Complete authoritative validation of the **Server's frozen point and
  sequence** against the cutoff and exact binding. The implemented
  `requestRoutedPointObservation()` already accepts a frozen point and sequence
  and joins native/painted observations; it still supplies no delivery permit.
  Retain native ownership, hit/occlusion, geometry, focus and freshness at use.
- [ ] Add owned first-click activation with fresh focus/paint validation if the
  Viewer is not focused. Do not inject a duplicate Linux click to activate it.
- [ ] Complete the X11 namespace witness and hit validation described in the
  coordinate plan using the real injection and Qt connections. Preserve their
  event-queue owners; do not open a second display and treat it as equivalent.
- [ ] Keep unsupported compositor/API/device/layout states unavailable rather
  than using guessed coordinates or a generic Wayland fallback.

**GNOME/Qt research leads — proposals requiring validation, not proven barriers:**

1. An EI ping after the old prefix may establish one sender/native-thread
   boundary. Process the entire returned batch: PONG followed by disconnect in
   the same batch must not succeed. `EiComputer` now has a connection-time
   PONG diagnostic; it must become a request for the exact frozen prefix.
   PONG alone does not prove compositor dispatch. Verify the supported Mutter
   build's libeis event-reference ordering, not just sender API availability.
2. A provider operation may combine an empty Clutter input queue with a genuinely
   later completed stage update. A single `before-update`/`after-update` pair is
   insufficient: `before-update` is emitted after stage event draining, and that
   drain detaches its queue before callbacks. Request arrival during reentrant
   dispatch must be tested. A conservative additional same-view frame cycle is
   a candidate, not an established proof. Check required APIs at runtime.
3. After the provider boundary, bind the implemented two asynchronous
   `wl_display_sync` callbacks on Qt's **existing default input queue** to the
   exact full forwarding fence. Qt may schedule a deferred-focus sync while
   processing old input; nested dispatch may retain an older handler on stack.
   Drain QPA window-system events on the GUI thread with the full gate closed
   and revalidate before reply. `sendWindowSystemEvents`' boolean means whether
   it sent events, not proof of success. No blocking roundtrip/new connection.
4. Explicit releases must stop key-repeat ownership. Asynchronous IME work can
   outlive a Wayland fence; explicitly disable/reject unsupported Viewer input-
   method acceptance or implement and prove its drain as a separate boundary.

Primary sources to recheck for supported versions: Mutter 50.1
`src/backends/meta-eis.c`, `meta-eis-client.c`, RemoteDesktop session code and
`clutter/clutter/{clutter-main,clutter-stage,clutter-stage-view}.c` in
[GNOME/mutter](https://github.com/GNOME/mutter/tree/50.1); Qt 6.10.2 and 6.8.3
`qwaylanddisplay.cpp` in [qtwayland](https://github.com/qt/qtwayland) and
`qwindowsysteminterface.cpp` in [qtbase](https://github.com/qt/qtbase).

The existing authoritative association is owned `Meta.WaylandClient` → exact
single `MetaWindow` → sole native surface, plus the inherited private socket to
that Viewer. There is no provider-returned `wl_surface` resource ID compared
directly with Qt's `nativeSurface`, and no established public EIS-session-to-
Clutter-device association. Do not invent either. Retain the enforced single-
window/single-surface restriction until a real per-window association replaces
it. A current session close kills the owned Viewer; closing it is not a drain.

Acceptance: delayed native/Qt queues, focus sync, reentrancy, same-batch loss,
stale/wrong cutoff or point, overlapping/shaped windows, letterboxing, expired
geometry, output/device changes and unsupported capability regressions. Cloud
may run a private headless compositor if available, but must not require the
user's desktop. See C04–C07 and H03–H05.

### P3 — Connect and enable the production original-input route

- [ ] Implement the coordinator across Server, Session/Preflight, private host
  channel, Linux validator, Bridge and NativeHost using P1/P2's actual permits.
- [ ] Install `OriginalInputDispatcher` only for an explicitly configured route
  whose providers can supply all prerequisites. Registering the capture inbox
  alone must continue to preserve ordinary delivery.
- [ ] Complete key/modifier/repeat, button/drag, pointer, two-axis scroll and
  momentum handling using the original captured record and exact owner.
- [ ] Recheck current admission/focus/geometry/connection evidence on delivery
  and heartbeat. Release idle held owners on loss, retain physical-up tombstones,
  and preserve the raw emergency chord after continuity failure.
- [ ] Exercise the real protocol/adapter chain in automated integration tests;
  extend the disposable source/receiver harness so local verification can prove
  native receipts, no duplicates and no held input on the exact candidate.

Cloud completion means the production path is wired and has meaningful tests;
physical correctness remains pending H02–H04. Do not use the historical
`onRoutedInput` callback as the original delivery operation.

### P4 — Holding display, multiple windows and two monitors

- [ ] Complete first-painted-frame → holding-display relocation → return as one
  owned transaction; preserve the source if creation/capture/relocation fails.
- [ ] Replace single-viewer/single-surface assumptions with explicit per-window
  associations before enabling multiple exported windows in GNOME. Preserve
  exact focus, press, video, geometry and restore ownership per session.
- [ ] Support overlapping exported/local windows and focus transitions without
  raising or targeting an unrelated application.
- [ ] Handle monitor selection, negative coordinates, mixed scale, output
  removal/layout changes, helper crash and source/host loss. Keep the holding
  display excluded from ordinary Deskflow cursor boundaries.

Acceptance: simulated topology/geometry/lifecycle failures and session isolation
in cloud; actual hidden-source rendering, two monitors and mixed scale in H05/H06.
Qt `QT_SCALE_FACTOR` tests alone do not validate compositor monitor scaling.

### P5 — Bidirectional edge drag

- [ ] Detect an eligible source-window drag across a configured Deskflow edge
  and identify the exact destination from topology, without polling a guessed
  foreground window or converting ordinary pointer motion into export.
- [ ] Implement prepare/first-frame/ownership-transfer/commit or rollback, with
  continuous drag ownership and one button-up destination.
- [ ] Implement dragging the Viewer back to the Mac edge, restoring the source
  position/focus and retiring the remote session without closing the source app.
- [ ] Handle canceled drags, mismatched topology, disconnect, output changes,
  late first frames and timeout at every transition.

Acceptance: deterministic transaction/fault tests and harness support for H07.
Do not call ordinary opening/closing of a Viewer "edge-drag support."

### P6 — Application compatibility

- [ ] Associate dialogs/child windows with the correct exported application and
  session; handle creation/destruction and modal focus without identity guesses.
- [ ] Handle popup/context menus and owned transient surfaces under both Linux
  adapters; unsupported surfaces must not be routed to an unrelated window.
- [ ] Integrate clipboard with the actual exported-window focus and lifetime,
  preserving configured sharing policy and avoiding feedback loops.
- [ ] Complete modifier/shortcut mapping and key-repeat behavior. Preserve the
  fixed Mac mapping: Option = `alt`, Command = `super`; the old reversal is fixed.
- [ ] Add reproducible browser/Electron and native-app scenarios, including
  Unicode/composition behavior or clearly documented unsupported input methods.

Acceptance: association, clipboard-loop and key mapping regressions in cloud;
named applications and real shortcuts/dialogs/menus in H08/H09.

### P7 — Recovery and performance

- [ ] Add crash-watchdog recovery to the existing restore journal, preserving
  unresolved records and kernel process-birth identities across restart.
- [ ] Make teardown/recovery idempotent for disconnect, host/helper/viewer/core
  failure, sleep/wake, session deactivation and emergency return. Bound shutdown
  and retain diagnostics for hung owned children.
- [ ] Implement fair active-stream scheduling/backpressure and bounded media
  queues, including hidden/minimized viewers, resize and slow consumers.
- [ ] Add reproducible latency/resource/load measurement tools and define
  explicit acceptance targets before measuring. Do not invent performance claims.

Acceptance: fault injection and bounded-resource tests in cloud; destructive
owned-process cases, sleep/wake and actual performance/soak in H10/H11.

### P8 — Packaging and hand-back

- [ ] Keep the separate experimental Mac bundle and exact dependency/signing
  policy. Update packaging/tests for new runtime components and permissions.
- [ ] Supply reproducible Linux packaging, dependencies, startup/configuration
  and GNOME provider compatibility diagnostics. Do not silently enable a desktop
  extension or overwrite the ordinary Deskflow installation.
- [ ] Run available clean builds, ON/OFF compatibility and meaningful regression
  suites. Use existing CI for Mac compilation/packaging if access is available;
  otherwise record exactly what was not compiled.
- [ ] Finish `TESTING.md` with all implementation deltas, actual cloud results,
  exact candidate SHA, artifact instructions and local verification steps.
- [ ] Push ordinary commits to the authorized feature branch/descendant when
  credentials are available. Otherwise provide the commit/patch and explain how
  to transfer it. Leave no essential code only in ignored build directories.

The cloud task can finish with hardware acceptance pending. The complete product
cannot be declared finished until the local chat executes the applicable H tests
and resolves failures. Do not request a Mac/SSH session merely to finish cloud
implementation, and do not spend time trying to reach the user's home network.

## Suggested commit order and hand-back message

Use one coherent commit per connected slice: private full-input contract;
asynchronous preparation; Linux drain; exact point validation; production
coordinator; multi-window/monitor lifecycle; edge drag; compatibility;
recovery/performance; packaging/testing documentation. Adjust boundaries when
needed, but maintain buildable checkpoints and document limitations honestly.

Your final message should name the branch and final implementation SHA, link
`TESTING.md`, summarize implemented runtime behavior, list commands actually
run with results, and list outstanding native/hardware verification or concrete
code gaps. The returning local chat should be able to begin verification without
reconstructing your cloud conversation.
