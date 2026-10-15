<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# H11 input and latency evidence

Physical H11 remains **NOT_RUN**. `h11-evidence.py` is an offline analyzer; it
does not create measurements, inject input, capture a desktop, launch Deskflow,
or modify either peer. Its unit tests use explicitly synthetic data, which is
not evidence of product performance. Do not copy generated test traces into an
acceptance bundle.

P7 also provides [media-load.md](media-load.md) for synthetic TLS/decoder/Qt
load and [resource-soak.md](resource-soak.md) for v2 sampling and fixed pilot
budget evaluation. Local receive-to-Qt-paint histograms are a distinct boundary
and cannot be converted to this analyzer's physical capture/display evidence.

The existing `physical-macos.py` receipts have target-local event sequence and
poll times. `resource-soak.py` has local process CPU/RSS counters. Neither has
source-to-native event correlation, every pending-queue mutation, or a common
capture/presentation clock. The native encoder's scheduled presentation time
is not a measurement of capture time. The existing GNOME fixture's ordered
protocol observations establish specific functional assertions, not H11's
120 Hz sustained workload or end-to-end latency. These files cannot be
automatically converted into a passing H11 bundle.

The production [original-input physical harness](original-input-physical.md)
has an explicit `freeze --recording-profile h11` option for a fixed 120-second
recording within a 150-second visible session. Its ordinary H04 profile remains
30 seconds. The extended profile makes the required interval collectable; it
does not supply the workload, native receipts, full queue certification or
latency evidence. Review and normalize the actual three-process journals as
described in [input-evidence.md](input-evidence.md), preserving all failures.

## Fixed acceptance checks

The analyzer uses the targets declared in `TESTING.md` before measurement:

For the 60 fps scenario, set numeric `"captureFps": 60` in the **Mac host's**
companion configuration or launch that host with `--capture-fps 60`. Its default
is 30. A Viewer-side setting cannot change the remote host. Only the active
stream requests that rate; visible inactive/hidden streams retain their 10/2
fps budgets. Confirm actual dimensions, cadence and delivery in the evidence;
the setting alone is not proof of 1080p60 output. Real source sample timestamps
are distinct from bounded synthetic timestamps used to re-encode a static image.

* Input runs last at least 120 seconds, with at least 120 observed source
  records per complete one-second bucket and at least 120 Hz over the whole
  interval. To reject one-second bursts masquerading as 120 Hz, the workload
  shape also permits at most two nominal periods (16,666,667 ns) between source
  observations, including the measured interval's boundaries. This is an
  explicit collection-shape requirement, not a measured product result.
  The mix includes key down/up, button down/up, drag, scroll and hover;
  repeat is counted separately when present. Every non-hover record must reach
  the native target once, in source order, with the expected native value.
* Hover compression is explicit. Each removed hover names a later hover
  replacement; the intervening records must all be hover. Increasing chains
  must terminate at an actual native receipt. Compression cannot excuse a
  missing drag, key, button or scroll record.
* Every **declared** pending-input queue has a complete mutation trace, a zero
  baseline, internally consistent cumulative counters, no overflow/deadline
  revocation, capacity respected and a drained final state. The trace must
  separate enqueue from retirement; combining both in one row can
  conceal an intermediate peak and is rejected. Bulk enqueue or bulk retirement
  may each occupy a row because its resulting depth retains that peak. The
  time-weighted per-second mean must have no positive linear trend, and its
  last-ten-second mean must not exceed its first-ten-second mean. These deliberately strict,
  predeclared growth tests may require investigating noise; do not loosen them
  after seeing a run. A missing queue in the collection inventory is a review
  failure that a parser cannot discover by itself.
  Legacy v1 Core EventQueue lifetime/dispatch metrics have no configured
  production capacity and retain `capacity:null`; accounting,
  observed growth and final-drain checks still run, but their capacity certification
  remains **INCOMPLETE**. A successful finite trace cannot prove a production
  bound. Platform/kernel buffers remain outside the collector's inventory.
