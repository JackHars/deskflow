<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Opt-in original-input evidence collector

This collector records actual Mac capture records before the first raw queue,
mutations of the declared queues below, explicit hover replacements, and actual
AppKit receipts in the owned NativeSmoke source window. It does not inject input
or grant a route. Ordinary launches install no recorder. Recording is one-shot,
explicitly armed, limited to 180 seconds, and bound to one exact live lease and
capture epoch. No text strings are collected; raw key codes, modifier flags,
pointer effects and timestamps are private diagnostic data.

**Physical observer provenance and AppKit field preservation are NOT_RUN. Full
pending-queue coverage is INCOMPLETE. H11 is not complete.** Evidence v2 observes
all Core Event lifetimes through pending/saved/retrieved storage and dispatch
completion, with a separate original-stamped subset. Explicit experimental
launches enforce 4096 outstanding Events, 64 active dispatch scopes and a
two-second residence deadline; see [input-queue-budget.md](input-queue-budget.md).
Native OS/library buffers before Core admission, compositor/device queues and
display presentation remain outside that inventory. A normalized trace cannot
certify those boundaries or substitute for a latency recording. Legacy v1
traces retain their original scope and explicit null EventQueue capacities.

## Private configuration and lifetime

Freeze and hash the actual Core, its owned Host, NativeSmoke source, collector
and procedure before a run. Use one new run ID and canonical absolute paths in
an owned `0700` directory. Configuration and command files must be owned regular
`0600` files without symlinks or hard links; output files must not already exist.
No environment variable enables recording.

Each of the three processes gets a separate six-field JSON configuration:

```json
{
  "version": 1,
  "runId": "planned-input-run-01",
  "role": "core",
  "durationMs": 30000,
  "output": "/absolute/private/run/core.jsonl",
  "control": "/absolute/private/run/core-command.json"
}
```

Use `host` and `native` roles with separate output/control files. The duration is
an integer from 1 to 180000 ms. Choose it before collection; do not shorten a
failed interval or restart counters under the same run ID. Waiting for an arm
request also has a 180-second bound. The current H04 physical harness uses a
fixed **30-second** recording inside its existing 120-second visible session;
that is deliberately insufficient for H11's 120-second workload.

The explicitly launched Core's existing `--windowshare-host-config` JSON may
contain:

```json
"inputEvidence": {
  "core": "/absolute/private/run/core-config.json",
  "host": "/absolute/private/run/host-config.json"
}
```

This requires the existing explicit nonempty `inputPeerPairs` policy. Core
forwards the Host configuration through its inherited companion bootstrap.
Standalone, probe, identity-init and help/action combinations cannot use the
private Host evidence option. Evidence paths and role labels never supply code
admission, peer trust, a native owner or input authority.

The owned NativeSmoke source accepts additive manual-mode arguments:

```text
--manual-status /absolute/private/run/manual-status.json
--input-evidence-config /absolute/private/run/native-config.json
--record-request /absolute/private/run/record-request.json
```

Optional `--separate-controls` leaves Start/Stop/status in a second local window.
The Record button always stays in the exported source so it remains reachable
inside the Viewer without replacing the active route. Metadata distinguishes
`sourceWindowId` from `controlWindowId` and includes `controlFrame`,
`controlOriginalFrame` and `controlVisible`, corroborated against the owned
WindowServer ID/PID. Legacy one-window mode remains the default.

## Collection sequence

1. Obtain fresh physical readiness for the explicitly selected disposable
   source. Start the existing production harness. Inspect the exact Core-owned
   Host bootstrap, source PID/window and actual Viewer connection. Do not infer
   original delivery from legacy synthetic-event tags or a protocol ACK.
2. Click visible Start. Enter the Viewer and prime every planned key/button
   control, then release them. Conservative capture ownership deliberately
   denies an unknown first cycle. Prepare scroll gestures as required by the
   scenario. This preparation is outside the measured interval.
3. Click Record in the source window inside the Viewer. It creates exactly one
   new private request file with `{ "version": 1, "runId": "...", "command":
   "record" }`, using exclusive creation. It does not arm a recorder itself.
