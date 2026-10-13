# Experimental window sharing

## Current implementation — 2026-10-13

The configured Mac Core now connects original captured input to the exact
exported source through the production coordinator. Private protocol v5 and
Core IPC5 retain the authenticated selections, full forwarding gate, native
queue seal, mapped point/deadline and completed cleanup. Up to 16 independently
mapped records share one closed prefix; held input survives point expiry while
the exact owner remains valid. This is explicit opt-in with paired certificates
and native/code admission, not the ordinary Deskflow default.

The current working candidate also includes exact Qt xdg_toplevel-to-Mutter
window association, painted-frame holding relocation, lifecycle fencing,
attached-sheet/drawer local return, and opt-in focused text clipboard. X11
namespace and hit checks are implemented, but X11 original delivery is denied
without a proven native application-input drain. Unknown Mutter builds and
unsupported roles/input contexts are likewise unavailable.

First-click preparation now has a distinct `IPP1` gate and focus-only activation
before the genuine Host All lease. Mac protocol/coordinator tests and scoped
sanitizers pass; native Linux verification is still in progress. Natural
bidirectional edge dragging remains open.
Physical input, real app compatibility, two monitors, installation and sustained
performance still require acceptance on the final candidate. See the checked
implementation roadmap in [`CHATGPT_HANDOFF.md`](../CHATGPT_HANDOFF.md), precise
test evidence in [`TESTING.md`](../TESTING.md), the
[input path](../deploy/windowshare/original-input-dispatch.md), and
[clipboard policy](../deploy/windowshare/focused-clipboard.md).

The experimental Mac host accepts `--capture-fps 30|60`, or numeric
`"captureFps": 60` in its companion configuration. The default is 30;
visible inactive and hidden streams retain their 10 and 2 fps budgets.
ScreenCaptureKit and VideoToolbox use the selected effective rate. Real capture
timestamps are preserved; recovery re-encoding uses a separately bounded
synthetic presentation timestamp. Selecting 60 is a requested rate, not a
measured delivery guarantee. See the
[performance evidence procedure](../deploy/windowshare/h11-evidence.md).

The dated sections below record historical checkpoints; their statements about
disabled or missing production coordination apply to those named revisions.

### Requested input cutoff and exact GNOME/Qt drain (2026-10-13)

The Core now negotiates a bounded, authenticated cutoff for a captured input
prefix, asks its real EI sender for a whole-batch PONG observation, and resumes
only that exact transaction. A separate provider operation joins an empty
Clutter queue and later frame to the Viewer's exact full-input fence and Qt
queue drain. Resume invalidates retained receipts before acknowledgement;
unsupported input contexts and nested dispatch refuse the drain.

