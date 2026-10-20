# Seamless Windows work state

## Current collector-thread checkpoint — 2026-10-20

Runtime `ccebffffeee68aefdf07d6ce02da4f1f22ad3e51` moves diagnostic control
reads, serialization and output off the application's input thread. One
dedicated consumer uses the existing 8,192-row journal, without a second payload
queue. Existing control validation, arm/stop boundaries, the native receipt tail,
output limits and sticky loss remain. Shutdown joins the collector after
producers retire. Native terminal close is dispatched to the application thread.
This does not make a blocked filesystem call bounded or prove live reliability.

The follow-up publication guard includes native observers before they reserve
a journal slot. Drained status also requires zero capture/mutation activity.
Regressions reject a native observer crossing a collector boundary and retain
an admitted late row after a failed forced stop. The updated focused sanitizer
run passes; its exact source/binary binding is `boundary-validation.json`.

The focused Mac suite and AddressSanitizer/UndefinedBehaviorSanitizer check pass
in `build/evidence-worker-check-yvvxwegc/`. The new regression stalls the actual
collector executor, proves the application can still append input, preserves
exact rows on teardown and proves existing buffer exhaustion remains failed.
ThreadSanitizer is inconclusive: its Qt cross-thread warnings also reproduce
in a standalone Qt-only blocking-queued-call baseline using the same
uninstrumented Qt binary. The instrumented suite exceeded its unchanged
cooperative-test deadline. Both raw failures are retained; no TSan pass is claimed.
The 211-case coordinator harness passes on both machines (one Linux-only skip
on Mac) in `build/controller-harness-h_h0ffs0/`. The final candidate passes
all 22 selected Mac suites and 17 Ubuntu suites, signing, dependency and package
verification in `build/collector-boundary-9rczih8p/`. Exact hashes are at the top
of `TESTING.md`. Source-bundle run 39489639299 and all six foundation jobs in
39489639329 pass. No earlier runtime's live results qualify it.

Actual-desktop viewer, Host and Core termination recovery passes on these
packages in `dashboard-replay-3nstj0u3`, `-zlzcxol1` and `-37apm0g4`,
respectively, with source restoration observed in 613, 767 and 533 ms.
The first holding-helper attempt `dashboard-replay-5q07sd_8` stops before
launch on a private-port check; no fault was submitted. Harness `ee7d4012f`
reproduces the stale TCP TIME_WAIT rejection and matches the native Unix
listener's restart policy while still rejecting live listeners. The native
artifacts are unchanged. Its 213-case harness passes on both platforms (one
Linux-only skip on Mac) in `build/controller-harness-hk1hv008/`.
Raw failing/passing port tests are in `build/port-preflight-err5humb/`.

The subsequent holding-helper run `dashboard-replay-rejs9ffd` is cancelled
before any fault because its wrapper mishandles a changing JSON publication.
Harness `81bb308d2` now waits for a stable live snapshot through the existing
event loop/deadline and preserves the Active notification. Malformed data,
owner changes and held input still fail; no uncertain snapshot grants fault
authority or recovery. The new 216-case harness passes on both platforms
(one Linux-only skip on Mac) in `build/controller-harness-5gngwzmo/`.

The final recovery matrix on unchanged `ccebffffe` packages and harness
`81bb308d2` passes all four cases: viewer `dashboard-replay-jfd51sdy`
(1,215.755 ms), Host `-9mh1rvg3` (515.087 ms), Core `-vs5x8wz5` (256.604 ms),
holding helper `-iluitjh8` (331.964 ms). All restore the source with zero held
input, unchanged ordinary settings and exact owned cleanup on both machines.
The matrix index is `build/collector-boundary-9rczih8p/recovery-matrix-v4.json`.
The intentionally failed pipelines remain failed; their independent recovery
assessment alone passes. This submits no input and does not qualify H10
physical/sleep acceptance or a full input run.

Sustained-input attempt `dashboard-replay-0dkhswz_` reaches Active but posts
no input: its activation helper times out before its first log stage. The
coordinator later reports the missed Record cutoff and completes restoration
and cleanup with no held input or ordinary-setting change. The exact first
failure remains in the replay logs. A separate cold/warm permission probe
does not reproduce the timeout. Launcher `build/replay-ready-lihb7ho2/`
now checks the exact copied helper before opening the session, retaining the
readiness receipt and launcher hash. Native artifacts and input deadlines
are unchanged; this preparation change is not evidence of a live fix.

`dashboard-replay-k14t48ax` completes the full two-minute session and all three
30-second recordings, with 1,200 exact ordered raw/flag scroll receipts at
117.84 Hz, all five non-repeat board checks, 21 valid queue histories, zero
evidence loss, restoration and clean owned teardown on both machines.
It is **not a strict input or performance pass**: 758/1,200 scroll timestamps
change, the strict normalizer fails, and held-X repeat is not exercised by
this profile (separate AppKit repeat diagnostics remain failed). The wrapper's
`verified: true` means its non-repeat diagnostic completed, not full acceptance.
The journals remain unmodified.

Capture-to-AppKit latency is median 133.77 ms, p95 308.27 ms, max 370.51 ms.
Same-Mac stage analysis identifies capture-to-map waiting of median 66.85 ms,
max 318.14 ms. Mapping round trips also reach 122.01 ms, with up to 105.11 ms
outside Ubuntu's measured mapping work; this includes transport/scheduling.
The owned SSH socket records 4,344 retransmitted bytes. Ubuntu's Wi-Fi power
saving is still off. No native authority or terminal dispatch failure occurs,
but neither the latency target nor timestamp preservation is met. Inspect
`scroll-stage-latency.json`, raw sample/journal records and network counters
before choosing the next change; do not infer a sole cause or rerun attended
tests. Harness `81bb308d2` is pushed, source run 39493140816 passes and its
foundation run 39493140792 is pending. The native runtime's six jobs already pass.

The additional self-PID variant in `build/native-repeat-layout-y1im_k1d/`
removes the explicit Unicode override and uses the production-style empty
annotation strings. All four non-repeat AppKit events retain exact timestamps;
both repeats still fail. No repeat-field workaround was introduced.
The self-process `CGEventPostToPSN` comparison in
`build/native-repeat-psn-2tzv8no7/` produces the same four exact non-repeat
receipts and two missing repeats. Production continues to use PID posting.

No attended test was launched. Startup and sustained input reliability, strict
repeat/timestamp preservation, three qualified complete same-artifact runs,
H03–H12 acceptance and upstream queue certification remain open.

## Earlier dispatch-failure checkpoint — 2026-10-20

Runtime `835bc6d71ca530b40dcc835aa0b850a4d298d887` is pushed. The new
candidate in `build/dispatch-failure-broezp0w/` passes all 22 selected Mac suites
and 17 Ubuntu suites, signing, dependencies and package verification. Its harness
in `build/controller-harness-_wlilij7/` passes 210 cases plus one Linux-only skip
on Mac and all 211 cases on Ubuntu. Source-bundle run 39485133920 passes; hosted
foundation run 39485133853 passes all six jobs. Actual-desktop verification is separate.

A terminal Server original-input dispatch failure now emits a structured cause.
The acceptance coordinator stops an Active run on that event or the exact older
warning, preserving the first cause through cleanup. It does not confuse normal
owner/lease revocation on an intentional return with terminal dispatch failure.

Evidence serialization continuations now only drain existing rows; the ordinary
10 ms timer retains control-file reads, owner/arm checks and stop handling.
The regression proves a queued stop command is not consumed by serialization,
then proves the ordinary timer handles it while retaining the native receipt
collection tail. Separate timing logs distinguish control processing from
journal serialization/flush. All deadlines, capacities, raw fields and sticky
failure rules remain unchanged. This removes redundant I/O; it does not yet
establish a live latency improvement.

The retrospective log replay in
`dashboard-replay-yz3tc837/retrospective-terminal-result.json` proves the updated
observer identifies the original deadline failure and still permits cleanup.
This is a regression replay, not a new live pass. Additional self-PID diagnostics
in `build/repeat-cadence-oihobxja/` retain the same missing repeat with the system
repeat delay, zero flags, or a process-local disabled press-and-hold preference.
Each receives all four non-repeat events with exact timestamps and no repeat at
AppKit. No global preferences, input source policy or acceptance rule changed.

Actual-desktop run `dashboard-replay-tdvv41lm` reaches Active but stops before
the scrolling burst when ScreenCaptureKit reports -3821. Retained macOS logs
show `replayd` receiving a high-urgency cache purge, stopping its streams, then
reporting the same capture failure. The coordinator preserves capture shutdown
as the first cause; source restoration and owned-process cleanup complete.
This run establishes no sustained-input improvement. The system requested
roughly another 4 GB of cache reclamation while about 10 GiB remained free.
`build/retired-bundle-compaction-qsy66g7c/` subsequently verifies each saved
archive, file, mode and symlink before removing 12 redundant extracted older
test bundles, recovering about 1.5 GB. Current artifacts and all archives and
raw logs remain. Freeing space is not evidence that capture reliability is fixed.

Subsequent `dashboard-replay-ekq94s8r` reaches Active, arms all three recorders
and completes five non-repeat board checks. The sustained burst stops after
106 posted/observed events when native focus/frame/minimized-state queries time
out: the failed candidate check takes 163 ms. The Host refuses submission with
72 ms still on the mapped receipt, so this is an authority failure rather than
receipt expiration. Diagnostics additionally measure a 43 ms Core journal drain
and 34 ms source control read on their application threads. These establish
blocking diagnostic work but not the sole cause of the native-query timeout.
Restoration, zero held input, unchanged ordinary configuration and owned-process
cleanup complete; the incomplete recorder and strict replay remain failed.
This motivated the collector-thread change above. No same-candidate full pass
or three-run qualification was obtained.

No attended test was launched. Repeat/AppKit preservation, startup and sustained
input reliability, three qualified same-artifact input runs, H03–H12 acceptance
and full upstream queue certification remain open.

## Earlier dispatch-cost checkpoint — 2026-10-20

Runtime `787fa54a8fead9749ca892ee1b70ed808b44af52` is pushed. Both packages
are built from that source in `build/native-dispatch-cost-65pux1r_/`: all 22
selected Mac suites and 17 Ubuntu suites pass, with signing and dependency/
package checks. Harness `42cf9560c` in `build/controller-harness-hgx8jc8x/`
is independently bound: Mac passes 209 cases with one Linux-only skip; Ubuntu
passes all 210. Source-bundle run 39481585331 passes; foundation run 39481585134
passes all six jobs. Hosted success does not close the live failures below.

Private channel v10 measures the peer's actual mapped-frame handling rather
than charging process/acknowledgement waiting as work for every record. The
coordinator uses that measurement for its existing 50 ms scheduling target,
retains the 16-record maximum and logs handling and round-trip time separately.
The exact ordered frame/count acknowledgement remains required. New regressions
cover scheduling gaps, callback/authorization cost, multiple barriers, one-time
consumption, earlier periodic replies, malformed/regressing/overflowed totals
and exact prefix matching. These measurements confer no input authority or
AppKit receipt. Older private versions cannot connect; the matched Core/Host
pair is packaged together. All input, preparation, placement and capture/route
deadlines are unchanged. The independent preparation cleanup fix from
`ad9e4a447` is also included.

The committed self-PID AppKit diagnostic reproduces the repeat gap without
Deskflow or Qt. `build/native-repeat-boundary-prdvr08n/v2-result.jsonl`
records actual active/key-window readiness before all six posts. All six
events reach its listen-only process tap; four non-repeat events reach AppKit's
sendEvent handler with exact timestamps and neither repeat does. Both plain
Quartz events and window-annotated events show the missing repeat. The earlier
prototype is retained separately; its receipt count measured timestamp matches
and it lacked the readiness check.
This narrows the failure boundary on this machine; it is not a platform-wide
conclusion, physical acceptance or permission to alter repeat/timestamp checks.

`dashboard-replay-8a96rrit` failed the repeated input-disabled package admission
before opening a test window. Its original structured stdout error was lost
behind stderr shutdown messages, so the initial cause is not established.
`build/admission-detail-rf7f7sws/` retains a passing same-package admission with
both streams. Harness `42cf9560c` now saves separately labelled, bounded stdout
and stderr for both nonzero exits and timeouts. Its regression proves this
failure stops before native input startup and still completes scoped cleanup.

`dashboard-replay-yz3tc837` reaches Active and five non-repeat board checks.
Batch sizing now varies with measured work (including multi-record prefixes),
but Server terminates original dispatch at 19:31:19.864 after a completed capture
misses its original 100 ms translated-input arrival deadline. The old test
observer misses this terminal warning; legacy input continues and the failure
surfaces only when the 30-second recorder ends. The helper posts all 1,200
scrolls at 117.31 Hz, but exact delivery fails; 81 functional receipts were
matched before the first unmatched event. Changed timestamps, host owner loss
and Core pipeline revocation remain failures. Source restoration, zero held
keys/buttons and owned cleanup complete. No full session or repeat pass is claimed.
Three qualified complete input runs, H03–H12 acceptance and full native/library/
OS queue certification remain open. The user is away; no attended physical test
was launched. Ask immediately before any attended test.

## Earlier preparation-cleanup checkpoint — 2026-10-20

Runtime `ad9e4a44751d7d912205d0fe355489e1d3a5a96e` passes 22 selected Mac
suites and 17 Ubuntu suites, with signing/package checks in
`build/cleanup-witness-t9xcvore/`. A regression reproduces the independent
preparation-cleanup witness being cancelled by a forwarding-fence change.
The fix preserves that exact pending cleanup while still rejecting geometry,
focus, gate or deadline invalidation. The earlier All-input snapshot is always
retired. Backend failures now log their original GNOME error before shutdown.
`build/preparation-cleanup-race-2xrtvlwp/` retains the failing and passing
regression; the live return race is not yet independently qualified.

Its live run, `dashboard-replay-doj2ir3b`, reaches Active and five non-repeat
checks, but sustained input loses the unchanged two-second capture/route join.
The producer stops at 313 posted/observed events. All 74 mapping requests contain
one record: the old delivery budget includes process/acknowledgement waiting as
per-record native work and stays at a one-record limit during the burst.
Source restoration, zero held input and owned cleanup complete. Thirteen
functional receipt timestamps change; the incomplete recording and strict input
failures remain failed. This run never reaches the return-shortcut check.

## Earlier input latency checkpoint — 2026-10-20

Runtime `5801513591291d9cc821a78d8b175f1fbb53d00f` is built and pushed.
`build/input-latency-vf23_d58/` passes 21 selected Mac and 16 Ubuntu suites,
signing and package/dependency verification. Harness
`build/controller-harness-gau69rjd/` has 209 cases: 208 pass plus one Linux-only
skip on Mac, and 209 pass on Ubuntu. Exact artifact hashes and raw evidence are
at the top of `TESTING.md`.

Input batching targets 50 ms of native work, preserving all original receipts,
ordering and authority checks. A self-targeted native diagnostic confirms that
this Mac rewrites timestamps already 101 ms old or older at posting; 0–99 ms
samples are preserved. The disposable source avoids redundant instruction
relayout, and the coordinator now retains native capture shutdown as the first
cause instead of a later restoration error. Placement uses its original
absolute two-second transaction deadline; input/environment remain 250 ms.

`dashboard-replay-rrbde301` completes 120 seconds Active, three 30-second
recordings, 21 queue checks, restoration, zero held input and owned cleanup.
All 1,251 functional receipts and the strict recorded-journal comparison pass,
including all 1,200 scroll events at 119.32 Hz. Capture-to-AppKit observation
is median 30.54 ms, p95 50.25 ms and maximum 85.57 ms. Synthetic repeated-x
still lacks an AppKit receipt: this is a non-repeat/lifecycle success, not full
input acceptance. Earlier `dashboard-replay-50csru1o` sends no input after a
five-second focus-readiness failure; its unarmed recording remains failed.
Follow-up `dashboard-replay-76oi4qmi` receives all 1,251 functional inputs but
changes 85 timestamps (p95 capture-to-AppKit 123.07 ms, maximum 195.46 ms).
Ubuntu then refuses exact preparation cleanup on the return shortcut and
disconnects. Restoration and owned shutdown complete, but strict matching and
the full session remain failed. The repeat sequence stops at that failure.

The previous `3ee0cf757` candidate passes all six hosted jobs in run
39472776971, but its live diagnostics remain failed: one sampled native authority
failure, then an unprofiled run with AppKit scroll combining, ten changed
timestamps and a ScreenCaptureKit -3821 shutdown during a system cache purge.
Those raw results are retained. Verified local archives retain 25 older
generated bundles whose redundant extracted copies were removed, recovering
about 3.2 GiB; see `build/bundle-compaction-fr5ptjcy/`.

Three qualified complete input runs have not been achieved. Startup reliability,
repeat, sustained-load repeatability, H03–H12 acceptance and full native/library/
OS queue certification remain open. The user is away; no attended test was
launched. Obtain fresh permission immediately before any physical test.

## Earlier native delivery checkpoint — 2026-10-20

Runtime `c80320392317d3f788460801879f8b6fbca03427` is built and signed in
`build/inspection-delivery-k2m8kj9o/`. All 21 selected Mac suites and 16 Ubuntu
suites pass. The read-only environment watchdog delivers completed observations
at high priority while still rejecting the original 250 ms deadline. Placement
completion keeps normal FIFO ordering with queued cancellation. The shared
64-inspection limit now retains a slot until both the QThread object and its
queued result have retired. Regressions cover late results, destroyed owners,
queued cancellation and 64 completed results whose thread objects are already
deleted. No input authority, receipt, deadline or capacity is relaxed.

Mac signed archive SHA-256:
`0a1bb3ad0bd13d07ea2c678c376dd2cc14435da322c60f3f96ac427a7008bf0b`.
Ubuntu archive SHA-256:
`1da444392c28d83113004c954e143b4df161133c4f3192dc21f7da89f20b7c19`.
Source fixture SHA-256:
`e3ff0cc74d13007029f064161bf8174d22d874bdfdaaaa540069b0a9d79bce38`.
The full 208-check coordinator harness passes on both machines and is bound to
this source in `build/controller-harness-grrf8d5x/`. Preparation initially
refused the old audit-document hash before launching any desktop session; that
failure is retained in `prepare-z1hq38fm.log`. No comparison was waived.

