<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Deskflow Seamless Windows: cloud implementation handoff

Prepared 2026-10-12. Read this file first, then `TESTING.md`.

## Current completion checklist — 2026-10-16

P1–P8 implementation is integrated, with the supported-capability limits below.
The product is **not physically accepted**. Latest attended attempt 27 reached
Active on actual jack with verified hidden source placement, but original-input
preparation expired and recording never armed. The Host later lost its private
heartbeat; macOS logs confirm App Nap throttling of that exact hidden process.
Both desktops/settings were restored with neutral final input receipts, while
broken-pipe cleanup diagnostics remain recorded.

Current runtime `fcd3547516` adds an activity guard for each shared window,
released on closure or failed setup, while allowing normal system sleep.
Mac build and placement/recovery/activity tests pass 3/3. See `TESTING.md` for
physical evidence and package status. Linux runtime code is unchanged; the
verified `b20bbf39d2` package remains compatible. That previous runtime passes
all six [hosted jobs](https://github.com/JackHars/deskflow/actions/runs/38924548952).
The new fix still needs physical verification, including the preparation timeout.

Attempt 28 (`build/h06-r28-3gbg3emp/`) is prepared with a signed Mac package
and passing live preflight; it has not launched. The new hosted run
[38959812367](https://github.com/JackHars/deskflow/actions/runs/38959812367) is still
in progress. Attempt 27's permission is consumed. Do not reuse an older plan.
Refresh the current candidate's live preflight and obtain fresh permission
immediately before any further physical launch. Tests use jack's actual Ubuntu
monitor, separate Mac controls, hidden source placement and saved diagnostics
with no results popup.

- [x] P1–P3: production input ownership, native drain and connected routing.
- [x] P4: per-window holding, geometry and journaled return implementation.
- [x] P5: connected bidirectional ordinary-titlebar drag implementation.
- [x] P6: supported child-window, menu, clipboard and input policies.
- [x] P7: recovery monitors, application queue limits and synthetic load tooling.
- [x] P8: separate packages, source/artifact verification and hosted checks.
- [ ] H03/H04/H06: complete the basic physical hidden-window/input test without
  startup failure, missing clicks, duplicate input or incomplete cleanup.
- [ ] H05/H06: verify real monitor layouts/scales and multiple windows.
- [ ] H07: verify ordinary dragging in both directions, cancellation and return.
- [ ] H08/H09: verify supported apps, dialogs, clipboard and input behavior.
- [ ] H10: verify owned-process failures, disconnect and sleep/wake restoration.
- [ ] H11: collect actual input/display timing and the native hour workload.
- [ ] H12: verify installation, upgrade, restart and removal on both machines.
- [ ] Complete the remaining native/library/OS queue contract review. Several
  upstream queues have no verified capacity bound; physical tests alone cannot
  certify these. Keep the gaps explicit in `native-queue-audit.md`.

Execution order: unblock the basic physical transfer first, then run the
dependent hardware rows. Obtain fresh permission immediately before **each**
physical launch, as the user requested. No results popup is wanted; retain
printed and saved diagnostics. Repeated automated passes cannot close these rows.

**Local implementation resumed on 2026-10-13.** The production coordinator is
now wired from Server through private protocol v9 (including P5 native-drag/return metadata), negotiated Core IPC5,
EI/GNOME/Qt and NativeHost. Audited Mutter build identities establish the
sender-ordering prerequisite; a retained prefix supports bounded exact point
batches without reopening the full forwarding gate. Original input requires
the explicitly configured peer pair and every live native prerequisite.
X11 has an actual-connection namespace witness but no proven application-input
drain, so it cannot authorize original input.

This checklist records implementation, not physical acceptance. The newest
`WORK_STATE.md` and `TESTING.md` entries identify each tested source and its
limits. Multiple-window association, focused text clipboard and negotiated
attached-window capture/lifecycle changes are part of the current working candidate;
first-click preparation now passes actual Core/GNOME integration at both Qt
scales. P5 bidirectional edge-drag and P6 supported child/menu code are integrated;
P7 media/resource tooling and whole-Core queue admission/evidence are implemented.
The newest verification results are recorded in `TESTING.md`. Runtime
`8132c8554` additionally expires blocked Core callbacks through an independent
dispatch monitor. All six hosted jobs, local Mac enabled/disabled/sanitizer
checks, the signed package/private handshake and exact source/artifact
verification pass. These do not establish physical restoration. Earlier runtime
`e154f94e0` adds independent Host progress expiration and passes all six hosted
jobs, local recovery/sanitizer/package checks and exact source verification.
The verified one-hour two-HD-stream synthetic workload/resource result remains attached to
`d57e78a0d`, with unchanged media sources. These do not close native or attended
acceptance.
Full platform/kernel queue certification and physical H07–H11
acceptance remain open. Historical
results below do not cover later source changes.

Earlier 2026-10-14 checkpoints: queue/admission commit `a2aa7a5e8` passes the home Ubuntu
37-suite enabled/5-suite disabled run and a fixed 12-session native matrix.
Commit `c8a9617aa` requires libei >= 1.5 for experimental Linux builds after
reproducing an upstream early-teardown crash in older releases; all six hosted
checks pass. The Mac Host's new private AX/native window-ID join has a passing
owned native regression and scoped sanitizer checks. Its production commit
`dbf1f07ef` passes all six hosted jobs and the staged signed Mac package audit
and private smoke. A subsequent owned AppKit sheet probe observes a distinct
child native ID and exact parent link. These results do not establish
drag ownership or remote child support. Two user-operated drag probes observed
continued movement after targeted PID mouse-up. Normal titlebar dragging is
still required; those checkpoints did not yet complete P5. The current
implementation checklist below supersedes their status.

A later explicitly selected system-stream experiment on `f5517122c` has a
positive physical observation: the user confirmed the owned window stopped
while held, and the new HID-head observer captured the eventual real release
before clean shutdown. Early post-submission frame updates are retained; no
native grab-completion acknowledgement or production handoff is claimed. This
fixture starts dragging from its own content view, so arbitrary-app titlebar
initiation remains a separate implementation boundary. See the latest
`TESTING.md` entry and `deploy/windowshare/native-drag-probe.md` before repeating
research already completed.

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
- Latest verified CI base: **`26b2bc6a46963fa8d1a9db30eac187ed09f5e12b`**.
  GitHub Actions is enabled and all six jobs pass for that runtime. Later
  source and documentation commits may be newer; start from the branch tip
  and use the exact results in `TESTING.md`.
- Prior implementation: `eda21b39b19004af9f4abfaacb707bb9b380f6f8` adds Server
  ownership before legacy dispatch; `ef5145665` fixes portable core linkage.
- Original local checkout, useful only to the returning local chat:
  `/Users/jackharsfai/tweakapp/deskflow`.
- `build/`, `deps/` and `.venv/` contain local/ignored material. They are not
  prerequisites available in a fresh cloud clone. Use the tracked source,
  workflow dependency lists and test scripts to reproduce checks.
- Read any applicable `AGENTS.md` in your checkout. Inspect status before edits
  and preserve unrelated changes. Follow existing formatting and REUSE headers.

At the original cloud handoff the project was approximately **50% complete overall**. This is a historical
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
| Production registration | `src/lib/deskflow/ServerApp.cpp`, `src/lib/windowshare/OriginalInputCoordinator.{h,cpp}` | Installs the configured Mac dispatcher; bounded Qt/Core mailboxes retain exact source, selection, lease and cleanup. |
| Capture | `src/lib/platform/OriginalInputCaptureSource_mac.mm`, `src/lib/windowshare/OriginalInput.{h,cpp}`, `OriginalInputCapturedInbox.h`, `OriginalInputRoute.{h,cpp}`, `LocalInputSession.cpp` | One raw consumer; separate completed-capture copies reach Server. `onRoutedInput` is historical observation, not a delivery callback. |
| Private Mac handoff | `src/lib/windowshare/LocalInputChannel.{h,cpp}`, `LocalInputPreflight.{h,cpp}`, `LocalInputSession.{h,cpp}` | Version 5 retains explicit scope, mapped point/deadline, exact Viewer target and completed Host release. |
| Host/viewer | `src/apps/windowshare/main.cpp` (`Bridge`), `NativeHost_mac.mm`, `Viewer.cpp`, `KeyboardRouteProtocol.cpp`; `src/lib/windowshare/KeyboardRoute.h` | Negotiated `input-route-v1` gates key/button/move/scroll forwarding. It is not a native queue drain or permission to post original input. |
| GNOME | `src/lib/platform/EiComputer.{h,cpp}`, `src/lib/windowshare/GnomeProviderClient.{h,cpp}`, `GnomeViewportProtocol.{h,cpp}`, `deploy/windowshare/gnome-provider/` | Owns an EIS/viewer lifetime and joins native hit observations to last-painted Qt geometry. No original-delivery permit. |
| Qt native viewport | `src/apps/windowshare/WaylandViewport.{h,cpp}`, `ViewerViewportEndpoint.{h,cpp}` | Actual QtWayland surface and private inherited observation channel. Preserve exact-version/private-API checks. |
| X11 coordinates | `src/lib/windowshare/X11CoordinateEndpoint.{h,cpp}`, `X11DisplayObserver.{h,cpp}`, `src/lib/platform/XWindowsComputer.cpp` | Actual Core and Qt connections exchange a private namespace witness and validate native hits. Original-input authority remains unavailable without a native drain. |
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
  preflight, host reservation/activation and release. Protocol v5 retains a 192-byte
  lease with explicit scope and strict version/capability decoding; old or
  mismatched scope cannot silently become a keyboard lease.
- [x] Preserve independent code-admission, delivery-ready and neutral-boundary
  checks. Production acquisition uses exact release-then-fresh-down source
  evidence (or the defined fresh scroll transition); capture continuity alone
  cannot qualify it. The legacy neutral-boundary mode remains separate. Active
  use may retain that owner's held input. Release must be synchronous or
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

- [x] Introduce negotiated, bounded Core cutoff and exact resume messages,
  retaining the authenticated peer, capture stamp, full forwarding binding,
  Viewer connection and one monotonic deadline. `IPC1` is separate from input
  sequence tracing; `DICQ`/`DICX` and `CICR` cannot enable an unacknowledged peer.
- [x] Connect the requested Core prefix to the real EI sender on its owner
  thread, flush prior compressed motion, refuse held native keys/buttons, and
  observe PONG only after its complete batch. Session/device loss revokes it.
- [x] Implement the separate provider `DrainInputPrefix` and Qt `input-drain`
  segment with exact correlation, inherited deadline, a nonrecursive empty
  Clutter queue and later completed frame, two default-queue Qt syncs, QPA
  dispatch and exact All-fence checks. Resume revokes pending/retained receipts.
- [x] Connect those segments to the **same frozen Server point and retained
  preparation lease**, verifying Mutter's compiled sender-ordering guarantee.
  The fixture's synthetic Core fields are not a production coordinator.
- [x] Fence new legacy key/button/wheel acquisition, explicitly release old
  owners, and establish backend → compositor → Qt consumption ordering while
  the full forwarding gate remains closed. Keep explicitly permitted cursor
  presentation distinct from input authority.
- [x] Complete authoritative validation of the **Server's frozen point and
  sequence** against the cutoff and exact binding. The implemented
  `requestRoutedPointObservation()` already accepts a frozen point and sequence
  and joins native/painted observations; it alone supplies no delivery permit.
  Retain native ownership, hit/occlusion, geometry, focus and freshness at use.
- [x] Add owned first-click activation with fresh focus/paint validation if the
  Viewer is not focused. Do not inject a duplicate Linux click to activate it.
- [x] Complete the X11 namespace witness and hit validation described in the
  coordinate plan using the real injection and Qt connections. Preserve their
  event-queue owners; do not open a second display and treat it as equivalent.
- [x] Keep unsupported compositor/API/device/layout states unavailable rather
  than using guessed coordinates or a generic Wayland fallback.

**GNOME/Qt implementation boundaries — segments are not delivery authority:**

1. An EI ping after the old prefix may establish one sender/native-thread
   boundary. Process the entire returned batch: PONG followed by disconnect in
   the same batch must not succeed. `EiComputer` now accepts a requested cutoff
   bound to the frozen capture stamp; the former connection-time ping is gone.
   PONG alone does not prove compositor dispatch. Verify the supported Mutter
   build's libeis event-reference ordering, not just sender API availability.
2. The provider operation combines an empty Clutter input queue at a GLib idle
   with main depth exactly one, then a genuinely later completed stage update.
   A single arbitrary `before-update`/`after-update` pair is
   insufficient: `before-update` is emitted after stage event draining, and that
   drain detaches its queue before callbacks. Request arrival during reentrant
   dispatch must be refused. The new operation ignores a frame begun before
   the empty-queue checkpoint and checks required APIs at runtime. It still
   reports `mutter-eis-sync-build-unverified` for builds outside the audited
   native ELF/source contracts. Known versions must still pass all runtime
   queue, frame, ownership and lifetime checks.
3. After the provider boundary, the distinct input-drain operation binds two asynchronous
   `wl_display_sync` callbacks on Qt's **existing default input queue** to the
   exact full forwarding fence. Qt may schedule a deferred-focus sync while
   processing old input; nested dispatch may retain an older handler on stack.
   It tracks GUI input notification and native-handler depth, drains QPA
   window-system events with the full gate closed and revalidates before reply.
   `sendWindowSystemEvents`' boolean means whether
   it sent events, not proof of success. No blocking roundtrip/new connection.
4. Explicit releases must stop key-repeat ownership. Asynchronous IME work can
   outlive a Wayland fence. The operation currently accepts only a null input
   context or the pinned built-in QtWayland context with input methods disabled;
   context replacement/destruction and other plugins deny. An implementation
   that supports more contexts needs a separately proven drain for them.

Primary sources to recheck for supported versions: Mutter 50.1
`src/backends/meta-eis.c`, `meta-eis-client.c`, RemoteDesktop session code and
`clutter/clutter/{clutter-main,clutter-stage,clutter-stage-view}.c` in
[GNOME/mutter](https://github.com/GNOME/mutter/tree/50.1); Qt 6.10.2 and 6.8.3
`qwaylanddisplay.cpp` in [qtwayland](https://github.com/qt/qtwayland) and
`qwindowsysteminterface.cpp` in [qtbase](https://github.com/qt/qtbase).

The per-window association now uses a private unpredictable app_id challenge
written on the actual Qt xdg_toplevel role. Its authenticated private snapshot
must match exactly one MetaWindow owned by the provider's Meta.WaylandClient;
the native wm-class/instance echo proves that role write reached Mutter.
Duplicate tokens, popup/unknown roles, native tree ambiguity and role/surface
replacement deny. The token stays stable through a retained seal. Do not
invent a compositor wl_surface resource-ID comparison or a public
EIS-session-to-Clutter-device association. Closing a session is not a drain.

Acceptance: delayed native/Qt queues, focus sync, reentrancy, same-batch loss,
stale/wrong cutoff or point, overlapping/shaped windows, letterboxing, expired
geometry, output/device changes and unsupported capability regressions. Cloud
may run a private headless compositor if available, but must not require the
user's desktop. See C04–C07 and H03–H05.

### P3 — Connect and enable the production original-input route

- [x] Implement the coordinator across Server, Session/Preflight, private host
  channel, Linux validator, Bridge and NativeHost using P1/P2's actual permits.
- [x] Install `OriginalInputDispatcher` only for an explicitly configured route
  whose providers can supply all prerequisites. Registering the capture inbox
  alone must continue to preserve ordinary delivery.
- [x] Complete key/modifier/repeat, button/drag, pointer, two-axis scroll and
  momentum handling using the original captured record and exact owner.
- [x] Recheck current admission/focus/geometry/connection evidence on delivery
  and heartbeat. Release idle held owners on loss, retain physical-up tombstones,
  and preserve the raw emergency chord after continuity failure.
- [x] Exercise the real protocol/adapter chain in automated integration tests;
  extend the disposable source/receiver harness so local verification can prove
  native receipts, no duplicates and no held input on the exact candidate.

Cloud completion means the production path is wired and has meaningful tests;
physical correctness remains pending H02–H04. Do not use the historical
`onRoutedInput` callback as the original delivery operation.

### P4 — Holding display, multiple windows and two monitors

- [x] Complete first-painted-frame → holding-display relocation → return as one
  owned transaction; preserve the source if creation/capture/relocation fails.
- [x] Replace single-viewer/single-surface assumptions with explicit per-window
  associations before enabling multiple exported windows in GNOME. Preserve
  exact focus, press, video, geometry and restore ownership per session.
- [x] Support overlapping exported/local windows and focus transitions without
  raising or targeting an unrelated application.
- [x] Handle monitor selection, negative coordinates, mixed scale, output
  removal/layout changes, helper crash and source/host loss. Keep the holding
  display excluded from ordinary Deskflow cursor boundaries.

Acceptance: simulated topology/geometry/lifecycle failures and session isolation
in cloud; actual hidden-source rendering, two monitors and mixed scale in H05/H06.
Qt `QT_SCALE_FACTOR` tests alone do not validate compositor monitor scaling.
The private native matrix covers two overlapping exact Viewer roles and return
to the original role at both Qt scales. Earlier intermittent invalidation
failures are retained; a passing matrix does not prove every timing race absent.

### P5 — Bidirectional edge drag

Product requirement confirmed by the user on 2026-10-14: ordinary titlebar
dragging must move an eligible application window between computers as one
continuous interaction. Selecting a source and dragging its thumbnail is not
an accepted substitute. A provisional remote preview may be an internal part
of the same ordinary drag, but must not require a separate user action.

- [x] Preserve touch-origin pointer input, zero-click real release and original
  event-time pointer metadata through the existing sole raw consumer.
- [x] Join passive application-annotated window identity to the original HID
  down/up with bounded storage/deadlines. The physical owned-window run passes
  both joins, 213 drag events and the real release; raw HID IDs remain unavailable.
- [x] Carry joined pointer observations/loss over separately negotiated private
  pointer metadata (introduced in v6, retained in v9) without acquiring an input
  lease. The native Host retains exact
  AX/native window identity, titlebar hit, process birth and physical layout.
- [x] Implement the bounded preparation/drop transaction and NativeHost gate:
  a preview Ready alone cannot mutate the source; matching local release and
  current produced/painted geometry are required. Existing durable recovery
  handles cancellation after mutation; untouched preparation leaves the source
  alone. Automated identity, stale-frame, timeout and reentrancy cases pass.
- [x] Physically verify the exact native titlebar eligibility observer against a
  separate owned AppKit window. The `--interactive-eligible` run
  `20261014T194753Z` passes native titlebar admission, movement, exact down/up
  joins and real release (201 drags, 131 frame changes). A timing difference
  between AX and WindowServer positions was fixed without relaxing identity or
  size checks. This does not yet verify every arbitrary application's titlebar.
- [x] Observe a configured Deskflow edge with the authenticated Core peer,
  screen bounds and exact locally owned press. The bounded cross-thread mailbox
  follows neighbor/partial-edge mapping, dwell, double-tap and locked corners;
  release, Escape, topology changes and disconnect revoke retained observations.
  The sole raw consumer joins the edge to the same annotated press and withdraws
  it immediately on real up, even if its annotation or Core release is delayed.
- [x] Connect that edge candidate through private v7 to the matching WindowShare
  peer and NativeHost preparation. Both endpoints check explicit dual-pin policy,
  code admission and their own current connection; NativeHost checks exact
  titlebar/window/layout. The Viewer negotiates provisional-preview support.
  The original press and active Core computer remain locally owned.
- [x] Bound preparation lifetime and revoke it on release, lost capture/press,
  Escape, changed edge/topology, either peer loss, private channel loss, lost
  admission or shutdown. A stationary held preview retains its boundary without
  turning an old motion sample into new movement. Painting cannot activate the
  Viewer or enable its input. Native callback failure and reentrancy are covered.
- [x] Integrate outbound preview motion, native placement/paint and real-release
  drop with one original button owner. Core arms movement-only capture gating
  after the exact preview acknowledgement; every original button passes.
  Real up, joined annotation and Server dispatch precede final native/paint
  verification, durable source relocation and ordinary cursor transfer. A
  raw producer fence revokes a pending drop on subsequent input. The Mac and
  Ubuntu integration suites pass; physical seamless behavior is still H07.
  The actual GNOME Viewer retains its native surface and placed frame at commit.
- [x] Connect ordinary Viewer dragging back to the Mac edge. Join the owned
  GNOME move to the original Core press, exact remote dispatch and configured
  return edge. Preserve the titlebar grip in an input-transparent Mac preview;
  require paint/native-frame witnesses and both real-release observations
  before journaled source restoration, focus, remote retirement and cursor return.
- [x] Add connected return regressions: both release orderings, no native up,
  native cancel, wrong route, newer input, failed cursor transfer, lost source,
  closed preview, TLS loss and retained rollback/recovery. Actual Cocoa/window
  behavior and output changes during real drags remain physical H07 cases.
- [ ] Complete attended bidirectional physical acceptance, including cancellation,
  recovery, cursor/grip continuity and repeated transfers. Follow
  [H07](deploy/windowshare/h07-edge-drag.md); the user deferred this test.

Acceptance: deterministic transaction/fault tests and harness support for H07.
Do not call ordinary opening/closing of a Viewer "edge-drag support."
The owned native titlebar eligibility check passes. The user requires fresh
permission immediately before each physical launch; H07 is NOT_RUN. Do not
launch physical fixtures or enable desktop routing without that approval. Bidirectional
drop coordination is implemented. All six hosted jobs pass at `c748915cc`
([run 38645498688](https://github.com/JackHars/deskflow/actions/runs/38645498688)); P5 physical acceptance remains open. See
`deploy/windowshare/window-drag-preparation.md` for the connected path and tests.

### P6 — Application compatibility

- [x] Associate supported dialogs/child windows with the exact exported parent
  and session. Negotiated `window-family-v1` uses independently captured native
  children, fresh painted acknowledgements, modal ancestor fencing, exact focus
  and shared root recovery. Native dismissal retires only the child subtree;
  unsupported identified children return their complete family locally.
- [x] Handle supported popup/context menus and owned transient surfaces under
  both Linux adapters. Qt and GNOME retain exact parent/surface associations;
  stale, foreign or ambiguous links do not acquire input. X11 preserves relative
  placement; GNOME currently uses compositor-managed transient placement.
- [x] Integrate clipboard with the actual exported-window focus and lifetime,
  preserving configured sharing policy and avoiding feedback loops.
- [x] Complete modifier/shortcut mapping and key-repeat behavior. Preserve the
  fixed Mac mapping: Option = `alt`, Command = `super`; the old reversal is fixed.
- [x] Add reproducible browser/Electron and native-app scenarios, including
  Unicode/composition behavior or clearly documented unsupported input methods.

Code and automated fixtures are implemented. **Physical H08/H09 remain NOT_RUN**;
no blanket compatibility claim is made for named applications or arbitrary
popup/IME implementations. See `deploy/windowshare/window-families.md` for the
protocol and limits, and `deploy/windowshare/app-compatibility.md` for the
updated acceptance procedure. The isolated GNOME fixture exercises real owned
sheet/menu surfaces and separate Qt/native parent mutations, without a Mac app
or production original-input authority.

### P7 — Recovery and performance

- [x] Add crash-watchdog recovery to the existing restore journal, preserving
  unresolved records and kernel process-birth identities across restart.
- [x] Add an independent Host progress monitor for stalled startup/event-loop
  work. Stop heartbeat renewal during shutdown, expire only the current Host,
  and let its existing recovery child reopen the journal. Real deadline,
  blocked-owner, healthy-heartbeat and teardown regressions run without desktop
  access; see [host-recovery.md](deploy/windowshare/host-recovery.md).
- [x] Make teardown/recovery idempotent for disconnect, host/helper/viewer/core
  failure, sleep/wake, session deactivation and emergency return. Bound shutdown
  and retain diagnostics for hung owned children.
- [x] Enforce the existing two-second Core dispatch deadline independently of
  callback return. Fixed retained slots cover nested/concurrent callbacks;
  expiry exits only the experimental Core with code 71. Ordinary launches
  create no monitor. Disposable-process tests cover blocked and healthy work,
  cancellation, exception/queue destruction and lease reuse. Startup and
  native calls outside active dispatch remain outside this guard's scope.
- [x] Implement fair active-stream scheduling/backpressure and bounded media
  queues, including hidden/minimized viewers, resize and slow consumers.
- [x] Add reproducible latency/resource/load measurement tools and define
  explicit acceptance targets before measuring. Do not invent performance claims.
  The bounded source/native recorder, strict normalizer, offline latency analyzer
  and resource sampler now exist, with targets documented before collection.
  The production Viewer has opt-in, fixed-storage receive-to-Qt-paint and queue
  metrics. `windowshare-media-load` exercises actual TLS/decoding/offscreen
  paint, bounded overload recovery and hidden/resumed streams. Resource v2
  records per-probe timing and counter resolution; its offline budget report
  applies the fixed pilot limits without accepting short runs as an hour soak.
- [x] Bound the complete experimental Core EventQueue, including configured
  actions/control events, pending/saved/retrieved lifetimes and nested callbacks.
  Admission enforces 4096 outstanding Events, 64 dispatch scopes and a two-second
  residence deadline. Cooperative failure revokes Mac capture synchronously;
  the independent monitor directly exits Core on a stuck callback. Existing
  disconnect recovery applies. Evidence v2 records both the original-stamped
  subset and all-event counts with their actual shared limits. Ordinary launches
  retain their admission policy; legacy evidence retains its original null bounds.
- [x] Audit native queue boundaries and limit libei batch processing separately
  from Core Events. The actual private EIS fixture covers capacity and expiry;
  the audit records unbounded enqueue paths and unknown OS/device capacities
  without treating them as certified. See `deploy/windowshare/native-queue-audit.md`.
- [x] Complete the declared synthetic two-stream 1080p/30-fps hour and fixed
  resource comparison on `d57e78a0d`: zero overflow/recovery/decode errors,
  134.65 MiB peak RSS and 10.44 MiB/h upper net growth. Retain failed earlier
  runs. This exercises loopback TLS, hardware decode and offscreen Qt paint;
  it does not complete H10/H11 native acceptance.
- [x] Supply an explicit fixed H11 physical recording profile: 120 seconds of
  evidence within a 150-second visible session, unchanged Linux resource caps,
  one-shot arm and early-stop rejection. Keep the default H04 profile at 30/120
  seconds. See `deploy/windowshare/original-input-physical.md`; a completed
  recording still requires native workload/receipt review and is not H11 PASS.
- [ ] Complete full native/library/OS/kernel queue certification on the frozen
  runtime and collect the actual stimulus/display measurements. Core admission
  cannot certify input buffered before it, compositor/device queues or a blocked
  native call. H10 crash/sleep and H11 physical latency/hour-soak acceptance remain
  NOT_RUN. See `deploy/windowshare/input-queue-budget.md`,
  `deploy/windowshare/media-load.md` and `deploy/windowshare/resource-soak.md`.

Acceptance: fault injection and bounded-resource tests in cloud; destructive
owned-process cases, sleep/wake and actual performance/soak in H10/H11.

### P8 — Packaging and hand-back

- [x] Keep the separate experimental Mac bundle and exact dependency/signing
  policy. Update packaging/tests for new runtime components and permissions.
  The current staged exact-library package passes its audit and private
  Core/Host handshake; installation and physical input remain H12/H04.
- [x] Supply reproducible Linux packaging, dependencies, startup/configuration
  and GNOME provider compatibility diagnostics. Do not silently enable a desktop
  extension or overwrite the ordinary Deskflow installation.
  The archive now verifies its complete provider/diagnostic inventory;
  end-user installation and upgrade acceptance remain H12.
- [x] Run available clean builds, ON/OFF compatibility and meaningful regression
  suites. Use existing CI for Mac compilation/packaging if access is available;
  otherwise record exactly what was not compiled.
- [x] Finish `TESTING.md` with implementation deltas, actual cloud results,
  exact candidate SHA, artifact instructions and local verification steps.
- [x] Push ordinary commits to the authorized feature branch/descendant when
  credentials are available. Otherwise provide the commit/patch and explain how
  to transfer it. Leave no essential code only in ignored build directories.

Runtime candidate `8132c8554` passes all six hosted jobs in run `38744933197`
and the independently verified 1160-entry source archive. Its local Apple-issued
exact-library bundle passes staging/handshake checks. CI retains verified
Linux and ad-hoc Mac archives for seven days. See `TESTING.md` for exact hashes,
commands and remaining hardware tests; these checks do not complete H12.

Collection/source-fixture follow-up `286328d05` also passes all six hosted jobs
in run `38750248886`, a fresh home Ubuntu enabled/disabled/sanitizer pass, and
151 Python checks on both local platforms. Its source export and downloaded
Mac/Linux archives verify independently. The local staged Core/Host runtime
is unchanged from `8132c8554`; the new signed source helper is separately
hashed. `TESTING.md` records the exact artifacts and remaining acceptance.

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
