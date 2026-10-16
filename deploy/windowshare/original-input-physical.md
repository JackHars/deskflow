<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors -->
<!-- SPDX-License-Identifier: MIT -->

# Production original-input physical verification (H04, optional H06/H11 input)

This is an explicit opt-in mode. `physical-macos.py` and `physical-linux.py`
retain their legacy defaults. The commands below enable the real production
Mac coordinator and exact approved peer pair for **one disposable source**;
the actual Ubuntu Core owns its EI connection, GNOME provider, and Viewer.
There is no synthetic input sender, fabricated private descriptor, or fixture
authorization. Authentication or a preparation ACK is not an input receipt.

No H04, H06, or H11 physical acceptance result is supplied by these scripts.
An exit-zero result records a session awaiting human and receipt review. This
default profile records 30 seconds of input inside a visible 120-second session.
The explicit `h11` profile records 120 seconds inside a 150-second session.
Duration alone does **not** establish H11's input workload, native delivery,
queue certification or capture-to-display latency.

Ready offers a separate five-minute countdown. The disposable native source
uses an absolute ten-minute preparation watchdog to cover that wait, startup
and priming; Core and Host retain their three-minute preparation watchdogs.
Neither countdown extends a recording. The source requests foreground once,
and initial readiness requires its own WindowServer on-screen visibility and
settled frame. A pre-Start cancellation verifies only the native recorder's
clean unarmed end and the absence of Core/Host evidence; it is not a measurement.

## Prepare the exact artifacts

Use a frozen, separately packaged experimental Mac app with
`org.deskflow.seamless.core.experimental` as its bundle identifier,
`deskflow-core` as its main executable, its sealed exact-library manifest,
and the sibling Host and holding helper. Use the matching newly built
`windowshare-smoke-source` with the evidence and separate-control options.
An installed ordinary Deskflow app is not a substitute. Grant the experimental
package's required Accessibility and Screen Recording permissions normally;
the harness checks its native probe before offering Start.
The private `tls/trusted-clients` location is a file. Create only its `tls`
parent directory; plan validation rejects a folder or link at the file path.
The coordinator writes the public pin before opening the source window.

First create a fresh private Mac preparation as specified in
[physical-macos.md](physical-macos.md), including its private INI, identity
modifier maps, two explicit routing hotkeys, fresh TLS identities, private
trust directory and version-1 preparation JSON. This is configuration only;
do not start its legacy physical session. The settings file's parent must
contain its private `tls/trusted-clients` location. All preparation files and
directories remain inside that private directory, with modes 0600/0700.
Use the planned loopback ports (defaults 24860 and 24861) on both machines.

Freeze a separate production plan in **the same private directory**:

```sh
python3 deploy/windowshare/original-input-physical.py freeze \
  --mac-plan /absolute/private/mac-preparation.json \
  --bundle '/absolute/frozen/Deskflow Seamless Experimental.app' \
  --source /absolute/frozen/windowshare-smoke-source \
  --linux-desktop \
  --output /absolute/private/mac-original-plan.json
```

`freeze` reads files only. It does not launch a binary, install an app, create
keys, change permissions or claim code admission. It records the exact Core,
Host, source, holding helper, Info.plist, CodeResources and library-manifest
SHA-256 values in an explicit v2 plan. Runtime rechecks these after Start and
requires the real Core's exact-library admission and authenticated companion
bootstrap. Core launches Host through its genuine inherited private channel;
the harness never launches a standalone original-input Host.

On the actual Ubuntu computer, prepare a new v3 Linux run using the Mac plan's
public `coreFingerprint` and `hostFingerprint`. Install the default-off provider
and identify the active graphical session as described in
[physical-desktop.md](physical-desktop.md). This is the required environment
for the user's physical monitor acceptance:

```sh
python3 deploy/windowshare/physical-linux.py prepare \
  --production-original-input --desktop-session LOCAL_GRAPHICAL_SESSION_ID \
  --run /absolute/new/private-original-run \
  --core /absolute/frozen/linux/bin/deskflow-core \
  --viewer /absolute/frozen/linux/deskflow-windowshare \
  --mac-core-pin MAC_CORE_SHA256 --mac-viewer-pin MAC_HOST_SHA256 \
  --ordinary-pid ORDINARY_DESKFLOW_PID \
  --ordinary-config /absolute/ordinary/Deskflow.conf
```