The Mac build, nine targeted suites and scoped proxy sanitizers pass. Home
Ubuntu passes 31 integrated suites, three actual feature-disabled suites and
private GNOME/Qt at scales 1/1.25, including revocation before expiry.
`TESTING.md` records frozen source, scope, cleanup and the fixture correction.
Implementation `bbe9ed3f2` also passed all six
[hosted CI jobs](https://github.com/JackHars/deskflow/actions/runs/38420241004).
This is a P2 component milestone:
the production coordinator and verified Mutter sender-ordering prerequisite
are still required before original-input delivery can be enabled.


### Local implementation return (2026-10-13)

The cloud code is integrated. Private preflight now supports a bounded wait
after the exact full-input forwarding fence closes, followed by one explicit
continuation that rechecks all authority conditions. Stale or duplicate
completion cannot activate a newer lease, and direct channel calls cannot
bypass the wait. This supplies the asynchronous preparation boundary needed
by the remaining native cutoff coordinator; original delivery stays disabled.

The Mac watchdog also recovers when its exited host has not yet been reaped.
Its isolated native regression failed before the fix and passes afterward.
Both local applications build, the five core suites pass 170 Qt entries, and
the new preflight/channel/companion suites pass. See `TESTING.md` for current
hosted CI and the precise distinction between these checks and hardware tests.
WiFiman is restored. The home Ubuntu baseline passed 31 application/core
suites, three feature-disabled core suites and private GNOME runs at scales
1 and 1.25, with ordinary Deskflow and unrelated containers preserved.
Physical input, two-monitor,
multiple-window and bidirectional edge-drag acceptance remain unfinished.

### P2 native GNOME StageView observation and strict reply validation (verified partial)

The actual GNOME Shell 50 extension now observes two same-stage-view
`before-update`/`after-update` pairs at the Server's frozen stage point.
It uses Mutter's `StageView.layout` boxed property on the native owner
thread, retaining only primitive x/y/width/height values across updates,
and rejects changed focus/point/surface/layout, stage mismatch, deadline,
and reentrancy. Its authenticated private D-Bus response schema is
validated with all thirteen mandatory **unique, correctly typed** keys,
including duplicate and substituted field rejection.

[`e117b66e`](https://github.com/JackHars/deskflow/commit/e117b66e22e7f2141b4a17b93f81e22c736f5b4c)
passed [six-job native CI](https://github.com/JackHars/deskflow/actions/runs/38408250298)
and [source bundle](https://github.com/JackHars/deskflow/actions/runs/38408250240).
This observation precedes the existing default QtWayland queue witness
and frozen GNOME hit recheck; it is **not** native input queue drainage,
device/EIS attribution, original-input delivery permission or physical
acceptance. The full original route remains **disabled**, H01–H12
**NOT_RUN**. Reproduction instructions in `TESTING.md`.

### Post-Qt native routed-point freshness (verified read-only P2 slice)

After the actual QtWayland default-queue observation, the client queries GNOME's exact owner/native hit for the original frozen routed coordinate a third time before publishing. A silent native revision change or local surface-transform drift denies the old observation. Code `32cb0cb9`, test checkpoint `13e62c02`, [native six-job CI PASS](https://github.com/JackHars/deskflow/actions/runs/38386740966). The EIS/Clutter old-event drain and original-input permit remain **unproven and disabled**, hardware H03–H05 **NOT_RUN**. See repaired `TESTING.md`.
### P2 QtWayland default-queue checkpoint (verified code, original input still disabled)

The GNOME private, authenticated Viewer viewport endpoint now routes final
`Check` through an asynchronous `QtWaylandQueueWitness`: two serial
`wl_display_sync` callbacks on the existing QtWayland *default* GUI event
queue, followed by a QPA pending-event dispatch and exact native/painted/focus
revalidation. Unsupported Qt builds, stale sessions/surfaces, callback reentry,
deadline, disconnect and destruction deny. The endpoint never opens a new
Wayland connection or native-input endpoint, and a QPA send result does not
claim application consumption.

Implementation [`d0b8e8a3`](https://github.com/JackHars/deskflow/commit/d0b8e8a3181dc70fd2ea462e261e3a3685704a70)
passed all six hosted CI jobs including native GNOME/QtWayland 6.10.2, Mac
and Linux application/core, and sanitizer foundations
([run 38383713813](https://github.com/JackHars/deskflow/actions/runs/38383713813)).
The [source bundle](https://github.com/JackHars/deskflow/actions/runs/38383713764)
also passed. The old libei → Mutter/Clutter → Qt source-bound drain still lacks
an authoritative compositor-side boundary and original input remains disabled.
Physical H03–H05: **NOT_RUN**. See `TESTING.md` for reproducibility.

### P4 local Mac monitor-topology safety (verified partial; not H05/H06 acceptance)

The experimental Mac host now records physical display identity, logical
bounds, pixel-mode dimensions and rotation. A changed/disappeared output
during export, holding-display relocation or the 500 ms owned safety poll
revokes held input owners and restores journaled source windows; equal geometry
alone never proves display identity. Malformed/unavailable display state fails
closed. The private holding display remains excluded from this fingerprint.

Implementation [`ec2fd2f8`](https://github.com/JackHars/deskflow/commit/ec2fd2f83fa6a308f1702036eb0bf4f3ff6dc079):
GCC/Clang sanitizer suites **11/11 each**, native [six-job CI](https://github.com/JackHars/deskflow/actions/runs/38352550451)
**PASS**, and [source bundle](https://github.com/JackHars/deskflow/actions/runs/38352550445)
**PASS**. Physical hotplug, scaling, multi-window focus, Qt/Clutter input
drain and original delivery are **not validated**. See `TESTING.md` for the
exact test commands, recovery instructions and remaining H05/H06 scenarios.

### Independent Mac crash-recovery watchdog (verified partial P7)

The experimental Mac host now requires an independent watchdog's private
readiness acknowledgment **before** relocating any exported source. Its
separate process group survives a host crash, verifies the original host's
kernel process-birth identity, and calls the existing journal-locked, exact
window-identity `--recover` mode after that host exits. If the watchdog
cannot start or dies, hosting fails closed or terminates and begins normal
restoration. The watcher cannot receive or authorize remote native input.

Implementation `5cc1b760`: GitHub hosted CI **6/6 jobs passed**, including
the macOS native build, watchdog subprocess regression and experimental
packaging; source bundle passed. Physical kill/holding-display acceptance
H10, hardware sleep/wake and real TCC are **NOT_RUN**. Original input remains
disabled. See `TESTING.md` for exact evidence and commands.

### Recent P4 monitor-restore improvement (cloud candidate)

Mac restore and journal recovery use the same physical-output selector:
maximum saved-window overlap, then nearest surviving output on unplug or
layout change. It is deterministic for negative/vertical origins, skips invalid
outputs, excludes the private holding display in the Mac enumerator, and
refuses unsafe restoration with no physical output. Portable GCC/Clang
sanitizer regressions pass and all six hosted CI jobs including Mac compilation
passed for `91dba759`. Physical monitor acceptance is pending.
This is **not** multi-monitor seamless ownership or P4 completion.

## Incremental implementation checkpoint (2026-10-12)

Additional partial implementation now includes fair keyframe recovery across
congested independent streams, a capability-gated read-only
`ValidateRoutedPoint` GNOME hit query for a frozen compositor-stage coordinate,
and a headless native Mutter regression demonstrating that this coordinate is
not silently replaced by the latest cursor sample. Native and painted
observations retain their existing absolute deadlines and revocation rules.
None of these tests proves compositor input consumption or original-input
authority: production original delivery is still disabled.

The experimental Viewer refuses input-method preedit and commit events rather
than treating asynchronous composition as drained; Unicode IME workflows
remain **unsupported** pending a proven lifecycle and hardware acceptance.

The experimental Linux binary/core staging archive uses an inert, opt-in layout
and a reproducible member manifest, without installing or enabling the GNOME
extension or overwriting ordinary Deskflow. See
[`package-linux.md`](../deploy/windowshare/package-linux.md) and
[`TESTING.md`](../TESTING.md) for scoped verification.

This branch contains a native Mac capture host and independent Qt viewer windows.
It is an opt-in development milestone, not yet the completed drag-between-desktops
feature. The ordinary Deskflow build remains the default.

## Current cloud integration checkpoint

The Server now holds pending asynchronous preparation without emitting the
original or a legacy acquisition. Polls retain exact source order, frozen routed
coordinates and the original selection. Capture-arrival and native-preparation
deadlines are separate, and reentrant cancellation preserves physical-release
tracking. This is tested against the actual enabled/disabled core and protocol
proxies; it is not native drain authority or hardware acceptance. See
[`TESTING.md`](../TESTING.md) for the candidate's actual commands and results.

## Roadmap status

The production input integration is implemented and undergoing final candidate
verification. The original cloud handoff's 50% estimate is historical. Code
completion and physical acceptance remain separate: no count of passing unit
tests establishes that a real keyboard, trackpad or desktop drag works.

For cloud implementation, use [`CHATGPT_HANDOFF.md`](../CHATGPT_HANDOFF.md).
The cloud implementer records available automated results and the procedures
for this local chat in [`TESTING.md`](../TESTING.md); hardware acceptance stays
pending until the returned code is tested on the actual machines.

| Milestone | Current evidence | Work required to finish |
| --- | --- | --- |
| Capture, viewer and transport | The native own-window smoke passes. Actual Mac capture to the home Ubuntu GNOME/Qt viewer, exact native pointer/click/scroll/F8 receipts and disconnect cleanup pass at Qt scale factors 1 and 1.25. | Verify the complete interactive flow across the two machines using physical Mac input. |
| Original input integration | Production coordinator, private v5 mapped delivery, IPC5 retained prefix/batches and audited Mutter build ordering are implemented. Exact cleanup requires both Host release and Core resume. | Complete first-click preparation and run final native/physical acceptance. |
| Linux and two monitors | Home Ubuntu builds and targeted tests pass with window sharing enabled and disabled. Native GNOME/Qt tests and both synthetic two-machine runs also pass. The physical desktop has one active monitor. | Complete input integration and verify compositor monitor scaling and both physical monitor destinations. |
| Multiple windows and holding display | Actual Qt role challenges select unique owned Mutter windows; holding relocation retains the first-painted geometry and exact restore journal. | Verify the new native multi-window fixture, focus changes, both monitors and physical failure recovery. |
| Drag out and drag back | Transactional session primitives exist. | Implement and test edge-drag ownership in both directions using Deskflow topology. |
| App compatibility | Exact original modifiers/repeats and opt-in focused text clipboard are implemented. Attached sheets/drawers return their parent locally; remote transients and IME remain unsupported. | Run native, browser and Electron shortcut/dialog/menu/copy-paste scenarios and resolve supported-path failures. |
| Recovery, performance and release | Journal/watchdog recovery, immediate lifecycle fencing, bounded fair media queues, passive CPU/RSS tooling and experimental packaging exist. | Verify final packaging, real-window crash recovery, latency/resource soak and installation/upgrade/removal. |

WiFiman access to home Ubuntu was restored on 2026-10-12, and SSH verified
hostname `jack` at `192.168.1.12`. The GNOME viewport milestone
`04bf68b4c044e1adf2ff463850ae8d1c35b76dad` passed all six CI jobs in run
`38296548399`, including the real native headless GNOME 50 tests for EIS/Wayland
ownership, the actual Qt painted viewport and caller-loss cleanup. Its production
source bytes also match the passing home Ubuntu candidate. These results do not establish
the physical two-machine interactive flow. Read-only display inspection currently
lists one active Samsung HDMI display at 1920×1080 and no second connected output.
The Mac lid is open and the experimental app has Screen Recording and
Accessibility permission; physical input and two-monitor handoff remain untested.
The Mac pointer fix `79de560ca3682e4fcc0bb973bc94abf1e22379c3` also passed all six
CI jobs in run `38299021367`. Its exact-library experimental package is installed
separately, with the previous experimental installation retained as a backup.
The 125% two-machine run used that installed host.
See `WORK_STATE.md` for test logs, package details and remaining acceptance tests.

A subsequent bounded human-test session also connected the actual Mac primary
to the Ubuntu core's provider-owned EIS/viewer session. It received no human
input before its automatic shutdown, so physical delivery remains unverified.
The test closed cleanly and preserved the ordinary installations. Its native
source recorder now exposes bounded event histories and monotonic sequences
so a collector can detect any missing input receipts.

A later physical test exposed reversed Mac hotkey modifiers. Registration now
uses `alt` for Option and `super` for Command, matching ordinary key capture.
Configurations that worked around the previous reversal need their hotkey
modifiers adjusted. The mapping regressions pass, but a complete physical input
round trip remains unverified.

The next manual test uses a visible Start/Stop control and instructions inside
the disposable source window. Its countdown begins after connection readiness;
the Linux preparation wait has a separate deadline. Fresh receipt, cancellation
and cleanup checks remain separate from the human input verdict. See
[`physical-macos.md`](../deploy/windowshare/physical-macos.md) for the procedure.

The negotiated input trace preserves the actual legacy input bytes and records
backend API submissions separately from native delivery. Its bounded packet
parsing, retirement and compatibility tests are documented in
[`input-sequence-trace.md`](../deploy/windowshare/input-sequence-trace.md).
Passing those tests does not replace physical input acceptance or enable
original-input delivery. Local packaging still awaits the requested Mac App
Management permission.

The final input-trace candidate passes 150 Qt test entries on both Mac and home
Ubuntu. Ubuntu also passes 26 compatibility entries with window sharing disabled;
29 trace entries are intentionally skipped in that build. All six CI jobs pass
on code revision `5de96808c` in
[run 38315383611](https://github.com/JackHars/deskflow/actions/runs/38315383611).

The full-input forwarding milestone `d879b781b` additionally passes nine Mac
suites and 30 home Ubuntu suites. Actual private GNOME 50.1 tests at Qt scales
1 and 1.25 verify that key, button, movement and scroll forwarding pause and
resume together, with exact controls and seven ordered fresh input receipts
per run. These tests use a synthetic frame and do not exercise physical Mac
input or compositor monitor scaling. Source and cleanup evidence are recorded
in `WORK_STATE.md`. All six CI jobs pass for this code revision in
[run 38318502915](https://github.com/JackHars/deskflow/actions/runs/38318502915);
original-input delivery remains disabled.

The subsequent Server ownership milestone `ef5145665` passes 161 core Qt entries
on both Mac and home Ubuntu. Ubuntu additionally passes three original-input
foundation suites and a fresh feature-disabled actual-core build with three
compatibility suites. The Server can reserve original output before any legacy
acquisition, preserve per-press ownership and resolve deferred work on selection
changes. The production launcher has no original dispatcher yet. See
[`original-input-dispatch.md`](../deploy/windowshare/original-input-dispatch.md)
for the contract and `WORK_STATE.md` for evidence and the remaining native handoff.
All six CI jobs pass for this implementation in
[run 38323926279](https://github.com/JackHars/deskflow/actions/runs/38323926279).

### Actual two-machine synthetic acceptance

The coordinator exports only its own disposable Mac source window. A private
SSH Unix-socket forward carries mutually pinned TLS to a network-isolated
container on Ubuntu. That container runs a real headless GNOME 50.1 compositor,
Qt 6.10.2 viewer, production provider client and synthetic libei sender.

Both scale runs verify a decoded 1280×896 Mac frame, three fractional pointer
positions against fresh native/painted observations and actual tagged Mac native
receipts, exact click/scroll coordinates and F8 down/up. The source has no held
keys or buttons afterward. Disconnect clears its export journal while the Mac
host is still alive; the original source frame remains intact. Cleanup removes
the owned processes, container and forwarded socket. The ordinary Ubuntu client
remains running.

These tests do not move the source onto a holding display or exercise physical
Mac input, ordinary Deskflow routing, drag handoff, or a physical Linux desktop.
The compositor monitor remains at scale 1; `QT_SCALE_FACTOR=1.25` tests Qt content
scaling. The TLS relay records coordinated socket closure separately from a
fully drained EOF, including any undeliverable encrypted Mac-to-viewer tail.

Prepare a fresh Linux plan as described in
[`crossmachine-linux.md`](../deploy/windowshare/crossmachine-linux.md), then run:

```sh
python3 deploy/windowshare/crossmachine-macos-smoke.py \
  --host /absolute/path/DeskflowSeamless-Core-Experimental.app/Contents/MacOS/deskflow-windowshare \
  --source /absolute/path/windowshare-smoke-source \
  --ssh-target USER@HOST --ssh-key /absolute/path/to/existing-ssh-key \
  --plan /absolute/path/fresh-linux-plan.json --output /absolute/path/new-results
```

Keep both machines' results and hashes. The Mac coordinator requires actual
receiver evidence; a Linux-only pass or EIS synchronization reply is insufficient.

### Experimental GNOME client configuration

The opt-in Linux core accepts `client --windowshare-gnome-config FILE` on a
Wayland desktop. The version-pinned provider must already be enabled in GNOME
Shell 50; the build does not install or enable it. The core owns a private D-Bus
connection, adopts that provider's EIS descriptor and launches the configured
viewer through Mutter's dedicated native Wayland client. Provider, caller,
viewer or EIS loss ends this explicit client session. It does not reconnect via
a replacement portal. Clipboard portal integration is not implemented in this
mode. Ordinary builds and invocations continue to use their existing backend.

The JSON configuration has a closed schema. `version`, `viewerExecutable`,
`connect`, `identity` and `peerPin` are required. `port` defaults to 24810 and
`screen` to 0. `exportWindow` is an optional nonzero uint32 decimal string.
`shareClipboardText` is an optional boolean, default false; sharing also
requires the host's explicit policy and exact current exported-window focus.
The executable must be an existing absolute executable path; identity must be
an absolute path, and the pinned fingerprint must have 64 hexadecimal characters
and cannot be all zeroes. Arguments and environment overrides cannot be supplied
in this file.

```json
{
  "version": 1,
  "viewerExecutable": "/absolute/path/to/deskflow-windowshare",
  "connect": "mac-hostname",
  "identity": "/absolute/path/to/viewer-identity",
  "peerPin": "REPLACE_WITH_THE_MAC_WINDOWSHARE_CERTIFICATE_FINGERPRINT",
  "exportWindow": "123"
}
```

The example requires the real paths, host, certificate fingerprint and catalog
window ID. Session setup and native observations do not authorize original-input
delivery. See `deploy/windowshare/gnome-provider/README.md` for the supported
compositor boundary and the remaining pointer/viewport checks.

The core also passes a private Unix socket to the provider-owned viewer. A
bounded observation joins a fresh GNOME surface hit to that viewer's last
painted frame, source geometry, focus revision and authenticated peer. It uses
Qt's native Wayland content transform to account for client-side decorations
and scaling; letterboxing is excluded. The 250 ms deadline starts when the core
queues the request and is never renewed. Surface, geometry, focus or connection
changes revoke retained observations.

The real headless GNOME 50.1/Qt 6.10.2 fixture passes with `QT_SCALE_FACTOR=1`
and `1.25`, including exact agreement with the viewer's normal pointer packets
and rejection of decorations, letterboxing, stale paint, lost focus and an
ambiguous second window. The virtual monitor stays at compositor scale 1;
these runs establish Qt fractional content scaling, not a fractional GNOME
monitor configuration or input on the physical desktop.

This channel currently provides observations only. The core requests one after
ordinary absolute EIS motion, but does not treat the request as a motion fence
or as evidence of which physical device caused the current pointer position.
Original-input delivery still requires those bindings and the production
handoff coordinator. This provider-owned viewer exits on connection loss;
restarting requires a fresh core/provider session.

## Current behavior

The Mac host lists eligible application windows. Either paired computer can
request a selected window. ScreenCaptureKit captures that window and VideoToolbox
encodes H.264. The viewer decodes on a worker thread and displays each stream in
an ordinary top-level window that can overlap local applications.

A source stays visible until the viewer has decoded and painted its first frame.
With `--holding-display`, the host can then move it to an owned virtual display.
If that display cannot be created, the source remains visible. This adapter uses
private macOS APIs and is isolated from the capture and protocol code.

Input requires a committed session, a current geometry revision, and a focus
acknowledgement. A focus change releases held input; stale focus replies and stale
video do not authorize new input. Native events target the source process and
carry a loop-suppression marker. The opt-in Deskflow Mac backend recognizes this
marker and excludes the holding display from its cursor boundaries. The marker
is bookkeeping, never peer authentication.

Targeted mouse and scroll construction additionally requires the private
CoreGraphics window-location getter/setter, resolved from the system framework
at runtime. Setting the public global position clears this separate annotation;
the host restores window-local coordinates afterward and checks both coordinate
domains and the destination window before posting. `--probe` reports
`targetedPointer` and the private API dependency. Missing symbols disable export;
there is no global posting or cursor-warp fallback. Current-machine native
receiver tests verify this path, but do not establish compatibility across
other macOS versions. The declarations also appear in
[Apple's WebKit test SPI](https://github.com/WebKit/WebKit/blob/main/Tools/TestRunnerShared/spi/CoreGraphicsTestSPI.h).

Closing a viewer returns its source window; it does not close the Mac application.
The host also restores on disconnect, sleep, session deactivation and normal exit.
The host listens for Control–Option–Shift–Escape to restore all exported windows.
When launched by the core with `--windowshare-host-config`, the core observes that
chord before ordinary Deskflow translation, including while controlling another
computer. It closes the private companion connection, causing the host to stop
accepting work, release input, attempt restore-all and exit. Ordinary Deskflow
continues running. Starting sharing again requires a new core session. The
observer does not consume the chord: its keys still follow ordinary Deskflow
routing. An owner-only restore journal is written before relocation. `--recover`
retries restoration after a crash or a failed restore, preserving unresolved or
malformed records rather than discarding them.

After a transient network failure, the viewer retries the same address and
approved fingerprint after 1, 2, 4, 8, 16 and then 30 seconds, continuing at the
30-second interval until canceled. Authentication and protocol failures stop
automatic retries. The panel provides Retry and Cancel; closing the panel exits
the application. Closing the last viewer does not stop a headless connection.
Each lost connection releases input and returns exported sources before a fresh
connection can open windows. Reconnection requests a new catalog and does not
replay sessions or input. The backoff resets after 30 seconds with an established
connection and received catalog, or when Retry is selected.

## Build

Use the repository's C++20 baseline, Qt 6.4+ Core/Network/Widgets, FFmpeg development
libraries (`libavcodec`, `libavutil`, `libswscale`), pkg-config, and OpenSSL. The full
Deskflow build can require a newer Qt independently. Hosting requires macOS 13+;
Linux builds provide the viewer.

Native GNOME viewport mapping additionally needs matching Qt Wayland and Qt Gui
private development headers plus `wayland-client`. The adapter supports Qt 6.8
and 6.10 and requires an exact build/runtime Qt version match. Unsupported or
missing private APIs leave that observation unavailable; they do not supply an
estimated transform.

```sh
cmake -S src/apps/windowshare -B build/windowshare-app \
  -DWINDOWSHARE_APP=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/windowshare-app --parallel
ctest --test-dir build/windowshare-app --output-on-failure
```

For the full Deskflow application and its opt-in platform hooks, configure the
repository root with `-DBUILD_WINDOWSHARE=ON`. Leave it off for an ordinary build.
Run its tests from `build/src/unittests` (substitute the chosen build directory).

Qt 6.8.3 on this Intel Mac references AGL, which is absent from the macOS 26 SDK.
The local build uses `-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk`.
Use an installed compatible SDK; do not copy that path blindly to another machine.

Core tests need no Qt:

```sh
cmake -S src/unittests/windowshare -B build/windowshare-core \
  -DWINDOWSHARE_SANITIZERS=ON
cmake --build build/windowshare-core --parallel
ctest --test-dir build/windowshare-core --output-on-failure
```

To package a Mac build with its Qt/FFmpeg libraries and owned helper:

```sh
MACDEPLOYQT=/path/to/this/Qt/bin/macdeployqt \
  deploy/windowshare/package-macos.sh build/windowshare-app build/package-new \
  "YOUR_SIGNING_IDENTITY"
```

The output directory must be new. The script creates and verifies a separately
named `DeskflowWindowShare-Experimental.app` and ZIP; omitting the signing identity
uses ad-hoc signing for local development. It does not replace the installed
Deskflow or notarize a release. Each installed app still needs its own normal
macOS permission grants.

Packaging verifies every non-system Mach-O dependency and sets the app's minimum
macOS version to match its bundled libraries. A local Homebrew build can therefore
require a newer macOS than the capture API itself. Recheck an existing package
without modifying it using:

```sh
python3 deploy/windowshare/macos-bundle.py verify /path/to/DeskflowWindowShare-Experimental.app
```

For a package that restricts loaded libraries to Apple or the app's signing Team,
put `--restricted-libraries` before the build directory. This requires an
Apple-issued signing identity; ad-hoc signatures are rejected:

```sh
MACDEPLOYQT=/path/to/this/Qt/bin/macdeployqt \
  deploy/windowshare/package-macos.sh --restricted-libraries \
  build/windowshare-app build/restricted-package-new "YOUR_SIGNING_IDENTITY"
python3 deploy/windowshare/macos-bundle.py verify \
  build/restricted-package-new/DeskflowWindowShare-Experimental.app --restricted-libraries
```

This mode signs all bundled dependencies with the same Team, enables hardened
runtime and explicit library validation on the fixed executables, and audits
every architecture's Apple-issued signature, Team and empty/absent entitlements.
The ordinary packaging mode remains available for ad-hoc CI and development.
Neither mode authorizes original input. Apple-or-same-Team library admission does
not identify an exact set of permitted library hashes. See Apple's
[library validation policy](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.disable-library-validation)
and [library constraints](https://developer.apple.com/documentation/security/defining-launch-environment-and-library-constraints).

To test the OS policy without starting Deskflow or any native input/display code:

```sh
python3 deploy/windowshare/macos-library-admission-tests.py \
  --signing-identity "YOUR_SIGNING_IDENTITY"
```

That test compiles disposable executable/dylib fixtures. It checks startup and
later loading of same-Team, unsigned and ad-hoc libraries, including constructor
suppression and an ordinary-loader control. It needs a real local signing identity
and does not claim coverage for a different Apple Team or exact-image constraints.

For a sealed set of permitted non-Apple library identities, use
`--exact-libraries` instead. It implies restricted admission and requires macOS
14 or newer. The packaging script signs all libraries first, records every
architecture and supported SHA-256 CodeDirectory variant in
`Contents/Resources/deskflow-exact-libraries.json`, then signs the fixed
executables with the same Team-and-CDHash constraint:

```sh
MACDEPLOYQT=/path/to/this/Qt/bin/macdeployqt \
  deploy/windowshare/package-macos.sh --exact-libraries \
  build/windowshare-app build/exact-package-new "YOUR_SIGNING_IDENTITY"
python3 deploy/windowshare/macos-bundle.py verify \
  build/exact-package-new/DeskflowWindowShare-Experimental.app --exact-libraries
python3 deploy/windowshare/macos-library-constraint-tests.py \
  --signing-identity "YOUR_SIGNING_IDENTITY"
```

The audit independently parses the embedded constraint's narrow supported DER
schema, verifies its CodeDirectory slot 11 seal, checks the root resource seal
and compares the manifest with the complete bundled library inventory. It rejects
unknown encodings, missing or duplicate records and unsupported digest variants.
The disposable OS suite checks native Intel x86_64 startup and later loading:
listed hashes load; excluded same-Team/same-identifier code and omitted transitive
dependencies fail before constructors; otherwise identical unconstrained controls
load them. Full-list transitive controls and an explicitly unlisted system library
check the policy's boundary. The suite does not establish arm64 or Rosetta behavior.

Apple/system libraries remain exempt. An allowed hash does not restrict a
library's path, enumerate currently loaded images or attest uncompromised process
memory. Exact packaging does not enable original-input delivery or preflight.

## Packaged core companion

The Mac opt-in core can launch the window host through a private inherited
socketpair. Package the integrated core as the bundle's main executable, with
the window host sealed as its fixed sibling:

```sh
MACDEPLOYQT=/path/to/this/Qt/bin/macdeployqt \
  deploy/windowshare/package-macos.sh build/windowshare-app build/core-package-new \
  "YOUR_SIGNING_IDENTITY" build/integrated/bin/deskflow-core
python3 deploy/windowshare/launch-smoke.py \
  build/core-package-new/DeskflowSeamless-Core-Experimental.app
```

Build the integrated core with `BUILD_WINDOWSHARE=ON` and `BUILD_OSX_BUNDLE=OFF`.
The new `DeskflowSeamless-Core-Experimental.app` is separate from both the ordinary
Deskflow installation and the standalone WindowShare bundle. Its `deskflow-core
--windowshare-ipc-probe` option must appear alone. That probe verifies the actual
packaged child, completes its private handshake, and checks its shutdown before
returning. It starts no input hooks, capture host, network listener or ordinary
Deskflow shared state, and can run with `QT_QPA_PLATFORM=offscreen`.

Companion startup and the private unauthenticated handshake each have an absolute
15-second default budget, separate from the authenticated channel's unchanged
2.5-second heartbeat timeout. Incoming handshake traffic never extends its
deadline. Successful authentication starts fresh heartbeat timing before
callbacks run. Signed-code validation remains synchronous and cannot be
preempted by a Qt timer; admission checks the deadline again after validation.
Timeout diagnostics identify the startup stage and elapsed time.

For a real host launch, supply `server --windowshare-host-config /absolute/host.json`
to that packaged core. The file must be a regular JSON file of at most 16 KiB:

```json
{
  "version": 1,
  "listen": "192.0.2.10",
  "port": 24810,
  "identity": "/absolute/path/to/existing/identity",
  "peerPin": "REPLACE_WITH_64_HEXADECIMAL_DIGITS",
  "journal": "/absolute/path/to/restore.json",
  "holdingDisplay": false,
  "headless": false
}
```

`holdingDisplay`, `headless`, `inputPeerPairs` and `shareClipboardText` are optional.
Text clipboard sharing defaults off and requires both peers to opt in; see the
[focused clipboard policy](../deploy/windowshare/focused-clipboard.md).
Use the Mac's actual interface
address, a previously created identity, and the approved viewer's fingerprint.
Unknown/duplicate fields, wrong types, relative identity/journal paths and arbitrary
child arguments are rejected. This option starts the real native host and needs
an open, unlocked Mac with its normal permission grants. A failed initial private
handshake prevents ordinary core startup; later host loss is reported while the
ordinary core continues. No automatic companion restart occurs.
Normal shutdown allows five seconds for EOF-driven restoration, then sends
SIGTERM and allows five more seconds before killing an unresponsive owned host.
The recovery journal remains the fallback for interrupted restoration. The
handshake-only probe uses a shorter shutdown interval.

For original-input admission, `inputPeerPairs` explicitly pairs the
approved Deskflow TLS certificate with the approved WindowShare viewer TLS
certificate. The two fingerprints identify separate connections and may differ:

```json
"inputPeerPairs": [
  {
    "deskflowPeerPin": "REPLACE_WITH_DESKFLOW_PEER_64_HEXADECIMAL_DIGITS",
    "windowSharePeerPin": "REPLACE_WITH_WINDOWSHARE_PEER_64_HEXADECIMAL_DIGITS"
  }
]
```

Insert that optional field in the host configuration object. Missing or empty
`inputPeerPairs` denies every original-input pair while preserving the existing
host configuration behavior. At most eight pairs are accepted. Each fingerprint
must contain exactly 64 ASCII hexadecimal digits; either letter case is accepted
and normalized to lowercase. All-zero values, duplicate pairs, either fingerprint
mapped to multiple partners, unknown row fields and duplicate JSON keys are
rejected. The host's existing `peerPin` still controls its separate WindowShare
TLS connection; it never creates a Deskflow pairing automatically. Only an exact
configured pair can match the independently authenticated live connections.

The core retains one immutable configuration snapshot and forwards its canonical
public pin map through a bounded internal option to its inherited companion.
The host retains that same map; neither process rereads or learns policy from
network messages, computer names or discovery. Replacing the file requires a new
launch to take effect. The production controller uses this configured route
only when exact module admission, source acquisition, native queue ordering,
mapped geometry, focus and complete forwarding ownership are available.
The configuration alone cannot satisfy those checks.

Both processes verify fixed role paths, PID birth identities and exact signed
code against the running bundle main's resource seal. A same-user process, an
unrelated ad-hoc signature, or an inherited FD alone cannot satisfy that check.
Directly launching the nested host cannot establish this private route. These are
packaged-code integrity checks, not publisher approval or complete loaded-module
integrity. The child drops ambient DYLD and Qt/QML plugin search overrides. Input
authorization remains unset. The companion-enabled Mac core now observes mixed
input in its existing HID tap, before Deskflow translation. It allocates a bounded
queue before starting the tap and drains it on the Qt thread. This observer does
not suppress events, forward them to the companion or post input. An ordinary
core launch without the companion does not attach this observer.

## Pair and run

The examples use `deskflow-windowshare` as the executable. On a Mac build it is
`build/windowshare-app/deskflow-windowshare.app/Contents/MacOS/deskflow-windowshare`;
on Linux it is `build/windowshare-app/deskflow-windowshare`.

Create a separate private identity on each computer:

```sh
deskflow-windowshare --identity "$HOME/.local/share/deskflow-windowshare/identity" --init-identity
```

This prints the public SHA-256 certificate fingerprint. Exchange and approve
those fingerprints through a trusted channel. Keep each private key on its own
computer. Existing identities are never overwritten. `--fingerprint` prints an
existing identity's fingerprint.

New identities include explicit TLS server and client key usages for both
OpenSSL and Apple's native TLS backend. If an older experimental identity is
rejected for its certificate purpose, create a replacement in a new identity
directory and explicitly approve its new fingerprint on the other computer.
The application never rotates an identity or changes an approved pin on its own.

On the Mac, check permissions and use the normal OS prompt if needed:

```sh
deskflow-windowshare --probe
deskflow-windowshare --request-permissions
```

Then start the Mac host and Ubuntu viewer, substituting actual addresses and the
other computer's fingerprint:

```sh
# Mac
 deskflow-windowshare --enable-seamless --listen MAC_INTERFACE_IP \
  --identity "$HOME/.local/share/deskflow-windowshare/identity" \
  --peer-pin UBUNTU_FINGERPRINT

# Ubuntu; screen 0 and screen 1 select different monitor destinations
 deskflow-windowshare --enable-seamless --connect MAC_INTERFACE_IP \
  --identity "$HOME/.local/share/deskflow-windowshare/identity" \
  --peer-pin MAC_FINGERPRINT --screen 0
```

The separate TLS port defaults to 24810. Both sides pin the exact peer leaf
certificate. There is no plaintext fallback or automatic trust on first use.
The existing Deskflow connection carries the physical Mac keyboard and pointer to
Ubuntu; use the opt-in core build for its injected-event and display exclusions.
Select a window in the connection panel to share it. `--holding-display` is an
additional host opt-in, separate from enabling the application.

After an interrupted host, with that host stopped:

```sh
deskflow-windowshare --recover
```

Use the same `--journal PATH` if the original host used a custom journal. A lock
prevents two hosts from owning the same recovery journal. A nonzero recovery exit
status means some records still need attention.

## Verification scope

Automated tests cover protocol bounds and replay rejection, session and geometry
fences, input ownership, exact display/event markers, mutually pinned TLS sockets,
video framing/backpressure, viewer focus/resize/return behavior, decoder operation,
private display API availability, recovery-journal handling, and reconnect cleanup.

The original Mac keyboard route is still under development. Its isolated helpers
track left/right modifiers, press ownership, revocation, and the emergency chord;
the Mac adapter only constructs events in memory. A private inherited socketpair
provides bounded, versioned core/host messages and revocable input leases. These
helpers now support the explicit packaged companion launch above; they do not
register an original-input tap or enable original OS input.
The eventual route must bind the current Deskflow client and window-share client
to an explicit pair of approved fingerprints and connection incarnations. The
core supplies a revocable ticket only after its complete trusted-peer handshake;
ordinary encryption-only clients provide no ticket for this route. Tickets bind
the leaf fingerprint to a fresh connection incarnation, so a same-pin reconnect
cannot revive a retained identity.

Selected-client snapshots also bind a server incarnation, recipient token and
selection generation. They are revoked before destination, configuration,
broadcast, screensaver and shutdown transitions. The core's event thread
publishes only immutable values to the Qt-owned Session's one-value mailbox;
no proxy or stream pointer crosses threads. Publication starts with the first
20 ms core timer dispatch. Observations expire after 250 ms independently of
timer delivery and remain invalid after mailbox replacement, closure or either
underlying revocation. This is identity infrastructure; it does not by itself
authorize original input.

WindowShare now exposes equivalent revocable TLS tickets with immutable six-second
read observations. A retained observation expires independently of later reads.
Queued traffic checks the old deadline before refreshing it, so an overdue timer
cannot revive a connection. Closing, socket failure and destruction revoke its
ticket before notifying the owner; a reconnect requires a new Channel instance.

Private protocol version 4 carries keys, motion, buttons, scroll and checkpoints
in a single ordered stream. Its 128-byte offer binds a private route generation,
core server/recipient/selection tokens, the capture epoch, the Deskflow connection
incarnation, both approved pins, and the explicit keyboard/all-input scope. A 192-byte
lease also binds the WindowShare incarnation, session, epoch, focus request,
focus revision, geometry revision and independent keyboard-fence generation.
Original event payloads use explicit tags and signed 32-bit motion/scroll values;
the largest is 244 bytes, within the unchanged 256-byte private limit. Capture
sequences may skip events outside a route but must increase within their epoch.
Replay marks survive same-epoch lease replacement; a greater capture epoch needs
a fresh Prepare, and an older epoch cannot return. Emergency/discontinuity
boundaries revoke rather than deliver native input. Versions 2 and 3 are rejected; none
of these original records is carried over network JSON.

Bootstrap independently advertises keyboard/full-input code capabilities and
confirms their intersection before authentication completes. A keyboard-only
endpoint cannot negotiate full scope. Code support is not native authority.
The lease retains scope through Candidate, Quiesced, Prepare, Commit, Original
and release, and a keyboard lease cannot carry pointer records. Host reservation
must match the requested scope exactly, including through reentrant cancellation.

`LocalInputPreflight` implements the exchange as an isolated, default-deny
coordinator. Each endpoint checks its own live connection and an explicit immutable
map of at most eight approved fingerprint pairs. An attempt has one absolute
two-second deadline. Refresh is allowed only while the retained observation is
still live and the exact selection/connection is unchanged, including after a
blocking read. Cancellation takes effect before a following event can authorize;
replacement waits for synchronous owner release. Reentrant controls use a bounded
eight-value queue with one cancellation slot. Missing neutral-boundary, module
integrity or ordered-input checks never authorize Prepare.
Core begin now requires a retained live capture token and rechecks it through
every callback, control and timer dispatch. The host binds the offered numeric
epoch; that number alone is not physical-state authority.

The bounded
`OriginalInputStream` carries keys, motion, buttons, scroll phases and checkpoints
in one capture epoch and sequence, preserving each record's original flags. Its
retained epoch evidence is revoked on overflow, discontinuity, emergency or stop.
Neutral cutover requires an explicit trusted physical observation covering keys,
buttons and scroll state, followed by an unchanged producer checkpoint. No OS
snapshot/tap fence or native physical-state provider is implemented. Unknown
startup state, held nonmodifier keys, pending momentum and missing coverage cannot
be replaced with quiet time or empty flags. The press ledger retains original
key/button owners and release tombstones; it does not authorize motion or scroll.
Consumers must feed every mixed record through both the barrier and press ledger,
apply the returned releases with their release-time flags before resuming a route,
and require an empty ledger in addition to a committed physical neutral boundary.
Scroll ownership must survive gesture end and any following momentum; silence or
a checkpoint cannot redirect that momentum to a new viewport.

The Mac capture adapter preserves supplied CGEvent fields and constructs private,
loop-tagged events without posting them. It rejects mismatched callback/event
types, tablet subtypes, unsupported third scroll axes and malformed fields. It
uses integer accessors for signed 16.16 scroll values. Every constructed field is
read back: if Quartz narrows a representable wire value, construction fails
instead of returning altered input. Constructed-event tests on the local Intel
Mac found this for large mouse deltas and line-scroll values; point and raw fixed
scroll fields retained signed 32-bit extremes.

`OriginalInputCaptureSource` attaches once for the companion session. Its retained
epoch is unavailable before tap startup and revoked on overflow, malformed input,
tap disable, companion loss, server stop or source destruction. Restarting or
resuming the ordinary core cannot revive it; a new core session is needed until
a validated rearm provider exists. Qt drains every record into the neutral
barrier, which has no trusted physical observation and cannot authorize input.
The emergency chord remains observable after queue overflow or barrier failure.
The Qt consumer checks its sticky flag before continuity evidence and starts
nonblocking companion shutdown. Closing the authenticated private connection
requests restoration even when no input lease exists. The host latches that
closure across startup and blocks new exports and reconnects before releasing
input and attempting restore-all. This is a terminal sharing stop, not a success
acknowledgement for every window restoration; failed restores remain journaled.
Key events marked as repeats, releases, incomplete chords and our tagged synthetic
events do not trigger it. Accessibility checks and tap re-enabling run on the
core owner thread, avoiding a tap-thread self-join. A queued run-loop stop also
survives shutdown before the first run begins.

`ViewportLease` validates one-output, axis-aligned X11 coordinate mappings and
host-request-based freshness. The Deskflow injection space and viewer space must
have the same explicitly bound identity and layout; matching desktop dimensions
do not suffice. Geometry calculation is separate from delivery permission.
Delivery additionally needs exact-sample validation and already-owned press
evidence for drag/release clamping. Only synthetic test fixtures can create that
validation today. The opt-in X11 observer now reads each owner's actual native
connection, root, client pointer, RandR topology and optional window geometry.
It does not yet provide an authenticated shared coordinate-space binding, a
pointer fence, or a Wayland coordinate provider; cached rectangles never grant
clicks.

On Linux, build with `BUILD_WINDOWSHARE=ON` and run the experimental client with
`--windowshare-observe-x11` alongside its existing client settings to inspect the
actual XWindowsComputer injection connection. This diagnostic is off by default
and reports availability in the core log. It neither enables original input nor
changes the ordinary client route. The viewer independently observes its own Qt
xcb connection and native window. No new Deskflow wire message publishes or
matches these observations yet.

The observer borrows the owner's connection without dequeuing its events, polls
bounded asynchronous replies, and compares two complete topology reads. Copies
expire after 250 ms and are revoked on observed changes, invalidation or owner
shutdown. Native root/output geometry, transforms and panning matter; matching
`DISPLAY` strings or screen dimensions is insufficient. Linux Unix-socket peer
credentials plus process birth/boot information record socket provenance, not a
proved X-server incarnation: an inherited listener can identify its creator.
The observer rejects TCP sockets and an advertised Xwayland extension, but an
absent extension does not prove native Xorg. None of these observations is an
atomic fence against other clients or later X-server changes.

`LocalInputPeerTrust::inspectRestrictedLibraryAdmission` optionally inspects the
live anchor, core and host against their existing exact process/code pins. It
requires Apple-issued matching Team signatures, hardened runtime, explicit
library validation, no entitlements and valid/non-debugged dynamic status. Its
result is a historical inspection snapshot, with start/end times, rather than
retained authority. This synchronous Security-framework check must not run in an
input tap. It establishes the limited Apple-or-same-Team admission policy, not
exact loaded-image identity, and cannot satisfy the preflight code-admission
provider by itself.

`inspectExactPermittedLibraries` adds a historical snapshot of the stronger
permitted-library policy. It verifies the canonical root-sealed manifest, all
declared library slices, and matching Team/CDHash constraints on the pinned live
root, self and peer. It authenticates slot 11 through each pinned CodeDirectory,
and rechecks process identities and restricted dynamic policy around the read.
Absent, stale, malformed, mismatched or restricted-only evidence denies this
inspection. The snapshot records observation times, manifest SHA-256, permitted
hashes, record count and distinct slice count; it supplies no retained capability
or production preflight provider. It must never run in an event tap.

`LocalInputCodeAdmission` binds that stronger inspection to the exact authenticated
private channel and live process/anchor identities. It checks the kernel-enforced
non-Apple library admission policy for those executable lifetimes. Apple/system
code remains an OS exception. This does not inventory loaded images, attest
process memory, detect changes among permitted libraries, or defend against
privileged compromise or defects in admitted code.

The live bundle-main anchor must be the authenticated Core endpoint. A separate
ancestor can be inspected historically but cannot supply this retained proof:
the core/host channel cannot observe that ancestor replacing its executable.
The adopted descriptors are close-on-exec. Trusted code deliberately copying or
delegating that private descriptor is outside this admission boundary.

Core and host request this optional inspection after authentication only when
`inputPeerPairs` is nonempty. One worker per process performs the synchronous
Security/filesystem work; no unbounded work queue is created. The Qt owner polls
without waiting for that worker and admits the result within an absolute
15-second budget. Timeout or destruction discards the result without waiting for
Security calls to return; a stalled worker keeps the single slot occupied. The
ordinary private handshake and its heartbeat budgets are unchanged.

Copies of the resulting proof require the original Qt thread and recheck channel
freshness, connection identity and process births at use. Channel closure/expiry,
anchor loss or guard revocation/destruction invalidates them. These checks read
no signature files in the input path. A failed check leaves original input
unavailable while ordinary Deskflow continues. Configuring pairs or obtaining a
proof still does not install the input coordinator or authorize delivery.

The signed peer checks are reproducible without a native host: build
`windowshare-peer-trust-tests`, use its `--make-fixture-bundle` option with a new
`deskflow-windowshare.app` path inside a private directory, and package those
fixture executables with `--exact-libraries`. Run
`deploy/windowshare/macos-peer-constraint-tests.py FIXTURE_BUNDLE NEW_OUTPUT
--signing-identity YOUR_SIGNING_IDENTITY` against that disposable package. It
checks both roles, altered policy, absent/noncanonical manifests, wrong slice
records, resource tampering, stale identities and a replaced live anchor. The
driver requires the fixture's proof resource and never runs a display helper;
do not substitute a production app for the fixture.

The same exact-signed disposable fixture's main executable supports
`--admission-core allow`. It authenticates both fixed endpoints, runs their real
asynchronous inspections and checks that closing the private connection revokes
both retained proofs. The ordinary test suite exercises the corresponding
ad-hoc denial case. These headless checks never start a native window host.

An optional `keyboard-route-v1` capability coordinates the viewer's keyboard
forwarding with an independent native admission fence. The viewer releases its
held Qt keys before acknowledging the exact session, focus and geometry binding;
pointer input continues. Invalidated fences remain closed until local original
authority has been revoked and the host requests an exact viewer resume. Old
held keys stay suppressed through their releases. Legacy peers keep the ordinary
viewer path. The executable does not yet install the local controller required
to initiate this handoff, and the network protocol carries no original Mac keys.

The additional `input-route-v1` capability uses a separate full-input control
message. It fences keys, buttons, movement and both scroll axes in Viewer and
NativeHost together. Scope is part of the exact binding, and older peers retain
keyboard-only behavior. Pending input stays discarded even if pause and resume
complete inside one callback; fenced button presses retain suppression through
their physical releases. See
[`forwarding-fence.md`](../deploy/windowshare/forwarding-fence.md) for its ordering
and compatibility contract. This is a forwarding gate, not a native queue drain.

Bridge reservation now separates selecting an exact binding from requesting the
viewer fence. It sends the request only after the private candidate can be sent,
then independently rechecks quiescence and the same fixed reservation deadline.
The local preflight must now explicitly activate that exact Bridge binding
before the private Commit acknowledgment. Successful activation retires the
setup deadline; abandoned reservations still expire. Active checks retain the
native focus/geometry and legacy-input fence while permitting owned original
presses to remain held. The channel's existing heartbeat rechecks active admission
and delivery providers and releases the owner on failure even without new input;
it does not repeat the one-time physical-neutrality check.

Activation still requires both live authenticated network connections, an
explicit approved dual-pin mapping, a verified capture boundary, retained code
admission, and causal ordering of original keys
with pointer events. A cache of the latest modifier flags is insufficient: a
delayed click must not be delivered after a later modifier release. These remain
integration requirements, not behavior claimed by the current automated tests.
The tested preflight coordinator is not yet constructed by either production
endpoint. Explicit policy and optional code-admission checks are wired into the
launch path; ordered ownership, pointer fencing and coordinate validation still
need integration. The existing global-neutrality interface has no native
provider; quiet time or a current HID-state query is not a substitute. The next
route must preserve each press and scroll gesture's owner through an ordered
cutover. This milestone does not suspend a real viewer or enable original input.

The manual `windowshare-native-smoke` executable opens its own disposable test
window and verifies real capture, decoding, targeted input and restoration. It
requires an unlocked Mac desktop with normal Screen Recording and Accessibility
permissions. It is deliberately not a permission-dependent CI test. The manual
holding-display lifecycle check is `windowshare-holding-tests --create-and-destroy`.
Do not confuse a read-only API probe with a successful display lifecycle test.
`windowshare-holding-tests --crash-owner-cleanup` additionally kills only its own
test owner and checks the helper/display cleanup against the original physical
display inventory. Run either live display test with an open, unlocked desktop.

Consult `WORK_STATE.md` for the actual results recorded for this milestone and
GitHub Actions for a commit's CI results. A workflow file alone is not evidence
that a job passed.

## Remaining work

The [coordinate integration plan](seamless-windows-coordinate-plan.md) retains
the original design. Its X11 namespace witness and native hit checks are now
implemented; X11 still lacks the native drain required for original input.

Automatic edge-drag export/return and transactional drag ownership are unfinished.
Precise Wayland placement may need compositor cooperation; Qt screen selection
does not guarantee coordinates under every compositor. Ubuntu builds, direct-host
foundation/application checks and private-Xvfb viewport/core tests now pass. The
actual GNOME Wayland desktop currently reports one active monitor; live window
input and a two-monitor arrangement still require direct validation.

Remote dialog/popup rendering and IME composition remain unavailable. Exact
attached sheets/drawers trigger local parent return. Focused text clipboard,
original shortcut/repeat delivery, stream scheduling and watchdog recovery have
code and automated coverage; real application and packaged cross-machine
acceptance remains separate. The experimental application does not replace the
ordinary Deskflow installation.
