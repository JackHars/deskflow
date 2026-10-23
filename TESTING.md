<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Seamless Windows: cloud results and local verification

## Current startup and scheduling candidate — 2026-10-23

Runtime `a17067ac776f74ce514dabdaa4cb6d7b2843d5ed` includes:

- `f13684d34`: coordinator wake on the exact private dispatch acknowledgement.
- `6f46ce690`: bounded Core wake on private preparation and release progress.
- `4061abb31`: Core wake on validated peer-mapping receipts, without waiting
  for the maintenance timer. Duplicate, malformed and late results grant nothing.
- `1e9271ddb`: inherited Mac Host activity protection from startup through
  teardown, including the input-disabled admission probe. Child diagnostic
  stderr and exit status are retained. Normal sleep remains allowed.
- `a17067ac7`: keep the test-only matched allocation replacements opaque to
  optimized GCC callers. No warning suppression or allocation-check removal.

All 18 selected Ubuntu suites and package checks pass in `build/startup-qualified-ilanoah3/`.
The Mac build succeeds and all 23 selected suites have passing evidence, with
three timing failures in the initial run retained. Evidence serialization,
private bootstrap and companion checks pass the isolated same-binary retry in
`build/resumed-mac-check-l_vi2xi2/`, with unchanged deadlines. The retry does not
prove scheduling reliability under every load. `mac-test-results-initial.json`
and `mac-app-tests.log` preserve the first failures; `mac-test-results.json`
links both attempts. Hosted source run 39639307808 and all foundation jobs in
39639307856 pass.

The signed Mac package verifies with all 77 dependency hashes unchanged. Two
fresh input-disabled packaged startup checks pass on October 23, including
exact library admission and clean companion exit. Total times are
21.566 s and 11.629 s; their logs show the inherited Host activity
token active before the handshake. These probes install no native input hooks
and do not qualify live input or establish the cause of every previous stall.

| Artifact | SHA-256 |
| --- | --- |
| Mac signed archive | `7ed28a95c906c8c960753a686e67a3e98e56a80d8c0323cf363e51dceae8ebb1` |
| Ubuntu archive | `9f0bc54f0769d1ce4db34a9186ad051c2b36a125ada77d69ac7809a801e05a00` |
| Source archive | `fb15edf15e9e556afd0af4150e84154e5beb17a0d455ebabb01036815898da32` |
| Disposable source fixture | `366f3bbf1b484b37d6019f4f7e26550a1e2cadda1192e094f63db3f07accc8af` |

No full live input run has qualified these artifacts. Earlier 4061abb31 run
`dashboard-replay-y7zhbguh` fails its second admission probe before sharing;
the first passes. The failure retains a 2.545-second heartbeat gap and failed
code admission, with complete owned cleanup and ordinary settings preserved.

On October 23, the user explicitly chose **Keep the regular connection running**.
The ordinary Mac Deskflow server is connected to jack. Do not stop it or launch
a competing capture/input session. The proposed unattended two-minute run was
not launched. A future live run must first resolve that conflict with user
authorization; any attended physical test still requires permission immediately
before launch. Strict scroll timestamps, held-repeat delivery, three complete
same-artifact qualifying runs, H03–H12 physical acceptance and queue certification
remain open. No acceptance rule or deadline has been relaxed.

## Earlier peer-map notification candidate — 2026-10-21

Runtime `4061abb3148cee4a1251558e9d21c5d0dee2966b` is pushed. It adds a bounded Core wake after
the exact validated peer-mapping result. Entire groups are validated before
publication; malformed/late results, an earlier Pong and identical duplicates
cannot trigger an input wake. Proxy-retirement callbacks and timer-free Core
completion are covered. No input field, authority, queue limit or deadline
changes. Focused regressions pass in `build/mapped-core-wake-0teq87p7/`.

All 22 selected Mac suites and 17 Ubuntu suites pass in `build/peer-wake-validation-j7_nh9yl/`.
Both packages verify, and all 77 Mac dependency hashes remain unchanged.
Hosted source run 39637249557 passes; foundation run 39637249597 was superseded and cancelled.
The unchanged test harness remains bound by `build/controller-harness-5gngwzmo/`.

| Artifact | SHA-256 |
| --- | --- |
| Mac signed archive | `4073b7dbe84d9c11fb72bd25c0a6072e5266f437d8572502d1600a4e68df5012` |
| Ubuntu archive | `d618bcc11ab33a74a39571fae5d7e6bb0ada6654e784e281a15c18eed50a9dd7` |
| Source archive | `39c1a5ae85e574227426b6d779cadb59dcf1b95fc38b8715b66b8d322d9f352c` |
| Disposable source fixture | `366f3bbf1b484b37d6019f4f7e26550a1e2cadda1192e094f63db3f07accc8af` |

## Earlier private-path notification candidate — 2026-10-21

Runtime `6f46ce6906553641b947ee8b7d9d5228b7462149` is pushed. It removes a
Core polling delay after private preparation, exact dispatch acknowledgement
and release. The notification conveys scheduling only. At most one is queued;
dropped notifications, a completion arriving during dispatch, stale callbacks
and Server destruction retain their exact lifetimes. No input field, authority
check, capacity or deadline changes. The focused 60-case Server and 34-case
coordinator suites pass in `build/core-progress-wake-9li2hhwi/`.

All 22 selected Mac suites and 17 Ubuntu suites pass in
`build/core-wake-validation-9ea_q2lt/`. Both packages pass dependency/package
verification; the Mac package is signed and retains the same 77 verified
libraries. The unchanged harness is bound by `build/controller-harness-5gngwzmo/`
(216 Ubuntu cases; 215 Mac cases plus one Linux-only skip).

| Artifact | SHA-256 |
| --- | --- |
| Mac signed archive | `a86db87404e520e68af038f13c5de8f83fb306a14cda5697d2f03e1c143186c4` |
| Ubuntu archive | `4d6b64df4b9a79fae742310b5a4c846272687cc2ef18bd9be3f23f96b57054c5` |
| Source archive | `6b88bfc3472e2c81bf539411b3605084a29eb3d0aa07fffddd8dce51379ab7c7` |
| Disposable source fixture | `366f3bbf1b484b37d6019f4f7e26550a1e2cadda1192e094f63db3f07accc8af` |

The `6f46ce690` live run `dashboard-replay-wlfk3mxg` lasts 120 seconds and
completes all three 30-second recorders. All 1,251 functional receipts match
exact raw fields and order, including all 1,200 scroll events at 119.75 Hz.
Normal restoration, zero held input, owned cleanup and all 21 queue histories
pass. **Strict acceptance still fails:** 29 scroll timestamps change, all in
the early burst above 100 ms delivery time; this profile does not exercise
repeat. Exact capture-to-AppKit latency is median 25.26 ms/p95 66.57 ms/max
128.60 ms, not display latency. The owned return stack sample observes normal
idle operation after successful restoration and does not reproduce the earlier
stall. Hosted source run 39634416930 passes; foundation run 39634417089 was
cancelled by the newer revision and is not a pass.

The immediately preceding `f13684d34` candidate also passes 22 Mac/17 Ubuntu
suites in `build/mapped-wake-validation-jjbp47u2/`, plus hosted source run
39501010497 and foundation run 39501010547. It wakes the Qt coordinator only
on the exact validated dispatch Pong. Earlier, malformed and unrelated Pongs
do not notify it; callback close/delete/throw regressions pass.

Its live `dashboard-replay-pk6fcntt` completes 120 seconds Active and all three
recordings but fails exact scroll delivery and normal restoration. The
1,200-event burst is 119.25 Hz. Native scroll combination changes raw order and
multiplicity, and timestamps also change. Both strict latency analyzers reject
the mismatched join; no valid full capture-to-AppKit latency result is claimed.
Mapping RTT alone is median 9.56/p95 19.13/max 145.08 ms, with Ubuntu mapping
work median 5.79/p95 13.51/max 30.58 ms. This is not display latency. All 21
queue histories validate; native pending overflow is zero.

Return frame writes succeed, but the immediate position read fails with
`kAXErrorCannotComplete` (-25204). Normal restoration stays failed and retains
the journal. A later recovery helper restores the source with zero held input
and clean owned teardown. The companion-exit/heartbeat failure remains in the
session result; recovery is not a normal-shutdown pass.

The repeat boundary controls are preserved in:

- `build/repeat-annotated-owned-l8q4v77s/`: corrected owned later-stage capture
  observes all six events and exact timestamps, including both repeats.
- `build/repeat-original-late-capture-2_xbnqy5/`: copying the original event
  fields through that later boundary also preserves repeat/key/flag fields;
  two timestamps change under concurrent build load. Early HID interception
  still loses both repeats.
- `build/repeat-owned-shortcut-6mr_xlkt/`: the Carbon shortcut never activates
  even in the unfiltered control, so shortcut safety is **inconclusive**.
- `build/repeat-shortcut-control-ifk9e79i/` and
  `build/repeat-shortcut-stages-r9se6z0x/`: corrected unmodified F18 control
  activates an owned shortcut. HID capture blocks it; annotated interception
  lets it activate before interception. Moving capture later is therefore
  unsafe as a production repeat fix.
- `build/repeat-hid-driver-9r2bo0h4/`: owned IOHID posting through the early
  filter still loses repeat; it does not reproduce physical hardware state.

None changes the production capture stage, global preferences or acceptance
rules. Repeat, timestamp/raw delivery, reliable normal restoration, three full
same-artifact qualified runs, H03–H12 and upstream queue certification remain
open. No attended test has been launched.

## Earlier collector-thread checkpoint — 2026-10-20

Runtime `ccebffffeee68aefdf07d6ce02da4f1f22ad3e51` moves evidence control reads,
serialization and output onto one dedicated collector thread. Its consumer
retains the existing 8,192-row producer journal, the one queued continuation,
fresh control validation, original arm/stop deadlines, native receipt tail and
sticky failure semantics. No second payload queue or larger capacity is added.
Shutdown joins the consumer after producers retire; the native-only terminal
close is queued once onto the application thread. Blocking filesystem calls
remain outside any verified completion bound.

Native observation now brackets admission through publication with the existing
activity guard. Drained status requires zero capture/mutation activity as well
as an empty journal. `nativeCollectorPublicationBoundary` rejects overlapping
native admission and preserves a late admitted row after a failed forced stop.
The final focused sanitizer proof is `boundary-asan-test.log`, with source and
executable hashes in `boundary-validation.json`; the earlier sanitizer pass
does not cover this added guard.

`build/evidence-worker-check-yvvxwegc/` contains the passing focused Mac suite
and focused AddressSanitizer/UndefinedBehaviorSanitizer run. The new regression
blocks the actual collector executor with an explicit rendezvous, verifies
application callbacks and native-row publication continue, and verifies exact
ordered teardown with both bounded input and deliberate buffer overflow.
Existing private-path, arm, stop, tail, continuation and native-close regressions
still pass. There are no fixed sleeps or deadline waivers.

The optional ThreadSanitizer check is **inconclusive**, not passing. Its warnings
at Qt queued-call publication/completion also reproduce in
`qt-tsan-baseline.cpp`, a standalone Qt-only program using the same
uninstrumented Qt library; every blocking-call value is correct, but it reports
two warnings and exits abnormally. The full instrumented evidence suite also
exceeds its unchanged cooperative-test deadline. Preserve `tsan-test.log`,
`qt-tsan-baseline.log` and their exact build arguments. A fully instrumented Qt
build would be needed to qualify that sanitizer boundary.

The final candidate in `build/collector-boundary-9rczih8p/` passes all 22
selected Mac suites and 17 Ubuntu suites. Signed Mac package verification,
all 77 unchanged Mac dependency hashes and Ubuntu package/dependency checks
pass. Source-bundle run 39489639299 and all six jobs in foundation run
39489639329 pass. Earlier `710dae975` local suites passed but were superseded by
the native publication guard before any live test; its hosted run was cancelled.

| Artifact | SHA-256 |
| --- | --- |
| Mac signed archive | `7db28557fbf1808004b48976a90839c0b30ad6ee7ae6f693fbdfc3c549a081d3` |
| Ubuntu archive | `4f8814d8797f820c979223b5ec1e30ab0010e88e0bfcbe5519ddb22882413927` |
| Source archive | `7260fb28f17dfdb6c3a946498680516a83ac053b7e40329c3bd9dfa115c2da3e` |
| Disposable source fixture | `366f3bbf1b484b37d6019f4f7e26550a1e2cadda1192e094f63db3f07accc8af` |

Harness `build/controller-harness-h_h0ffs0/` binds this source and passes all
211 cases on Ubuntu; Mac passes 210 and skips the Linux-only process-identity
case. Do not reuse the prior runtime's live results for these binaries.

The following actual-desktop recovery diagnostics use these exact native
packages and source fixture. Each checks restored source geometry, released
input, owned-process cleanup and unchanged ordinary configuration. These are
scoped recovery observations, not complete input or physical acceptance:

| Owned process terminated | Retained run | Restoration observed |
| --- | --- | --- |
| Ubuntu viewer | `dashboard-replay-3nstj0u3` | 613.346 ms |
| Mac Host | `dashboard-replay-zlzcxol1` | 767.395 ms |
| Mac Core | `dashboard-replay-37apm0g4` | 533.182 ms |

`dashboard-replay-5q07sd_8` failed before submitting the holding-helper fault:
the coordinator's port-availability check reported `EADDRINUSE`. No test
window or capture session started, and cleanup completed. Follow-up harness
`ee7d4012f` reproduces the failure with an owned closed TCP connection in
TIME_WAIT and uses the Unix native listener's `SO_REUSEADDR` restart policy.
It does not use `SO_REUSEPORT`, wait for port expiry or accept a live listener.
The check is explicitly availability-only; the native server must still bind.
Both the failing reproduction and passing regressions are retained in
`build/port-preflight-err5humb/`.
The updated bound harness `build/controller-harness-hk1hv008/` passes 213
cases on Ubuntu and 212 plus one Linux-only skip on Mac. The native runtime,
package hashes and source fixture above are unchanged.

The next holding-helper attempt, `dashboard-replay-rejs9ffd`, also submits no
fault: the recovery wrapper cancels on `ConcurrentPublicationChange` while
reading the source snapshot. Its original failure and cleanup are retained.
Harness `81bb308d2` treats only missing/changing live snapshots as pending,
logs that condition and retains the one-shot Active notification for the next
stable publication. It does not reuse the preceding snapshot, retry in a tight
loop or extend the original absolute deadline. Stable malformed data, held
input, owner/geometry changes and duplicate Active remain failures. The restore
journal follows the same stable-read rule; frozen plans and final reports still
use strict reads. Regressions cover pending-to-stable publication and denied
authority. `build/controller-harness-5gngwzmo/` binds this source and passes
all 216 cases on Ubuntu; Mac passes 215 with the same Linux-only skip.

The complete fresh recovery matrix passes with harness `81bb308d2` and the
unchanged `ccebffffe` packages:

| Owned process terminated | Retained run | Restoration observed |
| --- | --- | --- |
| Ubuntu viewer | `dashboard-replay-jfd51sdy` | 1,215.755 ms |
| Mac Host | `dashboard-replay-9mh1rvg3` | 515.087 ms |
| Mac Core | `dashboard-replay-vs5x8wz5` | 256.604 ms |
| Holding-display helper | `dashboard-replay-iluitjh8` | 331.964 ms |

`build/collector-boundary-9rczih8p/recovery-matrix-v4.json` indexes these
unmodified results. All four observe source restoration, zero held keys/buttons,
unchanged ordinary configuration and exact owned-process cleanup on both hosts.
No input is submitted. Each deliberately failed pipeline keeps its original
failure; only the independent scoped recovery assessment passes. No result
claims H10 physical acceptance, sleep/wake coverage, full input acceptance or
one of the three required complete pipeline passes.

The first sustained-input attempt on this candidate, `dashboard-replay-0dkhswz_`,
reaches Active, but its diagnostic activation helper exceeds its unchanged
two-second subprocess deadline before printing its first stage. No input is
posted and no recorder is armed. The coordinator subsequently reports the
missed Record cutoff; source restoration, zero held input, ordinary settings
and owned cleanup all pass. The original helper timeout remains in
`synthetic-replay.jsonl` and `replay-driver.log`; this is not a scrolling pass.
A later permission-only probe of the same helper completes in 38 ms. A separate
new-copy launch comparison in `build/replay-launch-preflight-v63ldoxe/` takes
396 ms cold and 44 ms warm, which does not reproduce the original timeout or
establish its system cause. Diagnostic launcher `build/replay-ready-lihb7ho2/`
now probes the exact staged helper before opening the coordinator, retaining
that readiness result and launcher hash per run. Previously it probed the
source copy. No native binaries or input/session deadlines changed.