Repeat the ordinary PID/config options for all relevant existing processes
and settings. If ordinary Deskflow is stopped on Ubuntu, use the explicit
`--no-ordinary-core` option instead of `--ordinary-pid`. This audits the absence
of a Core at preparation, startup and after cleanup; a newly running Core fails
the audit. This mode uses the existing physical GNOME Wayland desktop;
the exported source appears on its actual monitor. Both plans must explicitly
select `user-desktop`, and no container/image/preview may be substituted.
Preparation is inert. The temporary provider session and experimental Linux
Core/Viewer start only after the visible Start click. Production startup requires the provider's
actual preparation, mapped-drain, routed-point and supported ordering-contract
capabilities. Unsupported prerequisites fail the run. Preserve the full
private Linux results and source/binary/desktop identity hashes. Copy only
`linux-plan.json` to the Mac; no private key leaves either machine.

The older isolated diagnostic remains available with Mac `--desktop-preview`
and Linux `--desktop-preview --image sha256:EXACT_INSTALLED_IMAGE_ID`, omitting
both actual-desktop selectors. That session is headless: **its window does not
appear on the ordinary Ubuntu monitor**, and it cannot satisfy actual-monitor
acceptance. The two preview options make
the actual test desktop and pointer visible in a separate view-only Mac window.
Use that view while input is routed right. The visible original Mac source is
not the Ubuntu desktop and must not be used to aim the remote pointer.
The preview reads only the private compositor through a separate bounded SSH
image stream; it cannot inject input or establish original-delivery authority.
The session cannot become Active without live, visible preview receipts, and
stale/hidden/disconnected previews stop it. It closes after recording completes
to reveal the local source's Stop button. Existing plans without this explicit
option retain their historical headless behavior; do not use them for this
attended pointer test. Preview overhead is not a latency acceptance measurement.

Validate the complete plan pair without starting processes or SSH:

```sh
python3 deploy/windowshare/original-input-physical.py \
  --validate-only \
  --mac-plan /absolute/private/mac-original-plan.json \
  --linux-plan /absolute/copied/linux-plan.json \
  --ordinary-mac-config /absolute/ordinary/Deskflow.conf \
  --output /absolute/new/original-plan-review
```

The review directory contains prospective Core/source arguments, the sole
approved Core/Viewer peer pair, and private per-role recorder configurations.
It is not reusable as a live result directory. No original-input authority
comes from this validation. Version-1/v2 legacy plans and version-2/v3
production plans cannot be mixed.

## Run H04 with the person at the Mac

Before requesting launch permission, run the same read-only Mac conflict check
used at launch and Start:

```sh
python3 -c "import runpy; runpy.run_path('deploy/windowshare/physical-macos.py')['no_other_primary']()"
```

If the regular Mac Deskflow session is running, ask the person to click Stop
in that app. Do not signal or replace its process. Refresh ordinary-process
snapshots after the person changes their running sessions, then obtain fresh
permission for the physical attempt. This check opens no test window, runs no
native input hooks and changes no settings.

Use an existing SSH key and an already verified known-host entry. The
coordinator uses strict host-key checking and never accepts a password through
its plan or logs. List every ordinary Mac settings file to preserve.

```sh
python3 deploy/windowshare/original-input-physical.py \
  --mac-plan /absolute/private/mac-original-plan.json \
  --linux-plan /absolute/copied/linux-plan.json \
  --ssh-target USER@UBUNTU_IP --ssh-key /absolute/existing/ssh-key \
  --ordinary-mac-config /absolute/ordinary/Deskflow.conf \
  --ordinary-mac-config /absolute/ordinary/deskflow-server.conf \
  --output /absolute/new/original-physical-results
```

The source is the only exported window. The loopback listeners use temporary
private SSH sockets, and only the explicitly paired fresh TLS identities are
approved. A second running Mac Core is refused to avoid overlapping capture.
No input hooks or Linux native session start before the visible Start click.
Before opening the test window, the coordinator runs the packaged Core's
`--windowshare-code-admission-probe`. It authenticates the real companion,
performs the production exact-library inspection under its existing deadline,
and stops the companion with no input hooks, ordinary Core state or network
listeners. Admission failure now stops unattended setup before asking for Start.
This diagnostic does not supply authority to the later physical session, which
authenticates and inspects its own newly started endpoints again. The probe is
also available through `launch-smoke.py CORE_BUNDLE.app --code-admission` for
separately signed exact-library packages; ordinary ad-hoc CI packages cannot
pass exact-library admission.

