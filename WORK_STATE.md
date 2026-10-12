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

## Recorded validation

Local environment: Intel Mac, macOS 26.1, AppleClang 17, Qt 6.8.3, FFmpeg 8.0.1.
The widget/application build uses the installed macOS 15.5 SDK for Qt's AGL link.

- Core and input-routing tests passed with ASan/UBSan; platform-marker tests passed.
- All seven standalone application CTest suites passed: TLS transport, viewer,
  connection controller, read-only holding-display probe, native recovery journal,
  and explicit Apple SecureTransport runs of the TLS and connection-controller suites.
- TLS tests pass with both OpenSSL and SecureTransport, including wrong pins,
  invalid dates, wrong certificate purpose in both peer roles, and explicit
  certificate errors. Identity generation also passed with Apple's LibreSSL.
  The native backend evaluates only the approved leaf as a private trust anchor;
  it does not change keychains or waive certificate policy failures.
- Viewer tests independently passed with ASan/UBSan and strict compiler warnings,
  including focus revocation, deliberate reacquisition and stale-reply rejection.
- Connection-controller tests passed normally and with ASan/UBSan. Real TLS
  fixtures verify delayed catalog enumeration, bounded encrypted output, deferred
  keyframe recovery without repeated encoding while blocked, and cleanup of a
  replaced viewer incarnation.
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
- The full opt-in Deskflow build passed at the initial native milestone, followed
  by all 38 then-existing CTest suites. Four
  Server regressions exercise real primary/client routing and orderly disconnect
  through the existing protocol implementations.
- The final separate experimental app passed nested signing and dependency
  verification for all 62 Mach-O binaries. Packaged CLI startup, mutual TLS and
  clean exit passed with external Qt/library environment paths removed. Its
  five packaging fixture tests also passed.

## Local package

The signed Intel build is in
`build/packages/tls-portability-20261012/DeskflowWindowShare-Experimental.app`
with a ZIP beside it. This particular package requires macOS 26.0 because of its
Homebrew dependencies; it is not notarized. ZIP SHA-256:
`e35a2e7696591c2ab6d4dcb519ee51f145852afa43ace2a655abeed840ce8af6`.

The separate installation path is `~/Applications/DeskflowWindowShare-Experimental.app`.
The installed copy also passed dependency/signature verification and the packaged
CLI/mutual-TLS smoke test without external library environment paths.
The previous experimental installation is preserved in
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
3. Implement transactional edge-drag export and drag-back using Deskflow topology.
4. Handle associated dialogs, menus, clipboard and browser/Electron behavior.
5. Complete automatic crash recovery, reconnect, scheduling and performance work.
6. Package both machines and complete destructive-failure and ordinary-use tests.

The installed `/Applications/Deskflow.app` has not been replaced. Repository and
standalone tests are not a claim that the full seamless-window goal is complete.