The subsequent `dashboard-replay-k14t48ax` completes 120 seconds Active, all
three 30-second recordings and automatic return. All 1,200 burst events arrive
at AppKit in order with exact raw fields/flags, at a source rate of 117.84 Hz;
all five non-repeat board checks and 1,251 functional receipts are observed.
All 21 queue histories validate and end with zero loss and drained journals.
Both machines restore/clean up with zero held keys/buttons and unchanged
ordinary settings. The staged helper readiness check completes in 347 ms.

**Strict input and performance validation still fail.** Only 442/1,200 burst
timestamps are preserved (493/1,251 functional receipts overall); 758 scroll
timestamps change. The strict normalizer rejects the original unmodified
journals. The non-repeat profile does not exercise held-X repeat; the separate
self-targeted AppKit repeat diagnostics remain failed. The wrapper's
`verified: true`/`expectedNonRepeatRecordingChecksPassed` describes completion
of its diagnostic profile, not strict input, physical acceptance or roadmap
qualification. `fullReplayPassed` remains false.

Same-Mac capture-to-AppKit latency across all 1,200 burst records is median
133.77 ms, p95 308.27 ms and maximum 370.51 ms. `scroll-stage-latency.json`
separates capture-to-map waiting (median 66.85 ms, maximum 318.14 ms), the
mapping round trip, and reply-to-AppKit delivery. The 236 mapping batches
have median 10.62 ms, p95 91.18 ms and maximum 122.01 ms round trip; Ubuntu
mapping work itself is at most 25.57 ms. The remainder includes transport and
scheduling, not established one-way network latency. The owned SSH socket
records 4,344 retransmitted bytes during the burst; this is a contributor to
investigate, not proof that network loss explains every delayed record.
Ubuntu power saving remains off, verified read-only after the run.

Retain `scroll-capture-receipt-latency.json`, `scroll-stage-samples.json`,
`mapping-timings.json`, `network-counter-summary.json`, the process counters,
both raw host logs and all evidence journals. No native authority denial or
terminal dispatch failure occurs in this run, but excessive waiting and changed
timestamps prevent acceptance. Do not retry the attended test: reduce the
measured waiting/transport tail without changing authority, raw events, queue
limits or deadlines, then obtain three complete qualified runs on one frozen
candidate. The current source-bundle check for harness `81bb308d2` passes
(39493140816); its foundation run 39493140792 is pending.

`build/native-repeat-layout-y1im_k1d/` removes the isolated self-PID probe's
explicit Unicode override and uses empty annotated strings like production.
It still receives all four non-repeat AppKit events with exact timestamps and
neither repeat. This is another retained diagnostic failure, not physical
acceptance or a field-preservation waiver.
`build/native-repeat-psn-2tzv8no7/` also tests the deprecated public
`CGEventPostToPSN` against only the diagnostic's own process serial number.
It produces the same four exact non-repeat receipts and no repeat receipt.
The six submitted events retain their fields; production posting is unchanged.

## Earlier dispatch-failure checkpoint — 2026-10-20

Runtime `835bc6d71ca530b40dcc835aa0b850a4d298d887` is pushed. The new
candidate in `build/dispatch-failure-broezp0w/` passes all 22 selected Mac suites
and 17 Ubuntu suites, signing, dependencies and package verification. Its harness
in `build/controller-harness-_wlilij7/` passes 210 cases plus one Linux-only skip
on Mac and all 211 cases on Ubuntu. Source-bundle run 39485133920 passes; hosted
foundation run 39485133853 passes all six jobs. Actual-desktop verification is separate.

| Artifact | SHA-256 |
| --- | --- |
| Mac signed archive | `032da90187a7381de2e7f0e218f815c1a74ab0a56ef6aa2d15f9d521c37ddb16` |
| Ubuntu archive | `027a2ef1bd4d7e104e96db569f71e9da551130fecb2856643f70132e1e31abb9` |
| Source archive | `e771d55b3776dce857d68dc630dfc060c8b58ba318fee1fdba618ca1c13e934d` |
| Disposable source fixture | `e2a037745e33f4ac24f28658adb2936e5ca3828b9934b65aaa9fd8375f97262e` |

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
the scrolling burst when ScreenCaptureKit reports -3821. Its
`system-capture-failure.json` and `system-capture-cause.json` retain the macOS
sequence: `replayd` receives a high-urgency cache purge at 19:45:41.876 UTC,
stops its streams, and the Host reports capture failure at 19:45:41.913.
The coordinator preserves capture shutdown as the first cause; source restoration
and owned-process cleanup complete. This run establishes no sustained-input
improvement. The system requested roughly another 4 GB of cache reclamation
while about 10 GiB remained free. `build/retired-bundle-compaction-qsy66g7c/`
subsequently verifies each archive, file, mode and symlink before removing 12
redundant extracted older test bundles, recovering about 1.5 GB. Current artifacts,
all archives and raw logs remain. This cleanup does not qualify capture reliability.

Subsequent `dashboard-replay-ekq94s8r` reaches Active, arms all three recorders
and completes five non-repeat board checks. The burst stops after 106
posted/observed events. At 19:52:55.791 UTC, the Host denies
`input.native-candidate`: fresh focused ancestry is unavailable, frame is null
and minimized state is unknown. Focus and window-state queries take 50 and
100 ms, respectively; the full candidate takes 163 ms. The mapped receipt still
has 72 ms left. The coordinator preserves that authority denial as the first
cause and stops the run. Core journal draining takes 43 ms and source control
reading takes 34 ms on their application threads in the same interval; these
are observed stalls, not proof of the sole cause. Source restoration, zero held
keys/buttons, unchanged ordinary settings and owned cleanup complete. The
incomplete recorder, repeat and strict replay remain failed. See `verification.log`,
`authority-timings.json`, `owned-cpu-samples.txt` and the unmodified session logs.

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

| Artifact | SHA-256 |
| --- | --- |
| Mac signed archive | `f5375bf49511c4b741b808813b33f4dec7a17426375eb405d03eb22cb87ecb15` |
| Ubuntu archive | `9f6e804c21f1f79da2e8d7c4fd5d877291827f67ecf50fcfd93b8b105ea768e8` |
| Source archive | `4f949dba9932dfeb2774f27aa414ddd980fc59f83a8f93e990af9a1c582da743` |
| Disposable source fixture | `b55b5f0a04d1a3d6907bdd944453a223c070f7b03572e3168665a1b772e6346e` |

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

Runtime `5801513591291d9cc821a78d8b175f1fbb53d00f` is built, packaged and
pushed. `build/input-latency-vf23_d58/` contains 21 passing selected Mac suites,
16 passing Ubuntu suites, signed Mac package verification and Ubuntu dependency/
package checks. The coordinator harness in `build/controller-harness-gau69rjd/`
has 209 cases per platform: Mac passes 208 and skips the Linux-only `/proc`
exec-identity check; Ubuntu passes all 209. The harness source, package source
and disposable fixture are independently bound and checked before launch.

| Artifact | SHA-256 |
| --- | --- |
| Mac signed archive | `711ed0839784353dd74102f88f17725332ae74b5463c5cf8a4a30dd6901cd3fb` |
| Ubuntu archive | `9d244dfc463c48c54d8523312688f347dd30df9e33b1c836f5ab27a292ebfbdb` |
| Source archive | `5c4b0747da95ce498a550833af445ab3a1113f3901fc3298bede78d0e1de2622` |
| Disposable source fixture | `b55b5f0a04d1a3d6907bdd944453a223c070f7b03572e3168665a1b772e6346e` |

The native input batch now targets 50 ms of observed work instead of 125 ms,
leaving time for mapping and scheduling. It still preserves order, bounded
capacity, every event's original receipt and fresh native authority. The test
window sets its instruction text once per update rather than changing between
physical and automated text and causing repeated layout. The coordinator retains
`Host/capture.failure` as the first failure even when later restoration or input
shutdown reports another cause. Regression checks cover the unchanged ordered
prefix behavior, adaptive limits and capture-failure reporting.

An isolated, committed `native-timestamp-probe.m` diagnostic posts only to its
own PID, with no user window or global input posting. On this Mac, ages 0, 50,
90 and 99 ms retain their timestamp; 101, 125 and 250 ms are rewritten before
the process tap. All seven pre-post readbacks match the supplied timestamps.
`build/native-timestamp-3fq2w762/final-result.jsonl` records 7/7 observations
and 4/7 preserved timestamps. This identifies a native boundary on this machine,
not a universal OS guarantee or permission to rewrite evidence.

Actual-desktop results on the frozen candidate:

- `dashboard-replay-50csru1o` reaches Active but the synthetic driver's five-second
  owned-key-window observation expires. It sends no input and never arms the
  recorders. Source restoration and owned cleanup complete with zero held input;
  the original focus/readiness and unarmed-recorder failures remain failed.
- `dashboard-replay-rrbde301` completes the full 120 seconds Active, all three
  30-second recordings, 21 queue journals, normal source restoration and owned
  cleanup. All 1,251 functional receipts retain exact raw fields, flags and
  timestamps; the independent strict recorded-journal comparison also passes.
  All 1,200 scrolling events arrive at 119.32 Hz. Same-Mac capture-to-AppKit
  observation is median 30.54 ms, p95 50.25 ms and maximum 85.57 ms; these are not
  physical display latencies. Five non-repeat board checks pass. Synthetic
  repeated-x still has no AppKit receipt, so the full replay remains failed.
- `dashboard-replay-76oi4qmi` receives all 1,251 functional inputs, including
  1,200 scroll events at 119.11 Hz, but 85 timestamps change. Capture-to-AppKit
  observation is median 39.79 ms, p95 123.07 ms and maximum 195.46 ms. All
  recorders finish and the 21 queue checks pass; strict raw matching fails.
  On the return shortcut, Ubuntu refuses preparation cleanup and terminates
  its session. Source restoration and owned cleanup complete, while the
  premature relay disconnect and strict input comparison remain failures.
  The repeat sequence stops at this failure; no third follow-up is launched.
  See `followup-nonrepeat-results.json` and the unmodified Ubuntu Core log.

The helper adds changing native focus/owned-window observations to distinguish
request acceptance from actual readiness. Its five-second limit, posting scope
and native receipt requirements are unchanged. No attended test was launched.
Three qualified complete input runs have not been achieved. Original repeat,
startup reliability, sustained-load repeatability, H03–H12 and the unverified
native/library/OS queue contract remain open.

