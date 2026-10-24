#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Read-only GNOME readiness and explicit, stopped-session provider setup.

No operation creates a remote-desktop session or captures input. Installation
and enablement are separate explicit actions; neither is a readiness probe.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile
import uuid

UUID = 'seamless-windows@deskflow.org'
SERVICE = 'org.deskflow.Seamless.Gnome50'
OBJECT = '/org/deskflow/Seamless/Gnome50'
# Keep this canonical order identical to the module-load checksum in extension.js.
RUNTIME_FILES = ('extension.js', 'interface.xml', 'launcher.js', 'logic.js',
                 'metadata.json', 'ordering.js', 'window-drag.js')
MAX_FILE = 2 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def runtime_files(directory):
    require(not directory.is_symlink() and directory.is_dir(), 'Provider directory is missing or is a symbolic link')
    files = {}
    for name in RUNTIME_FILES:
        path = directory / name
        info = path.lstat()
        require(stat.S_ISREG(info.st_mode) and info.st_size <= MAX_FILE, 'Invalid provider file: ' + name)
        files[name] = path.read_bytes()
    metadata = json.loads(files['metadata.json'])
    require(metadata.get('uuid') == UUID and metadata.get('shell-version') == ['50'],
            'This package does not contain the expected GNOME 50 support')
    return files


def revision(files):
    checksum = hashlib.sha256()
    for name in RUNTIME_FILES:
        checksum.update(name.encode() + b'\0')
        checksum.update(files[name])
    return checksum.hexdigest()


