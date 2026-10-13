<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Seamless Windows: cloud results and local verification

## Published incremental results (2026-10-12)

**This branch has NOT finished `CHATGPT_HANDOFF.md` P1–P8.** Production
original-input delivery is still disabled. No physical hardware test or
native compositor/Qt input-drain test was run in this cloud session.

| Commit | Verified scope | Limits |
| --- | --- | --- |
| [`f86ee404`](https://github.com/JackHars/deskflow/commit/f86ee40430dc89b0612968c956c1f3c51f126df1) | Actual TLS Bridge recovery now rotates after the previously recovered session, including repeat failure on low-ID stream. All six existing GitHub CI jobs passed in [run 38339169894](https://github.com/JackHars/deskflow/actions/runs/38339169894). | No real multi-window frame rate or latency targets measured |
| [`fb0bf0dd`](https://github.com/JackHars/deskflow/commit/fb0bf0dd8951ada013d1cced450d196e30ac2523) | Read-only optional GNOME routed-point native query, private D-Bus tests for frozen stage point, echo, old capability, stale native hit, and deadline | No EIS/Qt queue drain, original owner, native event delivery, or stage-space conversion |
| [`fb504a3f`](https://github.com/JackHars/deskflow/commit/fb504a3f1db42de34e3a24c26ea337bb5dfa37dc) | Real private headless Mutter test validates a frozen point **different** from the live cursor and rejects stale revision/unrelated surface; GNOME native job passed in [run 38340048477](https://github.com/JackHars/deskflow/actions/runs/38340048477). | Not a physical or original-input acceptance test |

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
- Cloud final implementation SHA: **TO BE FILLED BY CLOUD IMPLEMENTER**.
- Returned branch/PR or patch: **TO BE FILLED**.
- Dependency/toolchain versions and build options: **TO BE FILLED**.
- Artifact locations and reproduction instructions: **TO BE FILLED**.
- Implemented changes, unsupported capabilities and code gaps: **TO BE FILLED**.

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
| C01 | Clean build and pure C++ foundations; GCC/Clang and applicable sanitizers | NOT_RUN — cloud implementation not started |
| C02 | Actual Linux core with window sharing ON and OFF; ordinary protocol compatibility | NOT_RUN |
| C03 | Full-input private lease/framing/preflight; old peers, malformed scope, stale lifetime and cancellation | NOT_RUN |
| C04 | Bounded async preparation, ownership, overflow, timeout, selection change, reentry and destruction | NOT_RUN |
| C05 | Ordered cutoff/drain and exact point validation; delayed queues, duplicates and same-batch disconnect | NOT_RUN |
| C06 | Actual Viewer/Bridge/transport; pending focus, pause/resume and old queued events | NOT_RUN |
| C07 | Private X11 and GNOME/Qt headless integration when the cloud supports it | NOT_RUN — record availability |
| C08 | Multi-window/topology/holding lifecycle, drag transaction and failure regressions | NOT_RUN |
| C09 | Dialog/clipboard/shortcut associations and resource scheduling/recovery tests | NOT_RUN |
| C10 | Mac compilation and private-event construction/packaging tests through available CI | NOT_RUN — no physical Mac required; record runner availability |
| C11 | Linux packaging/install/uninstall in a disposable environment | NOT_RUN |
| C12 | Formatting, REUSE, diff check and final source/commit reproducibility | NOT_RUN |

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
| H01 | Build current source on Mac and Ubuntu; confirm actual enabled/disabled core flags, load dependencies and run relevant local suites. Verify fresh experimental package identity and supported native capabilities. | NOT_RUN |
| H02 | Repeat actual disposable Mac capture → Ubuntu viewer → Mac tagged native-receiver tests at Qt scales 1 and 1.25. Check frame identity/geometry, pointer/click/scroll/key receipts, no stuck input and restoration. This is synthetic, not physical acceptance. | NOT_RUN |
| H03 | Run visible user-started physical flow. Confirm Start/Stop and countdown, actual Option hotkeys, type a/Shift+b/c, hold/release x, move/click/drag and scroll both axes, return to Mac. Match exact native source/host receipts and human result; no duplicate text or missing release. | NOT_RUN |
| H04 | Exercise actual production original route and native drain with delayed compositor/Qt processing. Verify no legacy duplicate, exact owner through reentry/focus changes and safe failure on timeout/loss. Test emergency chord before/while/after active input and after capture continuity failure. | NOT_RUN |
| H05 | Use both physical monitors with mixed compositor scaling, negative coordinates, different relative layouts, resize and output disconnect. Validate both destinations, edges, letterboxing, occlusion and correct source points. If only one monitor is connected, leave two-monitor cases unrun. | NOT_RUN |
| H06 | Export multiple windows from same/different apps; hold sources on the helper display, overlap remote/local windows, switch focus, resize/minimize/close viewers. Verify continued source capture, exact input owner and per-window restoration after helper loss. | NOT_RUN |
| H07 | Drag a real source window across a configured edge, continue/release the drag on Ubuntu, then drag back. Verify first-frame transfer, source location/focus, single button-up ownership and rollback for canceled drag/disconnect/layout change at each transfer phase. | NOT_RUN |
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
- `deploy/windowshare/package-macos.sh`, `macos-bundle-tests.py` and
  `launch-smoke.py` for experimental packaging and handshake checks.

The old physical harness explicitly leaves original-input delivery and holding
display disabled. H03 alone therefore cannot validate P3/P4. Extend or add a
clearly named harness for H04/H06 and document its real startup flags here; do
not silently change the meaning of historical test results.

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
