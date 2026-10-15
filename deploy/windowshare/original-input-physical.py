#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Explicit production original-input physical session; never an automatic PASS.

Reuses the physical coordinator's visible Start/Stop and exact remote cleanup.
Only packaged Core launches Host, using its genuine inherited private channel.
The default physical-macos.py legacy mode is unchanged. --validate-only reads
plans and frozen artifacts without launching an application, SSH or input hooks.
"""

import argparse
import ctypes
import errno
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import re
import secrets
import socket
import ssl
import stat
import struct
import sys
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def certificate_pin(path):
    text = Path(path).read_text()
    require(len(text) <= 65536, 'TLS identity exceeds its private file bound')
    match = re.search(r'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----', text, re.S)
    require(match is not None, 'Prepared TLS identity has no certificate')
    return hashlib.sha256(ssl.PEM_cert_to_DER_cert(match[0])).hexdigest()


def package_paths(core, host, source):
    core, host = Path(core).resolve(strict=True), Path(host).resolve(strict=True)
    bundle = core.parent.parent.parent
    require(core.name == 'deskflow-core' and host.name == 'deskflow-windowshare' and core.parent == host.parent and
            core.parent.name == 'MacOS' and core.parent.parent.name == 'Contents' and bundle.suffix == '.app',
            'Core and Host must be the fixed roles of one experimental app bundle')
    return bundle, {'core': core, 'host': host, 'source': Path(source).resolve(strict=True),
                    'holdingHelper': core.with_name('deskflow-holding-display-helper'),
                    'infoPlist': bundle / 'Contents/Info.plist',
                    'codeResources': bundle / 'Contents/_CodeSignature/CodeResources',
                    'libraryManifest': bundle / 'Contents/Resources/deskflow-exact-libraries.json'}


def validate(mac, linux, base, private_path, source=None, holding=False):
    require(mac.get('version') == 2 and mac.get('preparedOnly') is True and
            mac.get('originalInputEnabled') is True and mac.get('holdingDisplayEnabled') is holding and
            mac.get('separateControls', False) is holding,
            'Production input and holding require matching explicit plans and separate visible controls')
    require(linux.get('version') == linux.get('controlProtocol') == 3 and
            linux.get('routeMode') == 'production-original-input' and linux.get('macOriginalInputConfigured') is True,
            'Production input requires a freshly prepared v3 Linux plan')
    require(isinstance(mac.get('hostArgv'), list) and len(mac['hostArgv']) == 3 and
            mac['hostArgv'][1] == '--identity', 'Production Host is launched only by packaged Core')
    require(all(Path(value).is_absolute() for value in
                (mac['coreArgv'][0], mac['hostArgv'][0], source or mac['sourceExecutable'])),
            'Production roles need explicit absolute executable paths')
    bundle, paths = package_paths(mac['coreArgv'][0], mac['hostArgv'][0], source or mac['sourceExecutable'])
    expected = mac.get('frozenArtifacts')
    require(isinstance(expected, dict) and set(expected) == set(paths) and
            all(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) for value in expected.values()),
            'Production plan needs the exact frozen role and sealed-manifest hashes')
    for name, path in paths.items():
        require(path.is_file() and (name == 'source' or path.resolve().is_relative_to(bundle)),
                'Frozen package role escaped or is missing: ' + name)
        require(sha(path) == expected[name], 'Frozen production artifact changed: ' + name)
    with paths['infoPlist'].open('rb') as stream:
        info = plistlib.load(stream)
    require(info.get('CFBundleExecutable') == 'deskflow-core' and
            info.get('CFBundleIdentifier') == 'org.deskflow.seamless.core.experimental',
            'Production input requires the separately packaged Core app as its sealed main executable')
    identity = private_path(mac['hostArgv'][2], base)
    require(certificate_pin(identity / 'cert.pem') == mac['hostFingerprint'], 'Host certificate differs from the plan')
    require(linux['corePin'] != '0' * 64 and linux['viewerPin'] != '0' * 64,
            'Production peer pair cannot contain an empty pin')
    return {name: {'path': str(path), 'sha256': expected[name]} for name, path in paths.items()}


def host_configuration(mac, linux, journal, evidence=None):
    value = {'version': 1, 'listen': '127.0.0.1', 'port': mac['hostPort'], 'identity': mac['hostArgv'][2],
             'peerPin': linux['viewerPin'], 'journal': str(journal),
             'holdingDisplay': mac['holdingDisplayEnabled'], 'headless': True, 'shareClipboardText': False,
             'inputPeerPairs': [{'deskflowPeerPin': linux['corePin'], 'windowSharePeerPin': linux['viewerPin']}]}
    if evidence:
        value['inputEvidence'] = {role: str(evidence.configs[role]) for role in ('core', 'host')}
    return value


def core_arguments(mac, configuration):
    return [*mac['coreArgv'], '--windowshare-host-config=' + str(configuration)]


def source_identity(receipt, initial=None, holding=False):
    """Observe owned window identities; this never grants native input authority."""
    source = receipt.get('sourceWindowId')
    require(type(source) is int and 0 < source < 2**32 and source == receipt.get('windowId'),
            'The exact disposable source window was not identified')
    if initial:
        require(source == initial['sourceWindowId'], 'Disposable source window identity changed')
    if holding:
        control = receipt.get('controlWindowId')
        frame = receipt.get('controlFrame')
        require(type(control) is int and 0 < control < 2**32 and control != source and
                receipt.get('controlVisible') is True and isinstance(frame, dict) and frame and
                frame == receipt.get('controlOriginalFrame'),
                'Holding requires distinct, visible, stationary local controls')
        if initial:
            require(control == initial['controlWindowId'] and frame == initial['controlFrame'],
                    'The local control window moved or changed identity')
    else:
        require(receipt.get('controlWindowId') == 0, 'H04 requires the default one-window source')


def unsigned(value):
    require(isinstance(value, str) and re.fullmatch('0|[1-9][0-9]{0,19}', value) and int(value) < 2**64,
            'Evidence requires a canonical uint64 string')
    return int(value)


def evidence_owner(value, role):
    owner, identity = value.get('owner'), value.get('identity')
    require(isinstance(owner, list) and len(owner) == 11 and
            isinstance(identity, str) and re.fullmatch('[0-9a-f]{192}', identity), 'Invalid evidence owner')
    numbers = [unsigned(item) for item in owner]
    require(all(numbers) and identity != '0' * 192 if role != 'native' else
            numbers[0] > 0 and numbers[1] > 0 and not any(numbers[2:]), 'Empty or unexpected evidence owner')
    return (tuple(owner), identity)


class EvidenceReader:
    """Bounded append-only observer. Payload interpretation belongs to the normalizer."""

    def __init__(self, path, role, run_id, now):
        self.path, self.role, self.run_id, self.now = path, role, run_id, now
        self.identity = self.header = self.readiness = self.arm = self.end = None
        self.offset = self.rows = self.readiness_count = 0
        self.buffer = b''

    def poll(self):
        try:
            descriptor = os.open(self.path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except FileNotFoundError:
            require(self.identity is None, 'Evidence output disappeared')
            return
        with os.fdopen(descriptor, 'rb') as stream:
            info = os.fstat(stream.fileno())
            identity = info.st_dev, info.st_ino
            require(stat.S_ISREG(info.st_mode) and info.st_uid == os.getuid() and info.st_nlink == 1 and
                    not info.st_mode & 0o077 and self.offset <= info.st_size <= 512 * 1024 * 1024,
                    'Evidence output changed or exceeded its bound')
            require(self.identity in (None, identity), 'Evidence output inode changed')
            self.identity = identity
            stream.seek(self.offset)
            block = stream.read(256 * 1024)
            self.offset += len(block)
        self.buffer += block
        lines = self.buffer.split(b'\n')
        self.buffer = lines.pop()
        require(len(self.buffer) <= 32768, 'Evidence line exceeded its bound')
        for line in lines:
            require(0 < len(line) <= 32768 and self.rows < 2000000 and self.end is None,
                    'Evidence rows exceeded their bound or followed an end row')
            def unique(pairs):
                value = {}
                for key, item in pairs:
                    require(key not in value, 'Duplicate evidence JSON field')
                    value[key] = item
                return value
            value = json.loads(line, object_pairs_hook=unique,
                               parse_constant=lambda _: require(False, 'Nonfinite evidence JSON'))
            require(isinstance(value, dict), 'Evidence row must be an object')
            self.rows += 1
            kind = value.get('type')
            if self.header is None:
                require(kind == 'header' and value.get('schema') in ('deskflow-input-evidence-v1', 'deskflow-input-evidence-v2') and
                        value.get('role') == self.role and value.get('runId') == self.run_id and
                        unsigned(value.get('pid')) > 0 and isinstance(value.get('clockId'), str) and
                        isinstance(value.get('queues'), list), 'Wrong evidence header or process')
                self.header = value
                continue
            require(kind in ('readiness', 'arm', 'end', 'source', 'native', 'queue', 'hover'),
                    'Unexpected evidence row type')
            unsigned(value.get('timeNs'))
            if kind == 'readiness':
                require(self.arm is None and type(value.get('readyCandidate')) is bool and
                        type(value.get('loss')) is int and value['loss'] == 0,
                        'Evidence readiness was denied or arrived after arm')
                owner = evidence_owner(value, self.role) if value['readyCandidate'] else None
                self.readiness_count += 1
                self.readiness = (self.readiness_count, self.now(), owner)
            elif kind == 'arm':
                require(self.arm is None and type(value.get('accepted')) is bool, 'Repeated or invalid evidence arm')
                self.arm = value
            elif kind == 'end':
                self.end = value


class EvidenceSession:
    """One attempted arm, no clock comparison between processes, no acceptance claim."""

    roles = ('core', 'host', 'native')
    duration_ms = 30000

    def __init__(self, directory, atomic_json, now=time.monotonic):
        self.directory, self.atomic_json, self.now = directory, atomic_json, now
        directory.mkdir(mode=0o700, exist_ok=False)
        self.run_id = 'physical-' + secrets.token_hex(12)
        self.record_request = directory / 'record-request.json'
        self.configs, self.controls, self.readers = {}, {}, {}
        for role in self.roles:
            self.configs[role] = directory / (role + '.json')
            self.controls[role] = directory / (role + '-control.json')
            path = directory / (role + '.jsonl')
            atomic_json(self.configs[role], {'version': 1, 'runId': self.run_id, 'role': role,
                                           'durationMs': self.duration_ms, 'output': str(path),
                                           'control': str(self.controls[role])})
            self.readers[role] = EvidenceReader(path, role, self.run_id, now)
        self.pids, self.source_window = {}, None
        self.baseline = self.candidate = None
        self.record_identity = self.record_boundary = None
        self.attempted = self.armed = self.stopped = False

    def bind(self, role, pid, source_window=None):
        require(role in self.roles and type(pid) is int and pid > 0 and role not in self.pids,
                'Evidence process identity may be bound only once')
        self.pids[role] = str(pid)
        if role == 'native':
            require(type(source_window) is int and source_window > 0, 'Evidence requires the exact source window')
            self.source_window = str(source_window)

    def poll(self):
        for role, reader in self.readers.items():
            reader.poll()
            if reader.header and role in self.pids:
                require(reader.header['pid'] == self.pids[role], 'Evidence process differs from the owned process')
            if reader.arm:
                require(self.attempted and reader.arm['accepted'] is True, 'Evidence arm was denied or unsolicited')
                owner = evidence_owner(reader.arm, role)
                require(owner == self.candidate[role], 'Evidence owner changed between readiness and atomic arm')
            if reader.end and not self.stopped:
                # The independently configured duration is an absolute bound
                # even if a status request stalls this Python coordinator.
                # Compare only this recorder's own interval, never clocks.
                require(self.armed and reader.arm and
                        unsigned(reader.end.get('sourceEndNs')) == unsigned(reader.arm['timeNs']) + self.duration_ms * 1000000,
                        'Evidence recorder ended before its declared interval')
        if self.attempted and all(reader.arm for reader in self.readers.values()):
            self.armed = True

    def start_boundary(self):
        require(self.baseline is None and not self.attempted, 'Evidence Start boundary may be recorded only once')
        self.poll()
        self.baseline = {role: reader.readiness_count for role, reader in self.readers.items()}

    def try_arm(self):
        self.poll()
        require(self.baseline is not None and not self.stopped, 'Evidence requires a visible Start')
        if self.attempted or len(self.pids) != len(self.roles):
            return False
        try:
            descriptor = os.open(self.record_request, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except FileNotFoundError:
            require(self.record_identity is None, 'Visible Record request disappeared')
            return False
        with os.fdopen(descriptor, 'rb') as stream:
            before = os.fstat(stream.fileno())
            require(stat.S_ISREG(before.st_mode) and before.st_uid == os.getuid() and before.st_nlink == 1 and
                    not before.st_mode & 0o077 and 0 < before.st_size <= 512, 'Record request is not private and bounded')
            raw = stream.read(513)
            after = os.fstat(stream.fileno())
        identity = lambda info: (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)
        require(identity(before) == identity(after) and len(raw) == before.st_size,
                'Record request changed while reading')
        # The source creates this exact one-shot object with O_EXCL. Duplicate
        # members cannot be silently accepted as a second interpretation.
        pairs = json.loads(raw, object_pairs_hook=lambda values: values)
        require(isinstance(pairs, list) and len(pairs) == 3 and len(dict(pairs)) == 3 and
                dict(pairs) == {'version': 1, 'runId': self.run_id, 'command': 'record'} and
                type(dict(pairs).get('version')) is int, 'Wrong visible Record request')
        if self.record_identity is None:
            self.record_identity = identity(after)
            self.record_boundary = {role: reader.readiness_count for role, reader in self.readers.items()}
            return False
        require(self.record_identity == identity(after), 'Visible Record request was replaced')
        candidates = {}
        for role, reader in self.readers.items():
            value = reader.readiness
            if not reader.header or not value or value[0] <= self.record_boundary[role] or \
                    self.now() - value[1] > 0.5 or not value[2]:
                return False
            candidates[role] = value[2]
        if candidates['core'] != candidates['host']:
            return False
        require(candidates['native'][0][:2] == (self.pids['native'], self.source_window),
                'Native evidence names a different source')
        self.candidate, self.attempted = candidates, True
        for role in self.roles:
            self.atomic_json(self.controls[role], {'version': 1, 'runId': self.run_id, 'command': 'arm'})
        return True

    def stop(self):
        if self.stopped:
            return
        self.stopped = True
        errors = []
        for role in self.roles:
            try:
                self.atomic_json(self.controls[role], {'version': 1, 'runId': self.run_id, 'command': 'stop'})
            except Exception as error:
                errors.append(role + ': ' + str(error))
        require(not errors, 'Could not stop every evidence recorder: ' + '; '.join(errors))

    def finish(self):
        require(self.stopped, 'Evidence requires explicit Stop')
        for reader in self.readers.values():
            deadline = self.now() + 3
            while reader.path.exists() and reader.offset < reader.path.stat().st_size and self.now() < deadline:
                self.poll()
            require(not reader.buffer, 'Incomplete final evidence row')
        self.poll()
        require(self.armed and all(reader.arm and reader.end and reader.end.get('state') == 3 and
                                  type(reader.end.get('loss')) is int and reader.end['loss'] == 0 and
                                  reader.end.get('drained') is True and
                                  unsigned(reader.end.get('sourceEndNs')) ==
                                  unsigned(reader.arm['timeNs']) + self.duration_ms * 1000000
                                  for reader in self.readers.values()),
                'Evidence was not fully armed, cleanly stopped and drained in every process')
        return {'runId': self.run_id, 'recordersArmed': True, 'recordersEnded': True,
                'physicalAcceptance': 'not-assessed', 'nativeFieldPreservation': 'not-assessed',
                'observerProvenance': 'not-assessed',
                'logs': {role: str(reader.path) for role, reader in self.readers.items()}}


def stop_core(process, endpoint, inspector, expected, timeout=12):
    """Stop only the retained Mac child through its kernel-identified Qt IPC peer."""
    require(process.poll() is None and inspector.live(expected), 'Owned Core exited before requested IPC stop')
    info = endpoint.lstat()
    require(stat.S_ISSOCK(info.st_mode) and info.st_uid == os.getuid(), 'Core IPC socket is not privately owned')
    deadline = time.monotonic() + timeout
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(timeout)
        connection.connect(str(endpoint))
        # Apple sys/un.h: SOL_LOCAL=0, LOCAL_PEERPID=2, pid_t is int32.
        peer = struct.unpack('i', connection.getsockopt(0, 2, 4))[0]
        require(peer == process.pid and inspector.live(expected) and process.poll() is None,
                'Core IPC peer is not the retained owned process')
        connection.sendall(b'stop\n')
        reply = b''
        while b'ok' not in reply.splitlines():
            remaining = deadline - time.monotonic()
            require(remaining > 0 and len(reply) < 1024, 'Core did not acknowledge bounded IPC stop')
            connection.settimeout(remaining)
            block = connection.recv(1024 - len(reply))
            require(block, 'Core closed before acknowledging IPC stop')
            reply += block
    require(process.wait(timeout=max(0.001, deadline - time.monotonic())) == 0,
            'Owned Core did not exit cleanly after IPC stop')


def freeze_plan(arguments, coordinator):
    """Freeze an independently prepared private plan. No identity/process is created."""
    os.umask(0o077)
    path = arguments.mac_plan.resolve(strict=True)
    output = arguments.output.absolute()
    require(output.parent.resolve(strict=True) == path.parent and not output.exists(),
            'The new production plan must be in the same private prepared directory')
    mac = coordinator.read_json(path)
    require(type(mac.get('version')) is int and mac['version'] == 1 and mac.get('preparedOnly') is True and
            mac.get('originalInputEnabled') is False and mac.get('holdingDisplayEnabled') is False,
            'Freeze requires a separately prepared legacy v1 plan')
    require(len(mac.get('coreArgv', [])) == 5 and mac['coreArgv'][1:4] == ['server', '--new-instance', '--settings'] and
            len(mac.get('hostArgv', [])) == 3 and mac['hostArgv'][1] == '--identity', 'Unexpected prepared role arguments')
    bundle = arguments.bundle.resolve(strict=True)
    _, paths = package_paths(bundle / 'Contents/MacOS/deskflow-core',
                             bundle / 'Contents/MacOS/deskflow-windowshare', arguments.source)
    mac.update(version=2, originalInputEnabled=True, holdingDisplayEnabled=arguments.holding_display,
               separateControls=arguments.holding_display, sourceExecutable=str(paths['source']),
               frozenArtifacts={name: sha(role) for name, role in paths.items()})
    mac['coreArgv'][0], mac['hostArgv'][0] = str(paths['core']), str(paths['host'])
    # A future Linux plan repeats these public pins. Full configuration and
    # counterpart checks run again in --validate-only and after visible Start.
    require(certificate_pin(Path(mac['hostArgv'][2]) / 'cert.pem') == mac['hostFingerprint'],
            'Prepared Host certificate differs from the plan')
    with output.open('x') as stream:
        json.dump(mac, stream, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps({'status': 'frozen-plan-native-not-run', 'plan': str(output),
                      'physicalAcceptance': 'not-assessed', 'frozenArtifacts': mac['frozenArtifacts']}))
    return 0


class ProcessInspector:
    """Read exact kernel birth identities; never signal a discovered process.

    Layout and constants follow the Apple SDK sys/proc_info.h proc_bsdinfo
    and libproc.h. These checks supplement the production peer verifier;
    they never create private-channel or code-admission authority.
    """

    class BsdInfo(ctypes.Structure):
        _fields_ = [(name, ctypes.c_uint32) for name in (
            'flags', 'status', 'xstatus', 'pid', 'ppid', 'uid', 'gid', 'ruid', 'rgid', 'svuid', 'svgid', 'reserved')]
        _fields_ += [('comm', ctypes.c_char * 16), ('name', ctypes.c_char * 32)]
        _fields_ += [(name, ctypes.c_uint32) for name in ('nfiles', 'pgid', 'pjobc', 'tdev', 'tpgid')]
        _fields_ += [('nice', ctypes.c_int32), ('start_sec', ctypes.c_uint64), ('start_usec', ctypes.c_uint64)]

    def __init__(self):
        require(sys.platform == 'darwin' and ctypes.sizeof(self.BsdInfo) == 136, 'Supported Mac process ABI required')
        self.library = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
        self.library.proc_pidinfo.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int]
        self.library.proc_pidinfo.restype = ctypes.c_int
        self.library.proc_pidpath.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
        self.library.proc_pidpath.restype = ctypes.c_int
        self.library.proc_listpids.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_int]
        self.library.proc_listpids.restype = ctypes.c_int

    def info(self, pid):
        info = self.BsdInfo()
        ctypes.set_errno(0)
        count = self.library.proc_pidinfo(pid, 3, 0, ctypes.byref(info), ctypes.sizeof(info))
        if count == 0 and ctypes.get_errno() == errno.ESRCH:
            return None
        require(count == ctypes.sizeof(info) and info.pid == pid and info.uid == os.getuid() and
                info.start_sec > 0, 'Cannot identify the exact owned Mac process')
        return info

    def snapshot(self, pid):
        before = self.info(pid)
        if before is None or before.status == 5:  # SZOMB has stopped executing.
            return None
        path = ctypes.create_string_buffer(4096)
        require(self.library.proc_pidpath(pid, path, len(path)) > 0, 'Cannot read owned process executable')
        after = self.info(pid)
        require(after is not None and (before.pid, before.ppid, before.start_sec, before.start_usec) ==
                (after.pid, after.ppid, after.start_sec, after.start_usec), 'Process changed during its identity read')
        return {'pid': pid, 'parent': before.ppid, 'birthMicroseconds': before.start_sec * 10**6 + before.start_usec,
                'executable': str(Path(os.fsdecode(path.value)).resolve(strict=True))}

    def child(self, parent, executable):
        require(self.snapshot(parent['pid']) == parent, 'Core process identity changed before Host discovery')
        pids = (ctypes.c_int * 256)()
        count = self.library.proc_listpids(6, parent['pid'], pids, ctypes.sizeof(pids))  # PROC_PPID_ONLY
        require(0 <= count < ctypes.sizeof(pids) and count % ctypes.sizeof(ctypes.c_int) == 0,
                'Owned child inventory exceeded its bound')
        children = [self.snapshot(pid) for pid in pids[:count // ctypes.sizeof(ctypes.c_int)] if pid > 0]
        matching = [value for value in children if value and value['parent'] == parent['pid'] and
                    value['executable'] == str(Path(executable).resolve(strict=True))]
        require(len(matching) <= 1, 'Packaged Core has multiple matching Host children')
        require(self.snapshot(parent['pid']) == parent, 'Core changed during Host discovery')
        return matching[0] if matching else None

    def live(self, expected):
        return self.snapshot(expected['pid']) == expected


if __name__ == '__main__':
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location('original_physical_coordinator', Path(__file__).with_name('physical-macos.py'))
    coordinator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(coordinator)
    if sys.argv[1:2] == ['freeze']:
        parser = argparse.ArgumentParser(description='Freeze a private Mac preparation for explicit production testing; no native execution')
        parser.add_argument('--mac-plan', type=Path, required=True)
        parser.add_argument('--bundle', type=Path, required=True)
        parser.add_argument('--source', type=Path, required=True)
        parser.add_argument('--holding-display', action='store_true')
        parser.add_argument('--output', type=Path, required=True)
        raise SystemExit(freeze_plan(parser.parse_args(sys.argv[2:]), coordinator))
    if '--production-original-input' not in sys.argv:
        sys.argv.append('--production-original-input')
    raise SystemExit(coordinator.main())
