<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->

# Disposable Mac-to-GNOME acceptance: Linux half

`gnome-crossmachine-client.py` runs the actual Qt Wayland viewer, the production
GNOME provider client and a libei sender inside a new headless GNOME 50 session.
The Mac coordinator owns a disposable Mac source and the actual capture host.
This is synthetic input acceptance, not original physical input delivery.
It does not use the Linux user's desktop, install the extension there, or grant
original-input authority. `QT_SCALE_FACTOR=1.25` exercises Qt content scaling,
not fractional GNOME monitor scaling.

Use the dependency image built for `gnome-provider-container.sh`, including
GNOME 50, libei/libeis 1.5 or later and matching Qt 6.10 base/Wayland private headers.
Build with `WINDOWSHARE_APP=ON` and `WINDOWSHARE_GNOME_FIXTURE=ON`. The two
required executables are `deskflow-windowshare` and
`windowshare-gnome-viewer-fixture`. Record the source manifest, image ID and
executable hashes for each acceptance run.

Prepare a fresh private Linux run directory, with a `forward` directory mode 0700
and a `results` directory writable by the container's unprivileged UID. Generate
a unique container name `deskflow-cross-<16 lowercase hex characters>` and a
32-character lowercase hexadecimal ownership token. Write mode 0600
`forward/owner.json` as
`{"schema":1,"container":"NAME","token":"TOKEN"}`. Mount the forward directory
read-only into the container. A fresh SSH streamlocal reverse forward creates
`forward/mac.sock`; the harness accepts only an owned socket without group/other
permissions. Mount no user display sockets, input devices or runtime directories.

Initialize the identity once in an unprivileged disposable container, retaining
the identity directory only on Linux:

```sh
python3 /source/deploy/windowshare/gnome-crossmachine-client.py init-identity \
  --viewer /build/deskflow-windowshare --identity /identity-root/viewer
```

The identity directory must not exist. Output is only an `identity` JSON event
with its public `viewerPin`; never copy or print `key.pem`.

The Mac coordinator consumes a version 1 plan with `remoteSocket`, `viewerPin`,
`runArgv`, and `cleanupArgv`. Each argument is a separate JSON string. The run
command must include literal separate `{macPin}` and `{windowId}` placeholders:

```sh
docker run --rm --init -i --name NAME \
  --label org.deskflow.crossmachine.run=TOKEN --network none --user UID:GID \
  --shm-size 256m --tmpfs /tmp:rw,nosuid,nodev,exec,size=1g,mode=1777 \
  --mount type=bind,source=SOURCE,target=/source,readonly \
  --mount type=bind,source=BUILD,target=/build,readonly \
  --mount type=bind,source=IDENTITY,target=/identity,readonly \
  --mount type=bind,source=FORWARD,target=/forward,readonly \
  --mount type=bind,source=RESULTS,target=/results \
  IMAGE python3 /source/deploy/windowshare/gnome-crossmachine-client.py run \
  --viewer /build/deskflow-windowshare --fixture /build/windowshare-gnome-viewer-fixture \
  --identity /identity --forward-socket /forward/mac.sock \
  --peer-pin '{macPin}' --window-id '{windowId}' --output /results/run --scale 1
```

The Unix socket is the only route out of the network-isolated container.
A one-client loopback TCP relay forwards TLS bytes without terminating TLS or
logging payloads. Each direction is bounded to 256 KiB queued and 512 MiB total;
the run is bounded to 180 seconds (the C++ fixture to 150 seconds). EOF is propagated
only after buffered bytes drain. During an explicitly requested stop, the exiting
viewer can reset its socket with unread TLS bytes. Only that viewer-side reset or
broken pipe permits dropping the undeliverable encrypted Mac-to-viewer tail; Mac-bound input still
must drain. An already-closed socket during an empty-queue write shutdown is
also recorded during coordinated stop; it never permits discarding queued input.
The report distinguishes `coordinatedPeerClose` from `drainedAtEof`
and records both queue sizes and bytes read, forwarded and discarded. Forwarded
bytes mean accepted by the socket, not application receipt; kernel-buffered tail
loss after a full close is unknown and marked explicitly. Before coordinated
stop, a reset remains a failure. Reconnect requires a fresh coordinated run.

JSON lines on stdin use strictly increasing integer `id` (1 through 2^53-1),
at most 128 commands and 4 KiB buffered input. No unknown or duplicate fields:

| Command | Additional fields | Reply |
| --- | --- | --- |
| `prepare` | none; once only | `prepared`, full `snapshot` |
| `move` | finite native desktop `x`, `y` | `synced` after EI PONG |
| `click` | none; complete left down/up | `synced` |
| `key` | Linux evdev `code` 1..767; complete down/up | `synced` |
| `scroll` | signed integer `dx`, `dy` in [-1200,1200], nonzero pair; v120 units | `synced` |
| `observe` | none | `observed`, live `snapshot` or explicit denial |
| `retained` | none | `retained`, current evidence liveness |
| `sync` | none | `synced` |
| `stop` | none | `stopped` after process and relay cleanup |

Replies preserve the coordinator's `id`; `fixtureId` records the inner request.
Unsolicited `ready` includes public peer/viewer pins, exact window ID, provider
session/viewer identity and EIS region. `prepare` moves only the private EIS
pointer off GNOME's initial corner barrier, centers it, sends Escape, then waits
for the first actual painted source. Baseline Mac receipts **after** its reply.
Subsequent measured input and observation commands are never retried.

The full snapshot contains `desktopX/Y`, `surfaceX/Y`, mapped `sourceX/Y`, native
window/surface revision, and `paint` with authenticated connection/session,
geometry/focus/paint/surface revisions, source/image sizes, logical scale,
client-side decoration offsets and video rectangle. `surfaceDesktopOrigin` is
also returned by `prepare`. The coordinator compares those mapped points with
the Mac source's actual tagged native and Qt receipts. EI PONG alone is not
delivery evidence, and a Linux `passed` report alone does not prove Mac receipt.

After stopping the SSH forwarding process, invoke the plan's cleanup command:

```sh
python3 SOURCE/deploy/windowshare/gnome-crossmachine-client.py cleanup \
  --container NAME --token TOKEN --forward-directory FORWARD
```

It removes only a matching labeled container and `mac.sock` in the exact private
token-owned directory; an already absent socket is accepted. It preserves the
identity, logs and ownership record. A mismatched label, token, symlink or broad
directory permissions fail cleanup. Allow 35 seconds for bounded Docker calls.
The coordinator may first unlink a socket whose recorded inode/device/UID still
match. Never delete a forwarded socket while its SSH listener is running.

Run `python3 deploy/windowshare/gnome-crossmachine-tests.py` in Linux to verify
command rejection, replay bounds, half-close draining and token-bound cleanup
without any compositor or native input. Actual cross-machine acceptance must
retain both coordinator and Linux `result.json`, transcript/log files, exact
source/image/executable hashes, and an ordinary-client preservation audit.