Code is pushed. Source-bundle run 39468727732 passes;
[foundation run 39468727733](https://github.com/JackHars/deskflow/actions/runs/39468727733)
passes all six jobs. All six hosted jobs for the preceding `8063ae4c9` pass in
[run 39464867058](https://github.com/JackHars/deskflow/actions/runs/39464867058),
including the packaged Core/Host handshake. Earlier passes do not qualify the
new native binaries. Startup, sustained scrolling, original repeat/timestamp
and exact AppKit receipts, H03–H12 acceptance and the complete upstream queue
contract remain open. Three qualified same-artifact unattended input runs have
not been achieved. The user is away; ask immediately before attended testing.

The preceding `8063ae4c9` actual-desktop diagnostics remain failed:

- `dashboard-replay-xfjakh1j` reaches Active and completes five recorded
  non-repeat board checks in 2.99 seconds. Sustained scrolling then loses the
  two-second capture-to-route join. The coordinator names that failure about
  14 ms after its native log, instead of waiting for a generic recorder error.
  The burst stops after 582 posted/observed events. Source restoration is later
  observed, held input is zero and owned cleanup completes; the initial native
  restoration error and incomplete recorder remain failed. Its Host sample
  covers startup/early input, not the failing burst.
- `dashboard-replay-ns5kbr_u` reaches Active but the environment observation
  expires during the practice drag. No burst or burst-triggered sample occurs.
  Source restoration and owned cleanup complete with zero held input; the
  incomplete input collection is not a pass.

## Earlier snapshot build and verification — 2026-10-20

Runtime/source-fixture checkpoint `8063ae4c9835ef504c6180d2cf000283d3efbf59`
is built and packaged in `build/snapshot-publication-final-n9f5bps1/`.
All 21 selected Mac suites and 16 Ubuntu suites pass; signing, dependency and
package checks pass. Disposable UI snapshots now use private same-directory
atomic replacement without forcing every 100 ms update onto durable storage.
Recovery journals and raw evidence are unchanged. Regressions cover concurrent
complete-file reads, retained old readers, private permissions, failed rename
cleanup, missing parents and symlink replacement. The first Mac build failed
because its C++ library lacks `std::jthread`; `8063ae4c9` fixes the test to use a
joined thread, and the complete rebuilt suite passes. The failed build remains
in `build/snapshot-publication-5tb7j7hi/`.

Mac signed archive SHA-256:
`65f6b0e1e4ca632b0d85836511383b2513d2c6c9574c67ef4a53d5eb96af01f5`.
Ubuntu archive SHA-256:
`fb2f9e8f0b586b9f168886596d4265bf23a63c658476b8a382726028c544b5b6`.
Source fixture SHA-256:
`e3ff0cc74d13007029f064161bf8174d22d874bdfdaaaa540069b0a9d79bce38`.
The 208-check coordinator proof remains `controller-harness-efi8wbe4`; its
source hashes match. The new Linux snapshot regression has its own exact
source/archive binding beside the package. The first preparation refused an
old snapshot-test hash before launch; its failure is retained, and preparation
now uses the freshly passed proof instead of waiving that comparison.

All six hosted jobs on `a3a79385e` pass, including Mac app/integrated tests,
packaging and private Core/Host handshake:
[run 39460069913](https://github.com/JackHars/deskflow/actions/runs/39460069913).
Code through `8063ae4c9` is pushed. Its source-bundle run 39464867178 passes;
[run 39464867058](https://github.com/JackHars/deskflow/actions/runs/39464867058)
passes all six jobs. This result covers `8063ae4c9`, not later changes.

The roadmap remains incomplete: startup reliability, sustained scrolling,
original repeat/timestamp and exact AppKit receipts, H03–H12 acceptance, and
native/library/OS queue certification remain open. Three qualified same-artifact
unattended input runs have not been achieved. The user is away; only disposable
unattended diagnostics are authorized. Ask immediately before an attended test.

## Earlier diagnostic candidate — 2026-10-20

Runtime `b51487025a6277e4677d4b1b1a724b0ba9b052ae` is built and packaged in
`build/capture-failure-fzgavzj0/`. It records the first native capture rejection
in one immutable bounded record, with no allocation or logging in the event
tap. The consumer prints the failed stage, original timestamp/flags, prior
admitted timestamp, event types and scroll syntax. No character text is logged.
Native-event, unfinished-translation, stream-admission and downstream-join
failures are distinct; later teardown cannot overwrite the first cause.
Both native capture suites pass normally and with ASan/UBSan in
`build/capture-diagnostic-tests-wb58j7h1/`. All 21 selected Mac and 16 Ubuntu
suites, signing and package checks pass. The first capture rejection has not
yet recurred with the new diagnostic; do not claim a scrolling fix.

The preceding `cbd9d0c6e321d8bf63d69ca39826e9fcb32f6dca` candidate in
`build/native-capacity-d6q1nt7m/` passes all 21 selected Mac and 16 Ubuntu
suites, signing and package verification. It caps retained native-inspection
workers at 64 across result types, cancellation and destroyed/replaced owners.
A permit survives until both the native call and its deferred QThread deletion
finish. Exhaustion refuses new work and restores affected exports. The
regression blocks every slot, cancels/expires/destroys owners, rejects new work
and verifies recovery after all retained thread objects retire. This bounds
application workers, not allocations inside an OS call or other native queues.

`dashboard-replay-45kfgqh_` on `cbd9d0c6e` reaches Active, passes initial
functional typing/click/drag/two-axis-scroll receipts and arms all recorders.
During the recorded scroll exercise, capture continuity is revoked with reason
3. The generic report does not identify the precise rejected native event;
that is the reason for the new diagnostic. The 1,200-event burst never starts.
The source/frame restores, held keys/buttons return to zero, both machines'
owned processes stop and ordinary settings remain unchanged. The raw recorder
failure remains failed. Separate environment timing shows screen-capture
preflight accounts for nearly all polling cost (maximum 139 ms this run),
while layout/accessibility checks are small. This is not an environment-timeout
failure and not a qualified pipeline pass.

Current harness `65d49c18e` is bound to 208 passing checks on each machine in
`build/controller-harness-efi8wbe4/` (109 Mac-coordinator, 33 Linux-supervisor,
9 desktop, 46 normalizer and 11 recovery). The normalizer validates optional
failure metadata and names the original loss/site/queue while continuing to
reject every nonzero loss. `a3a79385e` replaces the concurrency tests' spin/backoff
barriers with blocking rendezvous, preserving all producer counts, rounds and
the 15-second test deadline. The evidence suite passes in 1.45 seconds on Mac
and 0.26 seconds on Ubuntu (`build/evidence-barrier-mz3ata8w/`). Hosted run
39460069913 on `a3a79385e` passes all six jobs, including all 38 Mac app suites,
integrated checks, signing/package verification and the packaged private
Core/Host handshake. The preceding Intel run passed 37/38 suites,
then timed out in concurrent preparation, so later Mac checks did not execute.
Earlier ARM capacity failures never ran a Mac step.

New b514 actual-desktop evidence:

- `dashboard-replay-kqvt3_jj` fails before Active: a 92 ms environment read's
  callback is processed after its unchanged 250 ms deadline. The holding display
  initially reports y=-1 rather than y=0. Raw restoration failure stays failed;
  subsequent owned shutdown leaves an empty journal and closes the disposable
  source. This is not an observed successful source restoration.
- Read-only samples of the exact owned Host in successful no-input Core-fault
  runs `dashboard-replay-8pblin0b` and `dashboard-replay-pxlsd0sk` observe source
  restoration in 286.506 and 259.175 ms. The latter sample includes AppKit display
  reconfiguration on the main thread. It does not prove the stack in the failed
  startup or establish input acceptance.
- `dashboard-replay-lk5tqqah` reaches Active and all five non-repeat board checks.
  The driver spends 18.8 seconds on those recorded checks, then posts 1,200
  scroll events at 119.35 Hz. This overruns the remaining recording interval:
  Core's 250 ms drain expires with one server-completed item. Loss 9 is Deadline,
  not a native capture rejection. Source/frame restoration, zero held input and
  owned cleanup complete. Raw failed evidence is unchanged; native timestamps
  still differ and strict input acceptance remains failed.

The next local diagnostic helper, `build/scroll-persistent-pr55pgd3/`, reuses one
listening event tap instead of creating a process/tap for every recorded board
action. It disables the tap while waiting, accepts only one command at a time,
rechecks source PID birth/Active/foreground before each post, and still waits
for the actual HID and target receipts. EOF, inactive-owner and oversized
command checks post no input. The recording and receipt deadlines are unchanged.
`dashboard-replay-2tgofzw8` confirms all five recorded non-repeat board checks
finish in 3.77 seconds, so the subsequent 1,200-event burst fits within the
30-second recording. The product route instead fails during sustained scrolling:
`capture.route-join` expires after its unchanged two-second history deadline.
Capture remains live; this is a routed-input backlog, not native parse rejection.
Host/native query timing and full raw traces are retained. The raw source-return
failure remains failed even though final owned shutdown empties its journal.
This is not an input or recovery pass.

Harness `65d49c18e` now terminates Active verification on a terminal capture/join
failure or native-delivery denial. It retains the first cause across later
cleanup logs and allows the stopped cleanup phase to finish normally. It does
not wait for a generic recorder error at the end of the interval. Two new
regressions and the full 208-check harness pass on Mac and Ubuntu; replaying the
old raw log separately diagnoses the exact join deadline without changing any
original result (`route-failure-reassessment.json`).

The read-only scroll-profile attempt `dashboard-replay-s4kjkmj8` fails before
posting any input: activation was accepted but the source key window did not
appear within its five-second observation deadline. No scroll sample was taken.
The deadline remains unchanged. Cleanup restores the source, leaves zero held
input and stops owned processes; the incomplete recorder remains failed.
`dashboard-replay-cil8b3_9` also posts no input: a 145 ms environment query's
result reaches the owner after the unchanged 250 ms limit during source placement.
No scroll profile is taken. Source restoration and owned cleanup complete, while
the original preparation/recorder failures remain failed.

A separate isolated logger probe (`build/diagnostic-cost-sko5qea9/`) measures
500 format/write/flush calls: median 98 us, p95 191 us, maximum 1,100 us. It does
not reproduce logging contention during a session or prove logging harmless;
it does not support attributing the native backlog to ordinary log emission.
The synthetic helper is diagnostic tooling, not an input implementation or a
physical acceptance waiver.

## Completed startup and no-input recovery work — 2026-10-20

`ec079b781` moves AXObserver creation/notification registration off the input
thread, retaining the original absolute placement deadline and fresh checks
before every write. `c319b2d88` completes AppKit initialization through Qt
before creating native sessions, private channels and watchdog timers. The
real Cocoa regression observes exactly one launch-complete notification before
session work and confirms no test window opens. All input/environment,
preparation and placement deadlines remain unchanged.

`dashboard-replay-ajx7xb0x` on ec079 failed when a 30 ms inspection result
reached the owner about 540 ms later, beyond its 250 ms deadline. A separate
successful Core-fault run, `dashboard-replay-d2e0ymza`, sampled only the exact
owned Host and found substantial AppKit launch/menu work inside its first
event loop. That motivates the launch-order repair; the successful sample does
not prove the precise stack in the failed attempt. The first profiler error
in `dashboard-replay-ijh_5a60` has explicit owned cleanup in
`profile-abort-cleanup.json` and is not a pipeline pass.

All four fresh c319 actual-desktop no-input recovery checks pass in
`build/native-startup-63_08raw/recovery-matrix-v3.json`:

| Fault | Directory under `build/` | Return observed |
| --- | --- | --- |
| Viewer killed | `dashboard-replay-xtzps_n3/recovery/` | 552.484 ms |
| Host killed | `dashboard-replay-2zt_br3s/recovery/` | 768.355 ms |
| Core killed | `dashboard-replay-d9gd5n2f/recovery/` | 291.664 ms |
| Holding helper killed | `dashboard-replay-y12h5gzh/recovery/` | 420.844 ms; Host alive |

Each observes the exact restored source/frame, empty journal, zero held input,
owned cleanup and preserved ordinary settings. Deliberately failed sessions
remain FAILED; `recoveryObserved` qualifies only the scoped fault diagnostic.
These times include observation/publication delay and do not measure display
completion or establish physical H10. They do not qualify changed binaries.

The supervisor now retains structured failures after a broken Stop pipe and
keeps expected fault shutdown separate from independent cleanup failures.
Missing receipts still fail. Original reports are unchanged; the separate
`build/async-registration-jtwk28cd/final-recovery-assessment-audit.json` records
which older reports the corrected assessor can explain. The incomplete
`dashboard-replay-1ly7nlye` remains failed.

Both subsequent c319 input attempts also remain failed: `dashboard-replay-po2emvk6`
times out activating the exact disposable source after two seconds, before any
input; `dashboard-replay-wkzyo3d_` starts input but its environment read takes
256 ms, crossing the unchanged 250 ms limit before the burst. Sources restore
and owned cleanup completes. The current replay helper retains its activation
stage output on success and timeout, with the same two-second deadline.

Three consecutive qualified same-artifact input runs remain unmet. P1–P8 code
is integrated; runtime reliability, scrolling/repeat/timestamp acceptance,
H03–H12 and the native/library/OS queue contract review remain open. The user
is away. No attended test has been launched; ask immediately before any such
test after the autonomous readiness gate is satisfied.


## Unattended recovery and current scroll backlog — 2026-10-20

Current tested runtime: `86699a430f10b60f21aa5a513a63e1e24c7adde3`,
with build/package records in `build/source-preparation-watchdog-m0_wr8ws/`.
All 20 selected Mac suites and 16 Ubuntu suites pass. The pending-placement
watchdog no longer repeats synchronous identity/associated-window queries
already owned by a bounded native observation. Preparation retains process
birth, permission, capture and deadline checks; commit and every input retain
their fresh native authority checks. This does not make every setup query
asynchronous, cache delivery authority or change any deadline.

The actual-desktop replay `dashboard-replay-nau1a1ge` completes 120 seconds
Active, all three 30-second recordings, all 21 queue journals, normal
restoration while Host is alive, zero held input and owned cleanup. All 1,200
original scroll events at 120.06 Hz reach Core and the exact target process
with unchanged raw fields and flags. However, latency is **unacceptable**:
median 984.1 ms, p95 1267.6 ms, maximum 1300.0 ms, tail 122.7 ms. Native
authority work backs up the serial delivery path; mapping is only 12.7 ms
median and 28.2 ms maximum. Server pending input peaks at 156/256, without
overflow. AppKit also combines pairs 405–406 and 657–658, leaving 1,198 exact
window receipts. This fails strict scrolling acceptance; the earlier 43 ms
median run is not evidence of current reliability. Repeat and timestamp
acceptance remain open.

Added `deploy/windowshare/pipeline-recovery.py` for explicitly authorized,
no-input disposable desktop diagnostics. It waits for real hidden placement,
checks frozen hashes and the exact owned process tree, then kills one named
test component. It never changes the failed coordinator result or grants
physical/input acceptance. Six selection/readiness/evidence regressions pass
on Mac and Ubuntu and are added to both CI jobs; no new hosted run is claimed.

Actual-machine observations using the same runtime:

| Fault | Retained recovery directory | Return observed |
| --- | --- | --- |
| Host killed | `build/dashboard-replay-kz_wr_s8/recovery/` | 770.6 ms |
| Core killed | `build/dashboard-replay-krka9lez/recovery/` | 283.1 ms |
| Holding helper killed | `build/dashboard-replay-1htr7kdp/recovery/` | 296.8 ms, Host still alive |

All three restored the exact source frame, emptied the journal, left no held
input, stopped owned processes and preserved ordinary settings. Times include
observer/publication delay. These are three different recovery diagnostics,
**not** three qualified input-pipeline passes. The initial diagnostic attempt
`dashboard-replay-cnk0xttv` stopped before launch/fault because native window
identity was not yet published; its failure is retained and the readiness
regression now covers that ordering.

The user is away. No attended test was launched or authorized by this request.
Three consecutive qualified same-artifact pipeline passes remain unmet.
H03–H12 and the incomplete native/library/OS queue review remain open. Next
work is the serial native-authority backlog, original repeat/timestamp/AppKit
receipt limitations, then the dependent hardware acceptance rows. Obtain fresh
permission immediately before a future attended test.

## Current scrolling result — 2026-10-20

The tested Mac/Ubuntu candidate is `6d1447f1c` in
`build/scroll-worker-final-ye18v6r6/`. All 20 selected Mac suites and 16 Ubuntu
suites pass. The unchanged two-machine harness proof has 107 coordinator and
42 normalizer checks passing on each machine. Mac archive SHA-256:
`91c4f611281121f067b5820ff0b62fe22492cd5b5970bfa83c16912211ecbca9`;
Ubuntu archive SHA-256:
`8ecbad3ce4fbc1faf1f62bbbd0baa378dea35118c2b9fe3ecaf7d867a829e0ac`;
source fixture SHA-256:
`9e2be3a1683e590664b728474a5fa79804e3f0004389749ab9512ad7ca428886`.

`dashboard-replay-64ie7k1n` completes normal startup, 120 seconds Active, the
three 30-second recordings, all 21 queue journals, normal restoration while
the Host remains alive, zero held input, owned cleanup and unchanged ordinary
configuration. The slow startup query is reproduced at 326 ms on the worker;
its result returns to the owner thread and setup succeeds. Other main-thread
identity queries still occur (232 and 253 ms during preparation); this is not
a claim that all native OS calls are nonblocking. No collector turn exceeds
the 25 ms diagnostic threshold in this run.

All 1,200 originals at 119.19 Hz reach the exact target process unchanged.
Same-Mac process delay is median 43.1 ms, p95 98.2 ms, maximum 128.4 ms and
50.1 ms at the tail. The observer has a complete clean footer. AppKit combines
1105–1106, so exact window delivery remains **failed** (1,199 receipts). The
normalizer failure, repeat limitation and strict timestamps are retained.
The report is `build/dashboard-replay-64ie7k1n/scroll-boundary-diagnostic.json`.
This is a successful diagnostic lifecycle with an unresolved acceptance gap,
not a complete scrolling/roadmap pass. No attended test is ready: the required
three consecutive qualified runs have not been obtained. See the current
`TESTING.md` section for the fixes, regression coverage and remaining scope.

## Scroll diagnosis — asynchronous placement observation — 2026-10-20

`0fbcc6e2c` passes 20 Mac and 16 Ubuntu suites; the coordinator and normalizer
checks also pass on both machines. `dashboard-replay-4gn4hxtf` completes all
120 seconds Active, all three 30-second recordings, the 21 queue journals,
normal restoration and owned cleanup. All 1,200 original records reach Core
and the exact target process unchanged at about 119 Hz. Same-Mac process delay
is median 51.2 ms, p95 162.5 ms and maximum 210.6 ms. One collector turn still
measures 26 ms; the slicing bound limits work per turn, not OS scheduling or
filesystem latency. AppKit combines three pairs (123–124, 199–200, 1161–1162),
so strict window receipt acceptance remains failed. No physical acceptance is
claimed.

The disposable local AppKit diagnostic in `build/scroll-appkit-baseline-ghl38vit/`
posts the same scroll fields only to its own window, with no Deskflow or network
path. Its final v4 runs exit normally: 1,200 sends at 120 Hz yield 730 AppKit
receipts; replaying the measured arrival cadence yields 659. Both preserve the
exact aggregate point and fixed-point movement. Line-delta transformations and
combined receipts still fail the exact comparison. These observations establish
an independent AppKit boundary limitation, not a waiver or a repaired journal.
The failed v1–v3 setup/termination attempts are retained separately and do not
count as successful probe runs.

The same-artifact repeat `dashboard-replay-9kz0dc43` fails before Active. A fresh
window identity read takes 336 ms and prevents delivery of the environment
watchdog response before its 250 ms deadline. The source restores and owned
processes stop. The setup observation now uses one background native read,
retaining only immutable expected identity and a retained window reference.
Completion returns to the Host thread and rechecks exact lifecycle, session,
process birth, frame, paint and permissions before continuing. Placement keeps
its existing two-second absolute verification deadline; the input/environment
250 ms limit is unchanged. Cancellation never starts another read while the
old native call remains outstanding.

The focused Mac placement suite passes with regressions for a blocked read
alongside a responsive environment/input turn, no overlap, one follow-up for
concurrent notifications, duplicate/stale callbacks, exact expiry, cancellation
and destruction. Slow-operation logs now include thread IDs for comparison with the owner
thread, without reading application lifetime from a late worker. Matching full builds and actual-desktop checks remain
pending; scrolling and the roadmap are not accepted as complete.

## Scroll diagnosis — cooperative evidence serialization — 2026-10-20

`84e602975` passes 20 Mac and 16 Ubuntu suites, but scrolling remains unaccepted.
In `dashboard-replay-7bxz7sd_`, all 1,200 original raw records reach the exact
process in order at 117.56 Hz; AppKit combines records 340 and 341. The independent
observer closes cleanly. All 21 queue journals validate and the three 30-second
recordings drain without loss. Same-Mac process latency is median 83.7 ms,
p95 223.1 ms and maximum 300.4 ms. The 120-second Active period finishes, but
ordinary restoration fails with AXErrorCannotComplete when reading position.
The retained recovery journal restores the source afterwards. This is a failed
run, not a successful normal restoration. The repeat `dashboard-replay-pyab6et6`
fails before Active: a fresh WindowServer identity read takes 410 ms and delays
an environment inspection beyond its unchanged 250 ms deadline. Recovery and
owned cleanup finish. Removing unused application metadata does not fix those
WindowServer stalls.

Raw evidence logs show collector turns as long as 55 ms. These run on the input
owner thread. The collector now serializes at most 128 rows or 2 ms per turn,
then schedules one continuation. The existing recorder capacity, row contents,
order and timestamps are unchanged; teardown still flushes before its footer.
A regression verifies that unrelated queued input runs before a 2,048-row burst
finishes, every row remains exact, and destruction safely flushes pending work.
The focused Mac runtime test passes. Full matching builds and desktop checks
remain required.

The combined diagnostic reader had silently discarded native states such as
`slow`, `denied` and `expired`. It now preserves every structurally valid state;
its regression and all 107 coordinator tests pass on Mac. Original raw logs were
always retained. Earlier claims of no slow collector turns are corrected above.
No input deadline, authority check, queue bound or acceptance rule is relaxed.
No attended test is ready and three consecutive qualified passes remain unmet.

## Scroll diagnosis — metadata on native control paths — 2026-10-20

`8a7f18d9f` passes 20 Mac and 16 Ubuntu suites. Its first desktop run,
`dashboard-replay-8i95d_m7`, completes 120 seconds Active and all three 30-second
recordings with loss=0, drained queues, restoration and owned-process cleanup.
All 1,200 raw scroll records and flags reach the exact target process in order;
the independent observer has a clean footer. AppKit combines three adjacent
pairs, leaving 1,197 window receipts. The strict receipt failure is retained.
Same-Mac HID-to-process latency is median 144.8 ms, p95 427.5 ms, maximum
467.1 ms, ending at 424.9 ms. This is **not an acceptable scrolling pass**.
The combined report initially omitted slow-operation states. The retained raw
Core log shows evidence collector turns of 39–47 ms. Network mapping reaches
102.8 ms and the SSH socket reports retransmissions. Core activity alone does
not prove the issue fixed.

The repeat `dashboard-replay-d3tiagii` stops before input starts. Its initial
holding-placement identity lookup takes about 335 ms; the background display
inspection finishes in 71 ms but its owner-thread delivery expires while that
lookup blocks. Restoration and owned cleanup succeed. Native identity lookups
still request titles and application bundle metadata on placement, watchdog,
focus and release paths although those callers use only ID/PID/birth/frame.

The next candidate defaults native identity reads to those fresh authority
fields. Only creation of parent/child exports explicitly requests metadata for
their labels/journal. Separate slow-operation logs identify WindowServer/process
reads and application metadata. No authority result is cached, no deadline is
changed, and an expired inspection remains a failure. Matching builds and
desktop verification are still required; three qualified consecutive passes
have not been obtained and no attended test is ready.

## Scroll diagnosis — Core scheduling and native receipt boundary — 2026-10-20

Scrolling is **not accepted**. `08df010f2` passes 19 Mac and 15 Ubuntu suites;
its desktop runs still vary. Moving periodic native environment inspection
off the Host thread eliminates the observed long synchronous poll, but does
not by itself resolve every dispatch stall. Failed runs `ef6ye2n0`, `xvigj6qm`
and `j7g6da1a` remain intact. `xvigj6qm` also failed restoration verification;
do not report it as clean cleanup merely because its processes stopped.

`dashboard-replay-bg6ympei` receives 1,198 exact AppKit burst records: 745 and
746 become one event whose six scroll deltas are their exact sums. Its partial
same-Mac capture-to-AppKit latency is median 56.2 ms, p95 221.9 ms and maximum
316.8 ms, with no accumulating tail. This is not an exact delivery pass.

The explicitly diagnostic continuation `dashboard-replay-xgpqbugc` observes
all 1,200 original raw fields and flags, in order, at a read-only event tap on
the target process. AppKit combines 683 and 684 downstream of that tap. The
full 30-second recording and 120-second session complete; all queues drain,
the source restores and owned processes stop. The exact AppKit mismatch stays
failed. The observer was terminated before its footer, so its completeness is
not certified. Its early data cannot replace owned-window AppKit evidence.

The repeat `dashboard-replay-j7g6da1a` exposes a separate actual failure.
Mapping returns in about 15 ms, but the 11 mapped records wait about 148 ms in
Core before the first private-channel write. The Host later rejects an expired
250 ms point. Core queue journals distinguish this delay from native queries
or Wi-Fi. Restoration and process cleanup succeed. The revised process observer
has a clean footer with no overflow, timeout or owner loss. Idle SSH probes
are about 2.3 ms median; Apple SSH already enables TCP_NODELAY, so no speculative
SSH or Wi-Fi setting changes were made. Owned probe processes/sockets are gone.

The next candidate gives the hidden Core its own scoped Foundation interactive
activity. Previously only the Host held this process-local scheduling hint.
It spans the private session and companion teardown, permits normal system
and display sleep, and changes no authority, queue or deadline. Existing token
lifetime tests are reused, with Core ownership/start-stop coverage. Slow capture
consumer and evidence-collector turns are also logged to locate any remaining
main-thread delay. App Nap is a hypothesis for the observed gap, not yet proven
as its cause; matching builds and desktop verification remain required.

`deploy/windowshare/scroll-process-observer.m` and `scroll-diagnostics.py`
preserve both observation points and explicitly report exact AppKit failure
even when adjacent sums explain it. They are diagnostics only. No acceptance
criterion or original journal has been changed. Three consecutive qualified
same-artifact desktop passes are still required before another attended test.

## Scroll diagnosis — optimized pipeline and prefix drain — 2026-10-20

`2604393d9` passes the selected 19 Mac and 15 Ubuntu suites, but yielding after
each native delivery does not fix coalescence. `dashboard-replay-e73e9whz`
combines scrolls 18 and 19, then expires a mapped receipt after a slow network
round trip. The SSH connection records 8,688 retransmitted bytes; the source
restores and owned processes stop. The extra per-record yield is removed;
the existing 8 ms dispatch quantum and ordered drain tests remain.

The next change moves periodic read-only display and permission inspection
off the input owner thread. Only one worker may run, with a 250 ms absolute
completion limit; a stalled or cancelled native call keeps its slot until it
returns. Its result can trigger recovery only for the same lifecycle, restore
generation and exported owners. Every input retains its fresh native checks.
Tests cover owner-thread responsiveness, no overlapping work, exact expiry,
cancellation/destruction, late results, exceptions and a fresh later read.
The first regression run caught a callback retained after moving its small
`std::function`; explicitly exchanging it with an empty callable makes expiry
and later worker completion invoke recovery only once. The repaired test passes.
No input deadline, event content, input queue bound or evidence rule changes.
Actual-desktop validation is still required; scrolling is not yet accepted.

With interactive tunnel classification, `dashboard-replay-p12sjp02` has no
observed SSH retransmitted bytes and stays connected through the burst.
The adaptive prefix eliminates accumulating lag in the matched records
(partial relative lag ends around -58 ms, maximum 267 ms). This is still not
an exact delivery pass: AppKit combines indexed scrolls 819 and 820 into one
native event with point delta 1639. Only 1,198 unique original records match.
There are no duplicate originals within the burst. Earlier exploratory counts
included preceding ordinary scroll checks with the same raw deltas; corrected
partial analyses restrict the interval to the burst. The real coalescence must
not be normalized away. Same-Mac capture-to-AppKit diagnostic latency for the
1,198 matched events is median 38.9 ms, p95 243.5 ms and maximum 366.3 ms; this
partial result is not acceptance of exact delivery or full replay.

The next channel change yields to the existing zero-delay event loop after
every native input callback, even when it takes less than the ordinary 8 ms
dispatch quantum. It prevents a fast mapped prefix from issuing consecutive
synchronous AX queries without giving native event handling a turn. No sleep,
input rewrite, merge, queue increase, receipt renewal or reordered barrier is
introduced. The mixed-record child fixture now requires a queued event-loop
marker between every native delivery while existing drain-order tests remain.

The adaptive-capacity build `bc6abdcb0` passes 19 Mac and 15 Ubuntu suites, but
`dashboard-replay-vj5vum94` fails before its stress burst. Core mapping request
39 exceeds 250 ms; the Mac SSH socket's retransmitted-byte counter rises from
0 to 756 during the failing ordinary-scroll sequence. Ubuntu receives request
39 late, rather than spending that time mapping it. An idle 30-packet LAN probe
has no loss and 1.9 ms average RTT; that does not rule out loss under live video.

The next harness requests SSH's interactive `IPQoS=ef` for the dedicated input
tunnel, while the separate video tunnel uses the OS default. This affects only
these disposable connections, not SSH configuration or network settings. It is
an attempt to reduce transport contention, not proof of a network fix. Runtime
artifacts remain exactly `bc6abdcb0`; fresh two-machine harness checks and
actual-desktop verification are required before any acceptance claim.

`6c48b5feb` passes 19 Mac and 15 Ubuntu suites. Its desktop run
`dashboard-replay-3v32sh4y` improves throughput but still fails exact delivery:
the source emits 1,200 events at 119.84 Hz, some native receipts are combined or
missing, and a later map response arrives after 250 ms. Observed matching
events accumulate roughly 1.7 seconds of lag; that partial analysis is not a
latency pass. The raw journals, mapping timings, restoration and cleanup remain.

The next candidate lets the existing adaptive dispatch budget use up to the
protocol's existing 16 records instead of an extra fixed ceiling of eight.
It still budgets only half of the unchanged 250 ms receipt lifetime from actual
ordered-dispatch timing, reduces immediately after slow work, and waits for the
preceding prefix's exact dispatch acknowledgement. No queue is enlarged and
no receipt, event, or evidence rule changes. This targets the remaining mapping
and scheduling gaps between small prefixes; it still requires desktop proof.

`cd6a86f6d` passes 19 Mac and 15 Ubuntu suites, but the actual-desktop run
`dashboard-replay-sf4gu98g` still fails: only 33 of 1,200 burst events arrive
before a translated-input deadline. Preview rows fall from 27,043 to 639 in
this shorter recording. Native checks remain the bottleneck: 27 slow authority
checks consume 409 ms during the first 1.2 seconds, plus 143 ms of polling.
The source restores and owned processes stop; failed raw records are retained.

The next native change removes preliminary discovery from events that already
have AX lookup references. It still freshly classifies every sibling's exact
ID, role, parent and modality, then reads source focus/frame/lifetime and a full
final WindowServer snapshot. That final snapshot must exactly equal the freshly
classified siblings plus source; new, retired, replaced, missing or unknown
windows deny input and discard the lookup references. Discovery still uses its
two-observation check. No previous native observation grants authority, and no
deadline, queue capacity, source event or evidence rule changes.


`668d515d8` still fails the sustained capture/route join in both
`dashboard-replay-12sgfc5q` and `dashboard-replay-3gr4kmuq`. The latter records
CPU counters without attaching a sampler. The five non-repeat board checks
pass, but the burst and recording fail. Core emits 67,767 journal rows, including
27,043 preview rows; the Python collector and Core consume significant CPU.
The next candidate publishes each bounded advisory preview with two actual
mutations: retire the old snapshot, then swap in the complete new snapshot.
This preserves each intermediate depth and one-sided journal counts while
removing up to sixteen partial publications. Actual capture,
dispatch, raw receipt and queue capacities stay unchanged. Native lookup hints
now retain only visible sibling references; the retained source is separately
revalidated later in each input, and any visible-ID change forces discovery.
No native check result is reused and no deadline or raw event is changed.

Review caught `de3ff8312` combining enqueue and retire counts, which the existing
recorder correctly rejects. It was built but never packaged or launched. The
replacement now keeps those mutations separate. The mixed-batch regression
observes repeated real preview replacement/clear operations with the recorder
installed, then verifies every mixed input still arrives once and in order.


`d25ce97ac` passes the selected Mac/Ubuntu suites. Its lightweight diagnostic
run `dashboard-replay-sp0x80hq` stops before the long burst: the second ordinary
scroll map expires, so it is not a throughput pass. Native event construction
and posting are tens of microseconds; native checks dominate successful input
processing. The raw failure and both-machine logs are retained.

The next candidate retains bounded opaque AX window references as lookup hints.
It re-enumerates when the fresh visible native ID set changes. Every event still
reads each referenced window's current native ID, role, parent and modality,
plus source focus, geometry, lifetime and permissions, then requires complete
fresh final WindowServer attribution. Missing/new/stale references cannot grant
input: incomplete final attribution denies submission and discards the hints.
No native authority result is reused between events. Existing inventory tests
cover unknown/disappearing/replaced windows and changed/unknown modality. The
candidate still needs matching builds and actual desktop verification.


The fresh-ID candidate `c71f653b9` also fails sustained scrolling: its
`dashboard-replay-pwllvqc6` run emits all 1,200 indexed events at 119.93 Hz but
receives only 362 burst events before the unchanged capture/route join deadline.
All five non-repeat board checks pass; restoration and owned cleanup complete.
The 19 Mac and 15 Ubuntu suites pass, but this is not a scrolling pass.
A separate sampler run `dashboard-replay-9jv1pgzv` perturbs the Host enough to
expire a receipt; it is diagnostic only. A lighter per-operation diagnostic now
separates event construction, native validation, submission and completion, and
splits the fresh inventory's window-ID and classification costs. No deadline,
event content, native check, queue capacity or evidence rule changes.


The prefix/counter candidate `c50025636` passes all 19 Mac and 15 Ubuntu suites,
plus 106 coordinator and 42 normalizer checks on each machine. Its
`dashboard-replay-2xgz_oo1` desktop run still fails the capture/route join after
273 burst receipts (324 functional receipts overall), despite timely mapping
(median 11.1 ms, maximum 25.7 ms). The source emits all 1,200 events at 120.10 Hz.
The barrier now occurs once per prefix as intended. Fresh Mac native queries
still exceed available throughput, especially the preliminary full WindowServer
inventory and AX window-list reads. The source restores, owned processes stop,
and the failed raw journals remain untouched. No attended launch is ready.

The next native inspection uses the public on-screen ID list for its preliminary
observation, then attributes every source-process window using the final full
WindowServer snapshot after fresh AX classification/focus/frame checks. It
requires the entire visible native ID set to remain unchanged, so an unknown
surface cannot disappear between observations unnoticed. Any missing, new,
duplicate, unclassified or changed window still fails; no authority is cached
between events. The ordinary non-input inventory path retains full snapshots.


The same `4cec4a143` source was rebuilt as RelWithDebInfo on both machines;
the prior desktop packages were Debug builds. All 19 selected Mac and 15 Ubuntu
suites pass. `build/scroll-optimized-t4q408gy/` binds the optimized artifacts.
Its full actual-desktop run `dashboard-replay-xliahnen` delivers all 1,200
indexed burst events at 120.11 Hz, all 1,251 functional receipts, the complete
30-second recording and 120-second session. All 21 queue histories validate,
the source restores, owned processes stop and held controls end at zero.
However, relative receipt lag accumulates to about 1.43 seconds. This is a
qualified delivery pass, not an adequate scrolling fix or physical acceptance.

Its 151 mapped prefixes trigger about 300 ordered drain exchanges: Core starts
a barrier after the first record, then needs another for the remaining writes.
The next change requests the eager barrier only after the full mapped prefix;
the next mapping still waits for that final ordered Pong. It also removes an
unused preliminary AX focus query in provisional inventory classification.
The later fresh focus/ancestry proof and final native inventory are unchanged.

A separate profiling run, `dashboard-replay-tf1zljk6`, delivers every burst event
but exposes diagnostic Capture-counter contention between the legitimate SPSC
producer and consumer. Capture, Translation and Completed now use the existing
bounded atomic cumulative accounting/journal ordering mechanism also used by
Core Event queues. Exact intermediate counts and original observation times
remain; real gaps, overflow, saturation and boundary races still fail. Total
pending diagnostic storage does not increase. New concurrency/publication tests
cover all three queues. One coordinator regression needed its event-pump order
corrected so it still tests arrival before the final queued writes are sent;
the initial failed test log is retained.

These changes require fresh matching packages, both-machine checks and three
consecutive full automated desktop passes with acceptable measured scroll lag.
No deadline or production queue capacity changed; no source records were
coalesced or rewritten. The synthetic repeat receipt and strict raw timestamp
acceptance remain separate unresolved limitations. No attended test launched.


## Scroll diagnosis in progress — 2026-10-19

The transport-timing candidate `8bea14176` passes 19 selected Mac suites and
15 selected Ubuntu suites, and its GitHub foundation/source-bundle jobs pass.
Ubuntu's first application run hit the existing nonblocking viewport-admission
lock race; the unchanged binaries passed the complete application recheck.
Both results remain in `build/scroll-map-timing-rz57gcs6/`.

Two actual-desktop replays still fail. `dashboard-replay-3xj_fpoh` has timely
mapping replies (median 12.3 ms) but a native check spike exhausts a mapped
receipt. `dashboard-replay-2nu_qd8a` uses a lighter incremental receipt observer
and instead reaches the capture/route join cutoff: mapping waits intermittently
include 80–100 ms outside the Ubuntu mapper. These are same-host monotonic
measurements; they do not establish one-way network latency. All raw failures
are retained. The receipt-observer cache validates file identity/replacement,
retains every raw receipt and rejects a changed Host identity.

The next diagnostic records socket-buffer waits of at least 5 ms, independently
on both sides, to distinguish application scheduling from transport delay.
It changes no socket scheduling, input deadlines, authority or queue bounds.
Scrolling remains unresolved; no attended test has launched.

`3ca1c32e7` passes the 19 Mac and 15 Ubuntu suites. Its actual-desktop run
`dashboard-replay-suo0rd44` again exhausts the capture/route join during sustained
scrolling. Mapping RTT is timely (median 13.3 ms, maximum 27.8 ms); socket-buffer
delays occur outside the failing burst. Fresh Mac native checks instead spike
to 28–67 ms, and the backlog reaches the unchanged cutoff. Owned windows and
processes restore/stop; the failed raw record remains intact.

The next change combines the final native surface inventory and exact source
identity into one fresh WindowServer snapshot, taken after the other per-input
AX/native checks. Initial classification remains provisional until that final
snapshot agrees. No observation survives the input call; missing sources,
changed/ambiguous inventories, source replacement, focus/geometry changes and
expired receipts still deny submission. The fallback full child inspection is
unchanged. This removes one WindowServer query per eligible event without
caching authority or changing queue/deadline policy. It needs actual-desktop
validation before a scrolling fix can be claimed.

`a42f4678a` passes all selected suites and the complete 360-event actual-desktop
run `dashboard-replay-fqur__tu` (411 functional receipts, full recording/session,
21 valid queue histories, restoration, owned cleanup and zero held controls).
The longer `dashboard-replay-ucdt8z2k` still fails the capture/route join after
195 of 1,200 burst events arrive; preserve its incomplete recording/restore
result. Its source emitted all 1,200 records at 119.97 Hz. A single short pass
does not satisfy the launch gate.

The next refinement performs the existing strict focused-source ancestry proof
once, after the fresh window-state read and before the final WindowServer
snapshot. The initial inventory classification remains provisional. Root inputs
cannot use the weaker focused-window fallback here; missing or changed focus
denies input. Associated-window and non-input inspection retain their existing
paths. This removes duplicate AX traversal within each input, not per-event
validation, and still needs sustained actual-desktop verification.

`51b7644e7` passes the selected suites, but `dashboard-replay-vpk2if48` still
fails its longer burst: the final 15-record mapped prefix takes 259 ms, including
a 40 ms periodic native poll, and the existing 250 ms receipt correctly expires.
The source emitted 1,200 records at 120.32 Hz; only 232 burst receipts arrived.
Network mapping is timely in this failure. The failed journals remain intact.

The next candidate limits each mapped delivery prefix to eight records (the
protocol and queue capacity remain 16; all later records stay ordered). It also
uses a scoped macOS interactive QoS override for each native input operation,
ending that exact override on every return/exception. This scheduling hint is
neither authority nor a deadline extension. Tests cover nested operations,
failure cleanup, refused acquisition, native acquisition and full mixed-event
delivery through changing bounded prefixes.

The adaptive-prefix candidate `e28deb425` passes all 19 selected Mac and 15
Ubuntu suites, but `dashboard-replay-_kqo3i_b` fails at a mapped receipt deadline.
The preceding 11-record group took 192 ms and correctly reduced the next group
to seven. That next group then waited about 183 ms before native dispatch,
leaving too little of its unchanged 250 ms receipt lifetime. Its five non-repeat
board checks passed; the sustained burst and full session did not.

The physical coordinator previously multiplexed its input and video forwards
over one SSH/TCP connection. It now launches an independently owned connection
for each, with SSH master reuse disabled, while preserving loopback-only ports,
private remote Unix sockets, pinned TLS, liveness checks and exact cleanup.
This isolates a possible source of head-of-line delay; it is not yet evidence
that scrolling is fixed. The next runs reuse the exact `e28deb425` application
artifacts and validate the changed coordinator separately on both machines.

`b72edf6ea` adds timing for each fresh native inventory stage. Its replay
`dashboard-replay-u7sc1pkd` stops before the burst with ScreenCaptureKit -3821,
so it provides timings only, not scrolling acceptance. A read-only alternative
WindowServer-owner lookup disagreed with the public inventory and was rejected;
no private owner lookup was added to the runtime.

`a7894f921` maps at most four records per delivery prefix instead of mapping
all 16 available preview records at once. The existing ordered peer-dispatch
barrier precedes each new prefix. All records, source/native deadlines,
ownership checks and queue bounds remain unchanged. The mixed-input regression
checks full previews split into ordered prefixes without omission/duplication.
All 19 selected Mac suites and the six integrated plus nine application Ubuntu
suites pass. Matching packages are in `build/scroll-prefix-xdkorvaf/`.

`dashboard-replay-d_0b7og3` also stops before its burst with -3821. The saved
`system-capture-detail.log` establishes that replayd received an urgent volume
cache purge and called `stopAllStreamsWithError`; it is not a scrolling pass.
The source's native restore readback succeeds and owned processes stop, but
preserve the collector's incomplete restore result. Superseded generated Mac
38 packages were archived with per-file/metadata verification to Ubuntu in
`build/scroll-storage-zwy8d7wh/`; current packages, reusable base, source and all
logs remain local. The archive SHA-256 is
`3f500ff370d8d56f7d39d6250ec8d476806c782fb8990ca4c613e909c623e391`.
Removing the verified inactive Mac copies recovered about 7.2 GiB and raised
free space to 16.1 GiB. The interrupted run left no current HID keys/buttons
held, checked independently without rewriting its failed fixture snapshot.
Automated exact-artifact checks have resumed.
No new attended test has launched.

After storage recovery, `dashboard-replay-515ku4e3` reaches the rapid burst
without a capture interruption but still expires a mapped receipt by 9.746 ms.
A periodic native watchdog occupies 111 ms immediately before the four-record
prefix; fresh delivery checks then use the rest of its unchanged 250 ms budget.
The next candidate keeps permission, topology, capture and preparation polling
on the existing cadence, but avoids repeating native root-window checks when
that exact live input owner has just passed them. A separate precise idle timer
schedules reinspection 500 ms after the last successful delivery. Every input
still performs its full fresh native check. The schedule cannot authorize input,
cover a changed binding/epoch, survive revocation, or postpone the idle cutoff
because of an old queued timeout. Native placement regressions pass locally;
this candidate still requires full build/package and actual-desktop validation.

The watchdog candidate `e769ff74e` passes the 19 Mac suites and six integrated
plus nine application Ubuntu suites. `dashboard-replay-_2dkqfme` completes the
120-event burst (122.18 Hz at the synthetic source), all 171 functional receipts,
30-second recording and full 120-second session with clean restore/cleanup.
It is one qualified automated pass, not physical/raw-timestamp acceptance.
AppKit receipt times show increasing burst lag (up to about 1.1 seconds), and
`dashboard-replay-k7r7hj7j` fails the next repetition at the capture/route join
deadline with 155 receipts. Both leave zero held controls and restore/stop their
owned windows/processes. Do not treat the single pass as attended readiness.

The next fixture repair moves its two WindowServer diagnostic queries per
25 ms snapshot off AppKit onto the existing bounded snapshot worker. QWidget
and NSWindow reads stay on their owning thread; the worker receives immutable
values and still verifies exact WindowServer IDs/PIDs, matching geometry and
visibility. Original return frames latch only after agreeing observations.
Final flush waits for observation before destroying the owned windows. Native
evidence remains synchronous and lossless. Deterministic missing/moving/hidden
snapshot cases, the blocked snapshot writer and visible board tests pass locally.
The changed fixture requires a new hash binding and fresh three-run validation.

The background-snapshot fixture (`ba821fad2`) passes the 19 selected Mac and
15 selected Ubuntu suites. `dashboard-replay-8mjuqy08` and
`dashboard-replay-_ys511me` each complete the full session, all 120 burst
receipts and clean cleanup, but `dashboard-replay-19861wfb` fails before input
on an unavailable AX source frame after activation. The 360-event, 120.92 Hz
stress run `dashboard-replay-lc3o_g17` reproduces the route-join deadline with
145 total receipts. This remains incomplete; no attended test has launched.

The next fixture change limits diagnostic refresh to 10 Hz and queries its two
owned WindowServer IDs in one request. Repeated unchanged control metadata no
longer schedules additional writes. The worker still observes fresh exact
ID/PID/frame/visibility data; raw input evidence, per-input native authority,
queue limits and all deadlines remain unchanged. This targets test-induced
WindowServer and storage contention, not permission to accept stale input.

`4fa8f8e20` passes the 19 Mac and 15 Ubuntu suites. Its 360-event replay
`dashboard-replay-l65t8biv` completes all 411 functional receipts, the full
recording/session and clean restoration/cleanup. However, observed receipt lag
reaches 1.89 seconds. The burst spends 2.66 seconds dispatching 90 small groups
and another 2.17 seconds between them; this is not yet an adequate scroll fix.

The next coordinator change requests the ordered dispatch barrier immediately
after output and sizes each later prefix from the same owner's measured service
cost. It starts at four, stays within the existing 16-record protocol limit,
and budgets half the unchanged 250 ms receipt lifetime for measured work.
Slow samples reduce the size immediately; faster samples recover it gradually.
The timing is scheduling advice only. Every input still needs its unchanged
receipt, exact owner, ordered preceding dispatch and fresh native validation.
A new owner starts without an inherited estimate. Invalid timing samples cannot
increase the limit. Mixed-input tests continue to verify exact source order and
no omitted or duplicated records across changing prefixes.

## Dashboard launch checks — 2026-10-19

The `331dd44bc` candidate passes 19 Mac suites and the Linux inventory cases.
Its actual-desktop inventory shortcut engages, but neither new replay passes:
`dashboard-replay-1w2807iz` stops on an AX frame read failure during source
activation; `dashboard-replay-yrzy811b` completes the five non-repeat board
checks then expires a mapped receipt during the 122.596 Hz scroll burst.
Both restore the source, stop owned processes and leave zero held controls.

The next repair waits for ordered native dispatch of the preceding mapped
batch before requesting fresh coordinates for another batch. A successful
socket write alone does not establish dispatch. Existing ordered Ping/Pong
frames provide this scheduling barrier; periodic replies cannot acknowledge a
later prefix. The barrier grants no input authority and renews no receipt.
Batch size, queue bounds, capture deadlines and the 250 ms native deadline
remain unchanged. Channel subprocess tests cover nested native event loops,
later writes and closed channels; the coordinator's mixed-batch suite also
passes. This shared runtime change requires fresh packages and checks on both
machines, followed by three matching actual-desktop runs before physical launch.

The dispatch-barrier candidate `1fe391f2f` is built and packaged on both machines
in `build/mapped-output-drain-3ptch20t/`. All 19 selected Mac suites and the six
integrated plus nine application Linux suites pass. Mac package SHA-256 is
`d4400993d7afc14ff8778ee322c732038b69cd423f05acf8cf56803b28da3ede`;
Linux package SHA-256 is
`3094ab94e1574ffba105dccec9c60e15f4e0e03e255d12e89aa36e6a5c28a8ea`.
The fixture remains `97aac763cdd7194f5d21cebc670d88e4a6465d2c8f1c3e164c6151a2b0edd7a7`.

Neither replay passes. `dashboard-replay-m7i56iub` stops on an AX frame timeout
during automatic source activation. The replay driver now requests activation
only if the fixture does not already report its own active key window; this
does not remove the need for a fresh focus observation. In
`dashboard-replay-n7iv615u`, all five non-repeat board checks register during
Recording, but a 120-event burst at 122.292 Hz still expires a mapped receipt
by 27.844 ms. The ordered drain works; the native work inside a single batch
can itself exceed 250 ms. The next investigation is per-event native inventory
cost and batch scheduling, including contention with the watchdog. Do not
extend deadlines or infer success from the five widget checkmarks.

Both runs stop their owned local processes and leave zero held controls.
The latter run briefly logs a restoration read failure, then the final fixture
observation confirms its original frame and a clean source exit. Its raw
journals and failed evidence drain remain preserved. No attended test launched,
and no complete automatic run has passed with this board. The current helpers
point to these matching Mac/Linux packages; fresh physical approval and three
consecutive complete automatic passes are still required. GitHub's foundation
run for `331dd44bc` passed; the `1fe391f2f` run was still in progress when checked.

The requested new attended launch first ran the updated fixture through the
actual Mac/jack desktop pipeline. Both `dashboard-replay-mn4nnoiv` and
`dashboard-replay-waqp9_d3` observed all five non-repeat board checks during
Recording: `aBc`, the click counter, a successful puck drop, and both scroll
directions on each axis. Neither is a passing pipeline or physical test.
Rapid scrolling reproduced mapped-receipt expiry while native AX authority
queries were running. The second producer observed 120 source events at
122.84 Hz, but native delivery failed before all receipts arrived. Preserve
both runs, their raw journals and the failed evidence drains. Each restored
the source and stopped local processes; final held controls were zero.

`build/dashboard-validation-h5m3nl7s/` binds the unchanged coordinator and
normalizer to fresh passing checks on both computers. The native query repair
batches fields from the same AX object in one fresh request, including role,
subrole, children and parent during the attached-surface scan. No observation
is cached across inputs. Exact window identity, fresh focus rechecks, attached
surface inspection, receipt deadlines and queue bounds remain in place.
`NativePlacementTests` covers partial errors, missing fields, changed results,
truncated/failed replies and ownership of retained values. Build/package and
pipeline validation for this repair belongs in `build/native-query-batch-0d6scheg/`.
The attended test has not launched. Refresh matching package/fixture proofs
and complete three consecutive pipeline runs before asking to launch it.

The batched-query runtime (`f39544e2d`) passes five integrated and nine app
suites, but `dashboard-replay-8jhy5_cd` still expires a mapped event during the
burst; it is not a throughput fix by itself. The next fixture change moves
atomic diagnostic snapshot writes off the AppKit thread. A bounded worker
retains only the latest unpublished snapshot and drains it on shutdown. This
does not coalesce, defer or change the independent input evidence journals.
Write failures are logged and exposed in snapshot metadata. Deterministic
blocked-writer, shutdown and failure tests pass on Mac and Ubuntu; the five
Mac fixture suites pass. Actual-desktop validation is still required.

`dashboard-replay-i5st7oq8` shows snapshot offloading alone also does not resolve
the expiry. The next runtime change keeps fresh native admission at Commit and
at final native posting, but uses an exact local pending-event fence for the
active private-channel preparation check. It checks the established owner,
capture epoch/failure latch, legacy gate/ledger, geometry generation, focus
request/revision, family fence and retained target. This cannot authorize
posting: `deliverOriginalInput` still makes the complete fresh native query
after constructing the event and checks the unchanged mapped deadline before
submission. This removes a redundant AX/WindowServer round trip from every
active event. It requires a newly built package and fresh pipeline evidence.

The corresponding `dashboard-replay-xtsa443o` and `dashboard-replay-j8_qwhoj`
runs still fail the rapid burst; neither is an attended-launch gate pass.
The next candidate reconciles two fresh WindowServer inventories with the
application's current AX window list. It skips the control-tree search only
when the focused element belongs to the source and every other visible native
window is freshly proven to be a standard window parented directly by that
application. Unknown IDs, attached roles, missing attributes, duplicates,
changed inventories or changed focus retain the full search. No observation
is cached across input events. Fresh source focus/identity/geometry checks and
mapped deadlines still precede posting. The decision has deterministic
regressions for missing/new/changed/duplicate windows and stale classifications.

`dashboard-replay-ehnrt637` still failed the burst with `6056f6af3`; the inventory
shortcut never engaged. An isolated owned Qt fixture inspection in
`build/owned-ax-inspection-u_19irgc/` established that AXTextField returns an
exact native window ID and AXParent, but AXTopLevelUIElement is unavailable.
The next candidate proves ownership through the bounded fresh AX parent chain,
rejecting another native surface, missing roles, other PIDs and incomplete
ancestry. It does not substitute AXFocusedWindow. The complete native inventory
and final focus recheck still apply. Diagnostic changes now log changed reasons
even when the full-search decision remains false. The previous inventory
candidate passes 19 Mac suites and the Linux inventory cases; its failed
desktop run is not a launch gate. The new candidate needs fresh validation.

`dashboard-replay-z_6c0tmv` reached all five recording checks but still failed;
the added diagnostic identified an unavailable initial native inventory.
`build/owned-ax-inspection-qg7cup1c/` shows Qt's dormant backing windows omit
the optional `kCGWindowIsOnscreen` key in the all-window list. The next query
uses WindowServer's on-screen-only list directly. The source must still be in
both identical inventories, and every other ID must be freshly classified in
AX. Missing source, newly visible children and changed focus retain the full
scan. This run's failed drain and raw evidence remain unmodified.

The visible-only candidate (`3d6c230ea`) passes the 19 Mac suites but
`dashboard-replay-gfe1wgwl` still fails the burst. Active inspections in
`dashboard-replay-67buzez3/owned-ax-active.txt` and
`dashboard-replay-ilhzbeuo/owned-ax-active.txt` identify an additional AXDialog
directly parented by the application's root, with AXModal=false. Both source
and controls are also direct application children with AXModal=false. The next
classification accepts only known standard/dialog window roles, an exact
application parent and an explicit fresh false modality value. Missing or true
modality, attached parents and unknown roles retain the full scan. Deterministic
inventory regressions cover each rejection. This avoids treating an unrelated
nonmodal sibling as a reason to inspect every source control. These diagnostic
runs failed and remain evidence only, not physical acceptance or launch gates.

## Visible physical-input board — 2026-10-19

The attended `physical-r34-rgza46pp` run on the unchanged runtime completed its
30-second recording and stopped through the visible Stop control. The source
returned, all owned processes stopped, held controls were zero, and ordinary
settings were preserved. All 21 queue histories validate, but the strict join
rejects the recording: both source and native input rows are empty. The user
reported repeating the actions, then clarified that a textbox showing `aBc`
did not let them judge repeat, dragging or scrolling. Preserve that feedback
and the raw logs; do not call this physical acceptance or assume its cause.
Review: `build/physical-r34-rgza46pp/receipt-review.json` and `human-feedback.json`.

The new disposable fixture replaces that textbox-only flow with a visible test
board: actual X-repeat count plus hold/release meter, click count, a puck/drop
target, and a moving grid with independent two-direction scroll indicators.
Practice and Recording have distinct banners. Recording resets all six checks,
shows its countdown and retains incomplete results. Scoring uses the recorder's
actual cutoff, not only the display timer. Phase-scoped progress is saved in
`source.json` under `inputChecklist` and printed when checkmarks change. These
widget observations cannot establish transport/native-field acceptance.

Validation: `build/input-dashboard-0lr7o8hq/`. The Mac source fixture builds;
`mac-ui-boundary-result.json` records four passing suites (input board, manual
status, report, input evidence). `linux-ui-boundary-result.json` records the
same board regressions passing offscreen in the existing pinned build container
on jack. Tests cover real widget events, missing repeats/releases, failed drops,
individual scroll axes, practice reset, empty recordings and post-cutoff input.
Rendered previews are in `previews-boundary/`. No physical test was launched
for this UI change, and no shared-input deadline or raw journal was changed.

The native Mac/Ubuntu runtime packages remain at `fd2db144c` / `52bddb84b`.
The source fixture changed: the old three-run artifact proof cannot authorize
this new fixture. Before the next attended run, refresh the source binding,
freeze this fixture and repeat the actual-desktop pipeline gate with matching
artifacts. Obtain fresh user permission immediately before physical launch.
H03–H12 acceptance remains open.

## Rapid scrolling repair — 2026-10-16

Current Mac runtime and source fixture: `fd2db144c3576cf9bcd8df8be9efdf4f48b109bb`.
Ubuntu binaries remain at `52bddb84b`; all changes since that runtime are Mac-only.
The coordinator/normalizer files still match their 104/42 checks on both machines
at `c3b545d78`. No new attended test is authorized.

The slower runs below did not establish trackpad burst reliability. Fresh
actual-desktop tests reproduced input expiry under rapid scrolling. The fixes:

- Screen Recording preflight measured about 19 ms per call and ran twice per
  input. It now remains at export and in the existing 500 ms watchdog. Capture
  failure is checked immediately via its atomic latch before and after native
  reads. This changes permission polling cadence; it does not promise immediate
  TCC revocation detection. Accessibility and native identity checks remain.
- Preflight and final delivery both searched the full AX child tree. New
  attached surfaces are now inspected once, immediately before native posting.
  Preflight still checks native focus/geometry and known family fences; initial
  activation still performs the complete check. Legacy dialog handling also
  defers expensive joins until a real Sheet/Drawer candidate exists.
- The disposable test window rewrote and committed its complete status file
  inside every Qt input callback, including propagated wheel callbacks. The
  existing 25 ms timer now publishes snapshots; final shutdown still flushes.
  Native evidence records every event synchronously with unchanged timestamps.

Receipt deadlines, ownership checks, queue bounds and the strict evidence
normalizer are unchanged. Slow native queries retain stage timing diagnostics.
The rebuilt signed Mac package passes five integrated and nine application
suites, including native recovery, original input and sharing activity. The
unchanged Ubuntu package passed six integrated and seven application suites.
Validation and package hashes: `build/snapshot-batch-wb5x86vr/`.

Retain the failed/intermediate evidence:

| Run | Scope/result |
| --- | --- |
| `recording-replay-abuz9zqt` | At `52bddb84b`, 16 fast scroll events caused mapped input expiry. |
| `recording-replay-3u2p4axg` | Screen-permission polling repair alone still failed the short burst. |
| `recording-replay-vzcxe85u` | Diagnostic build passed 16 events and a full recording cycle only. |
| `recording-replay-328pt6h1` | Profiling run failed; sampler timing is unbounded and its report was not retained. Not throughput acceptance. |
| `recording-replay-a02u2wwv` | 120-event run failed before native submission. |
| `recording-replay-m_ip61dj` | Removing duplicate child searches alone still expired input while the fixture wrote status inside input callbacks. |

Before the network adjustment, `recording-replay-esly50p8` passed all 120 fast
scroll receipts, 158 non-repeat receipts, the full recording/lifecycle checks,
all 21 queue histories and clean restoration. Its next run,
`recording-replay-159hs5ts`, failed before the burst while awaiting an Ubuntu
mapping reply; the existing deadline was not extended. Do not call that pair
stable or transfer the first run's pass to the second.

Ubuntu's active Wi-Fi connection had power saving enabled. A 30-packet Mac-to-
Ubuntu measurement averaged 42.727 ms with a 141.055 ms peak. Independent tests
from each computer to the router stayed below 3 ms. Disabling Ubuntu Wi-Fi power
saving reduced the same 30-packet measurement to 1.880 ms average / 2.504 ms peak,
with zero packet loss in both samples. The live device was changed without a
reconnect, and the existing NetworkManager profile was set to disable power
saving on future connections. This is a machine configuration change, not an
automatic policy in Deskflow. Exact prior/new settings and rollback commands
are retained in `build/wifi-awake-g4tf1rn7/network-setting.json`.

The first run after that change, `recording-replay-tv8md96r`, still expired
a mapped event during the 120-event burst, with 79 non-repeat receipts observed.
The repetition loop stopped on that failure. Network latency was not the only
bottleneck; preserve `build/wifi-awake-g4tf1rn7/` as failed evidence.

Commit `4793a4402` additionally skips unused subrole queries for AX objects that
are not windows and skips title/bundle metadata during per-event identity checks.
It retains the full attached-surface scan and exact WindowServer ID/PID/birth/
bounds observations. Its run `recording-replay-paii593c` passed all 120 scroll
receipts, the full 30-second recording interval and all 21 queue histories, but
then failed a native focus query while starting the return shortcut. The
50 ms focus query timeout remains retained as a failed run, not a full pass.
`fd2db144c` adds exact focus/geometry/minimized/holding-display rejection details
without changing authority. Validation/package hashes are in
`build/return-trace-35bi7wkq/`. All three fresh cycles passed the functional
non-repeat and complete lifecycle checks with identical native binaries, fixture
and coordinator. Each completed 120 seconds Active, all three 30-second
recordings, 120 scroll events at over 120 Hz, 158 checked non-repeat receipts,
21 valid queue histories, zero evidence loss, source restoration and stopped
processes. Ordinary Mac configuration was unchanged; no keys/buttons remained
held. Roots:

- `recording-replay-1ufbt9r3` — 120.309 Hz; strict recorded raw/timestamp join failed.
- `recording-replay-ny73n6oh` — 121.694 Hz; strict recorded interval join passed.
- `recording-replay-l4ja6xui` — 121.911 Hz; strict recorded interval join passed.

This is bounded stability evidence, not proof that the earlier intermittent
focus timeout is impossible. All three full synthetic replays still report
the known missing repeated-x AppKit receipt and timestamp changes outside
and/or within the recorded interval. Those checks are not waived. No physical
acceptance has been obtained.

Current Mac archive SHA-256:
`e51da724ded4c52aca77f44d4fb1aa4feb37547bc05359ba0e5520aafc4b4a95`.
Unchanged Ubuntu archive SHA-256:
`b912992380c7cca9ead85e3abe9d2456125ca9bc915a42e9d806ca732445a435`.
The current Mac passes five integrated and nine app suites. All six jobs in
CI foundation run `38999308404` passed; source-bundle run `38999308471` passed.
Fresh attended preparation/launch helpers are `build/prepare-physical-r34.py`
and `build/run-physical-r34.py`. The launch guard requires these exact three
artifact sets plus a new explicit user approval.

Attended attempt `build/physical-r33-gqm9_8ei/` opened its controls after the
October 17 approval, then timed out waiting for Start while the user was away.
It never began sharing. Its processes stopped with no held input and ordinary
Mac configuration unchanged. That approval is consumed and must not be reused.

On October 19, setup `build/physical-r34-rgza46pp/` caught a private-handshake
startup timeout before opening any test window. Preserve `code-admission.log`.
The two unchanged input-disabled retries passed: the second, uninstrumented
run completed in 12.828 seconds (`code-admission-uninstrumented.log`). A stack
sample from the first retry has no usable call graph; it does not establish
the cause. No timeout or trust check was relaxed. Final preflight confirms
jack's actual desktop is unlocked and no conflicting Mac primary is running.
The user subsequently approved and ran this attempt. It stopped cleanly, but
its empty input recording remains incomplete; see the October 19 board update
above. Its approval has been consumed. H03–H12 physical acceptance remains open.

## Private input dispatch repair — 2026-10-16

Previous native runtime: `52bddb84ba6dcfac91e44ad146cf1d7c0b4dae0d`.
Coordinator and normalizer files still match the 104/42 two-machine checks
bound to `c3b545d78` in `build/event-validation-utnbg_qt/`.

The previous candidate passed the full recording lifecycle in
`build/recording-replay-8i529fan/`, including all 21 queue histories. Its second
run, `build/recording-replay-rmirupyh/`, failed during initial input handoff:
the mapped-point deadline expired in Host after native authority checks. The
source then returned, causing the coordinator's secondary hidden-placement
failure. No recording began. Keep this failure and its non-neutral fixture
snapshot; do not claim three successful runs at that revision.

A deterministic regression reproduces private input bytes remaining in Qt's
output buffer until a later event-loop turn. `LocalInputChannel::send` now
performs one nonblocking flush after the enqueue observation has ended. A slow
native query cannot delay submission of already authorized bytes. Backpressure
still uses the same bounded queue and writable notification, and no authority
check, receipt deadline or loss requirement changes. A failed mapped receipt
also logs its exact capture sequence and original deadline for diagnosis.

The regression fails before the fix and passes afterward. The complete private
channel suite also passes, including congestion, destructive callbacks and
expiry. Evidence: `build/private-flush-regression-before.log`,
`build/private-flush-regression-after.log`, and
`build/private-flush-local-input-tests.log`. The rebuilt signed Mac package
passes five integrated and six application suites. Ubuntu passes six integrated
and seven application suites; its staged package resolves all dependencies.

Three consecutive fresh actual-desktop runs at this revision pass startup,
38 non-repeat native functional receipts, the complete 120-second Active
interval, all three 30-second recorder intervals, zero recorded loss, all 21
queue histories, restoration, process stop, neutral source input and unchanged
ordinary Mac settings. They use identical native binaries and source fixture:
`build/recording-replay-7ncfnjoo/`, `build/recording-replay-khwkbvxu/`, and
`build/recording-replay-tmvstz3r/`. Results and hashes:
`build/private-flush-sqtt1bm4/recording-desktop-verification.json`.

The Mac archive SHA-256 is
`f3f6c3a9a572e0f976c74e0cd272f0ed9622c7b690cf09b11ce3fd29e83751be`;
Ubuntu is `b912992380c7cca9ead85e3abe9d2456125ca9bc915a42e9d806ca732445a435`.
The synthetic driver still returns nonzero for its missing repeat receipt,
and full normalization still rejects native timestamp/raw mismatches. These
three successes cover the named non-repeat recording/lifecycle checks only.
The separate rapid-scroll check failed; see the repair and retained evidence above.

Held-key AppKit receipts and strict native timestamp matching remain open.
No physical acceptance row is complete and no attended launch is authorized.

## Measured Event concurrency repair — 2026-10-16

Previous native runtime: `7d50659b638bc0fd50d9c1b43c9bbbda6e8ac0d7`.
Current harness/postprocessor revision: `c3b545d785f9f646c1af3d34c7d04cae62a5f82d`.
The second run at the previous runtime (`build/recording-replay-n1guxg6p/`)
exposed concurrent measured `event-all-outstanding` mutations taking the same
single-writer diagnostic lock. Input delivery, restoration and neutral cleanup
completed, but the Core recording had loss=1 and was correctly rejected.

Measured Event counters now use an atomic pair of cumulative counts with a
fixed 16-attempt bound. Each row retains its exact snapshot and original clock
sample; preallocated consumer storage orders delayed copies by their counter
sequence. It never aggregates mutations, changes timestamps or overwrites a
pending row. Overflow, gaps, excessive contention, invalid boundaries and sticky
loss remain failures. Failed measurements retain actual lifetime accounting so
cleanup cannot mistake a live Event for an empty queue.

A concurrent-recording regression fails before the fix and passes afterward.
Additional cases cover delayed publication, bounded reorder storage, terminal
gaps, counter saturation and unchanged source rows/timestamps. The real
EventQueue suite also passes. Evidence: `build/event-counter-regression-before.log`,
`build/event-counter-regression-after.log`, and `build/event-counter-base-tests.log`.

Signed Mac package verification and five integrated/six application suites pass;
Ubuntu passes six integrated/seven application suites. Native package evidence:
`build/event-counter-v8bwiy65/`. All 104 coordinator checks and 42 postprocessor
checks pass on both machines from `build/event-validation-utnbg_qt/`.
The postprocessor now strictly validates the collector's optional unready
preparation diagnostics. It still rejects failed arms, loss, unknown metadata,
and native raw/timestamp mismatches. Saved journals are never rewritten.

The current repeated desktop checks additionally validate all three journal
schemas and all 21 queue histories using that strict parser. Results belong in
`build/event-counter-v8bwiy65/recording-desktop-verification.json`. Native
repeated-key and exact timestamp acceptance remain open; earlier successful
runs at other revisions do not establish this candidate's reliability.

## Recorder readiness race and cleanup repair — 2026-10-16

Previous candidate: `05a31025e206470e41a747639e6872731e03757c`.
The second full-recording run at `41d5ca17b`,
`build/recording-replay-z0bxhck9/`, rejected Host arm after a background message
made `private-output` nonempty between published readiness and the local arm
request. The journal did not lose entries. Source restoration, process stop and
neutral input succeeded, but terminal-log polling stopped at the rejected Host
acknowledgement, forcing the native fixture's fallback close.

Runtime now waits for a fresh local readiness check before the unchanged atomic
arm attempt, within the existing two-second coordinator budget. A pending
request never records, resets loss or retries a denied arm. Stop and the fixed
local arm deadline are terminal. New regression cases reproduce the stale
readiness rejection before the fix and cover output drain, pending cancellation,
expiry and forbidden late restart after it. Evidence: `build/arm-regression-before.log`
and `build/arm-regression-after.log`.

Cleanup now reads all owned recorder outputs separately from acceptance checks,
so a failed arm can still produce a graceful native close. Failed-arm acceptance
and wrong-process rejection remain strict. All 104 coordinator checks pass on
Mac and Ubuntu from the exact source archive in `build/arm-coordinator-16915b0s/`.
The rebuilt and signed Mac package passes four integrated and six application
suites. Ubuntu passes five integrated and seven application suites. Package
verification and all dependency checks pass; evidence belongs under
`build/arm-readiness-icjwz3r_/`. Repeated live recording validation is tracked
in its `recording-desktop-verification.json`; previous runtime successes do
not validate this candidate. GitHub checks for `41d5ca17b` are green, while
the newer `05a31025e` run is separate.

## Recording journal concurrency repair — 2026-10-16

Candidate runtime: `41d5ca17b`. The autonomous full-recording check
`build/recording-replay-sph3juuv/` at prior runtime `3995411a0` reached the
actual Ubuntu monitor, delivered Return through the original-input route and
armed all three recorders. The Core journal then reported loss=1 at
`journal-reserve`; Host and Native ended cleanly. The coordinator correctly
rejected that recording and restored the source. Keep this failed evidence.

The shared journal used a one-shot compare/exchange, so two independent valid
writers racing for an entry caused a lost row despite free space. A four-writer
regression reproduces that failure before the fix. The journal now reserves a
bounded capacity credit, then a unique atomic ticket. Credits include unfinished
writers and return only after the consumer copies a row. There are no retries,
waits or allocations in the producer. Same-counter contention, buffer overflow,
unknown ownership, incomplete drain and any actual loss still reject the run.

The regression passes after the fix, including repeated ring wrap and a stalled
unpublished row at full capacity. Evidence: `build/journal-regression-before.log`
and `build/journal-regression-after.log`. The signed Mac package passes four integrated and six application suites;
Ubuntu passes five integrated and seven application suites, including the new
journal regression. Build/package evidence is retained under
`build/journal-reservation-usos9_fz/`. The first live run
`build/recording-replay-g_6i_nbk/` completes the full 120-second active interval,
38 non-repeat native receipts, all three 30-second recordings with zero loss,
source restoration and neutral cleanup. Repeated-run verification is recorded
in that directory's `recording-desktop-verification.json`. The full replay still
fails synthetic repeat and strict timestamp preservation; physical acceptance
is not assessed. Coordinator source hashes are unchanged from the 102 Mac and
Ubuntu checks at `8e3a991c0`.

## Three autonomous input cycles — 2026-10-16

Runtime `3995411a03be3bae964a026d42a186232daea761` now completes three
consecutive actual-desktop runs: `build/handoff-replay-rbnium3p/`,
`build/handoff-replay-nvnrns82/`, and `build/handoff-replay-jbtyjcli/`.
Each reaches Active, verifies 20 non-repeat native receipts (typing, Shift+B,
click/drag, vertical and horizontal scroll), stays connected for the full
30-second pipeline interval, restores the source and stops every owned process.
All finish with zero held keys/buttons and unchanged ordinary Deskflow settings.
Ubuntu log archives and executable/fixture hashes are retained and verified.
Summary: `build/first-delivery-owatkikx/desktop-verification.json`.

These are scoped functional passes, not a complete input pass. Every replay
still exits nonzero on the synthetic repeated-x native receipt. The first run
preserves all compared timestamps; the next two include macOS timestamp changes.
H04/H06/H11 and every other physical acceptance row remain open. No failed
receipt or normalizer expectation has been rewritten.

Coordinator `8e3a991c0` additionally adds an explicitly synthetic Record-lifecycle
mode. Its 102 coordinator tests pass on both Mac and Ubuntu, bound to exact
source hashes in `build/recording-pipeline-60d6rcsr/`. Native packages remain at
`3995411a0`; no runtime source changed. The three runs above used the previous
coordinator, so they do not validate this new recording branch. GitHub checks
for the placement/startup candidate `c6cfdcb8c` are green; that CI result does
not cover the later diagnostic or recording-harness commits.

## First-click replay diagnosis — 2026-10-16

Current diagnostic runtime: `3995411a03be3bae964a026d42a186232daea761`, built
and packaged on Mac and Ubuntu. Placement/startup patches are unchanged from
`c6cfdcb8c`; this adds bounded first-native-delivery and precise revocation logs.
The placement repair reaches Active and completes the 30-second session in
`build/handoff-replay-2hz_3636/`, with source restoration and neutral cleanup.
Its replay fails before the first click; it must not count as an input pass.

`build/handoff-replay-lafs5dxe/` identifies the failure: the replay's Shift
setup cycle already acquires original-input ownership, but its first mouse
press has no preceding captured button release in this epoch. The coordinator
correctly revokes that unproven new acquisition. The trace independently shows
the earlier Shift delivered with 58 ms remaining on its mapped receipt. This
is separate from the repaired startup failures and the synthetic repeat issue.
The diagnostic driver now posts an explicitly marked button-up alongside its
initial key-up controls before switching routes. This is real captured test
input, not fabricated neutral evidence. The first-use/unknown-control contract
has not been weakened, and this setup cannot establish first-use physical UX.
The full native repeat and strict timestamp checks remain open.

Build/test/package evidence: `build/first-delivery-owatkikx/`. Mac coordinator
checks pass; Ubuntu passes five integrated and six standalone suites. The 101
Python coordinator checks are unchanged and remain bound by their source hashes.

## Placement guard cost repair — 2026-10-16

Earlier candidate: `c6cfdcb8c501e58c12bb8a8908957c8838887501` on both machines.
The next autonomous run (`build/handoff-replay-r73oqt21/`) completed the network
handshake but rolled back before Active: observer setup and native writes
repeated the full permission/display snapshot inside each authority fence.
The nine preparation operations consumed the two-second placement budget before
fresh geometry readback. All five native notification registrations themselves
succeeded. Source restoration, neutral input and ordinary-settings preservation
passed; this was not a successful input run.

ObservedPlacement now fences a single fresh native observation with cheap
in-process lifecycle checks. Every operation still checks native identity,
permissions and physical/holding display state before and after its AX call.
No snapshot is reused across an event-loop turn, and the absolute two-second
deadline and final AX/WindowServer agreement are unchanged. The authority helper
also handles token loss, native refusal, deadline overrun, cancellation and
owner destruction; those regressions pass with the placement/startup suites.
Build/package evidence: `build/placement-authority-hifhvuwx/`.

## Buffered startup handshake repair — 2026-10-16

Earlier candidate: `7212d309c4b8018cb8914e38cc7ea713f0b38f3b` on both machines.
The repeat run `build/handoff-replay-u_2q6vvf/` exposed another startup race:
Ubuntu sent hello and initial DINF together; the Mac changed stream readers
without waking the new reader for the already-buffered DINF. Since the packet
filter signals only readiness transitions, later bytes could not rescue it.
The client reached its heartbeat deadline before it was adopted by the server.

ClientProxyUnknown now queues a buffered-read wake after the versioned proxy
and its readiness handlers are installed, matching the client's existing
reader-handoff behavior. No connection, input or heartbeat deadline increased.
A deterministic regression reproduces the failure before the fix and passes
after it. Eight cases cover protocols 1.0/1.8, combined and split/partial packets,
and destruction before the queued read. Mac ServerTests, ClientProxyTests and
OriginalInputCoordinatorTests pass. Ubuntu passes those plus ServerProxyTests
and EiComputerTests, and five standalone GNOME/local-input/preflight/transport/
bridge suites. Evidence: `build/handshake-fix-1whkb2_i/` and
`build/handshake-regression-before.txt`, `build/handshake-regression-after.txt`.
The unchanged 101 coordinator checks remain bound by exact source hashes to
their earlier two-machine run. Signed Mac package verification also passes.

The first native replay below is historical input evidence, not proof of this
candidate's repeated startup reliability. The full synthetic repeat and strict
native-timestamp checks remain open; no physical acceptance row is closed.

## Input ownership repair and native replay — 2026-10-16

Earlier tested runtime: `2d51ac84c6da3a81aa037046adfb9f263a79c68c` on Mac and Ubuntu.
The earlier snapshot-retirement failure is fixed by `087d2f8b7`:
Ubuntu was cancelling its full input prefix when a Qt pointer snapshot expired
slightly before the request deadline, even though the independent prefix seal
was still live. The failing run (`build/handoff-replay-5e22n6j1/`) recorded
`sealLive=true` and 3,613 microseconds remaining at that cancellation.
A retired point now reports Sealed, with no mapping or ordering authority;
genuine seal revocation still denies. ServerProxy also handles retirement during
a mapped-point read only when that exact point was previously published.
Every subsequent input still requires a fresh map. Neither timeout was extended.

Regression checks cover independent Qt-point retirement, genuine revocation,
and eight single/batch, published/unpublished, live/revoked read races. Mac
ServerProxyTests pass. The current diagnostic build additionally passes Mac
ServerTests and OriginalInputCoordinatorTests; Ubuntu passes three integrated
suites and five GNOME/local-input/preflight/transport/bridge suites. The 101 Python
coordinator checks are unchanged and bound to their prior two-machine validation
by exact source hashes. Build/package evidence: `build/point-retirement-_lz86uog/`
and `build/repeat-diagnostics-sbvclj2z/`.

`build/handoff-replay-_zwa9xu_/` completes 20 independently observed native
receipts: initial click/release, A, Shift+B, x press/release, click/drag/release,
and vertical/horizontal scroll including phase endings. Raw fields, flags,
Host PID and destination window match. Input ownership survives the idle gap;
the 30-second actual-desktop session restores the source and finishes with no
held keys/buttons. This is synthetic functional evidence, not physical acceptance.

**The full replay still fails.** Its first x repeat reaches the source process's
PID tap but not AppKit's event handler. `build/handoff-replay-28wcl8yh/` traces
capture, mapping, private send and native submission for that exact repeat.
Independent disposable AppKit probes reproduce the loss with both direct CG and
NSEvent construction. The same probe receives repeats when its initial press
updates the HID state; the intercepted replay explicitly observes HID/combined
key state false. A process-local accent-menu setting does not fix it. This is
an observed synthetic-input limitation; real held-key behavior remains unverified.
No production event flags, test expectations or native receipts were rewritten.
The diagnostic driver retains the failed repeat result while continuing the
pointer/scroll checks; its exit remains nonzero.

Exact source-timestamp preservation also remains unaccepted: macOS sometimes
retimestamps target-PID delivery, as recorded in the earlier checkpoint below.
Do not close H04/H06/H11 from these synthetic runs. Continue autonomous disposable
checks; obtain fresh permission immediately before any attended physical test.
No results popup or sleep-based synchronization is wanted.

## Earlier handoff latency repair — 2026-10-16

Earlier tested runtime: `3b508d52400ceee1d7af20868e58b30e64953a66` on both
machines. The user authorized autonomous control of disposable test sessions;
a later attended test still needs fresh permission immediately before launch.

The unattended input replay reproduced the handoff expiry from attempt 32.
The runtime now disables TCP batching on window-sharing controls; the SSH relay
also disables batching on its accepted TCP socket. Native preparation uses the
exact fenced route/empty legacy ledger, with fresh native authority still
required at activation and delivery. Native delivery avoids redundant AX walks
around event construction/submission. Committed input now posts one bounded,
coalesced wake to the private Qt thread instead of relying only on polling.
Its regressions stop the timer and cover cross-thread commit, lost admission and
destruction before delivery. The original 500 ms preparation and 250 ms mapping
limits have not been extended.

`build/handoff-replay-41ufes4d/` reached Ready in 347 ms, queued/sent its first
original input in the same millisecond, and received that click at the exact
source window with matching raw effect and flags. Sharing ran for 30 seconds;
source restoration and neutral input cleanup pass. The strict replay still
**fails timestamp preservation**. Two separate self-targeted probes reproduce
macOS replacing a valid earlier CG timestamp at the destination PID tap, for
both CG and NSEvent-based construction. The event object retains the requested
timestamp immediately after posting. Probe evidence is retained privately in
`build/post-timestamp-probe-valid.jsonl` and
`build/post-timestamp-template-probe.jsonl`. No normalizer expectation was relaxed
and no native receipt was rewritten. Exact target timestamp preservation is
unsupported by these observed posting paths on this machine; physical H11
acceptance remains open.

Further functional replay uses a separately declared `--functional` diagnostic
profile: fresh native receipt, exact raw fields/flags, owned destination and Host
PID. It reports timestamp changes separately and cannot establish the strict
original-input acceptance contract. Do not equate a successful sharing cycle or
functional replay with physical acceptance. Complete the full functional replay
and repeat with identical artifacts before another attended attempt.

Validation: Mac coordinator suite passes, including the three new wake cases;
the preceding route change passes Mac preflight/bridge/secure-transport bridge
checks. Ubuntu integrated build/coordinator suite and four standalone
GNOME/preflight/transport/bridge suites pass. All 101 Python coordinator checks
pass on each machine. Native positive capture tests that skip on Linux remain
Mac-only. Evidence: `build/handoff-wake-bygcorgd/`,
`build/handoff-wake-tests.log`, `build/handoff-wake-coordinator-tests.log`, and
`build/handoff-wake-linux-coordinator-tests.log`. The signed Mac ZIP SHA-256 is
`1427c2a5056b633ce6dfbd66d51e61d375b11f56eee482e44ff2f71316bed82f`.
Ubuntu run archive SHA-256:
`36a3c7ef51f99fb53bea94acf64853fc93b6a15464b3c3edc91473059e9f544f`.

## Physical attempt 32: startup passes; original-input handoff expires — 2026-10-16

The user's direct “alright run the tests” request authorized
`build/physical-r32-bwjv118r/`. Frozen artifacts match all three passing unattended
runs. The user confirmed the shared source on jack's actual Ubuntu monitor.
Start at 22:20:41 UTC reached Active at 22:21:04 with verified hidden-source
placement. Keyboard, clicks and scroll reached the source through setup/legacy
traffic; this does not prove original-input delivery.

The original-input handoff at 22:21:29.592 reached focus preparation in 110 ms,
Host quiescence at 272 ms and mapped cutoff at 313 ms. Native preparation began
at 314 ms, with 185 ms remaining. The absolute 500 ms deadline expired at 502 ms,
and Core stopped original dispatch. Record was requested at 22:21:56, but neither
Core nor Host retained matching input ownership. At the 85-second recording
cutoff the coordinator stopped the session. No recorder armed; H04/H06 remain
unaccepted. This run did not reproduce the immediate startup crash.

Source restoration, neutral final keys/buttons, Core/source exit 0, owned-process
cleanup on both machines, Ubuntu desktop restoration and ordinary settings all
pass. Each unarmed recorder ended drained with loss 0 and zero measurement rows;
the missing armed measurement is the only recorded cleanup limitation. Ubuntu
archive SHA-256: `b0fe953d8cf99a27ddc98e128516a954b6502bc6d1072a488a9f04af05c15805`.
See the private `RESULTS.md`, `failure-summary.json` and retained traces.

Next work is to profile Host prepare/commit validation inside this deadline;
the existing trace does not identify a single slow native call. Reproduce and
repair the measured cause before requesting another physical attempt. Preserve
the deadline and authority checks. This attempt's launch authorization is
consumed; no second attended test has been launched.

## Autonomous pipeline verification — 2026-10-16

The user explicitly authorized unattended real-computer pipeline testing after
attended attempt 31 failed immediately. Use `physical-macos.py --pipeline-check`
with a fresh production plan for jack's actual Ubuntu desktop. The fixture
invokes its own guarded Start button, observes sharing for 30 seconds, then
verifies disconnect, source restoration and owned-process cleanup. It never
arms original-input recording or claims physical acceptance. Ask immediately
before a later human keyboard/trackpad test. No results popup or new sleeps.

The unattended runs reproduced and isolated these failures:

- `pipeline-run-n43f_473`: Core SIGILL in `TISCreateInputSourceList` on its worker
  thread. The saved macOS crash report identifies a main-queue assertion.
  `b3311dceb` marshals Text Input Services operations through the Qt main loop.
- `pipeline-run-qwwji94d`: the same holding display changed from
  `(1440,-1,4096,4096)` to `(1440,0,4096,4096)` during placement. `64ac90019`
  waits for the requested origin before accepting parent-side readiness.
- `pipeline-run-7c5anwbz`: AX CannotComplete (-25204) during an initial frame
  read after display creation. `9d570f20b` makes pre-journal, pre-move and final
  observations asynchronous and bounded. Only transient read errors remain
  pending. Changed identity, authority, frame or display still rejects; duplicate
  callbacks cannot reopen a transaction. Native writes are never retried.
- `pipeline-run-g6cy9z5g`: sharing stayed active for the full 30 seconds, then
  the coordinator's stop-message payload collided with a diagnostic argument.
  `d5e2bed3c` makes envelope arguments positional-only, preserving payload fields.
  Cleanup succeeded even in this failed run.

The corrected candidate passed one complete actual-desktop cycle at
`build/pipeline-run-bq7cnhdy/`: automatic Start, verified hidden source, 30 seconds
Active, source restoration before Host termination, Core/source exit 0, stopped
companions and preserved ordinary settings on both machines. Its status is
`pipeline-verified-no-physical-input`; original-input acceptance remains
`not-assessed`. Repeats `pipeline-run-6a_fwb49` and `pipeline-run-3dlbyv53` exposed a second
race: ordinary polling read AX while holding-display creation was pending and
returned the source prematurely. `5c113c700` leaves transient frame observation
to the bounded display transaction while retaining identity, topology,
permission, capture and preparation-deadline checks. The final candidate passes three consecutive fresh cycles:
`pipeline-run-zk3l3ege`, `pipeline-run-3b0qzu_a` and `pipeline-run-6ksuq816`.
Each verifies 30 seconds Active, hidden-source placement, restoration while Host
is alive, Core/source exit 0, stopped owned processes, unchanged ordinary settings
and no cleanup errors. Frozen executable hashes match across all three runs.
All three Ubuntu result archives are hash-verified. This closes unattended
startup/sharing/return verification for this candidate, not physical input.

Validation is retained in `build/pipeline-verify-fg881kq7/`:

- All 100 coordinator checks pass on Mac and Ubuntu.
- Mac holding-startup, native-placement and holding suites pass 3/3, including
  asynchronous callback, stale owner/display and duplicate-completion cases.
- The updated integrated Ubuntu Core build and both shared placement/startup
  suites pass. These isolated checks start no desktop or input devices.
- The native `OSXMainThreadTests` startup regression passes, including actual
  read-only layout enumeration from a worker, main-thread reentry, return-value
  ownership and exception propagation. GitHub Actions now includes it in both
  enabled and disabled Core builds. Both local configurations pass, as does
  the scoped ASan/UBSan relocation suite (Qt/system libraries are not instrumented).
  [Hosted run 38971647320](https://github.com/JackHars/deskflow/actions/runs/38971647320)
  completed successfully for `e228da3e0`: all six jobs pass, including both
  core compilers, transport, Mac, Linux and GNOME. The source artifact independently
  verifies all 1,189 tracked files, Git commit/tree, file types and executable modes.
- The signed Mac package uses source `e228da3e0` (native fix `5c113c700`) and passes exact-library
  seal/dependency verification. ZIP SHA-256:
  `9455e08e2fd13c2f6ef2f70fd508e4b0118363204b37bce9854343bc055f1304`.
  Actual Ubuntu keeps verified runtime `65dd42362`: changed runtime paths are
  Mac-only, and shared relocation logic is used only by the Mac implementation
  and the cross-platform regression tests.

Earlier failure logs, crash report, cleanup reports and hash-verified Ubuntu
result archives are retained in their respective run directories. Attempt 31
used `build/h06-r31-a17thl_i/`; its attended permission is consumed. Repeated
unattended passes do not close H03–H12, original-input delivery, or the remaining
native/library/OS queue certification gaps. The first main-thread dispatch
prototype deadlocked and was replaced before the passing tests; do not use a
blocking GCD dispatch under QCoreApplication.

## H06 attempt 30: native placement authority failure before Active — 2026-10-16

Permission `call_640f0af3231740d5a354cedc47ba0ef2` was consumed by
`build/h06-r30-i4aq8o8x/`, native runtime `65dd42362` and coordinator `f5f7179b9`.
Start was clicked at 20:48:15.379 UTC. Companion admission and Ubuntu forwarding
completed, but hidden-source readback failed at 20:48:34.490 with `Source placement
authority was revoked`. The retained lifecycle guard had no failure reason;
the old compound native/display predicate did not identify which prerequisite
failed. Do not infer that a permission was revoked or a source identity changed.
Active, original-input ownership and recording were never reached; scrolling
was not exercised.

Observer registrations and native writes ran consecutively on the Host event
loop. Four registrations and two writes returned AX CannotComplete after about
100ms each, alongside repeated display/identity queries. Host readiness had a
2342.66ms gap; Core's largest gap was 701.19ms. At 20:48:34.885 Core independently
closed the private channel with a 2715ms heartbeat age against its 2500ms budget.
This was later than the placement failure and is not proof of its cause.

Cleanup verifies the source returned to (400,188,640,448), with zero held keys
and buttons. Core exited 0 through private IPC, its companion and owned local
processes stopped, and Ubuntu reports its desktop restored and ordinary settings
preserved. Only unarmed input evidence remains a cleanup limitation. See
`failure-summary.json`. Verified Ubuntu archive SHA-256:
`e0bd0c82d9fe22ed6b79412aad21fdc7032349a856a66e63d5e4ae6db1861825`.

Mac runtime `d3d15a6d2` queues observer creation, each registration and each native
attribute write separately. The original two-second placement deadline includes
all preparation; cancellation, expiry, owner destruction or a failed step drops
remaining writes and prevents readback/commit. Notifications cannot skip steps.
A single verified holding-display observation supplies its ID and bounds,
avoiding several repeated full native queries at each guard. Fresh verification
still runs before/after native work. Exact prerequisite failures and slow native
steps are printed. No sleeps, deadline extensions or weaker input authority were
introduced. One blocking native call cannot be preempted by these queued steps.
The exact physical failure remains open until a successful retest.

Evidence: `build/placement-yield-nrbmlqfl/`. The Mac application/helper build and
four focused placement/holding/activity suites pass. The shared placement and
holding-startup suites pass 2/2 in an isolated Ubuntu container; no user desktop
or input was used. Scoped Mac ASan/UBSan placement checks pass 1/1; Qt/system
libraries are not instrumented. Regression callbacks model slow native work
without sleeps and prove independent dispatch, ordering, non-reentrancy, fresh
readback, absolute expiry, cancellation and destruction behavior. The unchanged
coordinator retains its 96/96 passing checks on each machine. Initial compile
errors were corrected before the passing build.

The signed Mac package passes its dependency/seal audit with no errors; ZIP
SHA-256 `2738d5f3f308d4964f4e507008d126b3e3929643d785ad8ae4950dd053ed9f7e`.
Ubuntu keeps the previously verified `65dd42362` native package: the changed
native files are Mac-only or test-only and the Linux runtime sources are
unchanged. The new shared verifier tests were rebuilt on Ubuntu. The previous
native runtime passes all six hosted jobs in run `38962542803`; that result must
not be represented as hosted verification of the new Mac runtime.

Historical preparation: attempt 31 was prepared at `build/h06-r31-a17thl_i/`, with fresh
private identities, Mac runtime `d3d15a6d2` and unchanged Ubuntu runtime
`65dd42362`. Preflight at 21:00:39 UTC confirms actual jack's unlocked Wayland
session 2, all Mac permissions and no competing primary. Exact packaged admission
passes in 11.706s with native input disabled and its companion stopped. Preparation
opened no window and enabled no desktop provider. See `source-binding.json`,
`preparation-summary.json` and `final-preflight.json`. Obtain fresh permission
immediately before launching this candidate. H03–H12 and native/library/OS queue
certification remain open.

## H06 attempt 29: moving local controls aborted preparation — 2026-10-16

Permission `call_8c719a47e7014336b21d23711cee286b` was consumed by
`build/h06-r29-x9_agpvf/`, native runtime `65dd42362`. Start was clicked at
20:34:40.689 UTC. At 20:34:55.089 the harness stopped while waiting for the
companion: `Holding requires visible, stationary local controls`. Active,
original-input ownership and recording were never reached. This attempt did
not exercise the earlier scrolling/preparation failure.

The exact local controls remained on screen, native/Qt-visible and not minimized.
Their native frame moved from (480,278,480,268) to (526,322,480,268), while Qt
sampled (545,348,480,268). The old aggregate visibility flag also required frame
agreement, so an ordinary drag falsely failed the stationary-controls rule.
The source identity/frame stayed unchanged at (400,188,640,448), with zero held
keys/buttons. Core exited 0 via private IPC, its companion and local owned
processes stopped, and Ubuntu reports desktop restoration and ordinary settings
preserved. The original report retains unarmed-evidence and control-presentation
cleanup errors; the latter masked the intact source. See `failure-summary.json`.
Ubuntu result archive SHA-256:
`f572c373386d9e4c4b1b1c582794254d574216a68c34aba35231576ed698c7ee`.

Coordinator `f5f7179b9` accepts movement of the separate controls using their exact
identity, valid native frame and current native/Qt visibility observations.
Start still waits for settled presentation, but a new settled controls position
is valid. Hidden-source verification still requires exact Host readback and no
physical-display overlap. Source restoration checks retained identities, neutral
input, the original source frame and an empty restore journal independently of
controls presentation. Hidden/minimized controls still stop an active session.
No native runtime, authority rule, deadline or input policy changed; no sleeps
were added.

Evidence: `build/moving-controls-bi7dnqpk/`. Both new reproductions fail against
the prior coordinator. All 96 coordinator tests pass on Mac and actual jack, and
the recorded attempt-29 final receipt passes the corrected identity check.
These checks start no native input or GUI. Native runtime `65dd42362` also passes
all six jobs in [hosted run 38962542803](https://github.com/JackHars/deskflow/actions/runs/38962542803),
including the corrected GNOME fixture. These are not physical acceptance passes.

Attempt 30 used the corrected coordinator with unchanged tested native packages.
Its permission is now consumed; see the newer attempt-30 entry for its native
placement failure, cleanup and next prepared candidate.

## H06 attempt 28: preparation timeout and Host stall during input — 2026-10-16

Permission `call_25eab23a1a89476583ee2b0ae5c2b65b` was consumed by
`build/h06-r28-3gbg3emp/`, runtime `fcd3547516`, on jack's actual Ubuntu
monitor. Active was reached at 20:10:33.107 UTC. The user reported closure
when scrolling in the text box. Original-input preparation had already been
requested at 20:10:50.427 and expired at 20:10:50.928, before the later stall.
No original-input owner became live and recording never armed.

The App Nap correction worked: macOS records this exact Host PID 18915 as
inactive for App Nap with Tier0 timers at 20:10:47.312. It did not eliminate
the later stall. Core revoked the private channel at 20:11:03.409; Host
readiness gaps reached 2138ms and 1874ms, versus 165ms in Core. The saved
outer failure was Host `QueueAccounting` with no site. That may be secondary
to teardown; it is not proof that scrolling or queue accounting caused the
stall. See `build/h06-r28-3gbg3emp/failure-summary.json` and
`mac-scheduling-early.log` for the retained sequence.

Cleanup restored the source to (400,188,640,448), with zero held keys/buttons.
Core exited 0 through private IPC; the companion and owned local processes
stopped. Ubuntu reports desktop restoration, owned processes stopped and
ordinary settings preserved. Unarmed evidence and Ubuntu stop exit 1 remain
recorded. Ubuntu archive SHA-256:
`a3c9b469292236b2e1cd15bada20a7e7f9c11e35508edf6104dbfc218efad4bf`.

Runtime `65dd423626626da0b2071067a855d62f30cc9a7a` addresses two demonstrated
scheduling defects: repeated socket notifications could bypass the network
read work budget, and private reads could recursively dispatch and had no
elapsed-work budget. Both channels now coalesce deferred reads; private
reads also yield between callbacks after 8ms. Individual native calls cannot
be preempted by this budget. No authority deadline or routing rule is relaxed.
Real socket/process regressions fail against the earlier implementation and
pass after correction. Slow-callback scheduling uses a fake work clock;
no sleeps were added. The existing retired-capture authorization test now
injects at the authorization boundary independently of serialized decoding,
and still proves that the older record cannot be delivered or reset replay state.

Diagnostics print each initial handoff stage once with elapsed time and the
original remaining budget, synchronous native input/poll/child checks taking
25ms or more, the first private-channel close reason with queue/heartbeat
counts, and the exact private accounting failure site. These diagnostics do
not authorize input or extend deadlines. The physical cause still needs
verification; this change is not a claimed H06 pass.

Evidence: `build/read-dispatch-ui9j2vl6/`. Mac private input, both TLS backends,
preflight, companion and evidence suites pass; integrated coordinator tests
pass. Ubuntu's five corresponding suites and integrated coordinator tests
pass. Both machines pass 45 Python fixture checks. Mac scoped ASan/UBSan
private-channel and transport suites pass 2/2 (73.36s); Qt/system libraries
are not instrumented. Initial fixture failures and the corrected checks are
retained, as is the build invocation with an incorrect final target name.
The corrected target builds successfully.

Previous hosted run `38959812367` finished with five jobs passing and a
GNOME fixture failure: two separate event-log snapshots produced an invalid
interval. The fixture now freezes one snapshot for each interval, retaining
its strict input-leak checks. [Hosted run 38962542803](https://github.com/JackHars/deskflow/actions/runs/38962542803)
passes all six jobs. Source run `38962542788` passes and independently verifies
all 1187 file contents/modes, the index and exact commit. Source archive SHA-256:
`d2375b8276f15a27bb9cfadacbc42bfea848fb02c6ecc02f66c8054a78d0dc1b`.

Attempt 28's permission is consumed. Build/package preparation is unattended;
obtain fresh permission immediately before the next physical launch. H03–H12
and the native/library/OS queue review remain open.

The native packages used in attempts 29/30 are unchanged. Mac package audit
passes 77 libraries with no errors; ZIP SHA-256
`9b70aef540350b1c3e1b4a5ed1d464d3f4efc547f9c9096cf055498fbc31fffd`.
Ubuntu archive SHA-256 `f64fbce9be5281d8edb42604049dfa5707b10d734115c0c2cef3854ad0eb4b6f`;
all 26 manifest files, native dependencies and offscreen startup checks pass.
See the newer attempt-29 entry for its consumed permission, failure and cleanup.

## H06 attempt 27: hidden Host throttled by App Nap — 2026-10-16

Fresh permission `call_7a128b3087b84019a91d1068462f0435` was consumed by
`build/h06-r27-ocvmxz5k/`, runtime `b20bbf39d2`, on jack's actual Ubuntu desktop.
The notification fix passed its first physical setup check: unavailable AX
notifications no longer aborted placement, exact readback verified the hidden
source, both peers authenticated and the test reached Active at 19:59:35 UTC.
This is setup evidence, not completed H06 acceptance.

The first original-input acquisition requested preparation at 19:59:51.981 UTC
and expired at 19:59:52.483 within the unchanged 500ms deadline. Core/Host never
reported a live original-input owner and recording did not arm. At 20:00:10 the
Host closed its private channel after a 3358ms heartbeat gap (budget 2500ms).
The Mac system log independently records App Nap active with timer, socket,
CPU and priority throttling for this exact Host PID from 19:59:46.961 UTC.
Host evidence has gaps of 2276ms and 1587ms while Core's largest gap is 179ms.
There is no new native crash report; the saved failure is companion exit.

Runtime `fcd35475167f21e6e9d4ae598261dab358423150` gives each active export a scoped
`NSActivityUserInitiatedAllowingIdleSystemSleep` activity, acquired before
capture and released with that exact export, including setup failure and
restoration. Logs retain activity acquisition/release. It does not inhibit
system/display sleep, alter input authority, extend deadlines or add sleeps.
Subsequent attempt 28 verifies App Nap inactive; preparation and a later stall
remain unresolved, as recorded in the newest entry above.

Mac Host build and placement/recovery/activity tests pass 3/3 (1.10s). Activity
checks cover overlapping exports, individual/last release, setup-error cleanup,
failed acquisition and actual Foundation acquisition without windows or input.
Evidence: `build/sharing-activity-ohqg2wo6/`. The first multi-target build regenerated
the Makefiles and built Host, then could not find the newly added target in the
old Make invocation; the subsequent test-target build passes. Linux runtime
sources are unchanged; retain the verified `b20bbf39d2` Ubuntu package.
All six hosted jobs for that previous runtime now pass in
[run 38924548952](https://github.com/JackHars/deskflow/actions/runs/38924548952).

Cleanup restored the source to (400,188,640,448) with zero held keys/buttons;
Mac Core exited 0 through private IPC, companion/owned processes stopped and
ordinary settings hashes match. Ubuntu reports desktop restoration, owned
processes stopped and ordinary settings preserved. SSH exit 255, broken-pipe
stop diagnostics and unarmed evidence remain recorded. No physical pass is
claimed. Full Ubuntu results archive SHA-256:
`0a857d0297631d03a53c7750cea56e3606ff92c12abb97781bc8535b28403433`.
See `build/h06-r27-ocvmxz5k/failure-summary.json` and `mac-scheduling.log`.
Attempt 27's permission is consumed; obtain fresh permission for the next launch.


The signed Mac package passes its 77-library audit with no errors. ZIP SHA-256:
`655f7343385128f23fdd5829aa36e0e5b876085f518e65f53c6d6b60e7ce3808`.
Hosted source bundle `38959812726` independently verifies all 1187 tracked
files/modes/blobs and the exact commit. Its archive SHA-256 is
`75c47838d5a05411d9f5f4654b76edcd70eb15c170b5942e5a49e8c36af61596`.
The [six-job hosted run](https://github.com/JackHars/deskflow/actions/runs/38959812367)
finished with five passes and one GNOME fixture snapshot race, addressed above.

Attempt 28 was prepared at `build/h06-r28-3gbg3emp/` with fresh identities,
the signed `fcd3547516` Mac package and verified compatible `b20bbf39d2`
Ubuntu package. Its passing preflight did not authorize delivery. Fresh
permission was subsequently granted and consumed; the failure is recorded
in the newest entry above.

## H06 attempt 26: AX notification registration failure — 2026-10-16

Fresh permission `call_65bb3e495b6f4b93a2db2146d028f89e` was consumed by
`build/h06-r26-nqwk_g_1/`, runtime `26b2bc6a4`, on jack's actual Ubuntu desktop.
The user reported that everything closed when the connection started. The
saved sequence identifies an intentional setup abort before Active: both peers
authenticated, the native observer was created, then registering `AXMoved`
returned `-25204` (`kAXErrorCannotComplete`) after about 100ms. Placement failed
at 15:23:00.651 UTC (11:23:00 locally), before any `source.holding.place` write.
The outer diagnostic reported unverified hidden-display placement. This run
does not test the earlier original-input owner/readiness failure.

Source restoration to (400,188,640,448) passed, the final receipt had zero held
keys/buttons, the Mac Core stopped through private IPC with exit 0, its companion
and the owned local processes stopped, and both ordinary Mac settings hashes
were preserved. Ubuntu reported owned processes stopped, desktop restored and
ordinary settings preserved. SSH exit 255 and unarmed input-evidence cleanup
remain recorded; no input acceptance is claimed. The full remote results were
copied and verified with archive SHA-256
`6b728e13f0ba0e5b8033b13409b854ec295e19bb21fb84238921136001d07c31`.

Runtime `b20bbf39d2da5cb5211e87151b98d971fe0dc4ac` treats an unavailable
movement notification as a missing wakeup, using the Host's existing watchdog
for fresh placement readback. This is consistent with the existing path for
unsupported notifications. Neither the notification result nor a successful
native write authorizes placement: the exact owner, AX/WindowServer frames,
permissions, holding layout and original two-second deadline still must pass.
Permanent invalid-element/observer, argument, API-disabled and memory failures
still stop setup. No sleeps, registration retries, repeated moves or extended
deadlines were added. Logs mark when watchdog readback is required.
Apple documents `CannotComplete` as a messaging failure in its
[notification API reference](https://developer.apple.com/documentation/applicationservices/1462089-axobserveraddnotification).

The regression exercises the production registration policy and asynchronous
placement gate without opening a window or subscribing to another application.
The old policy fails the exact timeout case. The correction passes a missing
notification at each position, all notifications unavailable, fresh later
readback, unchanged deadline expiry, and permanent errors after a transient
error. Mac placement and recovery suites pass 2/2 (1.35s); the placement suite
also passes scoped ASan/UBSan (0.10s). The initial sanitizer invocation requested
unsupported macOS leak detection and stopped before running tests; the corrected
run disables that facility, with the initial output retained. Qt/system
libraries are not instrumented. Evidence is in
`build/placement-notification-fcw2ecg2/`.

The separately signed Mac package passes its exact-library audit (77 records,
no errors). ZIP SHA-256:
`e3e6e6465964d1819b945eebce861f370a10fc8ffbdba5ee0ee873e1cbbf4ddb`.
Hosted source bundle `38924548926` verifies all 1185 tracked files/modes/blob
identities and the exact commit; archive SHA-256:
`766c28d4ac4ef89b648bd16a0ebb855b85f0c93849fef41d02ea3324a6e0d91a`.
[Hosted run 38924548952](https://github.com/JackHars/deskflow/actions/runs/38924548952)
passed all six jobs. The next physical launch requires fresh permission; attempt
26's permission is consumed.


A fresh isolated build on actual jack passes the Ubuntu Core and Viewer builds
and the portable placement suite (1/1; the native AX cases are Mac-only). The
26-member inert package verifies; both executables resolve their actual-host
libraries and pass offscreen `--help` without desktop connection variables.
Linux archive SHA-256:
`5b17f69c1d5fcddb50da5c7cdd10a79f2cba16e7d9bef8f28dca6104a901c3e5`.

Attempt 27 was prepared as `build/h06-r27-ocvmxz5k/`, frozen to this runtime with
fresh paired identities. Both live preflights pass: actual jack's unlocked
Wayland session, matching staged binary hashes, separate Mac controls and
hidden-source configuration, no competing Mac Core, permissions, and exact
packaged admission (12.017s, native input disabled).
Preparation did not open a test window or start sharing. Its later approved
physical launch and failure are recorded above; this plan must not be reused.

## Unattended input hardening — 2026-10-16

The user is away and deferred physical testing. Prepared attempt 25 remains
unlaunched; its older frozen build is not the current candidate. Obtain fresh
permission immediately before any future physical launch. No desktop window,
input sharing, test input or results popup was started in this continuation.

Runtime `a31d08c0d273c8535a252eb7d2787e823a5fffc6` fixes a reproducible route
correlation race. An uncertain one-part capture is already ineligible for
original delivery. If its late Core decision arrived before the Session popped
the denial, the join incorrectly applied the exact-input deadline and revoked
the route. The same decision after the pop was accepted only as a denial.
Both arrival orders now retain the denial, with no delivery authority. This
first fix preserved the existing exact-input history deadline; the separate
follow-up below corrects its mismatch with asynchronous preparation. Sequence,
declared-part and capacity checks remain required. The regression failed
before the fix and passes afterward without sleeps. This is an isolated reproduction, not
proof of attempt 24's exact revocation cause.

Diagnostics now retain the first capture-inbox failure (binding, malformed
capture, ordering, capacity or epoch loss), report the route-join failure, and
record each first Server eligibility boundary before a coordinator reservation.
These low-frequency logs run outside the native event tap and contain no key
codes, text or clipboard contents. The Python coordinator now reads the actual
nested `preparation` object, preserves failed readiness details, and identifies
missing/fresh/post-Record readiness and mismatched Core/Host ownership. Older
logs without preparation details remain unknown; diagnostics cannot authorize
recording or change physical acceptance.

Follow-up runtime `26b2bc6a46963fa8d1a9db30eac187ed09f5e12b` adds a second
regression joining the actual Server pending-input path to its historical
route observer. At 450ms, the Server's 500ms preparation is still valid, but the
old 250ms history timeout revoked their shared conduit. The test fails before
the correction. History now has a bounded two-second observation lifetime,
consistent with the experimental Core residence budget, and a compile-time
check requires it to outlive capture plus preparation. This is not a delivery
lease: the 100ms capture-arrival, 500ms preparation and at-most-250ms mapped
point deadlines are unchanged. Missing history still expires, buffers remain
bounded, and historical observations cannot grant input authority. The combined
Server/coordinator/history run passes on Mac (3/3, 5.89s); the changed portable
route suite passes scoped ASan/UBSan again (0.62s). A fresh isolated build on
actual jack passes Core compilation and both Server/coordinator suites
(2/2, 0.22s; native Mac-only cases are explicitly skipped on Linux), the three
portable sanitizer suites (3/3, 0.24s), and 92 Python checks. Evidence and the
frozen source archive are in `build/unattended-history-imi65vqc/`.

Validation is retained in `build/unattended-input-2k6lxjf7/`:

- Mac Core and Host builds pass. Server and production coordinator suites pass
  2/2 (5.56s) after the final Server diagnostics change.
- Portable original-input, route-join/inbox and press-routing suites pass scoped
  ASan/UBSan on Mac (3/3, 1.48s) and actual jack's isolated Ubuntu image
  (3/3, 0.25s). The changed portable sources are identical in both runs.
  The Ubuntu container used `--init`, no network, no desktop or input-device
  mounts, and dropped capabilities. Qt/system libraries are not instrumented.
- Python physical-coordinator regressions pass 92/92 on Mac and Ubuntu.
- The initial Mac command named a nonexistent aggregate `unittests` target
  after successfully building Core; the explicit targets above pass. The first
  remote runner had a placeholder-substitution syntax error before executing
  tests; the corrected runner passes. Both original logs remain retained.

The previous runtime `fe330635c` now passes all six hosted jobs in
[run 38844510594](https://github.com/JackHars/deskflow/actions/runs/38844510594).
Runtime `a31d08c0d` also passes all six jobs in
[run 38855480046](https://github.com/JackHars/deskflow/actions/runs/38855480046).
Final runtime `26b2bc6a4` passes all six jobs in
[run 38856650856](https://github.com/JackHars/deskflow/actions/runs/38856650856):
both portable compiler/sanitizer jobs, transport, Linux application, Mac
application and native GNOME integration. The original-input owner/readiness
issue from attempt 24 still needs diagnosis on the approved physical route. H03–H12 acceptance and
unverified native/OS queue bounds remain open; automated results do not close
them.

The new Apple-issued package passes its audit (77 exact-library records, no
errors). ZIP SHA-256:
`e3769643434329570a18b7c044f80d0d9d3d56d8128e28b9e0bd4c805d2ca3e4`.
The package was staged separately; no installation or physical launch occurred.
Hosted source bundle `38855479671` independently verifies all 1184 tracked
files/modes/blob identities and the exact commit. Source archive SHA-256:
`1e08661dd3f593fa07c1f97cb957909d7a8f9ab3f1150f78e9fc846e0c8aed26`.

The final follow-up package also passes the 77-library audit and its private
code-admission probe (12.257s total, exact admission verified, companion stopped,
native input hooks/original input disabled). Its ZIP SHA-256 is
`adc6d9952b749d5172de88e1bc6c6359dfcd1d81e169872dc7fb7ab21f0661b6`.
It is staged under `build/unattended-history-imi65vqc/mac-package/`; no physical
launch configuration was activated. Attempt 25 remains deferred and superseded.

The final hosted source bundle `38856650837` independently verifies all
1184 tracked files/modes/blob identities and the exact commit. Its archive
SHA-256 is `f3e63f65e9f3f5785155fa1238775892b2401d40bda11a528d841a295bb27eac`.

The matching Ubuntu runtime is also staged without installation. The Viewer
was freshly built against the same frozen runtime sources and packaged with
the newly built Core. All 1184 tracked blob identities were compared with
`26b2bc6a4`; only the five recorded Markdown files differ in the earlier frozen
snapshot. The 26-member inert package passes verification, both executables
resolve their libraries on actual jack, and both offscreen `--help` runs exit
successfully with desktop connection variables removed. Archive SHA-256:
`a854af1a015858e26b34eab5b452a2011334b5732bb3e4341e8624a21e939323`.
The package and stage are retained under jack's
`/home/jack/src/deskflow-h04-20261015-zgta6i4f/unattended-history-imi65vqc/`.
This verifies staging and loading, not desktop input or install acceptance.

The unexecuted `build/prepare-h06-history.py` helper now selects these exact
Ubuntu binaries, rejects later runtime-source changes and compares the
prepared remote binary hashes with the tested package. It retains separate
Mac controls, actual jack desktop, hidden source placement and no results
popup. It has only been syntax checked; preparation and physical launch remain
deferred. Recheck the live desktop/session and permissions when the user returns,
then obtain fresh launch permission immediately before execution.

## H06 attempt twenty-four: placement passed; input acceptance remains open — 2026-10-15

Permission `call_caf1c5af5eb74a32b699f02f82a6e588` was consumed by
`build/h06-r24-vtm4s8z9/`, using runtime `1f489fce0`. Start was recorded at
02:49:44 UTC October 16 (October 15 locally). The Host verified source placement
at (1440,188,640,448), outside the physical Mac display, and committed export.
Active began at 02:50:03.424; typing, button and scroll receipts followed.
The native placement change therefore passed its previously failing physical step.

This run still **failed**: original dispatch revoked at 02:51:11.575, then the
controls reported a missing/stale coordinator heartbeat at 02:51:13.879, roughly
70 seconds into the 120-second session. Return reached the source and requested
Record, but Core/Host never reported a ready original-input owner; recording was
never armed. Legacy receipts do not prove original-field delivery. The logs do
not yet distinguish the input-revocation cause, nor the exact heartbeat read
failure. Do not mark H04/H06 accepted.

Cleanup returned the source to (400,188,640,448), stopped the owned processes,
and preserved ordinary settings on both machines; Ubuntu's provider is inactive.
The final fixture ledger nevertheless contains one held key: Right arrow
(Qt 16777236/native 124). The full receipt collector shows an untagged repeat
from the switching chord reaching the Mac source with no matching local up.
Other observed typed keys released. This is not a neutral-input acceptance;
no ledger was cleared to hide the unmatched event. Mac SSH exit 255 remains
recorded. Ubuntu result archive SHA-256:
`bf0f660517b7fb3e17729ba54aab8549d1c690f86ca3ebfd22f595350267e469`.

Follow-up implementation:

- The status reader distinguishes a verified concurrent atomic replacement
  from malformed, missing, insecure or in-place-mutated data. A replaced inode
  can preserve only the last validated state's existing two-second lease;
  it cannot renew the heartbeat, consume new state or authorize Start.
  Invalid publications still fail immediately. Ready/Active deadlines are unchanged.
- Deterministic tests replace the actual file after open and after read. They
  reproduce the old metadata race without sleeps and check fixed expiry, initial
  freshness, duplicate JSON keys, permissions, links, deletion and mutation.
  The race is demonstrated in isolation, not retrospectively proven as this
  run's precise read failure. New logs report reason, errno, heartbeat age,
  read duration and poll gap.
- Mac shortcut handling now consumes a registered physical key cycle in the
  tap before the asynchronous screen switch completes. Repeats cannot leak to
  the source or retrigger the shortcut; release stays consumed after modifiers
  change. Ordinary held keys are not converted into shortcuts mid-press, and
  an orphan shortcut repeat cannot fabricate another shortcut action.
- Capture continuity/inbox failure and the first preparation decisions now
  print bounded diagnostics outside the native tap. Fixture receipts include
  held Qt key codes. These additions diagnose the still-unresolved owner/
  revocation problem; they do not assert it is fixed.

Evidence: `build/manual-heartbeat-ihbxq4t7/`. Mac runtime/fixture builds pass;
manual-status, shortcut, companion and evidence suites pass 4/4 (12.86s), and
Server/coordinator suites pass 2/2 (5.98s). Python coordinator checks pass 84/84.
Both new portable regressions pass scoped ASan/UBSan on Mac and actual jack's
isolated Ubuntu image. Qt/system libraries are outside sanitizer coverage.
Linux builds the changed library and passes status/shortcut/preflight/evidence.
The initial companion test failed without container init; a focused reproduction
confirmed an already-exited child retained as zombie Z under PID 1. The unchanged
suite passes with `--init` (5.71s). Initial failure and reproduction are retained;
no production cleanup assertion or timeout was weakened.

All six hosted jobs for placement runtime `1f489fce0` now pass in
[run 38841354672](https://github.com/JackHars/deskflow/actions/runs/38841354672).
This hosted pass precedes the current fixes. No new physical run has launched;
fresh permission is required immediately before the next one.
The new Apple-issued package audit passes (77 exact-library records, no errors).
ZIP SHA-256: `696cd0ed5c644b2e98602aeaf9cf67bd82395197f9ea25d70f222ab7b355bc7a`.

The follow-up is published as `fe330635ca1fd4dd64306022c61c123a694efe05`.
Hosted source bundle `38844510660` independently verifies all 1184 tracked
files, modes and exact Git blob/commit identities. Source archive SHA-256:
`e6a5bea160836b77ec96f2a240dd1c92b0a7365577760de6972de5b10cb0a55d`.
Foundation run `38844510594` subsequently passed all six jobs.

Fresh attempt 25 is `build/h06-r25-9cri24sx/`, frozen to this commit and the
package above. Plan validation, Mac permission/conflict checks and packaged
admission pass (11.758s, native input hooks disabled, companion stopped).
Actual jack's unlocked local Wayland session 2 is active; its provider is
disabled until launch. Holding/separate Mac controls are configured, with no
Mac desktop preview or results popup. No new window or input session has opened.
Fresh launch permission is required; do not reuse attempt 24's permission.

## Attempt twenty-three: asynchronous native placement verification — 2026-10-15

The user authorized `call_8924154ad7ed463fac941ea5d438a09b`; that permission
was consumed by `build/h06-r23-uk2ivwf9/` using runtime `4854f84a9`.
Start was received at 02:16:10 UTC on October 16 (October 15 locally). Both
helper and Host independently verified display 4128835 at (1440,-1,4096,4096),
with the physical output unchanged. The asynchronous display-readiness fix
therefore passed its previously failing step on the actual machine.

The next operation failed: unminimize, size and position writes all returned
AX success for destination (1440,188,640,448), but immediate position, size
and minimized reads each returned -25204 (`kAXErrorCannotComplete`) after
100ms. No lifecycle guard failed. This is a native readback failure after
accepted writes, not proof that placement completed. The immediate return
also encountered transient AX errors; the separate recovery process restored
(400,188,640,448). The final source receipt shows neutral keys/buttons, ordinary
Mac configurations match, and local owned processes stopped. Ubuntu confirms
its owned processes stopped, actual desktop restored and ordinary state
preserved. Viewer relay errno 104 / unknown buffered tail and Mac SSH exit
255 are retained; evidence was never armed, so this is not an accepted run.
Ubuntu result archive SHA-256:
`ec8e7f0934b28121ac8eca23dde4448585cc4a2827823ee1c90d0b6a1b4dbee4`.

The new implementation observes the retained source AX window before issuing
placement, then yields to the normal event loop. Native notifications and the
existing Host watchdog request coalesced fresh reads. A temporarily unavailable
AX read remains pending for an absolute two-second verification budget; only
matching AX/WindowServer geometry, known unminimized state and the same exact
source/display authority permit completion. No native write is retried. Lost
identity, permissions, layout, capture, owner or deadline fail closed and retain
the existing recovery transaction. Return cancels queued checks and the native
observer before restoring. The relocation policy now retains its callbacks
across the asynchronous step and revalidates owner, epoch, produced/painted
geometry, display identity/bounds and containment before committing.

`NativePlacementTests.cpp` exercises transient observations, notification
coalescing, no busy retry, absolute deadlines before/after probes, cancellation,
destructive callbacks and stale ownership/geometry/display after yielding.
The existing isolated relocation cases exercise the same refactored policy.
Evidence is retained in `build/placement-verify-vjmz7mbg/`. The final Mac
Host/helper/native targets build, and placement/holding/startup/native-recovery
CTests pass (4/4, 1.22s). Scoped ASan/UBSan passes on Intel Mac/Qt 6.8.3 and actual jack's isolated Ubuntu/Qt 6.10.2 image;
no desktop or input device is exposed to those tests. Prebuilt Qt/system
libraries remain outside sanitizer instrumentation. The first Mac compilation
identified missing CFString bridging and an Apple libc++ function-deduction
issue in the test runner; both were corrected and the failed logs retained.
The new portable target is included in hosted sanitizer checks.

All six hosted jobs for the prior diagnostic commit `60b074160` now pass in
[run 38839385272](https://github.com/JackHars/deskflow/actions/runs/38839385272),
including the previously timed-out evidence suite. This does not explain the
earlier timeout or certify the current placement change. Attempt 24 subsequently launched with fresh permission; see its outcome above. H04/H06 and dependent
hardware acceptance rows remain open.

The placement change is published as `1f489fce0acdb0863e0951f1f07d2be604a97621`.
Its Apple-issued package passes exact-library verification (77 records, no
errors); ZIP SHA-256:
`e18af5dd3668e92086a84e77d7104c1c335d79667ccf36894ade2e0f96905e69`.
Hosted source bundle `38841354603` independently verifies all 1179 tracked
files/modes/blob identities and the exact commit. Source archive SHA-256:
`27557b5898702bad6d2a74e8dd3c9e4a589c739ac0be5154bce679b7e7480a00`.
Foundation run `38841354672` subsequently passed all six jobs; see attempt 24 above.

Fresh attempt 24 is `build/h06-r24-vtm4s8z9/`, frozen to this runtime/package.
Plan validation, Mac permissions/no-conflicting-primary and packaged private
admission pass (11.489s total, native hooks/original input disabled, companion
stopped). Actual jack's unlocked local Wayland session 2 is active; the provider
is disabled until launch. Holding and separate Mac controls are configured,
with no Mac desktop preview or results popup. No test window or native session
was opened during preparation. That permission was subsequently granted and consumed by attempt 24; see above. Do not relaunch these files.

## Roadmap follow-up: absolute startup deadlines — 2026-10-15

The user asked to continue and finish the roadmap. The existing per-launch
physical permission requirement remains in force; no physical test was started.
All six hosted jobs for asynchronous-startup runtime `7453a4c77` pass in
run `38836549724`. The latest checklist at the top of `CHATGPT_HANDOFF.md`
now separates integrated P1–P8 code from the remaining native acceptance.

Review found a separate startup deadline gap: queued process/display readiness
could run before an overdue QTimer and succeed after the declared cutoff. The
gate now checks a monotonic deadline before process-start renewal, helper
readiness and parent verification, and again after the native probe. An early
timer notification rearms only the time remaining; duplicates do not extend it.
Success, timeout and cancellation still complete at most once. No sleep or
polling delay was added, and the five/ten-second limits are unchanged.

`windowshare-holding-startup-tests` runs this production gate on both platforms
without a display, helper or input. It covers late process/reply/display events,
a native probe crossing the cutoff, the exact boundary, early timer delivery,
duplicate notifications, cancellation and completion that destroys its gate.
It is registered in the portable transport CTests and hosted sanitizer job.
Evidence: `build/roadmap-startup-3noptkp0/`. Mac Host/helper targets build;
startup/holding/native-recovery CTests pass (3/3, 1.85s). The new suite also
passes scoped ASan/UBSan on Mac and actual jack's isolated Ubuntu image. Qt and
system libraries are not sanitizer-instrumented. The initial root-directory
CTest invocation found no tests and exited 8; the corrected application test
directory produced the three actual passes. All logs are retained.
The rebuilt local Apple-issued package under this evidence root's `mac-package`
passes its exact-library audit (77 records, no errors); Core archive SHA-256 is
`9cea1103fcd178257edf91d5d9df12c9b361bfd2fc9a17c4d693cad5aaa13dad`.

The change is published as `4854f84a9b345538a198ba8c3e0b1231a981f27f`.
Its hosted source bundle `38838517071` independently verifies all 1176 tracked
files, modes and Git blob/commit identities. Source archive SHA-256:
`7b967ee9f7d46a15c5129a601049c08801e2d765288de5377186ba1a7e903ad3`.
Foundation run `38838517207` passed the new startup test on Mac and Linux,
including the portable sanitizer step. Its Mac job failed at the existing
`windowshare-input-evidence` suite's 15-second CTest deadline, without output
identifying the stalled stage. The same suite with stage diagnostics passes
locally in 0.64s. Each test case now flushes start/completion/failure markers and
elapsed time; an entry marker distinguishes pre-main launch from Qt startup.
The Mac workflow retains CTest logs even on failure. Assertions, workload and
the 15-second test timeout are unchanged; no automatic retry or sleep is added.
This is diagnostic coverage, not a demonstrated fix for the hosted timeout.

The previously prepared attempt 22 uses the earlier binary and is superseded;
do not launch it. Fresh attempt 23 is `build/h06-r23-uk2ivwf9/`, with source
`4854f84a9`, the package above and matching private plans on actual jack.
Validation, Mac permission/conflict checks and packaged admission pass (11.69s,
native input hooks disabled, companion stopped). Jack's local Wayland session 2
is active and its temporary provider is disabled. Holding, separate Mac controls,
no desktop preview and no results popup are selected. That preparation was subsequently launched with fresh permission and failed
at AX placement readback, as recorded in attempt 23 above. Do not reuse its
permission or launch files. Physical H04/H06 remain unaccepted. The full native/OS
queue audit has unresolved upstream bounds, which cannot be inferred from a
passing finite test or the application queue limits.

## Attempt twenty-one: helper/parent display readiness race — 2026-10-15

Permission `call_db5cd7aaaccc4382b58b1683810ac249` was consumed by
`build/h06-r21-czr6qf_t/`, frozen source `c73337c57`. The user reports everything
closed after Start. Start was recorded at 01:19:14 UTC. Setup failed before
Active or recording, then the coordinator performed teardown.

At 01:19:32.162 UTC helper PID 38564 reported display 4128835 active, 4096x4096
at (1440,-1), with the physical layout unchanged. In the same millisecond Host
PID 38525 read that ID as inactive, with wrong identity and 1x1 bounds at (2470,0).
Its first failure was `The parent does not observe an active holding display`;
it immediately stopped the helper. AX reads then returned -25204 during removal.
Native return subsequently succeeded, restoring (400,188,640,448). Final source
input is neutral and the journal is empty. Mac Core/source exited 0, owned
processes stopped and ordinary settings match. Ubuntu desktop restoration,
owned cleanup and ordinary-state preservation pass, but its relay lost TLS
before coordinated stop and controller exited 1; Mac SSH exit 255 remains.
Ubuntu archive SHA-256:
`ee71540c32fb37a20974a14f18000e88c3efc368178204f75cabf55d04cea618`.

The parent blocked its event thread in `waitForStarted`/`waitForReadyRead` and
immediately treated the helper reply as final parent readiness. Apple's header
and [callback documentation](https://developer.apple.com/documentation/coregraphics/cgdisplayreconfigurationcallback)
specify updated display state after reconfiguration callbacks. Qt documents that
[synchronous process waits](https://doc.qt.io/qt-6.8/qprocess.html#synchronous-process-api)
suspend the calling thread. This explains the recorded disagreement; the native
fix still needs a separately approved run.

Production creation is now asynchronous. It subscribes to display-change
completion before starting the helper, parses its bounded reply through queued
process signals, and requires the parent's existing exact display checks.
An initially stale record waits for a notification within the existing readiness
deadline; no polling, sleep or arbitrary delay is added. Helper loss, timeout or
shutdown fences late completion. Verification runs outside the native display
callback, and shutdown unregisters it. Same-mode requests share the owned startup.
The synchronous adapter is reserved for the explicit standalone utility.

Host durably arms recovery before creation and returns to its normal event loop.
It retains actual Viewer paint acknowledgements while waiting, then rechecks the
session, source transaction, process/window identity, physical layout and source
geometry before placement. Drag reservation still requires the original release,
retains its deadline, and rechecks its painted geometry on continuation. Requested
hidden-display setup fails/restores if unavailable; default source-visible mode
remains available when holding is disabled. Combined logs now retain native
`observed` rows, including AX errors previously present only in the raw Core log.

Unattended evidence: `build/h06-startup-ra7c2l2d/`. Host/helper/native targets
build; all 84 coordinator cases pass on Mac (1.78s) and actual jack over SSH
(1.03s). Four Mac Server/coordinator/preflight/native CTests pass (22.92s).
The non-mutating holding test passes (0.50s), including the reproduced stale
parent observation, event-driven readiness, helper loss, cancellation and expiry.
The pure window-drag transaction suite also passes. The final continuation guard
was rebuilt and the native suite passed again (0.76s). The final signed package
is `mac-package-final/` and passes exact-library validation (77 records, no errors).
No physical display or
session was created by these checks. H04/H06 remain unaccepted; ask again
immediately before another physical launch.

Fresh preparation is `build/h06-r22-w5lwq1qf/` (pointer
`build/h06-r22-current.txt`), frozen source commit `7453a4c77`, using the final
signed startup package. Plan validation, Mac permissions/conflict checks and
packaged admission pass; the latter took 10.95s with native input hooks disabled
and its companion stopped. Actual jack session 2 is active/local/Wayland with
the temporary provider disabled. The plan uses holding, separate Mac controls,
no desktop preview and no results popup. Preparation opened no physical window.
There is no launch-permission receipt or session directory yet; obtain fresh
permission immediately before executing its prepared launch once.

## Attempt twenty: holding placement rejected; log-only diagnostics — 2026-10-15

Permission `call_7330e3b7ef1e4099bc00281b601d2ed6` was consumed by
`build/h06-r20-8g88ufzd/` using source `9e8c45903`. The source and separate
controls reached Ready with exact frames, and the human Start at 00:56:34.476 UTC
passed the fresh settled-window gate immediately. The presentation fix therefore
passed this startup check. The test used jack's actual desktop, without a Mac
desktop preview. It never reached Active and no input recorder armed.

The holding helper and parent both verified display 4128835 at
(1440,-1,4096,4096), isolated from unchanged physical outputs. Creation completed
00:56:57.417 UTC. The first failure was Host `source.holding` at 00:56:58.034:
`Holding display placement did not verify; restoring source window`. The source
was observed at (1440,190,640,448); controls stayed (480,280,480,268). The old log
does not identify which placement predicate failed. Initial native return also
failed and retained recovery intent; the later private-channel loss is not the
first error. Do not infer a specific AX/readback cause from this evidence.

Final cleanup restored the original source frame (400,190,640,448), with zero
held keys/buttons and an empty journal. Source/Core exited 0; Host/helper stopped;
ordinary Mac settings match. Ubuntu stopped owned processes, restored its desktop
and preserved ordinary state, but controller exit 1 and the relay's uncoordinated
TLS disconnect remain failures. The Mac SSH exit 255 and unarmed evidence are
retained. Collected Ubuntu archive SHA-256:
`b7b57c22d5de41d9b73969dbd04d977df737ddfa93720761633514f1ed8b1d8b`.

The user did not see the results window and explicitly requested diagnostics for
the agent, without a popup. Automatic report launch is removed; flushed console
and JSONL diagnostics remain. The manual report utility is not part of teardown.
Added native placement/restore traces: each AX read/write begins and returns with
its error code; verification records requested/observed frames and minimization;
holding checks name the lost identity, geometry, lifecycle or capture condition.
The relocation transaction reports its failing stage, distinguishing journaling,
native move, post-move identity and containment. Existing admission and rollback
checks remain strict; no sleeps, retries or deadline extensions were added.

Unattended evidence is `build/h06-placement-e2j2nzqs/`. The rebuilt Host/source and
all 83 coordinator cases pass on Mac (1.69s) and actual jack over SSH (1.00s).
All four Mac Server/coordinator/preflight/native
CTests pass (23.86s), and the default holding test passes (0.51s), including
failure-stage assertions in its pure relocation fixtures. That default test does
not create a display or move a window. The new signed Core/Host/helper package
passes exact-library validation (77 records, no errors); binary hashes are in
`validation-summary.json`. H04/H06 remain physically unaccepted.
Ask for fresh permission immediately before any next physical launch.

Fresh preparation is `build/h06-r21-czr6qf_t/` (pointer
`build/h06-r21-current.txt`), frozen source commit `c73337c57`. It uses the new
signed diagnostic bundle, separate Mac controls, actual jack session 2 and no
Mac desktop preview or results popup. Both plans and permission/conflict checks
pass. Packaged admission passes in 11.61s with native input hooks disabled and
its companion stopped. Ubuntu's local Wayland session is active and the temporary
provider remains disabled. Preparation did not open a physical test window.
The separately approved attempt twenty-one and its consumed permission are
recorded above. Neither attempt's approval may be reused.

## Attempt nineteen: pre-Start presentation failure — 2026-10-15

Permission `call_98ec79d8d6f644d687e004f5262445e3` was consumed by
`build/h06-r19-209et7ms/`. Diagnostics caught the first failure at
00:28:35.860 UTC, before Ready/Start or either machine's input session. An
initial exact source frame (400,190,640,448) became (399,190,642,448); controls
changed from (480,280,480,268) to (479,280,482,268). The fixture's composite
visibility flags became false because geometry no longer matched Qt. The old
receipt did not retain the separate native visibility flags. This is consistent
with the opening animation, not evidence that the user hid or moved a window.
The coordinator incorrectly treated that pre-Start transform as a running-test
control failure. No holding display was created and no recorder armed.

Source exited 0 with its original frame and zero held keys/buttons. Owned Mac
test processes stopped, ordinary settings match, and the independent report was
launched. Ubuntu stopped before native startup, with controller exit 0, provider
unchanged/disabled and ordinary state preserved. The Mac SSH exit 255 remains
in the result; it is distinct from the successful Ubuntu cleanup receipt.
Collected Ubuntu archive SHA-256:
`00e631c65e2d3a879b0142237d471afbeffb0344c72458ad711dba0136d074c5`.

The fixture now disables its own AppKit presentation animations before showing
either window, without changing desktop preferences or adding sleeps. Receipts
separately retain Qt/native visibility, minimization, on-screen status, expected
frames and geometry agreement. Before Start the coordinator keeps exact window
identity checks while allowing presentation to settle. After Start is requested,
a fresh exact visible receipt is required within the existing ten-second wait
before input can start; running sessions still require visible stationary controls.

Validation is `build/h06-presentation-1xwwf524/`: the Mac source builds, and all
83 coordinator cases pass on Mac (1.70s) and actual jack over SSH (0.98s). Four
new regressions cover the captured transform, identity changes, strict active
checks and expiry without readiness, with sleep calls forbidden. The Core/Host
package is unchanged from attempt nineteen. Native animation suppression and
H04/H06 physical acceptance still require an attended run. The consumed approval
does not authorize a retry; ask again immediately before the next physical launch.

Fresh preparation is `build/h06-r20-8g88ufzd/` (pointer
`build/h06-r20-current.txt`), source commit `9e8c45903`. It freezes the rebuilt
source with the unchanged signed Core/Host bundle and fresh private identities.
Both plans validate; permissions/conflict checks pass. The inert packaged
admission check passes in 11.50s, with its companion stopped. Actual jack
session 2 is active, local and Wayland, with the provider disabled. The prepared
launch uses holding/separate controls and no Mac desktop preview. No physical
window or input session was started by preparation. Its separately approved,
consumed attempt twenty is recorded above; neither approval may be reused.

## Attempt eighteen and persistent diagnostics — 2026-10-15

Permission `call_0c4a5ca69ff04dc5a8b0a97591b9851e` was consumed by
`build/h06-r18-i25moopp/`. Explicit holding/separate controls were enabled on
jack's actual desktop, without a Mac preview. The first substantive error was
`The helper's holding display failed independent geometry verification`, followed
by `Source-visible fallback changed during holding-display creation`. At
19:52:34 Core lost the trusted companion channel; the coordinator only reported
`Exact Core or authenticated companion process changed`. No recorder armed.
This does not establish physical H04/H06 acceptance or the specific native
predicate that rejected the display.

Source window 11180 moved from (400,190,640,448) to (9,50,640,448); separate
controls 11181 remained visible and stationary. The old result's
`sourceRelocationObserved:true` only meant a changed frame and is not hiding
proof. Preserve the failed result. The final source has zero held keys/buttons
but the wrong frame. The journal is empty; source restoration was not accepted.
Mac Core/source exited 0, the companion stopped, and ordinary settings match.
Ubuntu reports owned processes stopped, desktop restored and ordinary state
preserved; Core/controller exit 1 and relay/IPC cleanup failures remain recorded.
Collected Ubuntu archive SHA-256:
`df0fb1041c01d2aedc250215c8f98e8f8e0f7db33c8b56f8acf7ed06a722da53`.

The user now explicitly requires diagnostic prints at every step, without sleep
statements, before another physical launch. Added flushed, timestamped Mac and
Ubuntu stage logs, readiness changes, exact process-loss labels, first-failure
retention and native capture/holding/restore diagnostics. Helper stderr now
reaches the Host log; failed display verification names its precise predicate.
The Mac wait loop uses receipt-change notifications with deadline checks, with
no artificial visibility delays. This revision launched a separate read-only
report after teardown; the user's later log-only request supersedes that behavior
as recorded above. It never constructed a source recorder or input filter.

H06 now requires the committed Host placement and matching observed source
frame wholly on the holding display, outside the physical displays, before
showing Active. Also fixed the recovery bookkeeping gap: a display-creation
attempt must durably arm restoration before it can rearrange the source, even
if no explicit window move or Remote commit succeeds. Native-free rollback
coverage simulates the observed (9,50) displacement and failed creation.

Validation/preparation evidence is `build/physical-diagnostics-fxxw1od6/`.
Mac coordinator tests: 79 pass; Linux lifecycle tests: 31 pass; desktop tests:
8 pass/1 Linux-only skip on Mac, all 9 pass on actual jack. All three Python
suites also pass over SSH on jack without a native session. The persistent Qt
report tests pass offscreen on Mac and in the private Linux build container.
Mac Server/coordinator/preflight CTests pass (22.23s). This validates diagnostics,
not physical display creation or input delivery. The native recovery regression
also passes (0.54s), including the failed display-creation rollback. Collected
Linux diagnostics-validation archive SHA-256:
`c8f5567b67a08dd9d861e8910bcdc80d9f7b1ae64969bd3c86c11270be03fb93`.
No new physical test has been
launched by this work; obtain fresh permission immediately before the next one.

Fresh preparation is `build/h06-r19-209et7ms/` (pointer
`build/h06-r19-current.txt`), using source commit `28def6f59` and the new signed
package in `build/physical-diagnostics-fxxw1od6/mac-package/`. Both plans validate,
Mac permissions/conflict checks pass, and jack's actual unlocked Wayland session
2 is selected with the provider still disabled. The packaged admission probe
passes in 12.03s with native hooks off and its companion stopped. Preparation
opened no physical window or input session. A new launch-permission question is
pending; `prepared-launch-argv.json` is ready but has not been executed. The
previous approval belongs only to consumed attempt eighteen.

## Seamless test placement and handoff diagnosis — 2026-10-15

The user requests the original Mac window hidden while it is used on jack's
Ubuntu monitor. Prepare the next attended run in explicit H06 holding-display
mode, with separate persistent Mac controls, actual-desktop mode and no Mac
desktop preview. This is a change to the test setup; the existing holding-display
implementation still needs physical hiding/restoration acceptance. Ask for fresh
permission immediately before launch.

The source instructions now require moving the Ubuntu pointer into the shared
text box and clicking/releasing twice before typing. The former instructions
typed first and did not establish the mapped pointer position or a fresh button
boundary. This correction does not prove the cause of attempt seventeen's
missing readiness. Both of its sealed cutoff attempts were canceled; the many
busy viewport messages were background observations, not proof of failed input
drain. Preserve that uncertainty.

Added real production GNOME coverage for focused `DICS` acquisition after the
existing first-click `DIPM` promotion and exact cleanup. It passes with the
previous executables and the container's supported Mutter build. Rebuilt Linux
Core/Viewer then pass with read-only mounts of the exact Mutter/clutter libraries
used by jack's desktop, at Qt scales 1 and 1.25. All native runs use isolated
container compositors, private buses and synthetic protocol correlation; none
uses the user's desktop or establishes physical Mac input acceptance. Both new
direct cutoffs obtain exact mapped seals and acknowledge cleanup; Core exits 0.

Core now logs the first terminal capture/preparation failure, and the Linux
backend retains the provider's bounded drain/map refusal reason in its log.
These diagnostics do not change admission checks or extend deadlines. Evidence:
`build/h04-handoff-zzbhm4sr/`. Mac Server/coordinator/preflight CTests pass (23.31s);
the same Linux suites pass (15.91s); the Python wire self-tests pass (9 cases).
Rebuilt Core/Host and the new signed Mac package pass packaging checks. The
first packaging invocation lacked `MACDEPLOYQT` and exited before creating an
output; the retry explicitly selected the build's Qt 6.8.3 deployment tool.
Collected Linux validation archive SHA-256:
`71b0e27cb66a7b6be8420b21f3435327822d35f5940cab7a07d3d809defa5eab`.

H04 input readiness and H06 hiding/restoration remain unaccepted. This was the
preparation record; the separately approved attempt eighteen is recorded above.

Fresh preparation is `build/h06-r18-i25moopp/` (pointer
`build/h06-r18-current.txt`), source commit `22d7695c0`. It freezes the rebuilt
signed bundle and revised source, fresh private identities, explicit holding
display/separate controls, no preview, and jack's existing unlocked session 2.
Plan validation, permissions and conflict checks pass. The packaged admission
probe passes in 10.81s with native input hooks disabled and its companion stopped.
`prepared-launch-argv.json` remains reviewable; the historical preparation record
is followed by the consumed launch-permission receipt and failed attempt above.
Do not reuse that approval or replay the already consumed preparation.

## Actual-desktop attempt seventeen and display clarification — 2026-10-15

Fresh explicit permission launched `build/h04-r17-0zwgwnaz/` after the verified
storage cleanup. It reached Active on jack's actual monitor. The user reports
seeing the Mac test window, its sharing indicator, and the same contents inside
an Ubuntu window. They require the original to be hidden on the Mac during
normal seamless use; keep that requirement. They also saw a brief window
underneath during closure; its identity is not established by these receipts.

This H04 run intentionally configured `holdingDisplay:false`,
`desktopPreview:false` and `separateControls:false`. The Mac source was PID 10023,
window 11151, with no separate control window; the Host connection UI was
headless. Ubuntu ran the Viewer on the existing desktop. No Ubuntu preview was
launched on the Mac. H04 leaves the original visible to diagnose input; the
separate H06 holding-display mode must prove hiding and restoration. This
explains the duplicate content, not the input-readiness failure. The actual-
desktop instructions now state that distinction before launch.

The test ended with `Record was requested, but matching input readiness was
unavailable before the recording cutoff`. The Record request exists, but no
recorder armed: Core 0/926 and Host 0/858 ready rows, native 987/987. All three
ended denied, zero measured rows, loss 0, drained true. This is not physical
original-input acceptance. Unlike attempt sixteen, the run reached its
recording cutoff; the previous capture interruption did not end this run.

Mac Core/source exited 0 and the companion stopped. Local ports are released
and ordinary configuration hashes match. The source frame returned to
(400,190,640,448), but its final receipt retains one held key, so input restoration
is not accepted. Preserve the unarmed-evidence and source-restoration cleanup
errors and the Mac coordinator's Linux SSH exit 255. The copied Ubuntu results
separately report Core/controller exit 0, owned processes stopped, desktop
restored and ordinary state preserved. Collected Ubuntu evidence SHA-256:
`730779f8a96ee11a26f125199034ea5e90db69470b602695c22da34a77bda827`.

Human observations, cleanup checks and the unchanged failed result are retained
with the run. Diagnose missing Core/Host input readiness before asking the user
to repeat this diagnostic. No next attempt is prepared or authorized; obtain
fresh permission immediately before any physical launch. H04/P5 remains open.

## Actual-desktop attempt sixteen — 2026-10-15

Fresh permission launched `build/h04-r16-awg5w2ql/` from commit `50b61497b`.
The user clicked Start and confirmed the shared window appeared on jack's
actual Ubuntu monitor. The previous mapped-target error did not recur.
This run failed before Record; it does not establish physical input acceptance.

At 18:35:49.402, macOS replayd logged `cacheDeleteUrgencyHigh`, then
`handleMemoryWarningForCurrentActiveSession` and `stopAllStreamsWithError`.
The Host received ScreenCaptureKit `SCStreamErrorSystemStoppedStream` (-3821)
at 18:35:49.408, before its own stop request. At 18:36:00.760 Core closed the
private channel with `Trusted companion private channel lost authority`.
The coordinator then reported its generic changed-process failure. The capture
service's urgent purge is the observed trigger; the later Host responsiveness
failure remains a separate recovery concern. Do not mislabel this as the
previous fixed handoff race or infer that the user stopped recording.

The Mac had only 8.2 GiB available on its 932 GiB APFS container during diagnosis;
swap usage was zero. The scoped Host and replayd logs are retained with the run.
Mac Core/source exited 0, the companion stopped, and ordinary settings match.
The source retained one held key and its final frame (100,160,640,448) differed
from the initial frame (400,190,640,448). Restoration is not accepted.
Core/native ended denied with zero measured rows, loss 0 and drained true;
Host has no end row. Readiness was Core 0/323, Host 0/234, native 610/610.
Ubuntu reports owned processes stopped, desktop restored and ordinary state
preserved, with Core/controller exit 1. The archived Ubuntu evidence SHA-256 is
`edac8375b48d5192e48012087fd401c7d26346adbaf91298722c7f2d654262bf`.

The user clarified that they tried to press Record but the window shut down
before they could reach it. This agrees with the absent recording request;
it does not establish that input stopped first.

With explicit cleanup permission, all 41 historical `build/packages/` entries
were archived to jack and independently verified before removing the Mac copies.
The 3,346 regular files, 717 symlinks, 1,740 directories and 5,840 extended
attributes match contents, permissions, ownership and timestamps; the archive
also preserves AppleDouble metadata. A second source inventory matched before
removal. Current builds, failed-run records and ordinary settings remain;
the current package/source and retained result-file hashes match.

The archive is on `jack@192.168.1.12` at
`/home/jack/src/deskflow-h04-20261015-zgta6i4f/historical-mac-packages-20261015-7dcdf12a/packages.tar.gz`.
SHA-256: `3b60b8a3cae9a93d6160f4866a39d59efc63d5ecf3c86682b0232831311ef735`.
It is 2,975,406,080 bytes, with its manifest, verifier, verification receipt,
cleanup receipt and restore instructions beside it. Local receipts and restore
instructions are in `build/h04-r16-awg5w2ql/`; `build/packages/ARCHIVE_RESTORE.md`
points to the same backup. Actual free-space gain was 6,008,745,984 bytes
(5.60 GiB), increasing available space from 8.20 to 13.80 GiB. This reduces
storage pressure but does not establish capture recovery or physical acceptance.

No physical test was launched by cleanup. Historical attempt-seventeen
preparation at `build/h04-r17-0zwgwnaz/` used the unchanged current binaries,
fresh private identities and jack's actual unlocked desktop session 2. Both
plans validated; Mac permissions and conflict checks passed, and the packaged
Core/Host admission probe passed in 20.16 seconds with native input hooks off.
Its separately approved launch and failed result are recorded above.

## Actual-desktop attempt fifteen — 2026-10-15

Fresh permission launched `build/h04-r15-b6shuoux/`. This attempt reached
Active on jack's actual GNOME desktop. The user saw the Ubuntu pointer and
the typed characters in both the source and shared window, then reported
that only the Ubuntu pointer worked after typing and holding x.

At 18:03:46 the Mac Core closed its private channel with
`Mapped target does not name the pending full-input lease`; the Host and
Ubuntu connection then closed. No recorder armed and no Record request was
observed. Core/Host readiness remained zero. Mac Core/source exited 0 and
the companion stopped; ordinary configuration hashes match. The source's
final receipt retained one held key, so input restoration is not established.
Ubuntu reports owned processes stopped, actual desktop restored and ordinary
state preserved, but Core/controller exit 1. Preserve all failed receipts,
including the coordinator's Linux SSH exit 255. Collected Ubuntu archive
SHA-256: `5590214dfde941c7edd5454b288fd533050d653f57e1506876ffdbfec8bc9c6e`.

An unattended real-private-channel regression reproduces the identical error
by delivering an exact mapped-target reply after Core cancels its offer.
Mapped targets now use the existing ordered preflight queue and retirement
checks. Valid late replies cannot close or revive an owner; current targets
still require the exact full-input candidate, one target, and all later
authorization checks. Changed, future, malformed and duplicate current
replies fail closed. Validation evidence: `build/h04-late-target-94wwqjc4/`.
Mac private-channel/preflight suites pass (18.41/17.97 seconds), as do all
29 coordinator cases after the fixture waits for both owners' release.
The matching Ubuntu suites pass in 29.47 seconds; Mac capture cases are
explicitly skipped there. Ubuntu ASan/UBSan preflight passes in 18.54 seconds.
Core/Host builds and the separate Apple-signed package's inert admission and
bootstrap checks pass. These checks neither use the user's desktop nor inject
physical input. Both rebuilt Linux executables are retained with the evidence.

Historical actual-desktop preparation: `build/h04-r16-awg5w2ql/`, using the
rebuilt binaries and fresh identities. Its separately approved launch is
recorded above. Physical H04/P5 acceptance remains open.

## Actual-desktop attempt fourteen — 2026-10-15

After fresh explicit permission, attempt fourteen (`build/h04-r14-el4lnrkj/`)
opened Ready on the Mac, then closed immediately after Start. The user saw no
window on Ubuntu. The Mac preparation incorrectly made `tls/trusted-clients`
a directory; replacing it with the peer-pin file failed before Core/Host or
Ubuntu's native session started. This was a preparation mistake, not evidence
of an input-delivery or Ubuntu rendering failure.

Ubuntu completed its prepared-only stop with controller exit 0, all owned
processes stopped, desktop restored and ordinary state preserved. The provider
was never enabled. The Mac source exited 0 and restored its geometry/input;
ordinary configuration hashes match. Retain the original failed result,
including its Linux SSH exit 255 and recorder-stop timeout. The collected
Ubuntu archive SHA-256 is
`80644ab79f13b72e522572655037b70e57424ffbc6da7cf36f56270efa2a2e1c`.

Plan validation now rejects a directory, link, FIFO or stale staging path at
the private trust-list location. The atomic public-pin write completes before
opening the source window and its hash is rechecked after Start. Regression
coverage verifies actual file creation and exact contents without native apps
or SSH. All 72 Mac coordinator tests pass on Mac and home Ubuntu.
Historical preparation: `build/h04-r15-b6shuoux/`; its separately approved
launch and failed result are recorded above. H04/P5 remains unaccepted.

## Actual-desktop attempt thirteen — 2026-10-15

The user explicitly approved attempt thirteen (`build/h04-r13-3qf8w10p/`).
It refused startup before opening any test window or starting the experimental
Core/Host because the ordinary Mac Core was already running. Ubuntu cleanup
reports no provider activation, all owned processes stopped, desktop restored
and ordinary state preserved. No physical input was tested.

The original failed result also contains a misleading configuration-change
error: the old coordinator checked for another Core before capturing the
configuration baseline. Keep that result unchanged. The coordinator now takes
the baseline first and does not claim a comparison when that read fails. Its
read-only conflict check is available during preparation and is repeated at
launch and Start. Both unchanged and genuinely edited configurations are
covered; all 70 Mac coordinator regressions pass on the Mac and home Ubuntu.

The user reports stopping the regular Mac session; the ordinary Ubuntu Core
is also absent. Linux now accepts an explicit `--no-ordinary-core` preparation
and rechecks that absence at startup and cleanup. Its 30 coordinator regressions
pass on both machines. The earlier preparation failure against the stale Ubuntu
PID remains in the evidence; it started no native session. Fresh actual-desktop
preparation is in `build/h04-r14-el4lnrkj/`; it is not launch permission. Check
current ordinary processes and the actual desktop again, then request fresh
permission immediately before this next physical launch. H04/P5 remains open.

## Actual Ubuntu monitor test preparation — 2026-10-15

The user explicitly requires physical testing on the active desktop of host
`jack`, visible on the monitor behind the MacBook. Do not substitute another
headless desktop or the Mac preview. Ask for fresh permission immediately
before every physical launch, even after the user signs back in or answers Ready.

Attempt twelve (`build/h04-r12-kh2mcwyo/`) reached Active with its preview visible,
then ended when the preview process exited 0. No recording armed: Core/Host/native
ended denied, loss 0, zero measured rows, drained true. The final source receipt
has no held keys/buttons and restored geometry; this is not original-delivery
acceptance. Mac Core/source exited 0 and the Host stopped. The copied Ubuntu
result records Core/container exit 0, private IPC stop, drained relays and
ordinary state preserved; the Mac coordinator still records Linux exit 255 and
its unarmed-evidence cleanup failure. Preserve that failed result. The user
then signed back in (the observed Ubuntu boot ID also changed); older ordinary
PID audits cannot be repeated across that reboot. Ubuntu archive SHA-256:
`5f7eb794bde48f5f4d7a79b55b2ca9444366aeb6f0f3b6c7688bbfb5f4f31d4d`.

New explicit Mac `--linux-desktop` / Linux `--desktop-session` plans use the
existing physical Wayland session. The installed GNOME provider is unchanged,
recognized after sign-in, and disabled. Start enables it temporarily; Core and
Viewer use private settings, identities and IPC. No new compositor or Mac preview
starts. Desktop identity changes refuse startup. Durable rollback restores the
test extension's prior membership without discarding unrelated user edits, and
exact PID/birth/executable checks plus pidfds bound fallback process cleanup.
The Viewer exec wrapper preserves Mutter's native process and inherited FDs.

Evidence: `build/h04-desktop-f9vl8zk0/`. Read-only preflight identifies the actual
unlocked local GNOME desktop and unchanged installed provider. Home Ubuntu's
9 desktop, 28 Linux coordinator and 68 Mac coordinator regressions pass, including
real exec/PID/FD preservation with a plain Python child. The Mac checks also
pass; its Linux-only exec check is skipped. These tests do not open a physical
window, enable the actual provider or deliver physical input. The production
Mac and Linux binaries are reused from their previously verified frozen builds.

Historical actual-desktop preparation: `build/h04-r13-3qf8w10p/`, with fresh
private identities and matching plans. Its separately approved failed launch
is recorded above. The sign-in reply itself did not authorize a physical launch.
H04/P5 and physical roadmap acceptance remain open.
See [the actual-desktop procedure](deploy/windowshare/physical-desktop.md).

## Attended-test permission — current user instruction, 2026-10-15

The user explicitly authorized attempt sixteen above. Ask for fresh permission immediately before
executing the next physical test, and wait for an affirmative reply before
opening its test window or starting its physical session. Earlier Start/Ready
answers do not authorize another attempt. Unattended builds, unit tests and
isolated headless GNOME fixtures can continue. Prepare the tested artifacts and
plans before asking. Do not ask while the user is known to be away.

## Attempt eleven: invisible Ubuntu test desktop and unarmed Record request — 2026-10-15

After fresh launch permission, the user reached priming and reported that typing
worked but clicking did not. They returned left to the Mac to click Record;
Record was requested, but Core/Host never published a ready owner and no
recorder armed. The coordinator ended at its recording/safe-return cutoff.
Its old "No visible Record request" message was inaccurate in this case.

The user then clarified that no test window was visible on Ubuntu and they had
not known one was expected. The harness runs a **headless, isolated GNOME
session**, so its Viewer is absent from the ordinary Ubuntu monitor. The Mac
source remains visible, but it is not the desktop receiving the routed pointer.
Receipts show remote clicks reaching labels/widgets rather than Record. This
explains why the instructions were unusable; it does not establish that the
pointer transformation or original-input acquisition is correct.

Evidence: `build/h04-r11-b4be7iux/`, including both observations and cleanup
verification. Core/Host readiness counts were 0/940 and 0/850; native was
1005/1005. All ended denied/unarmed with loss 0 and zero measured rows.
The final receipt retains 2414 Qt events, the original frame, and an unmatched
routing Right-key press. Neutral input remains unverified. Mac Core/source
exited 0; exact Host, container and sockets are gone. Ordinary settings and
processes were preserved. The coordinator retains Linux exit 255, while the
copied remote result reports completed, container/Core exit 0, private IPC stop
and fully drained relays. Preserve both; do not rewrite the failed run.
Ubuntu archive SHA-256:
`14a54e71c3c950ed7391d1c0ffc54a4d967d7d951bd2c2d79bd151ea403c6243`.

The repair adds an explicitly planned, view-only preview of the real
isolated Ubuntu desktop and cursor on the Mac. It has a separate bounded SSH
image stream and observer process; it never injects input or grants authority.
Start cannot become Active without live visible preview receipts. Frozen,
closed or hidden previews abort; recording completion closes the preview so
the source's local Stop is visible again. Both plans must opt in. The ordinary
Ubuntu desktop is not used. Return in the existing source text box requests
the same one-shot Record as its button without returning left. New readiness
rows identify missing owners, active capture and blocked queues; arm conditions
and session deadlines remain unchanged. Evidence: `build/h04-readiness-c_zn8we0/`.
Validation passed: Mac recorder (0.76 s) and offscreen real-PNG renderer
(4.24 s); home Ubuntu recorder/preview/private-channel suites (17.31 s) and
ASan/UBSan recorder/preview (3.12 s); 66 Mac coordinator, 23 Linux coordinator
and 7 preview protocol checks. The actual isolated GNOME cursor-pixel fixture
passes in 3.72 s, uses only synthetic fixture input, and stops every owned
process. A later receiver regression also passes directly on home Ubuntu and
rejects a frozen renderer despite fresh network images. It is not a physical
input or end-to-end original-delivery pass. Mac Core/Host/source build; the
Apple-issued exact-library package and inert admission/bootstrap check pass.
Formatting, whitespace and REUSE pass. Linux temporary builds/containers are
gone. Earlier missing-target (before CMake reconfiguration) and C++ name-lookup
compile failures remain in the build evidence; both were repaired.

Ubuntu test archive SHA-256:
`b3ec11b0016f15ca9045b09af1b36b81d0e15a56346d53732d933672c070c24b`.
Mac package ZIP SHA-256:
`bd0b549cce87c10eef7fee167e8f6d21b324d81927a96cf2edbeaaf17332b5ff`.
Historical attempt twelve preparation: `build/h04-r12-kh2mcwyo/`, with the
preview required in both plans. Its subsequent approved run is recorded above.
It does not satisfy the user's actual-monitor requirement. H04/P5 remains unaccepted.

## Attempt ten: native stage invalidation retired the Linux session — 2026-10-15

The user explicitly approved this launch immediately beforehand. The run reached
Active and priming but no Record request or arm. They reported "scrolling i think"
just before closure. Ubuntu Core rejected request 29 with `GNOME stage observation
substituted its exact native binding`, then closed the viewport. The relay EOF
and coordinator `linux exited early: 1` followed. The prior Mac heartbeat stall
did not recur in this short run: maximum readiness gaps were 290 ms Host,
111 ms Core and 147 ms source. This is not a complete physical-input pass.

Evidence: `build/h04-r10-f_dfkh1x/`, including fresh permission, human observation,
copied Ubuntu results and separate cleanup verification. All three recorders
ended denied/unarmed, loss 0, zero measured rows. The final collected receipt
contains 889 Qt and 363 native events without gaps; the routing Right key still
lacks an observed release. Source geometry returned to its original frame;
neutral input remains unverified. Mac Core/source exited 0 and the exact Host,
owned Ubuntu container and forwarding sockets are absent. Ordinary processes
and settings were preserved. Ubuntu's failed IPC stop and terminated Core remain
recorded. H04/P5 remains unaccepted.

The real GNOME provider returns its newer native revision in a negative stage
reply when state changes during an observation. The client previously required
the old revision before checking whether the reply was a denial, turning that
normal invalidation into a terminal channel error. It now rejects the observation
while allowing a fresh request on the authenticated channel. Successful replies
still require the exact old revision; request, peer, point, schema and deadline
checks remain mandatory for every reply. Empty or contradictory reasons and
substituted bindings remain terminal.

Evidence: `build/h04-stage-f1bz5dzq/`. The new private D-Bus regression reproduces
the old shutdown on Mac and home Ubuntu. It covers invalidation with and without
the accompanying signal, rejects the old observation and requires a new revision
for recovery. Malformed denial and successful-substitution cases remain terminal.
Mac's full provider-client suite passes in 6.39 s; Ubuntu's normal and ASan/UBSan
provider-client suites pass in 4.28 s / 6.13 s. The production Linux Core builds;
isolated GNOME 50.1/QtWayland passes in 12.14 s with a clean production-client
stop, no original-delivery authority and no use of the user's desktop. The
temporary build/container are gone. Formatting, whitespace and full REUSE pass.
The source archive SHA-256 is
`2392590054d64fa3643bece3c0ead18d4444ed21b8b36087d5319fd810e65114`;
the rebuilt Linux Core SHA-256 is
`6e276b212d45ed2adf34b132165f3327535fc81d3fea52c111eff966f7ea9249`.
Attempt eleven used that Core with the unchanged tested `3e4690e99` Mac
package/Ubuntu Viewer and `14cd5514f` source fixture; its result is above.

## Attempt nine: Host event-loop stall during physical input — 2026-10-15

The user explicitly approved launch immediately beforehand, clicked Start,
confirmed Active, and clicked Record at 13:23:13 local. They reported repeating
the inputs before the window closed. Neither Core nor Host ever published a
ready candidate, so no recorder armed. Host reported `Local input heartbeat
expired (elapsed=3239ms, budget=2500ms)`; Core reported private-channel authority
loss at 13:23:40. Readiness timestamps show a 3229 ms Host gap while Core and
source stayed responsive (largest gaps 189 ms and 159 ms respectively).
This identifies a Host event-loop stall, not its exact native stack.

Evidence: `build/h04-r9-0q_whncg/`, with launch permission, human observation and
copied Ubuntu results. All three recorders ended denied/unarmed, loss 0 and
zero measured rows. All 1736 Qt receipt events were retained without sequence
gaps; the routing Right key still lacks an observed release. Source geometry
was restored, but neutral input remains unverified. Mac Core/source exited 0,
the exact Host stopped, and the owned Ubuntu container/sockets are absent.
Ordinary settings/processes were preserved. Ubuntu's failed private IPC stop,
relay EOF and terminated Core remain recorded. H04/P5 is not accepted.

The TLS reader could invoke hundreds of native input callbacks in one 64 KiB
read before servicing other channels. It now consumes one decoder fragment at
a time and yields after 32 packets, 8 ms of work or 64 KiB, without adding a
decoded-packet queue. Every continuation rechecks existing authentication and
deadlines. A single slow callback cannot be preempted; this repair does not
establish that every possible native stall or missing readiness is resolved.

Evidence: `build/h04-dispatch-_wdtca1j/`. A real TLS input-burst regression fails
against the old code and passes after the repair, including slow callbacks,
ordered lossless continuation and expiry during the yield. Mac transport,
Bridge and viewport suites pass (2.31 s / 12.63 s / 2.63 s). Home Ubuntu passes
the same suites (16.92 s total), core ASan/UBSan (0.17 s), and isolated real
GNOME/QtWayland (12.70 s, clean client stop, no original-delivery authority).
Temporary Linux build/container cleanup, formatting and full REUSE pass.
The updated Mac Core/Host builds and Apple-issued exact-library package verify;
the packaged admission/bootstrap smoke passes with native input disabled.
Package ZIP SHA-256:
`78959dd0b6a95ac9ca75c109d72b00096937c0bd2fc05feda0c58aa31d1e9ada`.
Attempt ten used the repaired Mac package and repaired Ubuntu Viewer under
`build/h04-r10-f_dfkh1x/`; its failed physical result is recorded above.

## Attempt eight: visible input, then Ubuntu viewport disconnect — 2026-10-15

The user clicked Start and confirmed that typed characters appeared after
the right-routing hotkey. The Ready fix survived more than three minutes of
waiting and the session reached Active. It failed before Record/arm when the
Ubuntu viewport channel closed during preparation inventory request 68.
The upstream reason was discarded by the Viewer endpoint/Bridge shutdown path.
This does not diagnose attempt four's separate Mac companion disconnect.

Evidence is retained in `build/h04-r8-5bjw64xh/`, including copied Ubuntu logs
and the direct human observation. Every recorder ended unarmed with loss 0;
the exact Mac Core/Host/source and owned Linux processes stopped. Ordinary
configurations and processes were preserved. Linux cleanup reports its failed
private IPC stop and relay EOF; the source receipt retains the routing Right
key without a matching observed release, so neutral-state restoration is not
verified. No physical acceptance is claimed.

The diagnostic change retains a bounded first close reason across
reentrant endpoint cleanup and logs it before Bridge shutdown can discard it.
Mac viewport/controller suites pass (2.73 s / 9.50 s). Home Ubuntu Debug build
and both suites pass (1.19 s / 12.12 s); the isolated GNOME 50.1/QtWayland
fixture passes (12.40 s), with clean production-client stop. It uses synthetic
fixture input, reports no original-delivery authority and does not use the
user's desktop. The diagnostic container and temporary build tree are gone.
Formatting, whitespace and full REUSE pass. These checks do not identify or
repair the initiating physical disconnect; no attended retry is authorized.

Evidence: `build/h04-viewport-elrcr6hw/`, including copied Ubuntu logs/results.
Source archive SHA-256:
`a20d3668040e43a9b26c46d4c3af6bc4b2a4bc4669f6a33298cd0fc3e705968b`.
Earlier build attempts are retained: the first omitted the optional fixture
target, and the second Release build stopped on GCC `maybe-uninitialized` in
unchanged `GnomeViewerFixture.cpp`. The passing Debug run matches CI's build
configuration; it does not establish that the Release fixture builds.
Hosted foundation/source-bundle runs on preceding commit `14cd5514f` also
passed (`38772970872` / `38772971003`); they do not cover these new diagnostics.

## Ready-window lifetime and visibility repair — 2026-10-15

Attempt seven (`build/h04-r7-ptwmxdfb/session-1`) ended before any Start click:
the native evidence recorder's 180-second preparation watchdog expired while
the window offered a 300-second Ready wait. The user could not see the window
after it closed. Native evidence retains Deadline loss 9 and no recorded rows.
All owned processes stopped; exact remote cleanup preserved the ordinary apps.
The old all-role evidence-cleanup timeout is retained in that failed result.

The disposable native fixture now explicitly uses a one-shot 600-second
preparation bound, matching the outer coordinator limit and covering Ready,
startup and priming. Core/Host still use 180 seconds. Recording duration,
explicit Start/Record requirements and authority checks are unchanged. The
source requests foreground once and publishes its own WindowServer visibility;
production readiness now requires that visibility as well as settled geometry.
Pre-Start cancellation waits only for the launched native recorder, verifies
its clean unarmed end and absence of other roles, and never claims a recording.

The actual runtime deadline regression passes on Mac (0.78 s) and home Ubuntu
ASan/UBSan; all 63 Python coordinator checks pass. Source builds and is signed.
Evidence is under `build/h04-ready-u_nmzsoi/`; fresh attempt eight is prepared
under `build/h04-r8-5bjw64xh/`. H04 and physical roadmap acceptance remain open.

## Concurrent preparation Event accounting — 2026-10-15

Attempt six (`build/h04-r6-e1zqbfc2/session-1`) received the user's Start and
passed packaged/real code admission, then stopped during Linux startup with
Core `Contention`, site `queue-mutation`, queue `event-all-outstanding`.
No recorder armed. Core/source exited 0, the exact Host stopped, and Linux
completed startup cancellation with private Core IPC, drained relays and
container exit 0. Ordinary processes/settings were preserved. The specific
active-session disconnect from attempt four remains unresolved.

Concurrent Event producers/consumers now maintain only an atomic live depth
before measurement; cumulative counts and rows still start at the empty arm
boundary. Each complete observation has an activity guard, so in-flight work
cannot be mistaken for a quiescent baseline. Underflow, capacity violation,
boundary overlap and any existing loss still deny the run. Recording keeps
its strict counter/journal ordering and contention rejection.

The new eight-worker regression reproduces the old failure and passes on the
home Ubuntu repair under ASan/UBSan; recorder/channel suites pass in 23.68 s.
The old failing run is retained, with exact source archive and patch, under
`build/h04-event-preparation-aawd8i2l/`. Mac recorder tests pass (0.66 s), and
all 30 integrated EventQueue checks pass (17.91 s), including eight concurrent
dispatch workers. Core, Host and source build; formatting and full REUSE pass.
Runtime `fbd1212b9` is pushed and its Apple-issued exact-library package verifies.
A fresh attempt-seven plan is staged; H04/P5 remains unaccepted.

The prior hosted Linux run on `4874c6084` found an overly broad test assertion:
the wrapper's Qt locale warning was counted as Host output. The subprocess
regression now checks the unique child stderr marker and forces `LC_ALL=C`.
The Mac suite passes in 11.93 s and home Ubuntu ASan/UBSan in 6.57 s with that
locale. Production forwarding behavior is unchanged; the CI failure remains
retained in `prior-ci-failure.log` under the same evidence directory.

## Active H04 session lost its private channel — 2026-10-15

The fourth attempt (`build/h04-r4-aa2qy2tm/session-1`) reached Active, confirmed
by the user. Packaged admission passed, both machines connected, and all three
recorders retained loss 0. During priming the authenticated Host stopped with
`Trusted companion private channel lost authority`. Evidence never armed and
full original-input readiness never became true. This is a failed session,
not an H04/P5 acceptance result. The specific channel-close reason was discarded;
the downstream Linux TLS disconnect does not establish the initiating cause.

Core and source exited 0, the exact Host stopped, the fresh final source receipt
was neutral with its frame restored, and ordinary configurations were preserved.
The Linux controller failed after the TLS loss and its Core required bounded
termination; exact owned-container/socket cleanup was separately verified.

Explicit evidence sessions now forward Host stderr directly into the coordinator's
Core log and report bounded channel-close reasons in both processes. Default
launches remain silent; stdout stays discarded. No parent-side output queue,
authority, timeout extension or automatic retry is added. The Mac companion
suite and home Ubuntu ASan/UBSan companion suite pass (11.80 s / 6.60 s).
Evidence is under `build/h04-channel-20261015-qo_wfxih/`; a fresh signed attended
run is still required. All six hosted jobs on recorder commit `0f77188a8`
passed in run `38763970033`; those do not cover this subsequent diagnostic edit.

Diagnostic runtime `efb9398b9` is pushed. Its Apple-issued exact-library bundle
and inert admission probe pass. Attempt five (`build/h04-r5-u7ubid0p/session-1`)
timed out waiting for visible Start; input sharing never began. Source exited
0, the remote controller/container stopped without starting a native session,
and ordinary processes/configurations were preserved. The evidence cleanup
timeout remains recorded; no measurement or acceptance is claimed. A new
attended Start is needed to capture the original live disconnect reason.

The user confirmed that attempt five's window closed. Ready now has a visible
five-minute one-shot countdown, independent of the unchanged active session
and recording deadlines. The source and collector accept that longer value
only in Ready; active limits and stale-coordinator checks remain unchanged.
H04 evidence instructions now include the Record/wait/recording-complete steps,
matching the existing recorder controls. The source builds and all 60 Python
coordinator tests pass, including late Start, idle expiry, non-renewal and
state-specific receipt limits. A fresh plan/source is required for the retry.

## Recorder publication overlap follow-up — 2026-10-15

The third attended H04 attempt (`build/h04-r3-spns1sz_/session-1`) stopped
during Preparing with Core recorder loss `Contention`, before Active or arm.
Its inert packaged admission probe had passed. Core and source exited 0,
the authenticated companion stopped, remote cleanup was rechecked, and ordinary
Mac/Ubuntu settings were preserved. The moved source frame remains a failed
restoration check. H04 acceptance is still open.

The recorder held its counter lock across SPSC publication, allowing a legitimate
consumer to collide with a producer finishing its operation. The capture,
translation and completed-copy rings now commit diagnostic counters before
publishing availability/free space. A separate atomic activity guard remains
through the full mutation so arm/drain cannot mistake in-flight work for an
empty queue. Genuine counter/journal contention still denies the one-shot run.
First-failure site and queue are atomically retained and surfaced by the harness.

The deterministic two-thread publication/baseline/drain regression and Mac
channel tests pass; four Mac input/capture suites pass under ASan/UBSan.
Home Ubuntu's recorder/channel ASan/UBSan suites and 98 Python coordinator/
normalizer tests pass in an offline disposable container. The exact source
snapshot and logs are under `build/h04-recorder-20261015-ayzo9bsd/`.
A fresh signed candidate reached Active in the fourth attempt above. That run
failed before evidence arm; these checks do not establish physical input or
roadmap acceptance.

## Attended startup and cleanup follow-up — 2026-10-15

The user returned to the Mac and clicked Start twice. Both fresh H04 attempts
stopped before Active/evidence arm: first the coordinator's short startup wait,
then the runtime's 15-second exact-library deadline. Ubuntu is now on the same
home LAN; WiFiman is unnecessary. Remote cleanup, exact owned-process exits
and ordinary configurations were checked after connectivity returned.

The coordinator now checks actual packaged code admission unattended before
opening the source, and independently checks the later physical endpoints.
The runtime inspection has a single 30-second startup allowance with unchanged
verification, liveness, worker and refusal rules. Explicit terminal fixture
closure bypasses its manual-close veto only after restoration/evidence cleanup;
ordinary window close still requests Stop. Configuration auditing runs even
when fixture closure fails. New probe option combinations fail before startup.

Five focused Mac suites and 56 coordinator tests pass. The home Ubuntu offline
GCC ASan/UBSan evidence/bootstrap/admission/companion suites and 56 coordinator
tests pass with Docker `--init`; full REUSE reports 1133/1133. Failed environment
attempts and both attended failures remain in `TESTING.md` and
`build/h04-retry-20261015-91731y1_/`. These checks do not close H04, P5, physical
recovery or the native/OS queue certification gap. The new Apple-issued package
passes exact admission, private handshake and invalid-option smoke; a separate
ad-hoc copy rejects admission and shuts down cleanly. A fresh physical plan is
ready for the newly signed package.

## H11 physical recording interval implemented — 2026-10-15

Collection/source-fixture commit `286328d055478692ea877c5d5d61d5c619e5093f`
closes a gap found in the roadmap audit: the physical recorder's fixed 30-second
interval could not collect H11's required two minutes. The explicit frozen
`h11` profile now records 120 seconds within a 150-second visible session;
ordinary H04 remains 30/120 seconds. The source UI and receipt/status readers
agree on that cap, and Linux keeps its existing independent 180/270-second
deadlines. All three recorders enforce their original duration. A late arm,
shortened tail or coordinator delay cannot reset or complete the interval.

Local Mac source build and all 151 Python coordinator/normalizer/analyzer tests
pass. Full-repository REUSE now passes (1132 files), and stale current roadmap
references were corrected to the verified `8132c8554` runtime. Evidence lives
under `build/p7-h11-profile-20261015/`. Core/Host runtime code is unchanged.
Home Ubuntu is reachable with an active Wayland session; the staged Mac native
capability probe passes. Readiness for attended tests has been requested, but
not confirmed. No physical input, window transfer or sleep has been started.

The home Ubuntu run on this exact source passes all 13 steps: nine enabled
Core suites, evidence runtime, private GNOME/Xvfb shutdown, five disabled
ASan/UBSan suites and all 151 Python tests. Its container and temporary build
directory were removed. The GitHub source export verifies all 1160 entries;
the Linux archive verifies its source, manifest and x86_64 binaries. A separately
ad-hoc-signed Intel source helper is retained in the evidence directory for the
next attended run. See `TESTING.md` for exact artifact hashes.

All six hosted jobs in run `38750248886` pass on `286328d05`, including Mac
27 standalone / nine integrated / four disabled / 19 sanitizer suites, Linux
22 standalone / 11 integrated / five disabled / one sanitizer suite and the
native GNOME matrix. Both downloaded ad-hoc ARM64 Mac archives also verify
their source, checksums, inventory and executable architecture. The existing
local Intel Apple-issued `8132c8554` Core package stays staged; only the source
fixture and collection tools changed. No installation or attended result is
implied by these package checks.

Full native/library/OS queue certification is still incomplete; the harness
cannot turn unknown or unbounded upstream capacity into a guarantee. The
applicable physical H02–H12 tests and observed installation remain open. Do
not mark the roadmap complete from a longer recorder or synthetic fixtures.

## Independent Core dispatch expiration implemented — 2026-10-15

Runtime `8132c85540b6c3f7f07acaeae0f61c98854df09d` arms an independent monitor before EventQueue producers
start. Its 64 fixed slots retain each active dispatch's original two-second
admission deadline; nested or concurrent work cannot renew an older scope.
Expiry exits only Core with code 71, closing peer/Host channels for existing
disconnect recovery. It performs no callbacks, logging or native cleanup.
Ordinary launches create no monitor. Calls outside an active dispatch, process
suspension and kernel stalls remain outside this guard's scope.

Both local Mac Core builds (window sharing ON/OFF), their EventQueue/Server/
proxy suites, the enabled argument checks and EventQueue ASan/UBSan pass.
All six disposable-process scenarios pass, including actual blocked, nested
and concurrent dispatch expiration and healthy/cancelled/ordinary survival.
The first normal test run consumed its own diagnostic output prematurely;
that harness bug is fixed and its failed log retained. Evidence is under
`build/p7-core-dispatch-20261015/`. All six hosted jobs in run `38744933197`
pass on this exact commit, including the new Mac/Linux subprocess tests,
disabled builds, sanitizers and native GNOME/Qt matrix. Source verification
matches all 1160 Git entries. The fresh local Apple-issued exact-library bundle
and private handshake pass; downloaded Mac/Linux package provenance, checksums,
architectures and Linux manifest verify. The staged package is under that
evidence root's `mac-package` directory. See `TESTING.md` for hashes and timings.

No physical input, window relocation or sleep was started. H10 real restoration
and H11 input/display acceptance, full native queue certification and the other
attended gates remain deferred. See `deploy/windowshare/input-queue-budget.md`.

## Host progress recovery implemented — 2026-10-15

Runtime `e154f94e0676b299b225080658c3d5c3dfbf9f50` closes an unattended recovery
gap: the existing child observes Host death, but cannot detect an event loop
that never returns. An independent Host thread now expires a 15-second startup
or five-second active progress deadline, exits only itself, and lets the child
reopen the exact journal. Heartbeats must come from the creating thread and
stop when shutdown starts. Successful teardown cancels and joins the monitor.

Mac build, actual deadline/crash/healthy/teardown fixtures, ASan/UBSan, both TLS
Bridge suites and native journal regressions pass. The new Apple-issued exact
library package and private handshake pass. Source export matches all 1158
tracked Git entries. All six jobs in hosted run `38712601209` pass, including
the expanded Mac watchdog suite. Downloaded Mac/Linux package provenance,
checksums and architectures pass; the Linux internal manifest also verifies.
Evidence is in `build/p7-host-progress-20261015/` and
`TESTING.md`; the new staged bundle is under that evidence root's `mac-package`.

The earlier one-hour media result remains attached to `d57e78a0d`; those media
sources are unchanged, but it does not test this new Host monitor. Full native
queue certification and attended H02–H12 remain open. In particular, real
window restoration and sleep/wake have not been accepted. Keep physical tests
deferred until the user says they are ready.

## P7 unattended candidate verified; attended gates remain — 2026-10-15

Runtime `d57e78a0da972d29b9e71201c3efdae8c00fac18` passes all six hosted jobs
(run `38675705240`), source export and independent artifact verification. Mac
native hardware-transfer/color/resize/reset checks and scoped ASan/UBSan pass;
the isolated Ubuntu application and Qt/Xvfb/load suites pass. The local Intel
Apple-issued exact-library package passes its audit and private launch check.
The downloaded source matches all 1157 tracked entries and blob identities.

The full, unchanged two-stream 1080p/30-fps synthetic workload and one-hour
resource comparison now both PASS. There are 1801 samples, zero overflow,
recovery or decode errors, and empty queues at exit. Peak sampled RSS is
134.65 MiB, upper net growth 10.44 MiB/h and upper average CPU 100.001%, within
1 GiB / 100 MiB/h / 200% limits. Hardware transfer storage is reused after the
first frame on both streams. The final load has 46 missed source offer slots;
local receive-to-Qt-paint p95/p99 histogram upper bounds are 64/256 ms. These
remain explicit and do not establish native latency acceptance.

Evidence and cleanup are verified in
`build/p7-completion-20261014/mac-transfer-rgb-hour/verification.json`.
No local build/test/signing/profiler work was launched by this task during
collection. Earlier failed hours remain recorded. `TESTING.md` contains exact
commit, artifact/source hashes, test scope and reproduction instructions.
The staged local bundle is `mac-transfer-rgb-package` under the same evidence
root. CI archives are separate ARM64 ad-hoc Mac and x86_64 Linux packages.

Next work requires the deferred native/attended acceptance: current desktop
capabilities and installation, real routing/dragging/app behavior, owned-process
crash and sleep/wake restoration, and end-to-end input/display measurements.
Full native/library/OS queue certification is still incomplete. Do not mark
P7/H10/H11 or the whole product accepted from this synthetic pass, and do not
start attended fixtures until the user returns ready to test.

## P7 reproducible quiet-hour failure; transfer/paint follow-up — 2026-10-14

The quiet hour on `6108bd285` completes but fails: 70/68 decoder recoveries and
498.05 MiB/h upper net RSS growth, with a 658.96 MiB sampled peak. All 1801 samples
are retained; both queues drain without decode errors. Recovery begins at
1892.38 seconds and decoder queue wait reaches about 0.59 seconds. No local
build/test/signing/profiler work overlapped this run. The exact stall cause
remains unknown; prior interference cannot explain away the reproduced failure.
All six hosted jobs and source export pass on that commit, and downloaded Mac
and Linux package provenance/checksums are verified. `TESTING.md` separates
those automated passes from the failed sustained workload.

An intentionally instrumented diagnostic confirms VideoToolbox transfers and
allocator activity. The decoder follow-up reuses one CPU transfer buffer for
the same retained hardware context/dimensions, and emits opaque native RGB32 to
avoid a second Qt pixel conversion. Real hardware tests check alternating
colors, retained images, resize and reset; unavailable hardware is reported
explicitly. The transfer-only five-minute comparison passes with zero recovery,
9891 reuses out of 9892 decoded frames per stream and RSS 123.48 to 133.74 MiB.
It cannot establish hour acceptance. Final RGB32 Mac/native/ASan tests and the
Ubuntu application/eight-suite/16-stream/two-HD checks also pass. Package, hosted
matrix and long-run results must be recorded separately before acceptance.

Attended tests remain deferred. Do not start physical routing, window relocation
or sleep without the user's later readiness. Full native/OS queue certification
and H07–H12 acceptance are still open.

## P7 retained steady-load failure and bounded diagnostics — 2026-10-14

All six hosted jobs and source export pass on `affdad48b`; downloaded Linux
and ARM64 ad-hoc Mac packages match their source and checksums. The local Intel
package is separately Apple-issued and staged. Five predeclared private Mac
channel repetitions pass. Those results do not explain the earlier transient
hosted epoch failure, whose original close reason was not recorded.

The completed synthetic hour passes its fixed resource comparison (254.41 MiB
peak RSS, 94.17 MiB/h upper net growth, 105.73% upper average CPU, 1801 samples)
but **fails the steady workload**: one decoder overflow/recovery on stream 1.
Both streams drain with zero decode errors. Local builds/tests and a memory
probe overlapped collection; the recovery timestamp was unavailable, and its
cause is unknown. Thirty-second control and memory-probe checks both pass, so
neither establishes a cause. All failures and raw data are retained.

New explicit diagnostics retain bounded recovery timestamps, complete failed
workload reports and decoder-queue/worker/GUI elapsed maxima. Exact timing and
regression tests, the Mac Viewer and mixed-load sanitizers, and Ubuntu full
application/eight suites/16-stream/two-HD-stream checks pass. No queue limit,
deadline, resource threshold or steady-workload rule changed. `TESTING.md`
records the exact evidence and frozen source hash.

Next: package and host-test this diagnostic candidate, then collect one new
predeclared hour after all local build/test/signing work, without local probes
or competing test suites. Keep the previous failed hour. The user explicitly
deferred attended testing; do not start physical input, relocation or sleep.
Full native/OS queue certification and H07–H12 physical acceptance remain open.

## P7 automated matrix green; package retention follow-up — 2026-10-14

Runtime candidate `bbb847796d361b0056178948b1cbf97cdd2e6e0f` passes all six
jobs in Actions run `38661949281`: GCC/Clang foundations, transport/private
sanitizers, Linux app/Core ON/OFF, native headless GNOME, and Mac app/Core
ON/OFF/packaging. The earlier epoch and resource-fixture failures are retained
and are not suppressed. Source run `38661949207` also passes; its downloaded
archive matches all 1156 tracked Git entries and blobs.

The local Apple-issued exact-library Mac package passes its dependency audit
and private Core/Host launch check. Evidence and package hashes are in
`build/p7-completion-20261014/mac-image-reuse-package-manifest.json`; this stages
the candidate without installing it or starting native input. The current CI
follow-up retains already verified Linux and ad-hoc Mac archives for seven
days, with source commit and checksums. It does not use the local signing key.

The roadmap and C01–C12 matrix now describe current implementation and actual
tests instead of the original cloud baseline. The replacement synthetic hour
is still collecting; no final resource verdict is available yet. Native queue
certification and physical acceptance remain open, and the user explicitly
deferred attended tests. Next: verify the new CI artifacts and finish the
existing hour collection without changing its plan or thresholds.

## P7 decoder allocation follow-up — 2026-10-14

The first offscreen hour attempt crossed the predeclared 1 GiB RSS comparison
and was stopped after 1064 seconds, with a 1,193,361,408-byte peak. Its raw
samples and incomplete result remain in `build/p7-completion-20261014`.
Allocator inspection found substantial reusable resident storage; this is not
proof of the absence of leaks. No resource threshold has been relaxed.

The decoder now reuses at most three unshared conversion images, retaining at
most 32 MiB per decoder. Shared in-flight results and larger formats use fresh
storage; reset and dimension changes release cache references. Alternating-color
tests retain more results than the cache can hold and verify that old frames
survive subsequent conversion and reset. Viewer and mixed-load ASan/UBSan tests
pass on Mac, and the full Ubuntu application build, eight Qt/Xvfb/load suites,
16-stream and two-stream 1080p checks pass. The Ubuntu archive SHA-256 is
`df0972a918df7a261b1e86c865e29602aa96714eca853041caa8b302c60b69a8`.

Hosted run `38660677004` on `3c4263f03` passes five jobs, including GNOME.
The Mac resource-parser fixture used a 1.1-second collection with a one-second
interval, leaving an extra slot only 100 ms before the end. The sampler correctly
rejected a skipped slot on the busy runner. The fixture now uses one initial and
one terminal sample; production sampling and completeness rules are unchanged.
All 15 budget tests pass locally on Mac and Ubuntu. Hosted retesting is pending.

A replacement offscreen workload is running with a predeclared 3630-second
duration, a post-warm-up start marker and 3600 seconds of resource sampling.
`build/p7-completion-20261014/mac-image-reuse-hour` retains its plan, source/binary
hashes and raw samples. It has no final verdict yet. This combined synthetic
fixture cannot establish native H11 performance. The user explicitly requested
unattended work and deferred all attended recovery/input/display tests.

## P7 native-batch audit and hosted checks resumed — 2026-10-14

GitHub Actions accepts dispatch again. Run `38659292670` tested `cb506b6e1`:
Linux application, transport and both portable sanitizer jobs passed. The Mac
private-channel epoch test failed without a retained close reason; it passes
locally. Failure unwinding now prints the bounded existing channel/child
diagnostics without changing deadlines. GNOME reached the family checks, then
failed because their output directory already existed when the provider
fixture tried to create it. The container now runs the provider before tests
that create its report subdirectories. Hosted retesting follows this change.

The native audit found libei's many-events-per-System-callback gap. Explicit
experimental Core now processes at most 4096 native events per batch and checks
the existing two-second deadline before/after library dispatch and handlers.
Failure invalidates the prefix observation and terminates the session. The
private EIS test sends actual protocol seat notifications and exercises both
capacity and an expired library dispatch, without devices or desktop input.
`deploy/windowshare/native-queue-audit.md` records inspected source hashes and
the remaining upstream allocation/OS-capacity gaps. A processing limit does
not certify library storage or preempt a blocked native call.

`build/p7-completion-20261014/linux-native-batch-2/result.json` passes the full
ON Core build and nine suites, private GNOME/Xvfb smoke, and the OFF Core build
with five ASan/UBSan suites. The existing 40/35/35 evidence/coordinator checks
also pass. Its frozen source archive is
`fe80d8d2f01ee09c0687cf5aa17724e74df25c24772e50db71e4f47f4a9a2814`;
the offline fixture removes its temporary directory. The first configure
attempt exposed a CMake mixed-signature error, corrected in this final run.
Mac watchdog/native/holding tests and the updated EventQueue/private-channel
suites pass in `build/p7-queue-20261014/p7-completion-*-tests.json`.

The user explicitly deferred attended testing again. H10/H11 physical tests
remain pending. A separate predeclared 3600-second, two-stream 1080p/30-fps
offscreen load is collecting private evidence; its outcome is not yet known
and cannot establish native capture/display performance.

## P7 Core backlog implementation and Actions enablement — 2026-10-14

Explicit experimental Core launches now enforce 4096 outstanding Event
lifetimes, 64 active dispatch scopes and a two-second age check before dispatch
and after callback completion. The shared budget covers pending, saved,
caller-retrieved, immediate and nested borrowed events. Failure is terminal;
the Mac callback synchronously revokes capture, completed input and held-motion
state, then the event loop exits through the existing cleanup path. Ordinary
Core launches keep their previous admission policy. Startup publication is
serialized, busy queues service cleanup timers, and spurious wakeups no longer
recurse. Rejected/expired/abandoned payloads are disposed.

Evidence v2 adds whole-Core counters, including configured actions and control
events, beside the original-stamped subset. It declares the actual 4096/64
limits. The normalizer rejects missing inventory, substituted limits and mixed
versions; legacy v1 null capacities remain explicitly unbounded. Application
limits do not establish upstream OS/libei/compositor/device bounds or physical
latency. See `deploy/windowshare/input-queue-budget.md`.

Final local results are retained in `build/p7-queue-20261014`:

- `mac-build-final-2.log` and `mac-tests-final.json`: production Core plus six
  signed suites pass, including the actual Mac buffer and capture-revocation
  fixture. No tap or OS input posting is used by those fixtures.
- `mac-companion-build.log` / `mac-companion-tests-final.json`: production
  companion and disposable source rebuilt for evidence v2; Bridge and native
  input regression suites pass offscreen. No desktop fixture was launched.
- `mac-ordinary-build.log` / `mac-ordinary-tests-final.json`: OFF Core build
  and four compatibility suites pass.
- `mac-sanitizer-build-final.log` / `mac-sanitizer-tests-final.json`: fully
  instrumented project Core dependencies, EventQueue, Server, evidence and
  private capture checks pass ASan/UBSan; external system/Qt libraries are not
  instrumented and leak detection is disabled.
- `linux-final-2/result.json`: full ON Core build, nine suites, private GNOME
  absence/Xvfb smoke, full OFF Core build and five ASan/UBSan suites pass.
  The actual EI buffer is tested through an owned Unix listener. All 40
  normalization, 35 H11 analyzer and 35 physical-coordinator parser tests pass.
  The offline Ubuntu 26.04 image has no host display/device/bus mounts and
  removes its temporary directory. Source archive SHA-256:
  `8df3799c5eaee5ff0e916c6279a3485841b98d70fb494f7d982f154bbdab245c`.
- The same Python checks pass on Mac. Earlier failing reports are retained:
  a capacity-validation regression and the Linux X11 `None` macro collision
  are corrected in the final candidate.

GitHub accepted repository Actions enablement and workflow activation, but
dispatch still returns HTTP 422, “Actions has been disabled for this repository.”
The Actions page separately says this fork was stopped due to the scale of
Actions usage and requires a maintainer to re-enable it. The available browser
is signed out. The user has been asked to clear that restriction; see
`actions-enablement.json`. Hosted verification is **NOT_RUN**, not passed.
Workflow path filters now include the complete EventQueue interfaces, platform
buffers and test/build files; the existing six-job matrix runs the regressions.

**P7 acceptance remains open:** full native/OS queue certification, H10 owned
crash/sleep recovery, H11 real input/display latency and the planned hour-long
resource workload. Fresh physical readiness was requested; do not treat an
unanswered request or the automated passes as readiness. H07/H08/H09 also remain
deferred. This entry supersedes older statements that Core backlog itself has
no production bound; it does not upgrade any historical physical result.

## P7 reproducible media/resource tooling — 2026-10-14

The next P7 implementation slice is complete. The production Viewer now has
explicitly enabled, fixed-storage queue/decode/unique-paint counters and a
local receive-to-Qt-paint histogram. The ordinary app leaves collection off.
`windowshare-media-load` drives actual pinned loopback TLS, FFmpeg and multiple
offscreen Viewers. It obeys Channel backpressure, counts source replacements,
separately injects decoder overload, and verifies hidden/resumed recovery.
It starts no NativeHost, input tap or desktop window.

Resource sampler v2 retains per-probe monotonic observation windows and CPU
counter resolution. `resource-budget.py` recomputes the existing fixed pilot
limits from raw samples, treats counter regression/missing slots as incomplete,
and requires at least 3600 planned seconds for a resource-component verdict.
It does not certify measurement provenance or the actual app workload.

Evidence is retained in `build/p7-evidence-20261014`:

- `mac-final-build.log`: production app, load tool, Viewer and Bridge build.
  `mac-viewer-bridge-final.json`: both signed regression suites pass.
  `mac-media-load-final.json`: four-stream steady/mixed and 16-stream steady
  pass; the mixed run reaches the actual 16-frame decoder bound, drains and
  recovers. `mac-hd-load-final.json`: two generated 1920×1080 streams pass the
  five-second workload invariants. These are short diagnostics, not sustained
  performance acceptance; small-frame Mac runs retain hardware-decoder fallback
  messages and do not establish GPU acceleration.
- `mac-sanitizers-final.json`: Viewer and the mixed load pass ASan/UBSan with
  separately signed copies and leak detection disabled.
- `linux-final/result.json`: the full standalone Ubuntu application builds;
  all eight selected Qt/X11/load suites pass, followed by 16-stream and two
  1920×1080-stream workloads. Its archive SHA-256 is
  `765af8e75127d2002f738b2fe4876f356128abae1e44f97cd277e5fd9b0c817b`;
  per-path hashes include source symlinks. The offline Ubuntu 26.04 container
  used no host display/device/bus mounts and removed its own temporary files.
- Final resource checks pass on both platforms: nine sampler and 15 budget
  tests, including a real owned child, malformed/incomplete evidence, fixed
  thresholds and conservative counter/timing uncertainty. Mac logs are
  `resource-sampler-tests-final.log` and `resource-budget-tests-final.log`.
  `linux-resource-final/result.json` and its source manifest cover the final
  negative-RSS-growth upper-bound correction after the full Ubuntu build.
- Earlier failed harness reports remain retained: they exposed a fixture
  violating Channel admission and automatic focus requests without a fixture
  reply. The final harness respects admission and explicitly denies input focus.

The reproducible-tooling roadmap item is checked. **P7 is not fully complete:**
full platform/kernel queue coverage and bounded Core EventQueue backlog remain
open; H10/H11 native failure, display/input latency and hour-long resource
acceptance remain NOT_RUN. Physical H07/H08/H09 are still deferred. Hosted
checks for this slice are NOT_RUN; repository Actions was disabled at the last
dispatch attempt recorded below. See `deploy/windowshare/media-load.md`,
`deploy/windowshare/resource-soak.md` and the newest `TESTING.md` entry.

## P6 implementation and local verification — 2026-10-14

Window-family code is implemented; **physical H08/H09 remain NOT_RUN**. The user
continues to defer physical testing. Do not launch visible Mac fixtures, event
taps or desktop input routing from the automated results below.

`window-family-v1` connects exact native child discovery/capture to separate
painted child Viewers, immutable bounded parent/session relations, modal
ancestor fencing, exact focus/clipboard ownership and child-only dismissal.
Children share the root's durable recovery; they never receive independent AX
relocation journals. Returning a member returns the complete family. Outside
parent clicks may invoke the exact menu/popover's AX Cancel action, subject to
existing input fences. Unsupported discovered children return locally.

X11 uses source-relative child placement and exact child XIDs. GNOME admits only
uniquely associated visible top-level ancestors owned by one retained native
client; it preserves its existing single-surface/focus/grab/queue requirements.
GNOME placement remains compositor-managed, so precise source-relative popup
positioning is an explicit H08 usability limit. Arbitrary OS/app menus and IME
candidates are not blanket-supported. See
`deploy/windowshare/window-families.md` and `deploy/windowshare/app-compatibility.md`.

Local evidence is in `build/roadmap-completion-20261014`:

- The production Mac app builds. `p6-final-mac-tests.json` passes all five
  signed suites: family, Viewer, Bridge, viewport and native original-input.
  `p6-final-mac-viewer-smoke.json` passes authenticated reconnect and clean shutdown
  with the production Viewer; it does not start NativeHost. The earlier
  component run caught the missing shared-recovery preparation path and an
  invalid input-fence test binding; both are corrected.
- `p6-linux-final/result.json` verifies implementation commit `bfb9cb9e8` with
  the full Ubuntu app build, eight Qt/X11
  suites (Xvfb at 1 and 1.25), provider policy, Viewer reconnect smoke and actual
  isolated GNOME 50.1 / Qt 6.10.2 sheet/menu lifecycle and parent mutation tests.
  Its frozen archive SHA-256 is
  `dd343d6799c5dd2733291903de1320f71246205cd980e1842c11b7e98f28e78e`.
  The source has per-file hashes; the offline container removes its
  own temporary directory and never accesses the user's desktop/devices.
- `p6-sanitize-tests.json` passes family, Viewer and Bridge under Mac ASan/UBSan,
  with separately signed copies and leak detection disabled. The retained
  failing report `p6-sanitize-tests-before-lifetime-fix.json` exposed QPointer's
  invalid Viewer downcast during QWidget destruction. A separate lifetime token
  now closes before derived members are destroyed.
- `p6-direct-destruction-before-fix.json` also reproduced a direct-deletion
  path through Bridge's family cleanup. Its destroyed callback now clears the
  typed pointer before retiring the family. The regression deletes both a
  parent and a child directly, without relying on a prior Closed packet;
  `p6-direct-destruction-fixed.json` passes the full Bridge suite under ASan/UBSan.
  The final production rebuild and signed Bridge suite also pass in
  `p6-destruction-mac-build.log` and `p6-final-bridge-tests.json`.
- Do **not** count `p6-linux-family-2` as a complete GNOME pass: its harness caught
  an assertion as an ordinary provider exception. Run 3 corrected that test and
  exposed that Qt's transientParent setter does not mutate its existing native
  xdg parent. Run 4 separately changes Qt state and the actual owned native role,
  and requires the provider's explicit “owned viewer parent changed” rejection.

Implementation commit `bfb9cb9e80152386f4cb4ca7b5cf3dc0969afb10` is pushed to
`feature/seamless-windows`. The new GNOME family fixture is wired into the hosted
native job, but **P6 GitHub checks are NOT_RUN**: GitHub rejected workflow
dispatch with HTTP 422, “Actions has been disabled for this repository.”
`p6-github-checks-unavailable.json` records the result. Repository settings were
not changed. The previous P5 CI result below does not cover P6.

## Previous verified P5 checkpoint — 2026-10-14

The implementation and fixture corrections are committed and pushed through
`c748915ccb65c4b169188d3ae2d1ea326a288ff9` on `feature/seamless-windows`.
[All six hosted jobs pass](https://github.com/JackHars/deskflow/actions/runs/38645498688): GCC and Clang
sanitizer foundations, transport/private IPC, Linux application/Core/package,
Mac application/Core/package and native headless GNOME integration. P5 code
implementation is complete; **H07 physical acceptance remains NOT_RUN**.

P5 ordinary-titlebar drag code is integrated in both directions. Private
protocol v9 carries 232-byte drag commands, 240-byte status messages and
112-byte Host native-move observations. Normal button ownership stays with its
original recipient; only held movement is gated after a native/painted preview.
The outbound path joins real up, native annotation and Server Local dispatch,
then journals holding relocation before ordinary cursor transfer. The return
path joins Core's exact remote press/edge to GNOME's owned Viewer grab; it
requires both real Core up/dispatch and native grab End before journaled Mac
source placement/focus and remote retirement. Source identity is resolved by
Host, never accepted as a remote native window ID.

Remote input has no application-stage Mac annotation because the HID tap
consumes it for Ubuntu. Return waits for the existing bounded join to publish
its exact raw stamp with window=0; no annotation or release is synthesized.
The Mac preview is frameless, input-transparent and non-activating. Readiness
requires actual Qt paint and an exact owned Cocoa/WindowServer frame witness.
Repeated GNOME watches release the concurrent 16-window capacity on retirement.

Current signed Mac checks pass: 36 private-channel coordinator scenarios,
private IPC (including v9 malformed/replay/role/capability cases), watcher and
viewport suites, Bridge return/cleanup cases, 50 enabled Server tests and five
feature-disabled Server tests. Both production Core build modes and the
WindowShare app build. Evidence: `p5-return-order-tests.json`,
`p5-return-wire-watcher-tests.json`, `p5-return-bridge-tests-3.json`,
`p5-return-server-tests.json`, `p5-return-server-off-tests.json` and the matching
build logs under `build/roadmap-completion-20261014`.

The first Bridge return fixture exposed lingering state after preview deletion;
a follow-up exposed QWidget's early destroyed signal while Viewer members were
already gone. Cleanup now clears the retained preview pointer before cancellation;
both failures are retained in `p5-return-bridge-tests*.json` and the scoped crash
backtrace. The original Ubuntu build reproduced the cleanup regression; its
seven other suites passed. The corrected frozen Ubuntu run
`p5-linux-return-final/result.json` passes the full application build, all eight
affected suites, actual headless GNOME continuity, and production Core/Server
with the feature enabled and disabled. The later retirement-order change passes
the full app rebuild and both affected suites in
`p5-linux-watch-retirement/result.json`, plus signed Mac viewport/Bridge checks
in `p5-return-retired-watches-tests.json`. These runs use disposable offline
containers on home Ubuntu and remove only their own temporary directories.
The actual GNOME check preserves the association, native surface/window and
exact (160,90,642,520) frame through commit. All four focused Mac ASan/UBSan
suites pass: Bridge, the 36-scenario coordinator, private IPC and viewport
endpoint (`p5-return-sanitize-tests.json`). Leak detection was disabled; each
sanitizer halts on its first reported error. These tests use offscreen Qt and
fake native return witnesses, not physical Cocoa acceptance. The initial SDK
link failure is retained in `p5-return-sanitize-build.log`; using the same
MacOSX15.5 SDK as the production build succeeds in
`p5-return-sanitize-sdk-build.log`.

Hosted GCC 13 exposed a copied `std::pair` loop variable rejected as a warning
in the pointer-join fixture. Using a const reference fixes that portability issue;
the signed Mac fixture and all 16 portable GCC 15 ASan/UBSan foundation suites
on home Ubuntu pass (`p5-pointer-fixture-tests.json`,
`p5-gcc-foundation-final/result.json`). Production code is unchanged by this
follow-up. The original hosted compiler failure remains recorded.

The hosted Linux run then passed all 19 application suites and exposed a stale
capability list in `viewer-smoke.py`. Its exact offscreen Hello expectation now
includes `window-drag-preview-v1`; unknown/duplicate/native move capabilities
remain rejected. The real Viewer reconnect smoke passes on both Mac and home
Ubuntu (`p5-viewer-smoke-mac.json`, `p5-viewer-smoke-linux/result.json`). This is
another fixture-only correction, with no native Host or desktop input involved.

The user's latest instruction is **“Keep building; test later.”** No readiness
question is pending, and no physical fixture or desktop input routing has been
started. **H07 is NOT_RUN and P5 physical acceptance remains open.** Current
fixtures do not establish arbitrary native/browser/Electron compatibility or
real Cocoa preview/restore behavior. Older status paragraphs below are historical.


The subsequent Core edge-observation checkpoint is implemented and tested.
`WindowDragEdgeMailbox` carries only one bounded value from the Server event
thread to `LocalInputCoreSession`; no second raw consumer or cross-thread Server
call is added. It binds the authenticated Core connection, exact original local
press, configured neighbor/edge segment, mapped point and both screen bounds.
Existing double-tap, corner locks and dwell policy are respected; only a real
stamped motion can finish dwell. Leaving the edge, release, Escape, reconfiguration,
shape change, saver, disconnect, shutdown or TLS revocation invalidates retained
observations. A stalled worker's observation expires after 250ms. The consumer
joins that press to the HID/annotation result, rejects expanded/uncertain input,
and revokes immediately on raw up without waiting for either downstream queue.
Its optional `onWindowDragEdge` callback is observation-only; it does not yet
send a drag command to Host, prepare a preview, switch computers or post input.

The enabled Core build, Server and companion suites, feature-OFF Core/Server
test, and new portable edge tests pass. Edge ASan/UBSan passes on Mac. Normal
and ASan/UBSan runs pass directly on home Ubuntu with exact source hashes,
no desktop interaction and only the owned temporary directory removed.
Evidence is `p5-edge-final-tests-2.*`, `p5-edge-portable-tests.*`,
`p5-edge-sanitize-tests.*`, `p5-edge-feature-off-*` and
`p5-edge-ubuntu-2/result.json` under `build/roadmap-completion-20261014`.
Earlier failures remain preserved: a missing motion in the new portable fixture,
an incorrect primary-recipient lookup (local ownership has no remote recipient),
and test configuration/API assumptions. The corrected integration suites pass.

The earlier join/preparation checkpoint is committed and pushed as
`5416e738249219f27afd7699f6c7953fa1539aac`; its 31 source hashes match that
commit (`p5-checkpoint-commit-binding.json`). The pending eligibility probe
remains built from that checkpoint. Use its frozen archive and commit binding,
not the subsequently edited working tree, when recording its physical result.

P5 is in progress, not complete. The latest working checkpoint adds the bounded
HID/annotated pointer join, private v6 observation transport, exact native
standard-titlebar observation and the NativeHost preparation/drop gate. The
owned physical join passes (`owned-titlebar-joined-physical-20261014T174302Z`):
window 10383, native event 516, sequences 211/425, 213 drags, 147 frame changes,
real release 3.828 seconds later, clean exit and no posted/suppressed input.

The sole original-input consumer remains `LocalInputCoreSession`; optional
annotation/identity failure must not stop ordinary input or emergency return.
The native titlebar observer uses the event-time ID, exact AX hit/window,
process birth and unchanged size with movement; it never guesses a foreground
window. Joined observations are metadata, not input or native grab authority.
The private protocol is now v6; Core network IPC5 is unchanged. Production
Session -> private channel -> Host observation is wired. The preparation API
requires that retained native observation, a matching press and physical layout.

Ten scoped Mac integration suites pass, as do IPC/transaction sanitizer tests,
prior join/capture sanitizers, the enabled Core/Host build and feature-OFF Core
build. The home Ubuntu join/transaction tests pass normally and under ASan/UBSan;
private v6 tests pass normally and under ASan/UBSan in a disposable Ubuntu build
container on that machine (the host has no Qt development files). Full native
Ubuntu/GNOME acceptance has not been rerun for this checkpoint.

The pending fresh-readiness question is for `--interactive-eligible`, a passive
30-second titlebar eligibility check. It now uses a separate owned AppKit source
process so AX queries cannot block their own application's main loop. Do not
reuse readiness from the completed join test. Keep failed runs and bind each
physical result to its frozen source/binary. No user response has arrived for
this next test at the time this checkpoint is written.

Remaining P5 integration: private edge command/dual-pin destination controller,
proven native drag/cursor cutover, moving/painted preview and real-release commit,
GNOME-owned Viewer drag-back, connected cancellation/recovery and H07. The
new preparation gate is not yet invoked by the Core edge candidate. A fixed frame
query delay is not a native grab-completion receipt, and a remote preview alone
does not prove seamless ownership. Do not mark P5 complete or silently substitute
thumbnail dragging for the user's ordinary-titlebar requirement.

Earlier evidence and implementation history follow; newer entries above supersede
statements below describing the same-event join as the next missing step.

The next P5 prerequisite is implemented in the production capture path:
`OriginalPointerObservation` retains the original pointer position, both native
window-under-pointer fields and reported source metadata beside the exact
input stamp. Capture completion and the existing Session/Server copy queue
preserve it without a second raw consumer or a protocol change. Missing IDs
stay unavailable; these observations grant no drag or posting authority.
At committed checkpoint `09d0fbfe8`, the full Mac build, 16 related routing/input
suites and four scoped ASan/UBSan suites pass. Two shared input suites also
compile and pass on the home Ubuntu
machine with GCC 15.2, both normally and with ASan/UBSan (four executions).
That remote test uses source hashes matching that checkpoint and removes
only its own temporary directory. See `TESTING.md` for exact evidence and limits.

The new passive `windowshare-native-titlebar-probe` builds and observes a normal
AppKit titlebar through that same adapter. It neither posts nor suppresses
input. Two physical attempts stopped before a titlebar-down: added rejection
diagnostics identify the first trackpad mouse move's subtype 3 as the cause.
The old adapter accepted only subtype 0. A constructed-only regression
reproduces the failure, and support for touch-origin mouse-button/movement
semantics is now implemented. Native touch/pressure/gesture reconstruction
remains unsupported. The next physical run records the ordinary titlebar down,
244 drag events and 164 owned-window frame changes, then exposes a second bug:
the native drag-ending up has zero clicks, which the adapter rejects. Both
event-time window IDs on the down are zero; HID-head fields alone do not
identify the arbitrary app window. That failed run remains preserved.

The zero-click release correction now preserves the native value through
capture, portable validation and delivery. Receiver release still requires its
exact retained button press and current route; positive click counts must still
match. New regressions reproduce the old failure. The two corrected Mac capture
suites and four scoped ASan/UBSan suites pass. Current source also passes both
shared input suites normally and with sanitizers on the home Ubuntu machine
(four executions, GCC 15.2, exact transferred hashes, owned temporary directory
removed). The fresh passive repeat (`owned-titlebar-physical-20261014T171313Z`)
passes: normal titlebar down, 226 drag events, 150 frame changes and the real
release 4.022 seconds later, with clean shutdown and no input posting. Both
HID-head window IDs remain zero. The full Mac Core/Host build passes. All nine
related integration suites pass across the initial 8/9 run and the corrected
IPC suite's separate pass: its expected byte fixture needed the zero-count
sample, while the previous positive-count representation remains covered.
Evidence is `titlebar-zero-up-final-evidence.json` under the Oct 14 root.
The fixes are committed and pushed as `e0a813d431d94c27a41cdd292ca34899bb0c9d3c`.
The explicit `--interactive-annotated` probe mode observes the later
application-annotated event stage. Its fresh physical run
`owned-titlebar-annotated-physical-20261014T171931Z` passes: both event-time
window IDs equal the owned window ID, followed by 167 drag events, 114 frame
changes and a zero-click real release 3.079 seconds later. The source event
number is 515 on both down and up. Production Core continues capturing at
HID-head with its existing sole consumer. Next: verify and implement a bounded
same-event join from annotated window identity to the original HID press.
Separate single-tap runs do not establish that correlation or native grab
authority, and the known fixture is not arbitrary-app titlebar eligibility.
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

## Historical local packages (archived)

The `build/packages/` paths in this section are historical and now reside in
the verified Ubuntu archive recorded above. Installed applications were not
changed. The current physical-test package remains locally at
`build/h04-late-target-94wwqjc4/mac-package/DeskflowSeamless-Core-Experimental.app`,
with ZIP SHA-256
`3a20163da3bc42706ee4f620d631229d886205079226b3bc3dfb7b658fb85c86`.

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