* Latency uses one wired 1920×1080, 60 fps stream, at least 600 predeclared
  consecutive source frames, and at least ten seconds of observation. The
  actual stimulus/capture timestamps must also match 60 fps within the declared endpoint
  uncertainty. Every planned frame has an observation, including an explicit
  `null` for an undisplayed frame. Missing frames cannot be omitted from the
  percentile population. Undisplayed frames fail this strict component check.
* Nearest-rank p95/p99 of **upper-bound** latency must be ≤100 ms / ≤150 ms.
  The upper bound adds the declared total uncertainty to each nominal sample.
  A nominal pass whose uncertainty crosses a threshold does not pass.

An exit code of `0` means these two measured components passed for the supplied
bundle. `1` means complete measurements failed a threshold or integrity check
such as missing native receipt. `2` means evidence is absent, incomplete,
malformed, changed, unsupported, or has a bad digest. JSON reports preserve the
individual component result, and always set `h11_complete:false`. Resource,
GPU, bandwidth, multiple-stream fairness, hidden-source rendering and held-input
cleanup remain independent requirements. Hashes detect bundle changes; they do
not authenticate the truth, completeness, native origin or review of a trace.

## Reproducible collection procedure

1. Freeze and hash the exact experimental binary/archive and collection
   adapter. Record OS, hardware, Qt/provider versions, input devices, layout,
   capture settings and the queue inventory in the retained collection
   procedure. Use only authorized disposable windows and a private evidence
   directory. Do not run the workload on an unrelated application. Choose the
   run ID, input mix, 120-second interval, consecutive frame population and
   timing uncertainty **before** recording. Do not upload credentials or typed
   user content.
2. The input adapter is a necessary prerequisite, not an existing implicit
   capability of the old logs. Instrument the owned workload's actual source
   capture and the owned target's actual native receiver, outside OS callbacks
   where logging could block. Source IDs must be assigned when the event is
   observed, before any tested queue; never renumber away a gap after a loss.
   Join the target's observed event to that exact ID with independently
   reviewed fixture correlation. A send attempt, queued command, successful
   CGEvent construction, or protocol ACK is not a native receipt. Do not pair
   source and target simply by ordinal position after a loss. Preserve raw
   observations and the adapter that normalizes them.
3. Log every mutation of each candidate's input queue and every deadline
   revocation/overflow in a bounded nonblocking buffer. Record loss counters
   even when the evidence buffer fills. Separate pure-hover replacements from
   the other records. A sampled queue-depth timer cannot prove absence of
   transient growth, overflow or loss and must not be emitted as this schema's
   complete queue trace. Start/end markers must refer to the same planned run
   and exact owner/epoch; queue clocks may differ because only durations within
   each trace are used. Review coverage of all production pending-input queues
   against the frozen implementation before accepting a component PASS.
4. Observe at least 120 seconds of actual mixed input at 120 Hz and retain
   the complete source, target, compression and queue records. Log intentional
   setup and teardown outside the planned measured interval; never trim an
   overflow, loss, slow period or replacement owner out of the interval.
   Ensure the final queues drain. Collector interruption, buffer loss or owner
   replacement invalidates the run rather than restarting counters in place.
5. Collect latency using one of the two methods below. Keep the original
   recording/instrument data, not only selected annotations. Record all
   predeclared frames, including failures. Hash the finalized artifacts, then
   create the manifest. No evidence file may be edited after hashing; a new
   collection is a new bundle/run ID.
6. Run locally (analysis only):

   ```sh
   python3 deploy/windowshare/h11-evidence.py /absolute/private/run/manifest.json
   python3 deploy/windowshare/h11-evidence-tests.py
   ```

   Retain the JSON report, its exit code and manifest hash with the raw
   evidence. Run the independent [resource procedure](resource-soak.md) and
   the remaining H11 checks separately. A component PASS never replaces them.

### Filmed stimulus and remote response

