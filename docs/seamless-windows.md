# Experimental window sharing

This branch contains a native Mac capture host and independent Qt viewer windows.
It is an opt-in development milestone, not yet the completed drag-between-desktops
feature. The ordinary Deskflow build remains the default.

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

Closing a viewer returns its source window; it does not close the Mac application.
The host also restores on disconnect, sleep, session deactivation and normal exit.
The host listens for Control–Option–Shift–Escape to restore all exported windows.
That chord still needs integration before the core's HID tap consumes events
while controlling another computer. An owner-only restore journal is written
before relocation. `--recover` retries restoration after a crash, preserving
unresolved or malformed records rather than discarding them.

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

Only `holdingDisplay` and `headless` are optional. Use the Mac's actual interface
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

Both processes verify fixed role paths, PID birth identities and exact signed
code against the running bundle main's resource seal. A same-user process, an
unrelated ad-hoc signature, or an inherited FD alone cannot satisfy that check.
Directly launching the nested host cannot establish this private route. These are
packaged-code integrity checks, not publisher approval or complete loaded-module
integrity. The child drops ambient DYLD and Qt/QML plugin search overrides. Input
authorization remains unset; original capture and injection are still disabled.

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
core exposes a fingerprint only after its existing trusted-peer check succeeds;
ordinary encryption-only clients provide no authentication proof for this route.

An optional `keyboard-route-v1` capability coordinates the viewer's keyboard
forwarding with an independent native admission fence. The viewer releases its
held Qt keys before acknowledging the exact session, focus and geometry binding;
pointer input continues. Invalidated fences remain closed until local original
authority has been revoked and the host requests an exact viewer resume. Old
held keys stay suppressed through their releases. Legacy peers keep the ordinary
viewer path. The executable does not yet install the local controller required
to initiate this handoff, and the network protocol carries no original Mac keys.

Activation still requires both live authenticated network connections, an
explicit approved dual-pin mapping, a neutral capture boundary, a loaded-module
integrity policy, and causal ordering of original keys
with pointer events. A cache of the latest modifier flags is insufficient: a
delayed click must not be delivered after a later modifier release. These remain
integration requirements, not behavior claimed by the current automated tests.

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

Automatic edge-drag export/return and transactional drag ownership are unfinished.
Precise Wayland placement may need compositor cooperation; Qt screen selection
does not guarantee coordinates under every compositor. The Ubuntu machine and its
two physical monitors still require direct validation.

Dialogs, popup menus, clipboard integration with exported-window focus, complete
shortcut remapping, browser/Electron behavior, active-stream media
scheduling, crash-watchdog recovery, and packaged cross-machine performance tests
remain development work. Do not treat this application as a replacement for the
working Deskflow installation yet.
