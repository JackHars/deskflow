<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Seamless Windows: cloud results and local verification

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