1. Wait for the source's **Ready**, then click **Start** within its countdown.
   Leave the Mac source in place and wait for **Active** and its shared window
   on the actual Ubuntu monitor.
   Start checks both routing-hotkey registrations and
   launches the actual Linux Core/provider/Viewer. The 120-second session
   deadline cannot be extended or restarted.
2. Press **Control+Option+Shift+Right**, release all modifiers, and dismiss
   overview with Escape if needed. Look at the physical Ubuntu monitor, locate
   the shared test window, and click its text box using the Ubuntu pointer.
   Setup actions are retained as setup; they are not evidence of
   successful original delivery.
3. Prime the intended physical controls: click/release inside the exported
   source, press/release a, b, c, x and Shift, and finish a short scroll gesture.
   The production transition policy intentionally denies an unknown first
   acquisition cycle. Do not invent a neutral physical snapshot to bypass it.
   Release everything and wait for the route to settle. If the source/Viewer
   cannot be reached or focus is wrong, Stop; do not claim the run succeeded.
4. Press **Return** in the source's text box, or click its distinct **Record**
   button in the Viewer. Both request the same one-shot recording. Stay on the
   Ubuntu route; returning left replaces the route being measured. Both controls
   remain in the exported source even when H06 uses separate local controls.
   They create a single exact run request; they do not grant input permission.
   The harness waits for **new**, matching Core/Host owner/identity readiness
   rows after that request and the exact source PID/window in the native
   recorder. It attempts arm once and requires accepted receipts from all
   three recorders. A denied, partial or changed-owner arm is terminal.
5. Wait for the coordinator's `evidence-recording` event before the measured
   sequence. For 30 seconds, type a, Shift+b, c; hold/release x; move the
   pointer, click/drag, and scroll vertically and horizontally inside the
   disposable source. Release each control. Do not change applications,
   routing owner, source window or geometry while recording. Unrelated keys
   are not a substitute for the declared primed controls.
6. Finish with all controls released and the pointer still. Wait for
   `evidence-complete`; terminal rows require the declared queues to drain.
   Only then press **Control+Option+Shift+Left**, release the modifiers, and
   click **Stop** locally. Switching route before evidence completes changes
   the owner and invalidates that measurement; the harness does not trim the
   failure out or rearm. Stop is always available for early cancellation, and
   the absolute deadline remains an independent stop.

The 30-second recording must begin with at least 35 seconds left in the
visible session. There is no retry within a run. Start a new run with fresh
plans/results after a failure. The source recorder has a bounded preparation
lifetime too; an unusually slow setup can fail rather than silently reset it.
After identifying the packaged Host child, the coordinator allows 50 seconds
for 15-second child authentication and subsequent 30-second exact-library
inspection plus scheduling grace. It still requires both
success markers, the private IPC socket and both hotkeys; runtime admission
uses a single absolute startup deadline with no retry or renewal. The original
15-second inspection allowance expired during full-package concurrent startup
on the Intel Mac; pending, expired or failed checks still provide no authority.
Final fixture closure uses a
separate explicit command after terminal evidence and restoration checks, so
the manual-close safeguard cannot veto the coordinator's final cleanup.

## Optional H06 holding lifecycle

Create another fresh production plan with `freeze --holding-display`, and
also pass `--holding-display` to both `--validate-only` and the live command.
This deliberate two-part opt-in sets Host holding mode and launches the source
with `--separate-controls`. The separate local control window must have its
own nonzero native ID, distinct from the source's ID. Its exact WindowServer
PID/ID, visible state, original frame and Qt/native frame agreement are checked
throughout. **Only the source window ID is exported.**

Holding startup waits asynchronously for the helper and the parent's verified
display state. Native display-change notifications drive verification within a
fixed deadline; a helper reply alone cannot authorize placement. Requested
holding setup fails and restores on loss/timeout instead of committing a visible
fallback. Default runs without holding retain their source-visible behavior.
Stage/error diagnostics are flushed to logs; no results popup is launched.

Start and Stop remain in the local control window; Record remains in the
exported source. Follow the same steps, then return left to those local
controls. The run must observe a source-frame relocation and later the exact
original source frame, no held keys/buttons, and an empty restore journal
while Host is still alive. Those lifecycle observations are recorded separately
from `holdingDisplayAcceptance:"not-assessed"`. A changed, hidden or moved
control window fails the run and triggers owned cleanup. This is not an
arbitrary-app compatibility test or proof of two physical monitors/edge drag.

## H11 sustained-input collection profile

