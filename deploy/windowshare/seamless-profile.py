#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Persistent Mac-to-Ubuntu sessions, separate from the acceptance-test runners.

Creating/pairing a profile installs no input hooks. Only `run` launches Core.
Pairing exchanges public fingerprints; private keys never leave their endpoint.
"""
from __future__ import annotations

import argparse
import configparser
import contextlib
import datetime
import fcntl
import hashlib
import io
import ipaddress
import json
import logging
import logging.handlers
import os
from pathlib import Path
import plistlib
import re
import select
import selectors
import signal
import socket
import ssl
import stat
import struct
import subprocess
import sys
import tempfile
import time
import uuid

SCHEMA = 'deskflow-seamless-profile-v1'
UPDATED_SCHEMA = 'deskflow-seamless-profile-v2'
PAIRING_SCHEMA = 'deskflow-seamless-pairing-v1'
MAX_JSON = 16384


def require(condition, message):
    if not condition:
        raise ValueError(message)


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def text(value):
    require(isinstance(value, str) and value and len(value) <= 4096 and
            not any(ord(c) < 32 or c == '\x7f' for c in value), 'Invalid configuration text')
    return value


def fingerprint(value):
    require(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) and value != '0' * 64,
            'Expected a nonzero SHA-256 fingerprint')
    return value


def unique_object(items):
    result = {}
    for key, value in items:
        require(key not in result, 'Duplicate JSON field: ' + key)
        result[key] = value
    return result


def read_json(path):
    with Path(path).open('rb') as stream:
        raw = stream.read(MAX_JSON + 1)
    require(0 < len(raw) <= MAX_JSON, 'Profile exceeds its file size limit')
    return json.loads(raw, object_pairs_hook=unique_object)


def write(path, value):
    """Atomically publish an owned private file; never follow a destination link."""
    path = Path(path)
    require(not path.is_symlink(), 'Refusing a symlink destination')
    data = value.encode() if isinstance(value, str) else value
    fd, temporary = tempfile.mkstemp(prefix='.write-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def write_json(path, value):
    encoded = json.dumps(value, indent=2, allow_nan=False) + '\n'
    require(len(encoded.encode()) <= MAX_JSON, 'Profile exceeds its file size limit')
    write(path, encoded)


def private_root(path):
    path = Path(path).absolute()
    info = path.lstat()
    require(stat.S_ISDIR(info.st_mode) and info.st_uid == os.getuid() and not info.st_mode & 0o077,
            'Profile must be a private directory owned by this user')
    return path.resolve()


@contextlib.contextmanager
def locked(root, message='This window-sharing profile is already running'):
    descriptor = os.open(root / 'session.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    try:
        info = os.fstat(descriptor)
        require(stat.S_ISREG(info.st_mode) and info.st_uid == os.getuid(), 'Invalid profile lock')
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError(message) from None
        yield
    finally:
        os.close(descriptor)


def session_lock_root():
    # A fixed per-user namespace also covers launches from different profiles
    # and terminals with different TMPDIR values. Never reuse another owner's path.
    return Path('/tmp') / ('deskflow-seamless-session-' + str(os.getuid()))


@contextlib.contextmanager
def session_lock():
    root = session_lock_root()
    root.mkdir(mode=0o700, exist_ok=True)
    with locked(private_root(root), 'Another window-sharing session is already running'):
        yield


def executable(path, name):
    path = Path(text(str(path))).resolve(strict=True)
    require(path.name == name and path.is_file() and os.access(path, os.X_OK),
            'Select the packaged ' + name + ' executable')
    return path


def certificate_pin(path):
    with Path(path).open() as stream:
        pem = stream.read(65537)
    require(len(pem) <= 65536, 'Certificate is too large')
    return hashlib.sha256(ssl.PEM_cert_to_DER_cert(pem)).hexdigest()


def public_identity(value):
    require(isinstance(value, dict) and set(value) ==
            {'schema', 'role', 'name', 'coreFingerprint', 'windowFingerprint'}, 'Invalid pairing file fields')
    require(value['schema'] == PAIRING_SCHEMA and value['role'] in ('mac', 'ubuntu'), 'Unsupported pairing file')
    require(isinstance(value['name'], str) and re.fullmatch('[A-Za-z][A-Za-z0-9_-]{0,62}', value['name']),
            'Computer names must start with a letter and contain only letters, numbers, _ or -')
    fingerprint(value['coreFingerprint'])
    fingerprint(value['windowFingerprint'])
    return value


def configuration_root(root, value):
    if value['schema'] == SCHEMA:
        return root / 'config'
    generation = value['configuration']
    require(isinstance(generation, str) and re.fullmatch('[0-9a-f]{32}', generation),
            'Invalid configuration generation')
    parent = private_root(root / 'configurations')
    return private_root(parent / generation)


def validate_binaries(value):
    core = executable(value['core'], 'deskflow-core')
    viewer = executable(value['windowshare'], 'deskflow-windowshare')
    require(core.parent == viewer.parent, 'Core and window-sharing executable must belong to the same package')
    require(value['binaryHashes'] == {'core': sha(core), 'windowshare': sha(viewer)},
            'Packaged binaries changed; explicitly update the saved setup to use this package')
    if value['local']['role'] == 'mac':
        with (core.parent.parent / 'Info.plist').open('rb') as stream:
            info = plistlib.load(stream)
        require(info.get('CFBundleExecutable') == 'deskflow-core' and
                info.get('CFBundleIdentifier') == 'org.deskflow.seamless.core.experimental',
                'Use the separately packaged experimental Core application')


def validate_profile(root, value, check_binaries=True):
    fields = {'schema', 'local', 'core', 'windowshare', 'binaryHashes'}
    require(isinstance(value, dict) and value.get('schema') in (SCHEMA, UPDATED_SCHEMA), 'Unsupported profile')
    if value['schema'] == UPDATED_SCHEMA:
        fields |= {'configuration', 'connection'}
    require(set(value) == fields, 'Invalid profile fields')
    public_identity(value['local'])
    text(value['core'])
    text(value['windowshare'])
    require(isinstance(value['binaryHashes'], dict) and set(value['binaryHashes']) == {'core', 'windowshare'},
            'Invalid saved package hashes')
    for digest in value['binaryHashes'].values():
        fingerprint(digest)
    if check_binaries:
        validate_binaries(value)
    require(value['local']['coreFingerprint'] == certificate_pin(root / 'core-identity/cert.pem') and
            value['local']['windowFingerprint'] == certificate_pin(root / 'window-identity/cert.pem'),
            'Profile identity has changed')
    require((configuration_root(root, value) / 'tls/deskflow.pem').read_bytes() ==
            (root / 'core-identity/key.pem').read_bytes() + (root / 'core-identity/cert.pem').read_bytes(),
            'Core TLS identity has changed')
    for channel in ('core', 'window'):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.load_cert_chain(str(root / (channel + '-identity/cert.pem')),
                                str(root / (channel + '-identity/key.pem')))
    return value


def load(root, check_binaries=True):
    return validate_profile(root, read_json(root / 'profile.json'), check_binaries)


def initialize(directory, role, name, core, windowshare):
    core, windowshare = executable(core, 'deskflow-core'), executable(windowshare, 'deskflow-windowshare')
    require(core.parent == windowshare.parent, 'Use Core and window-sharing executable from the same package')
    public_identity({'schema': PAIRING_SCHEMA, 'role': role, 'name': name,
                     'coreFingerprint': '1' * 64, 'windowFingerprint': '2' * 64})
    root = Path(text(str(directory))).absolute()
    root.mkdir(mode=0o700)  # Exclusive: never replace an existing identity/profile.
    root = private_root(root)
    for directory_name in ('config', 'config/tls', 'runtime', 'logs'):
        (root / directory_name).mkdir(mode=0o700)
    env = clean_environment(root)
    env['QT_QPA_PLATFORM'] = 'offscreen'
    for role_name in ('core', 'window'):
        result = subprocess.run([str(windowshare), '--identity', str(root / (role_name + '-identity')),
                                 '--init-identity'], env=env, capture_output=True, timeout=30)
        write(root / (role_name + '-identity-creation.log'), result.stdout + result.stderr)
        require(result.returncode == 0, 'Identity creation failed; see the retained creation log')
    local = public_identity({'schema': PAIRING_SCHEMA, 'role': role, 'name': name,
                             'coreFingerprint': certificate_pin(root / 'core-identity/cert.pem'),
                             'windowFingerprint': certificate_pin(root / 'window-identity/cert.pem')})
    write(root / 'config/tls/deskflow.pem', (root / 'core-identity/key.pem').read_bytes() +
          (root / 'core-identity/cert.pem').read_bytes())
    profile = {'schema': SCHEMA, 'local': local, 'core': str(core), 'windowshare': str(windowshare),
               'binaryHashes': {'core': sha(core), 'windowshare': sha(windowshare)}}
    write_json(root / 'profile.json', profile)
    load(root)
    write_json(root / 'pairing.json', local)
    return {'profile': str(root), 'pairingFile': str(root / 'pairing.json'), 'inputStarted': False}


def ini(sections):
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    for name, values in sections.items():
        parser[name] = {key: text(str(value)) for key, value in values.items()}
    output = io.StringIO()
    parser.write(output)
    return output.getvalue()


def connection_options(options):
    require(isinstance(options, dict) and set(options) ==
            {'macAddress', 'corePort', 'windowPort', 'screen', 'clipboardText'}, 'Invalid connection fields')
    address = ipaddress.ip_address(options['macAddress'])
    require(not address.is_unspecified and not address.is_multicast and not address.is_loopback,
            'Use the Mac address reachable from Ubuntu')
    for field in ('corePort', 'windowPort'):
        require(type(options[field]) is int and 1024 <= options[field] <= 65535, 'Invalid session port')
    require(options['corePort'] != options['windowPort'], 'Core and window-sharing ports must differ')
    require(type(options['screen']) is int and 0 <= options['screen'] <= 63, 'Invalid Ubuntu monitor index')
    require(type(options['clipboardText']) is bool, 'Clipboard policy must be explicit')
    return {**options, 'macAddress': str(address)}


def configuration(root, profile, peer, options):
    """Deterministic real runtime configuration; no test source or automatic export."""
    local = profile['local']
    public_identity(local)
    public_identity(peer)
    options = connection_options(options)
    directory = configuration_root(root, profile)
    require(local['role'] != peer['role'] and local['name'] != peer['name'], 'Pair one Mac with a differently named Ubuntu')
    mac, ubuntu = (local, peer) if local['role'] == 'mac' else (peer, local)
    settings = {
        'core': {'computerName': local['name'], 'port': options['corePort'], 'preventSleep': 'false',
                 'enableEnterCommand': 'false', 'enableExitCommand': 'false'},
        'security': {'tlsEnabled': 'true', 'checkPeerFingerprints': 'true',
                     'certificate': str(directory / 'tls/deskflow.pem')},
        'log': {'level': 'INFO', 'toFile': 'false'},
    }
    files = {}
    if local['role'] == 'mac':
        settings['core']['interface'] = options['macAddress']
        settings['server'] = {'externalConfig': 'true', 'externalConfigFile': str(directory / 'server.conf'),
                              'enableClipboard': 'false', 'relativeMouseMoves': 'false',
                              'enableHeartbeat': 'true', 'heartbeat': '1000', 'protocol': 'Barrier'}
        for computer in (mac, ubuntu):
            settings['computer_' + computer['name']] = {'name': computer['name'], 'modifierShift': 'shift',
                'modifierCtrl': 'ctrl', 'modifierAlt': 'alt', 'modifierAltGr': 'altgr',
                'modifierMeta': 'meta', 'modifierSuper': 'super'}
        files['server.conf'] = (
            f"section: screens\n    {mac['name']}:\n    {ubuntu['name']}:\nend\n\n"
            f"section: links\n    {mac['name']}:\n        up = {ubuntu['name']}\n"
            f"    {ubuntu['name']}:\n        down = {mac['name']}\nend\n\n"
            'section: options\n'
            f"    keystroke(control+alt+shift+Left) = switchToScreen({mac['name']})\nend\n")
        files['host.json'] = {'version': 1, 'listen': options['macAddress'], 'port': options['windowPort'],
            'identity': str(root / 'window-identity'), 'peerPin': peer['windowFingerprint'],
            'journal': str(root / 'restore.json'), 'holdingDisplay': True, 'headless': True,
            'shareClipboardText': options['clipboardText'],
            'inputPeerPairs': [{'deskflowPeerPin': peer['coreFingerprint'], 'windowSharePeerPin': peer['windowFingerprint']}]}
        files['tls/trusted-clients'] = 'v2:sha256:' + peer['coreFingerprint'] + '\n'
    else:
        settings['client'] = {'remoteHost': options['macAddress'], 'languageSync': 'false'}
        files['provider.json'] = {'version': 1, 'viewerExecutable': profile['windowshare'],
            'connect': options['macAddress'], 'port': options['windowPort'],
            'identity': str(root / 'window-identity'), 'peerPin': peer['windowFingerprint'],
            'screen': options['screen'], 'shareClipboardText': options['clipboardText']}
        files['tls/trusted-servers'] = 'v2:sha256:' + peer['coreFingerprint'] + '\n'
    files['core.ini'] = ini(settings)
    return files


def pair(root, peer_path, options):
    with locked(root):
        profile = load(root)
        peer = public_identity(read_json(peer_path))
        options = connection_options(options)
        require(connection_for(root, profile) is None, 'This profile is already paired; create a new profile to change peers')
        if profile['schema'] == UPDATED_SCHEMA:
            publish_update(root, profile, {'peer': peer, 'options': options})
            return {'paired': True, 'inputStarted': False, 'layout': 'Ubuntu above Mac', 'automaticExport': False}
        files = configuration(root, profile, peer, options)
        for name, value in files.items():
            if isinstance(value, dict):
                write_json(root / 'config' / name, value)
            else:
                write(root / 'config' / name, value)
        write_json(root / 'connection.json', {'peer': peer, 'options': options,
            'files': {name: sha(root / 'config' / name) for name in files}})
    return {'paired': True, 'inputStarted': False, 'layout': 'Ubuntu above Mac', 'automaticExport': False}


def connection_for(root, profile):
    if profile['schema'] == UPDATED_SCHEMA:
        return profile['connection']
    return read_json(root / 'connection.json') if (root / 'connection.json').exists() else None


def validate_connection(root, profile):
    connection = connection_for(root, profile)
    if connection is None:
        return None
    require(isinstance(connection, dict) and set(connection) == {'peer', 'options', 'files'} and
            isinstance(connection['files'], dict), 'Invalid paired connection')
    files = configuration(root, profile, connection['peer'], connection['options'])
    require(set(connection['files']) == set(files), 'Incomplete paired configuration')
    for name, expected in connection['files'].items():
        path = configuration_root(root, profile) / name
        require(sha(path) == expected, 'Session configuration changed: ' + name)
        value = files[name]
        encoded = (json.dumps(value, indent=2, allow_nan=False) + '\n') if isinstance(value, dict) else value
        require(path.read_bytes() == encoded.encode(), 'Session configuration no longer matches the pairing: ' + name)
    return connection


def publish_update(root, profile, connection):
    """Prepare immutable files, then atomically select their complete descriptor.

    The caller holds the profile lock. A failed/interrupted preparation leaves
    the previous descriptor selected; unused generations are retained for diagnosis.
    Identity files and restoration journals are never moved or rewritten.
    """
    parent = root / 'configurations'
    parent.mkdir(mode=0o700, exist_ok=True)
    private_root(parent)
    generation = uuid.uuid4().hex
    directory = parent / generation
    directory.mkdir(mode=0o700)
    (directory / 'tls').mkdir(mode=0o700)
    candidate = {**profile, 'schema': UPDATED_SCHEMA, 'configuration': generation, 'connection': None}
    write(directory / 'tls/deskflow.pem', (root / 'core-identity/key.pem').read_bytes() +
          (root / 'core-identity/cert.pem').read_bytes())
    if connection is not None:
        files = configuration(root, candidate, connection['peer'], connection['options'])
        for name, value in files.items():
            if isinstance(value, dict):
                write_json(directory / name, value)
            else:
                write(directory / name, value)
        candidate['connection'] = {'peer': connection['peer'], 'options': connection_options(connection['options']),
                                  'files': {name: sha(directory / name) for name in files}}
    validate_profile(root, candidate)
    validate_connection(root, candidate)
    write(root / 'profile.previous.json', (root / 'profile.json').read_bytes())
    write_json(root / 'profile.json', candidate)
    return candidate


def update(root, core, windowshare, changes=None):
    with locked(root):
        # Replacing/deleting the old package must not require regenerating keys.
        # Identity and configuration checks still run before approving new bytes.
        previous = load(root, check_binaries=False)
        connection = validate_connection(root, previous)
        core, windowshare = executable(core, 'deskflow-core'), executable(windowshare, 'deskflow-windowshare')
        candidate = {**previous, 'core': str(core), 'windowshare': str(windowshare),
                     'binaryHashes': {'core': sha(core), 'windowshare': sha(windowshare)}}
        validate_binaries(candidate)
        if changes:
            require(connection is not None, 'Pair this setup before changing connection options')
            require(set(changes) <= {'macAddress', 'corePort', 'windowPort', 'screen', 'clipboardText'},
                    'Unknown setup change')
            connection = {**connection, 'options': connection_options({**connection['options'], **changes})}
        candidate = publish_update(root, candidate, connection)
        return {'updated': True, 'inputStarted': False, 'pairingUnchanged': True,
                'configuration': str(configuration_root(root, candidate))}


def launch_plan(root):
    profile = load(root)
    connection = validate_connection(root, profile)
    require(connection is not None, 'Pair this setup before starting sharing')
    directory = configuration_root(root, profile)
    mac = profile['local']['role'] == 'mac'
    args = [profile['core'], 'server' if mac else 'client', '--new-instance', '--settings', str(directory / 'core.ini'),
            ('--windowshare-host-config=' if mac else '--windowshare-gnome-config=') +
            str(directory / ('host.json' if mac else 'provider.json'))]
    return {'role': profile['local']['role'], 'command': args, 'layout': 'Ubuntu above Mac',
            'automaticExport': False, 'journal': str(root / 'restore.json'),
            'log': str(root / 'logs/session.log'), 'peer': connection['peer']['name']}


def inspect_profile(root, core=None, windowshare=None):
    with locked(root):
        value = load(root, check_binaries=False)
        connection = validate_connection(root, value)
        package_error = ''
        try:
            validate_binaries(value)
            if core is not None or windowshare is not None:
                require(core is not None and windowshare is not None, 'Select both packaged executables')
                require(executable(core, 'deskflow-core') == Path(value['core']) and
                        executable(windowshare, 'deskflow-windowshare') == Path(value['windowshare']),
                        'This setup uses another package. Choose Use this version to update it.')
        except (ValueError, OSError) as error:
            package_error = str(error)
        result = {'profile': str(root), 'name': value['local']['name'], 'role': value['local']['role'],
                  'paired': connection is not None, 'pairingFile': str(root / 'pairing.json'),
                  'packageReady': not package_error, 'packageError': package_error,
                  'pairing': value['local']}
        if result['paired']:
            result.update(peer=connection['peer']['name'], layout='Ubuntu above Mac', options=connection['options'])
        return result


def clean_environment(root):
    env = {key: value for key, value in os.environ.items() if not key.startswith('DYLD_') and key not in
           {'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QML_PLUGIN_PATH', 'QML_IMPORT_PATH',
            'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM', 'QT_SCALE_FACTOR', 'QT_AUTO_SCREEN_SCALE_FACTOR'}}
    env.update(TMPDIR=str(root / 'runtime'), XDG_CONFIG_HOME=str(root / 'config-home'),
               XDG_DATA_HOME=str(root / 'data-home'), XDG_CACHE_HOME=str(root / 'cache-home'))
    return env


def running_cores():
    if sys.platform == 'darwin':
        result = subprocess.run(['pgrep', '-x', 'deskflow-core'], capture_output=True, text=True, timeout=5)
        require(result.returncode in (0, 1), 'Cannot check the current Deskflow session')
        return [int(line) for line in result.stdout.splitlines()]
    pids = []
    for path in Path('/proc').iterdir():
        if not path.name.isdecimal():
            continue
        try:
            if path.stat().st_uid == os.getuid() and (path / 'comm').read_text().strip() == 'deskflow-core':
                pids.append(int(path.name))
        except (FileNotFoundError, ProcessLookupError):
            continue
    return pids


class ProcessLog:
    def __init__(self, process, logger):
        self.process, self.logger, self.pending = process, logger, b''
        self.eof = False
        os.set_blocking(process.stdout.fileno(), False)

    def drain(self):
        try:
            data = os.read(self.process.stdout.fileno(), 65536)
        except BlockingIOError:
            return
        if not data:
            self.eof = True
            if self.pending:
                self.logger.info('%s', self.pending.decode(errors='replace'))
                self.pending = b''
            return
        self.pending += data
        while b'\n' in self.pending:
            line, self.pending = self.pending.split(b'\n', 1)
            self.logger.info('%s', line.decode(errors='replace'))
        if len(self.pending) >= 65536:
            self.logger.info('%s', self.pending.decode(errors='replace'))
            self.pending = b''

    def wait(self, seconds):
        deadline = time.monotonic() + seconds
        with selectors.DefaultSelector() as selector:
            if not self.eof:
                selector.register(self.process.stdout, selectors.EVENT_READ)
            while self.process.poll() is None or selector.get_map():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise subprocess.TimeoutExpired(self.process.args, seconds)
                for key, _ in selector.select(timeout=min(0.25, remaining)):
                    self.drain()
                    if self.eof:
                        selector.unregister(key.fileobj)
        return self.process.returncode


def stop_child(process, endpoint, output):
    if process.poll() is not None:
        return {'method': 'already-exited', 'exitCode': process.returncode}
    try:
        info = endpoint.lstat()
        require(stat.S_ISSOCK(info.st_mode) and info.st_uid == os.getuid(), 'Unexpected private Core socket')
        with socket.socket(socket.AF_UNIX) as client:
            client.settimeout(3)
            client.connect(str(endpoint))
            pid = (struct.unpack('i', client.getsockopt(0, 2, 4))[0] if sys.platform == 'darwin' else
                   struct.unpack('3i', client.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))[0])
            require(pid == process.pid and process.poll() is None, 'Private socket is not the owned Core')
            client.sendall(b'stop\n')
        output.wait(15)
        return {'method': 'private-ipc', 'exitCode': process.returncode}
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        output.logger.warning('STOP private IPC failed: %s', error)
        # Only this Popen child is addressed. Host's normal shutdown/recovery
        # remains responsible for restoring any shared source windows.
        if process.poll() is None:
            output.logger.warning('STOP sending SIGTERM to owned Core pid=%s', process.pid)
            process.terminate()
            try:
                output.wait(15)
            except subprocess.TimeoutExpired:
                output.logger.error('STOP killing unresponsive owned Core pid=%s; retain recovery journal', process.pid)
                process.kill()
                output.wait(5)
                raise RuntimeError('Core did not stop; retain the recovery journal') from None
        return {'method': 'signal-fallback', 'exitCode': process.returncode, 'ipcError': str(error)}


def run(root, dry_run=False, control_fd=None):
    with locked(root), contextlib.ExitStack() as session_locks:
        plan = launch_plan(root)
        if dry_run:
            return plan
        if control_fd is not None:
            require(stat.S_ISFIFO(os.fstat(control_fd).st_mode) or stat.S_ISSOCK(os.fstat(control_fd).st_mode),
                    'The controls connection must be an inherited pipe or socket')
        require(sys.platform == ('darwin' if plan['role'] == 'mac' else 'linux'),
                'Use this profile on its designated platform')
        session_locks.enter_context(session_lock())
        require(not running_cores(), 'Stop the regular Deskflow connection before starting window sharing')
        require(len(os.fsencode(root / 'runtime/deskflow-core')) < 104, 'Profile path is too long for the private IPC socket')
        env = clean_environment(root)
        if plan['role'] == 'ubuntu':
            require(env.get('XDG_SESSION_TYPE') == 'wayland' and env.get('DBUS_SESSION_BUS_ADDRESS'),
                    'Launch from the logged-in Ubuntu Wayland desktop with the Deskflow GNOME provider enabled')
            env['QT_QPA_PLATFORM'] = 'wayland'
        logger = logging.getLogger('seamless-session')
        logger.setLevel(logging.INFO)
        handler = logging.handlers.RotatingFileHandler(plan['log'], maxBytes=16 * 1024 * 1024, backupCount=3)
        logger.addHandler(handler)
        stop_requested = False
        def request_stop(_signum, _frame):
            nonlocal stop_requested
            stop_requested = True
        previous = {sig: signal.signal(sig, request_stop) for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)}
        process = None
        output = None
        result = {'startedAt': now(), 'role': plan['role'], 'automaticExport': False}
        try:
            logger.info('SESSION %s', json.dumps(result))
            # Closing the GUI (including a crash) closes its pipe. Do not start
            # native input if the controller went away during setup checks.
            if control_fd is not None and select.select([control_fd], [], [], 0)[0]:
                result.update(cancelled=True, inputStarted=False, exitCode=0, stopReason='controller-closed-before-start')
                return result
            process = subprocess.Popen(plan['command'], env=env, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
            output = ProcessLog(process, logger)
            result['pid'] = process.pid
            write_json(root / 'session.json', result)
            print('Window-sharing session starting. Drag a Mac titlebar through the top edge once both computers connect.', flush=True)
            print('Press Control+C here to stop. Detailed progress is saved in ' + plan['log'], flush=True)
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ, 'output')
                if control_fd is not None:
                    selector.register(control_fd, selectors.EVENT_READ, 'controller')
                while process.poll() is None and not stop_requested:
                    for key, _ in selector.select(timeout=0.5):
                        if key.data == 'controller':
                            # This is a lifetime channel, never a remote command
                            # interpreter. EOF or unexpected data requests Stop.
                            result['stopReason'] = ('controller-closed' if not os.read(control_fd, 1)
                                                    else 'unexpected-controller-data')
                            stop_requested = True
                            break
                        output.drain()
                        if output.eof:
                            selector.unregister(key.fileobj)
            if stop_requested:
                result['stop'] = stop_child(process, root / 'runtime/deskflow-core', output)
            result.update(exitCode=output.wait(5), stopRequested=stop_requested)
        except Exception as error:
            result['error'] = str(error)
            raise
        finally:
            try:
                if process is not None and process.poll() is None:
                    result['stop'] = stop_child(process, root / 'runtime/deskflow-core', output)
            finally:
                for sig, callback in previous.items():
                    signal.signal(sig, callback)
                result.update(stoppedAt=now(), running=False)
                write_json(root / 'session.json', result)
                logger.info('SESSION %s', json.dumps(result))
                logger.removeHandler(handler)
                handler.close()
                if process is not None:
                    process.stdout.close()
        return result


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    create = sub.add_parser('init', help='Create an isolated endpoint and its public pairing file')
    create.add_argument('--profile', type=Path, required=True)
    create.add_argument('--role', choices=['mac', 'ubuntu'], required=True)
    create.add_argument('--name', required=True)
    create.add_argument('--core', type=Path, required=True)
    create.add_argument('--windowshare', type=Path, required=True)
    connect = sub.add_parser('pair', help='Approve the other endpoint and configure top-edge transfer')
    connect.add_argument('--profile', type=Path, required=True)
    connect.add_argument('--peer', type=Path, required=True)
    connect.add_argument('--mac-address', required=True)
    connect.add_argument('--core-port', type=int, default=24870)
    connect.add_argument('--window-port', type=int, default=24871)
    connect.add_argument('--screen', type=int, default=0)
    connect.add_argument('--clipboard-text', action='store_true')
    launch = sub.add_parser('run', help='Start a normal session; Control+C stops and restores source windows')
    launch.add_argument('--profile', type=Path, required=True)
    launch.add_argument('--dry-run', action='store_true', help='Print the launch plan without starting processes')
    launch.add_argument('--control-stdin', action='store_true', help='Stop if the graphical controller closes its input pipe')
    inspect = sub.add_parser('inspect', help='Read saved setup without starting input')
    inspect.add_argument('--profile', type=Path, required=True)
    inspect.add_argument('--core', type=Path)
    inspect.add_argument('--windowshare', type=Path)
    refresh = sub.add_parser('update', help='Use a new package or connection options without changing pairing or starting input')
    refresh.add_argument('--profile', type=Path, required=True)
    refresh.add_argument('--core', type=Path, required=True)
    refresh.add_argument('--windowshare', type=Path, required=True)
    refresh.add_argument('--mac-address')
    refresh.add_argument('--core-port', type=int)
    refresh.add_argument('--window-port', type=int)
    refresh.add_argument('--screen', type=int)
    refresh.add_argument('--clipboard-text', action=argparse.BooleanOptionalAction, default=None)
    args = parser.parse_args()
    if args.command == 'init':
        result = initialize(args.profile, args.role, args.name, args.core, args.windowshare)
    elif args.command == 'pair':
        result = pair(private_root(args.profile), args.peer, {'macAddress': args.mac_address,
            'corePort': args.core_port, 'windowPort': args.window_port, 'screen': args.screen,
            'clipboardText': args.clipboard_text})
    elif args.command == 'inspect':
        result = inspect_profile(private_root(args.profile), args.core, args.windowshare)
    elif args.command == 'update':
        changes = {key: getattr(args, field) for key, field in
                   [('macAddress', 'mac_address'), ('corePort', 'core_port'), ('windowPort', 'window_port'),
                    ('screen', 'screen'), ('clipboardText', 'clipboard_text')] if getattr(args, field) is not None}
        result = update(private_root(args.profile), args.core, args.windowshare, changes)
    else:
        result = run(private_root(args.profile), args.dry_run, sys.stdin.fileno() if args.control_stdin else None)
    print(json.dumps(result, indent=2))
    return int(result.get('exitCode', 0) != 0)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        print('Window sharing: ' + str(error), file=sys.stderr)
        raise SystemExit(1)