4. The harness must observe **new** ready-candidate rows after that request.
   Core and Host must report matching exact lease fields and peer identities;
   Native must report its retained owned PID/window. Merely rereading an old
   ready row, or seeing a painted Viewer, is insufficient. Then atomically
   publish separate `arm` commands to the three configured control paths:
   `{ "version": 1, "runId": "...", "command": "arm" }`.
   Require all three new arm rows to report `accepted:true`. Each recorder
   independently rechecks its exact owner and every declared empty queue. A
   capture entering during the start boundary, an unknown baseline, contention,
   evidence overflow or owner replacement invalidates the run without delaying
   or changing ordinary input.
5. Perform the predeclared workload only in the owned source. Keep the exact
   route/focus/geometry throughout the interval. For H04, follow the visible
   short sequence; do not label it H11. H11 requires a separately approved
   collection lifetime with at least 120 seconds of actual mixed input at
   120 Hz, including key/button down/up, drag, scroll and hover. Predeclare the
   hardware/workload and do not use synthetic parser fixtures as measurements.
6. The duration deadline or exact `stop` command freezes source collection.
   Queue mutations and native receipts may continue through a bounded 250 ms
   drain tail. Wait for all three terminal rows before changing selection or
   returning with a routing shortcut. A terminal row must be drained, stopped,
   loss-free and cover all emitted payload/queue rows. The H04 harness also
   checks each process's own `sourceEndNs == arm.timeNs + 30000000000`.
7. Return to the Mac, click Stop and verify exact source restoration. Only then
   send Native's explicit `close` command through its recorder control file.
   `close` is rejected before a terminal row and is unavailable to Core/Host.
   It exits the fixture normally so native event filters are removed and final
   metadata is flushed. Core uses its existing private stop/cleanup path. A
   forced kill, missing terminal marker or early selection change is incomplete
   evidence, even if some receipts are present.

The recorder retains only counters before arm. Capture/native callbacks copy
fixed-size rows into preallocated process-lifetime storage; they perform no
file I/O, serialization, waits or allocations. Timer-driven serialization has
a bounded per-turn drain. Loss is sticky; full buffers never overwrite history.
Process-lifetime storage avoids freeing diagnostic memory beneath a late
producer, but it does not keep a run armed after shutdown.

## Declared queue inventory

Each mutation has separate cumulative enqueue/retirement counters. A row never
combines both directions, so an intermediate peak cannot disappear. Core has
all 17 entries; Host has the last four; Native has no input forwarding queue.
The normalized v2 Core/Host inventory has 21 separately scoped entries.

| Name | Unit/capacity | Observed structure |
| --- | --- | --- |
| capture | 256 records | OriginalInputStream raw capture ring |
| translation | 256 records | Capture source's completed translation ring; **not generic EventQueue** |
| completed | 256 records | Session-to-Server completed-copy inbox |
| server-pending | 256 records | Deferred Server input deque |
| server-completed | 256 records | Server's completed-capture correlation cache |
| server-executing | 256 active calls (diagnostic ceiling) | Server original dispatch stack |
| coordinator-operations | 17 records | GUI/Server coordinator operation deque |
| coordinator-batch | 16 records | Retained mapped batch |
| coordinator-preview | 16 records | Retained batch preview |
| event-outstanding | 4096 Event views (shared limit) | Original-stamped subset of pending, saved, retrieved and dispatch lifetimes |
| event-dispatch | 64 active callbacks (shared limit) | Original-stamped dispatch scopes, including nested InputFilter borrowed views |
| event-all-outstanding | 4096 Event views | All Core Events admitted through pending, saved, retrieved and dispatch lifetimes |
| event-all-dispatch | 64 active callbacks | All Core dispatch scopes, including configured actions and control events |
| private-output | 16384 bytes | Accepted QLocalSocket output, retired by bytesWritten |
| private-buffered-input | 1488 bytes | Actual QLocalSocket receive buffer, observed synchronously at readyRead and around reads/abort |
| private-incoming | 372 bytes | Private frame assembly buffer |
| private-dispatch | 256 active calls (diagnostic ceiling) | Private framed-message callback stack |

The four EventQueue counters count views and callbacks, not additional physical
source records. The move-owned token follows the actual Event across storage
transfers and remains owned by a local dispatch scope through nested callbacks,
including a callback that destroys its Event or queue. Unhandled, dropped,
overwritten or throwing stamped events record sticky loss. Public provenance
mutation and a late/replaced observer also invalidate evidence. Configured and
unstamped actions appear only in the all-event counters, without physical-input
provenance. The original subset and whole-Core counts must not be added together.

