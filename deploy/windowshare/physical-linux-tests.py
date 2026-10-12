#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Physical fixture gates/audits using pipes and a plain Python child only."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location('physical_linux', Path(__file__).with_name('physical-linux.py'))
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class Gates(unittest.TestCase):
    def setUp(self):
        self.now = 0
        self.clock = module.SessionClock(lambda: self.now)

    def test_visible_start_wait_does_not_spend_running_time(self):
        self.now = 599
        self.assertEqual(self.clock.state()['phase'], 'prepared')
        self.clock.start()
        self.now += 79
        self.clock.ready()
        self.assertEqual(self.clock.state()['maximumRemainingSeconds'], 180)
        self.now += 179
        self.clock.check()
        self.now += 1
        with self.assertRaisesRegex(RuntimeError, 'running deadline expired'):
            self.clock.check()

    def test_status_and_start_cannot_revive_expired_preparation(self):
        for value in (100, 500, 599):
            self.now = value
            self.clock.state()
        self.now = 600
        with self.assertRaisesRegex(RuntimeError, 'prepared deadline expired'):
            self.clock.start()

    def test_late_ready_and_duplicate_start_cannot_extend_authority(self):
        self.clock.start()
        with self.assertRaisesRegex(RuntimeError, 'exactly once'):
            self.clock.start()
        self.now = 80
        with self.assertRaisesRegex(RuntimeError, 'starting deadline expired'):
            self.clock.ready()

    def test_baseline_requires_running_and_is_once_only(self):
        with self.assertRaisesRegex(RuntimeError, 'requires running'):
            self.clock.mark_baseline()
        self.clock.start()
        self.clock.ready()
        deadline = self.clock.deadline
        self.clock.mark_baseline()
        self.assertEqual(self.clock.deadline, deadline)
        with self.assertRaisesRegex(RuntimeError, 'allowed once'):
            self.clock.mark_baseline()


class Commands(unittest.TestCase):
    @contextlib.contextmanager
    def reader(self, data):
        read, write = os.pipe()
        try:
            os.write(write, data)
            controls = module.Controls(read)
            try:
                yield controls
            finally:
                controls.close()
        finally:
            os.close(read)
            os.close(write)

    def test_only_bounded_lifecycle_commands(self):
        values = [{'id': i + 1, 'op': op} for i, op in enumerate(('status', 'start', 'baseline', 'status', 'stop'))]
        with self.reader(b''.join(json.dumps(v).encode() + b'\n' for v in values)) as reader:
            self.assertEqual([reader.next() for _ in values], values)

    def test_no_input_extra_fields_duplicates_or_invalid_ids(self):
        invalid = [b'{"id":1,"op":"key"}', b'{"id":1,"op":"move","x":1,"y":2}',
                   b'{"id":1,"op":"scroll"}', b'{"id":1,"op":"click"}',
                   b'{"id":1,"op":"stop","extra":1}', b'{"id":1,"id":2,"op":"stop"}',
                   b'{"id":true,"op":"start"}', b'{"id":0,"op":"stop"}',
                   b'{"id":9007199254740992,"op":"stop"}', b'{"id":1,"op":[]}', b'[]']
        for value in invalid:
            with self.subTest(value=value), self.reader(value + b'\n') as reader:
                with self.assertRaises(RuntimeError):
                    reader.next()

    def test_replay_frame_bound_and_command_count(self):
        with self.reader(b'{"id":1,"op":"status"}\n' * 2) as reader:
            reader.next()
            with self.assertRaisesRegex(RuntimeError, 'Nonmonotonic'):
                reader.next()
        with self.reader(b'x' * 4097) as reader:
            self.assertIsNone(reader.next())
            with self.assertRaisesRegex(RuntimeError, 'exceeded'):
                reader.next()
        with self.reader(b'{"id":257,"op":"status"}\n') as reader:
            reader.count = 256
            with self.assertRaisesRegex(RuntimeError, 'count exceeded'):
                reader.next()