class Desktop:
    """Bounded calls to the current Shell's unique bus owner; never auto-start."""
    def __init__(self):
        try:
            import gi
        except ImportError:
            raise ValueError('Install Ubuntu package python3-gi, then check support again') from None
        gi.require_version('Gio', '2.0')
        from gi.repository import Gio, GLib
        self.Gio, self.GLib = Gio, GLib
        self.connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        self.owner = self.bus('GetNameOwner', ('org.gnome.Shell',))[0]

    def call(self, destination, path, interface, method, signature=None, values=()):
        parameters = self.GLib.Variant(signature, values) if signature else None
        return self.connection.call_sync(destination, path, interface, method, parameters, None,
                                         self.Gio.DBusCallFlags.NO_AUTO_START, 2000, None).unpack()

    def bus(self, method, values):
        return self.call('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                         method, '(s)', values)

    def shell(self, method):
        return self.call(self.owner, '/org/gnome/Shell', 'org.gnome.Shell.Extensions', method, '(s)', (UUID,))

    def info(self):
        return self.shell('GetExtensionInfo')[0]

    def capabilities(self):
        require(self.bus('GetNameOwner', (SERVICE,))[0] == self.owner,
                'Window Sharing support belongs to a different desktop process')
        return self.call(self.owner, OBJECT, SERVICE, 'GetCapabilities')

    def current(self):
        require(self.bus('GetNameOwner', ('org.gnome.Shell',))[0] == self.owner,
                'The desktop restarted during the check; check again')


def result(code, detail, *, ready=False, action='', **extra):
    return dict(ready=ready, code=code, detail=detail, action=action, inputStarted=False, **extra)


def target_path():
    data = Path(os.environ.get('XDG_DATA_HOME', str(Path.home() / '.local/share')))
    require(data.is_absolute(), 'XDG_DATA_HOME must be an absolute path')
    return data / 'gnome-shell/extensions' / UUID


def readiness(source, target, desktop):
    expected = revision(runtime_files(source))
    info = desktop.info()
    require(isinstance(info, dict), 'GNOME returned invalid extension information')
    desktop.current()
    if not target.exists() and not target.is_symlink():
        return result('missing', 'Install Ubuntu support, then sign out and back in so GNOME can discover it.', action='install')
    try:
        installed = revision(runtime_files(target))
    except (ValueError, OSError) as error:
        return result('invalid-installation', str(error))
    if installed != expected:
        disabled = not info or (info.get('enabled') is False and info.get('state') in (2, 6))
        return result('update-needed', 'Ubuntu support needs updating. Turn it off in Extensions first if it is enabled.',
                      action='install' if disabled else '')
    if not info:
        return result('not-discovered', 'Save your Ubuntu work, sign out and sign back in, then check again.')
    require(info.get('uuid') == UUID and info.get('path') == str(target),
            'GNOME discovered a different copy of Window Sharing support')
    if info.get('error'):
        return result('extension-error', 'GNOME could not load Ubuntu support: ' + str(info['error'])[:500])
    if info.get('enabled') is False and info.get('state') in (2, 6):
        return result('disabled', 'Enable Ubuntu support. This does not start keyboard or pointer sharing.', action='enable')
    if info.get('enabled') is not True or info.get('state') != 1:
        return result('not-active', 'Ubuntu support is not active. Check Extensions, then check again.')
    version, shell, caps = desktop.capabilities()
    desktop.current()
    if caps.get('providerRevision') != expected:
        return result('restart-needed', 'GNOME is still running older Ubuntu support. Save your work, sign out and back in.')
    if version != 1 or str(shell).split('.')[0] != '50' or caps.get('backend') != 'MetaBackendNative':
        return result('unsupported-desktop', 'Use the supported GNOME 50 Wayland desktop.')
    required = ('nativeHitTesting', 'exactWaylandClientBinding', 'perWindowAssociation',
                'routedPointsObservation', 'inputPreparation', 'inputDrainSegment', 'windowDragPlacement')
    if any(caps.get(name) is not True for name in required) or not caps.get('inputDrainOrderingContract'):
        return result('unsupported-runtime', 'This Ubuntu compositor cannot provide the required input handoff. ' +
                      str(caps.get('inputDrainOrderingLimit', ''))[:500])
    return result('ready', 'Ubuntu support is ready. Sharing starts only when you click Start sharing.',
                  ready=True, revision=expected, shellVersion=shell,
                  orderingContract=caps['inputDrainOrderingContract'])


def owned_parents(path):
    # Do not traverse a link or write through another user's directory. Existing
    # system ancestors may be root-owned; the actual extension parent must be ours.
    for part in reversed([path, *path.parents]):
        if not part.exists() and not part.is_symlink():
            part.mkdir(mode=0o700)
        info = part.lstat()
        require(stat.S_ISDIR(info.st_mode) and info.st_uid in (0, os.getuid()) and
                (not info.st_mode & 0o022 or part == Path('/tmp')),
                'Unsafe Ubuntu support directory: ' + str(part))
    require(path.stat().st_uid == os.getuid(), 'Ubuntu support directory must belong to this user')


def install(source, target, desktop):
    files = runtime_files(source)
    info = desktop.info()
    require(not info or (info.get('uuid') == UUID and info.get('enabled') is False and
                        info.get('state') in (2, 6) and not info.get('error') and info.get('path') == str(target)),
            'Turn off Window Sharing support in Extensions before installing an update')
    desktop.current()
    owned_parents(target.parent)
    if target.exists() or target.is_symlink():
        # Do not adopt or destroy an unrelated installation. Keep its whole tree
        # as a sibling backup; no recursive deletion of the previous tree occurs.
        require(not target.is_symlink() and target.stat().st_uid == os.getuid(),
                'Existing Ubuntu support does not belong to this user')
        runtime_files(target)
    staged = Path(tempfile.mkdtemp(prefix='.deskflow-stage-', dir=target.parent))
    backup = None
    try:
        for name, data in files.items():
            (staged / name).write_bytes(data)
            (staged / name).chmod(0o600)
        require(revision(runtime_files(staged)) == revision(files), 'Staged Ubuntu support differs from this package')
        desktop.current()
        # Recheck enablement just before replacing anything. The profile caller
        # also holds the per-user session lock and refuses a running Core.
        require(desktop.info() == info, 'Ubuntu support changed during installation; check again')
        if target.exists():
            backup = target.with_name('.deskflow-previous-' + uuid.uuid4().hex)
            target.rename(backup)
        try:
            staged.rename(target)
        except OSError:
            if backup is not None:
                backup.rename(target)
            raise
    finally:
        if staged.exists():
            shutil.rmtree(staged)
    return result('installed', 'Ubuntu support installed. Save your work, sign out and back in, then check again.',
                  backup=str(backup) if backup else '', installedRevision=revision(files))


def operate(operation, source, target, desktop):
    if operation == 'install':
        return install(source, target, desktop)
    status = readiness(source, target, desktop)
    if operation == 'enable':
        require(status['action'] == 'enable', status['detail'])
        require(desktop.shell('EnableExtension') == (True,), 'GNOME did not enable Ubuntu support; check Extensions')
        desktop.current()
        # Enablement can complete asynchronously. No sleeps, polling or implied
        # readiness: the user explicitly checks again after GNOME has loaded it.
        return result('enable-requested', 'Enable requested. Click Check Ubuntu support to see when it is ready.')
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('status', 'install', 'enable'))
    args = parser.parse_args()
    require(sys.platform == 'linux' and os.environ.get('XDG_SESSION_TYPE') == 'wayland' and
            os.environ.get('DBUS_SESSION_BUS_ADDRESS'), 'Open the controls on the logged-in Ubuntu Wayland desktop')
    try:
        answer = operate(args.operation, Path(__file__).resolve().parent / 'gnome-provider', target_path(), Desktop())
    except Exception as error:
        answer = result('unavailable', 'Ubuntu support: ' + str(error)[:700])
    print(json.dumps(answer))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as error:
        print(json.dumps(result('unavailable', str(error))))
