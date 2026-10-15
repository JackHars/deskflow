#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Opt-in physical harness support for the user's existing GNOME desktop.

Never starts/restarts a compositor, sends input, or edits ordinary Deskflow
settings. The caller must obtain permission before launching the physical test.
The provider is enabled only after its visible Start, then restored on exit.
"""
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import signal
import stat
import subprocess
import time

MODE = 'user-desktop'
UUID = 'seamless-windows@deskflow.org'
FILES = ('extension.js', 'logic.js', 'launcher.js', 'ordering.js', 'window-drag.js',
         'interface.xml', 'metadata.json')
SETTINGS = ('enabled-extensions', 'disabled-extensions')


def require(value, message):
    if not value:
        raise RuntimeError(message)


def atomic(path, value):
    temporary = path.with_suffix('.new')
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, 'w') as stream:
        json.dump(value, stream, sort_keys=True)
        stream.write('\n')
    os.replace(temporary, path)


def identity(pid):
    proc = Path('/proc') / str(pid)
    require(type(pid) is int and pid > 1 and proc.stat().st_uid == os.getuid(),
            'Desktop process is not owned by this user')
    before = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
    executable = os.readlink(proc / 'exe')
    command = (proc / 'cmdline').read_bytes()
    after = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
    require(before[19] == after[19] and after[0] != 'Z', 'Desktop process changed during inspection')
    return {'pid': pid, 'startTicks': after[19], 'executable': executable,
            'commandSha256': hashlib.sha256(command).hexdigest()}


def same_process(expected):
    try:
        actual = identity(expected['pid'])
        return all(actual.get(k) == v for k, v in expected.items())
    except (FileNotFoundError, ProcessLookupError):
        return False


def environment():
    runtime = Path('/run/user') / str(os.getuid())
    info = runtime.lstat()
    require(stat.S_ISDIR(info.st_mode) and info.st_uid == os.getuid() and not info.st_mode & 0o077,
            'Actual desktop runtime is not private and owned')
    bus = runtime / 'bus'
    info = bus.lstat()
    require(stat.S_ISSOCK(info.st_mode) and info.st_uid == os.getuid(), 'Wrong desktop bus socket')
    return {'XDG_RUNTIME_DIR': str(runtime), 'DBUS_SESSION_BUS_ADDRESS': 'unix:path=' + str(bus)}


def settings(common):
    # Dconf must use the selected user bus, even when SSH has no desktop env.
    os.environ.update(environment())
    Gio, _ = common.load_gio()
    return Gio.Settings.new('org.gnome.shell')


def shell_call(observer, method, signature=None, values=()):
    return observer.call(method, signature, values, destination='org.gnome.Shell',
                         path='/org/gnome/Shell', interface='org.gnome.Shell.Extensions')


def provider_files(api):
    target = Path(pwd.getpwuid(os.getuid()).pw_dir) / '.local/share/gnome-shell/extensions' / UUID
    require(target.is_dir() and not target.is_symlink(), 'Install the default-off GNOME provider first')
    hashes = {}
    for name in FILES:
        path = target / name
        info = path.lstat()
        require(stat.S_ISREG(info.st_mode) and info.st_uid == os.getuid() and not info.st_mode & 0o022,
                'Installed provider file has unsafe ownership or permissions')
        hashes[name] = api.sha(path)
        require(hashes[name] == api.sha(api.HERE / 'gnome-provider' / name),
                'Installed provider differs from the frozen source: ' + name)
    return {'path': str(target), 'hashes': hashes}


def inspect(api, common, session_id):
    require(isinstance(session_id, str) and re.fullmatch('[A-Za-z0-9_-]{1,64}', session_id),
            'Explicit local graphical session ID required')
    fields = subprocess.check_output(['loginctl', 'show-session', session_id,
        '-p', 'User', '-p', 'Type', '-p', 'Active', '-p', 'Remote', '-p', 'State', '-p', 'Seat'],
        text=True, timeout=5)
    session = dict(line.split('=', 1) for line in fields.splitlines() if '=' in line)
    require(session.get('User') == str(os.getuid()) and session.get('Type') == 'wayland' and
            session.get('Active') == 'yes' and session.get('Remote') == 'no' and
            session.get('State') == 'active' and bool(session.get('Seat')),
            'Selected session must be this user\'s active physical Wayland desktop')
    env = environment()
    observer = common.Provider(env['DBUS_SESSION_BUS_ADDRESS'])
    try:
        owner, = observer.bus('GetNameOwner', '(s)', ('org.gnome.Shell',))
        pid, = observer.bus('GetConnectionUnixProcessID', '(s)', (owner,))
        shell = identity(pid)
        require(shell['executable'] == '/usr/bin/gnome-shell', 'Desktop bus is not owned by GNOME Shell')
        info, = shell_call(observer, 'GetExtensionInfo', '(s)', (UUID,))
        require(info and info.get('state') in (2, 6),
                'GNOME must recognize the inactive provider; sign out and back in after first installation')
        installed = provider_files(api)
        require(info.get('path') == installed['path'], 'GNOME loaded a different provider directory')
        active, = observer.call('GetActive', destination='org.gnome.ScreenSaver',
                               path='/org/gnome/ScreenSaver', interface='org.gnome.ScreenSaver')
        require(not active, 'Unlock the Ubuntu desktop before preparation')
        manager = subprocess.check_output(['systemctl', '--user', 'show-environment'],
                                         env={**os.environ, **env}, text=True, timeout=5)
        values = dict(line.split('=', 1) for line in manager.splitlines() if '=' in line)
        display = values.get('WAYLAND_DISPLAY', '')
        require(re.fullmatch(r'wayland-[0-9]+', display), 'Expected the actual desktop Wayland socket')
        endpoint = Path(env['XDG_RUNTIME_DIR']) / display
        metadata = endpoint.lstat()
        require(stat.S_ISSOCK(metadata.st_mode) and metadata.st_uid == os.getuid(), 'Wrong Wayland socket')
        store = settings(common)
        require(not store.get_boolean('disable-user-extensions'), 'User extensions are globally disabled')
        membership = {key: list(store.get_strv(key)) for key in SETTINGS}
        require(UUID not in membership['enabled-extensions'], 'Provider is already enabled; do not borrow its session')
        require(all(store.is_writable(key) for key in SETTINGS), 'Extension settings are not writable')
        return {'sessionId': session_id, 'session': session, 'shell': shell, 'shellBusOwner': owner,
                'bootId': Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                'environment': {**env, 'WAYLAND_DISPLAY': display},
                'waylandSocket': {'path': str(endpoint), 'device': metadata.st_dev, 'inode': metadata.st_ino},
                'provider': installed, 'extensionMembership': membership,
                'extensionUserValues': {key: None if store.get_user_value(key) is None else
                                        store.get_user_value(key).unpack() for key in SETTINGS}}
    finally:
        observer.close()


def restore_membership(current, before):
    """Restore only our UUID, retaining unrelated changes made during the run."""
    result = [item for item in current if item != UUID]
    if UUID in before:
        result.insert(min(before.index(UUID), len(result)), UUID)
    return result


def restore(api, common, plan):
    path = Path(plan['runDirectory']) / 'desktop-lease.json'
    if not path.exists():
        return {'desktopRestored': True, 'desktopWasEnabled': False}
    lease = api.read_owned_json(path)
    require(lease['token'] == plan['token'] and lease['before'] == plan['desktopAtPrepare'],
            'Desktop rollback belongs to a different session')
    if lease.get('restored'):
        return lease['cleanup']
    require(provider_files(api) == lease['before']['provider'], 'Provider changed before rollback')
    store = settings(common)
    before = lease['before']
    for key in SETTINGS:
        current = list(store.get_strv(key))
        restored = restore_membership(current, before['extensionMembership'][key])
        if restored == before['extensionMembership'][key] and before['extensionUserValues'][key] is None:
            store.reset(key)
        else:
            require(store.set_strv(key, restored), 'Cannot restore provider setting ' + key)
    common.load_gio()[0].Settings.sync()
    observer = common.Provider(before['environment']['DBUS_SESSION_BUS_ADDRESS'])
    try:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            present, = observer.bus('NameHasOwner', '(s)', (common.SERVICE,))
            if not present:
                break
            time.sleep(0.05)
        require(not present, 'Temporary provider did not stop after settings restoration')
    finally:
        observer.close()
    actual = {key: list(store.get_strv(key)) for key in SETTINGS}
    cleanup = {'desktopRestored': actual == before['extensionMembership'], 'desktopWasEnabled': True,
               'unrelatedExtensionChangesPreserved': True}
    lease.update(restored=True, cleanup=cleanup)
    atomic(path, lease)
    return cleanup


def make_viewer_wrapper(api, plan, root, env):
    """Exec retains Mutter's exact process and inherited Wayland/viewport FDs."""
    run = Path(plan['runDirectory'])
    wrapper = root / 'viewer'
    private_env = {key: value for key, value in env.items() if key in
                   ('HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'XDG_DATA_HOME', 'TMPDIR')}
    code = '''#!/usr/bin/python3
import json,os,pathlib,sys
os.umask(0o077)
binary = BINARY
run = pathlib.Path(RUN)
os.environ.update(ENVIRONMENT)
log = os.open(run/'results/viewer.log', os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW, 0o600)
os.dup2(log, 1); os.dup2(log, 2)
if log > 2: os.close(log)
ticks = pathlib.Path('/proc/self/stat').read_text().rsplit(')',1)[1].split()[19]
receipt = {'pid':os.getpid(),'startTicks':ticks,'executable':binary}
with (run/'viewer-process.json').open('x') as output: json.dump(receipt,output)
os.execve(binary,[binary,*sys.argv[1:],'--quit-after','270'],os.environ)
'''.replace('BINARY', repr(plan['viewerExecutable'])).replace('RUN', repr(str(run))).replace(
        'ENVIRONMENT', repr(private_env))
    wrapper.write_text(code)
    wrapper.chmod(0o700)
    return wrapper


class DesktopLease:
    def __init__(self, api, common, plan, processes, root):
        self.api, self.common, self.plan = api, common, plan
        self.pid = plan['desktopAtPrepare']['shell']['pid']
        self.closed = False
        self.observer = None
        self.next_check = 0
        before = inspect(api, common, plan['desktopAtPrepare']['sessionId'])
        require(before == plan['desktopAtPrepare'], 'Physical desktop changed since preparation')
        self.before = before
        processes.env.update(before['environment'])
        for key in ('LIBGL_ALWAYS_SOFTWARE', 'GALLIUM_DRIVER', 'GSK_RENDERER', 'GSETTINGS_BACKEND'):
            processes.env.pop(key, None)
        self.viewer = make_viewer_wrapper(api, plan, root, processes.env)
        # Durable rollback intent precedes every settings mutation.
        atomic(Path(plan['runDirectory']) / 'desktop-lease.json',
               {'token': plan['token'], 'before': before, 'restored': False})
        try:
            store = settings(common)
            require(store.set_strv('disabled-extensions', [u for u in store.get_strv('disabled-extensions') if u != UUID]),
                    'Cannot remove temporary provider disable entry')
            require(store.set_strv('enabled-extensions', [*store.get_strv('enabled-extensions'), UUID]),
                    'Cannot enable the temporary provider')
            common.load_gio()[0].Settings.sync()
            self.observer = common.Provider(before['environment']['DBUS_SESSION_BUS_ADDRESS'])
            def ready():
                processes.check()
                info, = shell_call(self.observer, 'GetExtensionInfo', '(s)', (UUID,))
                require(info.get('state') not in (3, 4), 'GNOME provider failed to enable')
                return info.get('state') == 1 and self.observer.bus('NameHasOwner', '(s)', (common.SERVICE,)) == (True,)
            common.wait_for(processes, ready, 'provider on the actual Ubuntu desktop', timeout=10)
        except BaseException:
            self.close()
            raise

    def poll(self):
        if not same_process(self.before['shell']):
            return 1
        if time.monotonic() >= self.next_check:
            owner, = self.observer.bus('GetNameOwner', '(s)', ('org.gnome.Shell',))
            require(owner == self.before['shellBusOwner'], 'Physical compositor bus owner changed')
            self.next_check = time.monotonic() + 1
        return None

    def close(self):
        if self.closed:
            return
        self.closed = True
        try:
            restore(self.api, self.common, self.plan)
        finally:
            if self.observer:
                self.observer.close()


def stop_process(expected):
    """Pidfd plus birth/executable checks: never target the compositor or user Core."""
    try:
        descriptor = os.pidfd_open(expected['pid'])
    except ProcessLookupError:
        return
    try:
        if not same_process(expected):
            return
        require(Path(expected['executable']).name in ('deskflow-core', 'deskflow-windowshare'),
                'Refusing a desktop cleanup target outside the two experimental roles')
        signal.pidfd_send_signal(descriptor, signal.SIGTERM)
        deadline = time.monotonic() + 3
        while same_process(expected) and time.monotonic() < deadline:
            time.sleep(0.05)
        if same_process(expected):
            signal.pidfd_send_signal(descriptor, signal.SIGKILL)
        deadline = time.monotonic() + 2
        while same_process(expected) and time.monotonic() < deadline:
            time.sleep(0.05)
        require(not same_process(expected), 'Owned desktop test process did not stop')
    finally:
        os.close(descriptor)


def cleanup(api, common, plan):
    errors = []
    for role in ('core', 'viewer'):
        path = Path(plan['runDirectory']) / (role + '-process.json')
        try:
            if path.exists():
                expected = api.read_owned_json(path)
                require(expected['executable'] == plan[role + 'Executable'], 'Wrong experimental cleanup executable')
                stop_process(expected)
        except BaseException as error:
            errors.append(str(error))
    restored = restore(api, common, plan)
    require(not errors, '; '.join(errors))
    return {'ownedProcessesStopped': True, **restored}
