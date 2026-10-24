<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors -->
<!-- SPDX-License-Identifier: MIT -->

# Persistent Mac → Ubuntu window sharing

This launcher runs an ordinary experimental session with Ubuntu above the Mac.
Drag an eligible Mac application's normal titlebar through the Mac display's
top edge, then release on Ubuntu. The application continues running on the Mac;
its shared window appears on Ubuntu and its source moves to the holding display.
There is no disposable test window, automatic export or two-minute session limit.

The connected edge-drag implementation is present; complete physical acceptance
is still pending. The experimental package now includes pairing and Start/Stop
controls backed by this profile manager.
Known scrolling timestamp and held-key repeat issues remain. Interactive dragging
back to the Mac is the last development priority; normal Stop, cancellation and
failure restoration remain part of the session.

## Graphical controls

On the Mac, open `DeskflowSeamless-Core-Experimental.app` normally. Its default
launch opens the controls and exits the short-lived Core launcher without
starting capture. Explicit Core server/client/help commands retain their normal
behavior. On Ubuntu, open `bin/deskflow-seamless-controls` from the extracted
package in your normal Wayland desktop session.

1. Click **Create setup** on each computer. The default private folders are
   `~/DeskflowMac` and `~/DeskflowUbuntu`. You can type a different new folder
   location or use **Open saved setup** for an existing one.
2. Click **Save pairing file** and transfer that public file to the other
   computer. Choose the other computer's file, enter the Mac's network address,
   and click **Pair computers** on each endpoint. Choose the receiving monitor
   on Ubuntu. Enable copied-text sharing on both endpoints if wanted.
3. Stop ordinary Deskflow on both computers, then click **Start sharing** on
   the Mac and Ubuntu. Once the peers connect, drag a Mac titlebar upward.
4. Click **Stop sharing**, close the controls, or quit the app to request
   graceful shutdown. The controls wait for their owned session to finish.

For a newer or relocated package, open its controls and select your existing
saved setup. Click **Use this version** to retain your identities and pairing.
Sharing stays stopped. You can also edit the Mac address, receiving monitor or
copied-text option in **Show setup**, then click **Save changes**. Start remains
disabled while these fields have unsaved changes. Change the Mac address on
both computers when it changes; clipboard sharing still needs both endpoints.

After pairing, the setup fields collapse into **Show setup** and the normal
view shows the monitor arrangement and Start/Stop. **Open logs** opens the
profile's logs directory. `controls.log` keeps bounded helper output, including
startup refusals before Core logging begins; `session.log` contains the runtime
stages. Failures remain visible in the control window.

**Ready to start** means the saved profile validates. **Session started** means
the owned runtime process has launched; it does not claim native delivery or
peer readiness. Opening, creating or pairing in these controls never starts
sharing. The GUI's inherited lifetime pipe is separate from input: closing it,
including on a controls-process crash, requests Stop; a closed pipe detected
before launch prevents starting Core. The backend owns shutdown and recovery
records. No ordinary connection is stopped automatically.

## Requirements

- Python 3.9 or later and OpenSSL on both computers.
- The separate experimental Mac **Core** bundle with the supported signing and
  exact-library policy, including its adjacent `deskflow-windowshare` executable.
  Grant the documented macOS capture and Accessibility permissions to that bundle.
- A matching Linux package and the supported native GNOME Wayland desktop.
  Install and enable the packaged `seamless-windows@deskflow.org` provider as
  described below, subject to its [compatibility contract](gnome-provider/README.md)
  and [Linux package requirements](package-linux.md). The launcher does not install
  or enable extensions. It does not create a nested desktop.
- The Mac's LAN address must be reachable from Ubuntu on TCP ports 24870 and
  24871 (or the same explicit replacements on both profiles).
- Stop the regular Deskflow connection on **both** computers before `run`.
  The launcher refuses a competing Core rather than stopping it for you.
  Profile creation, pairing and `run --dry-run` do not require that interruption.

Keep the package in a permanent location before creating a profile. Profiles pin
the executable paths and hashes; replacing a binary requires an explicit saved
setup update before it can start. Keep each profile
path short (for example `~/DeskflowMac` and `~/DeskflowUbuntu`) because private
Core IPC uses a length-limited Unix socket.

