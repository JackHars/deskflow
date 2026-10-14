# Seamless Windows work state

## Latest verified checkpoint — 2026-10-14

The next P5 prerequisite is implemented in the production capture path:
`OriginalPointerObservation` retains the original pointer position, both native
window-under-pointer fields and reported source metadata beside the exact
input stamp. Capture completion and the existing Session/Server copy queue
preserve it without a second raw consumer or a protocol change. Missing IDs
stay unavailable; these observations grant no drag or posting authority.
The full Mac build, 16 related routing/input suites and four scoped ASan/UBSan
suites pass. Two shared input suites also compile and pass on the home Ubuntu
machine with GCC 15.2, both normally and with ASan/UBSan (four executions).
That remote test uses source hashes matching this working tree and removes
only its own temporary directory. See `TESTING.md` for exact evidence and limits.

The new passive `windowshare-native-titlebar-probe` builds and observes a normal
AppKit titlebar through that same adapter. It neither posts nor suppresses
input. A fresh readiness question is pending; the physical run is **NOT_RUN**.
Procedure and limits: `deploy/windowshare/native-titlebar-probe.md`.
This does not complete automatic titlebar detection, transfer or P5/H07.

Production identity commit `dbf1f07ef5c28dc898a4a5fa5762b54aca23506d` now
passes all six [hosted checks](https://github.com/JackHars/deskflow/actions/runs/38591194186).
Its staged signed exact-library Mac bundle passes audit, private handshake and
rejection smoke, and a read-only packaged permission probe confirms the private
identity lookup is available. ZIP SHA-256:
`118cbcbe5dd895ae12d623ca3dd1814f27ec082ec1029f7180bfad6fadfcdac4`.
Evidence: `build/roadmap-completion-20261014/mac-identity-package/evidence.json`.
It has not replaced the installed experimental app; input acceptance remains due.

Later test-tool work adds a separately selected, one-shot system-stream release
experiment (`--interactive-hid`). After a zero-timestamp rejection and a
constructed-only clock-domain correction, the user observed the owned window
stop while the button remained held. The marked up reached the app and session
tap, but no later unmarked session-up was observed; a second press invalidated
the automated run. The result remains inconclusive for release ownership/H07.
A new listen-only HID-head observer plus separate session-up observer passes
both normal and scoped sanitizer regressions. Its fresh user-operated run on
`f5517122c` captures the real release 3.616 seconds after submission and exits
cleanly. The user confirms the window stopped while held and closed on release.
Two early frame updates remain in the conservative automated result; subsequent
frames are stable through continuing pointer motion. This is positive evidence
for the explicitly started owned drag, not arbitrary-app titlebar initiation
or a cross-machine handoff. Exact evidence and user report:
`owned-hid-drag-physical-20261014T161234Z` under the Oct 14 root. Production Core
already captures at HID-head; a second raw consumer is not needed or authorized
by this diagnostic. The owned AppKit attached-sheet observation finds a distinct
native window ID with the exact parent link, but does not enable child capture
or input. Both production drag and remote child interaction remain unfinished.

Commit `c8a9617aa8dd930f5929dbf173d4af374b6068b6` passes all six hosted
checks in [run 38586489277](https://github.com/JackHars/deskflow/actions/runs/38586489277).
It requires libei >= 1.5 for the experimental Linux build. The preceding CI
failure was reproduced on Debian 13/libei 1.3.901: `ei_unref` reaches an upstream
null connection during pre-handshake teardown. The source audit finds upstream
fix `12237f19` in 1.5, absent from 1.3.901 and 1.4.0. The startup/shutdown test
is unchanged apart from explanatory comments. Debian opt-in configuration now
rejects that dependency clearly; ordinary OFF configuration still succeeds.
Ordinary runtime behavior with the old library has not been repaired or claimed
safe. Diagnostic evidence is `debian-ei-teardown-red-1` and
`libei-baseline-guard-2` under `build/wifiman-resumed-20261013/ubuntu/`.

The queue/admission checkpoint `a2aa7a5e8` passes the home Ubuntu full build,
**37/37 enabled + 5/5 disabled suites**, actual Core/EI and GNOME/Qt tests at
scales 1/1.25, reconnect and package checks. Its 1,099-file source matches that
commit exactly; all 135 returned artifact hashes verify. The identical binaries
then pass **12/12 planned native sessions without retries**, with all 267
artifact hashes and cleanup/preservation checks verified. See
`queue-admission-integration-1` and `queue-admission-matrix-1` in that evidence
root. Earlier failing matrices remain failed; the earlier unclassified private
channel error is not retrospectively explained by these fixes.

Mac OFF compatibility passes 4/4 suites. Scoped EventQueue and input-evidence
ASan/UBSan regressions pass. The earlier enabled run remains 39/40 plus the
corrected EventQueue suite's separate pass. The separately staged exact-library
Mac package and Linux archive pass their audits and inert/private smokes; neither
is installed. These packages predate the native identity change below.

The new Mac Host lookup replaces title/geometry matching with a runtime-checked
private HIServices AX/native window-ID join for export and recovery. The real
owned-window regression distinguishes two same-title/same-frame windows,
preserves identity across rename, and rejects a closed window's replacement.
It passes with normal and ASan/UBSan builds. The final selected-window role
guard passes four related CTests and the real native regression; its scoped
sanitizer recovery and native tests also pass. Evidence is
`build/roadmap-completion-20261014/native-exact-identity-1/` and
`native-identity-role-evidence.json` and `native-identity-role-sanitize-*`; documentation is
`deploy/windowshare/native-window-identity.md`. This is a private SPI dependency,
not native grab authority or completed remote child support. The production
source's hosted checks and staged package now pass as recorded above.

The user explicitly requires **normal titlebar dragging**, not a selected-window
thumbnail interaction. Two fresh user-operated owned drag probes both record
native movement after the PID-targeted synthetic up was received by the app
(41/74 later pointer motions; 33/62 frame observations). Both close on the real
physical release. That method does not end the native drag. P5 and end-to-end
H07 remain open. Evidence: `owned-drag-physical-{1,2}` under the Oct 14 root.

### Earlier checkpoint notes (retained history)

## Roadmap completion in progress — 2026-10-14

The tested integration snapshot below is now committed as `e461d2b31` and
pushed to `feature/seamless-windows`. Its 1,098 paths, file modes and contents
match the frozen source archive exactly. The unchanged Mutter notification fix
is committed and pushed as `5589ec3dc`. Failure-only private-channel fixture
diagnostics are committed as `9b50e47a6`.

The fixed `association-noop-matrix-1` diagnostic uses the notification fix in
12 fresh private sessions. Eleven pass; ordinary-click scale 1.25/run 1 fails
because the production mapping request returns zero solely on state-mutex
contention. The old unchanged-notification failure does not recur. All 268
artifact hashes and cleanup/preservation checks pass; overall status remains
FAILED. A bounded pending-admission correction and generic EventQueue evidence
coverage are implemented, with native validation still due.

Commit `81efdc5a5` reserves one pending scalar/batch mapping without waiting for
the worker mutex. It retains the original deadline and exact seal, revalidates
on the worker and keeps unread results/cancellation tied to the original ID.
The preserved deterministic regression fails before this correction. All 21
private-bus groups, including ten new contention/lifetime scenarios, then pass
strict compilation and ASan/UBSan (5.502 seconds). Evidence is
`build/gnome-map-admission-20261014/verification.json`; it is not native GNOME
acceptance and does not explain the earlier separate private-channel failure.

The new full Mac build passes after correcting the code-admission fixture's
include path. Its enabled run passes 39/40 suites; the new EventQueue suite has
two incorrect assertions that confuse unread diagnostic rows with outstanding
input. Commit `0cb99c4f3` checks final input accounting explicitly, and the
targeted EventQueue retest passes. The initial failed suite remains recorded.
Logs/XML: `build/roadmap-completion-20261014/mac-{build-2,tests,event-test}.*`.

Hosted CI for `5589ec3dc` passes the GNOME, GCC, Clang and transport jobs.
Mac and Linux application tests pass, then their reconnect smoke fails because its
capability whitelist predates `mapped-input-v1`. The same failure reproduces
against the Mac Viewer; adding that exact capability set passes the real TLS
reconnect smoke. See `build/roadmap-completion-20261014/viewer-smoke-*.log`.
Commit `b10d05cbd` corrects the fixture without changing production capability
negotiation. That CI run remains failed; later skipped stages are not passes.

The final integrated local checkpoint builds both applications and passes
**39/39 enabled + 3/3 disabled Mac suites** and **36/36 enabled + 4/4 disabled
Ubuntu suites**. Both actual Core/EiComputer and the broader private GNOME/Qt
tests pass at scales 1 and 1.25. This source includes the collector, production
physical harness, diagnostic refinements and 96-frame receive-buffer regression.
Archive `f622a09a51c1a57f18071046ac7271ea4c325cd2d9bd00411bda730e52b99ee2`
has 1,098 source files; all 130 returned artifact hashes and preservation/cleanup
checks pass. See `TESTING.md` for exact paths and limits. The new exact-library
Mac package also passes its audit and private handshake smoke (archive
`5129765642ee36b0df1354d458fb4a4974663c8c87febe67bb34ba3e255e7b65`),
without installation or native input. Four private sanitizer suites pass;
local-input initially fails identity authentication, then passes one diagnostic
run with unchanged deadlines. The original cause remains unresolved.
This is not physical acceptance or complete roadmap delivery.

The next fixed 12-session native diagnostic, `focus-recovery-reasons-matrix-1`,
passes ten sessions and retains two ordinary-click failures. Both are traced
to unchanged complete wm-class/instance notifications unnecessarily revoking
an exact live seal. Commit `5589ec3dc` supplies the narrow correction. Prior channel and
admission failures did not recur, so their causes remain unresolved. All 269
artifact hashes and cleanup/preservation checks pass; overall status remains
FAILED. No failed session was retried.

The opt-in original-input evidence collector and production physical harness
are now connected. The source Record control arms one bounded run only after
matching live Core/Host ownership and empty declared queues. The recorder
includes source capture, 15 Core metrics, four Host queues and native disposable
source fields. Independent review caught and corrected a capture/start race
and omitted Qt receive buffering. The new EventQueue observer follows stamped
input across pending/saved/retrieved storage and callback completion. Its two
metrics have explicit null capacities; platform/kernel queues remain unmeasured.
Complete bounded queue coverage and H11 are not claimed. New pure checks pass:
38 normalizer and 35 analyzer cases; the earlier physical harness checks pass
34 Mac and 23 Linux cases.
The integrated application builds and regression results are recorded above;
the new observer is in `350d3cc67` with the test-only correction `0cb99c4f3`.

The fixed 12-session `focus-recovery-matrix-1` diagnostic verifies all 269
artifact hashes and cleanup/preservation checks. It uses the earlier identical
binaries: six prepared-first-click runs pass; three of six ordinary-click runs
fail. One failure occurs before either recovery mode, so that comparison does
not establish a causal fix. The failures expose a real window-association
notification and two insufficiently detailed channel/admission errors. Bounded
diagnostics now preserve their reasons without changing operation semantics.
Archive: `f4d01f6cb5f7edcba6711ac94f0dcc3683c9fe57c87fee96a1ac0824366090a4`.
This diagnostic retains overall FAILED status; no failed session was retried.

The first-click checkpoint now passes actual Core native GNOME integration at
Qt scale factors 1 and 1.25. Each run keeps one Core/provider connection,
observes the real four-second idle stop and fresh emulation generation, performs
focus-only preparation, genuine All promotion, exact cleanup, fresh key input
and normal peer-verified private IPC shutdown. Its source archive is
`56058d584cbf1ded23e24ef7a44ac1866179567fa424e5b6e75bddaa9d73c61a`;
69 returned artifact hashes and all preservation/cleanup checks pass under
`build/wifiman-resumed-20261013/ubuntu/first-click-core-ipc-stop-1/`.
The same compiled binaries' parent checkpoint `first-click-abi-timer-1` passes
35/35 enabled suites, 4/4 disabled suites and the broader native GNOME/Qt
first-click and two-overlapping-Viewer matrix at both scales. That parent run
retains its overall failure because its older harness used SIGTERM as a
graceful stop; the final retest changes only the Python shutdown procedure.
This establishes the named synthetic/private integration, not physical Mac
capture, original native delivery or monitor scaling. It predates the new H11
collector and physical-harness integration currently being completed.

The preceding actual-Core diagnostic reproduced a startup **SIGSEGV before IPC5/
IPP1 negotiation**. It is not a Linux acceptance pass. The debugger run uses
the identical Core binary (`81aeeacef30f0e9ab8d43a7dcb2ed3bc6737d0572d6ac90c5d1a92ac9f60b7e7`)
in the same isolated image. Its stack reaches `EiComputer::handleSystemEvent`
while reading an invalid mapped-cutoff member. The `DESKFLOW_GNOME_EI_PING`
definition was private to the platform target despite changing the public
class layout, so the app target allocated a smaller object. The public build
definition and real cross-target construction/batch regression now pass in
both enabled and disabled builds.
Evidence: `build/wifiman-resumed-20261013/ubuntu/first-click-core-backtrace-1/`.
All source/artifact hashes and ordinary-process/container preservation checks
pass; this diagnostic is not an acceptance run.

The preceding `first-click-core-1` archive
`1216087903edd450ee9d3243e1ee7674d3d3c381ee227bc1e403d645346979d4`
builds both Ubuntu applications and passes 34/34 enabled suites, then encounters
that startup crash. The independent native diagnostic also verifies the GNOME
activation correction: one deferred restack completes before the fresh prepared
focus is published. It then finds a fixture-only timer that remained active
after its exact PONG completed. That timer is now retired without extending
the original preparation deadline; 39 pure fixture regressions and the subsequent
native rebuild pass. The actual-Core harness has nine passing pure
wire/process-state tests.

The earlier `first-click-provider-1` source archive
`8285ea7842c1d2bd6d2e3537060767ed28da9a60452bfae5f2cf479a8c6d9386`
builds actual Ubuntu Core/Viewer and passes all 34 enabled suites, then fails
native scale 1 at first-click focus publication (`GNOME preparation focus
changed before publication`). The fixture observes one actual libei idle stop
and restart. Scale 1.25 and OFF checks were not reached. The evidence collector
verifies source/artifact hashes, cleanup and ordinary Deskflow preservation.
This is retained failing evidence for the subsequently corrected activation
boundary, not a Linux acceptance pass.

The current Mac applications also package into a separately staged ad-hoc
experimental bundle and pass the private Core/Host handshake and rejection
smoke. Evidence is `build/roadmap-completion-20261013/mac-package/`; archive
SHA-256 is `bc7afaca7ac7e2863b1257dec45ac6fd54bf15bfe7a329a7572f40c973bd7148`.
This is packaging validation only: no installation, input hooks or physical
acceptance, and no claim of exact-library native input admission.
The subsequent exact-library signed package also passes its audit and private
handshake; evidence is `mac-exact-package/` beside that directory, archive
`a5940a8cea7efc07d0bf4919d6f5fc160782ec06e2cd08837664fca2213a3867`.
Linux packaging now checks its complete runtime/diagnostic inventory, and the
Mac bundle auditor requires the Core companion and real Mach-O executables.
Eleven Linux and fourteen Mac pure packaging regressions pass. Neither package
was installed or used to start native input.

The local continuation now connects Server pre-dispatch ownership to a private
Mac coordinator, exact mapped Core/EI/GNOME/Qt validation and NativeHost original
input. Private protocol v5 carries mapped coordinates and an explicit completed
Host release; production registration remains explicit opt-in. The verified
Mutter ordering build contract, actual-connection X11 namespace witness,
painted-geometry holding relocation and immediate native lifecycle revocation
are implemented. X11 still has no proven application-input drain and cannot
authorize original input.

The subsequent Mac first-click `IPP1` Core build passes **six CTest suites /
234 Qt entries**, including 28 coordinator cases using actual framed Core
messages and a private Host child. A follow-up Core/coordinator relink also
passes all 28 cases after an atomic admission/cleanup ownership correction.
Fresh scoped ASan/UBSan also passes 28 coordinator, 62 ClientProxy and 44
ServerProxy entries; retained manifests name the instrumented implementations
and dependency archives that were not instrumented.
Evidence is `build/roadmap-completion-20261013/first-click-core-*` and
`first-click-admission-*`. The later Linux first-click integration results
are recorded above. The untouched-source rollback change separately passes the native,
holding and default pure drag-probe suites (3/3). These tests post no OS input.
The newer Mac app/capture-rate build passes both applications and ten selected
suites, including the strict 30/60 configuration and native rational-timestamp/
replay-flood regressions. Capture defaults to 30; 60 is an explicit requested
active-stream rate, with existing 10/2 inactive/hidden budgets. Live 60 fps
capture and performance have not been measured.

The earlier IPC5 batched-input, exact multi-window association and focused clipboard
checkpoint built both Mac applications and passed **18 targeted CTest suites**
(11 Core/native/policy and seven application/private-input/probe suites).
Scoped ASan/UBSan passed 45 Server and 15 coordinator Qt entries; retained
evidence names the instrumented sources and the dependency archives that were
not instrumented. Logs are in `build/roadmap-completion-20261013/`. This result
predates the first-click preparation and untouched-source rollback changes now
being integrated. No OS input or pasteboard access was used by these tests.

The home-Ubuntu `sealed-multiwindow-3` checkpoint builds and passes 34/34
enabled suites plus private native GNOME/Qt scale 1, including 16-point batches
and two exact Viewer roles. Scale 1.25 currently fails with `stale-revision`
after the multi-window sequence; its cause is under investigation. The run
stopped before OFF validation. This is a **partial, failed overall run**, not a
full Linux PASS. Its archive SHA-256 is
`f84bb459918a656f3aa8e4b4728184364cc69867061900244d9f7c45b52f864c`.
The collector verifies cleanup and preservation of ordinary Deskflow and
unrelated containers. It excludes the in-progress first-click production code.
The diagnostic-only `sealed-multiwindow-4` overlay also passes its build and
34/34 enabled suites, then fails at scale 1 when a retained seal is invalidated
during the second Viewer's idle-point check. Its archive is
`e12b4c37f9e32784b1e2fe2002115ece403d161253e265f9dbd642b8885b029f`.
Cleanup/preservation checks pass. Neither failure has been hidden by retries;
bounded diagnostics are being added to identify the native or Qt invalidation.

A frozen earlier production-coordinator candidate also passed the actual home
Ubuntu ON build (33 suites), OFF build (3 suites), and private native GNOME/Qt
runs at scale 1 and 1.25. Evidence is
`build/wifiman-resumed-20261013/ubuntu/mapped-coordinator-1/`; its archive SHA-256
is `9d6bcc4185fe445137e14fd23173a845fcdd17fc6562865b8db2236893bcc029`.
It predates retained-prefix batching, the final lifecycle changes and clipboard.
Ordinary Deskflow/configuration and unrelated containers were preserved.

Natural arbitrary-app edge dragging remains unproven: a targeted app mouse-up
is not a WindowServer grab-release acknowledgement. The owned AppKit probe
builds and its default pure tests pass; its interactive mode has not run.
Exact attached Sheet/Drawer focus returns the retained parent locally;
remote child/popup rendering remains unsupported. First-click native validation,
final packaging and hardware acceptance remain outstanding. The offline H11
evidence analyzer passes 31 synthetic parser tests, including a regression for
aggregated queue counters hiding an intermediate peak; it collects no physical
evidence and does not complete H11. The user's fresh
readiness for the physical probe is pending; no test may infer readiness from
historical confirmations.
Do not read any older “original input disabled” section below as the newest
code description; it documents the named historical revision.

## Requested cutoff and exact GNOME/Qt drain components — 2026-10-13

Implementation `bbe9ed3f2fb4d8519397bca61bb5b539082d00f6` adds negotiated,
authenticated Core cutoff/PONG/exact-resume messages and separately connects
the owned GNOME empty-queue/later-frame segment to an exact-fenced Qt input
drain. Absolute deadlines, whole EI batches, held native input, nested Qt/IME
callbacks, stale bindings and resume invalidation are covered. Independent
review found and fixed cleanup-callback lifetime gaps and a stream-wrapper
compatibility omission; destructive-callback regressions cover the corrections.

Both Mac applications build. Nine targeted suites pass, including 180 Core Qt
entries. The changed proxy/Client/Computer paths pass scoped ASan/UBSan with
54 ClientProxy and 36 ServerProxy entries. Home Ubuntu passes 31/31 integrated
application/core suites, 3/3 actual OFF suites and private native GNOME/Qt at
scales 1/1.25. Both scales prove retained-receipt revocation before expiry.
The frozen source and all artifacts are hash verified and bound to this commit
under `build/wifiman-resumed-20261013/ubuntu/final-candidate-2/`. Independent
cleanup confirms ordinary Deskflow and unrelated containers were preserved.
The preceding shutdown-fixture failure is retained; its TLS-close correction
has a failing-before/passing-after regression. See the newest `TESTING.md`
entry for commands, exact scope and hosted CI.

Exact implementation `bbe9ed3f2` passed all six jobs in
[CI 38420241004](https://github.com/JackHars/deskflow/actions/runs/38420241004)
and [source bundle 38420240757](https://github.com/JackHars/deskflow/actions/runs/38420240757).
Hosted native GNOME artifacts independently pass both scales and revocation
before expiry. REUSE lint and formatting checks pass.

P2 remains incomplete: the production controller still must join the Core
receipt and provider using the same frozen Server point/lease, release old
owners, and verify Mutter's sender-ordering build prerequisite. The provider
reports that missing prerequisite explicitly. Original-input dispatch remains
unregistered. Physical acceptance, multi-window/monitor behavior, bidirectional
edge dragging and the rest of P3–P9 remain unfinished.


## WiFiman restored: home Ubuntu baseline verified — 2026-10-13

Strict-key SSH again reaches `jack` through WiFiman. Frozen source
`2a72867e95892011a1597ecc811c5279227edf4c` passed **31/31** application/core
suites and **3/3** actual feature-disabled core suites on the home Ubuntu
machine. Private native GNOME/Qt integration passed at scales **1 and 1.25**.
The GNOME policy, readiness and fixture checks passed as well.

The run used the existing immutable dependency image in a disposable container
with no host display/input devices or network. Before/after and independent
cleanup checks verified ordinary Deskflow process identities, configuration
hashes and unrelated containers; all passed and the owned container is gone.
Evidence: `build/wifiman-resumed-20261013/ubuntu/baseline/evidence.json` and its
hash-verified `artifacts/`. This clears the connectivity/retest blocker below;
it does not complete physical input, package installation or P2/P3 integration.

## Local implementation resumed — 2026-10-13

Earlier code checkpoint: `c867af9d029897f860bc391aa68d4cafee49e601` on
`feature/seamless-windows`. The cloud changes were fetched and integrated,
including its subsequent GNOME fixture, boxed-layout and strict-reply fixes.

- `3d9be06db`: a missing advertised GNOME stage method must refuse the request
  without an earlier-snapshot fallback; a fresh successful request may recover.
- `7bc8cf332`: the Mac recovery watchdog now recognizes an exited, unreaped
  host using kernel birth identity and zombie status. A native owned-child
  regression failed before the fix and passed afterward; it uses only a private
  marker, not a native window or input session.
- `c867af9d0`: private preflight can hold its exact quiesced full-input lease
  while preparation runs, then queue one explicit continuation. The unchanged
  two-second private deadline, retained capture/selection and all admission,
  readiness and neutrality checks still apply. Direct channel preparation
  cannot bypass the wait. This connects the wait to the real private protocol;
  the production native-cutoff controller remains to be implemented.

Local Mac builds of both applications pass. Five actual core suites pass
**170 Qt entries**; seven selected application suites pass. The new deferred
preflight and its adjacent channel/companion suites pass **3/3** after rebuilding.
The watchdog has retained failing/passing proof and no leftover fixture
processes; the receipt-policy helper passes **13 tests**. Independent reviews
found no further actionable issues in these changes.

That implementation `c867af9d0` passed **all six** hosted jobs in
[CI 38410153483](https://github.com/JackHars/deskflow/actions/runs/38410153483),
including Mac/Linux integration, native headless GNOME, transport and both
sanitizer builds. Its [source bundle](https://github.com/JackHars/deskflow/actions/runs/38410153441)
also passed. Local evidence is retained privately in
`build/cloud-return-20261013/`; exact commands and scope are in `TESTING.md`.

Home Ubuntu retesting was initially blocked by WiFiman and has now passed as
recorded above. Fresh physical readiness and the outstanding Mac
App Management step remain prerequisites for installation/native acceptance.
No native user input, real-window recovery, package installation or two-machine
session was performed on this return. Original input remains disabled; the
next P2 work is one causal cutoff transaction through Core, EI, Mutter and the
exact fenced Qt viewer, followed by P3 production coordination. Multi-window,
edge-drag and physical acceptance remain unfinished.

## Earlier P2 verified GNOME StageView/native binding checkpoint (2026-10-13)

Implementation [`e117b66e`](https://github.com/JackHars/deskflow/commit/e117b66e22e7f2141b4a17b93f81e22c736f5b4c)
and its ancestors now exercise two real same-view Clutter stage updates on
the frozen native point, then continue the existing QtWayland default-queue
witness and exact post-Qt GNOME hit recheck. GJS uses the Mutter 50
`StageView.layout` boxed GObject property, immediately copying native
coordinates by value. The authenticated GIO service has a real matching
`ObserveStageFrames` method signature; its response parser rejects every
duplicate, unknown or wrong-typed field even if a substitute keeps the
total dictionary count at thirteen. Earlier actual Linux/GNOME failures
were diagnosed and fixed, not skipped or relabelled.

[GitHub CI 38408250298](https://github.com/JackHars/deskflow/actions/runs/38408250298)
**PASS 6/6 jobs**, including the real private GNOME headless integration,
macOS app/core, Linux opt-in ON and ordinary OFF, transport and GCC/Clang.
[Source bundle 38408250240](https://github.com/JackHars/deskflow/actions/runs/38408250240)
**PASS**. The cloud does not provide the user's Mac/Ubuntu hardware. These
are only read-only stage/viewport freshness proofs: **no event-specific
libei → Mutter/Clutter → Qt consumption fence exists**; original-input
delivery is disabled and all physical H tests remain NOT_RUN.
See top of `TESTING.md` for exact commands and limitations.

## P2 post-Qt native hit revalidation — verified 2026-10-13

The exact frozen stage point/native session/window/surface hit is now revalidated **after** Qt's asynchronous GUI queue witness, preventing an observation from surviving silent compositor revision or coordinate-transform changes during the wait. Runtime [`32cb0cb9`](https://github.com/JackHars/deskflow/commit/32cb0cb9c5e809f8884a6d53a2f8324d64418aa7), final test correction [`13e62c02`](https://github.com/JackHars/deskflow/commit/13e62c02d3417b162264256f12641f36300d1f46): [CI 38386740966](https://github.com/JackHars/deskflow/actions/runs/38386740966) PASS **6/6 jobs** (Linux CTest 13/13, GCC/Clang sanitizer 12/12 each, Mac, GNOME and transport) and [source bundle 38386740929](https://github.com/JackHars/deskflow/actions/runs/38386740929) PASS. This is **not** EIS-to-Mutter compositor consumption, production input authorization or physical acceptance. Original-input dispatch stays disabled; H01–H12 NOT_RUN. Rebuilt `TESTING.md` from last intact revision after a documentation interpolation error duplicated content.
## P2 connected QtWayland queue checkpoint (2026-10-13)

Implementation [`d0b8e8a3`](https://github.com/JackHars/deskflow/commit/d0b8e8a3181dc70fd2ea462e261e3a3685704a70)
runs two serial `wl_display_sync` callbacks on the real QtWayland GUI/default
queue as part of the existing GNOME private final painted-viewport `Check`.
The second callback processes queued Qt QPA window events and rechecks the
exact focus/surface/painted revision before reporting the read-only result.
The 200ms native budget is inside the existing 250ms snapshot lifetime;
revocation, unsupported builds, reentrancy, loss and pipelining fail closed.

Hosted [CI 38383713813](https://github.com/JackHars/deskflow/actions/runs/38383713813)
**PASS all 6** (GCC/Clang sanitizer 12 suites each, Mac and Linux app/core,
transport and true private headless GNOME/QtWayland 6.10.2); separate
[source bundle 38383713764](https://github.com/JackHars/deskflow/actions/runs/38383713764)
**PASS**. No local user hardware tested. **This is not native old-input
draining**: the specific EIS prefix → Mutter Clutter stage event consumption
proof and delivery ownership remain missing. Original-input routing is still
disabled, and P2/P3/H03–H05 are incomplete or NOT_RUN.
See the newest `TESTING.md` checkpoint.

## Latest P4 output-mode/identity recovery checkpoint (2026-10-13)

Implementation `ec2fd2f83fa6a308f1702036eb0bf4f3ff6dc079` introduces
local Mac physical-monitor identity, bounds, pixel-mode and rotation
fingerprints. It prevents a stale export/holding-display relocation on a
changed topology and revokes/restores exact held owners during the 500 ms
native host safety poll. Invalid, missing or duplicate display observations
cannot be interpreted as identical layouts, and the helper's owned virtual
monitor is excluded. The existing durable journal handles unresolved returns.

Portable GCC/Clang ASan+UBSan each **PASS 11/11** suites (exit 0); hosted
CI [38352550451](https://github.com/JackHars/deskflow/actions/runs/38352550451)
**PASS 6/6**, including macOS application/core and Linux enabled/disabled
compatibility; [source bundle 38352550445](https://github.com/JackHars/deskflow/actions/runs/38352550445)
**PASS**. All published code matches the sanitizer-tested local blobs.
No physical monitor was disconnected or scaled by this cloud run. H05/H06 and
all other hardware tests stay **NOT_RUN**; the compositor/Qt input-drain and
production original-input dispatcher are still *disabled*. See `TESTING.md`.

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

## Verified partial code identity (2026-10-12)

Most recent tested implementation:
[`868d9d9e6db6cb9896cd577cb2951d434117e8a3`](https://github.com/JackHars/deskflow/commit/868d9d9e6db6cb9896cd577cb2951d434117e8a3).
It extends the earlier three published native observations with an inert
reproducible Linux archive, SHA-256 verification and no-overwrite staging,
explicit Qt IME composition denial until true drain proof, and native packaged
launch smoke. Hosted GitHub Actions
[38341136093](https://github.com/JackHars/deskflow/actions/runs/38341136093)
passed all **six of six** jobs; portable GCC/Clang ASan/UBSan tests each
passed all 11 suites. The directly executed Node and Python helper suites also
passed; exact counts and commands are in `TESTING.md`.

These results are **not** full implementation. P2 native old-input drain and
P3 enabled original delivery are blocked on genuine EI/Clutter/Qt/QtWayland
ordering and identity proof, which cannot be inferred from read-only stage
hits. P4/P5/P6/P7 work and all physical H tests remain incomplete/unrun.
Original-input dispatch remains *disabled* to avoid duplicate or misrouted
input. The final SHA should not be filled in until actual code completion.

## Cloud partial integration checkpoint (2026-10-12)

Three commits are published on `feature/seamless-windows`:
`f86ee40430dc89b0612968c956c1f3c51f126df1` (fair live-TLS
multi-session keyframe recovery), `fb0bf0dd8951ada013d1cced450d196e30ac2523`
(read-only, capability-negotiated GNOME frozen-point lookup retaining capture
identity/native surface and painted Qt observation), and
`fb504a3f1db42de34e3a24c26ea337bb5dfa37dc` (real headless
Mutter stage-hit tests independent of the latest EIS cursor). Full six-job CI
passed on the first; the third's native-GNOME job passed. See
`TESTING.md` for exact claims and run links.

A separate fail-closed Viewer edit prohibits asynchronous Qt IME composition
until Qt's real input-method lifetime can be proven drained. It is tested by
direct preedit/commit injections; normal Unicode composition is explicitly
**not** supported by the experimental Viewer.

New partial P8 experimental Linux archive scripts stage a reproducible, inert
package and verified SHA manifest. They do not enable Shell extensions, replace
ordinary Deskflow, bundle system libraries or run on the actual Ubuntu machine.

**The handoff remains incomplete**: compositor/Qt/native input queue drain,
production coordinator and original event delivery, multi-window focus
ownership, full bidirectional edge drag, app compatibility, two physical
monitors and crash-recovery acceptance are still open. Never treat the new
read-only GNOME hit result as input-delivery permission. H01-H12 remain unrun.

Branch: `feature/seamless-windows` in `JackHars/deskflow`.
Starting milestone: `4070e8a57885dfac5739f88456c63efbc4521cbf`.

## Cloud checkpoint: asynchronous pre-dispatch state

Server preparation now has explicit pending/refused/accepted/revoked outcomes,
nonreused request identities, ordered polling and a separate 500 ms preparation
budget. Completed captures retain their original 100 ms arrival bound without
expiring merely because older preparation holds the queue. Exact capture,
selection, recipient and routed point remain frozen. Pending cancellation tracks
physical releases, including untranslated ups; malformed/late replies, callback
reentry/destruction and bounded-capacity failures cannot cause a legacy down.

The portable dispatcher interface is in `OriginalInputDispatch.h`; production
Server paths use it, and compatibility builds remain independently tested with
the feature enabled and disabled. Tests and actual result details are in
`TESTING.md`. The remaining native/Qt preparation service, complete native drain
and production activation are not supplied by a fixture's `Accepted` result.
No new native-input enablement is claimed at this checkpoint.

Sanitizer verification additionally corrected C++ ownership for connection and
InputFilter action events, the filter's borrowed event view, and the POSIX
architecture's initial main-thread reference. Fixture cleanup now releases
dropped notifications and a deliberately detached client. Enabled core: five
suites / 170 Qt entries; disabled: three / 31 with 29 explicit skips; ASan,
UBSan and leak detection: four / 124. All exit 0. These are cloud protocol/core
checks, not native input receipt or hardware acceptance. See `TESTING.md`.

## Cloud checkpoint: scoped private protocol (2026-10-12)

The private channel is now protocol v4: both endpoints negotiate code-support
capabilities, the offer/lease retain keyboard or all-input scope, and host
reservation/activation/release cannot downgrade it. Keyboard-only leases reject
pointer records at both sender and receiver. The real private-process channel
and preflight suites pass in the cloud; detailed commands and scope are in
`TESTING.md`. This checkpoint does not enable production original delivery.
Server asynchronous preparation and the native ordering coordinator remain next.

## Original input before legacy dispatch (2026-10-12)

Implementation `eda21b39b19004af9f4abfaacb707bb9b380f6f8` and portable linkage
fix `ef51456659edae1acf06a6c03b730bcaf66f2f8b` are pushed to the feature branch.
The Server can now decide original ownership before writing legacy input or
changing its press ledger. The production launcher connects the completed-capture
inbox but does not install the original dispatcher. Ordinary delivery retains
its existing timing. The contract is in
`deploy/windowshare/original-input-dispatch.md`.

A bounded capture inbox separates the Session's raw-stream consumer from the
Server event thread. An installed dispatcher receives only exact, authenticated,
single-part captures with their frozen recipient and routed point. Reservation
precedes delivery; accepting a reservation prevents a later legacy acquisition.
Keys, repeats, buttons, drag motion and physical releases retain exact ownership.
Selection changes flush older work before leave/enter controls; disconnected
recipients are detached first. Deferred diagnostic decisions retain source
order. Missing capture expires after 100 milliseconds, and queue/owner limits
terminate original eligibility while preserving legacy and release accounting.

Review and sanitizer runs exposed nested-disconnect double deletion, key-up
work after destruction, and stale raw-release bookkeeping. The final tests
cover those paths, callback reentry, nonreused press incarnations and untranslated
physical releases. The earlier failing logs remain preserved. The first Ubuntu
candidate also found missing portable input linkage in the actual Linux core;
the final CMake dependency is explicit on every supported platform and remains
absent when window sharing is disabled.

All nine targeted Mac suites pass, including five real core suites with 161 Qt
entries and no failures or skips. Five sanitizer suites pass; the scoped Server
run instruments modified Server/filter/key/test code, not its prebuilt dependency
archives. After the CMake fix, the actual Mac core and all five core suites were
rebuilt and passed again. Formatting, diff checks and REUSE lint pass. Evidence
is under `build/pre-dispatch-20261012/mac/`, including `linkage-followup/`.
No new Mac native input session or package installation was run.

Home Ubuntu built the actual core with window sharing enabled and disabled.
The enabled build passes eight suites: five core suites with 161 Qt entries,
zero failures/skips, and three original-input/route foundation suites. The
disabled build passes three compatibility suites with 30 Qt entries and 29
explicit feature-disabled trace skips. Actual `Server` and `EiComputer` compiler
flags are verified for both modes. All seven phases and the child/container
exit are zero. No native session was started.

All 1,031 frozen source entries match `ef5145665` Git blob bytes and modes. The
parent independently verified 39 copied artifacts, parsed test totals, checked
production flags and compared ordinary process/settings/container inventories.
The owned container and both supervisors are absent; ordinary Deskflow and the
two unrelated containers are unchanged. Ubuntu evidence is
`build/pre-dispatch-20261012/ubuntu/candidate-2/evidence.json`, SHA-256
`cd6c72e5ee4133ad8455fc99d8939ab5248708730157f805b3846911f1600b7a`;
the independent audit is `independent-verification.json` beside it. Candidate 1's
link failure and clean teardown remain available separately.

All six CI jobs pass for the final code revision in
[run 38323926279](https://github.com/JackHars/deskflow/actions/runs/38323926279).
Native compositor/Qt queue draining, exact routed-point validation and the
production coordinator remain unfinished. Original-input delivery is still
disabled. Physical acceptance and the requested Mac App Management permission
remain open; the overall roadmap estimate is approximately 50%.

The user is transferring implementation to a cloud coding environment without
access to the actual machines. `CHATGPT_HANDOFF.md` defines the remaining
implementation sequence and runtime boundaries; `TESTING.md` is the required
cloud-results and local-verification hand-back. Cloud work must implement and
test what its environment supports, leaving physical acceptance explicitly
unrun. This local chat will verify the returned candidate on the actual machines.
The local development goal is paused for the handoff.

## Full viewer input forwarding fence (2026-10-12)

Commit `d879b781b06f004e9694b8b894c26ad140fa55cc` is pushed to the feature
branch. The real Viewer and NativeHost can now pause keyboard, button, movement
and scroll forwarding together through the optional `input-route-v1` capability.
Scope belongs to the exact session/focus/geometry binding. Old peers retain
their existing keyboard-only or ordinary viewer behavior. The contract is in
`deploy/windowshare/forwarding-fence.md`.

The gate is installed before callbacks and releases; resume cannot retarget a
press that began while forwarding was closed. Exact-owner releases remain
available, and a raw network input packet cannot bypass the native host gate.
Review reproduced a pending-focus replay bug: a callback could pause and resume
before a detached input queue finished flushing. Retained scope-specific
generation markers now discard that old queue even after immediate resume.
The regression failed before the fix and passes afterward. The private
keyboard-only preflight also rejects full scope before constructing a lease,
rather than silently dropping the requested scope.

All nine targeted Mac suites pass, including both TLS backends, Viewer, Bridge,
native recovery/admission and private input/preflight/companion checks. The
viewer-only reconnect smoke and all 22 GNOME fixture unit cases pass. Formatting,
diff checks and REUSE lint pass. No new Mac native input session or local package
installation was run. Local evidence is under
`build/full-input-fence-20261012/mac/`.

Home Ubuntu built 32 actual application/core/fixture/test binaries from the
frozen source. All 30 selected CTest suites pass with zero failures or skips,
including five Qt core suites with 150 entries and the two private X11 scale
tests. GNOME provider logic, readiness and fixture checks pass 12, 11 and 22
cases respectively. The compiled production objects enable both the actual
EIS backend and the QtWayland adapter.

Actual private GNOME Shell 50.1 acceptance passes at Qt scales 1 and 1.25. Each
run records four exact full-scope acknowledgments, no forwarded key/button/
move/scroll packets in the fenced interval, and seven ordered fresh input
receipts after resume. Native cursor observations remain available while
forwarding is closed. This uses a private headless compositor, software
rendering and a synthetic source frame; physical monitor scaling, the Mac
source and human input are not part of this run. Neither an acknowledgment nor
an EIS synchronization reply is treated as compositor consumption evidence.

All 1,029 frozen source entries match the implementation commit's Git blob
bytes and modes. The parent independently verified 75 copied artifacts,
parsed the test results and exact TLS intervals, and compared the ordinary
process/settings/container inventories. The owned container and both
supervisors are absent, with zero child/container exits. Ordinary Deskflow and
the two unrelated containers were preserved. Ubuntu evidence is
`build/full-input-fence-20261012/ubuntu/evidence.json`, SHA-256
`287d569f69f724af4d1e31b2e4ef25d89018a41b5abca9ca10a1e2e9f99630b7`;
the independent audit is `independent-verification.json` beside it.

All six CI jobs and every job step pass for the implementation commit in
[run 38318502915](https://github.com/JackHars/deskflow/actions/runs/38318502915).
This includes native GNOME, both sanitizer core compilers, private input
sanitizers, the Linux compatibility build, Mac hotkey checks and packaged
private-core handshake. Complete metadata, logs and hashes are retained under
`build/full-input-fence-20261012/ci-38318502915/`. The next integration still
needs a Server decision before legacy input dispatch, native
queue draining, exact routed-point validation and the production coordinator.
Original-input delivery remains disabled. Physical acceptance and the requested
Mac App Management permission remain open; the overall roadmap estimate is
still approximately 50%.

## Negotiated input submission tracing (2026-10-12)

The captured-event stamp now follows the actual Server input write through the
existing legacy protocol into the receiving `Computer` dispatch. The EIS backend
counts its existing input/frame API calls, and an exact terminal receipt joins
back to the sender's retained token. This does not enable original-input
delivery, prove compositor or application consumption, or supply a native
device identity. The post-dispatch route callback remains unsuitable for
directly sending original input because it cannot replace the earlier dispatch.

Only an opt-in protocol 1.8 connection with live TLS and a live capture route
negotiates tracing. Generation changes, late replies, connection replacement,
packet boundaries and nested dispatch are checked. Pending records are bounded
to 256 across active and retired generations. Trace limits preserve ordinary
legacy input while recording that its trace is unavailable. Controls and older
peers keep their existing protocol. The contract is documented in
`deploy/windowshare/input-sequence-trace.md`.

Independent review found and reproduced a negotiation framing defect: a
truncated `DSOP` could borrow the following `CNOP` bytes as its generation and
emit `CISA`. The final fix bounds option decoding to the current packet and
rejects trailing or incomplete data before negotiation or handshake effects.
The failing regression is retained under
`build/input-sequence-receiver/dsop-red/`; the fixed receiver passes 32 strict,
32 scoped ASan/UBSan and six feature-off cases. Sanitizer coverage includes
modified receiver/test code, not the prebuilt dependency archives.

Implementation commit `ba22079b3d532fbd2519312c064ee9a5089e577d` and framing
follow-up `5de96808caa5c539383ce0d5953c3b72b2d10b02` are pushed to the feature
branch. The final Mac core builds and all five targeted suites pass 150 Qt
cases with zero failures or skips. Evidence is under
`build/input-sequence-20261012/mac-candidate-2/`. All 1,028 frozen candidate
source entries are checked against the committed tree, including symlink modes
and contents. No new Mac native input session or package installation was run.

The final source also passes fresh builds on home Ubuntu for both feature
settings, including the actual core and `EiComputer`. The enabled build passes
all five suites: 150 Qt passes, no failures or skips. The disabled build passes
both proxy suites: 26 passes, no failures and 29 intentional skips of disabled
trace cases. Qt totals include setup and teardown entries. Every build/test
phase and the build-only container exit is zero; no native session or input
was started. Ordinary GUI/core process identities, settings and unrelated
containers are unchanged, and the owned container and supervisor are absent.

Ubuntu evidence is
`build/input-sequence-20261012/ubuntu/candidate-2/evidence.json`, SHA-256
`b51d76ec163090ea1ab935bbf029973bf7bc1d577683e5f81f8db8284b8ab727`.
The parent independently verified all 32 copied artifact hashes, parsed the
test counts, compared ordinary process/configuration/container inventories and
checked actual EIS compiler flags in both builds. Candidate 1 remains preserved,
including its cleanup-classifier false alarm and the correcting read-only audit.

The final code revision `5de96808caa5c539383ce0d5953c3b72b2d10b02` passes all
six CI jobs in run `38315383611`: both application builds, both sanitized core
builds, transport and native GNOME integration. Every job step passed. Complete
metadata, logs and artifact hashes are retained under
`build/input-sequence-20261012/ci-38315383611/`. The Mac CI package and private
core handshake pass on the runner; local App Management permission remains a
separate requirement before updating the experimental app on this Mac.

The installed Mutter 50.1 introspection audit exposes
`Clutter.Event.get_source_device()` but no public EIS-session-to-device
association. The retained audit is
`build/gnome-attribution-audit-17a/installed-typelib.jsonl`. Native ownership and
consumption still require separate integration; a device name, current pointer
position or EIS synchronization reply cannot supply that evidence.

Physical acceptance and the Mac App Management permission remain open. The
overall roadmap estimate remains approximately 50%.

## Visible physical-test controls (2026-10-12)

The reusable physical acceptance harness now starts from a button in its own
disposable source window. Fixed instructions and a 120-second countdown appear
only after the route is ready. Before Start, the Mac core and Linux native
session remain unstarted. The old Option/Command mapping fails the setup check;
both actual hotkey registrations must report the expected Option mask before
the Linux session starts. Stop, close and status loss request bounded teardown.
The source never synthesizes input or records typed text.

The Mac coordinator retains gap-checked event history, rejects stale source
publications, drains known late control replies during cancellation and checks
fresh restoration both before and after host termination. A host shutdown
timeout preserves a bounded stack sample and remains a failed run. Cleanup of
one evidence stream cannot skip the other owned process cleanup.

The Linux helper separates preparation, startup and running deadlines. It
accepts Stop before preparation finishes and during startup. Its host supervisor
audits ordinary Deskflow processes/settings without exposing the host desktop
or process filesystem to the container. Lifecycle success remains explicitly
separate from physical acceptance. The current source builds; all 11 Mac
coordinator regressions and 20 Linux helper regressions pass without native
input. CI runs the new control tests. Usage is in
`deploy/windowshare/physical-macos.md` and `physical-linux.md`.

The same candidate passed all 61 control tests on the home Ubuntu machine
(20 Linux helper, 11 Mac coordinator, 10 relay, 11 startup-readiness and 9 viewer
fixture tests), with no skips. One actual preparation-to-Stop run passed in
2.05 seconds without receiving Start: its prepared container contained only
`docker-init` and Python, with no native session or relays. All three command
exits were zero, and the ordinary Deskflow process, settings and existing
container inventory were unchanged. Independent local verification matched
all 18 copied public artifacts and all 25 staged source files to their hashes.
Evidence is `build/physical-fixture-agent/ubuntu-gate-20261012/evidence.json`
and `independent-verification.json` in the same directory. Startup cancellation
has control regressions; this actual smoke does not exercise native startup.

The visible-controls commit `d3be1f8c75733f8377e2d9ecc67a5caaa0a36619`
passed all six CI jobs in run `38310550492`, including the new control tests,
both application builds, native GNOME integration, Mac hotkey regressions and
packaged private-core handshake. Every job step passed. Complete metadata,
logs and hashes are retained under
`build/manual-acceptance-20261012/ci-38310550492/`.

No new Mac source window or physical route was launched for this change.
The package permission and human physical acceptance remain open; the overall
completion estimate remains about 50%. Local evidence is under
`build/manual-acceptance-20261012/`.

## Physical input session preparation (2026-10-12)

The first bounded human-test session authenticated an actual experimental Mac
primary to the Ubuntu core, which opened its own EIS session and provider-owned
Qt viewer in private GNOME 50.1. A separate mutually pinned connection streamed
the disposable Mac source window. Both routes used SSH forwarding sockets;
the ordinary installations and settings were preserved. Original-input delivery
and holding-display relocation were disabled, with explicit hotkeys for entering
and returning from the private Linux screen.

The session expired without receiving any physical input: both the legacy packet
counts and Mac receiver event counts remained zero. This is an inconclusive
physical test, not an input-delivery pass. Disconnect emptied the restore journal,
left the source neutral at its original frame and stopped all owned processes,
container and sockets. Ubuntu's ordinary client PID 8762 and Mac settings remained
unchanged. Evidence and the explicit assessment are under
`build/physical-input-20261012/session-1/`.

The source recorder now retains the latest 128 events per stream, with monotonic
sequence numbers, total counts and dropped-history indicators. This permits a
bounded collector to detect missing intervals during a longer human test. The
existing native capture/input/release/restoration smoke passes with the new
recorder in 2.121 seconds, with 31 Qt and 11 native events, contiguous sequences
and no held input. Evidence is
`build/native-smoke-20261012/bounded-receipt-regression/`. This automated local
check does not replace the pending physical two-machine test.

The second session confirmed that the user could see the source and type into
it, but physical Control+Option+Shift arrows did not switch to Ubuntu. The source
recorded the exact physical chord while Linux received zero legacy packets and
the source received zero returned synthetic events. Cleanup and gap-free receipt
collection passed. This isolated a hotkey mismatch: registration reversed Option
and Command compared with ordinary key mapping and the event-tap matcher.

`OSXKeyState::mapDeskflowHotKeyToMac` now maps `alt` to Option and `super` to
Command. A regression using the exact physical flags from the failed test has
five failing cases before the fix and all ten selected cases passing afterward;
the tests use an in-memory key map and never post native input. The integrated
core builds, and CI runs only those mapping slots from `OSXKeyStateTests`.
Existing configurations that compensated for the reversed hotkeys need their
Alt/Super hotkey modifiers adjusted. Ordinary input remaps are unchanged.
Evidence is `build/physical-input-20261012/hotkey-fix-validation.json` and
`session-2/`. This result does not establish an event-tap or permission failure.

A third attempt used the unchanged installed binary's actual Command shortcut,
but received no physical input before expiry. It remains inconclusive. Linux
closed normally through its private IPC and the source was restored and neutral;
the Mac host subsequently exceeded the eight-second termination deadline and
required SIGKILL. This shutdown needs follow-up and is not a graceful-stop pass.
The actual exit statuses and joint evidence are retained under `session-3/`.

A follow-up of the unchanged installed host passes idle timer and SIGTERM exits
with status 0 (2.796 and 0.791 seconds including startup). No windows were
exported and no input was injected. This does not reproduce or resolve the
capture-session shutdown failure. The session's OS log records a system-stopped
capture stream before restoration; no stack sample was taken during the hang.
Evidence is `build/physical-input-20261012/idle-host-shutdown/` and
`session-3-host-system.log` in the same parent directory.

Packaging the hotkey fix stopped before signing: macOS explicitly denied
`kTCCServiceSystemPolicyAppBundles` to `com.openai.codex`, whose responsible app
was `/Applications/ChatGPT.app`. The user has been asked to enable App Management
for ChatGPT/Codex. Do not work around that OS permission or replace the installed
experimental bundle until it is resolved. The partial package and failed early
launch check remain under `build/packages/hotkey-modifiers-20261012/` and
`build/physical-input-20261012/package-*.log`; the existing installation is intact.
A retry must use a fresh output directory and finish packaging/verification before
running its launch tests. The source fix is committed as `f55fe8012`.
That exact revision passed all six CI jobs in run `38306411263`, including the
new Mac modifier regressions and actual GNOME integration. Complete metadata,
logs and their hashes are retained under
`build/physical-input-20261012/ci-38306411263/`.

## Actual Mac/Ubuntu synthetic round trip (2026-10-12)

Overall completion is now approximately 50%. WiFiman SSH again reaches hostname
`jack` at `192.168.1.12`. The full two-machine synthetic test passes with Qt
content scale factors 1 and 1.25, using the packaged Mac host and then its
byte-identical installed copy. The actual source frame is captured on Mac and
decoded to 1280×896 by the real Qt 6.10.2 viewer in a private GNOME 50.1 session
on the home Ubuntu machine.

Each run checks three unretried fractional motions against the fresh native
GNOME hit, painted Qt mapping and tagged Mac receiver events, followed by exact
click/scroll coordinates and F8 down/up. The target NSWindow, injector PID and
synthetic marker must match. No keys or buttons remain held. Before stopping the
Mac host or invoking recovery, disconnect must clear the export journal and
leave the source at its original frame. Both runs also pass owned process,
forwarding-socket and container cleanup. The ordinary Ubuntu client PID 8762
remains unchanged. This does not test physical input, holding-display relocation,
the ordinary Deskflow input route, drag handoff or physical monitor scaling.

Passing joint evidence is in `build/crossmachine-macos/run-{5,6}/` and
`build/crossmachine-linux/run-{5,6}/`. Earlier failed runs remain in place: source
startup readiness (run 1), Qt wheel-coordinate rounding in the test expectation
(run 2), and relay full-close/shutdown races (runs 3 and 4). All ten Linux
control/relay regressions now pass and are wired into CI. Coordinated shutdown
records encrypted Mac-to-viewer tail loss separately from fully drained EOF;
Mac-bound bytes must drain without discard. The fixture never treats an EIS
PONG or a Linux-only result as proof of Mac application delivery.

The Linux application and fixture were clean-built for the cross-machine
candidate. Their hashes are respectively
`5322c9eefbb3a093e3a5f43a491986509f9bceac59154a279b761575f861d362`
and `b70ad5eee8da76bc88780d8ba142f5013aae3932430186c0b60003ad571c9076`.
The Mac host uses commit `79de560ca3682e4fcc0bb973bc94abf1e22379c3`, which
passed all six CI jobs in run `38299021367`.

The exact-library package is now installed at
`~/Applications/DeskflowSeamless-Core-Experimental.app`; both staged and installed
audits, private launch/bootstrap checks, TLS/reconnect checks and the permission
probe pass. Installed core/host hashes match the tested package. ZIP SHA-256:
`e40076f7b4f121ff5b9a7e195674ffe8bf04d8f62678be66f1acc8af133de15d`.
The previous experimental copy is retained in
`build/packages/native-pointer-installed-backup-20261012/`.
`/Applications/Deskflow.app` was not replaced. Package and native evidence is in
`build/native-smoke-20261012/evidence.json`.

CI run `38301707752` for the cross-machine harness commit `b3d1a6430` passed
five jobs but exposed a GNOME fixture startup race before native acceptance:
Shell published disabled extension metadata before assigning its state. The
fixture now waits within its existing 40-second deadline for explicit disabled
or initialized state. Initial absence of the Shell object is allowed only before
its first reply; enabled/error metadata, owner loss, later method loss and other
D-Bus failures remain terminal. No measured native-input assertion was relaxed.
All 11 startup regressions and fresh home-Ubuntu GTK/EIS and Qt scale 1/1.25
acceptance runs pass with unchanged production binaries. The regressions are
included in the GNOME CI job. Evidence, including the failed first correction,
is under `build/gnome-startup-readiness/`; the original CI log is retained under
`build/crossmachine-ci/38301707752/`.

The next CI run, `38303125940` for `0760fcaff`, passed the five other jobs,
startup regressions, application suites and native GTK checks, but exposed a
second fixture problem in the Qt focus-loss check. Its independent GTK child
left a historical `focused: true` receipt after losing focus, and the fixture's
blind center click could reactivate the original viewer. The corrected fixture
publishes current focus and mapping changes atomically, removes that click and
requires the same child PID and focus sequence before and after one unretried
native denial. Nine focused regressions, all 11 startup regressions and fresh
home-Ubuntu Qt scale 1/1.25 acceptance pass with unchanged production binaries.
Both native runs observe `viewer-not-focused` while the independent child retains
focus. CI now runs the new regressions and retains their log. Evidence is under
`build/gnome-focus-readiness/`; the failed CI log and artifact remain under
`build/gnome-startup-readiness/ci-38303125940/`.
The focus correction and bounded recorder commit `eea9bcb49` subsequently passed
all six CI jobs in run `38305082628`; complete metadata and logs are retained in
`build/gnome-focus-readiness/ci-38305082628/`.

## Mac targeted pointer coordinates (2026-10-12)

The native own-window input failure is fixed. The public global-location setter
cleared the event's separate window-local annotation, so AppKit delivered mouse
and scroll events outside the correct native window. A narrowly scoped private
CoreGraphics getter/setter is now resolved from the system framework; the local
position is set after the global position, and both coordinates plus the window
binding are read back before PID-targeted posting. Missing APIs disable export.
No global-post or cursor-warp fallback was added. This dependency is reported by
`--probe` and remains limited to the tested macOS runtime.

The final bounded native smoke passes real ScreenCaptureKit/H.264 capture,
observed own-source focus, exact tagged native F8/A down/up sequences, motion,
drag, left click, scroll, held-right-button release, neutral key/button state,
original frame/journal restoration and process cleanup. Exit 0 in 2.693 seconds;
evidence is `build/native-smoke-20261012/final-exact-input/`. Receiver metadata
corroborates its own NSWindow ID with WindowServer PID/frame and waits for stable
initial geometry. Strict compilation and ASan/UBSan construction/journal tests
pass; logs are `build/native-smoke-20261012/native-unit-sanitize*` and
`native-unit-fixed.log`. Independent review prompted exact key assertions to
avoid Qt propagation inflating aggregate counts.
The rebuilt production host reports capture, Accessibility and targeted-pointer
capabilities available. All 15 standalone application suites pass in 100.10
seconds (`build/native-smoke-20261012/app-regressions.log`).

This local smoke does not establish physical Mac input, holding-display input,
first-click activation races or drag handoff. The subsequent two-machine
synthetic acceptance is recorded above.

The preceding GNOME viewport commit `04bf68b4c` passed all six CI jobs in run
`38296548399`. Exact-revision job metadata and logs are retained and hashed in
`build/gnome-viewport-ci/38296548399/` and `build/gnome-viewport-evidence.json`.

## GNOME native hit and painted Qt viewport join (2026-10-12)

Overall completion remains approximately 45%; the first physical two-machine
interactive window is still unverified. The current implementation joins the
GNOME provider's native hit to its owned Qt viewer's last painted geometry.
Original delivery, physical-device attribution and ordered pointer fencing are
not enabled by this change.

The provider forwards exactly one inherited Unix stream endpoint at child FD 4,
separate from Mutter's FD 3 Wayland connection. The core and child validate
descriptor type, family, connection and ownership; the provider deterministically
closes its duplicates. The child binds one live export to the pinned TLS peer,
connection, source geometry, acknowledged focus, surface and painted viewport.
Its mapping uses matching Qt private Wayland APIs for decorations and scaling,
rejects letterboxing and cannot renew a retained snapshot's deadline.

The GIO worker joins native hit, child mapping and final native/child checks
under an absolute 250 ms budget beginning at enqueue. Retained observations are
revoked by native or child invalidation, replacement, expiry or session loss.
The real EIS motion path requests this observation without blocking its input
thread. It continues ordinary injection and does not assert a motion fence.

Strict and ASan/UBSan checks passed for nine private-bus client cases and the
four scoped viewer/endpoint/Bridge/SecureTransport suites. The independent
production review found no actionable defect. Home Ubuntu candidate 1 compiled
and passed the 19 GJS descriptor checks and existing real GTK/EIS compositor
acceptance. The new Qt fixture correctly denied mapping because CMake did not
find installed private Wayland headers. Explicit matching-version private Qt
package discovery corrected that build issue. Two fixture-only lifetime/result
reporting issues were corrected before candidate 2.

Candidate 2 passes the actual GNOME 50.1/Qt 6.10.2 native viewer checks at
`QT_SCALE_FACTOR=1` and `1.25`. Three immediate, unretried motion/PONG/map rounds
at each factor agree with the actual viewer's TLS pointer packets. Decorations,
letterboxing, lost focus, stale paint and a second native window deny. Client
cleanup passes. The virtual monitor remains at compositor scale 1; this proves
Qt fractional content scaling, not a fractional GNOME monitor configuration.
Candidate 2 is a verified 1010-file working-tree snapshot based on `c64af6110`,
with archive SHA-256
`9f7f738085f431b367d74cb2737a937be3f6e31cfb38b7d41bb5ead22fb1bb25`.
It is not a clean committed revision. Its application run passed 12/13 suites
and full integrated run passed 56/57; both failures were the new Viewer
invalidation-coalescing assertion. Qt's offscreen plugin exposes an X11 native
interface with no connection. The observer now requires the actual `xcb` plugin
and a non-null borrowed connection before starting its native watcher. The
strict coalescing assertion remains unchanged.

The corrected candidate 3 passes all 13 application and all 57 integrated Linux
suites, including native X11 checks. Node logic (12), GJS descriptor cases (19),
the existing native GTK/EIS check, both actual QtWayland scale runs and real-core
provider-absence/Xvfb lifecycle smokes also pass. Candidate 3 derives from the
exact candidate 2 archive with four explicit viewer/harness updates; its archive
SHA-256 is
`d8babc2f3fc45921a7c82e1d156e8334a66e561ad2fe5b8e250208f4a0075eeb`.
The CI configuration now includes the complete application suite against the
GNOME fixture's Qt 6.10 dependencies as well as the native compositor checks. The ordinary running
Linux client was preserved. Mac verification passed all
59 integrated suites in 80.05 seconds and all 15 standalone application suites
in 99.91 seconds. Evidence is under `build/gnome-viewport-*.log` and
`build/gnome-qt-acceptance/candidate-{1,2,3}/`. The final observer correction also
passes scoped Viewer/endpoint checks with strict warnings and ASan/UBSan on Mac.

The separate exact-library Mac package was built and passed dependency/signature
audit (77 records), private core/host launch/bootstrap rejection and mutually
pinned TLS/reconnect/clean-exit smokes. It is retained at
`build/packages/gnome-viewport-20261012/DeskflowSeamless-Core-Experimental.app`,
with ZIP SHA-256
`76640ca9276b676bf85944907ef8617fa65e2e83b6a7a8ede3f19fb4fd69c325`.
It was built before the Linux-only observer correction and the ongoing Mac
native input fix. It has not replaced the installed experimental package.

A new bounded Mac own-window native smoke captured and decoded 1280×896,
delivered F8/A key events, and released the held key. It failed mouse buttons
and scroll: the source recorded zero such events. No smoke processes remained
after cleanup. This is a concrete remaining native input bug, under diagnosis;
it is not a successful interactive-window test. Receiver-side diagnostics now
show the pointer events reach the correct, active native window but carry
coordinates outside it; the event construction path is being corrected.
Exact commands, result, log and
cleanup are retained in `build/native-smoke-20261012/`. No holding display or
physical Mac-to-Ubuntu input was exercised by this local synthetic test.

## Input-route observation and GNOME client integration (2026-10-12)

The project remains before the first verified interactive Mac window on Ubuntu.
Original-event delivery is still disabled. The roadmap in
`docs/seamless-windows.md` distinguishes the completed transport, packaging and
test foundation from the remaining single-window, multi-window/two-monitor,
bidirectional drag, compatibility, recovery and performance milestones.

Captured Mac event identities now survive the actual Event, InputFilter and
Server route. A bounded core-to-Qt queue records the authenticated selected peer,
actual recipient and core coordinates; a bounded historical join pairs those
decisions with completed capture records. Filtered, expanded, uncertain, local,
broadcast, missing and late routes deny original-input eligibility. Retained
route decisions share terminal revocation. This observes real core decisions;
it does not install a production original-input authorizer or establish the
remaining physical-neutral boundary, coordinate authority or pointer fence.

The explicit Linux client option `--windowshare-gnome-config FILE` now selects
the experimental GNOME 50 provider. Its closed configuration fixes the viewer
arguments. A private GIO worker requires the provider and GNOME Shell to share
one unique bus owner, receives the provider-owned EIS descriptor and launches
the provider-owned native Wayland viewer. The worker starts only after the
Deskflow event queue is ready. Startup failure, owner loss and EIS disconnect
retire the session and exit unsuccessfully without a portal replacement; queued
input cannot restart emulation. Ordinary client startup remains unchanged.
Clipboard integration is not implemented for this experimental mode.

Local verification passed: 14/14 standalone application suites (214.43 seconds),
58/58 integrated suites (167.01 seconds), and an ordinary Mac core build.
Focused route, capture, session and Server tests passed strict and ASan/UBSan
builds; the bounded route queue also passed its 20,000-record TSan exercise.
Five private-bus GNOME client protocol/lifetime cases passed strict and
ASan/UBSan builds. Linux-only CLI branches were compiled and tested on macOS;
that is parser coverage, not Linux backend execution. Evidence is in
`build/gnome-route-*.log`, `build/gnome-client-agent/`,
`build/original-route-agent*/` and `build/original-route-server-*/`.

The new native headless GNOME 50 CI fixture passed on `d9dfa15a9`, run
`38284680572`: the real provider and EIS devices, caller/generation isolation,
provider-owned Wayland viewer binding, three motion/PONG/immediate native-hit
rounds matching actual GTK surface events, stale-revision denial, caller-loss
EIS revocation and viewer termination, and a fresh session after revocation.
This establishes compositor observations, not pointer-device attribution, Qt
viewport mapping or production delivery authority. The initial compositor cursor
at (0,0) triggered GNOME's hot-corner barrier; fixture setup now moves away and
waits for the first native surface hit before the measured assertions. These
tests use private buses, runtime/settings and software
rendering, without a user's display or devices. Retained logs and results are in
`build/gnome-provider-harness/ci-38284680572/` and its adjacent `.log` file.

The first combined CI run `38285549947` on `a864739bc` passed Mac application,
integrated core and package checks, the Clang foundation job and transport.
The real Linux backend compiled and its targeted core tests passed. Follow-up
test corrections use a reference in a GCC-strict tuple loop, wait for initial
GNOME surface readiness, and distinguish Qt's portal metadata discovery from
forbidden input/session fallback in the private-bus core smoke. Full job logs
and exact source revisions are retained under `build/gnome-route-ci/`; focused
passes alone must not be interpreted as a passing all-job run.

The corrected revision `6d96b1546c728ac3a8273f17ffd0f04492bc3f13` subsequently
passed all six jobs in CI run `38287148997`: GCC core, Clang core, transport,
Linux application, Mac application and the native headless GNOME provider.
The Linux job passed 12 application suites, three targeted integrated suites,
the private-bus provider-absence check, and private Xvfb core/viewer checks.
The GNOME job passed its logic tests and real EIS/Wayland motion and lifecycle
checks. Full logs and a result summary are retained in
`build/gnome-route-ci/38287148997/`. These results apply to that exact source
revision and do not establish physical two-machine input or drag handoff.

The updated exact-library experimental package is installed at
`~/Applications/DeskflowSeamless-Core-Experimental.app`. Staged and installed
bundle audits passed with 77 permitted library records/slices. Both copies
passed private core/host launch/bootstrap rejection and mutually pinned TLS,
reconnect and clean-exit smokes. The prior experimental installation is retained
under `build/packages/gnome-route-integration-installed-backup-20261012/`.
The ZIP is
`build/packages/gnome-route-integration-20261012/DeskflowSeamless-Core-Experimental.zip`
with SHA-256
`f550a2702a625944cc94a6827e8b72da535a04b08d093754431156a8a23f280e`.
It still requires Intel macOS 26 and is not notarized. The ordinary installation
was not replaced. The local verification record is `build/gnome-route-evidence.json`.

WiFiman access was restored later on 2026-10-12. A targeted port check and SSH
through the tunnel reached `jack@192.168.1.12`; the remote hostname is `jack`.
Existing key authentication worked. A fresh tracked-source snapshot of
`6d96b1546c728ac3a8273f17ffd0f04492bc3f13` was transferred and verified against
all 998 tracked files, with no extra source files. Its archive SHA-256 is
`dbb4d30e2257d053d49916f740d9316a5bebc93b5a94905b807dbd0865c0a061`.

The current home-Ubuntu validation passed:

- The full experimental integrated build, then all 56 integrated suites across
  the initial run and a focused correction run. Initially 54 passed and the two
  GNOME fixture suites rejected their temporary executable files because Docker
  mounted the private `/tmp` with `noexec`. Adding `exec` only to that disposable
  container mount made both pass; no source change or integrated rebuild was
  required. The initial failure and corrected commands are retained.
- The standalone application build and all 12 application suites (47.21 seconds
  in CTest).
- All 11 foundation suites: ten instrumented with ASan/UBSan and one marker-only
  suite without sanitizer instrumentation.
- Real Linux core startup with an absent GNOME provider on a private bus,
  private Xvfb core observer/default-off behavior, and the actual viewer's
  mutually pinned TLS, reconnect and clean-exit smoke.

These builds and tests ran on Ubuntu 26.04.1 in isolated dependency containers
with private displays/buses and no real desktop input. D-Bus test dependencies
were added to a separate container image. The ordinary installed/running
Deskflow was preserved. Remote sources/builds/logs remain under
`/home/jack/src/deskflow-seamless-x11-20261012/`; local evidence and copied logs
are under `build/ubuntu-home-6d96b1546/`.

Read-only inspection found GNOME 50.1 on Wayland and one active Samsung C32F391
HDMI display at 1920×1080, 60 Hz, scale 1. Mutter and DRM report no second
connected output. The Mac lid is now open, and the installed experimental app's
read-only probe reports Screen Recording and Accessibility permission. No
physical input, two-monitor handoff or drag-back was verified in this run.
The next implementation joins the provider's native surface/hit observations
to the production Qt viewer's last painted viewport, before the remaining
pointer fence and original-input coordinator. The full goal remains incomplete.

## Previous pairing and code-admission integration (2026-10-12)

The roadmap is now summarized near the top of `docs/seamless-windows.md`. The
project remains before its first verified interactive Mac window on Ubuntu.
The next acceptance test is one window with capture, focus, keyboard, pointer,
scrolling and safe return. Multiple windows/two monitors, edge drag in both
directions, compatibility, recovery and performance remain later milestones.

The companion configuration now accepts up to eight explicit one-to-one pairs
of Deskflow and WindowShare TLS certificate fingerprints. Missing/empty policy
denies every original-input pair. Configuration and inherited-host parsing
reject malformed or ambiguous pairs, duplicate JSON keys, wrong modes and
public argument injection before side effects. Both processes retain immutable
policy; configuring it does not enable input delivery.

An optional asynchronous code-admission guard now binds the exact sealed
non-Apple library policy to the authenticated core/host channel. It requires
the live bundle-main anchor to be the Core endpoint; a separate ancestor cannot
supply retained authority. One worker per process performs synchronous native
inspection without borrowing Qt objects. Owner polling never waits for its
mutex, enforces an absolute deadline and discards late results. Retained proof
copies recheck the original Qt thread, channel freshness/identity and process
births, and share terminal revocation. This establishes executable/library
admission, not current mappings or process-memory attestation; Apple/system code
remains an OS exception. Privileged compromise and defects or deliberate private
descriptor delegation by admitted code are outside that boundary.

Core and host request this guard after authentication only for nonempty pairing
policy. Neither creates the original-input coordinator or installs its authorizer.
The remaining route needs ordered per-press/per-scroll ownership through actual
core decisions, an authenticated coordinate provider and a pointer fence.
Public Mac state reads cannot establish a complete global physical-neutral
boundary. The existing neutral-boundary interface therefore stays default-deny.
`docs/seamless-windows-coordinate-plan.md` records the X11 connection-witness and
source-order design, and the separate compositor provider needed for GNOME
Wayland. Those coordinate providers are not implemented by this milestone.

Focused strict and ASan/UBSan pairing, channel and code-admission suites passed.
A disposable exact-signed fixture also passed real asynchronous inspection on
both authenticated endpoints and verified retained-proof revocation on close.
The fixture did not start NativeHost, capture, event taps or a display helper.
The final standalone application build passed all 13 suites in 124.35 seconds,
and the full opt-in Deskflow build passed all 56 integrated suites in 90.75
seconds. C++ formatting with clang-format 20.1.0, Python syntax, diff checks and
REUSE licensing checks passed (950/950 files).
Read-only reviews found and resolved an ancestor-lifetime gap and a result
publication failure path; the final wiring review found no actionable defects.
Logs use `build/dual-pin-agent/`, `build/code-admission-*.log` and
`build/paired-admission-*.log`.

The exact-library experimental package was rebuilt and its source-bundle
handshake/bootstrap-rejection and mutually pinned TLS/reconnect smokes passed.
It is installed at
`~/Applications/DeskflowSeamless-Core-Experimental.app`, with the prior
experimental installation retained under
`build/packages/paired-code-admission-installed-backup-20261012/`.
Both staged and installed bundle audits passed with 77 library records/slices
and permitted hashes. The installed copy also passed the handshake/bootstrap
rejection and viewer TLS/reconnect smokes. The ZIP is
`build/packages/paired-code-admission-20261012/DeskflowSeamless-Core-Experimental.zip`
with SHA-256
`59b33a8bffc7f6ddfa4303ad1b91f29455ccc46414124e1b3124f1976b478979`.
This Intel package still requires macOS 26 due to bundled dependencies and is
not notarized. The ordinary Deskflow installation has not been replaced.

SSH to the previously verified Ubuntu address now times out; its route goes
through the local gateway rather than the home tunnel. The Mac lid is still
closed. No new home-Ubuntu run, Wayland interaction, physical input or window
restoration tests ran for this milestone. The Ubuntu results below remain
valid for their recorded earlier source revision. The full goal is incomplete.

## Previous Linux observation and active input handoff (2026-10-12)

SSH key access through the home-network tunnel was verified, and an isolated
source/build directory was created at
`~/src/deskflow-seamless-x11-20261012`. The machine runs Ubuntu 26.04.1 with a GNOME
Wayland session and an RTX 4090. Read-only display inspection found one active
1920×1080 Samsung display, not a verified two-monitor arrangement. Build
dependencies live in a separate Docker image; the ordinary running Deskflow
installation and system binaries have not been replaced.

The opt-in Linux core can now observe its actual Xlib/XCB injection connection
with `client --windowshare-observe-x11`. The viewer independently observes its
actual Qt native connection and painted surface. Bounded, expiring observations
include native root/client-pointer identity, RandR topology, window origin,
focus, DPR and letterbox geometry. The observer preserves event-queue ownership
and existing root masks. Its private notification watcher is verified before
publishing observations, including when initial notifications invalidate the
pending setup. A native regression caught and removed a GetInputFocus query
that could implicitly select an unset XI client pointer.

These are local observations, not authenticated cross-connection coordinate
authority. Unix peer credentials do not prove an X-server incarnation. Xwayland
evidence is rejected, but its absence does not prove native Xorg. No Wayland
coordinate provider, pointer fence or production original-input controller is
enabled.

The tunnel route later disappeared and SSH timed out before the final startup
correction could be copied to Ubuntu. The recorded Ubuntu results below cover
the Linux observation/active-handoff milestone (`aab8467fb`); the subsequent
bootstrap deadline correction was checked locally. Consult the final commit's
CI results for additional Linux validation.

The host now distinguishes quiesced, activating and active original-input
ownership. Private Commit must activate the exact binding before acknowledging;
active ownership survives the setup timeout and remains checked through the
existing channel heartbeat while keys/buttons are held. Reentrant cancellation,
expiry, focus/geometry loss and terminal shutdown revoke ownership. This closes
an integration gap without enabling original event delivery.

Recorded checks:

- Ubuntu: all 10 foundation suites passed with ASan/UBSan in the build container,
  and the same binaries passed directly on the Ubuntu host.
- Ubuntu: all nine standalone application suites passed, including real private
  Xvfb viewport tests at 100% and 125% scale. All seven non-Xvfb suites also passed
  directly on the host. The native viewport tests passed with ASan/UBSan.
- The real Linux core compiled and passed argument checks and a private-Xvfb
  lifecycle smoke: observation is off by default, explicit opt-in reads the
  actual backend, and both clients shut down cleanly through their own IPC.
- The complete Linux application/core build and all 52 integrated CTest suites
  passed in 46.82 seconds, including both newly registered native viewport suites.
- The Ubuntu viewer passed the direct-host mutually pinned TLS, refused-connect,
  reconnect and clean-exit smoke.
- Mac: the full opt-in build and 55 integrated suites passed; the standalone app
  and all 12 suites passed. Both Bridge TLS backends and private preflight passed
  strict and sanitizer checks. The ordinary core also compiled.
- REUSE passed for 946/946 files. Logs are retained under
  `build/x11-active-linux/` and `build/x11-active-*.log`.
- A final read-only review of Preflight activation, active heartbeat and Bridge
  authorization found no actionable defects. Changed C++ files pass the
  repository's clang-format 20.1.0 check.

Xvfb does not implement SetCrtcTransform on this machine: current and pending
transform fields were read, but transform mutation was not exercised. These
tests did not use or alter the user's Wayland desktop. The Mac lid is still
closed; live capture, physical input, holding-display and window restoration
tests remain pending. There is still no verified interactive Mac window on
Ubuntu, and the full goal remains incomplete.

The packaged smoke then exposed a startup deadline issue: concurrent signed-code
verification could outlast the old five-second companion budget. Bootstrap now
uses separate absolute 15-second startup and handshake budgets, capped at
30 seconds. Handshake traffic cannot extend its budget; authenticated heartbeat
expiry remains 2.5 seconds. Every signature/identity check remains, and deadline
admission is rechecked after synchronous validation and before authentication.
Diagnostics report the stage and elapsed time.

The correction passed both strict and ASan/UBSan channel/companion suites,
including late queued hello/Ready rejection, delayed valid authentication,
callback freshness and post-verification expiry. The rebuilt Mac app passed all
12 suites in 169.99 seconds. The expanded sanitizer channel suite took 57.30
seconds under shared build load, so its CTest harness timeout is now 90 seconds;
production deadlines are unchanged by that harness setting. A separate read-only
review found no actionable defects. Logs use `build/cold-start-*.log` and
`build/bootstrap-budget-*.log`.

## Previous emergency-restore integration (2026-10-12)

The companion-enabled core now turns a captured Control–Option–Shift–Escape press
into terminal companion shutdown. The Qt consumer checks the sticky emergency
before epoch liveness, closes selected-peer publication and stops the owned
companion without a nested wait. Barrier failure revokes continuity without
turning off the observer, so a later emergency remains visible. Ordinary
Deskflow keeps running and retains its existing press/release handling; the
observer does not consume the chord or synthesize releases into that route.

The host latches private-channel closure across startup. Terminal shutdown blocks
new exports, packets, listener admission and reconnects before releasing owned
input and attempting restore-all. Startup closure cannot be lost by calling quit
before the application event loop. This uses the existing authenticated socket
EOF and needs no new private protocol message. Failed native restores remain in
the journal; a clean child exit is not proof every window was restored.

This requires an active core tap and a fresh Escape press. Original input delivery
and physical-neutral/viewport authority remain disabled or unfinished. Starting
sharing again requires a new core session. The Mac lid remains closed and Ubuntu
SSH timed out, so live desktop restoration remains unverified.

Recorded checks for this source state:

- The full opt-in build and all 55 integrated CTest suites passed.
- The standalone app build and all 12 CTest suites passed.
- Capture recognition, companion lifecycle and both Bridge TLS backends passed
  strict and ASan/UBSan runs. Regressions cover non-emergency lookalikes, rescue
  after overflow or barrier failure, nonblocking child shutdown, continued core
  event processing, destructive callbacks, pre-exec closure, terminal admission
  and release/restore ordering through nested revocation.
- Read-only review of the Session and host lifetime changes found no actionable
  defects. Formatting, diff and REUSE checks passed (941/941 licensed files).

These checks use constructed private events, fake native hosts and loopback TLS.
They do not establish live AX restoration, physical hotkey behavior or the full
seamless-window goal, which remains incomplete.

## Previous mixed-input milestone (2026-10-12)

The companion-enabled Mac core now attaches a preallocated, single-use capture
source to its existing event tap before Deskflow translation. Keys, motion,
buttons and two-axis scroll share one sequence and retain their original flags
and timestamps. Qt drains the bounded queue outside the tap. Source stop,
malformed input, overflow, tap disable, companion loss and server shutdown revoke
retained continuity; the emergency chord remains visible after overflow. This
observer does not suppress, forward or post original input.

Private protocol v3 transports the mixed records within the existing 256-byte
limit (236-byte maximum), with a capture epoch bound into its 120-byte offer and
184-byte lease. Preflight requires a retained live core-local capture token.
Same-epoch lease changes preserve replay counters; newer epochs require fresh
preparation, and older epochs cannot return. No production authorizer or
preflight controller is enabled.

Private Mac event constructors verify their exact readback and reject lossy
Quartz conversions. Tests preserve signed32 point/fixed-scroll limits and reject
oversized native mouse/line-scroll values on this Intel runtime. Two event-tap
lifecycle bugs are also fixed: permission loss no longer joins the tap from its
own callback, and queued stop survives shutdown before CFRunLoopRun starts.

Recorded checks for this source state:

- Full opt-in build and all 55 integrated CTest suites passed.
- All 13 foundation suites passed normally and with ASan/UBSan.
- Standalone app's 12 suites passed across the full run and the final companion
  rerun after migrating its lease fixture to v3. Separate strict and sanitizer
  channel/preflight suites and private Mac adapter suites passed.
- The ordinary core (`BUILD_WINDOWSHARE=OFF`) compiled successfully.
- Formatting, diff and REUSE checks passed (941/941 licensed files).

Physical neutrality remains Unknown: no complete physical-device/scroll provider
or fence against queued native events exists. Observer checkpoints and HID state
reads cannot substitute for that provider. Original input delivery, viewport
authority and, at that milestone, core-triggered restore-all remained unfinished.
The Mac lid was closed and Ubuntu SSH timed out again; no interactive native,
tap, posting or display tests ran during this milestone. The full goal remains
incomplete.

## Implemented in this milestone

A separate, default-off native Mac capture host and Qt viewer application now use
the existing session/protocol/geometry foundation and mutually pinned TLS.
ScreenCaptureKit, VideoToolbox, targeted Accessibility/CGEvent input, a persistent
restore journal, focus and geometry fences, bounded media queues, and independent
viewer windows are implemented. The old unused WindowSession/InputOwnership
scaffolds were removed in favor of the actual runtime and regression suite.

The Mac Deskflow backend has opt-in synthetic-event and holding-display exclusions.
The server retains each physical press's original recipients through screen
switches and disconnects, and releases their input before an orderly close.
The private virtual-display adapter runs in an owned helper process so it can
remove its display while the host stays alive. Application builds, native smoke
tools, CI jobs and a separate Mac packaging script are included.

The viewer now reconnects to the same approved endpoint after transient network
loss, with bounded exponential backoff, explicit Retry/Cancel, fresh catalogs,
and cleanup before accepting a replacement connection. Authentication/protocol
failures stop automatic retries. Old viewer callbacks cannot affect a new
connection, and closing the last viewer preserves a headless connection.

The core now exposes a retainable authenticated peer ticket after the complete
trusted-peer handshake. Each ticket has a fresh connection incarnation and an
atomic revocation fence; retained copies become invalid before connection-loss
notifications, half-close, fatal errors, reset and destruction. Encryption-only
clients provide no ticket. A TLS close
deadlock found during this work is fixed by quiescing multiplexer callbacks
before locking and destroying TLS state.

Original-keyboard tracking, press ownership and Mac event construction are
implemented as isolated helpers. A private inherited core/host socketpair adds
bounded messages, process-birth checks, expiring leases and exact-owner
revocation. These helpers are not yet wired into a running input route: they
do not register an original-input tap or post original OS input. Integration must
bind both authenticated connections to an explicit approved fingerprint pair
and coordinate a neutral keyboard boundary with viewer quiescence.

The optional `keyboard-route-v1` handshake now fences Qt keyboard forwarding
without interrupting pointer delivery. Viewer-held keys are released before an
exact session/focus/geometry acknowledgment; the native host independently checks
focus, geometry and its empty Qt ledger before closing network key admission.
Invalidation preserves the fence until synchronous local revocation and an exact
viewer-resume exchange. Reentrant cancellation, monotonic deadlines and deferred
controller notification prevent stale authority. The production executable has
no local route controller installed, so original capture/injection is still off.

The opt-in Mac core now supports an explicit packaged companion launch and a
handshake-only diagnostic. The inherited FD is protected before Qt starts;
process trust comes from fixed role paths, kernel PID/birth identities, and exact
code hashes bound to the running bundle main's sealed resources. The host must be
the core's direct child. A separate core-main bundle supports direct CLI launch;
the standalone WindowShare bundle remains independent. Strict bounded host JSON
cannot override the executable, trust identities or private FD. Startup, channel
loss and owned-child teardown fail closed; original authorization stays unset.
This verifies packaged code, not complete loaded-module integrity. Child launch
strips ambient DYLD and Qt/QML plugin search overrides, but activation still
needs an enforceable module-integrity policy.

The next integration slice installs the isolated Offer → Candidate → Quiesced
preflight before the private channel's Prepare exchange, once the activation
providers below are available.
The main Qt thread must own private IPC; the core runs a separate blocking event
loop. Bind accepted active client connections to revocable immutable snapshots,
never names or proxy pointers across threads. Original keys and pointer intent
must share capture order; current OS-to-Qt mouse delivery cannot preserve a source
token. Resolve that ordering before activation rather than using latest flags.

The selected-client foundation now copies only server/recipient incarnations,
selection generation and the TLS ticket. Selection changes, disconnects,
configuration/broadcast changes, screensaver transitions and shutdown revoke
retained snapshots before callbacks or mutations. A failed switch leaves no
authority; nested transitions publish only their completed final selection.
The core publishes one latest immutable value through a bounded mailbox every
20 ms after its event loop starts. Each observation expires after 250 ms even
when timers stall; refresh cannot extend an already retained observation.
The Qt-owned companion Session closes the mailbox before channel/child/session
teardown, while ServerApp clears it before nested shutdown loops. No proxies,
streams, queued Qt calls or pre-ready EventQueue events cross this boundary.
This foundation remains inactive input metadata: no original authorizer or
Offer/Candidate/Quiesced coordinator has been installed in production.

The private v2 protocol and isolated preflight coordinator now implement exact
Offer/Candidate/Quiesced/Cancel bindings. Offers contain core server, recipient
and selection identity; leases add both TLS incarnations, `focusRequest`, and
separate private-route and keyboard-fence generations. Explicit immutable policy
admits at most eight unambiguous dual-pin pairs. The coordinator checks independent
local connection evidence, the same retained selection, and one absolute two-second
deadline. Required module-integrity, ordered-input and neutral-boundary providers
default to denial. A bounded control FIFO preserves transport ordering and defers
reentrant replacements until owner releases complete.

WindowShare TLS now publishes revocable tickets and immutable six-second read
observations; queued late reads cannot refresh expired authority. Bridge can
reserve an exact native focus candidate without requesting a viewer fence, then
start that reservation and independently query quiescence. A stalled reservation
expires without renewing its deadline. Production policy loading/controller
wiring remains pending, as do capture ordering and module integrity. Original
capture and injection remain off.

Three activation prerequisites now exist as isolated, default-deny foundations.
OriginalInput carries keys, buttons, motion, scroll and checkpoints through one
bounded SPSC stream. Retained epoch revocation survives popped records, overflow
and shutdown. Neutral cutover requires complete trusted physical coverage and a
consumed checkpoint committed by the producer; unknown keys, buttons, Caps Lock
physical state or pending scroll momentum cannot be inferred away. A mixed press
ledger retains each original owner and releases ordinary keys/buttons before
modifiers. Both keyboard ledgers reject undelivered downs without manufacturing
release debt and preserve release order on a conflicting modifier up.

Viewport leases copy exact route, connection, coordinate-space, output and layout
identities. A 250 ms host-observation lifetime, independent delivery validation
and original press ownership govern mapping and drag clamping. Refresh cannot
extend an old observation or preserve press ownership across an expiry gap.
The only accepted coordinate kind is explicit X11 root geometry; an authoritative
X11 provider and any Wayland provider remain unimplemented. Pure geometry mapping
does not grant delivery authority, and neither motion nor scroll ownership is
implemented by the mixed key/button ledger.

Optional restricted-library packaging signs the dependency closure with one real
Apple-issued Team and enables hardened runtime plus explicit library validation
on fixed executables. Static audits cover each architecture, fixed executable paths,
Team and absent/empty entitlements. PeerTrust can inspect live self, peer and
anchor signing facts around its existing exact identity checks, but returns only
an immutable historical snapshot. Apple-or-same-Team admission does not establish
an exact permitted image set or satisfy the preflight module-integrity provider.
These admission foundations install no controller or input authorizer. The
latest passive capture observer is described above.

## Recorded validation

Local environment: Intel Mac, macOS 26.1, AppleClang 17, Qt 6.8.3, FFmpeg 8.0.1.
The widget/application build uses the installed macOS 15.5 SDK for Qt's AGL link.

- All 10 isolated foundation suites pass with strict compiler warnings and
  ASan/UBSan. The mixed stream also passes TSan with 40,000 threaded records.
  Regressions cover queue overflow/revocation, contradictory physical snapshots,
  incomplete coverage, Caps Lock, scroll uncertainty, checkpoint races, owner
  tombstones and modifier release order. Removing either keyboard rejection or
  removed-up ordering fix makes its regression fail. Viewport tests cover
  fractional DPR, letterboxing, stale identities, expiry during a mutex wait,
  renewal gaps and independent ownership/proof revocation.
- The final foundation sources build in the complete standalone and integrated
  opt-in configurations; all 12 standalone and 52 integrated CTest suites pass.
  Eight packaging fixture tests, formatting, whitespace and REUSE validation
  (928/928 files) pass. The default-off configuration is unchanged by this slice.
- Restricted-library policy and PeerTrust fixtures pass strict and ASan/UBSan
  suites. A real Apple Development signed, hardened fixture package passes live
  inspection in both roles. Eight headless OS admission checks confirm startup
  and later same-Team loads, reject unsigned/ad-hoc library constructors, and
  confirm ordinary-loader controls can load the same ad-hoc fixtures. They do
  not test a different Apple Team or exact-image constraints and never start
  NativeHost, register input hooks or mutate a display.
- After preflight v2, all 30 default-off Deskflow, all 12 standalone app and all
  49 integrated opt-in CTest suites pass. Strict and ASan/UBSan preflight suites
  cover missing integrity/neutral/ordering/release providers, both independent
  identities, deadline expiry during a blocked mailbox read, exact focus binding,
  queue overflow, retired/future traffic, policy replacement and destructive
  callbacks. Consecutive CancelOffer/Original and immediate activation regressions
  fail when their ordering fixes are removed. Pending owner releases cannot start
  a replacement attempt. These tests use fixture processes, not OS input.
- Private v2 framing strict and ASan/UBSan suites pass with golden 112/176/209-byte
  payload checks, full-width private counters, JSON-safe host counters, role/length
  rejection and unchanged default-deny input. Transport evidence and Bridge
  reservation suites pass strict and ASan/UBSan with OpenSSL and SecureTransport.
  TLS tests cover retained expiry, queued late reads, blocked packet callbacks,
  immediate terminal revocation and same-pin reconnects. Leak detection is disabled
  for the unsupported Mac runtime; address and undefined-behavior checks are active.
  Formatting, whitespace and REUSE validation (918/918 files) also pass.
- All five isolated core suites passed. Protocol, input-routing and original
  keyboard suites use ASan/UBSan and strict compiler warnings; platform-marker
  tests also pass. The keyboard suite checks 12,000 deterministic interleavings
  against an independent recipient model, and Mac tests construct private
  CGEvents in memory. Neither is hardware/layout/IME proof.
- All seven isolated suites pass; the new selected-peer mailbox and core timer
  publisher use strict warnings and ASan/UBSan. Mailbox
  concurrency tests also pass TSan. Tests cover immutable freshness, replacement,
  immediate revocation, terminal closure/overflow, startup readiness, stale timer
  callbacks, reader failure, reentrant destruction and restart. Companion/Session
  regressions verify mailbox closure before callbacks and after channel loss or
  Session destruction; strict and sanitizer runs pass.
- After the ticket and mailbox changes, the complete default-off Deskflow build
  passed all 30 existing CTest suites, and the standalone experimental app passed
  all 11 suites. The full opt-in integrated build then passed all 48 suites.
  These are separate from desktop input/display validation.
- All 26 real TLS identity cases and all 14 fake-platform Server cases pass
  normally and with ASan/UBSan instrumentation of the changed implementation and
  test sources. Retained TLS proofs are invalid before error publication; same-pin
  reconnects have distinct incarnations; half-close/reset/fatal paths are terminal.
  Server tests cover failed switches, independent TLS loss, removal, configuration,
  broadcast, screensaver, shutdown and counter exhaustion.
- TLS tests pass with both OpenSSL and SecureTransport, including wrong pins,
  invalid dates, wrong certificate purpose in both peer roles, and explicit
  certificate errors. Identity generation also passed with Apple's LibreSSL.
  The native backend evaluates only the approved leaf as a private trust anchor;
  it does not change keychains or waive certificate policy failures.
- All eleven standalone application CTest suites passed after the final fixes:
  private local input, early bootstrap/configuration, owned companion lifecycle,
  signed process trust, both TLS backends, viewer, both connection-controller
  backend runs, read-only holding-display probe and native recovery journal.
- The integrated opt-in core and its argument-parser regression target build;
  the parser tests pass. The packaged, certificate-signed x86_64 core/host probe
  passes the real mutual handshake, default-deny input and confirmed normal
  child exit. Invalid flag combinations and an untrusted socketpair-owning parent
  are rejected before native actions. A measured full probe took 4.29 seconds on
  this machine; it is not a hard bound on synchronous Security.framework work.
- New companion/session tests pass with strict warnings and ASan/UBSan. They
  cover default-deny authorization, startup expiry at use, child death,
  nonzero/crashed exits, graceful EOF drain, terminate/kill escalation, reentrant
  stop/destruction and nested-loop deletion. PeerTrust signed fixtures pass
  strict and sanitizer runs for wrong role/birth/path, direct nested launch,
  resource tampering, sibling substitution and whole-bundle resealing. Bootstrap
  sanitizer tests pass, including malformed FDs, CLOEXEC/ownership, duplicate
  configuration keys, unknown fields, embedded NUL and nonregular files.
  A custom-grace regression completes 2.2 seconds of child cleanup and confirms
  normal exit. Actual native hosts get five seconds after EOF and five more after
  SIGTERM before forced termination, allowing more time for AX window restoration.
- Viewer tests independently passed with ASan/UBSan and strict compiler warnings,
  including focus revocation, deliberate reacquisition and stale-reply rejection.
- Connection-controller tests passed normally and with ASan/UBSan. Real TLS
  fixtures verify delayed catalog enumeration, bounded encrypted output, deferred
  keyframe recovery without repeated encoding while blocked, and cleanup of a
  replaced viewer incarnation.
- Reconnect regressions pass with OpenSSL and SecureTransport. They cover
  refused connections, established connection loss, backoff/reset/cancel,
  terminal authentication/protocol errors, held-input cleanup, fresh catalogs,
  stale viewer callbacks and application lifetime. A volatile queue-drain test
  now records the instant of success so a subsequent heartbeat cannot turn an
  already successful observation into a false failure.
- Real core TLS tests pass for trusted peers, wrong pins, missing client
  certificates, encryption-only connections, filtered streams and immediate
  proof revocation. A bounded child-process test reproduces the old active-close
  lock inversion and passes with the fix; sanitizer runs also pass.
- Private-channel subprocess tests cover descriptor inheritance, process birth
  identity, all lease bindings, expiry at the point of input delivery, replay,
  queue bounds, peer death and exact-owner releases. Reentrant authorization,
  callback destruction, queued disconnects and crossed cancellation/input
  regressions pass. Retired input advances replay protection without delivery;
  stale control traffic cannot activate or revoke a newer lease.
- Keyboard-fence regressions pass for held-key releases before acknowledgment,
  pointer preservation, exact native resume, crossed cancellation, invalidation,
  reconnect and late replies. Stalled event loops and delayed native queries
  cannot bypass the monotonic deadline. Reentrant callback closure and exceptions
  cannot strand a successfully revoked fence or revoke its replacement.
- After the fence changes, all eight standalone app suites, all eleven integrated
  WindowShare suites and all five isolated core suites passed. The core suites,
  viewer, native admission, and both TLS backends of Transport/Bridge also passed
  ASan/UBSan and strict warnings. A newly exposed socket-destruction callback bug
  is fixed by disconnecting callbacks before the socket and timer members are
  destroyed; live and pending teardown regressions pass under both TLS backends.
- Actual host and viewer executables negotiated mutual TLS, exchanged a catalog,
  and disconnected cleanly over loopback with disposable identities.
- macOS reports Screen Recording and Accessibility permission granted to the local
  test executable. This is not evidence for another installed application's grants.
- A live disposable native window produced a 1280×896 ScreenCaptureKit frame,
  VideoToolbox H.264 output, a successfully decoded frame, and a focus callback.
- That live test did not pass keyboard/pointer delivery. Subsequent pointer window
  targeting and key-release fixes compile, but still need a desktop retest.
- Holding-display creation and normal removal passed while the owner host stayed
  alive. The forced owner-crash check was inconclusive: the physical display
  inventory changed and macOS reported a closed laptop lid. No owned helper or
  tagged holding display remained, but that is not a passing restoration test.
- Recovery regressions passed for malformed/oversized journals, private file
  permissions, failed durable writes and retry. Unavailable display/identity
  information preserves unresolved records.
- The default full Deskflow build passed, followed by all 30 existing CTest suites.
- The full opt-in Deskflow build passed, and all 42 suites passed across the
  main run and targeted final Bridge/private-channel reruns. Its Server
  regressions exercise real primary/client routing and orderly disconnect
  through the existing protocol implementations.
- The final separate experimental app passed nested signing and dependency
  verification for all 62 Mach-O binaries. Packaged CLI startup, mutual TLS and
  clean exit passed with external Qt/library environment paths removed. Its
  five packaging fixture tests also passed.
- The current unpackaged, packaged and installed viewer executables passed a
  disposable loopback TLS fixture: initial refusal, recovery, authenticated
  disconnect/reconnect, fresh negotiation and clean exit. This newer smoke test
  never starts NativeHost, registers a tap, exports a window or changes displays.
- All five GitHub Actions jobs passed for `3752b54f2f868331cddb3f2d65e4bb72ff105acd`
  ([run 38242032018](https://github.com/JackHars/deskflow/actions/runs/38242032018)).
  That includes preflight v2, selected-peer snapshots and the packaged trusted
  core/host handshake. The subsequent foundation work is recorded separately.

## Local package

The signed core-main bundle is
`build/packages/x11-active-handoff-v2-20261012/DeskflowSeamless-Core-Experimental.app`,
with its ZIP beside it. The dependency/signature audit and exact permitted-library
audit pass for signing Team `MM5PBJ9D4Y`: 77 library records, slices and allowed
CDHashes, sealed by the fixed executables' same-Team/CDHash constraints.
It is x86_64, requires macOS 26.0 due to the bundled dependencies, and is not
notarized. ZIP SHA-256:
`a383e4a1204e2fed41046944bdc513128cfc78550fe3fd275ce86935cddf0fe5`.
The separate installation is `~/Applications/DeskflowSeamless-Core-Experimental.app`.
The package, staged copy and installed copy passed recursive dependency/signature
verification. Both the new package's first launch and the installed copy's first
launch passed the private handshake/bootstrap-rejection suite; both also passed
viewer-only TLS/reconnect checks. Original input delivery remains disabled. The
previous emergency-restore installation is retained in
`build/packages/x11-active-handoff-v2-installed-backup-20261012/`.

The first candidate exposed the old five-second startup timeout; a measured
failed probe took 7.66 seconds including prelaunch checks. It was rolled back
before installing the corrected bundle, and is retained at
`build/packages/x11-active-handoff-startup-timeout-20261012.app` for comparison.
Its core supports the handshake-only probe and explicit host-config launch
documented in `docs/seamless-windows.md`; original input authorization remains off.

The existing standalone WindowShare installation is retained unchanged:

The signed Intel build is in
`build/packages/keyboard-fence-20261012/DeskflowWindowShare-Experimental.app`
with a ZIP beside it. This particular package requires macOS 26.0 because of its
Homebrew dependencies; it is not notarized. ZIP SHA-256:
`33b79eba93a2728596723ac3691f4af861ad4f069ef6fcf8816c939a31f43a18`.

The separate installation path is `~/Applications/DeskflowWindowShare-Experimental.app`.
The installed copy also passed dependency/signature verification and the
viewer-only TLS/reconnect smoke test without external library environment paths.
The immediately preceding experimental installation is preserved in
`build/packages/reconnect-installed-backup-20261012/DeskflowWindowShare-Experimental.app`.
Earlier TLS and native installations remain in their respective
`build/packages/tls-installed-backup-20261012/` and
`build/packages/native-installed-backup-20261012/` directories.
Launch with the explicit options documented in `docs/seamless-windows.md`; it is
an experimental command-line-configured app, not yet the final integrated GUI.

Ubuntu SSH access and isolated builds/tests are now verified as recorded above.
The active desktop is GNOME Wayland with one active monitor. Synthetic native
cross-machine input passes; physical input, two-monitor behavior and an installed
Linux release remain unproven.

## Remaining milestones

1. Complete the original mixed-input route: authenticated coordinate publication,
   ordered pointer/press/scroll ownership, live integrity checks and production
   preflight wiring. Prove one Mac window's capture, keyboard, pointer, scroll and
   return path on Ubuntu; validate the emergency chord on an unlocked Mac.
2. Prove holding-display lifecycle, multiple exported windows, focus changes,
   shortcuts and both monitor destinations. Linux builds, headless X11 and native
   private GNOME/Wayland tests pass; the physical desktop and two-monitor
   arrangement remain open.
3. Complete compositor-specific coordinate handling and cross-machine input
   fencing for the supported Linux session types.
4. Implement transactional edge-drag export and drag-back using Deskflow topology.
5. Handle associated dialogs, menus, clipboard and browser/Electron behavior.
6. Complete automatic crash recovery, scheduling and performance work.
7. Package both machines and complete destructive-failure and ordinary-use tests.

The installed `/Applications/Deskflow.app` has not been replaced. Repository and
standalone tests are not a claim that the full seamless-window goal is complete.
