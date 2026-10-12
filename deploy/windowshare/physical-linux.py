#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Disposable legacy Deskflow -> provider-owned EIS -> real viewer session.

No test EI sender, synthetic input command, original-input controller or
user desktop is used. Receipt verification belongs to the Mac coordinator.
Only the actual core opens the caller-bound provider session.
"""
import argparse
import contextlib
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import secrets
import selectors
import signal
import socket
import ssl
import stat
import subprocess
import sys
import tempfile
import time
import traceback

sys.dont_write_bytecode = True
SCREEN = 'gnome-physical-test'
HERE = Path(__file__).resolve().parent
PREPARED_SECONDS, STARTUP_SECONDS, RUNNING_SECONDS, RESOURCE_SECONDS = 600, 80, 180, 270


def require(value, message):
    if not value:
        raise RuntimeError(message)


def emit(value):
    encoded = json.dumps(value, separators=(',', ':'), allow_nan=False)
    require(len(encoded) <= 32768, 'Coordinator response exceeded its bound')
    print(encoded, flush=True)


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def pin(path):
    return hashlib.sha256(ssl.PEM_cert_to_DER_cert(path.read_text())).hexdigest()


def canonical_pin(value):
    require(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) and value != '0' * 64,
            'Expected a canonical public SHA256 pin')
    return value


def read_log(path):
    if not path.exists():
        return ''
    require(path.stat().st_size <= 16 * 1024 * 1024, 'Owned log exceeded its bound')
    return path.read_text(errors='replace')


def init(args):
    require(sys.platform == 'linux' and os.getuid() != 0 and Path('/.dockerenv').is_file(), 'Disposable Linux container required')
    os.umask(0o077)
    core = args.run / 'core'
    core.mkdir(exist_ok=False)
    tls = core / 'tls'
    tls.mkdir()
    config = tls / 'certificate.cnf'
    config.write_text('[req]\ndistinguished_name=dn\nprompt=no\nx509_extensions=ext\n'
                      '[dn]\nCN=Deskflow-physical-gnome-test\n[ext]\nbasicConstraints=critical,CA:false\n'
                      'keyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth,clientAuth\n')
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-sha256', '-nodes', '-days', '1',
                    '-config', str(config), '-keyout', str(tls / 'key.pem'), '-out', str(tls / 'cert.pem')],
                   check=True, capture_output=True, timeout=20)
    (tls / 'deskflow.pem').write_bytes((tls / 'cert.pem').read_bytes() + (tls / 'key.pem').read_bytes())
    (tls / 'key.pem').unlink()
    identity = args.run / 'viewer-identity'
    environment = {'PATH': '/usr/bin:/bin', 'HOME': '/tmp', 'LANG': 'C.UTF-8', 'QT_QPA_PLATFORM': 'offscreen'}
    result = subprocess.run([str(args.viewer), '--identity', str(identity), '--init-identity'],
                            env=environment, check=True, capture_output=True, text=True, timeout=20)
    viewer_pin = pin(identity / 'cert.pem')
    require(result.stdout.strip().lower().replace(':', '') == viewer_pin, 'Viewer identity mismatch')
    public = {'schema': 1, 'linuxScreen': SCREEN, 'corePin': pin(tls / 'cert.pem'), 'viewerPin': viewer_pin}
    (args.run / 'public.json').write_text(json.dumps(public, indent=2) + '\n')
    emit({'event': 'identities', **public})
    return 0


class Controls:
    def __init__(self, descriptor=None):
        self.descriptor = sys.stdin.fileno() if descriptor is None else descriptor
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.descriptor, selectors.EVENT_READ)
        self.buffer, self.last, self.count = b'', 0, 0

    def close(self):
        self.selector.close()

    def next(self):
        if b'\n' not in self.buffer:
            if not self.selector.select(0.05):
                return None
            data = os.read(self.descriptor, 4096)
            require(data, 'Coordinator control ended before stop')
            self.buffer += data
            require(len(self.buffer) <= 4096, 'Control input exceeded its bound')
            if b'\n' not in self.buffer:
                return None
        line, self.buffer = self.buffer.split(b'\n', 1)
        def unique(pairs):
            result = {}
            for key, value in pairs:
                require(key not in result, 'Duplicate control field')
                result[key] = value
            return result
        value = json.loads(line, object_pairs_hook=unique)
        require(isinstance(value, dict) and set(value) == {'id', 'op'}, 'Exact id/op command required')
        require(type(value['id']) is int and self.last < value['id'] <= 2**53 - 1, 'Nonmonotonic command id')
        require(value['op'] in ('start', 'status', 'baseline', 'stop'), 'No input injection operations are supported')
        self.last = value['id']
        self.count += 1
        require(self.count <= 256, 'Command count exceeded bound')
        return value


class SessionClock:
    """A one-shot Start gate; status cannot extend any absolute deadline."""

    def __init__(self, now=time.monotonic):
        self.now = now
        self.phase = 'prepared'
        self.deadline = now() + PREPARED_SECONDS
        self.resource_deadline = None
        self.baseline = False

    def check(self):
        require(self.now() < self.deadline, f'{self.phase} deadline expired')

    def start(self):
        self.check()
        require(self.phase == 'prepared', 'Start is allowed exactly once')
        self.phase = 'starting'
        self.deadline = self.now() + STARTUP_SECONDS
        self.resource_deadline = self.now() + RESOURCE_SECONDS

    def ready(self):
        self.check()
        require(self.phase == 'starting', 'Ready requires accepted Start')
        self.phase = 'running'
        self.deadline = self.now() + RUNNING_SECONDS
        require(self.deadline + 10 <= self.resource_deadline, 'Insufficient bounded cleanup time')

    def mark_baseline(self):
        self.check()
        require(self.phase == 'running' and not self.baseline, 'Baseline requires running and is allowed once')
        self.baseline = True

    def state(self):
        self.check()
        return {'phase': self.phase, 'maximumRemainingSeconds': max(0, math.ceil(self.deadline - self.now()))}


class StartupStopped(Exception):
    pass


def session(args):
    require(sys.platform == 'linux' and os.getuid() != 0 and Path('/.dockerenv').is_file(), 'Disposable Linux container required')
    os.umask(0o077)
    canonical_pin(args.mac_core_pin)
    canonical_pin(args.mac_viewer_pin)
    require(1 <= args.window_id <= 2**32 - 1, 'Explicit Mac source window ID required')
    cross = module('physical_cross', HERE / 'gnome-crossmachine-client.py')
    common, qt = cross.common, cross.qt
    x11 = module('physical_stop', HERE / 'x11-core-smoke.py')
    output = args.run / 'results'
    output.mkdir(exist_ok=True)
    require(not (output / 'container-result.json').exists(), 'Container session was already used')
    report = {'schema': 1, 'status': 'failed', 'scope': 'legacy physical input through actual core and provider-owned viewer',
              'originalInputAuthorityGranted': False, 'syntheticMeasuredInput': False, 'syntheticStartupInput': False,
              'linuxScreen': SCREEN, 'windowId': args.window_id, 'commands': [], 'physicalReceipts': 'external-Mac-required'}
    public = json.loads((args.run / 'public.json').read_text())
    require(cross.fingerprint(args.run / 'viewer-identity') == canonical_pin(public['viewerPin'])
            and pin(args.run / 'core/tls/cert.pem') == canonical_pin(public['corePin']),
            'Prepared identity differs from the fixed public pins')
    report['publicPins'] = {**public, 'macCorePin': args.mac_core_pin, 'macViewerPin': args.mac_viewer_pin}
    core = None
    processes = None
    relays = []
    stopped = False
    stop_id = None
    clock = SessionClock()
    cleanup_errors = []
    def safely_close(label, action):
        try:
            action()
        except BaseException as error:
            cleanup_errors.append(f'{label}: {error}')
    try:
        with contextlib.ExitStack() as cleanup:
            controls = Controls()
            cleanup.callback(controls.close)
            emit({'event': 'prepared', 'controlProtocol': 2, 'windowId': args.window_id,
                  'publicPins': report['publicPins'], 'syntheticStartupInput': False,
                  'nativeProcessesStarted': False, **clock.state()})
            start_id = None
            while start_id is None:
                clock.check()
                command = controls.next()
                if command is None:
                    continue
                report['commands'].append(command)
                if command['op'] == 'stop':
                    stop_id = command['id']
                    break
                if command['op'] == 'start':
                    clock.start()
                    start_id = command['id']
                else:
                    require(command['op'] == 'status', 'Baseline requires a running session')
                    emit({'event': 'status', 'id': command['id'], 'nativeProcessesStarted': False, **clock.state()})
            if start_id is None:
                report['status'] = 'completed-without-start'
                report['ownedProcessesStopped'] = True
                return finish_session(output, report, stop_id, core, processes, relays, stopped, cleanup_errors)
            root = Path(cleanup.enter_context(tempfile.TemporaryDirectory(prefix='df-physical-', dir='/tmp')))
            environment = common.private_environment(root)
            class PhaseProcesses(common.Processes):
                def check(self):
                    nonlocal stop_id
                    clock.check()
                    if clock.phase == 'starting':
                        command = controls.next()
                        if command is not None:
                            report['commands'].append(command)
                            if command['op'] == 'stop':
                                stop_id = command['id']
                                raise StartupStopped()
                            require(command['op'] == 'status', 'Only status/stop are allowed during startup')
                            emit({'event': 'status', 'id': command['id'], 'nativeProcessesStarted': True, **clock.state()})
                    super().check()
            processes = PhaseProcesses(output, environment, lifetime_seconds=RESOURCE_SECONDS)
            cleanup.callback(safely_close, 'processes', processes.close)
            for endpoint in (args.core_socket, args.viewer_socket):
                relay = cross.UnixRelay(endpoint, lifetime_seconds=RESOURCE_SECONDS)
                relays.append(relay)
                cleanup.callback(safely_close, 'relay', relay.close)
            shell = qt.start_shell(processes, root, report)
            configuration = args.run / 'provider.json'
            configuration.write_text(json.dumps({'version': 1, 'viewerExecutable': str(args.viewer),
                                                 'connect': '127.0.0.1', 'port': relays[1].port,
                                                 'identity': str(args.run / 'viewer-identity'), 'peerPin': args.mac_viewer_pin,
                                                 'exportWindow': str(args.window_id)}))
            settings = args.run / 'core/core.ini'
            settings.write_text('[client]\nremoteHost=127.0.0.1\nlanguageSync=false\n'
                                '[core]\n' + f'port={relays[0].port}\ncomputerName={SCREEN}\n'
                                'preventSleep=false\nenableEnterCommand=false\nenableExitCommand=false\n'
                                '[security]\ntlsEnabled=true\ncheckPeerFingerprints=true\n'
                                f'certificate={args.run}/core/tls/deskflow.pem\n'
                                '[log]\nlevel=VERBOSE\ntoFile=false\n')
            (args.run / 'core/tls/trusted-servers').write_text('v2:sha256:' + args.mac_core_pin + '\n')
            argv = [str(args.core), 'client', '--new-instance', '--settings', str(settings),
                    '--windowshare-gnome-config=' + str(configuration)]
            report['coreArgv'] = argv
            core = processes.start('core', argv)
            report['corePid'] = core.pid

            def stop_core_if_running():
                nonlocal stopped
                if core.poll() is None:
                    for relay in relays:
                        relay.expected_disconnect.set()
                    x11.stop_core(core, Path(environment['TMPDIR']), 'physical fixture core')
                    stopped = True
            cleanup.callback(safely_close, 'private core IPC stop', stop_core_if_running)

            def status():
                processes.check()
                for relay in relays:
                    relay.check()
                require(shell.poll() is None and core.poll() is None, 'Owned compositor/core exited before stop')
                core_text, shell_text = read_log(output / 'core.log'), read_log(output / 'gnome-shell.log')
                actual_pins = [value.replace(':', '').lower() for value in re.findall(r'peer fingerprint: ([0-9a-fA-F:]+)', core_text)]
                return {'coreProcessLive': core.poll() is None, 'compositorProcessLive': shell.poll() is None,
                        'relayThreadsLive': all(relay.thread.is_alive() for relay in relays),
                        'connectionsLive': all(relay.thread.is_alive() and relay.stats['connections'] == 1
                                               for relay in relays),
                        'historicalCoreAuthenticated': 'fingerprint matches trusted fingerprint' in core_text and
                                            'connected to secure socket' in core_text and args.mac_core_pin in actual_pins,
                        'historicalCorePeerPin': args.mac_core_pin if args.mac_core_pin in actual_pins else None,
                        'historicalCoreConnected': 'connected to server' in core_text,
                        'historicalProviderOwnedSession': 'connected experimental GNOME input session and owned Wayland viewer' in core_text,
                        'historicalEisConnected': 'connected to eis' in core_text,
                        'eisDevicesObserved': core_text.count('adding device '),
                        'historicalViewerAuthenticated': 'Connected to approved peer — window sharing enabled' in shell_text,
                        'historicalPaintedViewportObserved': 'GNOME viewer painted viewport observed:' in core_text,
                        'sourceViewerFocusReady': None,
                        'focusEvidence': 'not exported by actual core; Mac source receipts required',
                        'legacyPacketCounts': {name: core_text.count(marker) for name, marker in {
                            'keyDown': 'recv key down ', 'keyUp': 'recv key up ', 'keyRepeat': 'recv key repeat ',
                            'buttonDown': 'recv mouse down ', 'buttonUp': 'recv mouse up ',
                            'motion': 'recv mouse move ', 'relativeMotion': 'recv mouse relative move ',
                            'wheel': 'recv mouse wheel ', 'eiKey': 'fake key: '}.items()},
                        'coreLogBytes': (output / 'core.log').stat().st_size,
                        'shellLogBytes': (output / 'gnome-shell.log').stat().st_size,
                        'coreRelay': dict(relays[0].stats), 'viewerRelay': dict(relays[1].stats)}

            def ready():
                value = status()
                report['lastStartupState'] = value
                markers = ('coreProcessLive', 'compositorProcessLive', 'connectionsLive', 'historicalCoreAuthenticated',
                           'historicalCoreConnected', 'historicalProviderOwnedSession', 'historicalEisConnected',
                           'historicalViewerAuthenticated')
                return value if all(value[k] for k in markers) and value['eisDevicesObserved'] >= 3 else None

            value = common.wait_for(processes, ready, 'actual authenticated core and provider-owned viewer', timeout=35, retry_errors=False)
            report['ready'] = value
            clock.ready()
            emit({'event': 'ready', 'id': start_id, 'controlProtocol': 2,
                  'linuxScreen': SCREEN, 'windowId': args.window_id, 'publicPins': report['publicPins'],
                  'corePid': core.pid, 'compositorPid': shell.pid, 'originalInputAuthorityGranted': False,
                  'syntheticStartupInput': False, **clock.state(), 'state': value})
            while True:
                clock.check()
                state = status()
                command = controls.next()
                if command is None:
                    continue
                report['commands'].append(command)
                if command['op'] == 'stop':
                    stop_id = command['id']
                    stop_core_if_running()
                    require(core.returncode == 0, 'Core did not exit normally')
                    for relay in relays:
                        relay.finish()
                    report['coreExit'] = core.returncode
                    report['finalStateBeforeStop'] = state
                    break
                if command['op'] == 'baseline':
                    clock.mark_baseline()
                    report['physicalBaseline'] = {'monotonic': time.monotonic(), 'state': state}
                else:
                    require(command['op'] == 'status', 'Start is allowed exactly once')
                emit({'event': 'baseline' if command['op'] == 'baseline' else 'status', 'id': command['id'],
                      **clock.state(), 'state': state})
        report['status'] = 'completed'
        report['ownedProcessesStopped'] = True
    except StartupStopped:
        report['status'] = 'completed-startup-cancelled'
    except BaseException as error:
        report['failure'] = str(error)
        (output / 'failure.log').write_text(traceback.format_exc())
    return finish_session(output, report, stop_id, core, processes, relays, stopped, cleanup_errors)


def finish_session(output, report, stop_id, core, processes, relays, stopped, cleanup_errors):
    if cleanup_errors:
        report['status'] = 'failed'
        report['cleanupErrors'] = cleanup_errors
    if core is not None:
        report['coreExit'] = core.poll()
    report['processExits'] = [] if processes is None else [
        {'pid': p.pid, 'arguments': p.args, 'exitCode': p.poll()} for p in processes.children]
    report['ownedProcessesStopped'] = all(value['exitCode'] is not None for value in report['processExits'])
    report['coreStoppedByPrivateIpc'] = stopped
    report['relays'] = [dict(relay.stats) for relay in relays]
    (output / 'container-result.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    completed = report['status'] in ('completed', 'completed-without-start', 'completed-startup-cancelled') \
        and report['ownedProcessesStopped'] and (core is None or (core.returncode == 0 and stopped))
    emit({'event': 'stopped' if completed else 'failed', 'id': stop_id, 'status': report['status'],
          'lifecycleCompleted': completed, 'physicalAcceptance': 'not-assessed',
          'failure': report.get('failure'), 'coreExit': report.get('coreExit'), 'coreStoppedByPrivateIpc': stopped,
          'relays': report['relays'], 'physicalReceipts': 'external-Mac-required'})
    return 0 if completed else 1


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while data := stream.read(1024 * 1024):
            digest.update(data)
    return digest.hexdigest()


def read_owned_json(path, limit=131072):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(descriptor)
        require(stat.S_ISREG(info.st_mode) and info.st_uid == os.getuid() and info.st_nlink == 1
                and not info.st_mode & 0o077 and info.st_size <= limit, 'Invalid private configuration file')
        data = os.read(descriptor, limit + 1)
        require(len(data) <= limit, 'Private configuration exceeded its bound')
        return json.loads(data)
    finally:
        os.close(descriptor)


def ordinary_snapshot(pids, configs):
    """Hash explicitly supplied ordinary settings, never return their contents."""
    result = {'bootId': Path('/proc/sys/kernel/random/boot_id').read_text().strip(), 'processes': [], 'configs': []}
    require(len(pids) <= 8 and len(set(pids)) == len(pids) and len(configs) <= 16,
            'Ordinary audit inventory exceeds bound or repeats a PID')
    for pid in pids:
        require(type(pid) is int and pid > 1, 'Invalid ordinary PID')
        proc = Path('/proc') / str(pid)
        require(proc.stat().st_uid == os.getuid(), 'Ordinary process belongs to a different user')
        before = (proc / 'stat').read_text().rsplit(')', 1)[1].split()[19]
        executable = os.readlink(proc / 'exe')
        require(Path(executable).name == 'deskflow-core', 'Ordinary PID is not a Deskflow core')
        command = (proc / 'cmdline').read_bytes()
        require(len(command) <= 65536, 'Ordinary command metadata exceeds bound')
        after = (proc / 'stat').read_text().rsplit(')', 1)[1].split()[19]
        require(before == after, 'Ordinary process identity changed during audit')
        result['processes'].append({'pid': pid, 'startTicks': before, 'executable': executable,
                                    'commandSha256': hashlib.sha256(command).hexdigest()})
    for name in configs:
        path = Path(name)
        require(path.is_absolute(), 'Ordinary configuration path must be absolute')
        try:
            descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except FileNotFoundError:
            result['configs'].append({'path': str(path), 'exists': False})
            continue
        try:
            before = os.fstat(descriptor)
            require(stat.S_ISREG(before.st_mode) and before.st_size <= 16 * 1024 * 1024,
                    'Ordinary configuration is not a bounded regular file')
            content = bytearray()
            while data := os.read(descriptor, 65536):
                content.extend(data)
                require(len(content) <= 16 * 1024 * 1024, 'Ordinary configuration grew beyond bound')
            after = os.fstat(descriptor)
            require((before.st_size, before.st_mtime_ns, before.st_ctime_ns) ==
                    (after.st_size, after.st_mtime_ns, after.st_ctime_ns), 'Ordinary configuration changed during audit')
            result['configs'].append({'path': str(path), 'exists': True, 'device': before.st_dev,
                                     'inode': before.st_ino, 'mode': stat.S_IMODE(before.st_mode),
                                     'sha256': hashlib.sha256(content).hexdigest()})
        finally:
            os.close(descriptor)
    return result


def source_hashes():
    paths = [HERE / name for name in ('physical-linux.py', 'gnome-crossmachine-client.py',
             'gnome-qt-viewer-tests.py', 'gnome-provider-tests.py', 'x11-core-smoke.py')]
    paths += [p for p in sorted((HERE / 'gnome-provider').rglob('*')) if p.is_file() and '__pycache__' not in p.parts]
    return {str(p.relative_to(HERE)): sha(p) for p in paths}


def host_only():
    require(sys.platform == 'linux' and os.getuid() != 0 and not Path('/.dockerenv').exists(),
            'Run this mode as the ordinary user on the Linux Docker host')


def safe_mount(path):
    require(path.is_absolute() and not any(c in str(path) for c in ',\n\r\0'), 'Unsafe bind-mount path')
    return str(path)


def container_command(plan):
    run = Path(plan['runDirectory'])
    result = ['docker', 'run', '--rm', '--init', '--network', 'none', '--user', f'{os.getuid()}:{os.getgid()}',
              '--shm-size', '256m', '--tmpfs', '/tmp:rw,nosuid,nodev,exec,size=1g,mode=1777', '--env', 'HOME=/tmp']
    for source, destination, readonly in ((HERE.parents[1], '/source', True),
            (Path(plan['coreExecutable']).parent, '/core', True), (Path(plan['viewerExecutable']).parent, '/viewer', True),
            (run, '/run', False), (run / 'forward-core', '/forward-core', True),
            (run / 'forward-viewer', '/forward-viewer', True)):
        result += ['--mount', f'type=bind,source={safe_mount(source)},target={destination}' + (',readonly' if readonly else '')]
    return result


def prepare(args):
    host_only()
    canonical_pin(args.mac_core_pin)
    canonical_pin(args.mac_viewer_pin)
    require(re.fullmatch(r'sha256:[0-9a-f]{64}', args.image), 'Use an exact installed Docker image digest')
    require(args.run.is_absolute() and not args.run.exists(), 'Run directory must be new and absolute')
    safe_mount(args.run)
    for path in (args.core, args.viewer):
        require(path.is_absolute() and path.is_file() and os.access(path, os.X_OK), 'Explicit existing Linux executable required')
        with path.open('rb') as stream:
            require(stream.read(4) == b'\x7fELF', 'Expected a Linux ELF executable')
    require(1 <= args.mac_core_port <= 65535 and 1 <= args.mac_viewer_port <= 65535
            and args.mac_core_port != args.mac_viewer_port, 'Distinct valid Mac ports required')
    actual_image = subprocess.check_output(['docker', 'image', 'inspect', '--format', '{{.Id}}', args.image],
                                           text=True, timeout=10).strip()
    require(actual_image == args.image, 'Installed Docker image differs')
    before = ordinary_snapshot(args.ordinary_pid, args.ordinary_config)
    os.umask(0o077)
    args.run.mkdir(mode=0o700)
    container, token = 'deskflow-cross-' + secrets.token_hex(8), secrets.token_hex(16)
    for name in ('forward-core', 'forward-viewer'):
        directory = args.run / name
        directory.mkdir()
        (directory / 'owner.json').write_text(json.dumps({'schema': 1, 'container': container, 'token': token}) + '\n')
    plan = {'version': 2, 'controlProtocol': 2, 'container': container, 'token': token,
            'runDirectory': str(args.run), 'coreExecutable': str(args.core), 'viewerExecutable': str(args.viewer),
            'linuxScreen': SCREEN, 'macCorePin': args.mac_core_pin, 'macViewerPin': args.mac_viewer_pin,
            'remoteSockets': {role: str(args.run / ('forward-' + role) / 'mac.sock') for role in ('core', 'viewer')},
            'macLoopbackPorts': {'core': args.mac_core_port, 'viewer': args.mac_viewer_port},
            'remoteResults': str(args.run / 'results'), 'preparedWaitSeconds': PREPARED_SECONDS,
            'startupTimeoutSeconds': STARTUP_SECONDS, 'runningSeconds': RUNNING_SECONDS,
            'maximumRuntimeSeconds': RESOURCE_SECONDS, 'syntheticStartupInput': False,
            'imageId': args.image, 'sourceHashes': source_hashes(),
            'binaryHashes': {'core': sha(args.core), 'viewer': sha(args.viewer)}, 'helperSha256': sha(Path(__file__)),
            'ordinaryPids': args.ordinary_pid, 'ordinaryConfigs': [str(p) for p in args.ordinary_config],
            'ordinaryAtPrepare': before}
    arguments = container_command(plan) + ['--name', container + '-init', '--label', 'org.deskflow.crossmachine.run=' + token,
                 args.image, 'python3', '/source/deploy/windowshare/physical-linux.py', 'init',
                 '--run', '/run', '--viewer', '/viewer/' + args.viewer.name]
    try:
        with (args.run / 'identity-init.log').open('xb') as log:
            result = subprocess.run(arguments, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, timeout=60)
    finally:
        stop_owned_container({**plan, 'container': container + '-init'})
    require(result.returncode == 0, f'Isolated identity generation failed with exit {result.returncode}')
    public = read_owned_json(args.run / 'public.json')
    plan.update({key: canonical_pin(public[key]) for key in ('corePin', 'viewerPin')})
    require(ordinary_snapshot(args.ordinary_pid, args.ordinary_config) == before,
            'Ordinary configuration/process changed during preparation')
    plan['runArgv'] = ['python3', str(Path(__file__).resolve()), 'run', '--run', str(args.run), '--window-id', '{windowId}']
    plan['cleanupArgv'] = ['python3', str(Path(__file__).resolve()), 'cleanup', '--run', str(args.run)]
    target = args.run / 'linux-plan.json'
    with target.open('x') as output:
        json.dump(plan, output, indent=2, sort_keys=True)
        output.write('\n')
    emit({'event': 'prepared-plan', 'plan': str(target), 'planSha256': sha(target), 'nativeSessionStarted': False,
          'corePin': plan['corePin'], 'viewerPin': plan['viewerPin'], 'ordinaryPreserved': True})
    return 0


def load_plan(run):
    require(run.is_absolute(), 'Absolute owned run directory required')
    info = run.lstat()
    require(stat.S_ISDIR(info.st_mode) and info.st_uid == os.getuid() and not info.st_mode & 0o077,
            'Run directory must be private and owned')
    plan = read_owned_json(run / 'linux-plan.json')
    require(plan['version'] == plan['controlProtocol'] == 2 and plan['runDirectory'] == str(run), 'Wrong physical plan')
    require(re.fullmatch(r'deskflow-cross-[0-9a-f]{16}', plan['container'])
            and re.fullmatch(r'[0-9a-f]{32}', plan['token']), 'Invalid exact-owned container identity')
    for role in ('core', 'viewer'):
        require(plan['remoteSockets'][role] == str(run / ('forward-' + role) / 'mac.sock'), 'Wrong owned socket path')
        owner = read_owned_json(run / ('forward-' + role) / 'owner.json', 512)
        require(owner == {'schema': 1, 'container': plan['container'], 'token': plan['token']}, 'Wrong forward owner')
    return plan


def stop_owned_container(plan):
    result = subprocess.run(['docker', 'inspect', plan['container']], capture_output=True, text=True, timeout=10)
    if result.returncode:
        require('no such object:' in result.stderr.lower(), 'Cannot inspect the owned container')
        return
    info, = json.loads(result.stdout)
    require((info['Config']['Labels'] or {}).get('org.deskflow.crossmachine.run') == plan['token'],
            'Refusing to stop an unrelated container')
    subprocess.run(['docker', 'rm', '-f', info['Id']], capture_output=True, check=True, timeout=15)


def run(args):
    host_only()
    plan = load_plan(args.run)
    require(type(args.window_id) is int and 1 <= args.window_id <= 2**32 - 1, 'Explicit owned Mac window ID required')
    require(source_hashes() == plan['sourceHashes'], 'Fixture source changed after preparation')
    require({role: sha(Path(plan[role + 'Executable'])) for role in ('core', 'viewer')} == plan['binaryHashes'],
            'Production binary changed after preparation')
    before = ordinary_snapshot(plan['ordinaryPids'], plan['ordinaryConfigs'])
    require(before == plan['ordinaryAtPrepare'], 'Ordinary state changed since preparation; prepare a fresh plan')
    with (args.run / 'supervisor-started.json').open('x') as marker:
        json.dump({'pid': os.getpid(), 'monotonic': time.monotonic()}, marker)
    output = args.run / 'results'
    output.mkdir(exist_ok=False)
    report = {'version': 2, 'status': 'failed', 'physicalAcceptance': 'not-assessed', 'syntheticInputSent': False,
              'ordinaryBefore': before, 'planSha256': sha(args.run / 'linux-plan.json'), 'commands': []}
    arguments = container_command(plan)
    arguments.insert(2, '-i')
    arguments += ['--name', plan['container'], '--label', 'org.deskflow.crossmachine.run=' + plan['token'],
                  plan['imageId'], 'python3', '/source/deploy/windowshare/physical-linux.py', 'session', '--run', '/run',
                  '--core', '/core/' + Path(plan['coreExecutable']).name,
                  '--viewer', '/viewer/' + Path(plan['viewerExecutable']).name,
                  '--core-socket', '/forward-core/mac.sock', '--viewer-socket', '/forward-viewer/mac.sock',
                  '--mac-core-pin', plan['macCorePin'], '--mac-viewer-pin', plan['macViewerPin'],
                  '--window-id', str(args.window_id)]
    child = None
    terminal = None
    stop_id = None
    cleanup_errors = []
    try:
        with (output / 'container-stderr.log').open('xb') as errors, \
                (output / 'control.jsonl').open('x', buffering=1) as transcript, contextlib.ExitStack() as resources:
            controls = Controls()
            resources.callback(controls.close)
            child = subprocess.Popen(arguments, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors)
            selector = selectors.DefaultSelector()
            resources.callback(selector.close)
            selector.register(child.stdout, selectors.EVENT_READ)
            buffer = b''
            deadline = time.monotonic() + PREPARED_SECONDS + RESOURCE_SECONDS + 20
            prepared = False
            replies = 0
            while terminal is None:
                require(time.monotonic() < deadline, 'Host supervisor exceeded total lifetime')
                command = controls.next()
                if command is not None:
                    require(prepared or command['op'] == 'stop',
                            'Wait for prepared before sending commands other than Stop')
                    if command['op'] == 'start':
                        require(ordinary_snapshot(plan['ordinaryPids'], plan['ordinaryConfigs']) == before,
                                'Ordinary state changed before Start')
                    if command['op'] == 'stop':
                        stop_id = command['id']
                    report['commands'].append(command)
                    child.stdin.write((json.dumps(command) + '\n').encode())
                    child.stdin.flush()
                if b'\n' not in buffer:
                    if not selector.select(timeout=0.01):
                        require(child.poll() is None, f'Container exited without a final receipt: {child.returncode}')
                        continue
                    block = os.read(child.stdout.fileno(), 4096)
                    require(block, f'Container output closed before final receipt: {child.poll()}')
                    buffer += block
                    require(len(buffer) <= 32768, 'Container response exceeded bound')
                    continue
                line, buffer = buffer.split(b'\n', 1)
                value = json.loads(line)
                replies += 1
                require(replies <= 260 and isinstance(value, dict), 'Invalid/excessive container replies')
                transcript.write(line.decode() + '\n')
                if value.get('event') == 'prepared':
                    require(not prepared and value.get('controlProtocol') == 2 and value.get('windowId') == args.window_id,
                            'Invalid prepared receipt')
                    prepared = True
                if value.get('event') in ('stopped', 'failed'):
                    terminal = value
                else:
                    emit(value)
            report['containerFinal'] = terminal
            require(child.wait(timeout=10) == 0, f'Container exit was nonzero: {child.returncode}')
            require(terminal.get('event') == 'stopped' and terminal.get('id') == stop_id
                    and terminal.get('lifecycleCompleted') is True, 'Container did not complete the requested stop')
            report['status'] = 'completed'
    except BaseException as error:
        report['failure'] = str(error)
        (output / 'supervisor-failure.log').write_text(traceback.format_exc())
    finally:
        if child is not None:
            if child.stdin:
                try:
                    child.stdin.close()
                except (BrokenPipeError, OSError) as error:
                    cleanup_errors.append(f'Control closure: {error}')
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                safely_error = 'Container failed to exit after control closure'
                cleanup_errors.append(safely_error)
        try:
            stop_owned_container(plan)
            report['ownedContainerStopped'] = True
        except BaseException as error:
            cleanup_errors.append(str(error))
            report['ownedContainerStopped'] = False
        if child is not None:
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.terminate()
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=3)
            report['containerExit'] = child.returncode
            if child.stdout:
                child.stdout.close()
        try:
            report['ordinaryAfter'] = ordinary_snapshot(plan['ordinaryPids'], plan['ordinaryConfigs'])
            report['ordinaryPreserved'] = before == report['ordinaryAfter']
        except BaseException as error:
            cleanup_errors.append(str(error))
            report['ordinaryPreserved'] = False
    completed = report['status'] == 'completed' and not cleanup_errors and report['ordinaryPreserved'] \
        and report['ownedContainerStopped'] and report.get('containerExit') == 0
    report['lifecycleCompleted'] = completed
    report['status'] = 'completed' if completed else 'failed'
    report['cleanup'] = {key: report.get(key) for key in ('ordinaryPreserved', 'ownedContainerStopped', 'containerExit')}
    report['cleanupErrors'] = cleanup_errors
    (output / 'result.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    emit({**(terminal or {}), 'event': 'stopped' if completed else 'failed', 'id': stop_id,
          'lifecycleCompleted': completed, 'physicalAcceptance': 'not-assessed', 'cleanup': report['cleanup'],
          'failure': report.get('failure'), 'cleanupErrors': cleanup_errors})
    return 0 if completed else 1


def cleanup(args):
    host_only()
    plan = load_plan(args.run)
    stop_owned_container(plan)
    cross = module('physical_cleanup', HERE / 'gnome-crossmachine-client.py')
    removed = {role: cross.cleanup_socket(args.run / ('forward-' + role), plan['container'], plan['token'])
               for role in ('core', 'viewer')}
    preserved = ordinary_snapshot(plan['ordinaryPids'], plan['ordinaryConfigs']) == plan['ordinaryAtPrepare']
    emit({'event': 'cleaned', 'container': plan['container'], 'forwardSocketsRemoved': removed,
          'ordinaryPreserved': preserved})
    require(preserved, 'Ordinary state changed since preparation')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest='mode', required=True)
    preparation = modes.add_parser('prepare', help='Linux host: create a fresh private plan and identities only')
    preparation.add_argument('--run', type=Path, required=True)
    preparation.add_argument('--core', type=lambda p: Path(p).resolve(strict=True), required=True)
    preparation.add_argument('--viewer', type=lambda p: Path(p).resolve(strict=True), required=True)
    preparation.add_argument('--image', required=True)
    preparation.add_argument('--mac-core-pin', required=True)
    preparation.add_argument('--mac-viewer-pin', required=True)
    preparation.add_argument('--mac-core-port', type=int, default=24860)
    preparation.add_argument('--mac-viewer-port', type=int, default=24861)
    preparation.add_argument('--ordinary-pid', type=int, action='append', required=True)
    preparation.add_argument('--ordinary-config', type=Path, action='append', required=True)
    running = modes.add_parser('run', help='Linux host: supervise the private container and audit ordinary state')
    running.add_argument('--run', type=Path, required=True)
    running.add_argument('--window-id', type=int, required=True)
    cleaning = modes.add_parser('cleanup', help='Linux host: remove only this plan\'s owned resources after SSH closes')
    cleaning.add_argument('--run', type=Path, required=True)
    identity = modes.add_parser('init', help=argparse.SUPPRESS)
    identity.add_argument('--run', type=Path, required=True)
    identity.add_argument('--viewer', type=Path, required=True)
    native = modes.add_parser('session', help=argparse.SUPPRESS)
    native.add_argument('--run', type=Path, required=True)
    native.add_argument('--core', type=Path, required=True)
    native.add_argument('--viewer', type=Path, required=True)
    native.add_argument('--core-socket', type=Path, required=True)
    native.add_argument('--viewer-socket', type=Path, required=True)
    native.add_argument('--mac-core-pin', required=True)
    native.add_argument('--mac-viewer-pin', required=True)
    native.add_argument('--window-id', type=int, required=True)
    args = parser.parse_args()
    def interrupted(signum, _frame):
        raise KeyboardInterrupt(f'Owned session interrupted by signal {signum}')
    signal.signal(signal.SIGTERM, interrupted)
    return {'prepare': prepare, 'run': run, 'cleanup': cleanup, 'init': init, 'session': session}[args.mode](args)


if __name__ == '__main__':
    raise SystemExit(main())