Use a fixed camera that can see a physical **pre-capture stimulus** and the
remote Viewer in the **same recording**, at a verified constant rate of at
least 240 fps. An owned fixture must establish that the marked source frame
cannot be created/captured before that visible stimulus. For example, a
reviewed external fixture can light its trigger indicator before sending the
command that makes the owned source create the next uniquely marked frame.
Account for indicator onset/command ordering in the error budget; do not
assume it from an LED name or an application's send timestamp. Repeat at 60 Hz
in the disposable 1080p source and match the exact marker at the remote display.
Retain the fixture implementation, instrument contract and observed markers.
This hardware/fixture arrangement is a collection prerequisite, not something
the analyzer creates or the current application logs establish.

Record the entire selected population, with lead-in and tail long enough to
observe late responses. Review actual camera timestamps and dropped/duplicated
camera frames; a slow-motion file's playback frame rate is not its acquisition
rate. Unsupported variable timing or dropped camera frames requires an
explicitly reviewed uncertainty model or a new recording.

For each predeclared marker, annotate its first visible stimulus camera frame
and its first visible remote camera frame, preserving original camera
frame numbers and PTS in the raw annotation artifact/collection procedure.
Convert **both** to nanoseconds in that recording's one acquisition clock.
Use at least two camera periods of total uncertainty to cover quantization at
both endpoints, and add exposure/readout/rolling-shutter/marker ambiguity from
the actual setup. If either endpoint cannot be identified, preserve the
failure; do not silently skip the sample. A frame never displayed is `null`.

This measures **pre-capture stimulus to remote-visible** latency, a conservative
upper bound on capture-to-remote-visible latency because the copied marked
frame is created/captured after the filmed stimulus. Review that ordering in
the actual fixture. It includes fixture/input/rendering delay, so a failed
stimulus-bound result does not isolate the capture/stream pipeline as the cause.
It does not directly timestamp ScreenCaptureKit, prove all hidden holding-display
rendering, or use Qt paint as display completion.

Filming only a counter on the source display and the copied counter on the
remote display is **not accepted as this boundary**: ScreenCaptureKit may
capture the source buffer before its local physical display presents it. That
can understate capture-to-remote-display latency. If no actual pre-capture
ordering or common-clock capture observation is available, keep capture
latency **NOT_RUN** rather than assigning this schema to those annotations.

### Shared instrument clock

The same measurement instrument/timebase must observe both the actual capture
boundary of the exact marked frame and the remote display completion, with a
documented resolution and total error budget. Preserve the original instrument
trace and the reviewed boundary instrumentation. A source timestamp copied
inside a packet and a receiver's local monotonic timestamp are **not** a shared
instrument clock. NTP wall clocks, SSH round-trip estimates, an assumed fixed
offset, encoder PTS, and Qt paint callbacks alone do not satisfy this method.
The analyzer rejects differing endpoint clock IDs and has no offset-conversion
option. Merely giving two machine clocks the same ID cannot make them valid.

## Strict bundle schema

Only finalized UTF-8 JSON/JSONL is accepted: no duplicate fields, NaN/infinity,
unknown fields, booleans in integer fields, missing end marker or partial last
line. Artifacts must be regular, non-symlink **sibling** files, each referenced
once by `{ "path": "source.jsonl", "sha256": "64 lowercase hexadecimal digits" }`.
There are at most 100,000 records per trace, 32 MiB per trace, 32 queue descriptors, ten
minutes per trace, and 16 GiB for the single raw latency recording. Oversize
evidence is denied rather than truncated. Split a longer soak into separately
predeclared runs; do not extract an arbitrary passing interval afterward.

The manifest has exactly:

```text
format: "deskflow-h11-evidence-v1"
run_id: bounded ASCII identifier
candidate_sha256: hash of frozen tested candidate
collection_artifact: artifact for exact adapter + collection/review procedure
input: null, or the object below
latency: null, or the object below
```

`input` fields:

```text
owner: exact run-local identity identifier (mapping retained in procedure)
epoch: positive capture epoch integer
source_clock, receipt_clock: separate explicit local clock identifiers
source, receipts, compressions: artifacts, even when no hover was compressed
queues: [{name, capacity, clock, trace: artifact}, ...]
```

