<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors -->
<!-- SPDX-License-Identifier: MIT -->

# Disposable physical-input acceptance: Linux half

`physical-linux.py` runs the actual experimental Deskflow client and its
provider-owned Qt viewer in a private, native headless GNOME 50 container. It
never sends test EIS input. Original-input authorization remains off. An EIS
connection or a painted-viewport observation is **not physical-input success**;
the Mac coordinator must verify the human's input and the owned source receipts.

Use an existing Ubuntu 26.04 image containing GNOME 50, libei 1.5+, Qt 6.10 Wayland
and the dependencies listed in `gnome-provider-container.sh`. Supply its exact
installed `sha256:` image ID, the actual experimental Linux core and viewer,
and the public pins of the Mac's fresh core and WindowShare host identities.
The source checkout must remain unchanged from preparation through cleanup.
No host packages, user compositor, ordinary Deskflow settings, or ordinary
Deskflow processes are modified.

On the Linux Docker host, as the ordinary user:

```sh
python3 deploy/windowshare/physical-linux.py prepare \
  --run /absolute/new/private-run \
  --core /absolute/build/bin/deskflow-core \
  --viewer /absolute/build/deskflow-windowshare \
  --image sha256:EXACT_INSTALLED_IMAGE_ID \
  --mac-core-pin MAC_CORE_SHA256 --mac-viewer-pin MAC_HOST_SHA256 \
  --ordinary-pid ORDINARY_DESKFLOW_PID \
  --ordinary-config /absolute/ordinary/Deskflow.conf
```

Repeat `--ordinary-pid` and `--ordinary-config` for each relevant ordinary
process/settings file. Missing explicitly listed settings files are tracked as
absent; existing files must be regular files. Configuration contents and private
keys are never returned. Preparation creates fresh core/viewer identities using
an isolated container with no network or display access. It writes
`private-run/linux-plan.json` (version 2, controlProtocol 2), including public pins,
binary/source hashes, ordinary-state hashes, and exact owned cleanup arguments.

The Mac coordinator preserves that plan, installs only its public pins into
fresh private trust/configuration, and invokes `runArgv` over isolated SSH,
substituting the exact owned source window ID for `{windowId}`. Both endpoints
use mutual TLS pinning. The two `remoteSockets` are in separate token-owned
directories; `macLoopbackPorts` uses the same `core`/`viewer` keys. Forward each
socket to its corresponding temporary Mac loopback listener. No container host
networking or physical display/input devices are used.

The control sequence is:

1. Linux emits `prepared`. No compositor, relay or core has started, and the
   forwarding socket files need not exist yet. This phase expires after 600 s.
2. The user presses the visible Mac **Start** button. The coordinator starts
   its own Mac endpoints and SSH forwards, then sends `{"id":1,"op":"start"}`.
3. Linux validates the actual sockets and starts GNOME/core/viewer. Startup is
   bounded to 80 s. `ready` echoes the start ID and starts a separate 180 s running
   deadline. The resource lifetime after Start is at most 270 s.
4. Send `baseline` once, before human actions. Keep focus/setup actions in the
   receipts and distinguish them from the requested measurement sequence.
   `status` reports passive packet counters and
   current process/relay liveness; authentication/EIS log markers are explicitly
   historical. Viewer focus and physical success remain unknown.
5. Send `stop` before the deadline. The Linux core receives a stop on its own
   private IPC socket; its real exit status and all cleanup failures are saved.

Every command has exactly `id` and `op`; IDs strictly increase up to 2^53−1.
Allowed operations are `start`, `baseline`, `status`, `stop`. Frames are at
most 4 KiB and a run accepts at most 256 commands. No keyboard, mouse, scroll,
synthetic setup, or arbitrary process arguments are accepted through control.
Neither status queries nor later commands renew deadlines. Stop before Start
and during startup is supported, including before the initial `prepared`
receipt. Other commands must wait for `prepared`. A pending startup Stop is checked between
bounded setup operations and prevents a later `ready` receipt. EOF, expiry,
malformed commands and process/transport failures
terminate the owned run.

The host supervisor audits the ordinary process identities and settings before
the container, immediately before forwarding Start, and after it exits. A
successful `stopped` receipt requires `lifecycleCompleted:true` and
`cleanup.ordinaryPreserved:true`, `cleanup.ownedContainerStopped:true`, and
`cleanup.containerExit:0`. It always says `physicalAcceptance:"not-assessed"`;
these fields describe lifecycle, not a physical-input verdict. A started run
also needs `coreExit:0` and `coreStoppedByPrivateIpc:true`. Preparation waiting
alone can stop successfully without ever creating a core.

After stopping SSH, the coordinator verifies/removes only its recorded socket
inodes and invokes the exact `cleanupArgv`. Cleanup validates the token-owned
directories and permits already absent sockets. Preserve `remoteResults`
including `result.json`, `container-result.json`, process logs and control
transcript. Nonzero exits remain evidence even when final cleanup succeeds.
Use a fresh run directory and new identities for another attempt.

The headless desktop may initially show overview or lack viewer focus. Human
Escape/move/click setup must be distinguished from the requested measurement
sequence while preserving all post-baseline receipts. Do
not introduce a synthetic EI sender to make a physical-input test appear ready.
The Mac coordinator checks permissions and source visibility before presenting
Start. Only after the user presses Start does it launch the Mac core and verify
both expected hotkey registrations (`macKey123`/`macKey124`, `macMask1a00`). It
starts the Linux native session and shows Active only after those checks; no
Mac core or input hooks are needed while waiting at the visible Start gate.

Pure/control tests (no container, compositor, core or native input):

```sh
python3 deploy/windowshare/physical-linux-tests.py
python3 deploy/windowshare/gnome-crossmachine-tests.py
```