## Ubuntu provider setup

On Ubuntu set `PACKAGE` to the extracted Linux package directory. For a **first
installation**, run this in jack's normal desktop terminal:

```sh
EXT="$HOME/.local/share/gnome-shell/extensions/seamless-windows@deskflow.org"
if test -e "$EXT"; then
  echo "Provider already exists; compare its version before replacing it."
else
  install -d -m 700 "$EXT"
  for name in extension.js logic.js launcher.js ordering.js window-drag.js interface.xml metadata.json; do
    install -m 644 "$PACKAGE/share/deskflow-seamless-experimental/gnome-provider/$name" "$EXT/$name" || break
  done
fi
```

Verify that all seven files match the selected package. If an existing provider
differs, stop window sharing and preserve that installation before deliberately
updating it. After first installation, save your work and sign out and back in
so GNOME discovers the extension. Then enable it from a desktop terminal:

```sh
gnome-extensions enable seamless-windows@deskflow.org
```

This enables the provider; a paired session still requires an explicit `run`.
To disable it later, stop window sharing first, then use
`gnome-extensions disable seamless-windows@deskflow.org`. Do not restart Shell
or log out while shared source windows still need restoration.

## Create the endpoints

Use the actual absolute paths to your extracted packages. On the Mac:

```sh
APP="/absolute/path/DeskflowSeamless-Core-Experimental.app"
python3 "$APP/Contents/Resources/seamless-profile.py" init \
  --profile "$HOME/DeskflowMac" --role mac --name MacBook \
  --core "$APP/Contents/MacOS/deskflow-core" \
  --windowshare "$APP/Contents/MacOS/deskflow-windowshare"
```

On Ubuntu:

```sh
PACKAGE="/absolute/path/deskflow-seamless-linux-experimental"
"$PACKAGE/bin/deskflow-seamless-session" init \
  --profile "$HOME/DeskflowUbuntu" --role ubuntu --name JackUbuntu \
  --core "$PACKAGE/libexec/deskflow-seamless-experimental/deskflow-core" \
  --windowshare "$PACKAGE/libexec/deskflow-seamless-experimental/deskflow-windowshare"
```

Initialization generates two local TLS identities and a public `pairing.json`.
Only exchange that **public pairing file** over your trusted SSH connection.
Do not copy either profile directory or its private keys to the other computer.
Names must be different, start with a letter, and contain letters, numbers,
underscores or hyphens. Creation refuses an existing profile directory.

## Pair and inspect

Copy Ubuntu's public file to the Mac as `ubuntu-pairing.json`, and the Mac's
public file to Ubuntu as `mac-pairing.json`. Replace `192.168.1.101` below with
the Mac's current LAN address on **both** commands:

```sh
# Mac
python3 "$APP/Contents/Resources/seamless-profile.py" pair \
  --profile "$HOME/DeskflowMac" --peer /absolute/path/ubuntu-pairing.json \
  --mac-address 192.168.1.101

# Ubuntu
"$PACKAGE/bin/deskflow-seamless-session" pair \
  --profile "$HOME/DeskflowUbuntu" --peer /absolute/path/mac-pairing.json \
  --mac-address 192.168.1.101 --screen 0
```

`--screen` selects the Ubuntu monitor by the runtime's zero-based screen index.
Text clipboard sharing defaults off; add `--clipboard-text` on both pairing
commands to enable it. Core's separate generic clipboard forwarding stays off.

Pairing writes mutual Core and window-stream pins, the reciprocal top/bottom
screen links, and the private runtime configuration. It deliberately omits
`exportWindow`: a normal titlebar drag is the trigger. Pairing does not start
Core, capture, the holding display or the GNOME session.

Inspect each prepared launch without accessing its desktop:

```sh
# Mac
python3 "$APP/Contents/Resources/seamless-profile.py" run \
  --profile "$HOME/DeskflowMac" --dry-run
# Ubuntu
"$PACKAGE/bin/deskflow-seamless-session" run \
  --profile "$HOME/DeskflowUbuntu" --dry-run
```

This checks the saved identities, binaries and generated configuration. It does
not establish desktop permissions, peer availability or that input will work.