`capacity` is a positive integer from 1 to 65536 for bounded metrics. The
production v2 Core EventQueue metrics require exactly 4096 for
`core-event-outstanding`/`core-event-all-outstanding` and 64 for
`core-event-dispatch`/`core-event-all-dispatch`. The normalizer requires all
21 v2 Core/Host descriptors and reports the still-unmeasured native/OS boundary.
See [input-queue-budget.md](input-queue-budget.md) for the enforced lifetimes.

Legacy v1 normalization emits 19 descriptors and retains JSON `null` for
`core-event-outstanding` and `core-event-dispatch`. The analyzer accepts those
two legacy nulls but reports capacity certification INCOMPLETE. It rejects
substituted positive EventQueue limits, null on another name, missing metadata,
booleans and string `"null"`. Event metrics count views/callbacks, not additional
physical source records; the subset and whole-Core counts are not summed.
The 32-descriptor parser bound is not a queue-depth limit. Actual loss, growth,
insufficient workload and drain failures still return FAIL. Finite application
limits alone cannot certify opaque OS, compositor or device queues.

Each trace begins with exactly
`{event:"start", format, run_id, clock, t_ns, owner, epoch}`. Latency traces omit
`owner` and `epoch`. Each ends with exactly
`{event:"end", index:N+1, t_ns, count:N, lost_records:0, complete:true}`. Every body
row has an `index` from 1 through N, plus nondecreasing local `t_ns`; source IDs
also cover 1 through N without gaps. Owner, epoch, clock and run must match the
manifest; timestamps cannot precede start or exceed end. Formats/body fields:

| Format | Exact body fields beyond `index`, `t_ns` |
| --- | --- |
| `deskflow-h11-source-v1` | `event:"input", id, kind, signature` |
| `deskflow-h11-receipts-v1` | `event:"receipt", id, kind, signature` |
| `deskflow-h11-hover-v1` | `event:"compress", id, replacement` |
| `deskflow-h11-queue-v1` | `event:"queue", enqueued, retired, depth, deadline_revocations, overflows` |
| `deskflow-h11-latency-v1` | `event:"frame", frame_id, start_clock, display_clock, start_ns, display_ns` |

`kind` is `key_down`, `key_up`, `key_repeat`, `button_down`, `button_up`, `drag`,
`scroll` or `hover`. `signature` is SHA-256 of the fixture's canonical expected
native effect (kind/control/transition/modifier sides/click count/drag button/
coordinates/scroll values and phases as applicable), independently computed
again from the actual native receipt. Preserve the canonicalization definition
and raw fixture observations for review; do not hash machine-local timestamps
or unrelated sequence numbers into it. Do not hash real typed text as a
privacy measure: low-entropy text can be recovered. Use a disposable known
test pattern and omit user text entirely.

Queue `enqueued`, `retired`, `deadline_revocations`, and `overflows` are cumulative
nondecreasing unsigned 64-bit counters, with `depth == enqueued - retired`. The first queue row
is an observed zero-counter/empty baseline at start. Every subsequent mutation
is included; a retirement after failed delivery still requires its failure to
appear in the receipt/deadline evidence. Hover `replacement` is a later source
ID; compression time cannot precede that replacement's source observation.

`latency` fields:

```text
method: "filmed" or "shared-instrument-clock"
boundary: "stimulus-before-capture-to-remote-visible" for filmed,
          "capture-to-remote-visible" for shared-instrument-clock
clock: one actual measuring clock identifier
uncertainty_ns: positive total uncertainty, at most 50,000,000
observer_hz: actual camera acquisition rate or instrument clock frequency
recording: original recording/instrument-data artifact
samples: annotation trace artifact
planned_frames: predeclared count, 600 through 100,000
width: 1920; height: 1080; stream_fps: 60; streams: 1; network: "wired"
```

Latency `frame_id == index` names consecutive observed source markers.
`t_ns == start_ns`; `start_clock == display_clock == clock`. `display_ns` is
the remote-visible observation in that same clock, or `null`. Displayed frames
must have increasing display times and cannot precede their source observation.
The raw retained artifact must map those IDs/times back to the actual physical
observations. A renamed local clock or a generated trace is not measurement
evidence even if its JSON happens to satisfy the parser.