Earlier `3ee0cf757` passes all six hosted jobs in
[run 39472776971](https://github.com/JackHars/deskflow/actions/runs/39472776971).
Runtime `580151359` also passes all six hosted jobs in
[run 39476869149](https://github.com/JackHars/deskflow/actions/runs/39476869149).
These hosted passes do not close its retained actual-desktop failures.
Both `3ee0cf757` and `da2d1e4c6` pass their 21 Mac/16 Ubuntu local suites.
The latter accidentally applied the generic 250 ms inspection default to
placement; `3ee0cf757` restores the pre-existing absolute two-second placement
contract. Tests reject completion at the original transaction deadline and
cancelled writes. Input/environment observations remain at 250 ms.

The `3ee0cf757` actual-desktop diagnostics remain failed:

- Sampled `dashboard-replay-fmstxyia` loses native focus/state authority during
  the burst; it stops at 254 posted events. Profiling can perturb timing.
  Owned processes stop, but source restoration was not observed; retain the
  restoration failure.
- Unprofiled `dashboard-replay-4ct8_67z` posts all 1,200 events and completes all
  30-second recordings with zero recorder loss. AppKit combines two adjacent
  scrolls, and ten other timestamps change before the process tap. The strict
  comparison fails. macOS then stops ScreenCaptureKit with -3821 during a
  logged high-urgency cache purge, before the full Active interval completes.
  Source restoration and cleanup succeed. The system log identifies the purge;
  it does not prove whether storage or memory was its initiating cause.

`build/bundle-compaction-fr5ptjcy/` verifies archive hashes, every regular file,
symlink target and mode before removing 25 redundant extracted generated app
bundles. Saved archives and all logs remain. The manifest records exact restore
commands. Available disk space increased by about 3.2 GiB; this alone does not
prove system purge failures are fixed.

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

## Earlier capture diagnostics and incomplete acceptance — 2026-10-20

Current tested runtime on both machines:
`b51487025a6277e4677d4b1b1a724b0ba9b052ae`.
Build/package records: `build/capture-failure-fzgavzj0/`.
All 21 selected Mac suites and 16 Ubuntu suites pass. The two native capture
suites additionally pass normally and with ASan/UBSan in
`build/capture-diagnostic-tests-wb58j7h1/`, with retained source hashes.
Mac signed archive SHA-256:
`ad8e387c638fc9489da68acc51aa03950bf52b488917fa4f3f33d0cd28413c19`;
Ubuntu archive SHA-256:
`eb7b3c1da78c1a782c89ca227b796d43ddc27f46357a28c73e07308321611af8`;
source fixture SHA-256:
`9e2be3a1683e590664b728474a5fa79804e3f0004389749ab9512ad7ca428886`.
Harness `65d49c18e` is bound to 208 passing checks on each machine in
`build/controller-harness-efi8wbe4/`: 109 Mac coordinator, 33 Linux supervisor,
9 desktop, 46 normalizer and 11 recovery checks. The normalizer now explains
validated recorder failure metadata and still rejects all nonzero losses.

The current diagnostic retains one immutable native capture failure with the
rejected stage, event types, original timestamp/flags and scroll syntax. It
allocates no storage and prints nothing in the event tap; the consumer logs
the publication, including when stream revocation wins the first observation.
It does not rewrite events, reopen a revoked epoch, loosen deadlines or claim
to fix the underlying continuity failure. All source/window/focus/geometry
checks and 250 ms input/environment limits remain.

`cbd9d0c6e` also bounds outstanding native inspection workers at 64 process-wide,
including cancelled/destroyed owners and finished QThreads awaiting deletion.
The limit is shared across result types. Exhaustion refuses placement and
restores exports when the environment watchdog cannot obtain a slot. The
regression fills all slots and proves they cannot be reused until native work
and retained QObject lifetimes both finish. This closes that application
allocation gap only; see the still-incomplete native/library/OS queue audit.

Retained new actual-desktop failures:

| Runtime / run under `build/` | Failure and evidence |
| --- | --- |
| c319 / `dashboard-replay-po2emvk6` | Exact disposable source activation exceeded two seconds before input. The first driver lost the helper stderr; newer frozen helpers retain activation stages on success and timeout. |
| c319 / `dashboard-replay-wkzyo3d_` | Native environment read took 256 ms and crossed the unchanged 250 ms limit during functional input, before the burst. Source restoration and owned cleanup completed. |
| cbd9 / `dashboard-replay-45kfgqh_` | Initial functional typing/click/drag/two-axis scroll receipts passed and all recorders armed. Capture continuity was then revoked with reason 3 during recorded scrolling. The precise rejected event was not in the old diagnostic. The burst never started; source returned, held state reached zero and owned cleanup completed. |
| b514 / `dashboard-replay-kqvt3_jj` | Setup failed before Active. A 92 ms environment worker result completed before the owner processed it after the 250 ms deadline. The helper reported a transitional origin `(1440,-1)` rather than requested `(1440,0)`. Initial source restore also failed AX readback and left a journal; owned shutdown ultimately closed the disposable source and the journal is empty. The raw result retains its restoration/recorder failures. This is not a restoration or pipeline pass. |

Two b514 read-only startup profiles target only the exact owned Host during
successful no-input Core-fault diagnostics: `dashboard-replay-8pblin0b` and
`dashboard-replay-pxlsd0sk` observe restoration in 286.506 and 259.175 ms.
The latter samples AppKit display reconfiguration on the main thread. These
successful samples do not identify the failed startup's exact stack or prove
input reliability. The cbd9 environment timing attributes almost all polling
cost to capture-permission preflight (139 ms maximum).

`dashboard-replay-lk5tqqah` reaches all five non-repeat board checks and posts
1,200 scroll events at 119.35 Hz. The recorded board exercise uses 18.8 seconds;
per-action process/tap startup then leaves too little of the 30-second interval
for the ten-second burst. Core expires its 250 ms drain with one server-completed
item, recording loss 9 (Deadline). Capture reason 5 occurs during later cleanup;
it is not the original failure. Source restoration, zero held input and owned
cleanup complete. The raw failed journals remain unchanged. Native timestamps
still differ, and this is not a scrolling or recording pass. The revised local
helper removes per-action startup overhead while retaining actual event receipts
and every deadline; its subsequent result is described below. Older passes
cannot qualify b514.

`dashboard-replay-cil8b3_9` is another b514 startup failure, before input or a
scroll sample. A 145 ms environment read completes on its worker, but the main
thread handles it after the unchanged 250 ms deadline during placement. The
source restores, held state reaches zero and owned cleanup completes; the
preparation and incomplete-recorder results remain failures.

Four fresh c319 no-input actual-desktop recovery diagnostics passed, with
exact source/frame/journal restoration, zero held input, owned cleanup and
ordinary settings preserved. `build/native-startup-63_08raw/recovery-matrix-v3.json`
records Viewer kill 552.484 ms, Host kill 768.355 ms, Core kill 291.664 ms and
holding-helper kill 420.844 ms (Host alive). Exact run directories are in the
current `WORK_STATE.md`. These observer times include publication delay.
The underlying sessions remain FAILED after the intended faults; these are
scoped recovery observations, not H10 or input acceptance. Older raw reports
are unchanged; `build/async-registration-jtwk28cd/final-recovery-assessment-audit.json`
separately records reassessment with the corrected bounded supervisor/assessor.

Hosted run 39455238057 executes the explicit Intel Mac runner after two earlier
Mac jobs failed to acquire ARM runners. Its five non-Mac jobs pass. The Mac
job passes 37 of 38 application suites, including native startup, but times
out in `concurrentEventPreparation` within the input-evidence suite's 15-second
limit. Later Mac build/package steps are not executed. Source-bundle run
39455238390 passes. Test-only `a3a79385e` replaces standard barriers with explicit
blocking rendezvous, keeping all producer counts/rounds and the 15-second suite
limit. The suite passes locally on Mac (1.45 seconds) and Ubuntu (0.26 seconds)
in `build/evidence-barrier-mz3ata8w/`. Hosted run [39460069913](https://github.com/JackHars/deskflow/actions/runs/39460069913)
on `a3a79385e` passes all six jobs, including all 38 Mac app suites, integrated
checks, package verification and the packaged private Core/Host handshake. Its
source-bundle companion 39460069894 also passes. This does not qualify later
unpushed source changes or actual-desktop acceptance.

The revised diagnostic helper in `build/scroll-persistent-pr55pgd3/` retains
one command in flight and fresh source PID-birth/Active/foreground checks, with
the event tap disabled while waiting. Its three no-input boundary checks pass.
`dashboard-replay-2tgofzw8` reduces the recorded board exercise to 3.77 seconds,
then posts the ten-second burst inside the original recording interval. The
actual route expires its two-second capture-to-route history budget during the
burst. The capture epoch remains live; the native capture rejection diagnostic
is not the cause. Core reports PipelineRevoked and Host reports OwnerChanged.
The raw restoration failure remains failed; later owned shutdown leaves an empty
journal. It is not a scrolling or recovery pass.

Harness `65d49c18e` stops Active verification as soon as that terminal route failure
or a native-delivery denial is observed. Cleanup still runs, and the first cause
survives later stop logs. The new tests cover every terminal class, unrelated
observations and stopped cleanup. All 208 coordinator/normalizer/recovery checks
pass on both machines. A separate audit of the original log identifies the join
deadline; no raw report is rewritten. Read-only profile attempt
`dashboard-replay-s4kjkmj8` fails to establish the owned native key window within
five seconds after activation was accepted. It posts no input and takes no sample.

Three consecutive qualified same-artifact input runs remain unmet. H03–H12,
original repeat/timestamp/window receipts, latency and full native/library/OS
queue certification remain open. No attended test was launched while the user
was away. Ask immediately before a future attended launch, after the autonomous
readiness gate passes. Logs are saved; no results popup or fixed sleep is used.

## Pending-placement watchdog and actual recovery — 2026-10-20

Latest tested runtime on both machines:
`86699a430f10b60f21aa5a513a63e1e24c7adde3`.
Build/signature/test records: `build/source-preparation-watchdog-m0_wr8ws/`.
Mac package archive SHA-256:
`25737111c45e3ca349a87dc8f336ead2c97da0da13cee33481ac01445e164736`;
Ubuntu archive SHA-256:
`4ccdf5e8b850fb64f2c0f4bee2bb7da17c6b7958042be70218ef4ccaf8d9e03b`;
unchanged source fixture SHA-256:
`9e2be3a1683e590664b728474a5fa79804e3f0004389749ab9512ad7ca428886`.

The pending native placement observation now owns its setup query; the
watchdog avoids repeating blocking identity/associated-window reads while
that bounded preparation is outstanding. Process birth, permissions, capture
failure and deadlines remain checked. Final placement and input still require
fresh native authority. All 20 selected Mac suites and 16 Ubuntu suites pass;
the signed Mac package and Ubuntu package checks pass. The unchanged harness
remains bound to 107 coordinator and 42 normalizer checks on each machine in
`build/scroll-harness-validation-t0cohbgq/`.

The next actual-desktop burst is **not a scrolling pass**:
`build/dashboard-replay-nau1a1ge/scroll-boundary-diagnostic.json` retains all
1,200 exact raw/flag arrivals at Core and at the target-process tap, at
120.06 Hz. The observer closes cleanly. AppKit combines pairs 405–406 and
657–658, leaving 1,198 window receipts. Same-Mac HID-to-process delay rises
to median 984.1 ms, p95 1267.6 ms and maximum 1300.0 ms; tail delay is
122.7 ms. Mapping measures median 12.7 ms and maximum 28.2 ms. Serial native
checks and shrinking bounded batches accumulate pending input (peak 156/256).
There is no overflow or evidence loss, but that does not make the delay
acceptable. The full 120-second Active period, three 30-second recordings,
21 queue journals, normal restoration, held-zero and owned cleanup complete.
The five non-repeat board checks pass; repeat and strict timestamps remain
unaccepted. Earlier lower-latency runs cannot substitute for this result.

The new [recovery procedure](deploy/windowshare/host-recovery.md) performs
no-input faults on the actual Mac/Ubuntu desktop. It revalidates frozen
executables and exact owned process identities before the fault, observes
automatic return and audits cleanup without injecting a manual restore.
Six unit tests pass locally on Mac and Ubuntu; both hosted job definitions
now run them, but those CI changes have not been executed on GitHub here.

| Fault/run under `build/` | Observation | Underlying pipeline result |
| --- | --- | --- |
| `dashboard-replay-kz_wr_s8/recovery/` — Host killed | Exact frame/journal restored in 770.6 ms | Failed as expected: companion exited |
| `dashboard-replay-krka9lez/recovery/` — Core killed | Exact frame/journal restored in 283.1 ms | Failed as expected: Core exit -9 |
| `dashboard-replay-1htr7kdp/recovery/` — holding helper killed | Exact frame/journal restored in 296.8 ms while Host remained alive | Failed as expected: hidden placement ended |

Each reports `recoveryObserved: true`, owned cleanup, zero held input and
unchanged ordinary settings. These observation times include polling/status
publication delay, not measured physical display completion. Host/Core
termination prevents the ordinary input recorder footer; the saved coordinator
errors are retained. Core-kill additionally retains its expected unsuccessful
Core drain. The helper case has no cleanup errors.

The first two used procedure SHA-256
`c6112f8e0ea4a60d5b60281dde16f64c623b5523569ed4b579e04ce36a971e19`;
the helper case used
`07e5f3cf4a1635b093f683210b2f81e6eec75a0644d3ebd90d7a4564b12bc41e`
and retains its exact `procedure.py`. The final assessment additionally
checks clean source exit and the expected Core exit. Earlier harness attempt
`dashboard-replay-cnk0xttv/recovery/` submitted no fault and failed while
waiting for native source identity; keep that result. Readiness now waits for
the initial native snapshot, with regression coverage.

These no-input faults do not establish held-input release, viewer/network
disconnect, sleep/wake, physical restoration or H10 acceptance. None counts
toward the three consecutive qualified input-pipeline passes. H03–H12 remain
open. No attended physical test was launched while the user was away; obtain
permission immediately before any future attended launch.

## Scrolling fixes and remaining acceptance gap — 2026-10-20

Current tested runtime: `6d1447f1cf1d7a55086e66b175e12d0f00f45263` on both machines.
Build/signature records: `build/scroll-worker-final-ye18v6r6/`.

Two sources of blocking work are repaired. Evidence serialization previously
occupied the input thread for 39–55 ms; it now yields after 128 rows or 2 ms,
with one continuation and unchanged recorder capacity, content, order and
footer rules. A separate setup failure occurred when a 336 ms WindowServer
read blocked delivery of the environment watchdog response. Setup observations
now run in one background reader, followed by fresh owner/lifecycle/geometry
checks on the Host thread. Placement retains its original two-second absolute
verification deadline. Input/environment deadlines remain 250 ms. Cancellation,
stale results and duplicate completions cannot resume a stopped transaction.

The diagnostic parser also discarded native `slow`, `denied`, `expired` and
similar states. It now retains all structurally valid states. Native logs carry
thread IDs without dereferencing the application object from a late worker.
Original raw failure logs remain intact. No sleep, input rewrite, queue increase
or acceptance waiver was introduced.

Validation: all 20 selected Mac suites and 16 Ubuntu suites pass. The unchanged
harness binding in `build/scroll-harness-validation-t0cohbgq/` records 107
coordinator and 42 normalizer checks passing on each machine. New regressions
cover cooperative exact-row serialization and teardown, a blocked placement
read alongside responsive environment/input work, no overlapping native reads,
coalesced observation notifications, duplicate/late callbacks, expiry,
cancellation and destruction. An intermediate GCC test-fixture compile error
is retained in `build/scroll-placement-worker-en697kmi/`; the corrected
explicit callback parameter passes on both compilers.

Actual-desktop replay: `build/dashboard-replay-64ie7k1n/`, using jack's real
GNOME monitor. The setup query again takes 326 ms, now on a background thread;
startup succeeds. The session completes 120 seconds Active and all three
30-second recordings. All 21 queue journals validate, recorder loss is zero,
queues drain, held input is zero, and the source restores while its Host is
still alive. Owned processes stop and ordinary Mac configuration is unchanged.

All 1,200 indexed scroll events at 119.19 Hz reach Core and the exact target
process with unchanged raw fields and flags. Same-Mac HID-to-process latency
is median 43.1 ms, p95 98.2 ms and maximum 128.4 ms; the final event is 50.1 ms.
The process observer closes cleanly. The strict AppKit-window comparison still
**fails**: events 1105 and 1106 become one combined receipt, leaving 1,199.
The saved `scroll-boundary-diagnostic.json` explains the sum without replacing
or repairing the original evidence. The ordinary board observes typing,
clicking, drag/drop and both scroll axes; repeat and strict timestamp acceptance
remain separate unresolved requirements.

The local AppKit isolation experiment and earlier failures are detailed in
`WORK_STATE.md`. It also combines updates without Deskflow or a network path,
while preserving total point/fixed-point movement. That is diagnostic evidence,
not permission to weaken exact receipt requirements. Scrolling acceptance and
the roadmap remain open: three consecutive qualified same-artifact passes
have not been achieved. No attended test was launched; obtain permission
immediately before any future physical test.

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

## H06 attempt twenty-three: accepted writes, unavailable native readback — 2026-10-15

Approved run `build/h06-r23-uk2ivwf9/` used runtime `4854f84a9`. The hidden
display was independently verified by helper and Host, confirming the previous
readiness fix at its failed step. The next step accepted all three native frame
writes, then returned AX -25204 on all three immediate readbacks. It stopped
before Active or recording. The independent recovery process restored the
source to (400,188,640,448); held keys/buttons are zero. Both machines report
owned cleanup and ordinary-state preservation. Ubuntu relay errno 104, unknown
kernel tail and Mac SSH exit 255 remain recorded. This is a failed physical run.
Remote logs are collected under that root; archive SHA-256:
`ec8e7f0934b28121ac8eca23dde4448585cc4a2827823ee1c90d0b6a1b4dbee4`.

The placement follow-up adds native notification observation and queued,
coalesced readback, bounded by a monotonic two-second verification deadline.
It performs no sleeps, nested event loops or repeated native writes. Exact
owner, process birth, painted/produced geometry, permissions, physical layout,
holding-display identity, capture health and durable recovery remain required.
Cancellation removes queued observations before source return. Final placement
must agree in AX and WindowServer and remain contained in the original holding
display. Unsupported notifications cannot substitute for fresh native reads;
the existing owner watchdog can request verification within the same deadline.

Evidence root: `build/placement-verify-vjmz7mbg/`.

- Final Mac Host/helper/native build: PASS. Placement, holding, startup and
  native-recovery CTests: PASS (4/4, 1.22s).
- New portable `windowshare-native-placement` tests cover asynchronous success,
  transient readback, notification coalescing, no busy retry, late timers/probes,
  cancellation/destruction and changed owner/paint/display after yielding.
- Existing `windowshare-holding` relocation assertions use the same production
  policy after its asynchronous refactor.
- Scoped ASan/UBSan passes on Mac and actual jack's isolated Ubuntu image
  (`native-placement-_zqnef_p/` under the established remote test root).
  Qt/system libraries are not instrumented. These tests use no display/input.
- Prior diagnostic commit `60b074160`: all six hosted jobs pass in
  [run 38839385272](https://github.com/JackHars/deskflow/actions/runs/38839385272).
  The current placement code still requires its own hosted/physical validation.

Placement runtime: `1f489fce0acdb0863e0951f1f07d2be604a97621`.
Signed package audit: PASS, 77 exact-library records, no errors. ZIP SHA-256
`e18af5dd3668e92086a84e77d7104c1c335d79667ccf36894ade2e0f96905e69`.
Hosted source bundle `38841354603`: all 1179 tracked files, modes and Git blob/
commit identities independently verified. Source archive SHA-256
`27557b5898702bad6d2a74e8dd3c9e4a589c739ac0be5154bce679b7e7480a00`.
Foundation run `38841354672` subsequently passed all six jobs; see attempt 24 above.

Attempt 24 is prepared at `build/h06-r24-vtm4s8z9/`; frozen plan validation,
Mac permission/conflict checks and packaged admission pass (11.489s, hooks off).
Jack's actual local Wayland session 2 is active, provider disabled. Holding and
separate controls are configured; there is no Mac desktop preview/results popup.
Attempt 24 subsequently launched and failed as recorded above. Its permission is consumed; do not relaunch this plan.
The acceptance checklist remains open in `CHATGPT_HANDOFF.md`.

## Roadmap follow-up: deadline-order regression — 2026-10-15

The asynchronous display startup now uses absolute monotonic deadlines in
addition to QTimer wake-ups. A delayed started/reply/display callback cannot
win merely by running before an overdue timer. Verification must finish before
the same cutoff; early timer notifications and duplicates do not renew it.
The new portable suite runs the production gate with a controlled clock, without
sleep statements, real displays, helpers or input. Destructive completion and
late events after cancellation are included.

Implementation: `4854f84a9b345538a198ba8c3e0b1231a981f27f`.
Evidence: `build/roadmap-startup-3noptkp0/`.

- Mac Host/helper builds: PASS.
- `ctest --test-dir build/mac-default/src/apps/windowshare --no-tests=error
  --output-on-failure -R '^(windowshare-holding-startup|windowshare-holding|windowshare-native-recovery)$'`:
  PASS, 3/3 (1.85s). The root build directory has no CTest registration; the
  earlier command there failed with exit 8 and is retained, not counted as a pass.
- Scoped startup ASan/UBSan: PASS on Intel Mac/Qt 6.8.3 and jack's isolated
  Ubuntu/Qt 6.10.2 image. Prebuilt Qt/system libraries are outside instrumentation.
  Linux evidence is also retained in
  `/home/jack/src/deskflow-h04-20261015-zgta6i4f/roadmap-startup-qs1w7ixy/`.
- Hosted runtime `7453a4c77`: all six jobs PASS in
  [run 38836549724](https://github.com/JackHars/deskflow/actions/runs/38836549724).
  That run precedes this deadline change and does not certify it.
- Local Apple-issued `mac-package/DeskflowSeamless-Core-Experimental.zip`:
  exact-library audit PASS (77 records, no errors), SHA-256
  `9cea1103fcd178257edf91d5d9df12c9b361bfd2fc9a17c4d693cad5aaa13dad`.
- New source bundle `38838517071`: all 1176 tracked files/modes/blob identities
  and exact Git commit verify independently. Source archive SHA-256
  `7b967ee9f7d46a15c5129a601049c08801e2d765288de5377186ba1a7e903ad3`.
  New foundation run `38838517207` passes the startup checks but its Mac job
  fails the existing `windowshare-input-evidence` suite's 15-second CTest timeout.
  That run is not an all-green candidate. No stage output was retained by the
  old fixture. The diagnostic follow-up prints and flushes every case boundary
  and elapsed time, and preserves Mac CTest logs on CI failure. Its local Mac
  evidence suite passes in 0.64s; workload, assertions and timeout are unchanged.
  Hosted revalidation is required; local success does not explain the timeout.

Attempt 22's unused plan was superseded. Attempt 23 was then authorized and
executed using this package, with holding enabled, separate Mac controls and
no preview/results popup. It verified the display but failed placement readback;
see the current attempt-23 entry above. No physical acceptance is claimed.

## H06 attempt twenty-one: native display notification race — 2026-10-15

`build/h06-r21-czr6qf_t/` consumed fresh permission and failed before Active.
The helper saw its verified 4096x4096 display, while the blocked Host immediately
read an inactive 1x1 record for the same ID and stopped the helper. Native source
return and final cleanup succeeded; Ubuntu relay/controller and Mac SSH failures
remain recorded. The user reports the test closed after Start. No results popup
was launched and H04/H06 are not accepted.

Production display creation now returns to the event loop and completes only
after independent parent verification, driven by helper/display notifications.
Existing deadlines, exact identity/layout checks and write-ahead recovery remain;
helper loss/cancellation fences late callbacks. No sleeps or busy polling are
added. Native observed/error-code rows now reach the combined diagnostic log.
Evidence: `build/h06-startup-ra7c2l2d/`. Mac builds, all 84 coordinator cases on
both machines, four Mac Server/coordinator/preflight/native CTests, and the default
non-mutating holding suite pass. The new startup cases reproduce the stale
parent state and exercise notification completion, cancellation and deadline.
The pure drag-transaction tests pass. Physical display creation and hidden
placement still require fresh permission.

## H06 attempt twenty: placement diagnostics — 2026-10-15

`build/h06-r20-8g88ufzd/` consumed fresh permission and passed Ready/Start with
settled source and control windows. The holding display was independently
verified, but source placement failed before Active. The exact failing native
predicate was not recorded. Initial return failed; final cleanup restored the
original frame, zero held input and an empty journal. Both machines stopped
owned processes and preserved ordinary state. Ubuntu relay/controller errors
and Mac SSH exit 255 remain recorded; H04/H06 did not pass.

The user now wants printed diagnostics for the agent, without a results popup.
Automatic report launch is removed. Native logs now bracket each placement and
restore AX operation, retain error codes and frame readbacks, identify failed
guards and name the relocation transaction stage. No admission checks are
relaxed and no sleeps are added. Evidence: `build/h06-placement-e2j2nzqs/`.
All 83 coordinator cases on Mac and actual Ubuntu, four Mac Server/coordinator/preflight/native
CTests and the default non-mutating holding test pass. Its existing relocation
fixtures now distinguish pre-move, move, post-move and containment failures.
Rebuild/freeze fresh artifacts and obtain fresh permission before a physical
retry; these unattended checks do not establish native placement success.

## H06 attempt nineteen: opening-window regression — 2026-10-15

`build/h06-r19-209et7ms/` consumed fresh permission and stopped before Start.
The saved timeline identifies a two-pixel opening-frame transform that made the
fixture's composite visibility flags false after its first settled receipt.
No native sharing session, holding display or evidence recording started.
Source returned to its original frame with zero held input, and both machines'
ordinary state is preserved. The independent report was launched after cleanup;
the complete first failure and cleanup remain in `session-1/diagnostics.jsonl`.

The test fixture now disables only its own automatic presentation animations,
logs raw geometry/visibility observations, and treats pre-Start presentation as
readiness. Exact window identity is checked throughout; fresh settled visible
windows are required after Start, and active controls must remain stationary.
No sleep statements were added. The Mac source builds and all 83 coordinator
tests pass on both Mac and actual Ubuntu, including four new regressions.
Evidence: `build/h06-presentation-1xwwf524/`. This is not physical acceptance.
Prepare a fresh run and obtain fresh permission immediately before launching.

## H06 attempt eighteen and step diagnostics — 2026-10-15

`build/h06-r18-i25moopp/` was launched with fresh permission and failed. The
holding helper's display failed independent geometry verification; the source
changed from (400,190,640,448) to (9,50,640,448). The later private-channel loss
obscured that initial error in the coordinator result. No recorder armed, source
restoration failed, and H04/H06 remain unaccepted. Mac processes stopped and
ordinary settings match; Ubuntu owned processes stopped and desktop/settings
were preserved despite Core/controller exit 1. See `WORK_STATE.md` and the
unchanged run receipts for the complete failure and cleanup record.

The next attempt must use the rebuilt diagnostic source/Host/helper and fresh
plans. `diagnostics.jsonl` records every lifecycle stage, readiness transitions,
native holding verification details and cleanup. This revision also launched an
independent read-only report; the later log-only request above removes that launch.
No sleep statements or artificial display delays were added. H06 must verify
the committed hidden placement before Active; ordinary source movement is not
accepted as hiding. Display creation now arms recovery before its first possible
source movement, including failed creation. Obtain fresh permission immediately
before launch; no physical pass is inferred from automated diagnostics tests.

Unattended checks: 79 Mac coordinator cases, 31 Linux lifecycle cases, 9 desktop
cases on actual Ubuntu (8 + 1 platform skip on Mac), the report UI offscreen on
both operating systems, native source rollback, and Mac Server/coordinator/preflight CTests. Evidence:
`build/physical-diagnostics-fxxw1od6/` and `build/physical-diagnostics-*.log`.

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

## H04 hidden desktop and Record readiness — 2026-10-15

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

## H04 native stage invalidation during priming — 2026-10-15

Attempt ten (`build/h04-r10-f_dfkh1x/`) ran after fresh explicit launch permission.
It reached Active and priming, with no Record request or armed recorder. The user
reported "scrolling i think" before closure. Ubuntu Core's first failure was
`GNOME stage observation substituted its exact native binding`, request 29,
followed by viewport closure, TLS relay EOF and `linux exited early: 1`.
No Mac heartbeat expiry occurred; maximum Host/Core/source readiness gaps were
290/111/147 ms. The short failed run cannot establish sustained responsiveness.

All recorders ended denied/unarmed, loss 0 and zero measured rows. The collected
final source receipt contains 889 Qt and 363 native events without gaps and the
original frame, but retains the routing Right key without an observed release.
Neutral input is unverified. Exact owned processes, container and sockets are
gone; ordinary settings/processes were preserved. Ubuntu's failed private IPC
stop and terminated Core remain recorded. **Physical acceptance is unassessed.**
Copied Ubuntu archive SHA-256:
`af310cb019c3ce76235c6537adf72db416857b1b6db6171ce7937b0d13a4ddc2`.

The provider's negative stage reply legitimately carries a newer native revision
after invalidation. The client now denies that observation without terminating
the authenticated session. A fresh request must establish a new observation;
success still requires the exact requested revision and two same-view updates.
Malformed schema, substituted identities/points, backward revisions and empty
or contradictory reasons remain terminal. No old observation is renewed and
no input-delivery authority comes from a denial.

Evidence: `build/h04-stage-f1bz5dzq/`. Real private D-Bus regressions reproduce
the previous shutdown on Mac and Ubuntu. With the repair, both signal-delivered
and signal-not-yet-delivered invalidations reject that request and allow a fresh
revision; malformed denials and substituted successful replies still close the
channel. Mac provider-client suite: PASS, 6.39 s. Ubuntu normal and ASan/UBSan
provider-client suites: PASS, 4.28 s / 6.13 s. Production Linux Core build: PASS.
Isolated GNOME 50.1/QtWayland: PASS, 12.14 s, clean production-client stop,
no original-delivery authority and no user desktop. Temporary build/container
cleanup, formatting, whitespace and full REUSE pass. Copied Ubuntu archive
SHA-256: `3583d39ff93b0b2e20b470dd18276a8bb65136ecb419648170c13ab515d457db`.
Attempt eleven (`build/h04-r11-b4be7iux/`) subsequently ran with the repaired
Linux Core; its failed result is above. These regressions do not establish
physical readiness or repair every possible native disconnect.

## H04 Host stall and TLS input-burst regression — 2026-10-15

Attempt nine (`build/h04-r9-0q_whncg/`) ran only after fresh explicit launch
permission. The user confirmed Active and reported clicking Record and
repeating input before closure. Record was requested at 13:23:13 local, but
Core/Host never published readiness and none of the recorders armed. Host
reported a 3239 ms heartbeat age against the unchanged 2500 ms limit; its
readiness publications have a 3229 ms gap while Core/source remain responsive.
The native stack causing that stall was not sampled. All measured row counts
are zero, all terminal losses are zero, and physical acceptance is unassessed.

All 1736 Qt receipt events were collected without gaps. The routing Right key
still lacks an observed release; source geometry was restored but neutral
input is unverified. All exact owned processes and forwarding sockets stopped,
ordinary settings/processes were preserved, and failed Ubuntu IPC/relay/Core
shutdown evidence remains retained.

The TLS reader's 64 KiB batch previously dispatched every decoded packet before
yielding, allowing a burst of costly native callbacks to starve other channels.
It now limits each turn by bytes, packets and elapsed work, leaving remaining
bytes in existing bounded socket/decoder buffers. A real TLS regression fails
on the previous implementation and passes with the change. It verifies fair
continuation for both large bursts and a slow callback, packet order, complete
delivery, and refusal to revive expired authority after yielding. A single
blocking native callback remains a limitation, and physical readiness still
requires verification.

Evidence: `build/h04-dispatch-_wdtca1j/`. Mac transport/Bridge/viewport suites
pass (2.31 s / 12.63 s / 2.63 s); home Ubuntu passes the same suites (16.92 s),
core ASan/UBSan (0.17 s), and isolated GNOME/QtWayland (12.70 s). The isolated
fixture reports a clean client stop and no original-delivery authority; it
uses no user desktop or physical input. Formatting and full REUSE pass.
The Apple-issued exact-library Mac package verifies, and packaged admission/
bootstrap smoke passes with native input disabled. Attempt ten subsequently ran
under `build/h04-r10-f_dfkh1x/`; its failed physical result is recorded above.

## H04 visible input and Ubuntu disconnect diagnostics — 2026-10-15

Attempt eight (`build/h04-r8-5bjw64xh/`) stayed visible beyond three minutes,
accepted the user's Start and reached Active. The user confirmed that typed
characters appeared after the routing hotkey. Ubuntu then lost its viewport
channel during preparation inventory request 68, before Record/arm. All three
recorders ended unarmed with loss 0; this is not original-input acceptance.
The source retains a Right-arrow press/autorepeat without an observed release,
so neutral-state restoration remains unverified. All exact owned processes
stopped and ordinary apps/configurations were preserved; failed private IPC
stop/relay EOF remain in the retained Ubuntu cleanup evidence.

The Viewer now retains the first bounded, single-line endpoint close reason
and logs it before synchronous Bridge shutdown can discard it. Regression
coverage exercises EOF, descriptor retirement, exactly-once notification and
reentrant/later cleanup preserving that reason. This diagnostic change does
not establish the initiating cause or fix the physical disconnect.

Mac viewport/Bridge suites pass (2.73 s / 9.50 s). Home Ubuntu Debug build and
the same suites pass (1.19 s / 12.12 s); its isolated GNOME 50.1/QtWayland
fixture passes in 12.40 s with a clean production-client stop. Synthetic fixture
input has no original-delivery authority and never touches the user's desktop.
The diagnostic container and temporary build tree were removed. Formatting,
whitespace and full REUSE pass. Evidence: `build/h04-viewport-elrcr6hw/`.
Retained earlier build attempts include a missing optional fixture flag and a
Release-only GCC `maybe-uninitialized` error in unchanged fixture code; the
passing Debug run does not verify the Release fixture build.

The user is away. Ask for fresh permission immediately before the next physical
test and wait for an affirmative reply before opening its window or starting
its session. Previous Start/Ready replies do not authorize a new attempt.

## H04 Ready wait and source visibility — 2026-10-15

Attempt seven failed before Start (`build/h04-r7-ptwmxdfb/session-1`): the
native recorder's three-minute preparation limit was shorter than the new
five-minute Ready countdown. Its terminal evidence has Deadline loss 9 and
zero rows. All owned processes stopped; exact remote cleanup preserved the
ordinary apps. The user reported that the already-closed window was not visible.

The native fixture now has an explicit absolute ten-minute preparation budget;
Core/Host retain three minutes and measured duration remains unchanged. Tests
exercise the actual timer at 180001, 300001, 599000 and 600000 ms, verify default
180000 ms expiry, reject invalid/non-native overrides and require an unarmed
terminal record. Mac recorder tests pass (0.78 s), and the home Ubuntu
ASan/UBSan recorder suite passes. All 63 Python coordinator checks pass,
including clean native-only pre-Start cancellation and retention of deadline
failure. The window requests foreground once; initial production readiness
also checks its own on-screen and non-minimized state. Human visibility still
requires observation. Evidence: `build/h04-ready-u_nmzsoi/`; next fresh run:
`build/h04-r8-5bjw64xh/`. This is a harness repair, not physical acceptance.

## H04 concurrent Event preparation repair — 2026-10-15

Attempt six: **failed preparation; no physical acceptance**. The user clicked
Start in the new countdown window. Evidence under
`build/h04-r6-e1zqbfc2/session-1/` identifies Core recorder loss `Contention`,
site `queue-mutation`, queue `event-all-outstanding`. Both real packaged
endpoints had authenticated and passed exact admission. The inert probe had
also passed (7.697 s admission / 12.632 s total). Linux startup was cancelled
before Active or recording. The Core and source exited 0, exact Host stopped,
Linux Core stopped through private IPC, relays drained, and container exit was
0. Ordinary Mac/Ubuntu processes and settings were preserved. Unarmed evidence
remains denied; the attempt-four active disconnect is not diagnosed by this run.

Event observations from multiple connection workers can legitimately overlap.
Before recording, they now update the exact live depth with one atomic
operation instead of taking the measured counter lock. Their full observation
activity excludes arm/drain. Only an empty, quiescent baseline resets measured
counters; measured rows, strict ordering, one-shot rejection and all recording
contention checks are unchanged. Underflow/capacity/boundary failures remain
terminal. No preparation payload or artificial measurement is recorded.

Evidence under `build/h04-event-preparation-aawd8i2l/` retains an eight-worker,
200-round synchronized observer regression: exact occupied/empty phases,
no pre-arm rows, fresh measured peaks, underflow rejection and refusal to arm
with unfinished observation activity. The same test fails against the prior
header and passes against the repair on home Ubuntu with GCC ASan/UBSan.
The fixed recorder and private-channel suites pass together in 23.68 s; the
offline container and temporary build directory were removed. Mac recorder
tests pass in 0.66 s and all 30 integrated EventQueue checks pass in 17.91 s,
including a separate eight-worker real-dispatch regression. Core, Host and
the source build; formatting, whitespace and full REUSE pass. Runtime
`fbd1212b95e8f30e21e19b437fe748bba2299e6e` is pushed. The Apple-issued
exact-library package verifies; ZIP SHA-256 is
`8ab3bc026e2836fa993de5be94e6b36bbba606edc9fcc21d627e9d2a7cf02dfb`.
Attempt seven is staged under `build/h04-r7-ptwmxdfb/`.

Hosted run `38767970888` on the previous countdown commit passed five jobs
but failed Linux companion diagnostics: Qt's non-UTF-8 locale warning came
from the wrapper itself. The test had incorrectly required its entire stderr
to equal the Host marker. It now requires the unique child marker exactly
once when forwarding is enabled and absent otherwise, with stdout still
discarded, while forcing `LC_ALL=C`. This changes the test, not forwarding.
The Mac suite passes in 11.93 s and home Ubuntu GCC ASan/UBSan in 6.57 s with
that locale; logs/source snapshot are retained with `locale-` prefixes.

## H04 reached Active, then lost its private channel — 2026-10-15

Fourth attended attempt: **failed session; physical acceptance not assessed**.
Evidence is under `build/h04-r4-aa2qy2tm/session-1/`, with remote failure logs
under `build/h04-r4-aa2qy2tm/linux-collected/`. The user confirmed Active.
The inert packaged probe passed (7.641 s admission, 13.650 s total); real
admission and the Linux TLS/native endpoints subsequently connected. During
priming the Host stopped with `Trusted companion private channel lost authority`.
Core/Host readiness never became true, no recorder armed, and all three ended
with loss 0 / denied state. No input delivery or H04/P5 pass is inferred.

Core exited 0 through private IPC, its exact Host stopped, and source exit was
0. The fresh final source receipt records held keys/buttons 0 and the original
frame restored. Ordinary Mac and Ubuntu configurations were preserved. The
Linux relay failed when TLS ended before coordinated stop; its Core required
termination (-15), and its controller/container returned failure. Separate
cleanup confirmed the exact owned container and sockets gone. These failures
are retained rather than treated as clean coordinated shutdown.

Mac runtime: `0f77188a811eceeff70d9ebbf997fea74fa878c8`. The physical Linux
package remains `286328d055478692ea877c5d5d61d5c619e5093f`; the newer recorder
source was separately tested in an offline Ubuntu container, not substituted
into this physical run. `source-binding.json` records that distinction.

The original reason was not retained by the session callback and Host stderr
was discarded. The diagnostic follow-up forwards stderr only for an explicit
evidence session and logs bounded close reasons after revocation begins.
The direct inherited descriptor adds no QProcess output buffer. The subprocess
regression checks default silence, explicit stderr forwarding, discarded stdout,
default-deny authority and normal authenticated child cleanup. Verification
under `build/h04-channel-20261015-qo_wfxih/`:

- PASS: Mac Core/Host build and companion suite (11.80 s).
- PASS: home Ubuntu GCC ASan/UBSan companion suite (6.60 s), including the real
  two-level subprocess output checks. The offline container and temporary
  source/build tree were removed.
- PASS: formatting, whitespace and full REUSE (1133/1133 files).
- Recorder commit `0f77188a8`: all six hosted jobs in run `38763970033` passed.
  This hosted result precedes the diagnostic follow-up.

Diagnostic runtime **`efb9398b9a488113cca8b7b02351f02d2e7d9b8b`** is packaged
and pushed. The Apple-issued exact-library bundle verifies successfully; ZIP
SHA-256 is `e209d8c5a31b365b8a5c5163fd44d9da28905695cfd582d121cb0a7dfc05fe1d`.
The next run's inert admission probe passed (9.372 s admission, 15.213 s total,
input disabled, companion stopped). Attempt five, under
`build/h04-r5-u7ubid0p/session-1/`, then timed out waiting for the visible Start
button. No physical route or recording started. Source exited 0, the remote
controller/container completed without start, exact local processes stopped,
and ordinary Mac/Ubuntu processes and configuration hashes were preserved.
Its evidence-stop timeout remains in `cleanupErrors`; absent unstarted roles
cannot supply terminal evidence. This is not a successful recording or a
reproduction of the earlier active-session failure. Source export run
`38766497090` passed; hosted checks `38766497148` were still running when recorded.

After the user confirmed the idle closure, the Ready wait was extended from
90 to 300 seconds and made visible. It is independent from the unchanged
120/150-second active profiles, 30/120-second recordings, native authority
deadlines and 600-second total coordinator watchdog. Linux already permits
600 seconds in prepared state before native startup. The native source clamps
the local Ready countdown and the collector permits the larger receipt value
only in Ready. H04 evidence-mode instructions now include Record and waiting
for recording completion; legacy input-only instructions remain available.
The source build and all 60 coordinator regressions pass. The four new checks
cover idle expiry, non-renewal, independent late-Start active budget and
state-specific bounds. Logs are `ready-coordinator-tests.log` and
`source-ready-build.log` in the diagnostic evidence root.

## Recorder publication overlap repair — 2026-10-15

Third attended H04 attempt: **failed setup, physical acceptance not assessed**.
`build/h04-r3-spns1sz_/session-1/` records a successful inert admission probe
(6.931 s inspection, 12.247 s total), followed by Core recorder `Contention`
during real preparation. No recorders armed and Linux native input did not start.
Core/source exited 0; the exact companion and owned container stopped; ordinary
Mac/Ubuntu configuration hashes were preserved. Source movement during setup
remains recorded as a restoration failure. No failure is trimmed into a pass.

The recorder's scope-long counter lock could collide with a legitimate SPSC
consumer after publication. The three native rings now commit counters before
publishing availability/free space, while a separate atomic activity guard
covers the whole native operation. Start and terminal checks require both
empty counters and no in-flight activity. Genuine contention, ordering loss
or a racing measurement boundary still denies diagnostics. The first failure's
site and queue are published atomically; the coordinator now names the role
and details instead of only reporting generic readiness failure.

Evidence under `build/h04-recorder-20261015-ayzo9bsd/`:

- PASS: Mac recorder regression reproducing producer/consumer publication
  overlap, exact intermediate depths, incomplete start and delayed drain.
- PASS: Mac private-channel suite and four ASan/UBSan input, routing, native
  field and capture suites. Core, Host and source fixture build successfully.
- PASS: home Ubuntu GCC ASan/UBSan recorder and private-channel suites
  (23.81 s combined), 58 coordinator and 40 normalizer tests. The container used
  `--init`, no network or desktop/device mounts; its temporary tree was removed.
- PASS: separately signed Apple-issued exact-library package verification.
  The initial package invocation lacked its Qt tool path and stopped before
  creating output; the corrected invocation used the build's Qt 6.8.3 tool.
- Prior runtime `58ab63674`: all six hosted jobs in run `38760298734` passed.
  This hosted result does not cover the subsequent recorder repair.

The fourth physical session reached Active but failed as recorded above.
H04/P5 and later attended gates remain open.

## Attended setup failures and startup/cleanup repairs — 2026-10-15

The user confirmed they were at the Mac and clicked Start in two fresh H04
sessions. Neither reached Active or armed evidence. These are **failed setup
attempts, not H04 or P5 acceptance**. The first generic ten-second readiness
wait ended before production exact-library admission completed. Its SSH
connection also dropped during the move from WiFiman to the home LAN. Ubuntu
`jack` was subsequently reached directly at its existing address; owned remote
cleanup and ordinary configuration preservation were verified. The second run
observed an explicit 15-second code-admission timeout with both runtime
components present. Both attempts retain the source's changed frame rather
than claiming restoration of a window the user moved during setup.

Evidence: `build/h04-20261015-h9zilmyj/session-1/` and
`build/h04-retry-20261015-91731y1_/session-1/`. The second run confirms Core exit
0 through private IPC, its exact companion stopped, Linux container exit 0,
ordinary Mac/Ubuntu configuration preserved, and source exit 0. The first
source required termination because its manual-close veto rejected the
coordinator's final `quit()`; that failure remains recorded.

Repairs and verification:

- Explicit native `close` after terminal evidence now ends the main event loop
  directly. Manual window close still requests restoration. The regression
  rejects ordinary Quit, stops an unarmed recorder, requires its terminal row,
  then verifies explicit close exits successfully without inventing recording.
- The one-shot exact-library inspection budget is now 30 seconds, within its
  existing maximum. Both endpoints retain independent channel liveness and
  all code/identity checks; pending, expired or failed inspection supplies no
  authority. No worker retry, budget renewal or queue was added. The physical
  coordinator observes the 15-second child startup plus 30-second admission
  and five seconds of scheduling grace.
- `--windowshare-code-admission-probe` exercises the actual packaged Core and
  Host without input hooks, ordinary Core startup, exported windows or network
  listeners. It reports elapsed times and clean companion shutdown. Physical
  setup requires this unattended diagnostic before showing Start, then checks
  the new physical endpoints independently. CI's ad-hoc package must reject
  exact admission while still completing diagnostic cleanup.
- PASS: local Mac evidence, bootstrap, code-admission, companion and peer-trust
  suites; 56 coordinator regressions; formatting, whitespace and full REUSE
  (1133/1133 files). Core, Host and the source fixture build locally.
- PASS: home Ubuntu GCC ASan/UBSan evidence, bootstrap, code-admission and
  companion suites plus all 56 coordinator regressions. `linux-final-init-results.json`
  retains the exact source archive hash and four successful steps. The offline
  container and temporary directory were removed. Use Docker `--init`: without
  it, the nested parent-exit fixture left a zombie under Python PID 1 and failed
  its disappearance check. Earlier attempts also retain a GCC `-O1` custom
  allocator warning and missing Clang sanitizer runtime; the final GCC `-O0`
  sanitizer run needs neither a warning suppression nor package installation.
- PASS: the new Apple-issued exact-library package's inert admission probe,
  private handshake and invalid-option rejection smoke. A separate ad-hoc copy
  passes the inverse check: admission is rejected, input stays disabled and the
  companion exits cleanly. Final signed ZIP SHA-256:
  `c7d8aaae04afbd54c3edbecfa0a8c79d21ee528be1a0f2199372c8740b74cfa5`.

The signed candidate package, logs, source snapshots and diagnostics live under
`build/h04-retry-20261015-91731y1_/`. Earlier successful seven/eight-second
diagnostics and a later 15-second expiration show why one fast probe did not
establish reliable startup. Physical input, dragging, recovery and installation
acceptance remain open; full upstream native/OS queue capacity is still uncertified.

## H11 physical recording profile — 2026-10-15

Collection/source-fixture commit **`286328d055478692ea877c5d5d61d5c619e5093f`**
offers a frozen `h11` profile: a 120-second recording
inside a 150-second visible session. The existing H04 profile remains 30/120
seconds. Every role receives the same predeclared duration; the source,
receipt collector and status publisher enforce the selected session cap.
The Linux controller retains its 180-second active / 270-second resource caps.
Late arm, early Stop, wrong profile, partial arm and owner changes cannot yield
a completed recording. See [the procedure](deploy/windowshare/original-input-physical.md).

Local evidence under `build/p7-h11-profile-20261015/`:

- PASS: rebuilt `windowshare-smoke-source` on the Intel Mac / Qt 6.8.3.
- PASS: 53 Mac coordinator, 23 Linux coordinator, 40 evidence normalizer and
  35 H11 analyzer tests (**151 total**). The complete evidence coordinator suite
  exercises both durations; a 30-second terminal row cannot complete H11.
  Validate-only checks launch neither native processes nor SSH.
- PASS: full-repository REUSE 3.3 lint, 1132/1132 licensed files, no errors;
  C++ formatting and whitespace checks. Runtime Core/Host code is unchanged
  from `8132c8554`; the source fixture and collection tools changed.
- Read-only preflight: home Ubuntu `jack` is reachable; session 1 is active
  Wayland on kernel `7.0.0-34-generic`. The existing frozen Ubuntu image is
  available. The staged Mac package's native `--probe` reports Accessibility,
  Screen Capture, holding display, exact window identity and targeted pointer
  support. No input routing or physical test started.

Home Ubuntu verification on that exact source also **PASS**: nine enabled Core
suites (20.35 s), one evidence-runtime suite, private GNOME-bus and Xvfb
startup/shutdown checks, and five disabled Core ASan/UBSan suites (20.45 s;
leak detection disabled). All 151 Python tests pass there too. The offline
container used the existing frozen Ubuntu image with no network, host desktop
mounts or devices; its temporary build directory and container were removed.
`linux-results.json` records all 13 steps and the exact source/binary hashes;
`linux-verification.json` confirms every step succeeded. The desktop preflight
records GNOME 50.1, libei 1.5.0 and Qt 6.10.2, separately from this private run.

The [source export](https://github.com/JackHars/deskflow/actions/runs/38750248860)
matches the raw commit and all **1160** tracked entries/modes/blob identities.
Source tar SHA-256:
`f0ed021f99f8cbfd33e3a3915f88aabedafea246eac4826ba32638bbcb3d7bd7`.
The downloaded Linux archive's provenance, all manifest members and both
x86_64 ELF architectures verify without launching them; SHA-256:
`2c73eca2062538a24711c4a9f04f9db6eb71980cab34018a2c42f040d1f3d5f9`.
The locally rebuilt, separately ad-hoc-signed Intel source helper is retained
as `build/p7-h11-profile-20261015/windowshare-smoke-source`, SHA-256
`d648cd6ff021dc4dfa2feffbb893f715c39abd0080e5ce909e519ddef38f2864`.
Its signature verifies; it has not been launched for physical testing.

Hosted [run 38750248886](https://github.com/JackHars/deskflow/actions/runs/38750248886)
**PASS — all six jobs** on `286328d05`. Mac passes 27 standalone, nine integrated,
four disabled and 19 private/native sanitizer suites. Linux passes 22 standalone,
11 integrated, five disabled and one EventQueue sanitizer suite. Both compiler
foundations, transport and native GNOME/Qt jobs pass. `ci-final.json` verifies
the exact commit and all six job conclusions; `ci.log` retains their results.
The downloaded ad-hoc Mac artifacts also verify source/checksums, archive
inventory and ARM64 executables (minimum macOS 15.0), without launching them:

- Core ZIP: `a04c3fa89a7855938daca12c35e50ef9389be199616f11e6297f605a1d906977`.
- Viewer ZIP: `e59fdecfd57ab0cf6b09575923dc0e69093a03a2585434b886c6cf91ea838478`.

The existing local Intel Apple-issued Core/Host package remains the staged
`8132c8554` runtime; its production code did not change in this collection
follow-up. The new standalone source helper has its own separately frozen hash.

These are collection and preflight results, **not** physical acceptance. P5
bidirectional dragging, P6 application behavior, P7 H10/H11 and P8 installation
acceptance still need the applicable H tests. The native queue audit still
identifies unbounded upstream queues and unknown OS/device capacities.

## Independent Core dispatch expiration — 2026-10-15

Runtime **`8132c85540b6c3f7f07acaeae0f61c98854df09d`** enforces the existing two-second event residence
deadline even when a handler never returns. An independent thread scans 64
fixed dispatch slots every 25 ms, subject to scheduling. Each slot retains the
original admission deadline; nested and concurrent callbacks cannot refresh
it. An overdue active scope exits only Core with code 71, without invoking a
revocation callback, logger, Qt or native destructor. The guard outlives Core
thread/queue shutdown, and ordinary launches create no watchdog thread.
See [input-queue-budget.md](deploy/windowshare/input-queue-budget.md).

Local results in `build/p7-core-dispatch-20261015/`:

- PASS: Core builds with `BUILD_WINDOWSHARE=ON` and `OFF`.
- PASS: EventQueue suite on both builds, **29 Qt entries each** (17.43/17.46
  seconds inside the test runner). Three directly owned child processes expire
  with code 71 under blocked, nested and concurrent callback load. Healthy
  idle/reuse, exception/destructive dispatch, cancellation and ordinary
  unguarded callbacks survive. Admission age, moves and slot reuse are checked.
- PASS: Server, ClientProxy and ServerProxy suites on both builds, plus enabled
  Core argument checks. Disabled ClientProxy has 41 intentional feature skips.
- PASS: EventQueue ASan/UBSan, 29 entries, 18.10 seconds inside the runner;
  leak detection disabled. All temporary ad-hoc-signed copies were removed.
- PASS: changed-file REUSE, formatting and whitespace checks.
- PASS: full Mac build and a fresh Intel Apple-issued exact-library bundle
  audit plus private Core/Host handshake with original input disabled. Staged:
  `build/p7-core-dispatch-20261015/mac-package/DeskflowSeamless-Core-Experimental.app`.
  ZIP SHA-256: `9f8b62634e4a8d2b2db08a5f9638812d230a97dddd6677c17f8b860a99802bbf`.
- PASS: [source export](https://github.com/JackHars/deskflow/actions/runs/38744933891),
  with independent verification of the raw commit and all **1160** tracked
  entries, modes/symlinks and Git blob identities. Source tar SHA-256:
  `4e8cc7751f7b47b648eb687612d63cb213e2accfcda8bf66305333b0f16b6968`.
- Retained initial harness failure: timeout children exited correctly, but a
  diagnostic `readAll()` consumed their output before the marker assertion.
  Fixed the harness and reran the full suite; `event-queue-on.log` retains the
  original three assertion failures, `event-queue-on-fixed.log` the passing run.

Hosted [run 38744933197](https://github.com/JackHars/deskflow/actions/runs/38744933197):
**PASS — all six jobs** on this exact commit. Mac passes 27 standalone, nine
integrated, four disabled-feature and 19 portable/native-private sanitizer
suites plus packaging checks. EventQueue takes 17.64/17.84 seconds in the enabled/
disabled Mac builds. Linux passes 22 standalone, 11 enabled, five disabled and
one EventQueue sanitizer suite; EventQueue takes 17.07/17.07/17.26 seconds.
Both compiler foundations, transport and native GNOME/Qt integration also pass.

Downloaded packages have independently verified source/checksums, inventories
and executable architecture; the Linux internal manifest verifies. No
downloaded executable was launched locally. The CI Mac archives are ARM64,
ad-hoc signed and declare macOS 15.0. The separate local bundle above uses the
Intel Mac's Apple-issued identity and exact-library policy, with minimum macOS
26.0 inherited from its bundled dependencies.

| CI artifact | SHA-256 |
| --- | --- |
| Linux x86_64 tar | `276acc8d0089501089356cd501fb16949c3288cfaf820c69ea014c1627ec4a93` |
| Mac ARM64 Core ZIP | `d616ea16db0e0736707ad5401525960350b68cd38803640d47331c3ca307668a` |
| Mac ARM64 Viewer ZIP | `e344b35e6a6b80931b2d9baa83cd318498c37b09d42759269c75e22454a7683a` |

CI artifact retention is seven days. For example:

```sh
gh run download 38744933197 \
  -n windowshare-linux-8132c85540b6c3f7f07acaeae0f61c98854df09d \
  -D /absolute/new/private/download
python3 deploy/windowshare/package-linux.py \
  --verify /absolute/new/private/download/DeskflowSeamless-Linux-Experimental.tar.gz
```

`candidate.json`, individual test logs/JSON, `ci-final.json`, `ci.log`,
`package-verification.json`, `source-verification.json`,
`github-linux-verification.json`, `github-mac-verification.json` and the Linux
manifest log retain the evidence under the local evidence root above.

These fixtures never start Core, a Host, an input source, a native window or a
desktop connection. Expiration closes private channels for existing disconnect
recovery, but does not prove physical restoration. Blocking startup, platform
buffer waits and teardown outside an active dispatch remain outside this
monitor. Native/physical H02–H12, full native queue certification, H10 crash/
sleep restoration and H11 input/display acceptance remain open. The unchanged
media hour remains a result for `d57e78a0d`, not this candidate.

## Previous candidate: Host progress recovery — 2026-10-15

Runtime **`e154f94e0676b299b225080658c3d5c3dfbf9f50`** adds an independent
progress monitor inside the experimental Mac Host. The existing recovery child
could observe Host death, but could wait forever when the Host's event loop was
blocked. Startup now has a 15-second deadline; owner-thread heartbeats renew a
five-second active deadline. Expiry exits only that Host with code 70 so its
existing child can reopen the exact recovery journal. Shutdown stops renewal,
including during nested Qt cleanup, and the guard outlives native destructors.
See [host-recovery.md](deploy/windowshare/host-recovery.md) for scope and limits.

Actual local results, retained in `build/p7-host-progress-20261015/`:

- PASS: full Mac application build; actual watchdog startup stall, event-loop
  stall, ineffective worker-thread heartbeat, healthy heartbeat, normal teardown,
  readiness, crash and unreaped-host recovery cases (32.74 seconds). Tests use
  real production deadlines and disposable marker-writing processes only.
- PASS: the same watchdog suite under ASan/UBSan (33.50 seconds; leak detection
  disabled). Temporary ad-hoc-signed executable copies were removed.
- PASS: Bridge shutdown/ordering suites using both OpenSSL and SecureTransport,
  plus native recovery-journal and forwarding-admission regressions.
- PASS: new Intel Apple-issued exact-library bundle audit and private Core/Host
  handshake, with original input disabled. The staged bundle is
  `build/p7-host-progress-20261015/mac-package/DeskflowSeamless-Core-Experimental.app`.
  ZIP SHA-256:
  `b3a8e04a6ee3097fa36bf44da97f40b5965d1b08753cf0dd0e9a0c57c725fd32`.
- PASS: [source export](https://github.com/JackHars/deskflow/actions/runs/38712601256)
  and independent verification of the raw commit, index, modes/symlinks and all
  **1158** Git blob identities. Source tar SHA-256:
  `a01b7be6d8902ac42d35510b9e28137ef28f0b52950b118aedf7b46fe99ba6ca`.
- Hosted [Mac/Linux matrix](https://github.com/JackHars/deskflow/actions/runs/38712601209):
  **PASS — all six jobs**, including the expanded watchdog suite on the hosted
  Mac (33.12 seconds). The Mac job passes 27 standalone, nine integrated, four
  disabled-feature and 19 portable/native-private sanitizer suites plus 21
  bundle tests. Linux passes 22 standalone, 11 enabled, five disabled-feature,
  one EventQueue sanitizer and 11 package suites; private GNOME/Qt/X11 passes.

Downloaded CI archives match the source commit, checksums, archive inventory
and executable architectures; the Linux internal manifest passes verification.
No downloaded executable was launched locally. These Mac archives are ARM64,
ad-hoc signed and declare macOS 15.0; the separate local bundle above uses the
Intel Mac's Apple-issued identity and exact dependency policy.

| CI artifact | SHA-256 |
| --- | --- |
| Linux x86_64 tar | `8337a305ed5a90277b29af50c54015a5fbaeb508c705bead681e977cacf7eb00` |
| Mac ARM64 Core ZIP | `0472305dc655fdc833ead17d8f41182f6c91b6762725a8b2e632c2d0ffe716a7` |
| Mac ARM64 Viewer ZIP | `886a2af25ca05c73c32f1c66ac60d7ef3335feff8257cba8231f6bd9f3e0eaa1` |

CI retention is seven days. Download the current Linux archive with:

```sh
gh run download 38712601209 \
  -n windowshare-linux-e154f94e0676b299b225080658c3d5c3dfbf9f50 \
  -D /absolute/new/private/download
cd /absolute/new/private/download
shasum -a 256 -c SHA256SUMS
```

`candidate.json` records exact source hashes; `local-tests.json`,
`sanitizers.json`, `package.log`, `package-smoke.log`,
`package-verification.json`, `source-verification.json`,
`github-package-verification.json`, `github-linux-manifest.log` and full
`ci.log` retain the evidence.
The previous synthetic hour remains a result for `d57e78a0d` below. Its media
decoder, Viewer, transport and load sources are unchanged, but the hour did not
exercise this new Host monitor and is not relabeled as an `e154f94e0` run.

Native and physical H02–H12 acceptance and full native/library/OS queue
certification remain open. The new fixture proves process expiration and
recovery-child dispatch, not actual window restoration, sleep/wake, or a blocked recovery process. No
physical routing, window relocation or sleep was started.

## Previous candidate: unattended media verification — 2026-10-15

Runtime candidate **`d57e78a0da972d29b9e71201c3efdae8c00fac18`** passes all six
[hosted jobs](https://github.com/JackHars/deskflow/actions/runs/38675705240),
local Mac native hardware/color/reset and scoped sanitizer checks, the isolated
Ubuntu application/Qt/Xvfb/load checks, and the complete synthetic hour below.
The earlier failures remain in the dated entries. No admission limit, recovery
deadline, resource budget or steady-workload rule was relaxed.

The decoder reuses one private hardware-download buffer for the same retained
hardware context and dimensions, and converts directly to opaque native RGB32.
Tests verify changing colors, retained images, resize and reset. Existing image
cache limits remain unchanged. Hardware counters distinguish real hardware
frames and buffer reuse from software fallback.

### Completed synthetic hour

The declared workload was two 1920×1080 streams offered at 30 fps for 3630
seconds, with 3600 seconds of passive CPU/RSS collection after warm-up.
No local builds, other tests, signing, debugger or memory probes were started
by this task during collection. This exercises actual loopback TLS, decoding
and offscreen Qt paint. It does not capture native source windows or measure
physical display/input latency.

| Check | Final result |
| --- | --- |
| Workload / fixed `pilot-20261013-v1` resource comparison | PASS / PASS |
| Resource samples / observed lower interval | 1801 / 3600.001405 seconds |
| Sampled peak RSS | 141,189,120 bytes (134.65 MiB); limit <1 GiB |
| Upper net RSS growth per hour | 10,948,603.73 bytes (10.44 MiB); limit <100 MiB |
| Upper average CPU | 100.001%; limit <200% |
| Hardware decoded frames, streams 1 / 2 | 108840 / 108839 |
| Reused hardware transfers, streams 1 / 2 | 108839 / 108838 |
| Overflow / recovery / decode errors / invalid timing tuples | Zero on both streams |
| Final queued frames and bytes | Zero on both streams |
| Maximum queued frames | 12 / 11, below the unchanged 16-frame limit |
| Workload completed Qt paints per second | 29.9829 / 29.9826 |
| Receive-to-Qt-paint histogram p95 / p99 upper bounds | 64 / 256 ms on both streams |
| Missed source offer slots | 46, retained in the report |

The local paint histogram includes warm-up and is not H11's end-to-end latency
boundary. The workload reports `performanceAcceptance: NOT_RUN`; the resource
evaluator also leaves authenticity/workload verification and `h11_complete`
flags unchanged. This pass is scoped to the declared synthetic workload and
resource comparison, not full native performance or an absence-of-leaks claim.

Evidence: `build/p7-completion-20261014/mac-transfer-rgb-hour/` contains the
predeclared plan, source/binary hashes, workload, raw samples, budget result and
independent verification. Trace SHA-256:
`2cb333f09f3ec69c76213d68c95401e4815b6dfe7bdf46f4e881ac6a24a77efd`.
The load stayed alive through the final sample. Both owned processes exited,
the temporary signed copy was removed, and all recorded runtime sources stayed
unchanged during the run. Reproduce with `deploy/windowshare/media-load.md` and
`resource-soak.md`; do not replace a full hour with the short comparison below.

### Verified delivery artifacts

The source [export](https://github.com/JackHars/deskflow/actions/runs/38675705333)
passes. Its raw commit object, index, archive modes/symlinks and all **1157** Git
blob identities match `d57e78a0d`. Package source commits, checksums, archive
inventory and executable architectures also match; the Linux internal manifest
passes `package-linux.py --verify`. No downloaded executable was launched locally.

| Artifact | SHA-256 |
| --- | --- |
| Source tar | `deb41b1f2a5d8abffa14e8c92cbf8f09eb566fa04a64322489d63f236ae18a99` |
| Linux x86_64 tar | `721395859a11bc24db71ab385f189da4dbe37447c85420677ce8a744d4dd7f88` |
| CI Mac ARM64 ad-hoc Core ZIP | `68363485b3668e2ec9953e561d0f90b34bfbe819c1adc89fac15ff93d57cb7f8` |
| CI Mac ARM64 ad-hoc Viewer ZIP | `1005c9b28c49c94a892e7ab95da440ed2b17fe516c1192f9180f407ab7c78b6f` |
| Local Intel Apple-issued exact-library Core ZIP | `ac9fc9e16f5d9f76e6bb11a18cfe5d75c19d2acff981cd700a8b663e71582f2a` |

CI artifacts expire after seven days. For example, download the verified Linux
package into a new directory, then check its recorded checksums:

```sh
gh run download 38675705240 \
  -n windowshare-linux-d57e78a0da972d29b9e71201c3efdae8c00fac18 \
  -D /absolute/new/private/download
cd /absolute/new/private/download
shasum -a 256 -c SHA256SUMS
```

The separate local bundle is
`build/p7-completion-20261014/mac-transfer-rgb-package/DeskflowSeamless-Core-Experimental.app`.
Its dependency/signature audit and private Core/Host handshake pass, with
original input disabled. It requires macOS 26.0 because of its bundled local
libraries and was tested on 26.1; the hosted ARM64 archives declare macOS 15.0.
Artifact verification JSON, complete CI logs and local package manifest/smoke
results are retained under `build/p7-completion-20261014`.

**Remaining:** full native/library/OS queue certification and applicable H02–H12
acceptance on this final candidate. H01 has automated build/package evidence;
installation and desktop capability acceptance remain pending. The user
explicitly deferred attended tests. Do not launch physical routing, relocation
or sleep tests until they are ready.

## P7 quiet-hour failure and decoder follow-up — 2026-10-14

The predeclared quiet hour on `6108bd2853c173ccba19b4913d95195a7f0e8110`
**fails both steady workload and resource comparison**. No local builds,
signing, other test suites, debugger or memory probe ran during collection.
The first recovery occurs at workload elapsed 1892.38 seconds. This reproduces
recovery without the earlier run's local test/probe interference; the exact
cause of each native delay remains unknown.

| Observation | Completed result |
| --- | --- |
| Resource samples | 1801 across the planned 3600 seconds |
| Sampled peak RSS | 690,970,624 bytes (658.96 MiB) |
| Upper net RSS growth | 522,240,554.63 bytes/hour (498.05 MiB/hour): FAIL |
| Upper average CPU | 108.97% |
| Stream 1 / stream 2 overflow and recovery counts | 70 / 68 |
| Maximum decoder-queue wait | 593.55 / 585.65 ms |
| Maximum decoder-worker elapsed time | 221.47 / 251.11 ms |
| Maximum GUI-completion queue wait | 36.88 / 93.54 ms |
| Final decode errors, pending frames and bytes | Zero on both streams |
| Retained recovery transitions / omitted transitions | 64 / 212 |
| Missed source offer slots | 42 |

The separate workload and budget reports, full stream counters, bounded
recovery timestamps and unchanged source/binary hashes are retained in
`build/p7-completion-20261014/mac-media-phase-hour`. The resource trace SHA-256 is
`68287cb52400933d2479cd8acb878a7901567c2c0b31c2f41622bd8e61d47fa9`.
The workload stayed alive through sampling; the owned children exited and the
temporary signed executable was removed. No threshold was relaxed.

All six hosted jobs on that same commit pass in
[run 38668714140](https://github.com/JackHars/deskflow/actions/runs/38668714140),
and source export `38668714196` passes. Downloaded package checksums, source
commit, archive inventory and executable architectures were independently
checked without launching the downloaded binaries:

- Linux x86_64 tar: `78989e88dae43a9c55fd144ff2e6a3e496f9ea9813b9dfe456b19591064ea565`.
- Mac ARM64 ad-hoc Core ZIP: `c8df492e6c4b5ad7d93560b293363667c122f98e0f9bf85f13c7fcb47436718a`.
- Mac ARM64 ad-hoc Viewer ZIP: `d899ff6a6d3a1d225250473ffa4b02a74a88052e99abaee0166c186eb57fef2e`.
- Local Intel Apple-issued exact-library Core ZIP:
  `dd30dd9e2655fe89b4707e15488f03cae63b34cf685d2bca2b01949d31df58ce`;
  audit and private launch handshake pass, with original input disabled.

A separate 70-second diagnostic run deliberately used `vmmap`, `heap` and
`sample`; it is not performance acceptance. Its profile confirms VideoToolbox
and repeated allocation/free work, including hardware-frame transfers and Qt
pixel conversion. The decoder previously discarded its CPU transfer buffer on
every hardware frame. The follow-up reuses one private buffer only with the
same retained hardware context and display dimensions. Reset/context/dimension
changes discard it; current source color metadata is read directly instead of
copying accumulating frame properties. Opaque output uses native-endian RGB32
to avoid the profiled RGBA-to-premultiplied conversion during Qt painting.

Before the RGB32 change, a predeclared five-minute transfer-reuse comparison
passes its workload with 151 resource samples: RSS 123.48 to 133.74 MiB, sampled
peak 133.93 MiB. Both streams decode 9892 hardware frames, reusing transfer
storage on 9891, with zero overflow/recovery and drained queues. This short
result is diagnostic only; **hour-long resource acceptance is NOT_RUN** for
that intermediate source. See `mac-transfer-reuse-short/diagnostic-summary.json`.
The final pixel-format candidate passes Mac Viewer tests, including the actual
native hardware reuse/color/resize/reset check, and the Viewer/mixed-load
ASan/UBSan run (`mac-transfer-rgb-sanitizers.json`; leak detection disabled).
`linux-transfer-rgb/result.json` passes the Ubuntu application build, eight
Qt/Xvfb/load suites, nine sampler tests, 15 budget tests, 16-stream steady load
and two HD streams. Its frozen archive SHA-256 is
`06168a5444104f1e4e28e0340d55218f3c2c285d062f3efbab651bcc6b7ee001`;
the offline fixture removes its temporary directory. No native GPU is exposed
in that container, so its hardware-specific branch is explicitly unavailable.
The final package, hosted matrix and long retest need separate results.

H07–H12 attended acceptance and full native/OS queue certification remain open.

## P7 completed hour and diagnostic follow-up — 2026-10-14

The first complete two-stream 1080p/30-fps synthetic hour **failed its steady
workload check** because stream 1 recovered from one decoder overflow. Its
resource comparison passed the unchanged `pilot-20261013-v1` limits:

| Observation | Measured result |
| --- | --- |
| Complete resource interval | 3600.000357 seconds; 1801 samples |
| Sampled peak RSS | 266,772,480 bytes (254.41 MiB) |
| Upper net RSS growth per hour | 98,742,262.21 bytes (94.17 MiB) |
| Upper average CPU | 105.73% |
| Steady workload | FAIL: one overflow/recovery on stream 1 |
| Decode errors and queued frames at exit | Zero on both streams |

`build/p7-completion-20261014/mac-image-reuse-hour/verification.json` records
these separate verdicts and the source/binary hashes. The raw trace SHA-256 is
`59100567dfdf3608b4e75bef614bf32166ed1a7a77c1cdbb3524ea5489886466`.
The workload remained alive through the final sample and its temporary signed
copy was removed. This combined sender/viewer fixture is only a diagnostic
resource comparison; it cannot establish native H11 performance.

Local builds, private-channel tests and one memory inspection overlapped that
run. The old workload report did not timestamp recovery, so its cause is
unknown. A predeclared 30-second control and 30-second run with three memory
inspections both passed; neither reproduces nor explains the earlier failure.
Their complete results remain in `media-recovery-diagnostics/result.json`.
The failed hour remains evidence and is not replaced by those shorter passes.

The workload now retains complete stream counters on invariant failure and up
to 64 timestamped recovery transitions. Explicit Viewer diagnostics also record
checked decoder-queue, worker and GUI-completion elapsed times in fixed storage.
These wall times include descheduling and native waits; they do not identify
CPU or GPU causes. Ordinary launches leave collection disabled. The strict
steady check still rejects any recovery; capacities and deadlines are unchanged.

Validation of these diagnostics:

- Mac Viewer tests pass, including exact phase boundaries and rejection of a
  regressed timestamp. Viewer and four-stream mixed load also pass ASan/UBSan
  with no invalid timing tuples; leak detection is disabled.
- `linux-media-phases/result.json`: Ubuntu application build, eight Qt/Xvfb/load
  suites, nine sampler tests, 15 budget tests, 16-stream steady load and two
  1080p streams pass. The isolated container removes its temporary directory.
  Source archive SHA-256:
  `e0c9203d3d278edb0a089a6445879a0720e444be6a5dae92400d3ae2dec48a44`.

One new hour is planned after all local builds, signing and tests finish, with
unchanged workload/limits and no local build, test suite, debugger or memory
probe during collection. Its result and hosted verification of this diagnostic
change must be recorded separately. Attended H07–H12 remain deferred.

## Final CI artifact verification — 2026-10-14

All six jobs pass on `affdad48bc93ce6b1c47272edc087130b0da0984` in
[run 38664125034](https://github.com/JackHars/deskflow/actions/runs/38664125034).
The intervening commits change CI retention, licensing and documentation;
runtime sources still match `bbb847796`. Source run `38664125025` also passes.
Both platform artifacts were downloaded and independently verified:

| Artifact in that run | Scope | Verified archive SHA-256 |
| --- | --- | --- |
| `windowshare-linux-affdad48bc93ce6b1c47272edc087130b0da0984` | x86_64; inert Linux archive, complete internal manifest and source commit verified | `8344a4d5e66ec039f89ca7e43abae6f36fd03f42262a50f538cdc3a19d51c842` |
| `windowshare-macos-adhoc-affdad48bc93ce6b1c47272edc087130b0da0984` — Core ZIP | ARM64; CI ad-hoc signature, minimum macOS 15.0 | `223792cd7dc86c4b3e9b428d5f9e706458c75cd30ea8c123952674437a9dac38` |
| Same Mac artifact — standalone Viewer ZIP | ARM64; CI ad-hoc signature, minimum macOS 15.0 | `a4204a46467edc7757cc74a5fdff618d221bacc633ba0ec3e3503622bd13cf09` |

Artifacts expire after seven days and contain `SOURCE_COMMIT` and `SHA256SUMS`.
Use a new destination directory, then verify before staging, for example:

```sh
gh run download 38664125034 \
  -n windowshare-linux-affdad48bc93ce6b1c47272edc087130b0da0984 \
  -D /absolute/new/private/download
cd /absolute/new/private/download
shasum -a 256 -c SHA256SUMS
```

No downloaded executable was run locally. The user's Intel Mac instead has the
separately rebuilt Apple-issued exact-library package recorded below; CI's
ARM64/ad-hoc archives do not replace it for attended production-input testing.
Full run logs, artifact identities and `github-*-package-affdad48b-verification.json`
are retained under `build/p7-completion-20261014`.

Run `38663112081` retained a genuine Linux provenance-step failure after its
builds/tests passed: Git rejected the container's differently owned checkout.
The correction scopes `safe.directory` to one read of `GITHUB_WORKSPACE` and
requires HEAD to equal `GITHUB_SHA`. No global Git policy is changed. The isolated
Ubuntu fixture verifies default rejection, exact-checkout success and rejection
for a different directory; see `linux-git-provenance-check.json`.

Five predeclared sequential Mac private-channel repetitions all pass, including
`captureEpochCutover`, with no retry-on-failure. `mac-channel-repeat/result.json`
retains every result and binary/source hashes. The earlier hosted epoch failure
was not reproduced; its missing original close reason is still unknown.
Attended tests remain deferred. The completed hour's resource pass and steady
workload failure are recorded above; they do not establish native acceptance.

## P7 current hosted and staged-package result — 2026-10-14

All six jobs pass for runtime candidate `bbb847796d361b0056178948b1cbf97cdd2e6e0f`
in [run 38661949281](https://github.com/JackHars/deskflow/actions/runs/38661949281).
This includes clean Mac/Ubuntu app and real Core builds, ON/OFF compatibility,
GCC/Clang foundations, private protocol sanitizers, real private GNOME/Qt/X11
fixtures, both platform packages and their noninteractive smoke checks. The
earlier failures are retained below. Production deadlines and resource limits
were not loosened to obtain the pass.

Source [run 38661949207](https://github.com/JackHars/deskflow/actions/runs/38661949207)
passes. The downloaded archive has SHA-256
`ef8748a34e3a4a2c2eb13e3f1288f596c43ad7a3e718719b2a5a8740d2a3d684`;
all 1156 entries, executable/symlink modes and Git blob identities match the
published commit. `build/p7-completion-20261014/github-source-verification.json`
retains that check; `bbb847796-source-match.json` also confirms all 987 runtime/
build files in the earlier Ubuntu frozen archive match this candidate.

The rebuilt local exact-library bundle is
`build/p7-completion-20261014/mac-image-reuse-package/DeskflowSeamless-Core-Experimental.app`.
Its Apple-issued signature/dependency audit and private Core/Host handshake
pass. The ZIP SHA-256 is
`3c7b33e920fca26e4ab692b4ea6384c32855626833610889aeb9f35eb17e7788`;
`mac-image-reuse-package-manifest.json` records individual executable hashes.
It is staged only; original input remains disabled in this smoke.

The workflow follow-up adds seven-day download retention for verified Linux
and ad-hoc Mac packages, with `SOURCE_COMMIT` and `SHA256SUMS`. Its actual run
and downloaded-artifact result are recorded above. The full synthetic hour's
resource pass and workload failure are also retained above. Native/physical
H07–H11 and install/upgrade H12 remain
pending; the user requested unattended checks and attended testing later.

## P7 decoder reuse and resource retest — 2026-10-14

The production decoder retains at most three unshared conversion images within
a 32 MiB cache. It never overwrites an image still owned by a returned result or
the GUI. Alternating-color tests exercise cache exhaustion, reuse and reset.
This addresses allocation churn observed during the first long offscreen load;
it does not change queue capacities or resource acceptance thresholds.

Evidence under `build/p7-completion-20261014`:

- `mac-first-soak-stopped.json`: incomplete first attempt, stopped at 1064 seconds
  after exceeding the 1 GiB diagnostic RSS comparison; peak 1,193,361,408 bytes.
  `mac-hour-resources.jsonl` and allocator diagnostics retain the failure.
- `mac-image-reuse-sanitizers.json`: Viewer and mixed four-stream load pass
  ASan/UBSan as separately signed copies; leak detection is disabled.
- `linux-image-reuse/result.json`: full standalone Ubuntu app build, eight
  Qt/Xvfb/load suites, nine sampler tests, 15 budget tests, 16-stream steady load
  and two 1080p streams pass. Archive SHA-256:
  `df0972a918df7a261b1e86c865e29602aa96714eca853041caa8b302c60b69a8`.
  The offline container has no host desktop/device/bus mounts and removes its
  temporary directory.
- `resource-budget-fixed-tests.log`: all 15 budget tests pass on Mac. The short
  real-child fixture now has exactly an initial and terminal sample, avoiding
  an unintended 100-ms final interval. Missing production slots still fail.
- Hosted run [38660677004](https://github.com/JackHars/deskflow/actions/runs/38660677004)
  on `3c4263f03`: five jobs pass, including the repaired GNOME report ordering;
  Mac fails on the short resource fixture corrected above. A new run is needed.
- `mac-image-reuse-hour/plan.json`: replacement steady workload and unchanged
  limits declared before launch. The completed run passes the resource
  comparison but fails steady workload validation, as detailed above.

The user requested unattended checks and attended testing later. Physical
H07–H11 remain deferred. A complete synthetic hour would establish only this
fixture's resource behavior, not native capture, display or input latency.

## P7 native-batch and hosted follow-up — 2026-10-14

Native queue review: [`native-queue-audit.md`](deploy/windowshare/native-queue-audit.md).
The guarded libei consumer now limits processed native events to 4096 per
System callback, checks two seconds around library dispatch and handlers, and
invalidates the prefix on failure. This limits application processing, not
libei's earlier allocations. There is no new input authority or synthetic
native delivery. Linux test builds additionally need `libeis-dev`.

Actual evidence:

- `build/p7-completion-20261014/linux-native-batch-2/result.json`: ON Core,
  nine suites and private GNOME/Xvfb checks pass; OFF Core and five ASan/UBSan
  suites pass. `EiComputerTests` uses a real private EIS server to compare
  ordinary/guarded 4096/4098-event batches and a delay inside library dispatch.
  The 40 normalization, 35 H11 and 35 physical-coordinator checks also pass.
  Archive SHA-256: `fe80d8d2f01ee09c0687cf5aa17724e74df25c24772e50db71e4f47f4a9a2814`.
- `build/p7-queue-20261014/p7-completion-recovery-tests.json`: Mac watchdog,
  native journal/input-construction and holding policy suites pass. These
  fixtures do not relocate a live desktop window or establish H10.
- `build/p7-queue-20261014/p7-completion-guard-tests.json`: Mac EventQueue and
  private local-input suites pass, including the new failure diagnostics.
- Hosted run [38659292670](https://github.com/JackHars/deskflow/actions/runs/38659292670)
  on `cb506b6e1`: four jobs pass; Mac local-input epoch preparation and GNOME
  report-directory setup fail. The latter has a corrected ordering; the former
  now retains channel/child failure details. Neither deadline is loosened.
  A fresh hosted result is required for these changes.
- `build/p7-completion-20261014/mac-soak-plan.json` predeclares an automated
  two-stream 1080p/30-fps hour. Its offscreen media and combined-process resource
  outcome is pending and is separate from native H11 acceptance.

The user chose unattended checks and deferred attended recovery/input/display
tests. H07–H11 physical rows are unchanged. The upstream native queue gaps must
remain visible even after successful application tests.

## P7 whole-Core backlog verification — 2026-10-14

The candidate adds production admission limits to explicitly configured
experimental Core sessions and evidence v2 for every Core Event lifetime.
Limits are 4096 outstanding Events, 64 dispatch scopes and two-second age
checks. An overload stops the session instead of continuing after dropping a
release. Ordinary launches remain unguarded. See
[`input-queue-budget.md`](deploy/windowshare/input-queue-budget.md) for the
native boundary and exact failure behavior.

Evidence directory: `build/p7-queue-20261014` (local, ignored).

| Check | Result and evidence |
| --- | --- |
| Mac enabled Core build and six regression suites | PASS, `mac-build-final-2.log`, `mac-tests-final.json`: EventQueue, Server, both protocol proxies, input evidence and private capture lifecycle |
| Mac companion/source rebuild and two regressions | PASS, `mac-companion-build.log`, `mac-companion-tests-final.json`: Bridge and native-input checks offscreen |
| Mac disabled Core build and four compatibility suites | PASS, `mac-ordinary-build.log`, `mac-ordinary-tests-final.json` |
| Mac ASan/UBSan | PASS, `mac-sanitizer-build-final.log`, `mac-sanitizer-tests-final.json`: EventQueue, Server, evidence and private capture; project dependencies instrumented, leak detection disabled |
| Ubuntu enabled Core build and nine suites | PASS, `linux-final-2/result.json`: EventQueue, EI, Server/proxies, original-input coordinator, sequence checks and evidence |
| Ubuntu private native startup checks | PASS, same report: terminal GNOME provider absence on a private bus and actual Core observation on private Xvfb |
| Ubuntu disabled Core and five ASan/UBSan suites | PASS, same report: EventQueue, actual EI buffer, Server and both proxies; leak detection disabled |
| Strict evidence and coordinator parsers | PASS on Mac and Ubuntu: 40 normalization, 35 H11 analysis, 35 physical-coordinator tests, including legacy v1 and bounded v2 |
| GitHub hosted checks | NOT_RUN: enable API calls accepted, but GitHub separately blocks workflows on this fork |
| Full upstream queue certification, physical H10/H11 | NOT_RUN; automated counts and parser fixtures cannot certify these |

New cases cover overflow before/after queue readiness; concurrent producers;
retrieved/moved Event lifetime; callback/Event/queue destruction; nested dispatch
limits; expired retrieval and payload disposal; foreign-budget reuse; platform
buffer refusal; cleanup timers under continuous load; 10,000 spurious wakeups;
actual Mac posted IDs; actual EI buffer shutdown; and immediate capture/inbox/
held-motion revocation. Mac capture checks construct private CGEvents only.
No input tap, post, desktop capture or visible native fixture is started.

The Ubuntu source archive SHA-256 is
`8df3799c5eaee5ff0e916c6279a3485841b98d70fb494f7d982f154bbdab245c`;
`linux-final-2/source-sha256.json` records files and symlink targets. It runs in
the existing offline Ubuntu 26.04 / GCC 15.2 / Qt 6.10.2 dependency image on the
home machine, without host display/device/bus mounts, and removes its temporary
directory. Mac binaries are tested as separately ad-hoc-signed copies beside
their build outputs, retaining hashes in each JSON report.

Retained initial failures exposed an evidence check accepting an invented
capacity and a Linux-only collision with X11's `None` macro. Both are fixed;
the final builds/tests above use the correction. `actions-enablement.json`
records accepted repository/workflow enable calls and the rejected dispatch.
The Actions UI identifies a fork usage restriction requiring maintainer
re-enablement, and the available browser is signed out. A source change or a
successful settings call is not a successful hosted run.

The next physical step remains fresh readiness followed by the recorded H10
owned-process recovery procedure and the H11 predeclared measurement workload.
The 2-second age check cannot preempt a hung callback or certify events retained
inside an OS/library before Core sees them. Keep those native queues in the
remaining audit and retain unknown bounds explicitly. Do not mark P7 accepted
from these short deterministic checks.

## P7 media/resource tooling verification — 2026-10-14

This checkpoint adds executable measurement tools; it does **not** complete
H11 or the remaining platform/kernel queue work. Physical tests remain deferred.
All new media runs force Qt offscreen and use only synthetic loopback video;
no native source, input injection, input permit or physical display is involved.

Evidence directory: `build/p7-evidence-20261014` (local, ignored).

| Check | Result and evidence |
| --- | --- |
| Mac production build | PASS, `mac-final-build.log`; app, load tool, Viewer and Bridge |
| Mac regressions | PASS, `mac-viewer-bridge-final.json`; both separately signed suites |
| Mac steady/mixed and maximum stream load | PASS, `mac-media-load-final.json`; four-stream steady/mixed and 16-stream steady, three seconds each |
| Mac 1080p load | PASS, `mac-hd-load-final.json`; two generated 1920×1080 IDRs, offered 30 fps, five seconds |
| Mac ASan/UBSan | PASS, `mac-sanitizers-final.json`; Viewer suite and mixed four-stream load, leak detection disabled |
| Ubuntu standalone app and Qt/X11 regressions | PASS, `linux-final/result.json`; full app build and eight CTests: family, viewport, Viewer, steady/mixed load, X11 scale 1/1.25, Bridge |
| Ubuntu maximum streams and 1080p load | PASS, same report; 16-stream steady and two generated 1920×1080 streams |
| Resource collector and fixed budget parser | PASS on Mac and Ubuntu: nine collector and 15 parser tests; Mac `resource-*-tests-final.log`, Ubuntu `linux-resource-final/result.json` |
| Hosted checks | NOT_RUN; repository Actions was disabled at the recorded P6 dispatch attempt |
| Physical H07/H08/H09, native H10 and performance H11 | NOT_RUN on this candidate |

Ubuntu used the existing offline dependency container on the home machine,
Ubuntu 26.04 / GCC 15.2 / Qt 6.10.2. It had no host desktop or device mounts.
The full build's frozen archive SHA-256 is
`765af8e75127d2002f738b2fe4876f356128abae1e44f97cd277e5fd9b0c817b`;
`linux-final/source-sha256.json` records regular files and symlink targets.
The final resource-only manifest and run cover the later correction that uses
the longest observation interval for an upper bound on negative RSS growth.

The mixed load must reach the real decoder capacity, reconcile every admitted
and retired item, drain, request fresh video and resume painting. Its deliberate
decoder burst bypasses transport and is labelled separately. Normal frames
travel over the actual pinned Channel and respect its one-frame admission.
Each stream must make progress; that is not a native scheduler fairness or
sustained-fps verdict. Mac small-frame tests retain VideoToolbox initialization
fallback messages, so GPU acceleration is not established by these results.

For reproduction, use [media-load.md](deploy/windowshare/media-load.md) for
build/CTest commands, generated-frame workloads, signing, metrics and failure
interpretation. Use [resource-soak.md](deploy/windowshare/resource-soak.md)
for collection and the fixed offline budget report. Both Python suites run as
`python3 deploy/windowshare/resource-soak-tests.py` and
`python3 deploy/windowshare/resource-budget-tests.py`. The workflow now includes
both; this wiring is not hosted verification.

Keep the initial failing `mac-media-load.json` and `mac-media-load-2.json` as
failure evidence: the fixture first ignored Channel backpressure, then needed
to deny automatic focus requests because it deliberately has no NativeHost.
Final reports include source/executable hashes and preserve misses/replacements.
No short run is promoted into the 60-minute pilot; all reports retain
`h11_complete:false`. Actual capture-to-visible/input latency, GPU/network
measurements, full native queue bounds, crash/sleep recovery and physical
acceptance remain separate work.

## P6 family verification — 2026-10-14

Implementation is connected; **H08/H09 stay NOT_RUN** until actual app and
physical input acceptance. The test procedure is
[app-compatibility.md](deploy/windowshare/app-compatibility.md); implementation
boundaries and commands are in [window-families.md](deploy/windowshare/window-families.md).

- Implementation `bfb9cb9e80152386f4cb4ca7b5cf3dc0969afb10` is pushed.
  **GitHub checks: NOT_RUN.** Dispatch returned HTTP 422 because Actions is
  disabled for the repository (`p6-github-checks-unavailable.json`). The new
  GNOME fixture is included in the workflow, but wiring it in is not a pass.
- `p6-final-mac-tests.json`: all five signed Mac suites pass (family, Viewer,
  Bridge, viewport and native original-input). The production app builds;
  `p6-final-mac-viewer-smoke.json` passes authenticated reconnect and clean shutdown
  with that Viewer, without starting NativeHost.
- `p6-linux-final/result.json`: exact implementation `bfb9cb9e8`, full Ubuntu
  app build; eight Qt/X11 suites;
  provider logic; Viewer reconnect smoke; real isolated GNOME sheet/menu
  creation, native mapping, parent blocking, dismissal and separate Qt/native
  reparent denial pass. X11 exercises 1 and 1.25 scale. Frozen archive SHA-256:
  `dd343d6799c5dd2733291903de1320f71246205cd980e1842c11b7e98f28e78e`.
- `p6-sanitize-tests.json`: family, Viewer and Bridge pass Mac ASan/UBSan with
  leak detection disabled. `p6-sanitize-tests-before-lifetime-fix.json` preserves
  the parent-destruction failure fixed by a separate Viewer lifetime token.
  `p6-direct-destruction-before-fix.json` separately reproduces Bridge cleanup
  accessing an already-destroyed Viewer; the destruction callback now clears
  its typed pointer before retiring the remaining family members. The full
  Bridge suite passes ASan/UBSan in `p6-direct-destruction-fixed.json`.
  The final production rebuild and signed Bridge suite pass in
  `p6-destruction-mac-build.log` and `p6-final-bridge-tests.json`.
- Earlier P6 reports remain evidence of failures. In particular run 2's GNOME
  fixture incorrectly swallowed an assertion; run 3 correctly failed because a
  Qt parent-property change alone did not change the compositor parent. Run 4
  independently changes the real owned xdg role and requires an explicit native
  parent-change rejection. Those earlier runs are not accepted as full passes.

GNOME compositor placement is supported; exact source-relative popup placement
is still a limitation. These fixtures do not exercise actual Mac app capture,
key release across native modal creation, AX Cancel behavior in named apps,
physical input, arbitrary popup implementations or IME composition.

## Previous P5 evidence — 2026-10-14

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
- Current runtime implementation: `4854f84a9b345538a198ba8c3e0b1231a981f27f`.
  This includes asynchronous holding startup and absolute deadline enforcement,
  following the P5/P6 and P7 queue/media/recovery work. Current local checks and
  exact package/preparation results are recorded at the top of this file. All six
  hosted jobs pass for its parent runtime `7453a4c77`; the new run is tracked above.
  The declared
  synthetic hour remains a `d57e78a0d` result on unchanged media sources; it did
  not exercise the new Host monitor. Full native queue certification and hardware
  acceptance remain incomplete. Earlier checkpoint results are historical.
- Returned branch: `feature/seamless-windows` in `JackHars/deskflow`; incremental changes pushed directly, no PR.
- Local runtime/toolchain: Intel macOS 26.1 (25B78), SDK 15.5, Qt 6.8.3;
  isolated Ubuntu 26.04, GCC 15.2, Qt 6.10.2, libei/libeis 1.5.0 and Mutter
  50.1-0ubuntu2.4. Hosted toolchains and complete commands are retained in the
  linked Actions logs. No compatibility with an untested OS/library build is inferred.
- Source reproducibility: linked GitHub implementation SHA and CI source bundle; inert Linux package instructions: `deploy/windowshare/package-linux.md` (not installed).
- Unsupported capabilities: X11 original-input delivery, unknown Mutter/Qt
  ordering builds, unproved input-method composition and unsupported native
  child roles remain explicitly denied. See `deploy/windowshare/app-compatibility.md`,
  `window-families.md` and `native-queue-audit.md`. Actual installation, native
  queue certification and physical acceptance are still outstanding.
- Windows was not compiled in this Mac/Ubuntu verification pass. Sanitizers
  instrument project code; system/Qt libraries are not rebuilt, and the scoped
  Mac and native Linux fixtures disable leak detection as recorded above.

Implementation-to-test map (later commits include earlier runtime work):

| Roadmap | Implementation checkpoints | Automated coverage / remaining hardware rows |
| --- | --- | --- |
| P1 | `5d3cdf23f`, `c867af9d0` | C01/C03/C04; H03/H04 |
| P2 | `bbe9ed3f2`, `81efdc5a5`, `cb506b6e1`, `3c4263f03` | C02–C07; native capacity gaps and H04 remain open |
| P3 | `e461d2b31` and subsequent route fixes | C02–C07/C10; H02–H04 |
| P4 | `e461d2b31` and subsequent holding/recovery fixes | C04/C08/C10; H05/H06/H10 |
| P5 | `63396d630`, `c748915cc` | C04/C07/C08; H07 |
| P6 | `bfb9cb9e8` | C06/C07/C09; H08/H09 |
| P7 | `796495b86`, `cb506b6e1`, `3c4263f03`, `6108bd285`, `d57e78a0d`, `e154f94e0`, `8132c8554`; collection profile `286328d05` | C01–C10 and recorded offscreen load; full native queue certification and H10/H11 remain open |
| P8 | Existing package scripts and the current CI retention follow-up | C10–C12; installation/upgrade/removal H12 remains open |

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

## Cloud test matrix — current automated coverage

The latest run and source hashes are recorded at the top. A component PASS
does not promote a native/physical H row or certify unknown upstream queues.

| ID | Required check | Current candidate result / command / evidence |
| --- | --- | --- |
| C01 | Clean build and pure C++ foundations; GCC/Clang and applicable sanitizers | PASS at `286328d05`: both hosted compiler jobs run 16 foundation/sanitizer suites plus nine library-policy checks. |
| C02 | Actual Linux core with window sharing ON and OFF; ordinary protocol compatibility | PASS: current hosted integrated ON/OFF build/tests; independent home Ubuntu frozen Core and private EIS results are retained above. |
| C03 | Full-input private lease/framing/preflight; old peers, malformed scope, stale lifetime and cancellation | PASS at `286328d05`: hosted transport 11 suites and five private-protocol ASan/UBSan suites; Mac standalone suites include the same channel/preflight regressions. |
| C04 | Bounded async preparation, ownership, overflow, timeout, selection change, reentry and destruction | PASS: production coordinator, Core EventQueue and private channel regressions on Mac/Ubuntu, including overflow, expiry, reentry and exact cleanup. No physical delivery claim. |
| C05 | Ordered cutoff/drain and exact point validation; delayed queues, duplicates and same-batch disconnect | PASS for implemented prefix, routed-point and private GNOME/Qt fixtures. Full native/OS queue capacities and actual physical delivery remain uncertified; see the native audit and H04. |
| C06 | Actual Viewer/Bridge/transport; pending focus, pause/resume and old queued events | PASS: current Mac/Ubuntu Viewer, Bridge, viewport, transport and bounded media-load suites, plus image-ownership/hardware-transfer/color regressions, scoped Mac sanitizers and the earlier complete synthetic hour on unchanged media sources at `d57e78a0d`. |
| C07 | Private X11 and GNOME/Qt headless integration when the cloud supports it | PASS at `286328d05`: hosted real private GNOME/Qt and X11 fixtures, including attached surfaces; no user-desktop or physical-monitor claim. |
| C08 | Multi-window/topology/holding lifecycle, drag transaction and failure regressions | PASS automated policy/transaction/native-fixture coverage for integrated bidirectional P5 dragging, holding restoration and watcher/helper lifecycle. Real-window transfer and failure acceptance remains H05–H07/H10. |
| C09 | Dialog/clipboard/shortcut associations and resource scheduling/recovery tests | PASS component regressions, including actual private GNOME/X11 child surfaces and bounded overload/hidden recovery. Named app dialogs, clipboard and shortcut acceptance remains H08/H09. |
| C10 | Mac compilation and private-event construction/packaging tests through available CI | PASS at `286328d05`: 27 standalone, nine integrated, four OFF compatibility and 19 portable/native-private sanitizer suites; 21 bundle tests and both package smoke paths pass. Local unchanged `8132c8554` Core dispatch/Host recovery, ASan/UBSan, Apple-issued exact-library package and private handshake checks remain recorded above. |
| C11 | Linux packaging/install/uninstall in a disposable environment | PASS at `286328d05`: inert archive/manifest/staging and both wrapper help checks. End-user install/upgrade/removal H12 remains NOT_RUN. No host installation performed. |
| C12 | Formatting, REUSE, diff check and final source/commit reproducibility | PASS changed C++ formatting, changed-file REUSE and diff checks. Downloaded `286328d05` source archive matches all 1160 Git tree entries and blob identities. Full-repository REUSE 3.3 lint PASS on 2026-10-15: 1132/1132 licensed files, no errors. |

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
| H01 | Build current source on Mac and Ubuntu; confirm actual enabled/disabled core flags, load dependencies and run relevant local suites. Verify fresh experimental package identity and supported native capabilities. | PARTIAL — `b51487025` passes 21 selected Mac and 16 Ubuntu suites and fresh package verification; native capture checks also pass normally and with ASan/UBSan. Harness source is bound to 202 checks on each machine. Current exact artifacts are recorded above; hosted Mac evidence-suite timeout, native reliability and installation acceptance remain open. |
| H02 | Repeat actual disposable Mac capture → Ubuntu viewer → Mac tagged native-receiver tests at Qt scales 1 and 1.25. Check frame identity/geometry, pointer/click/scroll/key receipts, no stuck input and restoration. This is synthetic, not physical acceptance. | NOT_RUN |
| H03 | Run visible user-started physical flow. Confirm Start/Stop and countdown, actual Option hotkeys, type a/Shift+b/c, hold/release x, move/click/drag and scroll both axes, return to Mac. Match exact native source/host receipts and human result; no duplicate text or missing release. | NOT_RUN |
| H04 | Exercise actual production original route and native drain with delayed compositor/Qt processing. Verify no legacy duplicate, exact owner through reentry/focus changes and safe failure on timeout/loss. Test emergency chord before/while/after active input and after capture continuity failure. | NOT_RUN |
| H05 | Use both physical monitors with mixed compositor scaling, negative coordinates, different relative layouts, resize and output disconnect. Validate both destinations, edges, letterboxing, occlusion and correct source points. If only one monitor is connected, leave two-monitor cases unrun. | NOT_RUN |
| H06 | Export multiple windows from same/different apps; hold sources on the helper display, overlap remote/local windows, switch focus, resize/minimize/close viewers. Verify continued source capture, exact input owner and per-window restoration after helper loss. | NOT_RUN |
| H07 | Follow [h07-edge-drag.md](deploy/windowshare/h07-edge-drag.md). Drag a real source window across a configured edge, continue/release the drag on Ubuntu, then drag back. Verify first-frame transfer, source location/focus, single button-up ownership and rollback for canceled drag/disconnect/layout change at each transfer phase. | NOT_RUN |
| H08 | Test native dialogs, modal child windows, popup/context menus, browser and Electron app. Check intended window association, stacking, focus and input targeting; no unrelated application receives an event. Record app/OS versions. | NOT_RUN |
| H09 | Verify copy/paste directions and focus policy; modifiers, shortcuts, key repeat, Unicode/composition and both scroll axes/momentum. Check clipboard feedback and unsupported IME behavior. | NOT_RUN |
| H10 | Terminate only recorded owned experimental processes at defined host/core/viewer/helper stages; test connection loss and sleep/wake. Confirm automatic/journal recovery, no orphan display/process, no stuck input and no changes to ordinary Deskflow. | PARTIAL — four c319 no-input actual-desktop fault diagnostics restore exact source/frame/journal and clean up owned processes. They do not qualify changed binaries or establish held-input release, connection-loss/sleep-wake behavior or human restoration observations. See current evidence above. |
| H11 | Run a documented long-duration/multiple-stream scenario; measure end-to-end latency, CPU/GPU, memory, bandwidth, dropped frames and fairness. Record hardware/settings, duration and thresholds before running. Verify bounded resources and cleanup. | NOT_RUN for native end-to-end acceptance. The separately scoped synthetic hour/resource comparison passes at `d57e78a0d`; it does not close H11. |
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
  An explicit frozen `h11` recording profile now supplies a 120-second input
  interval within a fixed 150-second session; all workload, native receipt,
  queue certification and physical display measurements still require evidence.
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

- [x] Fill candidate identity, branch and dependency versions above.
- [x] Replace every C-row placeholder with an actual result and reproduction.
- [x] Add exact commands for new tests, production launch modes and packaging.
- [x] List uncompiled platforms and unimplemented/unsupported features.
- [x] Keep unperformed H tests marked NOT_RUN; document their prerequisites.
- [x] Map implemented P1–P8 changes to commits and associated test IDs.
- [x] Include expected native receipt fields/counters and pass/fail criteria for
  new barriers, routed points, transfers and recovery cases.
- [x] Ensure the branch contains all needed source/scripts/docs; ignored build
  artifacts must not be the only copy of a required test or configuration schema.
- [x] State the next local verification entry points and prerequisite user actions.