## Start, use and stop

After stopping regular Deskflow on both computers, run these commands in their
respective desktop terminals. On Ubuntu use a terminal in jack's actual Wayland
session, with its normal session bus. Start Mac first, then Ubuntu:

```sh
# Mac
python3 "$APP/Contents/Resources/seamless-profile.py" run --profile "$HOME/DeskflowMac"
# Ubuntu
"$PACKAGE/bin/deskflow-seamless-session" run --profile "$HOME/DeskflowUbuntu"
```

Once both runtimes connect, use a normal titlebar drag through the top edge.
The setup command's “starting” message is not a connection or transfer receipt.
Detailed runtime stages and failure reasons are written to `logs/session.log`
under each profile. Three rotated backups bound these logs to roughly 64 MiB.

Press **Control+C in the launching terminal** to stop that session. The launcher
requests graceful shutdown over the private socket after checking that the peer
is its own child. It drains logs during restoration; if shutdown fails it signals
only its own child, retains the restore journal and reports the failure.
Terminal hangup and termination requests use the same graceful shutdown path.
**Control+Option+Shift+Left** returns the cursor to the Mac; stopping the session
restores shared source windows. These are different operations.

Stop both experimental endpoints before restarting regular Deskflow. Nothing in
this launcher changes ordinary Deskflow settings or automatically resumes it.

## Changes, failures and recovery

`inspect --profile DIRECTORY` returns validated public setup information without
starting input. The GUI's pairing-file export uses that validated public identity,
not arbitrary replacement contents of the local `pairing.json` copy.
Inspection reports `packageReady: false` if the saved binaries changed or went
missing; it still validates identities and saved configuration. It never approves
replacement bytes. The GUI also supplies its current executable paths so a setup
bound to a different package requires the explicit update action.

`session.json` records the launch time, owned process, stop request, exit code,
shutdown method (including any signal fallback) and launcher error.
It is historical evidence, not proof a recorded PID is still
alive. Each profile has an exclusive session lock. A second run, pairing or
update operation is refused while that lock is held. A separate per-user lock also
prevents two different profiles from launching competing experimental sessions.

For a **new peer**, create a new profile and pair it. Package, address, monitor
and copied-text changes can retain the existing pairing. Stop and restore the
experimental session before updating; no ordinary connection needs to stop for
the update itself. For example, using the new package's launcher:

```sh
python3 "$APP/Contents/Resources/seamless-profile.py" update \
  --profile "$HOME/DeskflowMac" \
  --core "$APP/Contents/MacOS/deskflow-core" \
  --windowshare "$APP/Contents/MacOS/deskflow-windowshare"

"$PACKAGE/bin/deskflow-seamless-session" update \
  --profile "$HOME/DeskflowUbuntu" \
  --core "$PACKAGE/libexec/deskflow-seamless-experimental/deskflow-core" \
  --windowshare "$PACKAGE/libexec/deskflow-seamless-experimental/deskflow-windowshare"
```

Optional `--mac-address`, `--core-port`, `--window-port`, `--screen`, and
`--clipboard-text`/`--no-clipboard-text` update only those connection settings.
Unspecified values stay unchanged. Peer fingerprints, names and private keys
cannot be replaced by this operation. The old package may already be missing;
its saved identity and configuration must still validate before update.

The update prepares a complete private `configurations/<generation>` directory,
validates it, saves the previous descriptor in `profile.previous.json`, then
atomically selects the new version-2 `profile.json`. Launch uses only that
descriptor's generation. An interruption before selection leaves the old
descriptor selected; unused directories are retained for diagnosis. Existing
version-1 setups remain readable. Older launchers reject the version-2 schema
rather than silently running a stale configuration. Updates do not install or
enable the Ubuntu provider; update that component separately as described above.

Keep the old package, profile history and especially `restore.json` until any
outstanding restoration has completed. Do not erase a
restore journal to get past a failure. Native recovery remains responsible for
restoring sources after process failure; a launcher exit alone does not prove
that every source was restored.

Offline launcher checks (no desktop capture):

```sh
python3 deploy/windowshare/seamless-profile-tests.py
python3 deploy/windowshare/package-linux-tests.py
```