The optional base observer uses a process-retained function-pointer target,
installed by Core before EventQueue construction. Event's layout and move rules
are identical in ON/OFF builds. Added callbacks perform no allocation or wait.
Installation is one-shot and checks a monotonic process-wide queue-construction
marker, including already-destroyed queues whose retrieved Events may survive.
Creating a new queue cannot conceal a missing earlier queue baseline. Replacement
is refused, and the recorder/observer target is retained until process exit.
All four counters participate in the same try-lock arm/terminal boundary as other
declared queues: a retrieved event or active callback prevents an empty arm,
and a concurrent mutation invalidates the attempted evidence boundary without
delaying ordinary dispatch. Evidence recording supplies no input authority and
does not enable the production admission guard; explicit session configuration
does that independently.

Some existing execution-depth ceilings bound evidence accounting, not production
queue storage; the Core EventQueue limits are enforced by admission. Private-
output retirement is a Qt write-buffer observation, not peer consumption.
Version 2 requires exact limits of 4096/64 for the four EventQueue metrics.
An unguarded diagnostic fixture serializes null and cannot normalize as a v2
production run. Version 1 still requires explicit null for its two original-
stamped metrics and cannot claim bounded backlog. Null is never inferred from
a missing field, serialized as zero, or replaced with the recorder's 8192-row
capacity. Mixed journal versions are rejected. Accounting, growth, final drain
and loss checks apply to both versions. Unmeasured native/OS queues still
prevent full pending-input coverage.

## Normalization and latency

After the files stop changing, run analysis only:

```sh
python3 deploy/windowshare/input-evidence-normalize.py \
  --core /absolute/private/run/core.jsonl \
  --host /absolute/private/run/host.jsonl \
  --native /absolute/private/run/native.jsonl \
  --output /absolute/private/run/normalized
```

The new output directory is private. The normalizer hashes raw journals,
retains original capture epoch/sequence, and joins a native receipt only to a
**unique exact** CG timestamp, flags and raw-effect tuple. It also checks the
actual owned destination, Host source PID, marker, order, duplicates, missing
records and explicit hover-replacement chains. It refuses ambiguous matches,
timestamp/field transformations, unexplained coalescing and unsupported modifier
toggle representations. It never repairs a value or joins by ordinal position.
AppKit may alter fields; validate preservation on the actual frozen candidate
with a short physical run before attempting sustained collection.

Success means `NORMALIZED_UNVERIFIED`, with `h11_complete:false`,
`pending_queue_coverage.status:"INCOMPLETE"`, `observer_provenance:"NOT_RUN"`,
`native_field_preservation:"NOT_RUN"` and no latency estimate. It emits the
declared-subset traces in the H11 analyzer's format; their syntax or threshold
results cannot certify missing queue coverage or authenticate the observer.

For wired 1920×1080 at 60 fps, configure the actual Mac Host's `captureFps:60`
and collect the independent frame measurements described in
[h11-evidence.md](h11-evidence.md). Use one shared instrument clock or the
predeclared filmed method, retain all planned frames and uncertainty, and test
p95 ≤100 ms and p99 ≤150 ms. Never subtract unrelated Mac/Linux monotonic
clocks, derive latency from scheduled encoder timestamps, or infer it from a
send/ACK. This collector makes no measured performance claim.

Pure regression commands (no input or process launch):

```sh
python3 deploy/windowshare/input-evidence-normalize-tests.py
python3 deploy/windowshare/h11-evidence-tests.py
ctest --test-dir build/mac-default -R '^windowshare-input-evidence$' --output-on-failure
ctest --test-dir build/mac-default -R '^(EventQueueTests|ServerTests)$' --output-on-failure
```

Use the build directory containing the frozen candidate; the C++ suite uses
only pure recorder/queue/config fixtures and private temporary files. The
normalizer tests are explicitly synthetic parser fixtures, never acceptance
bundles. EventQueueTests also runs with WindowShare OFF; the Server regression
uses the real InputFilter borrowed-dispatch path without a display or OS input.
