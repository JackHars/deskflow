<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors -->
<!-- SPDX-License-Identifier: MIT -->

# User-started physical input acceptance

This records a physical Mac keyboard/trackpad session through the experimental
Mac primary, the actual Ubuntu client, its private GNOME EIS/viewer session,
and the Mac native source. Only the disposable source window is exported.
The test does not enable original-input delivery, move a source to a holding
display, use the ordinary Linux desktop, or establish two-monitor/drag support.

For the distinct opt-in production original-input mode and its optional
separate-control holding run, use [original-input-physical.md](original-input-physical.md).
Its v2 Mac/v3 Linux plans cannot be used by this legacy default.

First install and verify the experimental Mac package containing the Option
hotkey fix. Build the current `windowshare-smoke-source` target. Prepare a
fresh Linux plan as described in [physical-linux.md](physical-linux.md).
Never use the ordinary Deskflow settings or trust directory for this test.

The Mac preparation file is a private version 1 JSON object with
`preparedOnly:true`, `originalInputEnabled:false`, and
`holdingDisplayEnabled:false`. It describes an independently prepared
loopback-only server with fresh test certificates:

| Field | Required value |
| --- | --- |
| `coreScreen`, `linuxScreen` | The two test screen names; Linux uses `gnome-physical-test` |
| `corePort`, `hostPort` | The distinct temporary loopback ports matched by the Linux plan |
| `coreFingerprint`, `hostFingerprint` | Public SHA-256 pins of the Mac core and native host certificates |
| `coreArgv` | Separate arguments: absolute experimental core, `server --new-instance --settings`, private INI path |
| `hostArgv` | Experimental host path and its `--identity` path; the coordinator constructs the actual loopback invocation |
| `sourceExecutable` | Absolute path to the current disposable source executable |
| `coreIpcExpectedPath` | `deskflow-core` inside the test's private `TMPDIR` |
| `environmentOverrides` | Private absolute paths for `XDG_CONFIG_HOME`, `XDG_CONFIG_DIRS`, `XDG_STATE_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `XDG_DATA_DIRS`, `XDG_RUNTIME_DIR`, `TMPDIR` |

All configuration, trust, identity and IPC paths must remain under the Mac
preparation directory. Create that directory and its state directories privately
(mode 0700), with configuration and certificate/key files mode 0600.
The separate core INI must enable TLS and peer fingerprint checking, disable
clipboard and enter/exit commands, use DEBUG logging, identity modifier maps
for both screens, and bind to `127.0.0.1`. Its external server configuration
contains only:

```text
section: options
    keystroke(control+alt+shift+Right) = switchToScreen(gnome-physical-test)
    keystroke(control+alt+shift+Left) = switchToScreen(MAC_TEST_SCREEN_NAME)
end
```

Use the actual `coreScreen` in the second rule. No edge links are installed.
The coordinator validates these settings, refuses another running Mac primary,
and updates only the prepared private `tls/trusted-clients` with the new
Linux core's public pin. It never copies a private key between machines.

Run on the Mac with an existing SSH key and a fresh output directory:

```sh
python3 deploy/windowshare/physical-macos.py \
  --mac-plan /absolute/private/mac-preparation.json \
  --linux-plan /absolute/fresh/linux-plan.json \
  --ssh-target USER@UBUNTU_IP --ssh-key /absolute/existing/ssh-key \
  --ordinary-mac-config /absolute/ordinary/Deskflow.conf \
  --ordinary-mac-config /absolute/ordinary/deskflow-server.conf \
  --output /absolute/new/results
```

List the ordinary configuration files whose hashes must remain unchanged.
The Linux plan carries its own ordinary process/configuration audit. A source
override is available with `--source`; record its executable hash with the run.

The source window shows preparation, then a **Start** button. Before Start,
only this source and the prepared Linux controller run; the Mac core input
hooks and the Linux compositor/client have not started. After Start, the
coordinator starts the isolated route and verifies that both arrow hotkeys
registered the expected Control+Option+Shift Carbon mask. The old swapped
modifier build fails setup here. **Active** begins the visible 120-second
countdown only after the two connections are ready and baselines are recorded.
Readiness does not imply viewer focus or prove input delivery.

Follow the instructions in the window: switch right, release modifiers, press
Escape, type a/Shift+b/c, hold and release x, exercise pointer/click/drag and
both scroll directions, then switch left and click Stop. Human setup is retained
in the baseline-to-stop event history; distinguish it from the requested input
when reviewing receipts. Stop, close, loss of the status heartbeat and the
deadline all request teardown. An unchanged old status file cannot rearm the
window or extend its countdown. If the coordinator dies, a later close is
allowed after 15 seconds without fresh status.

The source stores event codes and metadata, never typed text. The collector
merges bounded histories by monotonic sequence and detects missing intervals.
Missing/stale source publications cannot establish restoration. Disconnect must
produce a fresh neutral receipt and an empty journal while the host remains
alive. Cleanup separately checks a fresh source receipt after host termination,
ordinary settings, exact owned socket identities, and child exits. A hung host
gets a bounded stack sample before force termination; the run remains failed.

An exit-zero coordinator result means
`session-recorded-awaiting-human-and-receipt-review`, with
`physicalAcceptance:"not-assessed"`. It is not a physical pass. Preserve the
Mac results, exact Linux plan and all Linux results, then correlate actual
legacy/EIS activity with tagged receipts from the correct Mac host PID/window,
verify no gaps or held input, and obtain the human's visual result.

Data/pipe regressions require no native application or input:

```sh
python3 deploy/windowshare/physical-macos-tests.py
```
