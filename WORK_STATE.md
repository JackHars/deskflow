# Seamless Windows work state

Branch: `feature/seamless-windows` in `JackHars/deskflow`.
Starting milestone: `4070e8a57885dfac5739f88456c63efbc4521cbf`.

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

## Recorded validation

Local environment: Intel Mac, macOS 26.1, AppleClang 17, Qt 6.8.3, FFmpeg 8.0.1.
The widget/application build uses the installed macOS 15.5 SDK for Qt's AGL link.

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
- All five GitHub Actions jobs passed for `6d9da2014794832e64f10df9e9b74031f7fc0b81`
  ([run 38237378832](https://github.com/JackHars/deskflow/actions/runs/38237378832)).
  That includes selected-peer snapshots and the packaged trusted core/host
  handshake. The subsequent preflight work is recorded separately below.

## Local package

The new signed core-main bundle is
`build/packages/preflight-20261012/DeskflowSeamless-Core-Experimental.app`,
with its ZIP beside it. All 63 Mach-O files pass dependency/signature verification.
It is x86_64, requires macOS 26.0 due to the bundled dependencies, and is not
notarized. ZIP SHA-256:
`b2ae647726574948eece1c67ac54a58af222fcfa3ac642889cdbfef8c1b84b56`.
The separate installation is `~/Applications/DeskflowSeamless-Core-Experimental.app`.
The package and installed copy passed recursive dependency/signature verification,
the private handshake/bootstrap-rejection smoke, and the viewer-only TLS/reconnect
smoke, with original input disabled. The preceding selected-peer installation is
preserved in `build/packages/preflight-installed-backup-20261012/`.
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

The Ubuntu host was unreachable over SSH: the WiFiman Teleport connection is down.
No Linux desktop session, GPU, monitor arrangement, installation, or real
cross-machine input behavior has been verified in this environment.

## Remaining milestones

1. Prove the single-window native capture/input/return path and owned holding
   display lifecycle on the Intel Mac.
2. Build and deploy the viewer to Ubuntu; test X11/Wayland, both monitors,
   focus changes, shortcuts and loop-free input using the Mac keyboard/trackpad.
3. Integrate the private original-keyboard route, dual-connection authentication,
   viewer quiescence, authoritative pointer modifiers and core emergency chord.
4. Implement transactional edge-drag export and drag-back using Deskflow topology.
5. Handle associated dialogs, menus, clipboard and browser/Electron behavior.
6. Complete automatic crash recovery, scheduling and performance work.
7. Package both machines and complete destructive-failure and ordinary-use tests.

The installed `/Applications/Deskflow.app` has not been replaced. Repository and
standalone tests are not a claim that the full seamless-window goal is complete.
