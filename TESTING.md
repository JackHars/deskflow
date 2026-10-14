<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Seamless Windows: cloud results and local verification

## Latest local evidence — 2026-10-14

- **Final hosted checkpoint passes all six jobs:** commit
  `c748915ccb65c4b169188d3ae2d1ea326a288ff9`, [run 38645498688](https://github.com/JackHars/deskflow/actions/runs/38645498688).
  GCC/Clang sanitizer foundations, transport/private IPC, Linux and Mac
  application/Core checks, feature-disabled compatibility, package verification
  and actual headless GNOME integration all pass. The two fixture corrections
  below are included. `p5-final-github-checks.json` preserves the exact job
  results; this result does not turn the deferred physical H07 into a pass.
- **Bidirectional P5 code is integrated; physical acceptance is deferred:**
  private v9 carries native move offers independently from original-input
  leases. Return joins the exact remote Core press/edge/dispatch to the owned
  GNOME grab and requires both real release observations before journaled
  native restoration and ordinary cursor transfer. The input-transparent Mac
  preview requires Qt paint plus an exact owned Cocoa/WindowServer witness.
- **Current Mac return regressions pass:** `p5-return-order-tests.json` has
  36 child-process scenarios, including zero-annotation remote input, ordered
  metadata publication, both release orders, absent native release, native
  cancel, wrong route, newer input and failed transfer. The wire/watcher/
  endpoint suites pass in `p5-return-wire-watcher-tests.json` (15 malformed
  native-offer variants, role/capability/replay/destructive callbacks, repeated
  watcher retirement). `p5-return-server-tests.json` passes all 50 cases,
  exercising real up before transfer in both directions and producer/layout/
  TLS/capture loss. The feature-disabled Server passes five cases in
  `p5-return-server-off-tests.json`; both production build modes pass.
- **The Qt return preview failure cases are reproducible:**
  `p5-return-bridge-tests.json` exposed pending return state after preview
  destruction. `p5-return-bridge-tests-2.json` and the scoped crash backtrace
  exposed QWidget emitting destroyed after Viewer members had been destroyed
  but before QPointer cleared. Retiring that pointer before cancellation fixes
  both. `p5-return-bridge-tests-3.json` passes normal paint/native-witness/drop,
  denied restoration, native cancel, TLS loss, unavailable source and destroyed
  preview with real TLS and offscreen Qt, explicit fake native witnesses.
- **First Ubuntu return run:** `p5-linux-return-build/result.json` builds the
  full app and passes seven suites; Bridge reproduces the same cleanup failure.
  It is preserved as a failed run, not counted as a pass.
- **Corrected Ubuntu verification passes:**
  `p5-linux-return-final/result.json` records the full application build, all
  eight affected suites (48.81 s), the actual headless GNOME production-Viewer
  continuity check, and production Core/Server builds and tests with the
  feature both enabled and disabled. Its frozen archive is
  `629fcca6781af1ed3b861cdb79666575dbdaa889315014e1f10f2243f14d261d`.
  The subsequent bounded watcher-retirement ordering change passes the full
  app rebuild and Bridge/viewport suites (13.63 s) in
  `p5-linux-watch-retirement/result.json`, archive
  `cc40c2e4832c0d3ccdc8e3ee4feff7c526ea5e70c976da93feb103c2551c94a0`.
  Signed Mac equivalents pass in `p5-return-retired-watches-tests.json`.
  Both Linux runs use offline disposable image `65f389a94c05` on home Ubuntu,
  with no desktop sockets/devices or network, and remove their temporary
  directories after collecting source/binary hashes and logs.
- **GCC fixture portability correction:** hosted GCC 13 in
  [run 38644155477](https://github.com/JackHars/deskflow/actions/runs/38644155477)
  rejected a copied `std::pair` loop variable in the pointer-join test under
  `-Werror=range-loop-construct`. The fixture now uses a const reference.
  Its signed Mac check passes (`p5-pointer-fixture-tests.json`), and all 16
  portable foundation suites pass under GCC 15 ASan/UBSan on home Ubuntu
  (`p5-gcc-foundation-final/result.json`, 1.13 s). The source hashes and original
  hosted compiler log are retained; production behavior is unchanged.
- **Reconnect smoke recognizes the preview capability:** hosted Linux in
  [run 38644472166](https://github.com/JackHars/deskflow/actions/runs/38644472166)
  passed all 19 application suites, then the older smoke fixture rejected the
  new `window-drag-preview-v1` Hello capability. Its exact accepted list now
  includes that capability; duplicates, unknown capabilities and native motion/
  move offers from the offscreen Viewer still fail. The real signed Mac and
  rebuilt Ubuntu Viewer both pass refused-connection recovery, mutual TLS,
  authenticated reconnect and clean exit (`p5-viewer-smoke-mac.json`,
  `p5-viewer-smoke-linux/result.json`). Neither run starts a native Host or
  interacts with the desktop. Production code is unchanged by this correction.
- **Current Mac ASan/UBSan checks pass:**
  `p5-return-sanitize-tests.json` records four separately signed offscreen
  suites: Bridge (including preview destruction), all 36 coordinator cases,
  private IPC and viewport endpoint. Both sanitizers halt on error; leak
  detection is disabled. Native return witnesses are fakes in these tests.
  The initial default-SDK build could not link Qt's AGL dependency;
  `p5-return-sanitize-sdk-build.log` records the successful build with the
  production MacOSX15.5 SDK. The original failed build log is retained.

- **Outbound P5 real-release/drop coordination passes automated checks:**
  private v8 adds exact Drop/Finish and Host status messages. Core joins the
  original real release, annotated identity and Server Local dispatch; a raw
  producer fence cancels on newer input. Native holding relocation precedes
  ordinary cursor transfer. No synthetic button is posted. Signed Mac tests:
  `p5-drop-coordinator-tests-3.json` (29 scenarios), `p5-drop-tests-2.json`
  (IPC/Bridge; its older coordinator failure is superseded),
  `p5-drop-server-tests.json` (50 cases), `p5-drop-native-tests.json`,
  `p5-drop-gate-tests.json` and `p5-drop-surface-viewer-tests.json` all pass.
  `p5-drop-final-build.log` and `p5-drop-off-build.log` record enabled and
  disabled production Core builds. The coordinator helper initially consumed
  its transfer request twice; retaining its first read fixes the test.
- **Ubuntu full application and six affected suites pass:**
  `p5-linux-drop-build/result.json` records configure/build success and passing
  private IPC, drag coordinator, preview, viewport, Viewer and Bridge tests
  (41.11 s) in offline image `65f389a94c05`. Source and executable hashes are
  retained; only the owned temporary source/build directory was removed.
- **Actual GNOME 50 committed-window continuity passes:**
  `p5-native-drag-surface-1/result.json` exposed QWidget flag changes replacing
  the placed native window and moving it from (160,90) to (319,116).
  The fix updates the existing QWindow hint without QWidget reparenting.
  `p5-native-drag-surface-2/result.json` verifies identical association, Qt
  surface incarnation/resource, native window/surface tokens and exact
  (160,90,642,520) frame before/after commit. The resulting native window can
  be watched for ordinary titlebar grabs. The tracked runner is
  `deploy/windowshare/gnome-window-drag-tests.py`, with the actual production
  Viewer fixture `windowshare-gnome-drag-surface-fixture`. Use only the private
  GNOME container; this test does not route input on the real desktop.
- **Physical testing remains deferred:** the latest user reply is “Keep
  building; test later.” No physical movement-hold or bidirectional edge run
  occurred. H07 remains **NOT_RUN**; bidirectional coordination is now implemented.

Earlier entries below describe earlier checkpoints and are superseded where
these newer results cover the same path.

- **P5 motion/placement implementation, physical test deferred by the user:**
  movement-only capture gating preserves every original button transition and
  retains raw deltas for the sole Session consumer. Signed Mac gate and capture
  tests pass (`p5-motion-hold-tests.json`); the production Core builds with the
  feature enabled and disabled. The new hold probe is prepared but **NOT_RUN**.
- **Private GNOME placement/watcher protocol passes on Linux:** normal and
  AddressSanitizer/UndefinedBehaviorSanitizer builds of the production provider
  client pass its private D-Bus suite, including exact native placement echoes,
  watcher events, missing capability, substituted/duplicate fields and continued
  independent viewport requests (`p5-drag-channel-ubuntu-2/result.json`). The
  first image lacked `dbus-daemon` and skipped; the passing run mounted only
  that executable and its missing AppArmor library read-only, with its own bus
  configuration, no desktop bus, no network and disposable filesystem.
- **Actual GNOME 50 compositor placement and ordinary move-grab pass:**
  `p5-native-gnome-drag-2/result.json` records an exact owned 640x400 window
  moved from (320,176) to (350,196), restored, and verified against replay,
  foreign caller and substituted association requests. The same isolated
  headless compositor reports native titlebar grab 1, begin/motion/end sequences
  1/2/3 and button release at (710,223). All source files, Shell logs and the
  native report are retained. This is fixture EIS input, **not physical Mac
  input or H07**. The first native run exposed non-enumerable GI rectangle
  fields; explicit field copying fixes it and has a regression test. The
  combined JS suite passes 39 tests; the Linux package suite passes 11.
- **Viewer preview placement requires fresh paint:** the new controller joins
  exact native placement → an actual later Qt paint → native position recheck.
  Signed Mac preview tests cover coalesced final movement/drop, missing paint,
  identity/position changes, expiry, cancellation and late callbacks. Existing
  Viewer, Bridge and private drag-coordinator tests also pass
  (`p5-placement-tests.json`, `p5-placement-bridge-tests.json`,
  `p5-coordinator-hold-tests.json`). The first coordinator run expected Hold
  never to reach native placement; its assertion now distinguishes stationary
  verification from new motion. Production still awaits the local release and
  cursor transfer coordinator; **P5/H07 remain open**.
- **Linux application build and five affected suites pass:**
  `p5-linux-placement-build/result.json` records a frozen complete source tree,
  successful Ubuntu Qt application build, and passing drag coordinator,
  preview controller, viewport endpoint, Viewer and Bridge suites (25.01 s).
  The container had no network, user display socket or physical input devices;
  the temporary source/build directory was removed after collecting evidence.
- **Native ordinary-titlebar eligibility passes physically:** the user returned
  and requested physical testing. `owned-titlebar-eligible-physical-20261014T194753Z`
  records exact owned window **10482**, source PID **64457**, native event **521**,
  joined raw down/up sequences **82/284**, accepted native titlebar identity,
  **201 drag events**, **131 independent frame changes** and the real zero-click
  release **3.472907914 seconds** after down. Completion is `physical-release`,
  `eligibleSourceDrag=true`, exit 0, with exact owned-fixture cleanup and no
  surviving titlebar probe. No input was posted or suppressed. Source/binary
  hashes, raw logs and independently checked ordering are preserved in that
  run's `launch.json`, `evidence.json` and `analysis.json`. The user confirmed
  **“Yes, it worked normally”** to movement and closing only after release;
  `user-observation.json` preserves that answer. The aggregate evidence is
  `p5-titlebar-physical-evidence.json`.
  This verifies the production read-only observer against a separate owned
  AppKit window; arbitrary application compatibility and **H07 remain open**.
- **Physical movement exposed and corrected asynchronous frame checks:** the
  diagnostic run `20261014T194537Z` reports AX position **400,138** while
  WindowServer reports **402,143**, both **640x368**, for the same retained
  native window. Comparing their independently sampled origins incorrectly
  retired an ordinary drag. Continued observation now uses WindowServer's pose
  after rechecking exact AX/native ID, process birth, visibility and both native
  sizes against the original extent. Either resize, invalid geometry, lost
  identity or minimization still retires the observation. The recorded sample
  and resize/invalid-geometry regressions pass in the native suite
  (`p5-titlebar-physical-native-tests.*`); the production WindowShare build and
  default/invalid probe invocation checks also pass. The subsequent physical run above
  confirms movement and release with the correction.
  Earlier startup failures, the user's missed 30-second window and two failed
  drags remain recorded. The passive fixture now allows 90 seconds, has a
  separate five-second watchdog cleanup grace, waits at most one second for
  its owned WindowServer frame before installing taps, and reports the exact
  startup/read failure. These harness changes do not extend production drag
  budgets. An accidentally rerun older test executable is explicitly retained
  as `p5-titlebar-old-binary-tests-not-current.*`, not current validation.
- **Private drag preparation is connected:** Core/Host **v7**, separately
  negotiated capability 16 and fixed 208-byte message 20, carries Begin/Motion/
  Hold/Cancel with the original press, boundary, bounds, both explicit pins and
  Core connection identity. Host independently binds its current WindowShare
  connection and code admission before NativeHost eligibility/capture. The
  production Viewer negotiates `window-drag-preview-v1`, paints without activation
  or input, and rejects a changed drag identity. Both coordinators retain only
  one preparation, with 100 ms Hold traffic, 350 ms Host silence timeout and
  30-second absolute budget. Source/peer/topology/press loss and shutdown revoke
  it. Raw Escape cancels independently of a delayed Server worker.
  `NativeHost::observeWindowPointer` now cancels held preparation on real up;
  it never automatically invokes the explicit drop-release API. Native cutover,
  production preview placement/drop and GNOME drag-back remain unfinished.
  See `deploy/windowshare/window-drag-preparation.md`.
- **Connected preparation validation passes:** **8/8** Mac Core/private suites
  (`p5-preview-core-tests-2.*`: IPC, coordinator, code admission, preflight,
  bootstrap, companion, Server and portable edge) and **3/3** Viewer/Bridge/native
  suites (`p5-preview-ui-final-tests.*`). The coordinator exercises 21 real
  socketpair child-process scenarios with fake native callbacks. Enabled
  Core/Host and feature-OFF Core builds pass. On home Ubuntu, disposable offline
  containers with GCC 15.2 / Qt 6.10.2 pass the IPC and coordinator suites normally
  and under ASan/UBSan (`p5-preview-ubuntu-final/result.json`, four passing core
  executions); the current Viewer suite also passes normally and under
  ASan/UBSan (`p5-preview-viewer-ubuntu/result.json`, two executions). Viewer
  tests use the offscreen platform; these are not physical Linux acceptance.
  Container source hashes and owned temporary-directory removal are recorded.
  The original Viewer fixture destroyed its recorder before the Viewer and
  corrupted its test log storage; recorder lifetime is corrected. The initial
  abort, compiler warnings/fixture error and failed runs remain preserved.
  Some unsigned Mac test launches stalled in kernel wait. Ad-hoc signed owned
  build test executables pass; signing manifests retain before/after hashes.
  A terminated unsigned-copy launch was still in kernel wait at the final
  diagnostic snapshot (`p5-preview-unsigned-launch-diagnostic.json`); do not
  interpret that attempt as a test pass or blindly reuse its historical PID.
- **Configured-edge observation and exact press join pass:** the Server now
  publishes the authenticated configured neighbor, screen bounds, mapped point
  and exact original local press through a one-value mailbox. Tests cover all
  four directions, partial-edge mapping, dwell requiring a later real motion,
  double-tap/corner/scroll-lock policy, unchanged local button ownership and
  revocation on release, Escape, reconfiguration, shape change, disconnect,
  screensaver and shutdown. Portable tests cover stale/replayed/mismatched
  observations, TLS loss, counter exhaustion and concurrent mailbox closure.
  Actual Session tests cover joining through the sole raw consumer, immediate
  withdrawal before delayed release annotations, and deletion inside the new
  callback. The corrected Server/companion suites pass **2/2**
  (`p5-edge-final-tests-2.*`), as do the enabled Core build, feature-OFF Core and
  Server suite, portable Mac edge test and scoped ASan/UBSan test
  (`p5-edge-portable-tests.*`, `p5-edge-sanitize-tests.*`, `p5-edge-feature-off-*`).
  The same portable edge suite passes normally and under ASan/UBSan directly
  on home Ubuntu, GCC 15.2, with transferred source hashes verified and owned
  temporary files removed (`p5-edge-ubuntu-2/result.json`). The earlier failed
  fixture/build/routing runs are retained, not reclassified. This is Core
  observation only at that earlier checkpoint; private preparation was connected
  subsequently as described above. Source mutation, input switch, moving preview
  and GNOME drag-back are not enabled by the current preparation path.
- **Same-event HID/annotated identity join passes physically:**
  `owned-titlebar-joined-physical-20261014T174302Z` records the exact owned
  window **10383** on both joined down and up, native event number **516**,
  raw sequences **211/425**, **213 drag events**, **147 frame changes** and
  a real zero-click release **3.828392286 seconds** after down. HID window
  fields remain zero; they are not replaced with fixture identity. Exit 0,
  `physical-release`, no rejection, no input posted/suppressed and owned probe
  cleanup are recorded. `p5-pointer-join-evidence.json` binds the tested source
  and binary; the run has raw logs, `evidence.json` and `analysis.json`.
  Production keeps the sole raw consumer and a separate bounded passive
  annotation queue. The 32-slot, 50ms join rejects mismatches/duplicates and
  expires missing metadata without delaying ordinary input. This verifies the
  owned same-event join, **not cross-machine dragging or native grab transfer**.
- **Private pointer observations and drag preparation are implemented:** private
  Core/Host protocol **v6** adds separately negotiated, 72-byte window-pointer
  metadata and canonical loss notification. Input lease counters/admission are
  unchanged. The existing Session consumer sends joined observations; the Host
  retains an exact standard AX window, process birth, native titlebar hit and
  physical layout, and requires actual source movement before preparation.
  Missing/ambiguous identity, content/control hits, a replacement press and
  observation loss deny/cancel provisional work. `WindowDragTransaction` and
  NativeHost gate source mutation on a matching local release plus a produced
  and painted current geometry. Old geometry, changed identity, timeout and
  reentrant cancellation cannot commit; existing source journaling owns rollback.
  The post-release frame query/timer is **not** a native grab-completion receipt.
  The later v7 Core coordinator now calls preparation; the GNOME drag-back
  controller remains unfinished. The ordinary explicit export path is unchanged.
- **Current automated validation:** the enabled Mac Core/Host build and ordinary
  feature-OFF Core build pass. **10/10** scoped Mac integration suites pass
  (`p5-integrated-tests.*`); the IPC and transaction sanitizer suites pass
  (`p5-private-sanitize-tests.*`, `p5-transaction-sanitize-tests.*`). Earlier
  join/capture normal and sanitizer checks pass (`p5-pointer-join-*`). The home
  Ubuntu machine passes join/transaction normal plus ASan/UBSan (**4 executions**,
  `p5-join-transaction-ubuntu/result.json`). Its host lacks Qt development files;
  private v6 IPC normal and ASan/UBSan tests instead pass in a new disposable
  Ubuntu build container using Qt 6.10.2, with exact transferred source hashes
  (`p5-private-ubuntu/container-result.json`). No desktop input, installed app,
  existing container or machine setting was changed by these runs. The failed
  direct-host Qt dependency check remains recorded in `p5-private-ubuntu`.
- **Physical eligibility probe:** `--interactive-eligible` uses
  one separately owned AppKit test window and the production read-only native
  titlebar observer. A separate source process keeps AX queries independent of
  the observer's event loop. Its physical pass is recorded above. The default
  invocation exits 2 without opening a window or tap. P5/H07 remain **NOT_RUN** for actual
  edge crossing, continuous native drag cutover, destination preview/drop and
  drag-back. See `deploy/windowshare/native-titlebar-probe.md`.

- **Annotated-stage window identity passes in the owned fixture:** the separate
  `--interactive-annotated` mode builds and default/invalid invocations exit 2
  without a window or tap (`titlebar-annotated-*`). Fresh physical run
  `owned-titlebar-annotated-physical-20261014T171931Z` records both native window
  fields as **10359**, exactly the owned window ID. It records 167 drag events,
  114 frame changes and the real zero-click/subtype-3 up **3.078813456 seconds**
  later; down/up event number is 515. Completion is `physical-release`, exit 0,
  no capture rejection, no input posting/suppression, and the probe exits.
  The run's `analysis.json` binds its exact raw evidence. Its single passive
  tap is at the application-annotated stage; production Core's HID tap is
  unchanged. These separate single-tap runs do not prove same-event correlation
  across stages, arbitrary-app titlebar eligibility or native grab authority.
  The later joined run above supersedes this correlation gap; P5/H07 is open.
- **Normal titlebar capture passes on the Mac:** with fresh user readiness and
  both capture fixes, `owned-titlebar-physical-20261014T171313Z` records one
  ordinary titlebar down, **226 drag events, 150 owned-frame changes and one
  real release 4.022145439 seconds later**. It completes with `physical-release`,
  exit 0, no capture rejection, no input posted/suppressed, and no probe left
  running. `analysis.json` verifies the raw evidence hashes and sequence/time
  ordering; `titlebar-zero-up-evidence.json` freezes the exact code/binary.
  This passes owned normal-titlebar capture only. Both HID-head native window
  IDs are still zero: arbitrary-app detection, transfer and **P5/H07 remain open**.
- **Native zero-click release failure and correction:** the fresh run
  `owned-titlebar-physical-20261014T170310Z` records an ordinary titlebar down,
  **244 drag events and 164 independent owned-frame changes**. It then revokes
  on the real touch-origin left-up (type 2, subtype 3, click count 0). Its exit 0
  does not make it a pass: `complete.reason` is `capture-revoked`. Both window
  ID fields on the down are **zero**, so the known fixture's identity cannot be
  generalized to arbitrary application windows. `analysis.json` binds this
  interpretation to the retained raw evidence. No input was posted/suppressed.
  Three new regressions fail against the old zero-up guards
  (`titlebar-zero-up-red-*`). The fix preserves zero on up through capture,
  validation, IPC and native receiver preparation; down still requires a
  positive count, and release still requires the exact retained press/route.
  The corrected two Mac capture suites and four scoped ASan/UBSan suites pass
  (`titlebar-zero-up-capture-*`, `titlebar-zero-up-sanitize-*`). Current shared
  code also passes **4/4 home Ubuntu executions**: original-input and route,
  normal plus ASan/UBSan, strict GCC 15.2, exact verified transferred source
  hashes. Only the test's temporary directory is removed; no desktop input is
  posted. See `titlebar-zero-up-ubuntu-portable/result.json`. This is portable
  code verification, not a native Ubuntu acceptance rerun. The Mac Core/Host
  build passes; the integrated run passes 8/9 suites. The sole IPC failure was
  an expected-byte fixture still describing the old positive-count sample.
  Its corrected zero-count golden vector and a retained positive-count check
  pass the separate IPC rerun (`titlebar-zero-up-ipc-tests-2.*`). The failed
  integrated result is retained; all nine related suites have a current pass.
  The fresh physical repeat passes above. Final source and result hashes are
  bound in `titlebar-zero-up-final-evidence.json`; the golden-vector correction
  changes only tests after the physical run, not its probe or production code.
- **Trackpad capture failure reproduced:** the normal-titlebar attempt
  `owned-titlebar-physical-20261014T165243Z` stops before any titlebar-down record.
  After adding first-rejection diagnostics, the fresh repeat
  `owned-titlebar-physical-20261014T165717Z` records callback/native type 5,
  subtype 3, flags 0, zero deltas and a nonzero native timestamp. The old adapter
  returns Malformed and revokes on that first event. Both processes exit and
  post no input; neither is a successful drag observation. The documented
  `NX_SUBTYPE_MOUSE_TOUCH` was incorrectly excluded with tablet subtypes.
  The constructed-only regression fails against that old adapter; see
  `titlebar-touch-red-{test.log,test.xml,evidence.json}` under the Oct 14 root.
  The correction supports touch-origin mouse pointer semantics, preserves the
  subtype locally and continues rejecting tablet/unknown subtypes. The next
  physical run above confirms the down/drag path and exposes the separate
  zero-click-up bug; no touch-contact or gesture delivery is claimed. Before
  the zero-up correction, seven related integration suites also pass
  (`titlebar-touch-integrated-*`); that result does not cover the release fix.
- **Event-time pointer metadata:** the existing production capture/completion
  path and Server copy conduit now retain position, native window IDs and
  reported source fields with the exact epoch/sequence/source timestamp. The
  two Mac constructed-event suites pass after correcting two fixture assumptions:
  Quartz normalizes out-of-range window fields, and the synthetic marker must
  be set on the private source before event creation. The final copy-queue test
  publishes intervening records, so stale metadata rejection cannot pass due
  to an unrelated sequence gap. The full Mac build and **16/16 related suites**
  pass, including actual Server/coordinator/companion tests. Four scoped
  ASan/UBSan suites pass with leak detection off; system frameworks and prebuilt
  Qt libraries are not instrumented. The shared original-input and route suites
  compile with strict warnings and pass on the home Ubuntu machine (GCC 15.2),
  normally and with sanitizers: **4/4 executions**. The transferred source
  hashes match the local tree; its owned temporary directory is removed.
  This is portable-code Linux verification, not another native desktop matrix.
  Logs/XML are `titlebar-integrated-tests-2`, `titlebar-capture-sanitize-tests`,
  `titlebar-capture-tests-*` and `titlebar-ubuntu-portable/result.json` under
  `build/roadmap-completion-20261014/`; earlier failures are retained. The final
  adapter fixture accepts Quartz's actual representable field or expects the
  optional metadata to be absent when it is out of range; its normal and
  sanitizer retests both pass (`titlebar-final-adapter-*`). Source, binaries
  and results are bound in `titlebar-capture-final-evidence.json`. A new
  passive [normal-titlebar probe](deploy/windowshare/native-titlebar-probe.md)
  builds; the subsequent physical failures and correction are recorded above.
  P5/H07 remains open. These changes do not enable export or input redirection.
- **HID-head physical repeat:** exact `f5517122cdb800232e41e7e1ee9176d8dff7170d`,
  source/binary hash verification and fresh user readiness. The run
  `owned-hid-drag-physical-20261014T161234Z` exits 0 on a later unmarked HID
  left-up, **3.616467373 s after submission**. Both taps and the app observe
  the marked synthetic up. The user confirms “Yes, stopped then closed on
  release.” Counts: 211 later pointer motions, 175 frame observations paired
  with newer motion. Two early frame changes remain in the raw outcome
  `window-movement-observed-after-release-submission`; the following 180 frame
  samples are stable. This is a positive owned-fixture observation with the
  real release captured, **not H07 or arbitrary-app titlebar acceptance**.
  The probe's immediate HID-release shutdown makes its zero unmarked-session-up
  count inconclusive about later delivery. `interpretation-evidence.json`
  binds the logs, analysis and direct user report; source hashes match the
  tested commit. No probe remains running and no production input route changed.
- **User-operated system-release experiment:** the first HID attempt
  (`owned-hid-drag-physical-20261014T155548Z`) rejects a zero-timestamp event
  before posting. An independent constructed-only check identifies the wrong
  clock domain in the first repair, also without posting. The final constructor
  uses system uptime; the independent native-clock regression and pure
  accounting pass **2/2**. The corrected physical attempt
  (`owned-hid-drag-physical-20261014T155854Z`) observes the marked up in the
  session and app. The user directly reports “Stopped while I held the button.”
  The automated result stays **inconclusive/invalid-observation**: no unmarked
  session-up was observed and a second press ends the probe. Raw logs, source
  and binary hashes, `motion-analysis.json` and `user-observation.json` are
  retained. This is not an H07 pass. A later HID-head observation tap with a
  separate session-up observer builds and passes **2/2 noninteractive tests**
  in both normal and scoped ASan/UBSan builds (leak detection off; system and
  prebuilt Qt libraries not instrumented). Hashes are in
  `hid-drag-dual-observer-evidence.json`; the later physical repeat is recorded
  above. Logs are `hid-drag-dual-observer-*`.
- **Native identity production checkpoint:** `dbf1f07ef5c28dc898a4a5fa5762b54aca23506d`
  passes all six hosted jobs in
  [run 38591194186](https://github.com/JackHars/deskflow/actions/runs/38591194186).
  `native-identity-role-commit-binding.json` verifies the six identity-related
  source files against their final local-test hashes. The separate signed
  exact-library Mac package passes audit, private Core/Host handshake and
  rejection smoke. Its read-only permission probe reports the exact-identity
  API available under the packaged signing policy. ZIP SHA-256:
  `118cbcbe5dd895ae12d623ca3dd1814f27ec082ec1029f7180bfad6fadfcdac4`
  (47,801,994 bytes). Evidence is `mac-identity-package/evidence.json` under
  the Oct 14 Mac root. It is staged, not installed; original input stayed disabled.
- **Further test tools:** the separate `--interactive-hid` drag probe builds
  and passes default pure accounting plus scoped ASan/UBSan checks. It is a
  one-shot system-stream experiment restricted to its active owned fixture,
  not a production input fallback. Its later physical attempts and harness
  corrections are recorded above. Logs are `hid-drag-probe-*` under the Oct 14 Mac root.
  The `--native-child-probe` observation finds the owned AppKit sheet's distinct
  native ID and exact attached parent; it posts no input and grants no child
  rendering authority. Both the regular and scoped ASan/UBSan native runs
  record that observation and a clean owned-child exit. Leak detection is off
  and prebuilt Qt/system libraries are not instrumented. Source, binary and
  log hashes are in `native-probe-tools-evidence.json`. See `native-child-identity-*` and the
  [procedure](deploy/windowshare/native-window-identity.md). These later tool
  changes are not covered by the earlier hosted run above.
- **Supported Linux CI:** all six jobs pass for `c8a9617aa` in
  [run 38586489277](https://github.com/JackHars/deskflow/actions/runs/38586489277),
  including the real EI startup/shutdown regression, ordinary OFF compatibility,
  native GNOME, scoped sanitizers and packaging. Experimental Linux builds now
  require libei >= 1.5; the CI application container uses Ubuntu 26.04.
- **Retained Debian failure:** `debian-ei-teardown-red-1` reproduces the same
  crash as CI run 38581798655 on its exact Debian base and libei 1.3.901.
  GDB reaches `ei_disconnect` from `ei_unref` in `EiComputer::cleanupEi` before
  an EIS connection object exists. This is upstream fix
  [12237f19](https://gitlab.freedesktop.org/libinput/libei/-/commit/12237f19),
  present in inspected 1.5 source and absent from inspected 1.3.901/1.4.0.
  All 33 returned artifact hashes and preservation/cleanup checks verify.
  The diagnostic remains FAILED. A collector-format correction only adapts its
  compile-command inventory; raw artifacts and their hashes are retained.
- **Dependency guard:** `libei-baseline-guard-2` verifies actual Debian ON
  configuration rejects 1.3.901 with the actionable version diagnostic, while
  actual OFF configuration succeeds. All 27 hashes and cleanup checks pass.
  `libei-baseline-guard-1` retains a harness assertion failure caused by CMake
  wrapping the diagnostic across lines; the second run normalizes whitespace.
  No test is skipped and no production assertion is relaxed. This is not an
  OFF runtime pass on the affected library. Archive:
  `5a4219681212b992d63e2fd7108d2235617e6d1588481aed6d4ee6c4920096e8`.
- **Home Ubuntu queue/admission build:** `queue-admission-integration-1`, exact
  commit `a2aa7a5e8`, **37/37 ON + 5/5 OFF suites PASS**; actual Core/EI and
  broader GNOME/Qt tests pass at scales 1/1.25. Reconnect, pure harness checks,
  Linux package verification and inert launchers pass. All **135 artifact
  hashes**, source/binary/runner identities and preservation/cleanup checks pass.
  Source archive `c748a5627a83d66f6fa0540c594e8032caa5687924ee66ace5965662f1741882`,
  manifest `8e4ce752affa59c631f7b0c7e5e8a936978e32d7225b74788dc2f38e4fc260e2`,
  1,099 files. `commit-binding.json` compares every path/mode/content with Git.
  Package SHA-256 `08ddad901465b7870febdc4aed865d0e3a7649b8e20474cd16eb1ce4defe85e5`;
  the returned archive is independently verified locally. No installation.
- **Repeated native matrix:** `queue-admission-matrix-1` uses the same verified
  binaries and all 1,099 unchanged source files. Three runs per scale 1/1.25
  and ordinary/prepared-first-click mode give **12/12 PASS**, with no retries.
  All **267 artifact hashes** and preservation/cleanup checks pass. This is a
  native-only reuse run, not another full build. Source archive:
  `412ddb939a2bfc31d3d3af36ce92b72643e868ef9e722de06c9d7d644adbfa57`.
  The preceding 9/12, 10/12 and 11/12 matrices remain FAILED evidence.
- **Mac queue/admission checks:** ordinary OFF build and **4/4 suites PASS**.
  Scoped ASan/UBSan EventQueue and input-evidence suites both PASS. Enabled
  results remain the original **39/40**, then the corrected EventQueue suite's
  separate pass. Logs/XML are `mac-off-*`, `event-sanitize-*` and
  `input-evidence-sanitize-*` under `build/roadmap-completion-20261014/`.
  Exact-library staged Mac package audit and private handshake/rejection smoke
  PASS; archive SHA-256
  `cebb12ba409f73a2475f25fdd0a0864f24eb7f41aae83672fe12b6c0399abecc`.
  It is not installed and predates the identity change below.
- **New native Mac identity:** current Host code uses the exact private
  AX/native window-ID lookup for export and recovery. Actual owned AppKit
  collision/rename/replacement checks PASS under `native-exact-identity-1/`.
  Three pure CTests PASS (`mac-exact-identity-tests.*`) and the new native
  recovery/admission executable PASS (`mac-exact-identity-recovery.log`).
  Scoped ASan/UBSan recovery and the real owned-window test both PASS in
  `native-identity-sanitize-*`. Leak detection is disabled; system/Qt libraries
  are not fully instrumented. The first build reaches Core/Viewer/recovery
  targets, then its original Make invocation cannot see the newly generated
  identity-test target; the second and final build invocations pass. No source
  workaround was needed. See the [native procedure](deploy/windowshare/native-window-identity.md).
  The final window-role guard also passes **4/4 related CTests** and the real
  collision/rename/replacement test (`mac-identity-role-*`). Its rebuilt scoped
  ASan/UBSan recovery and native desktop tests pass
  (`native-identity-role-sanitize-*`). REUSE passes **1,076/1,076 files**.
  `native-identity-role-evidence.json` binds this final source and binaries to
  those results; earlier evidence retains its earlier source hashes.
  This does not establish child rendering, focus/drag authority or H08 acceptance.
- **Physical drag prerequisite:** the user explicitly authorized the first
  owned probe and requested the second. Both exit 0 after real release while
  recording `window-movement-observed-after-targeted-up-submission`, app-up
  true and `serverReleaseProven:false`. The two runs record 41/74 post-submission
  motions and 33/62 later frame observations. Evidence and hashes:
  `owned-drag-physical-{1,2}/` under the Oct 14 root. These are negative evidence
  for that release method, not H07 passes. The user requires normal titlebar
  dragging; selected-window thumbnail dragging is not an accepted substitute.

Ubuntu folders above are under `build/wifiman-resumed-20261013/ubuntu/`;
Mac folders are under `build/roadmap-completion-20261014/`. Full physical input,
monitors, remote dialogs/popups, sleep/fault acceptance, performance/soak and
installation remain pending. Earlier notes below describe their named source
checkpoints; statements that a later check is pending are historical.

## Continuation — 2026-10-14

The `final-integration-2` source described below is committed as
`e461d2b31d66eb3cfc17a08e0cc3ba2953573ebc`. Independent comparison verifies
all 1,098 source paths, modes and contents against its frozen archive;
`build/roadmap-completion-20261013/integration-commit-binding.json` retains
that binding. Notification correction `5589ec3dc` is also pushed. The new
EventQueue observer and pending mapping-admission work are not covered by
those older passing builds.

- Generic EventQueue instrumentation now retains move-owned observation from
  admission through pending/saved/retrieved storage and completed dispatch,
  including the real InputFilter's nested borrowed view. The two counters
  explicitly serialize `capacity:null`. Late observer installation, abandoned
  events, exceptions and stamp mutation invalidate evidence. Normalizer
  **38/38** and analyzer **35/35** pure cases pass, including the complete
  19-entry declared Core/Host inventory. Source hashes and strict syntax checks
  are in `build/event-queue-observer-20261014/freeze.json`. Commit `350d3cc67`
  includes that freeze and the integrated target/CI configuration. This does not establish configured queue
  bounds, platform/kernel coverage, native provenance or H11 completion.
- Actual Mac full build **PASS** after adding the missing `src/lib` include path
  to the standalone code-admission fixture. `mac-build.log` retains the first
  failed build; `mac-build-2.log` records the passing rebuild. Enabled CTest is
  **39/40 PASS** with two assertions failing inside the new EventQueue suite:
  they check unread diagnostic rows instead of outstanding input. Test-only
  commit `0cb99c4f3` now checks final counter depth and enqueue/retirement equality.
  A targeted EventQueue rebuild/retest **PASSES**. This is 39 passing suites
  plus the corrected suite's pass, not a rewritten all-green original run.
  Evidence: `build/roadmap-completion-20261014/mac-tests.{log,xml}` and
  `mac-event-test.{log,xml}`. OFF and observer sanitizer runs are still pending.
- GNOME map admission `81efdc5a5`: an immutable, single pending scalar/batch
  slot removes the state-mutex contention refusal. Worker admission retains
  exact seal/request identity and every original deadline; unread completion,
  cancellation, terminal failure, stale replies and ID exhaustion remain bounded.
  The real private-bus regression fails before the correction. Strict compilation
  and scoped ASan/UBSan then pass **21 groups / 10 new scenarios** in 5.502 s,
  without GLib warnings; leak detection is disabled and prebuilt dependencies
  are not fully instrumented. Evidence/hashes:
  `build/gnome-map-admission-20261014/verification.json`. Native testing on the
  home Ubuntu and the repeated focus matrix are still due. The earlier separate
  private-channel failure remains unresolved.

- `association-noop-matrix-1`: 12 planned fresh sessions, **11 PASS / 1 FAIL**,
  without retries. Ordinary-click scale 1.25/run 1 fails on the actual client's
  `reason=lock-contention` admission refusal. Values printed as zero while its
  mutex is unavailable are placeholders, not observations of empty state.
  This is an availability failure; it does not establish misdirected input.
  The unchanged-notification revocation does not recur. Source archive
  `9775730a5040c9a4a4376553b193a653ec297563e0257dc3d4bb229d4681fb61`,
  manifest `5465aa7d9657048641c99d690b7adbf4f0231cba1eb6ebe4586eabd6015b0554`.
  All **268 artifact hashes** and preservation/cleanup checks pass. Evidence:
  `build/wifiman-resumed-20261013/ubuntu/association-noop-matrix-1/`.
- [CI run 38577676662](https://github.com/JackHars/deskflow/actions/runs/38577676662)
  on `5589ec3dc`: native GNOME, GCC, Clang and transport pass. Both Mac and Linux
  application builds/tests pass, then their TLS reconnect fixture rejects the
  new `mapped-input-v1` capability. This is reproduced locally against the
  actual Mac Viewer and corrected by accepting its exact advertised set;
  unknown sets, wrong order and duplicates still reject. The same binary
  passes the corrected real TLS reconnect test, without a native Host or input
  hooks. Logs: `build/roadmap-completion-20261014/viewer-smoke-before-app.log`,
  `viewer-smoke-after.log`, `ci-linux-app-38577676662.log`, and
  `ci-mac-app-38577676662.log`. Commit `b10d05cbd` fixes this fixture; the
  original CI run remains failed and later skipped stages remain unrun.

## Current local completion checkpoint — 2026-10-13 (working tree)

Current validation is split by the actual source checkpoint:

- Final integrated source checkpoint `final-integration-2`: actual Mac
  application/Core build and **39/39 enabled suites PASS**, followed by actual
  ordinary Core build and **3/3 disabled compatibility suites PASS**. Actual
  Ubuntu application/Core build, **36/36 enabled suites** and **4/4 disabled
  suites PASS**. Actual Core/EiComputer and the broader GNOME/Qt first-click,
  batched-map and two-window tests each pass at **scales 1 and 1.25**. This
  includes the collector and its real 96-frame private-buffer regression.
  Source archive:
  `f622a09a51c1a57f18071046ac7271ea4c325cd2d9bd00411bda730e52b99ee2`;
  manifest `2c6e01814157f30803a7670cb524ef029985cfe709434ee6283474822ef7aacd`
  covers 1,098 files. All **130 returned artifact hashes**, runner identity,
  ordinary-process/configuration preservation, unrelated-container preservation
  and owned cleanup checks pass. Mac logs are
  `build/roadmap-completion-20261013/final-mac-{build-2,tests,off-build,off-tests}.log`;
  Ubuntu evidence is under `build/wifiman-resumed-20261013/ubuntu/final-integration-2/`.
  Mac was macOS 26.1 with Qt 6.8.3; Ubuntu used the retained pinned
  Qt 6.10.2/GNOME 50.1/Mutter 50.1/libei 1.5 image. Physical H tests remain unrun.
- The subsequent exact-library signed Mac package includes the integrated
  collector and physical-source controls. Bundle audit and private Core/Host
  handshake/rejection smoke **PASS**. Archive SHA-256:
  `5129765642ee36b0df1354d458fb4a4974663c8c87febe67bb34ba3e255e7b65`.
  Evidence: `build/roadmap-completion-20261013/final-mac-package-1/evidence.json`.
  This is a separate staged package; no installation or native input occurred.
- Current private-component ASan/UBSan run passes input-evidence, code-admission,
  preflight and bootstrap; local-input initially fails at identity authentication.
  The original failure did not preserve its channel/child reason. A failure-only
  diagnostic change leaves all waits and deadlines unchanged. One diagnostic
  local-input run then **PASSES** (82.19 seconds), so the original cause is
  **unresolved, not reproduced**. No sanitizer violation was reported in these
  logs. Retained results and hashes:
  `build/roadmap-completion-20261013/final-private-sanitize-evidence.json`.
  This is four passing suites plus a separate diagnostic pass, not a claim that
  the initial five-suite run passed. Qt and prebuilt dependency archives are
  not fully instrumented.
- The fixed `focus-recovery-reasons-matrix-1` diagnostic completes 12 sessions
  using the integrated candidate's exact binaries and new diagnostic messages.
  Ten pass; ordinary-click scale 1/run 2 and scale 1.25/run 2 fail. Both failures
  follow a selected-window `notify::wm-class` whose complete class/instance
  pair did not change. This unnecessarily revokes an otherwise live native
  seal; commit `5589ec3dc` supplies the narrow fix. Earlier channel/admission failures did not
  recur and their causes remain unresolved. All 269 artifact hashes and
  preservation/cleanup checks pass. Source archive:
  `56846827097db715def1dc4ba2a1bc2b22a3ebb60db1cd1937e8de5382672e3f`.
  This diagnostic retains **FAILED** overall status; no failed session was retried.
- Controlled focus-recovery diagnostic: `focus-recovery-matrix-1` reuses
  the verified first-click binaries for 12 fresh private sessions: three runs
  per scale (1/1.25) and recovery mode. All six prepared-first-click runs pass;
  three of six ordinary-click runs fail. One failure precedes either recovery
  mode, so these counts do not establish a causal fix. The failures identify
  a window-association invalidation, a hidden private-channel error and an
  uncategorized map admission refusal. Additional bounded diagnostics preserve
  those reasons without changing acceptance or timing. The source archive is
  `f4d01f6cb5f7edcba6711ac94f0dcc3683c9fe57c87fee96a1ac0824366090a4`;
  all 269 artifact hashes and preservation/cleanup checks pass. The diagnostic
  retains **FAILED** overall status and does not rerun failed sessions.
- The connected opt-in input recorder and production physical harness are
  now implemented. The recorder observes source copies before their first
  queue, 13 Core queues and four Host queues, and the disposable source's
  native event fields. It requires an explicit private configuration and a
  visible one-shot Record request. Capture/start races, intermediate queue
  mutations, replacement owners and diagnostic loss invalidate the recording.
  Generic Core EventQueue and platform/kernel queues remain unmeasured;
  normalized output explicitly reports incomplete queue coverage and cannot
  certify H11. **34 normalizer, 34 Mac-harness and 23 Linux-harness pure tests
  pass**. Native provenance, physical delivery and H11 remain **NOT_RUN**.
  See [input evidence](deploy/windowshare/input-evidence.md) and
  [production physical verification](deploy/windowshare/original-input-physical.md).
- Ubuntu first-click integration: `first-click-abi-timer-1` builds actual
  Core/Viewer, passes **35/35 enabled and 4/4 disabled suites**, including the
  cross-target EI layout/construction regression, and passes native GNOME/Qt
  first-click plus overlapping exact Viewer associations at **1 and 1.25**.
  Archive: `78f1d2905e605fbb9040da245c21aea02e9f5d66e617b9a1ca18bd45e6ddbfdc`.
  The initial Core test completed input checks but failed its SIGTERM-based
  teardown; that overall failure is retained.
- Actual Core native retest: `first-click-core-ipc-stop-1` reuses those exact
  verified binaries, changing only the Python harness to the supported private
  IPC stop with exact kernel peer credentials. **Both scales PASS**, including
  real EI idle stop/restart, IPP1 focus-only preparation, IPC5 All promotion,
  exact asynchronous cleanup, fresh key pair and exit 0. Archive:
  `56058d584cbf1ded23e24ef7a44ac1866179567fa424e5b6e75bddaa9d73c61a`.
  All 69 returned artifact hashes, source/runner checks and preservation/cleanup
  checks pass. These are private native integration results with synthetic Core
  source correlation; physical Mac capture/original delivery remain NOT_RUN.
  Neither checkpoint includes the subsequent H11 collector integration.
- Mac first-click `IPP1`: actual Core build and six suites PASS, **234 Qt
  entries**, zero failures/skips. The final atomic admission/cleanup correction
  has a subsequent Core/coordinator relink and 28/28 coordinator entries PASS.
  Logs: `build/roadmap-completion-20261013/first-click-core-{build,tests}.log`
  and `first-click-admission-{build,tests}.log`. The fixture uses real packet
  decoders and an authenticated private Host child; it posts no OS input.
  Linux/native first-click validation for the named checkpoint is recorded above.
- First-click scoped ASan/UBSan: coordinator **28/28 Qt entries PASS**. See
  `first-click-coordinator-sanitize/evidence.json` and `test.log`; Coordinator,
  private channel/preflight, sender proxy and fixture are instrumented. Qt,
  native capture and the named dependency archives are not fully instrumented.
  The separate `first-click-proxy-sanitize/` run also passes **62 ClientProxy
  and 44 ServerProxy entries** with changed proxy, Client and Computer sources
  instrumented and dependency archives/Qt not fully instrumented.
- Untouched-source rollback: native, holding and default pure drag-observation
  suites PASS, **3/3**. See `untouched-source-followup-build.log` and
  `untouched-source-tests.log` in that directory. A canceled preparation that
  never moved the source leaves the user's current placement unchanged.
- H11 offline analyzer: **31 synthetic parser tests PASS**, including retained
  failing-before/passing-after evidence for aggregated queue counters hiding
  transient peaks. Logs: `h11-aggregation-before.log`, `h11-evidence-tests.log`.
  [Collection and analysis procedure](deploy/windowshare/h11-evidence.md).
  No measured performance or H11 completion follows from parser tests.
- Subsequent application/capture-rate build: both Mac applications PASS and
  **10 selected suites PASS** (nine in `first-click-app-capture-tests.log`,
  native timing/recovery in `capture-rate-native-tests.log`). This covers the
  first-click Viewer/Bridge gate, configuration propagation, strict 30/60 rate
  selection, original real-frame timestamps, and bounded recovery replay.
  Repeated recovery requests cannot advance synthetic time enough to starve
  the next real frame. No live capture or measured 60 fps delivery was tested.
- Current staged Mac package: dependency/signature audit and private Core/Host
  handshake/rejection smoke PASS. `mac-package/evidence.json` records the ad-hoc
  archive SHA-256 and commands; nothing was installed and original input stayed
  disabled. This does not validate exact-library native input admission.
- Subsequent exact-library Mac package: audit and private handshake PASS under
  `mac-exact-package/`, archive SHA-256
  `a5940a8cea7efc07d0bf4919d6f5fc160782ec06e2cd08837664fca2213a3867`.
  Its fixed executables include both Core and its WindowShare companion.
  No installation or native input was performed. Packaging regression checks
  also pass: 11 Linux archive tests and 14 Mac inventory/signing/parser tests;
  see `build/roadmap-completion-20261013/packaging/`.
- Ubuntu `first-click-provider-1`: actual Core/Viewer build and **34/34 enabled
  suites PASS**; native scale 1 **FAILS** at first-click focus publication after
  one libei idle stop/restart. The reported reason is `GNOME preparation focus
  changed before publication`. Scale 1.25 and OFF checks were not reached.
  Source archive is `8285ea7842c1d2bd6d2e3537060767ed28da9a60452bfae5f2cf479a8c6d9386`.
  Hash-verified logs and cleanup/preservation evidence are under
  `build/wifiman-resumed-20261013/ubuntu/first-click-provider-1/`.
- Actual Core native harness: `python3 deploy/windowshare/gnome-core-input-tests.py
  --self-test` passes **9 pure wire/transcript/process-state tests**. Its native mode requires
  the disposable GNOME container, actual enabled Core and Viewer binaries, an
  absolute new output directory and `--scale 1` or `--scale 1.25`. It reports synthetic Core correlation
  separately from actual Core/EiComputer execution and grants no original-input
  delivery authority.
- Ubuntu `first-click-core-1`: actual application/Core build and **34/34 enabled
  suites PASS**, then real Core **SIGSEGV before IPC5/IPP1 negotiation**. Source
  archive `1216087903edd450ee9d3243e1ee7674d3d3c381ee227bc1e403d645346979d4`.
  `first-click-native-diagnostic-1` reuses the exact verified binaries and
  records signal 11 before cleanup, separating the Core crash from the resulting
  Viewer disconnect. Its independent GNOME/Qt run verifies completed activation
  after one deferred restack, then fails on an obsolete fixture PONG timer.
  The timer correction has 39 pure fixture checks and the later native rebuild
  passes, as recorded above. The focus-recovery option adds six pure checks.
- `first-click-core-backtrace-1`: isolated parent debugger run against the same
  Core SHA-256 `81aeeacef30f0e9ab8d43a7dcb2ed3bc6737d0572d6ac90c5d1a92ac9f60b7e7`
  identifies invalid mapped-cutoff state in `EiComputer::handleSystemEvent`.
  `DESKFLOW_GNOME_EI_PING` changed the class layout but was private to the platform
  target, leaving app allocations undersized. The corrected public definition
  and cross-target regression now pass as recorded above. This is diagnostic
  evidence, with no acceptance claim or new
  ON/OFF suite run. Source/runner/artifact hashes and cleanup/preservation pass.

- Earlier IPC5 batching/clipboard/multi-window Mac Core and application build: PASS. Under
  `build/roadmap-completion-20261013/`, `mac-core-native-tests.log` records 11/11
  suites and `mac-application-tests.log` records 7/7 suites. The initial probe
  compile error (ambiguous native `Rect`/`Point`) is corrected and its follow-up
  build/default pure test pass. No input was posted into ordinary applications.
- Scoped ASan/UBSan: Server 45 Qt entries, coordinator 15 entries PASS. Each
  evidence file records actual sources/archives; Qt and dependency archives
  are not fully instrumented.
- Ubuntu `sealed-multiwindow-3`: build and 34/34 enabled suites PASS. Native
  GNOME scale 1 passed batching and two real Viewer roles. Scale 1.25
  failed with `stale-revision` after the multi-window sequence. OFF was not
  reached. This is not an overall passing run. Source/archive and returned
  artifact hashes, ordinary-process/config preservation, unrelated-container
  preservation and completed cleanup were independently verified.
- Ubuntu diagnostic overlay `sealed-multiwindow-4`: build and 34/34 enabled
  suites PASS, native scale 1 FAILS when the second Viewer's retained seal is
  invalidated during an idle-point check. This precedes scale 1.25 and OFF.
  Archive `e12b4c37f9e32784b1e2fe2002115ece403d161253e265f9dbd642b8885b029f`
  contains only a bounded diagnostic change over the prior snapshot. Collector
  hashes, preservation and cleanup checks pass. The native invalidation cause
  remains under investigation; this run is not an overall Linux PASS.
- Frozen earlier Ubuntu candidate `mapped-coordinator-1`: 33/33 enabled suites,
  3/3 disabled suites and private GNOME/Qt scale 1 and 1.25 PASS. Its source and
  82 returned artifact hashes were independently verified. See `WORK_STATE.md`
  for the archive identity; this is not a result for later working-tree edits.
- Actual physical input, native edge drag, two physical monitors, application
  compatibility and a long resource/latency soak: **NOT_RUN** for this candidate.

The next final run must cover IPC5 exact batched mappings and source-order
private delivery, point expiry with a live sealed prefix, idle stationary holds,
full cleanup, the actual EI production object, and fresh private GNOME runs.
Final evidence must bind the frozen archive/manifest to the implementation
commit and record ordinary-process/configuration preservation and cleanup.

Engineering acceptance targets for H11 (defined before measuring, not claimed
results): a 120 Hz mixed pointer/drag/scroll stream for two minutes must have no
missing/reordered key, button or scroll records, no duplicate native receipts,
no growing pending-input queue and no deadline revocation. Hover compression
must be separately counted. On a wired LAN with one 1080p60 stream, initial
targets are p95 capture-to-displayed-frame <=100 ms and p99 <=150 ms; measure
that with a shared-clock or filmed stimulus/response procedure, not unrelated
machine clocks. Retain the previously defined 60-minute CPU/RSS budgets and
read-only collector in `deploy/windowshare/resource-soak.md`; add independent
bandwidth, GPU, frame/drop and held-input evidence as that procedure requires.
Queues must remain bounded and every visible stream must keep progressing.
Qt-scale fixtures do not measure these targets.

## Requested Core cutoff and exact GNOME/Qt drain — 2026-10-13

Implementation: [`bbe9ed3f2`](https://github.com/JackHars/deskflow/commit/bbe9ed3f2fb4d8519397bca61bb5b539082d00f6).
P2 components are implemented; the original-input route remains disabled.

**Hosted PASS:** [CI 38420241004](https://github.com/JackHars/deskflow/actions/runs/38420241004)
passed all six jobs at that exact implementation: Mac application/core,
Linux application/core ON/OFF, native GNOME, transport, and GCC/Clang sanitizer
foundations. The [source bundle](https://github.com/JackHars/deskflow/actions/runs/38420240757)
also passed. Retained hosted native reports independently show both scales
passing and gate revocation with 183 ms remaining in each run.

The optional Core `IPC1` capability retains its actual authenticated peer and
nonreused generation. `DICQ` carries the exact capture/full-forwarding/Viewer
binding and remaining deadline; prior compressed motion is submitted before
the real EI backend starts its requested ping. Held native keys/buttons,
invalid session/device/emulation, same-batch loss, malformed/stale requests and
expiry deny. `DICX` resumes that exact transaction; new writes revoke the old
receipt before the ordered resume. Old peers and feature-disabled builds do
not negotiate the new capability.

The separate owned GNOME operation validates that binding and frozen point,
waits for an empty Clutter queue outside recursive dispatch and a later
completed same-view frame, then asks the actual Qt Viewer for `input-drain`.
Two default-queue syncs, QPA dispatch, native/paint/focus checks and the exact
All gate share the inherited deadline. Input/IME-query/native handler depth,
handler ownership and supported disabled input-context checks prevent nested
or unsupported delivery from being mistaken for completion. Gate resume
revokes pending and retained snapshots before acknowledgement.

**Mac PASS:** both applications built; nine targeted CTest suites passed,
including five Core suites with **180 Qt entries**, zero failures/skips.
AddressSanitizer/UndefinedBehaviorSanitizer runs passed **54 ClientProxy** and
**36 ServerProxy** entries. Those runs instrument the changed proxy, Client
and Computer implementations plus tests; dependency archives and Qt are not
fully instrumented. The fixture policy suite passes **28 tests**, including
red/green proof for explicitly requested TLS shutdown handling.

**Home Ubuntu PASS:** 31/31 integrated application/core suites and 3/3 actual
`BUILD_WINDOWSHARE=OFF` suites, with no failed/skipped CTest suites. The disabled
build intentionally skips 33 opt-in per-case checks and passes 32 Qt entries.
GNOME policy (20 cases), readiness and fixture policy (28 cases) pass. Actual
private GNOME/Qt acceptance passes at scales **1 and 1.25**. Retained receipts
were revoked with **165 ms and 156 ms** remaining, respectively, and runtime
logs confirm the built-in `QtWaylandClient::QWaylandInputContext`.

The isolated dependency image uses Qt 6.10.2, GNOME Shell 50.1, Mutter
50.1-0ubuntu2.4, libei/libeis 1.5.0 and GCC 15.2. Actual compiled EI production
objects were independently checked for ON/OFF feature flags. Before/after
process births/configuration hashes and final independent cleanup confirm
ordinary Deskflow and unrelated containers were preserved; the owned container,
supervisor and wrapper are gone. No host desktop or input devices were used.

The frozen candidate and every changed implementation/regression file match
this commit, verified by `ubuntu/final-candidate-2/commit-binding.json` under
`build/wifiman-resumed-20261013/`. Archive SHA-256:
`219d8d2ae62d02ca4978c017df49a34a79dcaf39f7ddb9de2e16cff31b73a58c`.
`evidence.json`, hash-verified `artifacts/`, full logs/JUnit/native reports,
runner identities and real compile commands retain the result. Mac evidence
is in `mac/final-results.json`, `mac/final-ctest.xml` and `mac/proxy-sanitize/`.
Hosted job metadata, logs and native artifacts are in `ci-38420241004/`.
The preceding candidate passed all 31 application/core suites and scale-1
native drain assertions, then failed on a `BrokenPipeError` during requested
Viewer shutdown. The harness now accepts only the expected EOF/reset/broken-
pipe exceptions after that shutdown request. Protocol errors, timeouts and
unexpected disconnects still fail. The original failure and cleanup evidence
remain under `ubuntu/final-candidate-1/`; it is not recorded as a passing run.

Reproduce the changed paths after installing dependencies from the workflow:

```sh
cmake -S . -B build/input-cutoff -DBUILD_WINDOWSHARE=ON -DBUILD_INSTALLER=OFF -DBUILD_TESTS=ON -DSKIP_BUILD_TESTS=ON
cmake --build build/input-cutoff --parallel 4 --target deskflow-core deskflow-windowshare InputSequenceTests ComputerInputSequenceTests ClientProxyTests ServerProxyTests ServerTests windowshare-ei-prefix-probe-tests windowshare-viewer-viewport-tests windowshare-viewer-tests windowshare-bridge-tests windowshare-gnome-client-tests
QT_QPA_PLATFORM=offscreen ctest --test-dir build/input-cutoff/src/unittests --no-tests=error --output-on-failure -R '^(InputSequenceTests|ComputerInputSequenceTests|ClientProxyTests|ServerProxyTests|ServerTests|windowshare-(ei-prefix-probe|viewer-viewport|viewer|bridge|gnome-client))$'
node --test deploy/windowshare/gnome-provider/tests/logic.test.js
python3 -B deploy/windowshare/gnome-qt-viewer-fixture-tests.py
bash deploy/windowshare/gnome-provider-container.sh "$PWD" "$PWD/build/input-cutoff-native"
cmake -S . -B build/input-cutoff-off -DBUILD_WINDOWSHARE=OFF -DBUILD_INSTALLER=OFF -DBUILD_TESTS=ON -DSKIP_BUILD_TESTS=ON
cmake --build build/input-cutoff-off --parallel 4 --target deskflow-core ServerTests ClientProxyTests ServerProxyTests
QT_QPA_PLATFORM=offscreen ctest --test-dir build/input-cutoff-off/src/unittests --no-tests=error --output-on-failure -R '^(ServerTests|ClientProxyTests|ServerProxyTests)$'
```

Expected native results include exact positive cutoff echoes, wrong-gate and
wrong-Viewer-connection refusal, refusal after resume, a new nonreused cutoff,
and retained-receipt revocation **before expiry**. The fixture records the
actual Qt input-context messages. These tests use a private compositor and
synthetic Core correlation; they do not execute the entire production
ClientProxy → ServerProxy → real EiComputer → provider chain.

**Remaining P2/P3:** connect the Core result to the provider using the same
Server frozen point and retained preparation lease, finish old-owner releases,
verify the supported Mutter build's sender-ordering prerequisite, and install
the production dispatcher only with all required evidence. Provider receipts
still explicitly report `mutter-eis-sync-build-unverified` and no original
input authority. An imported `eis_event_ref` symbol in the installed Mutter
binary is not sufficient runtime attestation. H01 is partial; physical H02–H12,
packaging installation, multiple monitors/windows and edge-drag remain pending.

## WiFiman-restored home Ubuntu baseline — 2026-10-13

**PASS at exact source `2a72867e95892011a1597ecc811c5279227edf4c`:** 31/31
integrated application/core suites, 3/3 actual `BUILD_WINDOWSHARE=OFF` core
suites, GNOME provider policy/readiness/fixture tests, and real private native
GNOME/Qt acceptance at scales 1 and 1.25. The code matches `c867af9d0`; the
remaining changes in this baseline are documentation. No CTest suite failed
or skipped. The disabled suites retain their intentional per-case trace skips.

Host: Ubuntu 26.04.1, kernel 7.0.0-34-generic. The already-installed dependency
image was pinned to
`sha256:65f389a94c05ac9eb2b72d62424d80a52650849918582b0bacc9f12507290086`.
Its disposable run used a read-only root and source mount, private HOME/XDG,
no network, no capabilities, no host display/GPU/input devices, and bounded
CPU/memory/process/time limits. Native sessions were private headless GNOME
instances. Ordinary Deskflow process births/configuration hashes and unrelated
Docker containers were unchanged; independent final cleanup passed.

Evidence under `build/wifiman-resumed-20261013/ubuntu/baseline/` includes the
frozen source manifest/archive hash, runner hashes, launch identity, actual
container inspection, complete build/test logs/JUnit, both native reports,
binary hashes, supervisor exit and independently verified cleanup. Start with
`evidence.json`; exact argv for each phase is in `artifacts/result.json`.
H01 remains partial because fresh experimental packaging/native capability
acceptance is separate. H02–H12 remain unrun on this baseline.

## Local return: deferred preflight and crash recovery (2026-10-13)

Earlier implementation: [`c867af9d0`](https://github.com/JackHars/deskflow/commit/c867af9d029897f860bc391aa68d4cafee49e601).
Its optional private-preflight notification holds the exact lease in Quiesced
until one explicit continuation is dequeued. Notification and continuation are
scheduling only: they do not grant input permission. The existing private
two-second deadline never restarts or extends the Server's 500 ms budget.
All prior admission, selected-peer, capture, delivery and neutrality checks
remain mandatory. Callers without the notification keep synchronous behavior.

**Local Mac results: PASS** on macOS 26.1 (25B78), Qt 6.8.3, CMake 4.4.4,
Debug build with the MacOSX15.5 SDK and `BUILD_WINDOWSHARE=ON`. Both `deskflow-core` and `deskflow-windowshare`
rebuilt. Five core suites passed 170 Qt entries; seven selected application
suites passed. After the continuation change, all three private preflight,
channel and companion suites passed again (43.53 seconds). The new cases cover
held full-scope quiescence, direct-channel bypass, exact continuation, duplicate
or mismatched leases, cancellation followed by a new lease, provider/capture
loss, unchanged timeout, bounded queue overflow, nested event processing,
throwing callbacks and coordinator/channel destruction. No production native
cutoff provider or original dispatcher was enabled.

Watchdog fix [`7bc8cf332`](https://github.com/JackHars/deskflow/commit/7bc8cf3320cf7163c7bb5476d97594e189793049)
has native **FAIL-before / PASS-after** evidence. The regression keeps a
test-owned exited host unreaped, requires its independent watcher to recover,
and reaps both owned children on success or failure. Recovery writes only a
temporary marker; it does not access AX, a real journal or the desktop.
The old code failed in 3.60 seconds; fixed code passed in 1.75 seconds, with no
leftover fixture processes. The receipt-policy helper also passed 13 tests.

Reproduce in the existing configured Mac checkout (a fresh checkout must first
use the Qt/dependency/SDK configuration from the workflow):

```sh
.venv/bin/cmake --build build/mac-default --parallel 4 --target deskflow-core deskflow-windowshare windowshare-preflight-tests windowshare-local-input-tests windowshare-companion-tests windowshare-recovery-watchdog-tests
QT_QPA_PLATFORM=offscreen .venv/bin/ctest --test-dir build/mac-default/src/apps/windowshare --output-on-failure --no-tests=error -R '^windowshare-(preflight|local-input|companion|recovery-watchdog)$'
QT_QPA_PLATFORM=offscreen .venv/bin/ctest --test-dir build/mac-default/src/unittests --output-on-failure --no-tests=error -R '^(ServerTests|ClientProxyTests|ServerProxyTests|InputSequenceTests|ComputerInputSequenceTests)$'
python3 -B deploy/windowshare/physical-macos-tests.py
```

Evidence: `build/cloud-return-20261013/mac/` contains build logs, CTest output
and JUnit, the red regression patch, source hashes and `results.json`.
`ci-38410153483/` retains the current six-job successful hosted run for `c867af9d0`;
Linux application 13/13, actual core ON 8/8 and OFF 2/2, GCC/Clang foundations
12/12 each, and native GNOME all passed. The current continuation's
[CI 38410153483](https://github.com/JackHars/deskflow/actions/runs/38410153483)
**PASS 6/6 jobs**, including the new continuation and watchdog regressions;
[source bundle 38410153441](https://github.com/JackHars/deskflow/actions/runs/38410153441)
passed. Hosted GNOME uses a private headless compositor, not the user's Ubuntu
desktop. Home SSH timed out with no WiFiman route. H01 is only partially
performed; H02–H12 remain NOT_RUN for this candidate. Existing App Management
and fresh physical-readiness prerequisites still apply to local installation
and native interaction.

The new cloud StageView checkpoint below is retained. An accidental exact copy
of the older history embedded inside its CTest command was removed and that
command repaired; no test result was inferred from the document repair.

## P2 native StageView two-frame observation and strict D-Bus schema — VERIFIED (2026-10-13)

**Exact tested implementation SHA:** [`e117b66e22e7f2141b4a17b93f81e22c736f5b4c`](https://github.com/JackHars/deskflow/commit/e117b66e22e7f2141b4a17b93f81e22c736f5b4c),
branch `feature/seamless-windows`. This includes the actual GNOME provider
`ObserveStageFrames` implementation from `28021a31`, native boxed-layout
lifetime fix `9c5300dd`, missing D-Bus fixture-method signature
`2936a180`, real GJS `view.layout` property adaptation `664df3b1`,
and strict returned dictionary validation `e117b66e`. Related macOS
receipt fixture `56107d94` now *atomically replaces* intentionally
malformed JSON, avoiding test-created partial writes while still refusing
duplicate keys. A separate concurrent Mac watchdog improvement `7bc8cf33`
is included in this source but does not prove physical recovery.

**Actual hosted execution:**
[GitHub CI 38408250298](https://github.com/JackHars/deskflow/actions/runs/38408250298)
**PASS 6/6 jobs**, zero failing jobs: macOS application/core, Linux
application/opt-in ON and ordinary Deskflow OFF core, GNOME private native
Shell/Mutter/QtWayland headless integration, transport, GCC and Clang sanitizer
foundations. The native GNOME regression previously **FAILED** with
`unsupported-stage-view-layout`; reading Mutter 50's introspectable
boxed `view.layout` property (instead of a zero-argument `get_layout()`)
resolved it without changing the native hit or lease criteria. The private
C++ GIO fixture previously **FAILED** because its introspection lacked
`ObserveStageFrames`; the exact real method signature is now declared.
The [source bundle run 38408250240](https://github.com/JackHars/deskflow/actions/runs/38408250240)
**PASS** for that same implementation SHA.

**What actually executes:** A capability-negotiated original frozen
GNOME compositor-stage coordinate is validated against the owned
single-surface Viewer and session/revision, then its real same-view Clutter
`before-update`/`after-update` signals are observed across two paired
frame updates within an absolute 120 ms local stage deadline and the
250 ms overall viewport budget. The GJS callback reads the native
`StageView.layout` property and snapshots only its four primitive
coordinates. The GLib client requires the **exact 13 unique typed**
reply fields; unknown keys, duplicate keys, wrong types, substituted
request/session/view, mismatched stage point, premature or nonmatching
frames, and unsupported provider methods are denied rather than using a
fallback or treating an observation as input permission. The full native
QtWayland default-queue observation and post-Qt frozen-hit recheck are
still required before a read-only viewport snapshot is returned.

**Reproduce cloud regression** (on the supported GNOME/Mutter 50 and exact
QtWayland versions, using dependencies in the GitHub workflow):

```sh
node --test deploy/windowshare/gnome-provider/tests/logic.test.js
cmake -S src/apps/windowshare -B build/app -DWINDOWSHARE_APP=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/app --parallel 2
ctest --test-dir build/app --output-on-failure -R '^windowshare-(gnome-client|viewer-viewport|viewer|bridge)$'

bash deploy/windowshare/gnome-provider-container.sh "$PWD" "$PWD/build/gnome-provider"
```

The first CTest covers the private D-Bus service, malformed or missing
methods, session/epoch/routed coordinate/revision and strict dictionary
types. The final native script exercises the real owned GNOME headless
compositor and painted Qt Viewer in isolation. Always retain the exact SHA,
command, runner versions, logs and failures when executing local tests.

**Safety/acceptance:** P2 remains **INCOMPLETE**, as two Clutter update pairs
are not identified with any particular prior libei/EIS injected input-event
prefix, and no authoritative backend → compositor event consumption →
Qt application ordering has been established. The earlier libei PONG is only
a sender/batch diagnostic; Wayland `wl_display_sync` and Qt
`sendWindowSystemEvents` are different observations, not downstream
Mac application receipts. Provider `deliveryReady=false`,
`syncFenceVerified=false`, and `pointerDeviceAttribution=false`
remain enforced. No original-input dispatcher has been installed, and
P3 original key/button/pointer/scroll delivery is **DISABLED**.
Physical keyboard, mouse, holding display, monitor scaling, all H01–H12:
**NOT_RUN**. This is a verified read-only diagnostic improvement, **not**
completion of `CHATGPT_HANDOFF.md`.

## P2 final post-Qt GNOME frozen-point recheck (2026-10-13; verified code checkpoint)

Runtime [`32cb0cb9`](https://github.com/JackHars/deskflow/commit/32cb0cb9c5e809f8884a6d53a2f8324d64418aa7) and regression correction [`13e62c02`](https://github.com/JackHars/deskflow/commit/13e62c02d3417b162264256f12641f36300d1f46). [Native CI 38386740966](https://github.com/JackHars/deskflow/actions/runs/38386740966) **PASS 6/6** jobs: native macOS app/core, Linux app/core ON/OFF, GNOME headless, transport and GCC/Clang sanitizers. Linux application **13/13 CTest suites**; GCC and Clang sanitizer foundations **12/12** each; integrated Linux ON **8/8** and OFF **2/2**. [Source bundle 38386740929](https://github.com/JackHars/deskflow/actions/runs/38386740929) **PASS**. No user's physical Mac/Ubuntu tested.

**Connected runtime:** Following a frozen routed-point/native hit and Qt's asynchronous serial default-input-queue `Check`, the GNOME provider is queried *again* for the **same** routed point, native owner/window/surface, revision, session and request identity, all inside the original 250ms deadline. If the final hit's native transform or point has changed, deny publication. The real private-bus fixture tests a silent native revision change after Qt completion (no Invalidated signal) and silent transform drift with unchanged tokens. Both reject old observations without creating input-delivery authority; valid later snapshots may establish their own fresh identity.

**Cloud reproduction** (use dependencies and Qt-version environment from `.github/workflows/windowshare-foundation.yml`):

```sh
cmake -S src/apps/windowshare -B build/app -DWINDOWSHARE_APP=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/app --parallel 2
ctest --test-dir build/app -R '^(windowshare-viewer-viewport|windowshare-gnome-client|windowshare-bridge)$' --output-on-failure
```

Run real private GNOME/QtWayland headless integration with `bash deploy/windowshare/gnome-provider-container.sh "$PWD" "$PWD/build/gnome-provider"`. Its exact version/extension prerequisites are also checked by the GitHub GNOME job.

**P2 is not complete.** This is a read-only GNOME/Qt native-hit freshness observation. It does not prove that EIS events reached/left Mutter's Clutter event queue, were dispatched into the correct Qt widget, or were consumed by the Mac application. No compositor event-drain/physical receipt has been established; no original-input authorization/registration is enabled. P3, full multi-window, bidirectional edge drag, app composition and hardware H01–H12 remain open/NOT_RUN.

## P2 QtWayland default queue observation (`d0b8e8a3`; historical verified slice)

Implementation [`d0b8e8a3`](https://github.com/JackHars/deskflow/commit/d0b8e8a3181dc70fd2ea462e261e3a3685704a70) installed an asynchronous, owned `QtWaylandQueueWitness` on the **existing QtWayland default GUI input queue**. It chains two nonblocking `wl_display_sync` callbacks, drains GUI-side queued QPA window-system events, then rechecks current painted geometry, focus and native surface before replying to the authenticated private viewport `Check`. A 200ms local timeout is inside the existing 250ms snapshot deadline. Unsupported Qt versions/private APIs, callback reentry, invalidation, destruction and stale state fail closed; `sendWindowSystemEvents` boolean is never treated as a receipt. No new Wayland connection or input endpoint is opened. [Hosted CI 38383713813](https://github.com/JackHars/deskflow/actions/runs/38383713813) **PASS all six**; [source bundle 38383713764](https://github.com/JackHars/deskflow/actions/runs/38383713764) **PASS**. Upstream compositor input consumption and physical H03–H05 remain **NOT_RUN**.
## P4 monitor-identity/mode restore safety — tested source `ec2fd2f8` (2026-10-13)

**Implemented:** The experimental Mac native host retains one validated
physical-output fingerprint (CoreGraphics display ID, bounded logical frame,
native pixel-mode dimensions and exact quarter-turn rotation), excluding its
owned virtual holding display. The same output set is compared independent of
CG enumeration order before any new export, before source-window relocation,
after virtual display creation, and in the existing 500 ms owner-thread safety
poll. Missing/invalid outputs, replaced IDs, negative-origin changes, rotation
or a changed native pixel mode are **not** proof of retained identity. On loss
the host revokes its exact held owners and calls `restoreAll(AuthorityLost)`;
physical output selection tries to return each source to a surviving actual
display, preserving unresolved records in the durable journal. It never uses a
peer-supplied display ID, arbitrary new focus target or raw-native-input
endpoint. Original-input delivery remains disabled.

**Executed, not inferred:**

- Commit: [`ec2fd2f83fa6a308f1702036eb0bf4f3ff6dc079`](https://github.com/JackHars/deskflow/commit/ec2fd2f83fa6a308f1702036eb0bf4f3ff6dc079).
  All four committed code files were independently checked against the local
  source with exact Git blob identities.
- Portable Debian GCC/Clang ASan+UBSan: **PASS 11/11 CTest suites each,
  exit 0**, including invalid/duplicate output, equal-frame/replaced ID,
  native pixel mode, rotation, negative origin and reordered enumeration.
  Reproduce on a fresh source checkout:
  ```sh
  cmake -S src/unittests/windowshare -B build/topology-gcc -G Ninja -DWINDOWSHARE_SANITIZERS=ON -DCMAKE_CXX_COMPILER=g++
  cmake --build build/topology-gcc --parallel 4
  ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/topology-gcc --output-on-failure
  cmake -S src/unittests/windowshare -B build/topology-clang -G Ninja -DWINDOWSHARE_SANITIZERS=ON -DCMAKE_CXX_COMPILER=clang++
  cmake --build build/topology-clang --parallel 4
  ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/topology-clang --output-on-failure
  ```
- Hosted [CI **38352550451**](https://github.com/JackHars/deskflow/actions/runs/38352550451):
  **PASS all six jobs**, including native macOS experimental application build
  and tests, Linux application/ON-OFF compatibility, real private GNOME
  headless integration, transport and the GCC/Clang foundations. Independent
  [source bundle **38352550445**](https://github.com/JackHars/deskflow/actions/runs/38352550445):
  **PASS**.
- These cloud results show portable topology logic and native compilation,
  **not** real monitor hotplug, CoreGraphics frame restitution, user focus,
  compositor output scaling or physical input ownership.

**H05/H06 manual verification on the actual computers: NOT_RUN.** Use only
the separately packaged opt-in experimental host and an authorized disposable
source window, with its private identity, peer pin and restore-journal location
recorded. First verify normal export/restore, then with a window held on the
helper display change *one* physical Mac output via System Settings > Displays
(rotation, resolution or second-display disconnection). Expect host denial of
new exports or an `AuthorityLost` restore after its next owner-thread poll,
without choosing a replacement by matching dimensions. Confirm exact old
pressed-input releases, source visibility on a surviving physical output, and
no extra restoration of unrelated windows. If no usable output remains,
**do not delete the journal**; restore display connectivity and run
`deskflow-windowshare --recover --journal /absolute/private/restore.json`
with the prior host exited and valid Accessibility permission. Separately run
GNOME two-physical-output/mixed-scale/negative-coordinate and native input
tests for H05/H06; Mac fingerprint tests cannot substitute for these.

Evidence to retain privately: candidate implementation SHA and bundle checksum,
Mac display arrangements/modes before and after, session/epoch, exact window
identity and recovery journal outcome, source/Viewer native receipts, mode change
time and restoration observation, logs/errors and any failed recoveries.
Do not store passwords, keys, typed text or private windows in GitHub.
The P2 upstream native drain, P3 original coordinator, full multi-window
handoff, bidirectional edge-drag and all physical H rows remain unfinished.

## P7 independent Mac recovery watchdog (commit `5cc1b760`; H10 NOT_RUN)

**Runtime code:** The experimental Mac host creates a separate process-group
watchdog before opening any exported window. Its fixed, CLOEXEC-backed private
pipe carries a startup acknowledgement, not control or input events; a missing
acknowledgement or dead watcher refuses/terminates the hosting session. The
watcher records the original Mac host's **kernel process-birth identity**, observes
the pipe and waits for that exact process to exit rather than trusting a PID
number (which can be reused). It then re-execs the **existing** offscreen
`--recover --journal <absolute-path>` path. That routine enforces exclusive
journal locking and saved AX/CG identities and preserves unresolved recovery
entries. The watcher does not bypass native code admission, alter standard
Deskflow, implement input forwarding or grant a remote window-control API.

**Executed checks for implementation SHA
[`5cc1b760`](https://github.com/JackHars/deskflow/commit/5cc1b760d744bddad21b0cabd327e2495df6264f):**

- Hosted GitHub [Window sharing checks **38343504035**](https://github.com/JackHars/deskflow/actions/runs/38343504035):
  **PASS, six of six jobs** (Mac application/integrated core, Linux
  application, native headless GNOME, transport, GCC and Clang sanitizers).
  The Mac application CTest includes the new `windowshare-recovery-watchdog`
  subprocess regression. Mac experimental package/handshake jobs succeeded.
- Hosted [source bundle **38343504082**](https://github.com/JackHars/deskflow/actions/runs/38343504082):
  **PASS**.
- Independent local GNU and Clang sanitizer portable suites **11/11 PASS**
  each; cross-platform simulator for the Mac-only watcher was compiled against
  a test-only `libproc` shim and passed child-ready → host-exit → recover-exec,
  premature-recovery refusal and missing-executable denial. The shim was
  **not** committed/packaged; the native Mac Actions CTest is the real Mac build.
- Repeatable hosted verification:
  `cmake --build build/app --parallel 3 && ctest --test-dir build/app
  --output-on-failure` with `BUILD_WINDOWSHARE=ON` on macOS via
  `.github/workflows/windowshare-foundation.yml`; the watchdog regression is
  `ctest --test-dir build/app -R windowshare-recovery-watchdog
  --output-on-failure`.

**Not done/limits:** Mac SIGKILL during a truly relocated AX window, app
revocation, TCC denial, multi-monitor host recovery, helper crash, native
kernel/Qt scheduling, sleep/wake, abnormal `--recover` failure diagnostics,
and prolonged soak/H10/H11. All relevant physical acceptance remains **NOT_RUN**.
The watcher is a P7 recovery slice, **not** completion of P1–P8. The desktop's
original-input path stays fail-closed pending authoritative EI/Clutter/Qt drain
and source-bound delivery permits.

## P4 physical-display restore safety (commit `91dba759`, no hardware claims)

Mac host `recoverFrame` now calls a portable `recoveryRect` selector for both
normal return and journal recovery. Physical displays are supplied by the existing
CG display enumerator, which excludes Deskflow's holding output by its exact
vendor/model/serial. The selector prefers largest overlap with the saved window;
if all overlaps are empty, chooses nearest physical rectangle (not the first
enumerated display), with deterministic ties. Invalid or unavailable physical
outputs fail closed, retaining the unresolved recovery journal.

**Executed in this isolated Linux source snapshot:**
`cmake --build /mnt/data/deskflow-work/build-gcc -j4` followed by
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir
/mnt/data/deskflow-work/build-gcc --output-on-failure`: **PASS 11/11 suites**,
0 failures. Same build and test using `build-clang`: **PASS 11/11 suites**,
0 failures. New `CoreTests` cases include negative output origins, vertical
arrangements, removed outputs, enumeration order, offscreen restoration and
no-valid-output refusal. This checks the portable policy under GCC/Clang
sanitizers, **not** actual window recovery.
GitHub CI for [`91dba759`](https://github.com/JackHars/deskflow/actions/runs/38342620778):
**PASS 6/6 jobs**, including Mac application, GCC/Clang foundation, Linux
application, GNOME provider and transport; source bundle
[38342620807](https://github.com/JackHars/deskflow/actions/runs/38342620807)
**PASS**. Physical H05/H06/H10: **NOT_RUN**. This does not complete P4 or P1–P8.

## Verified published code checkpoint (2026-10-12; implementation incomplete)

- **Tested implementation SHA:** `868d9d9e6db6cb9896cd577cb2951d434117e8a3`
  (source tree `106c1527abc5f949b599d7825b111fd7d213cca3`).
  Branch: [`feature/seamless-windows`](https://github.com/JackHars/deskflow/tree/feature/seamless-windows).
- **Cloud CI:** [GitHub Actions run 38341136093](https://github.com/JackHars/deskflow/actions/runs/38341136093)
  finished `success`, six of six jobs passed: GNU GCC and Clang foundation,
  Mac application, Linux application/integrated ON/OFF core, transport, real
  headless GNOME provider and QtWayland fixture, and the newly staged Linux
  package/checksum/launcher smoke. Source-bundle run
  [38341136096](https://github.com/JackHars/deskflow/actions/runs/38341136096)
  succeeded independently. CI uses its own hosted runners, not either of
  the user's physical computers.
- **Separate local portable run on source archive for the exact tested SHA:**
  CMake 3.31.6, Ninja 1.12.1, GNU G++ 14.2.0 and Clang from the local toolchain;
  configure `src/unittests/windowshare` with `WINDOWSHARE_SANITIZERS=ON`,
  build all targets and run `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1
  ctest --output-on-failure` for each compiler. **PASS: 11/11 suites on GCC,
  11/11 suites on Clang**, 0 failed, exit 0. This environment lacks Qt and
  libei developer packages; full native builds came from hosted CI, not here.
- **Additional local scripts on the exact source:** `node --test
  deploy/windowshare/gnome-provider/tests/logic.test.js` **PASS: 13/13**;
  `python3 deploy/windowshare/package-linux-tests.py` **PASS: 7/7**;
  `python3 deploy/windowshare/physical-linux-tests.py` **PASS: 20/20**;
  `python3 deploy/windowshare/physical-macos-tests.py` **PASS: 11/11**;
  all exit 0. The last two test only the supervisor/schema/harness with no
  physical input or user desktop; they do **not** change any H-row to PASS.
- **Implementation still outstanding:** native EI/Clutter/Qt input-consumption
  proof and release drain, exact source-stage coordinate authority, native
  P2 permit, P3 enabled trusted original-input coordinator/delivery, P4
  multi-window/multi-monitor native ownership, P5 bidirectional edge drag,
  P6 popups/clipboard/Unicode IME, and remaining P7 native recovery,
  fault injection, soak and latency targets. All H01–H12 remain **NOT_RUN**. No test above demonstrates
  original Mac input receipt or authorizes re-enabling that path.

The SHA is a **verified partial code checkpoint**, *not* the final implementation
of the handoff. Any documentation-only follow-up commit can have a newer branch
SHA without changing the tested implementation; never attribute a test to an
untested code revision.

## Published incremental results (2026-10-12)

**This branch has NOT finished `CHATGPT_HANDOFF.md` P1–P8.** Production
original-input delivery is still disabled. No physical hardware test or
native compositor/Qt input-drain test was run in this cloud session.

| Commit | Verified scope | Limits |
| --- | --- | --- |
| [`f86ee404`](https://github.com/JackHars/deskflow/commit/f86ee40430dc89b0612968c956c1f3c51f126df1) | Actual TLS Bridge recovery now rotates after the previously recovered session, including repeat failure on low-ID stream. All six existing GitHub CI jobs passed in [run 38339169894](https://github.com/JackHars/deskflow/actions/runs/38339169894). | No real multi-window frame rate or latency targets measured |
| [`fb0bf0dd`](https://github.com/JackHars/deskflow/commit/fb0bf0dd8951ada013d1cced450d196e30ac2523) | Read-only optional GNOME routed-point native query, private D-Bus tests for frozen stage point, echo, old capability, stale native hit, and deadline | No EIS/Qt queue drain, original owner, native event delivery, or stage-space conversion |
| [`fb504a3f`](https://github.com/JackHars/deskflow/commit/fb504a3f1db42de34e3a24c26ea337bb5dfa37dc) | Real private headless Mutter test validates a frozen point **different** from the live cursor and rejects stale revision/unrelated surface; GNOME native job passed in [run 38340048477](https://github.com/JackHars/deskflow/actions/runs/38340048477). | Not a physical or original-input acceptance test |

The Viewer additionally explicitly disables Qt input-method acceptance and
ignores injected preedit/commit `QInputMethodEvent`s. A Qt regression checks
there is no forwarded input even after a painted, focused Viewer is ready.
This is a **documented lack of IME/composition support**, not completed
Unicode/IME compatibility or an ordered Qt input-event drain.

The P8 partial release packaging is `deploy/windowshare/package-linux.py`,
`package-linux-tests.py` and `package-linux.md`. It writes an inert
reproducible archive of explicitly provided Linux ELF binaries, launch wrappers,
and unenabled GNOME provider; its per-member verifier checks SHA-256 and modes.
It never touches ordinary Deskflow. CI has a dedicated package/unit/smoke step
in the newer documentation/packaging commit; do not attribute its results to the
earlier implementation commits.

The full C01-C12 and H01-H12 matrices below remain the contractual acceptance
criteria. A native-hit result or a staging-archive pass is **not** a PASS for an
unfinished complete-roadmap row. Current physical H01-H12 are NOT_RUN.

This is the testing contract for the implementation described in
`CHATGPT_HANDOFF.md`. Created 2026-10-12; the cloud implementer must update it
throughout development. The original local chat will execute the hardware
sections after receiving the final branch.

## Candidate identity and result rules

- Baseline implementation: `ef51456659edae1acf06a6c03b730bcaf66f2f8b`.
- Cloud **final** implementation SHA: **NOT_AVAILABLE — roadmap incomplete**. Latest component implementation: `bbe9ed3f2fb4d8519397bca61bb5b539082d00f6` (Mac and home Ubuntu checks PASS; see the newest entry for hosted CI). Earlier checkpoints below are historical; physical acceptance remains incomplete.
- Returned branch: `feature/seamless-windows` in `JackHars/deskflow`; incremental changes pushed directly, no PR.
- Toolchain and build options for the tested partial code: documented in the exact checkpoint section above and the successful CI run. Native hardware acceptance remains NOT_RUN; local Mac automated checks are recorded above.
- Source reproducibility: linked GitHub implementation SHA and CI source bundle; inert Linux package instructions: `deploy/windowshare/package-linux.md` (not installed).
- Partial implemented changes, unsupported capabilities and outstanding code gaps: linked checkpoint and P1–P8 inventory above; final full-plan delivery remains incomplete.

Use `PASS`, `FAIL`, `NOT_RUN` or `NOT_APPLICABLE` with a reason. A skipped case is
not a pass. Separate historical baseline results from results for the returned
candidate. Record tested source SHA, command, environment, exit code and test
counts. State whether evidence proves protocol behavior, backend submission,
native dispatch, Qt consumption or actual Mac application receipt.

The implementation SHA may precede a documentation-only commit containing this
report; identify both rather than claiming a self-referential final file hash.
Do not store private keys, credentials, typed private text or full environment
dumps in results. Keep useful logs/artifacts and their hashes, not just screenshots
or an unexplained "all tests pass."

## Cloud implementation progress: private protocol v4

This is an incremental implementation checkpoint, not completion of P1 or P2–P8.
The private channel and preflight now retain keyboard/all-input scope end to end,
negotiate explicit code-support capabilities, and reject v3, malformed scope,
capability downgrade, and pointer delivery under a keyboard lease. No new native
input authority is installed by this checkpoint.

Debian 13 disposable cloud environment: GCC 14.2.0, Qt 6.8.2, CMake 3.31.6.
The dependency archive is from source-helper run `38326221704`; SHA-256
`c8cdc4abc44124ac163e51ebcee4a20f0c2cf90b07f7ae6eedf351084880ca0e`.
Its package hashes were verified before installation. Existing libsystemd0 was
retained to avoid replacing the container's systemd; no user machine was touched.

PASS: configure `src/apps/windowshare` with `WINDOWSHARE_APP=ON`, Debug;
build `windowshare-local-input-tests` and `windowshare-preflight-tests`.
PASS: `LC_ALL=C.UTF-8 QT_QPA_PLATFORM=offscreen ctest --test-dir build/cloud-app
--output-on-failure --no-tests=error -R '^windowshare-(local-input|preflight)$'`;
exit 0, 2 suites passed, 0 failed, 26.13 seconds. These prove private-process
framing, scope, state and release behavior, not compositor or Mac app receipt.

New concrete cases: `negotiatedScope`, `fullInputScope`, expanded
`malformedBootstrap`, `malformedPreflight`, and `changedBindings`. The full-scope
preflight receiver confirms all eight ordered records before revoking its owner;
this test does not infer success from a sleep. Existing cancellation, capture
revocation, deadline, callback destruction and malformed mixed-record cases remain.
Two first-run failures were test migrations (old fixed offsets and a pointer
used under a now-enforced keyboard lease); neither was suppressed or relabelled.

Current checkpoint native/CI/sanitizer checks beyond the above are NOT_RUN until
recorded for an exact published source SHA. Hardware H01–H12 remain NOT_RUN.

## Cloud implementation progress: asynchronous Server preparation

This checkpoint implements the Server half of P1's asynchronous contract. It is
not completion of the native coordinator or the whole handoff. `reserve` and
`poll` distinguish `Pending`, `Refused`, `Accepted` and `Revoked`; pending input
retains its exact completed capture, recipient, selection and routed point at
the head of the existing bounded queue. No callback or nested event-loop wait
is added to the event tap. Only a finished refusal may use legacy delivery.

The capture-arrival budget remains 100 ms. Asynchronous preparation has a
separate **500 ms absolute budget**, starting on the first provider request;
polls do not extend it. Captures received on time remain eligible while waiting
behind that request. Pending and detached/reentrant operations share the
256-entry bound. Native output must already be reserved before an accepted
reply, and the dispatcher abort/release contract keeps unfinished native release
explicit rather than treating cancellation as permission to resume forwarding.

Regression cases added to the actual `ServerTests` target:
`originalDispatch_pendingOrderingAndCaptureArrival`,
`originalDispatch_pendingRefusalAndRawRelease`,
`originalDispatch_pendingCancellationLifetime`,
`originalDispatch_pendingMalformedRepliesAndDeadline`,
`originalDispatch_pendingReentryAndLimit`,
`originalDispatch_pendingFrozenPointAndTicketReuse`, and
`originalDispatch_pendingDragRevocation`.
These use real protocol proxies plus a local provider fixture, not a native
compositor. They test delayed and malformed replies, a reused ticket, exact
frozen coordinates, exceptions, callback destruction, selection/disconnect,
queue saturation, and untranslated key/button releases. A first regression run
caught missing raw-up tracking on selection cancellation; it was fixed, not
skipped. A revoked drag also releases its exact held original owner immediately.

Source checkout reconstructed and verified against `6e977ff05c6e787fed6814a5094d0152b1eb63e3`
and tree `f66c58c6b07b845b9b937d67a39897b97ff09e77` before edits; all 1,035 file
bytes/modes matched the authenticated source index. Debian 13, GCC 14.2.0,
Qt 6.8.2, CMake 3.31.6, Ninja. The source helper now preserves symlink targets
when adding an archive prefix. Candidate implementation identity and CI results
will be recorded after publishing the hash-verified source objects.

Executed commands and exact logs are under `build/cloud-logs/`:

```sh
cmake -S . -B build/cloud-on -G Ninja -DBUILD_WINDOWSHARE=ON \
  -DBUILD_INSTALLER=OFF -DBUILD_TESTS=ON -DSKIP_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/cloud-on --parallel 3 --target deskflow-core \
  ServerTests ClientProxyTests ServerProxyTests InputSequenceTests ComputerInputSequenceTests
QT_QPA_PLATFORM=offscreen ctest --test-dir build/cloud-on/src/unittests \
  --no-tests=error --output-on-failure \
  -R '^(ServerTests|ClientProxyTests|ServerProxyTests|InputSequenceTests|ComputerInputSequenceTests)$'

cmake -S . -B build/cloud-off -G Ninja -DBUILD_WINDOWSHARE=OFF \
  -DBUILD_INSTALLER=OFF -DBUILD_TESTS=ON -DSKIP_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/cloud-off --parallel 3 --target deskflow-core \
  ServerTests ClientProxyTests ServerProxyTests
QT_QPA_PLATFORM=offscreen ctest --test-dir build/cloud-off/src/unittests \
  --no-tests=error --output-on-failure \
  -R '^(ServerTests|ClientProxyTests|ServerProxyTests)$'

cmake -S . -B build/cloud-asan -G Ninja -DBUILD_WINDOWSHARE=ON \
  -DBUILD_INSTALLER=OFF -DBUILD_TESTS=ON -DSKIP_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build/cloud-asan --parallel 3 --target \
  ServerTests ClientProxyTests ServerProxyTests ArchTests
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  QT_QPA_PLATFORM=offscreen ctest --test-dir build/cloud-asan/src/unittests \
  --no-tests=error --output-on-failure -R '^(ServerTests|ClientProxyTests|ServerProxyTests|ArchTests)$'
```

PASS: enabled core build and five suites, exit 0, 170 Qt entries, zero failures
or skips. PASS: disabled core build and three suites, exit 0, 31 Qt entries,
zero failures and 29 explicit disabled-feature skips. PASS: ASan/UBSan plus leak
detection, four suites (`ArchTests`, `ServerProxyTests`, `ServerTests`,
`ClientProxyTests`), exit 0, 124 Qt entries, zero failures or skips. Counts include
Qt initialization/cleanup. Logs: `build-core-on.log`, `test-core-on.log`,
`build-core-off.log`, `test-core-off.log`, `build-asan-cleanup.log` and
`test-asan-cleanup.log`; detailed Qt counts are in each build's
`src/unittests/Testing/Temporary/LastTest.log`.

The first sanitizer run failed on fixture-owned queued events, an explicitly
detached test client and the POSIX architecture's initial main-thread reference.
Freeing event payloads correctly then exposed a production `new`/`free` mismatch
in connection notifications. Fixed real C++ notification ownership, all four
related InputFilter action payloads/consumers and the filter's synchronous
borrowed-event view; no sanitizer check was disabled. New regressions
`connectionEvent_destroysOwnedPayload`,
`inputFilter_ownedActionsAndConnectionLifetime` and
`mainThreadOwner_repeatedConstructionReleasesItsReference` exercise those fixes.
The POSIX destructor releases only its own initial reference, not externally
retained worker references. Existing allocation/state tests remain enabled.

The production compiler flags, not just fixture flags, include ASan/UBSan in
this sanitizer build. Distribution-provided Qt and system libraries are not
rebuilt with sanitizers. H01–H12 remain `NOT_RUN — requires local hardware`.

## Historical baseline

All six jobs pass for `ef5145665` in
[run 38323926279](https://github.com/JackHars/deskflow/actions/runs/38323926279).
The original Mac and home Ubuntu each passed 161 Qt core entries. Ubuntu passed
eight enabled suites and three disabled compatibility suites; the latter had
30 passed Qt entries and 29 explicitly disabled trace skips. These results do
not validate the new cloud implementation. Physical input, two physical monitors
and edge drag remain unaccepted.

## Cloud test matrix — update every row

| ID | Required check | Current candidate result / command / evidence |
| --- | --- | --- |
| C01 | Clean build and pure C++ foundations; GCC/Clang and applicable sanitizers | PASS at c867af9d0 — GCC and Clang ASan/UBSan foundations 12/12 each. |
| C02 | Actual Linux core with window sharing ON and OFF; ordinary protocol compatibility | PASS at c867af9d0 — actual Linux core ON 8/8 and OFF 2/2. |
| C03 | Full-input private lease/framing/preflight; old peers, malformed scope, stale lifetime and cancellation | PASS locally at c867af9d0 — preflight, channel and companion 3/3 including deferred full-scope continuation; hosted private-protocol sanitizer suites 4/4 PASS. |
| C04 | Bounded async preparation, ownership, overflow, timeout, selection change, reentry and destruction | PASS locally — five actual core suites / 170 Qt entries; deferred continuation cancellation, overflow, timeout, reentry and destruction pass in C03. Production native adapter remains unimplemented. |
| C05 | Ordered cutoff/drain and exact point validation; delayed queues, duplicates and same-batch disconnect | NOT_RUN for a complete cutoff — exact-point/stage/Qt observations pass in hosted GNOME at c867af9d0; no causal old-input drain or delivery permit exists yet. |
| C06 | Actual Viewer/Bridge/transport; pending focus, pause/resume and old queued events | PASS component checks — local Viewer/viewport/Bridge and hosted transport at c867af9d0; complete old-input drain remains C05, not covered by these passes. |
| C07 | Private X11 and GNOME/Qt headless integration when the cloud supports it | PASS at c867af9d0 — real private headless GNOME/Qt and X11 fixtures; no user desktop or physical monitor claim. |
| C08 | Multi-window/topology/holding lifecycle, drag transaction and failure regressions | NOT_RUN for full milestone — portable topology/restore cases pass in C01; multi-window ownership and bidirectional edge drag remain unfinished. |
| C09 | Dialog/clipboard/shortcut associations and resource scheduling/recovery tests | NOT_RUN for app compatibility — scheduling/recovery component regressions pass in hosted app checks; native dialogs, clipboard and shortcut acceptance remain open. |
| C10 | Mac compilation and private-event construction/packaging tests through available CI | PASS at c867af9d0 — Mac application/core, native event construction and isolated packaging checks; local unreaped-host regression FAIL before / PASS after 7bc8cf332. |
| C11 | Linux packaging/install/uninstall in a disposable environment | PASS inert archive/manifest/staging tests at c867af9d0; NOT_RUN for end-user install/upgrade/removal acceptance. No host installation performed. |
| C12 | Formatting, REUSE, diff check and final source/commit reproducibility | PASS preflight C++ formatting, changed-file REUSE lint and git diff check; exact source and evidence recorded. Full-repository REUSE lint NOT_RUN. All six current hosted jobs and source bundle PASS. |

### Reproducible baseline commands

Run from the repository root in a disposable cloud checkout. Install dependencies
using `.github/workflows/windowshare-foundation.yml` as the maintained source;
the integrated build needs more than the pure foundations. Use separate build
directories so ON/OFF and sanitizer settings cannot contaminate each other.
These commands are starting points: append new production targets/tests and
record the exact final commands above. Do not omit new tests because they are
absent from this baseline regex.

```sh
# Pure foundations and sanitizers.
cmake -S src/unittests/windowshare -B build/cloud-foundation \
  -DWINDOWSHARE_SANITIZERS=ON
cmake --build build/cloud-foundation --parallel 2
ctest --test-dir build/cloud-foundation --output-on-failure --no-tests=error

# Standalone transport, host/viewer and existing application tests.
cmake -S src/apps/windowshare -B build/cloud-app \
  -DWINDOWSHARE_APP=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cloud-app --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build/cloud-app \
  --output-on-failure --no-tests=error

# Actual integrated core, enabled.
cmake -S . -B build/cloud-on -DBUILD_WINDOWSHARE=ON \
  -DBUILD_INSTALLER=OFF -DBUILD_TESTS=ON -DSKIP_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/cloud-on --parallel 2 --target \
  deskflow-core ServerTests ClientProxyTests ServerProxyTests \
  InputSequenceTests ComputerInputSequenceTests
QT_QPA_PLATFORM=offscreen ctest --test-dir build/cloud-on/src/unittests \
  --no-tests=error --output-on-failure \
  -R '^(ServerTests|ClientProxyTests|ServerProxyTests|InputSequenceTests|ComputerInputSequenceTests)$'

# Actual integrated core, disabled.
cmake -S . -B build/cloud-off -DBUILD_WINDOWSHARE=OFF \
  -DBUILD_INSTALLER=OFF -DBUILD_TESTS=ON -DSKIP_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/cloud-off --parallel 2 --target \
  deskflow-core ServerTests ClientProxyTests ServerProxyTests
QT_QPA_PLATFORM=offscreen ctest --test-dir build/cloud-off/src/unittests \
  --no-tests=error --output-on-failure \
  -R '^(ServerTests|ClientProxyTests|ServerProxyTests)$'

# Existing physical-test control regressions; these do not start physical input.
python3 deploy/windowshare/physical-macos-tests.py
python3 deploy/windowshare/physical-linux-tests.py
python3 deploy/windowshare/gnome-crossmachine-tests.py
git diff --check
```

An integrated root `ctest` invocation may discover no tests; use the actual
`src/unittests` directory shown above and require nonzero discovery. Verify the
compiled **production** Server and platform backend flags, not merely fixture
flags. Preserve ON/OFF binary hashes and complete test output. If a suite relies
on display services, follow its documented private fixture rather than changing
production defaults to make it run offscreen.

For GNOME/X11, use the existing procedures and add the new delayed-queue tests:

- `deploy/windowshare/gnome-provider-container.sh`
- `deploy/windowshare/gnome-provider-tests.py`
- `deploy/windowshare/gnome-qt-viewer-tests.py`
- `deploy/windowshare/gnome-qt-viewer-fixture-tests.py`
- `deploy/windowshare/x11-core-smoke.py`
- `.github/workflows/windowshare-foundation.yml`

Document actual invocation arguments and dependency versions after implementation.
GNOME private headless tests can run in cloud when dependencies are available;
they are distinct from the user's physical desktop. Do not require privileged
access to the user's devices or weaken native capability checks to run them.

## Required regression scenarios for the new input route

For C03–C07, provide automated cases for:

- Every key/button/move/scroll kind, modifiers, repeat, two-axis scroll/momentum,
  expanded/untranslated captures and uncertain physical state.
- Pending vs refused preparation; acceptance before any native delivery; no
  fallback legacy acquisition after accepted original ownership.
- Press/release through focus/selection/connection replacement; held-drag/up
  stays with its owner; late raw-up cleanup cannot erase a new press incarnation.
- Wrong/full-vs-keyboard scope, missing authority, malformed/truncated frames,
  duplicate/stale/out-of-order receipt and exhausted counters/queues.
- Reentrant release/disconnect/destroy during callbacks; no use-after-free,
  double deletion or stale destination pointer. Run relevant sanitizers.
- PONG followed by disconnect in the same batch; frame completion from an
  already-running dispatch; Qt input queued after a socket acknowledgment;
  focus-sync callback ordering; repeat/IME work after apparent quiescence.
- Exact Server-routed point differing from current cursor; occlusion, letterbox,
  focus change, surface replacement, mixed scale, output removal and expired
  painted geometry. Unsupported topology/surface must deny explicitly.
- Emergency handling after capture overflow and owner loss; resume only after
  exact release/drain completion. A failed barrier never reopens forwarding.

Add the concrete test names/commands and evidence that each intended bug would
fail the test. A mocked receipt validates a state machine, not the native barrier.

## Local hand-back procedure — for this chat, not the cloud implementer

1. Obtain the returned branch and final implementation SHA; inspect changes and
   cloud results. Preserve local work. Confirm the code being built matches the
   claimed tested revision and identify any documentation-only follow-up.
2. Rebuild on the actual Mac and Ubuntu before installation. Recheck toolchain,
   OS, Qt private-API compatibility, GNOME/provider capabilities and monitor
   layout. Never reuse an old package and label its behavior as the new code.
3. Resolve the pending Mac App Management requirement before changing the
   experimental app. Obtain fresh readiness for physical input and destructive
   test cases. The user's previous `ok` only confirmed local window visibility.
4. Use separate experimental bundles, private settings/trust/IPC directories,
   fresh test identities and bounded sessions. Preserve ordinary Deskflow.
   Discover/reconfirm SSH routing locally; no credentials are in this handoff.
5. Execute H01–H12 in order where applicable, stop an unsafe or failed session,
   preserve diagnostics and fix the underlying defect before claiming acceptance.
6. Retain exact package/binary/source hashes, both machines' results, native
   receipts and ordinary-process/settings comparisons. Record the human visual
   verdict separately from automated cleanup success.

### Hardware and native acceptance checklist

All rows below start **NOT_RUN for the cloud candidate**. The cloud implementer
must add exact commands/options for new code and harnesses before hand-back.

| ID | Procedure and required observations | Result / evidence |
| --- | --- | --- |
| H01 | Build current source on Mac and Ubuntu; confirm actual enabled/disabled core flags, load dependencies and run relevant local suites. Verify fresh experimental package identity and supported native capabilities. | PARTIAL — bbe9ed3f2: Mac builds, 9/9 targeted suites and scoped proxy sanitizers PASS; home Ubuntu 31/31 integrated, 3/3 OFF and private GNOME scales 1/1.25 PASS. Fresh package and complete native capability acceptance pending. |
| H02 | Repeat actual disposable Mac capture → Ubuntu viewer → Mac tagged native-receiver tests at Qt scales 1 and 1.25. Check frame identity/geometry, pointer/click/scroll/key receipts, no stuck input and restoration. This is synthetic, not physical acceptance. | NOT_RUN |
| H03 | Run visible user-started physical flow. Confirm Start/Stop and countdown, actual Option hotkeys, type a/Shift+b/c, hold/release x, move/click/drag and scroll both axes, return to Mac. Match exact native source/host receipts and human result; no duplicate text or missing release. | NOT_RUN |
| H04 | Exercise actual production original route and native drain with delayed compositor/Qt processing. Verify no legacy duplicate, exact owner through reentry/focus changes and safe failure on timeout/loss. Test emergency chord before/while/after active input and after capture continuity failure. | NOT_RUN |
| H05 | Use both physical monitors with mixed compositor scaling, negative coordinates, different relative layouts, resize and output disconnect. Validate both destinations, edges, letterboxing, occlusion and correct source points. If only one monitor is connected, leave two-monitor cases unrun. | NOT_RUN |
| H06 | Export multiple windows from same/different apps; hold sources on the helper display, overlap remote/local windows, switch focus, resize/minimize/close viewers. Verify continued source capture, exact input owner and per-window restoration after helper loss. | NOT_RUN |
| H07 | Follow [h07-edge-drag.md](deploy/windowshare/h07-edge-drag.md). Drag a real source window across a configured edge, continue/release the drag on Ubuntu, then drag back. Verify first-frame transfer, source location/focus, single button-up ownership and rollback for canceled drag/disconnect/layout change at each transfer phase. | NOT_RUN |
| H08 | Test native dialogs, modal child windows, popup/context menus, browser and Electron app. Check intended window association, stacking, focus and input targeting; no unrelated application receives an event. Record app/OS versions. | NOT_RUN |
| H09 | Verify copy/paste directions and focus policy; modifiers, shortcuts, key repeat, Unicode/composition and both scroll axes/momentum. Check clipboard feedback and unsupported IME behavior. | NOT_RUN |
| H10 | Terminate only recorded owned experimental processes at defined host/core/viewer/helper stages; test connection loss and sleep/wake. Confirm automatic/journal recovery, no orphan display/process, no stuck input and no changes to ordinary Deskflow. | NOT_RUN |
| H11 | Run a documented long-duration/multiple-stream scenario; measure end-to-end latency, CPU/GPU, memory, bandwidth, dropped frames and fairness. Record hardware/settings, duration and thresholds before running. Verify bounded resources and cleanup. | NOT_RUN |
| H12 | Install, launch, upgrade, restart and remove the separate experimental packages on both machines. Verify code/dependency identity, permission messages, provider version diagnostics and preservation of ordinary Deskflow/settings. | NOT_RUN |

### Existing local test entry points

Follow these tracked documents for their full prerequisites and current CLI
schemas; do not fabricate or reuse expired preparation files:

- `deploy/windowshare/crossmachine-linux.md` and
  `deploy/windowshare/crossmachine-macos-smoke.py` for synthetic two-machine runs.
- `deploy/windowshare/physical-linux.md` and
  `deploy/windowshare/physical-macos.md` for fresh physical-test plans and the
  visible Start/Stop coordinator.
- `deploy/windowshare/original-input-physical.md` for the explicit production
  H04 route and optional H06 holding mode. Its wrapper is
  `original-input-physical.py`; Mac plans are version 2, Linux plans/control
  version 3. Start/Record/Stop, exact packaged Core-owned Host, native receipts,
  restoration and private IPC shutdown are mandatory. Validate-only creates a
  review without launching processes; no hardware result is inferred.
- `deploy/windowshare/input-evidence.md` and `h11-evidence.md` for private
  recording, strict raw-field normalization and separate latency/resource
  measurement prerequisites. The H04 recording is 30 seconds and cannot
  substitute for H11's two-minute input and 60-minute resource workloads.
- `deploy/windowshare/package-macos.sh`, `macos-bundle-tests.py` and
  `launch-smoke.py` for experimental packaging and handshake checks.

The legacy physical-harness mode still leaves original-input delivery and
holding display disabled. H03 alone therefore cannot validate P3/P4. The
explicit production mode above preserves that historical distinction; its
exit-zero result still awaits receipt and human review.

During teardown revoke input, release exact owners, restore sources and stop
only the test's recorded process/container/socket identities. Capture a bounded
diagnostic before force-stopping a hung owned process. Verify ordinary process
birth identities/settings and absence of owned leftovers. A clean exit proves
lifecycle cleanup, not physical delivery or visual correctness.

## Cloud implementer: required final hand-back contents

- [ ] Fill candidate identity, branch and dependency versions above.
- [ ] Replace every C-row placeholder with an actual result and reproduction.
- [ ] Add exact commands for new tests, production launch modes and packaging.
- [ ] List every uncompiled platform path and unimplemented/unsupported feature.
- [ ] Keep unperformed H tests marked NOT_RUN; document their prerequisites.
- [ ] Map implemented P1–P8 changes to commits and associated test IDs.
- [ ] Include expected native receipt fields/counters and pass/fail criteria for
  new barriers, routed points, transfers and recovery cases.
- [ ] Ensure the branch contains all needed source/scripts/docs; ignored build
  artifacts must not be the only copy of a required test or configuration schema.
- [ ] State the next local verification command and any prerequisite user action.