class Lifetimes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cross = module.module('physical_test_cross', module.HERE / 'gnome-crossmachine-client.py')

    def test_process_lifetime_default_and_deliberate_longer_bound(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            now = [10]
            with patch.object(self.cross.common.time, 'monotonic', side_effect=lambda: now[0]):
                default = self.cross.common.Processes(output, {})
                extended = self.cross.common.Processes(output, {}, lifetime_seconds=270)
                now[0] = 190
                with self.assertRaisesRegex(RuntimeError, '180 seconds'):
                    default.check()
                extended.check()
                now[0] = 280
                with self.assertRaisesRegex(RuntimeError, '270 seconds'):
                    extended.check()

    def test_invalid_process_and_relay_bounds_fail_before_resources(self):
        for value in (0, 29, 601, True, 270.0, '270', float('inf')):
            with self.subTest(value=value):
                with self.assertRaisesRegex(RuntimeError, 'integer from 30 to 600'):
                    self.cross.common.Processes(Path('/unused'), {}, lifetime_seconds=value)
                with self.assertRaisesRegex(RuntimeError, 'integer from 30 to 600'):
                    self.cross.UnixRelay(Path('/unused'), lifetime_seconds=value)

    def test_relay_uses_requested_absolute_deadline(self):
        # Drive only the acceptance loop with a fake clock/listener. No socket
        # or thread is opened, and expiry must use the selected270s bound.
        relay = self.cross.UnixRelay.__new__(self.cross.UnixRelay)
        relay.lifetime_seconds = 270
        relay.stopping = SimpleNamespace(is_set=lambda: False)
        relay.listener = Mock()
        relay.listener.accept.side_effect = TimeoutError
        relay.failure = None
        with patch.object(self.cross.time, 'monotonic', side_effect=[0, 181, 269, 270]):
            relay.run()
        self.assertEqual(relay.listener.accept.call_count, 2)
        self.assertIn('acceptance deadline expired', relay.failure)


class PreparedSession(unittest.TestCase):
    def test_stop_before_start_needs_no_socket_or_native_resources(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            pin = '1' * 64
            (run / 'public.json').write_text(json.dumps({'corePin': pin, 'viewerPin': pin}))
            args = SimpleNamespace(run=run, mac_core_pin=pin, mac_viewer_pin=pin, window_id=42)
            cross = SimpleNamespace(common=SimpleNamespace(Processes=Mock()),
                                    qt=SimpleNamespace(start_shell=Mock()), UnixRelay=Mock(), fingerprint=lambda _: pin)
            replies = []
            controls = SimpleNamespace(next=Mock(return_value={'id': 1, 'op': 'stop'}), close=Mock())
            original_is_file = Path.is_file
            with patch.object(module.sys, 'platform', 'linux'), patch.object(module.os, 'getuid', return_value=1000), \
                    patch.object(Path, 'is_file', lambda p: True if str(p) == '/.dockerenv' else original_is_file(p)), \
                    patch.object(module, 'module', side_effect=[cross, SimpleNamespace()]), \
                    patch.object(module, 'pin', return_value=pin), patch.object(module, 'Controls', return_value=controls), \
                    patch.object(module, 'emit', side_effect=replies.append):
                self.assertEqual(module.session(args), 0)
            self.assertEqual([r['event'] for r in replies], ['prepared', 'stopped'])
            self.assertFalse(replies[0]['nativeProcessesStarted'])
            self.assertTrue(replies[-1]['lifecycleCompleted'])
            cross.UnixRelay.assert_not_called()
            cross.qt.start_shell.assert_not_called()
            cross.common.Processes.assert_not_called()

    def test_stop_during_startup_prevents_late_ready(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            pin = '1' * 64
            (run / 'public.json').write_text(json.dumps({'corePin': pin, 'viewerPin': pin}))
            args = SimpleNamespace(run=run, mac_core_pin=pin, mac_viewer_pin=pin, window_id=42,
                                   core_socket=run / 'absent-core', viewer_socket=run / 'absent-viewer')
            shared = module.module('physical_cancel_common', module.HERE / 'gnome-provider-tests.py')
            relay = SimpleNamespace(close=Mock(), stats={})
            shell = Mock(side_effect=lambda processes, _root, _report: processes.check())
            cross = SimpleNamespace(common=shared, qt=SimpleNamespace(start_shell=shell),
                                    UnixRelay=Mock(return_value=relay), fingerprint=lambda _: pin)
            replies = []
            controls = SimpleNamespace(next=Mock(side_effect=[{'id': 1, 'op': 'start'}, {'id': 2, 'op': 'stop'}]),
                                       close=Mock())
            original_is_file = Path.is_file
            with patch.object(module.sys, 'platform', 'linux'), patch.object(module.os, 'getuid', return_value=1000), \
                    patch.object(Path, 'is_file', lambda p: True if str(p) == '/.dockerenv' else original_is_file(p)), \
                    patch.object(module, 'module', side_effect=[cross, SimpleNamespace()]), \
                    patch.object(module, 'pin', return_value=pin), patch.object(module, 'Controls', return_value=controls), \
                    patch.object(module, 'emit', side_effect=replies.append):
                self.assertEqual(module.session(args), 0)
            self.assertEqual([r['event'] for r in replies], ['prepared', 'stopped'])
            self.assertEqual(replies[-1]['id'], 2)
            self.assertEqual(replies[-1]['status'], 'completed-startup-cancelled')
            self.assertEqual(relay.close.call_count, 2)
            self.assertEqual(shell.call_count, 1)


class OrdinaryAudit(unittest.TestCase):
    def test_existing_and_absent_settings_changes_are_detected_without_contents(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'config'
            absent = Path(directory) / 'missing'
            config.write_text('private-value-one')
            with patch.object(Path, 'read_text', return_value='test-boot'):
                before = module.ordinary_snapshot([], [config, absent])
                config.write_text('private-value-two')
                changed = module.ordinary_snapshot([], [config, absent])
                absent.write_text('new-value')
                created = module.ordinary_snapshot([], [config, absent])
            self.assertNotEqual(before, changed)
            self.assertNotEqual(changed, created)
            self.assertNotIn('private-value', json.dumps(before))
            self.assertFalse(before['configs'][1]['exists'])

    def test_nonregular_settings_are_rejected_without_blocking(self):
        with tempfile.TemporaryDirectory() as directory:
            fifo = Path(directory) / 'settings'
            os.mkfifo(fifo)
            with patch.object(Path, 'read_text', return_value='test-boot'):
                with self.assertRaisesRegex(RuntimeError, 'bounded regular'):
                    module.ordinary_snapshot([], [fifo])


FAKE_CHILD = r'''
import json,sys
print(json.dumps({'event':'prepared','controlProtocol':2,'windowId':42}),flush=True)
for line in sys.stdin:
    value=json.loads(line)
    if value['op']=='start':
        print(json.dumps({'event':'ready','id':value['id']}),flush=True)
    elif value['op']=='stop':
        print(json.dumps({'event':'stopped','id':value['id'],'lifecycleCompleted':True}),flush=True)
        raise SystemExit(int(sys.argv[1]))
raise SystemExit(9)
'''


class Supervisor(unittest.TestCase):
    def drive(self, *, exit_code=0, changed_at_start=False, changed_at_exit=False, start=True, early_stop=False):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        run = Path(temporary.name)
        read, write = os.pipe()
        self.addCleanup(os.close, read)
        self.addCleanup(os.close, write)
        before, changed = {'snapshot': 1}, {'snapshot': 2}
        snapshots = [before]
        if start:
            snapshots.append(changed if changed_at_start else before)
        snapshots.append(changed if changed_at_exit else before)
        plan = {'sourceHashes': {}, 'binaryHashes': {'core': 'hash', 'viewer': 'hash'},
                'coreExecutable': '/tmp/deskflow-core', 'viewerExecutable': '/tmp/deskflow-windowshare',
                'ordinaryPids': [42], 'ordinaryConfigs': ['/tmp/config'], 'ordinaryAtPrepare': before,
                'container': 'deskflow-cross-' + '1' * 16, 'token': '2' * 32,
                'macCorePin': '3' * 64, 'macViewerPin': '4' * 64, 'imageId': 'sha256:' + '5' * 64}
        controls = module.Controls(read)
        if early_stop:
            self.assertFalse(start)
            os.write(write, b'{"id":1,"op":"stop"}\n')
        replies = []
        children = []
        real_popen = subprocess.Popen

        def launch(_arguments, **kwargs):
            child = real_popen([sys.executable, '-c', FAKE_CHILD, str(exit_code)], **kwargs)
            children.append(child)
            return child

        def reply(value):
            replies.append(value)
            if value['event'] == 'prepared' and not early_stop:
                os.write(write, json.dumps({'id': 1, 'op': 'start' if start else 'stop'}).encode() + b'\n')
            elif value['event'] == 'ready':
                os.write(write, b'{"id":2,"op":"stop"}\n')

        with patch.object(module, 'host_only'), patch.object(module, 'load_plan', return_value=plan), \
                patch.object(module, 'source_hashes', return_value={}), patch.object(module, 'sha', return_value='hash'), \
                patch.object(module, 'ordinary_snapshot', side_effect=snapshots) as audit, \
                patch.object(module, 'container_command', return_value=[]), \
                patch.object(module, 'stop_owned_container') as stop, \
                patch.object(module, 'Controls', return_value=controls), patch.object(module, 'emit', side_effect=reply), \
                patch.object(module.subprocess, 'Popen', side_effect=launch):
            result = module.run(SimpleNamespace(run=run, window_id=42))
        self.assertTrue(all(p.poll() is not None for p in children))
        self.assertEqual(audit.call_count, 3 if start else 2)
        stop.assert_called_once_with(plan)
        return result, replies, json.loads((run / 'results/result.json').read_text())

    def test_stopped_success_requires_before_start_after_audits(self):
        result, replies, report = self.drive()
        self.assertEqual(result, 0)
        self.assertEqual([v['event'] for v in replies], ['prepared', 'ready', 'stopped'])
        self.assertEqual(replies[-1]['cleanup'], {'ordinaryPreserved': True, 'ownedContainerStopped': True, 'containerExit': 0})
        self.assertEqual(report['physicalAcceptance'], 'not-assessed')

    def test_stop_without_start_is_clean_and_never_running(self):
        result, replies, _ = self.drive(start=False)
        self.assertEqual(result, 0)
        self.assertEqual([v['event'] for v in replies], ['prepared', 'stopped'])

    def test_stop_before_first_prepared_receipt_is_forwarded_without_start(self):
        result, replies, report = self.drive(start=False, early_stop=True)
        self.assertEqual(result, 0)
        self.assertEqual([v['event'] for v in replies], ['prepared', 'stopped'])
        self.assertEqual(replies[-1]['id'], 1)
        self.assertEqual(report['commands'], [{'id': 1, 'op': 'stop'}])

    def test_changed_ordinary_state_prevents_forwarding_start(self):
        result, replies, report = self.drive(changed_at_start=True)
        self.assertEqual(result, 1)
        self.assertNotIn('ready', [v['event'] for v in replies])
        self.assertIn('before Start', report['failure'])

    def test_changed_final_audit_cannot_report_success(self):
        result, replies, _ = self.drive(changed_at_exit=True)
        self.assertEqual(result, 1)
        self.assertEqual(replies[-1]['event'], 'failed')
        self.assertFalse(replies[-1]['cleanup']['ordinaryPreserved'])

    def test_nonzero_container_exit_is_retained_after_stopped_receipt(self):
        result, replies, report = self.drive(exit_code=7)
        self.assertEqual(result, 1)
        self.assertEqual(replies[-1]['cleanup']['containerExit'], 7)
        self.assertEqual(report['status'], 'failed')


if __name__ == '__main__':
    unittest.main(verbosity=2)