For a separately predeclared H11 input run, add `--recording-profile h11` to
the **freeze** command. The new private Mac plan names `recordingProfile:h11`;
validation and execution derive every recorder's 120,000 ms duration and the
source's 150-second session from that fixed profile. There is no live duration
override. Old production plans without this field retain the H04 defaults.
Legacy plans cannot select a production profile. Use the newly built frozen
source with `--h11-input` support; the coordinator supplies that argument.

Run `--validate-only` first. Its report must name `h11`,
`predeclaredRecordingSeconds:120`, `maximumSessionSeconds:150` and three
120,000 ms recorder configurations. Then follow the same visible Start/Record
flow with the person present. Complete routing, priming and the Record request
within the first 25 seconds of Active; arm requires more than 125 seconds left
for the complete interval and safe return. A late or partial arm ends the run;
it cannot reset the clock or select a shorter passing interval. Each recorder
also enforces its own absolute duration if the coordinator stalls. An early
Stop, including a 30-second tail, cannot complete an H11 recording.

The Ubuntu controller's existing 180-second active and 270-second total
resource deadlines stay fixed. The Mac requires at least 165 seconds remaining
on the live Linux route before its 150-second countdown starts. File, row,
ring and drain bounds are unchanged. A slow preparation can still expire.

Predeclare and perform the complete mixed-input workload in
[h11-evidence.md](h11-evidence.md), including the 120 Hz cadence, complete native
receipts and queue traces. Normalize and review the retained logs with
[input-evidence.md](input-evidence.md). A recorded session remains
`physicalAcceptance:not-assessed`; it is not an H11 PASS. Full native/library/
OS queue certification remains incomplete, and physical display latency needs
its own common-clock evidence. This profile supplies neither result.

## Evidence and teardown

The three private recorder configurations share a fresh run ID but retain
their independent clock IDs. Readiness freshness uses local observation of
new rows, never subtraction of process-local clocks. Arming revalidates the
actual owner and queues independently in each process. Native rows preserve
the CG timestamp and raw effect tuple; a tag, event ordinal, PID or ACK alone
cannot prove original-field preservation. The harness records no text-field
contents, but raw key codes and input metadata are sensitive: retain this
bundle privately.

The evidence Stop marks the source interval end and permits only the bounded
declared-queue/native receipt tail. Terminal rows must be loss-free and
drained. This is local instrumentation, not an assertion that OS/compositor
queues were consumed. Cleanup then uses the Linux Core's private IPC stop,
checks fresh source restoration while Host is alive, and stops the exact Mac
Core using a kernel-identified private IPC peer. Core owns its Host's lifetime.
The harness verifies the same recorded Host birth identity exited; it never
signals a discovered unrelated process. Only after final source/control/frame
checks does native `close` exit the fixture cleanly. Forced cleanup remains a
failure. Ordinary settings/PIDs/containers and exact owned socket cleanup
are audited separately.

Preserve `result.json`, source/native `receipts.jsonl`, all three
`evidence/*.jsonl`, their configurations, Linux transcripts/results, and the
frozen hashes. Use [the input-evidence normalizer](input-evidence-normalize.py)
and [H11 evidence guidance](h11-evidence.md) for independent raw-field matching
and provenance review. Do not pair source/native records by order after a
loss. A clean recorder lifecycle remains `physicalAcceptance:"not-assessed"`
and `nativeFieldPreservation:"not-assessed"` until that review and the person's
visual result are recorded. Do not edit a failed trace into a successful one.

Pure tests require no native windows, input, SSH, or containers:

```sh
python3 deploy/windowshare/physical-macos-tests.py
python3 deploy/windowshare/physical-linux-tests.py
python3 deploy/windowshare/physical-preview-tests.py
```
# Unattended setup verification

With explicit authorization to run disposable desktop tests, add
`--pipeline-check` to a freshly prepared production holding-display launch.
This requires `linuxEnvironment: user-desktop` and the H04 recording profile.
The source fixture invokes its own guarded Start button after coordinator
readiness, using `--automated-start`; it does not synthesize a system click.
The same packaged Core, authenticated companion, Ubuntu desktop, hidden-source
verification, disconnect and source-restoration path runs for 30 active seconds.
Stop still cancels the run. Any early stop, placement loss or cleanup error fails.

All three diagnostic recorders must stop cleanly without arming or recording
input. Success is labeled `pipeline-verified-no-physical-input`, with physical
acceptance still `not-assessed`. This does not exercise real keyboard/trackpad
delivery and cannot satisfy H04/H06 physical acceptance. Normal launches still
require a human Start click and retain their original recording requirements.

