# Seamless Windows work state

Branch: `feature/seamless-windows` in `JackHars/deskflow`.
Starting milestone: `4070e8a57885dfac5739f88456c63efbc4521cbf`.

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
