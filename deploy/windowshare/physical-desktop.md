<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors -->
<!-- SPDX-License-Identifier: MIT -->

# Physical testing on the actual Ubuntu desktop

Use this mode when the person is watching the monitor attached to Ubuntu.
The previous container mode starts a separate desktop on the same machine;
neither its headless output nor its Mac preview tests the user's actual monitor.
Obtain fresh user permission immediately before **each** physical launch.
Configuration, read-only preflight and the tests below do not launch that test.

## One-time preparation

The logged-in user must have an active, unlocked local GNOME 50 Wayland session,
the supported mapped Mutter/libeis build, Qt Wayland, Python GIO and the tested
experimental Core/Viewer executables. Use `loginctl list-sessions` to identify
the graphical `seat0` session, not the SSH session or systemd user manager.

Install these unchanged files from `gnome-provider/` into the user's
`~/.local/share/gnome-shell/extensions/seamless-windows@deskflow.org/`:
`extension.js`, `logic.js`, `launcher.js`, `ordering.js`, `window-drag.js`,
`interface.xml`, and `metadata.json`. Preserve an existing installation; do not
overwrite a different version. Keep the extension disabled. After first
installation, the user must save work and sign out/back in so GNOME discovers
the files. Do not restart the compositor or log the user out automatically.
GNOME 50's public `ReloadExtension` is deprecated and returns not-supported;
do not enable unsafe Shell evaluation to work around it.

Sources: [Shell extension service](https://github.com/GNOME/gnome-shell/blob/50.1/js/ui/shellDBus.js)
and [extension discovery](https://github.com/GNOME/gnome-shell/blob/50.1/js/ui/extensionSystem.js).

## Per-attempt preparation and launch

Follow [original-input-physical.md](original-input-physical.md), selecting Mac
`freeze --linux-desktop` and Linux `prepare --production-original-input
--desktop-session SESSION_ID`. Do not specify a container image or desktop
preview. Use fresh private directories and identities each time. Preparation
hashes the installed provider, ordinary settings/processes and experimental
binaries, and retains the actual boot, login session, Shell PID/birth identity,
Shell bus owner and Wayland socket identity. It refuses an already enabled
provider, a locked/remote/non-Wayland session or a source mismatch. Identity
generation uses Qt offscreen; it opens no window or input session. The plan
pair can then be checked with `--validate-only`.

Ask for permission when ready to launch. The Mac source opens at Ready. Its
Start click revalidates the selected desktop and begins the temporary provider
session, Linux Core and the provider-owned Viewer on the actual monitor. No
compositor is created, replaced, signaled or restarted. The Mac preview is off.
Start/stop commands cannot inject keys or pointer events. Existing absolute
startup/session/recording bounds and all production input checks still apply.

The actual Core has private configuration, certificates, cache and IPC. A
short exec wrapper gives the provider-owned Viewer private application paths
and a bounded private log, retaining its exact PID and Mutter's inherited
Wayland/viewport descriptors. The runtime desktop bus/socket remain real.
The wrapper does not synthesize input, replace the native protocol, or grant
delivery authority. Only the disposable Mac source is exported.

After Active, look at the Ubuntu monitor. Switch with
Control+Option+Shift+Right, release the modifiers, and click the test window's
text box there. Follow its priming/Record sequence, staying on the Ubuntu route.
Control+Option+Shift+Left returns to the Mac. Never treat missing focus, absent
windows or a failed Record request as successful delivery.

## Restoration and evidence

A durable rollback record is written before enabling the extension. Cleanup
first stops the experimental Core through its private IPC, then disables the
temporary provider and restores its previous extension membership. The provider
retires only its owned Viewer/EIS session. Exact PID/birth/executable records and
Linux pidfds bound fallback cleanup to the two experimental roles. Neither the
ordinary Deskflow client nor GNOME Shell is a cleanup target.

Restore only the test UUID; keep unrelated extension changes made by the user.
Such changes prevent an unchanged-desktop audit from passing. Settings that
originally used schema defaults are reset to defaults when no unrelated edits
intervened. A completed rollback is idempotent. The installed provider files
remain available, switched off, for the next separately approved test.

Preserve `results/desktop-result.json`, `results/result.json`, Core/Viewer logs,
the desktop rollback record, control transcript and Mac evidence. Successful
lifecycle receipts require `ordinaryPreserved`, `ownedProcessesStopped`,
`desktopRestored` and `controllerExit:0`; they never invent a container exit.
Actual input/neutral restoration still requires the native receipts and the
human's observation. `physicalAcceptance` remains `not-assessed` until review.

Unattended regressions (no real desktop access or input):

```sh
python3 deploy/windowshare/physical-desktop-tests.py
python3 deploy/windowshare/physical-linux-tests.py
python3 deploy/windowshare/physical-macos-tests.py
```

The Linux-only exec test verifies real descriptor/PID preservation using two
plain files and a Python child. It does not open a compositor or input device.
