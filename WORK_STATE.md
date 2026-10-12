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

The core exposes the authenticated leaf fingerprint only after its existing
trusted-peer check succeeds, and revokes that proof before connection-loss
notifications. Encryption-only clients provide no such proof. A TLS close
deadlock found during this work is fixed by quiescing multiplexer callbacks
before locking and destroying TLS state.

Original-keyboard tracking, press ownership and Mac event construction are
implemented as isolated helpers. A private inherited core/host socketpair adds
bounded messages, process-birth checks, expiring leases and exact-owner
revocation. These helpers are not yet wired into a running input route: they
neither launch a companion nor register a tap or post OS input. Integration must
bind both authenticated connections to an explicit approved fingerprint pair
and coordinate a neutral keyboard boundary with viewer quiescence.

## Recorded validation

Local environment: Intel Mac, macOS 26.1, AppleClang 17, Qt 6.8.3, FFmpeg 8.0.1.
The widget/application build uses the installed macOS 15.5 SDK for Qt's AGL link.

- All five isolated core suites passed. Protocol, input-routing and original
  keyboard suites use ASan/UBSan and strict compiler warnings; platform-marker
  tests also pass. The keyboard suite checks 12,000 deterministic interleavings
  against an independent recipient model, and Mac tests construct private
  CGEvents in memory. Neither is hardware/layout/IME proof.
- TLS tests pass with both OpenSSL and SecureTransport, including wrong pins,
  invalid dates, wrong certificate purpose in both peer roles, and explicit
  certificate errors. Identity generation also passed with Apple's LibreSSL.
  The native backend evaluates only the approved leaf as a private trust anchor;
  it does not change keychains or waive certificate policy failures.
- All eight standalone application CTest suites passed after the final fixes:
  private local input, both TLS backends, viewer, both connection-controller
  backend runs, read-only holding-display probe and native recovery journal.
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
- All five GitHub Actions jobs passed for `fee69b2f5df63337ec6feabecc43b1ddd25f763c`
  ([run 38217599883](https://github.com/JackHars/deskflow/actions/runs/38217599883)).
  That result predates the reconnect and original-input foundation changes.

## Local package

The signed Intel build is in
`build/packages/reconnect-input-20261012/DeskflowWindowShare-Experimental.app`
with a ZIP beside it. This particular package requires macOS 26.0 because of its
Homebrew dependencies; it is not notarized. ZIP SHA-256:
`6e9bb9a309b99baf96eed1f3b4259bccc9fb0c115d3894f06704d156b877be2d`.

The separate installation path is `~/Applications/DeskflowWindowShare-Experimental.app`.
The installed copy also passed dependency/signature verification and the
viewer-only TLS/reconnect smoke test without external library environment paths.
The immediately preceding experimental installation is preserved in
`build/packages/tls-installed-backup-20261012/DeskflowWindowShare-Experimental.app`;
the earlier native milestone is preserved in
`build/packages/native-installed-backup-20261012/DeskflowWindowShare-Experimental.app`.
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
