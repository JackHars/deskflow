#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Manual coordinator regressions without native apps, input, or SSH."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import plistlib
import ssl
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

SPEC = importlib.util.spec_from_file_location("physical_mac", Path(__file__).with_name("physical-macos.py"))
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


def receipt(total=0, first=1, request="none", request_sequence=0):
    return {"events": [{"sequence": n, "type": 6} for n in range(first, total + 1)],
            "eventCount": total, "nativeEvents": [], "nativeEventCount": 0,
            "manual": {"state": "preparing", "request": request, "requestSequence": request_sequence,
                       "statusValid": True, "remainingSeconds": 0}}


class HistoryTests(unittest.TestCase):
    def test_ring_retirement_is_distinct_from_missing_input(self):
        history = module.ReceiptHistory()
        self.assertEqual(len(history.merge(receipt(128))), 128)
        self.assertEqual(len(history.merge(receipt(129, 2))), 1)
        self.assertEqual(history.gaps, [])
        self.assertEqual(history.merge(receipt(129, 2)), [])
        self.assertEqual(len(history.merge(receipt(260, 133))), 128)
        self.assertEqual(history.gaps, [{"stream": "events", "after": 129, "next": 133}])

    def test_reset_malformed_ring_and_exhaustion_are_terminal(self):
        history = module.ReceiptHistory()
        history.merge(receipt(3))
        with self.assertRaisesRegex(RuntimeError, "total reset"):
            history.merge(receipt(2))
        for bad in ({"events": [{"sequence": 1}, {"sequence": 3}], "eventCount": 3},
                    {"events": [], "eventCount": 1}, {"eventSequenceExhausted": True},
                    {"eventCount": True}):
            value = receipt()
            value.update(bad)
            with self.subTest(bad=bad), self.assertRaises(RuntimeError):
                module.ReceiptHistory().merge(value)

    def test_manual_stop_is_terminal_and_cannot_rearm(self):
        history = module.ReceiptHistory()
        history.merge(receipt(request="start", request_sequence=1))
        history.merge(receipt(request="stop", request_sequence=2))
        with self.assertRaises(RuntimeError):
            history.merge(receipt(request="start", request_sequence=1))
        for request, sequence in (("start", 0), ("none", 1), ("start", 2), ("stop", True)):
            with self.subTest(request=request, sequence=sequence), self.assertRaises(RuntimeError):
                module.ReceiptHistory().merge(receipt(request=request, request_sequence=sequence))
        # A stop before start, or a collected stop after missing the start
        # metadata publication, remains terminal cancellation.
        module.ReceiptHistory().merge(receipt(request="stop", request_sequence=1))
        module.ReceiptHistory().merge(receipt(request="stop", request_sequence=2))


class StatusTests(unittest.TestCase):
    def test_countdown_cannot_extend_or_restart(self):
        for session_seconds in (120, 150):
            with self.subTest(session_seconds=session_seconds):
                self.check_countdown(session_seconds)

    def check_countdown(self, session_seconds):
        publisher = module.StatusPublisher.__new__(module.StatusPublisher)
        publisher.session_seconds = session_seconds
        publisher.lock = threading.Lock()
        publisher.state, publisher.deadline, publisher.was_active = "preparing", None, False
        now = [100.0]
        with patch.object(module.time, "monotonic", side_effect=lambda: now[0]):
            publisher.set("ready")
            with self.assertRaises(RuntimeError):
                publisher.set("active", session_seconds + 1)
            publisher.set("active", session_seconds)
            now[0] += 30
            publisher.set("active", session_seconds)
            self.assertEqual(publisher.snapshot()["remainingSeconds"], session_seconds - 30)
            with self.assertRaises(RuntimeError):
                publisher.set("preparing")
            now[0] += session_seconds
            self.assertEqual(publisher.snapshot(), {"version": 1, "state": "stopped", "remainingSeconds": 0})
            with self.assertRaises(RuntimeError):
                publisher.set("active", session_seconds)

    def test_extended_receipts_require_the_explicit_session_profile(self):
        value = receipt(request="start", request_sequence=1)
        value["manual"].update(state="active", remainingSeconds=150)
        with self.assertRaisesRegex(RuntimeError, "manual controls"):
            module.ReceiptHistory().merge(value)
        module.ReceiptHistory(150).merge(value)
        value["manual"]["remainingSeconds"] = 151
        with self.assertRaisesRegex(RuntimeError, "manual controls"):
            module.ReceiptHistory(150).merge(value)
        for invalid in (0, 121, 180, True, "150"):
            with self.subTest(invalid=invalid), self.assertRaises(RuntimeError):
                module.ReceiptHistory(invalid)

    def test_publications_are_private_and_change_file_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "status.json"
            value = {"version": 1, "state": "ready", "remainingSeconds": 0}
            module.atomic_json(path, value)
            before = path.stat()
            module.atomic_json(path, value)
            after = path.stat()
            self.assertEqual(after.st_mode & 0o777, 0o600)
            self.assertNotEqual(before.st_ino, after.st_ino)
            self.assertEqual(module.read_json(path), value)

    def test_duplicate_and_nonfinite_json_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "value.json"
            for value in ('{"version":1,"version":2}', '{"value":NaN}'):
                path.write_text(value)
                with self.assertRaises(RuntimeError):
                    module.read_json(path)


class CollectorTests(unittest.TestCase):
    def test_unchanged_missing_and_fresh_replaced_receipts(self):
        with tempfile.TemporaryDirectory() as directory:
            path, log = Path(directory) / "receipt.json", Path(directory) / "history.jsonl"
            value = receipt()
            value.update({"pid": 123, "title": "Own source", "ready": True, "windowId": 7})
            module.atomic_json(path, value)
            collector = module.Collector(path, 123, "Own source", log)
            try:
                module.CROSS.wait_for(lambda: collector.snapshot()["receipt"], "initial receipt", timeout=1)
                with collector.lock:
                    collector.publication_observed -= 3
                time.sleep(0.05)
                with self.assertRaisesRegex(RuntimeError, "stale"):
                    collector.snapshot()
                module.atomic_json(path, value)
                def fresh():
                    try:
                        return collector.snapshot()["receipt"]
                    except RuntimeError:
                        return None
                # Hosted macOS runners can briefly deschedule the 25ms receipt collector.
                # Only this test's observation allowance changes; the real
                # 2-second stale publication threshold remains enforced above.
                module.CROSS.wait_for(fresh, "fresh replacement", timeout=3)
                path.unlink()
                with self.assertRaisesRegex(RuntimeError, "missing"):
                    collector.snapshot()
                # A concurrent publication removal cannot permanently poison
                # the worker or make it reuse the last fresh receipt. Even
                # after a normal poll cycle the snapshot still refuses it.
                time.sleep(0.075)
                self.assertIsNone(collector.error)
                self.assertTrue(collector.thread.is_alive())
                with self.assertRaisesRegex(RuntimeError, "missing"):
                    collector.snapshot()
            finally:
                collector.close()


    def test_torn_read_never_renews_freshness_or_kills_monitor(self):
        with tempfile.TemporaryDirectory() as directory:
            path, log = Path(directory) / "receipt.json", Path(directory) / "events.jsonl"
            value = receipt()
            value.update({"pid": 321, "title": "Exact owner", "ready": True, "windowId": 9})
            module.atomic_json(path, value)
            collector = module.Collector(path, 321, "Exact owner", log)
            try:
                module.CROSS.wait_for(lambda: collector.snapshot()["receipt"], "first stable receipt", timeout=3)
                with collector.lock:
                    collector.publication_observed -= 3
                real_read = module.read_json_with_identity
                # Simulate a concurrent fstat/open/file rewrite without
                # creating a permanently malformed or newer publication.
                with patch.object(module, "read_json_with_identity",
                                  side_effect=module.ConcurrentPublicationChange("torn read")):
                    time.sleep(0.09)
                    self.assertIsNone(collector.error)
                    self.assertTrue(collector.thread.is_alive())
                    with self.assertRaisesRegex(RuntimeError, "stale"):
                        collector.snapshot()
                module.atomic_json(path, value)
                def recovered():
                    try:
                        return collector.snapshot()["receipt"]
                    except RuntimeError:
                        return None
                module.CROSS.wait_for(recovered, "stable replacement after torn read", timeout=3)
                self.assertIsNone(collector.error)
                self.assertIs(module.read_json_with_identity, real_read)
            finally:
                collector.close()

    def test_stable_duplicate_json_remains_terminal(self):
        with tempfile.TemporaryDirectory() as directory:
            path, log = Path(directory) / "receipt.json", Path(directory) / "events.jsonl"
            value = receipt()
            value.update({"pid": 321, "title": "Exact owner", "ready": True, "windowId": 9})
            module.atomic_json(path, value)
            collector = module.Collector(path, 321, "Exact owner", log)
            try:
                module.CROSS.wait_for(lambda: collector.snapshot()["receipt"], "first stable receipt", timeout=3)
                # Publish stable malformed input in one rename, not by
                # truncating the watched path and exposing a transient empty
                # file. The collector must still reject *genuine* duplicate
                # keys, not a test-created partial-write artifact.
                replacement = path.with_name("invalid-receipt.next")
                replacement.write_text('{"pid":321,"pid":321,"title":"Exact owner"}')
                replacement.replace(path)
                module.CROSS.wait_for(lambda: collector.error is not None,
                                      "stable malformed source JSON refusal", timeout=3)
                with self.assertRaisesRegex(RuntimeError, "Duplicate JSON field"):
                    collector.snapshot()
            finally:
                collector.close()

class DriverTests(unittest.TestCase):
    @contextlib.contextmanager
    def driver(self, replies, check=lambda: None):
        read, write = os.pipe()
        with os.fdopen(read, "rb", buffering=0) as incoming, os.fdopen(write, "wb", buffering=0) as outgoing:
            outgoing.write(b"".join(json.dumps(reply).encode() + b"\n" for reply in replies))
            process = SimpleNamespace(stdout=incoming, stdin=io.BytesIO())
            driver = module.Driver(process, io.StringIO(), check)
            try:
                yield driver
            finally:
                driver.selector.close()

    def test_cancel_start_drains_only_its_late_reply_before_stop_ack(self):
        def cancelled():
            raise module.Cancelled("Stop clicked")
        with self.driver([{"event": "prepared"}, {"event": "ready", "id": 1},
                          {"event": "stopped", "id": 2}], cancelled) as driver:
            driver.event("prepared", check=False)
            with self.assertRaises(module.Cancelled):
                driver.command("start")
            self.assertEqual(driver.command("stop", check=False), {"event": "stopped", "id": 2})
            self.assertEqual(driver.pending, {})
            self.assertIn('"event": "ready"', driver.transcript.getvalue())

    def test_cancel_before_prepared_still_requires_own_stop_ack(self):
        with self.driver([{"event": "prepared"}, {"event": "stopped", "id": 1}]) as driver:
            self.assertEqual(driver.command("stop", check=False)["id"], 1)
            self.assertTrue(driver.prepared_seen)

    def test_unknown_wrong_kind_or_boolean_reply_id_fails(self):
        for reply in ({"event": "stopped", "id": 99}, {"event": "ready", "id": 1},
                      {"event": "stopped", "id": True}):
            with self.subTest(reply=reply), self.driver([reply]) as driver, self.assertRaises(RuntimeError):
                driver.command("stop", check=False)

    def test_ordinary_status_does_not_accept_an_unrelated_late_reply(self):
        with self.driver([{"event": "ready", "id": 1}]) as driver:
            driver.sequence = 1
            driver.pending[1] = "ready"
            with self.assertRaisesRegex(RuntimeError, "Unrelated"):
                driver.command("status")


class ProductionPlans(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name).resolve()
        self.support = module.original_support()
        self.bundle = self.base / 'Frozen Experimental.app'
        roles = self.bundle / 'Contents/MacOS'
        roles.mkdir(parents=True)
        self.paths = {name: roles / executable for name, executable in
                      (('core', 'deskflow-core'), ('host', 'deskflow-windowshare'),
                       ('holdingHelper', 'deskflow-holding-display-helper'))}
        self.paths.update({'source': self.base / 'source', 'infoPlist': self.bundle / 'Contents/Info.plist',
                           'codeResources': self.bundle / 'Contents/_CodeSignature/CodeResources',
                           'libraryManifest': self.bundle / 'Contents/Resources/deskflow-exact-libraries.json'})
        for name, path in self.paths.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('unexecuted-fixture-' + name).encode())
        self.paths['infoPlist'].write_bytes(plistlib.dumps({'CFBundleExecutable': 'deskflow-core',
                                                         'CFBundleIdentifier': 'org.deskflow.seamless.core.experimental'}))
        identity = self.base / 'host-identity'
        identity.mkdir()
        # These are parser fixtures, never launched or treated as signed code.
        (identity / 'cert.pem').write_text(ssl.DER_cert_to_PEM_cert(b'host-certificate-parser-fixture'))
        certificate = self.base / 'core.pem'
        certificate.write_text(ssl.DER_cert_to_PEM_cert(b'core-certificate-parser-fixture'))
        core_pin, host_pin = self.support.certificate_pin(certificate), self.support.certificate_pin(identity / 'cert.pem')
        server = self.base / 'server.conf'
        server.write_text('section: options\n'
                          'keystroke(control+alt+shift+Right) = switchToScreen(gnome-physical-test)\n'
                          'keystroke(control+alt+shift+Left) = switchToScreen(mac-physical-test)\nend\n')
        self.settings = self.base / 'core.ini'
        self.settings.write_text(
            '[core]\ninterface=127.0.0.1\ncomputerName=mac-physical-test\nport=24860\n'
            'enableEnterCommand=false\nenableExitCommand=false\n'
            '[server]\nexternalConfig=true\nenableClipboard=false\nrelativeMouseMoves=false\n'
            f'externalConfigFile={server}\n[security]\ntlsEnabled=true\ncheckPeerFingerprints=true\ncertificate={certificate}\n'
            '[log]\nlevel=DEBUG\n' + ''.join('[computer_' + screen + ']\n' + ''.join(
                'modifier' + key + '=' + key.lower() + '\n' for key in ('Shift', 'Ctrl', 'Alt', 'AltGr', 'Meta', 'Super'))
                for screen in ('mac-physical-test', 'gnome-physical-test')))
        self.mac = {'version': 2, 'preparedOnly': True, 'originalInputEnabled': True, 'holdingDisplayEnabled': False,
                    'coreScreen': 'mac-physical-test', 'linuxScreen': 'gnome-physical-test',
                    'corePort': 24860, 'hostPort': 24861, 'coreFingerprint': core_pin, 'hostFingerprint': host_pin,
                    'coreArgv': [str(self.paths['core']), 'server', '--new-instance', '--settings', str(self.settings)],
                    'hostArgv': [str(self.paths['host']), '--identity', str(identity)],
                    'sourceExecutable': str(self.paths['source']), 'coreIpcExpectedPath': str(self.base / 'tmp/deskflow-core'),
                    'environmentOverrides': {key: str(self.base / key) for key in
                        ('XDG_CONFIG_HOME', 'XDG_CONFIG_DIRS', 'XDG_STATE_HOME', 'XDG_CACHE_HOME',
                         'XDG_DATA_HOME', 'XDG_DATA_DIRS', 'XDG_RUNTIME_DIR', 'TMPDIR')},
                    'frozenArtifacts': {name: self.support.sha(path) for name, path in self.paths.items()}}
        self.linux = {'version': 3, 'controlProtocol': 3, 'routeMode': 'production-original-input',
                      'macOriginalInputConfigured': True, 'syntheticStartupInput': False,
                      'macCorePin': core_pin, 'macViewerPin': host_pin, 'corePin': '1' * 64, 'viewerPin': '2' * 64,
                      'linuxScreen': self.mac['linuxScreen'], 'macLoopbackPorts': {'core': 24860, 'viewer': 24861},
                      'remoteSockets': {'core': '/private/core.sock', 'viewer': '/private/viewer.sock'},
                      'runArgv': ['python3', '/private/controller', 'run', '{windowId}'],
                      'cleanupArgv': ['python3', '/private/controller', 'cleanup']}

    def test_explicit_mode_uses_one_exact_peer_pair_and_core_owned_host(self):
        artifacts = module.validate_plans(self.mac, self.linux, self.base, production=True)
        self.assertEqual(artifacts['core']['sha256'], self.mac['frozenArtifacts']['core'])
        configuration = self.support.host_configuration(self.mac, self.linux, self.base / 'restore.json')
        self.assertEqual(configuration['inputPeerPairs'], [{'deskflowPeerPin': '1' * 64, 'windowSharePeerPin': '2' * 64}])
        self.assertFalse(configuration['holdingDisplay'])
        self.assertFalse(configuration['shareClipboardText'])
        argv = self.support.core_arguments(self.mac, self.base / 'production-host.json')
        self.assertEqual(argv[:-1], self.mac['coreArgv'])
        self.assertEqual(argv[-1], '--windowshare-host-config=' + str(self.base / 'production-host.json'))
        self.assertFalse(any('--core-input-fd' in value for value in argv))

    def test_default_cannot_enable_production_and_versions_cannot_cross(self):
        with self.assertRaises(RuntimeError):
            module.validate_plans(self.mac, self.linux, self.base)
        legacy_mac = {**self.mac, 'version': 1, 'originalInputEnabled': False}
        legacy_linux = {k: v for k, v in self.linux.items() if k not in ('routeMode', 'macOriginalInputConfigured')}
        legacy_linux.update(version=2, controlProtocol=2)
        self.assertIsNone(module.validate_plans(legacy_mac, legacy_linux, self.base))
        for mac, linux in ((legacy_mac, self.linux), (self.mac, legacy_linux)):
            with self.assertRaises(RuntimeError):
                module.validate_plans(mac, linux, self.base, production=True)

    def test_hash_peer_package_and_holding_substitutions_refuse(self):
        for name in self.paths:
            altered = {**self.mac, 'frozenArtifacts': {**self.mac['frozenArtifacts'], name: '0' * 64}}
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                module.validate_plans(altered, self.linux, self.base, production=True)
        for mac, linux in (({**self.mac, 'holdingDisplayEnabled': True}, self.linux),
                           (self.mac, {**self.linux, 'viewerPin': '0' * 64}),
                           (self.mac, {**self.linux, 'macCorePin': '3' * 64}),
                           (self.mac, {**self.linux, 'macOriginalInputConfigured': False}),
                           ({**self.mac, 'hostArgv': [*self.mac['hostArgv'], '--core-input-fd=3']}, self.linux)):
            with self.assertRaises(RuntimeError):
                module.validate_plans(mac, linux, self.base, production=True)

    def test_validate_only_needs_no_ssh_or_native_process_and_never_passes_physical(self):
        mac_plan, linux_plan = self.base / 'mac.json', self.base / 'linux.json'
        module.atomic_json(mac_plan, self.mac)
        module.atomic_json(linux_plan, self.linux)
        args = SimpleNamespace(mac_plan=mac_plan, linux_plan=linux_plan, source=None,
                               production_original_input=True, validate_only=True, output=self.base / 'result')
        with patch.object(module.subprocess, 'Popen', side_effect=AssertionError('Native process launched')), \
                patch.object(module.subprocess, 'run', side_effect=AssertionError('External command launched')), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(module.run(args), 0)
        result = module.read_json(args.output / 'result.json')
        self.assertEqual(result['status'], 'plan-validated-native-not-run')
        self.assertEqual(result['physicalAcceptance'], 'not-assessed')
        self.assertFalse(result['nativeProcessesStarted'])
        self.assertFalse(result['originalDeliveryVerified'])
        self.assertEqual(result['predeclaredRecordingSeconds'], 30)
        self.assertEqual(result['maximumSessionSeconds'], 120)
        self.assertNotIn('--h11-input', result['prospectiveSourceArgv'])

    def test_h11_plan_predeclares_all_recorders_and_source_without_native_execution(self):
        mac_plan, linux_plan = self.base / 'h11-mac.json', self.base / 'linux.json'
        mac = {**self.mac, 'recordingProfile': 'h11'}
        module.atomic_json(mac_plan, mac)
        module.atomic_json(linux_plan, self.linux)
        args = SimpleNamespace(mac_plan=mac_plan, linux_plan=linux_plan, source=None,
                               production_original_input=True, validate_only=True, output=self.base / 'h11-result')
        with patch.object(module.subprocess, 'Popen', side_effect=AssertionError('Native process launched')), \
                patch.object(module.subprocess, 'run', side_effect=AssertionError('External command launched')), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(module.run(args), 0)
        result = module.read_json(args.output / 'result.json')
        self.assertEqual(result['recordingProfile'], 'h11')
        self.assertEqual(result['predeclaredRecordingSeconds'], 120)
        self.assertEqual(result['maximumSessionSeconds'], 150)
        self.assertIn('--h11-input', result['prospectiveSourceArgv'])
        self.assertEqual(result['physicalAcceptance'], 'not-assessed')
        self.assertFalse(result['nativeProcessesStarted'])
        for role in ('core', 'host', 'native'):
            config = module.read_json(args.output / 'evidence' / (role + '.json'))
            self.assertEqual(config['durationMs'], 120000)
        for invalid in ('h12', '', None, 120, True, []):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(RuntimeError, 'recording profile'):
                module.validate_plans({**self.mac, 'recordingProfile': invalid}, self.linux, self.base, production=True)
        legacy = {**mac, 'version': 1, 'originalInputEnabled': False}
        linux = {k: v for k, v in self.linux.items() if k not in ('routeMode', 'macOriginalInputConfigured')}
        linux.update(version=2, controlProtocol=2)
        with self.assertRaisesRegex(RuntimeError, 'Legacy physical mode'):
            module.validate_plans(legacy, linux, self.base)

    def test_same_pid_with_new_birth_is_not_the_owned_companion(self):
        inspector = object.__new__(self.support.ProcessInspector)
        identity = {'pid': 42, 'parent': 41, 'birthMicroseconds': 100001, 'executable': '/frozen/host'}
        inspector.snapshot = Mock(return_value={**identity, 'birthMicroseconds': 100002})
        self.assertFalse(inspector.live(identity))
        inspector.snapshot.return_value = identity
        self.assertTrue(inspector.live(identity))
        self.assertEqual(self.support.ctypes.sizeof(self.support.ProcessInspector.BsdInfo), 136)

    def test_holding_requires_explicit_plan_flag_and_verified_separate_controls(self):
        mac = {**self.mac, 'holdingDisplayEnabled': True, 'separateControls': True}
        with self.assertRaises(RuntimeError):
            module.validate_plans(mac, self.linux, self.base, production=True)
        module.validate_plans(mac, self.linux, self.base, production=True, holding=True)
        self.assertTrue(self.support.host_configuration(mac, self.linux, self.base / 'restore.json')['holdingDisplay'])
        frame = {'x': 10, 'y': 20, 'width': 480, 'height': 240}
        receipt = {'windowId': 11, 'sourceWindowId': 11, 'controlWindowId': 12,
                   'controlVisible': True, 'controlFrame': frame, 'controlOriginalFrame': frame}
        self.support.source_identity(receipt, holding=True)
        for changed in ({'sourceWindowId': 10}, {'controlWindowId': 11}, {'controlVisible': False},
                        {'controlFrame': {**frame, 'x': 11}}, {'controlWindowId': 13}):
            with self.subTest(changed=changed), self.assertRaises(RuntimeError):
                self.support.source_identity({**receipt, **changed}, receipt, holding=True)

    def test_freeze_reads_only_existing_preparation_and_exact_package(self):
        old = self.base / 'legacy.json'
        module.atomic_json(old, {**self.mac, 'version': 1, 'originalInputEnabled': False})
        args = SimpleNamespace(mac_plan=old, bundle=self.bundle, source=self.paths['source'],
                               holding_display=False, output=self.base / 'production.json')
        with patch.object(module.subprocess, 'Popen', side_effect=AssertionError('Unexpected process')), \
                patch.object(module.subprocess, 'run', side_effect=AssertionError('Unexpected external command')), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.support.freeze_plan(args, module), 0)
        frozen = module.read_json(args.output)
        self.assertEqual(frozen['version'], 2)
        self.assertEqual(frozen['recordingProfile'], 'h04')
        self.assertEqual(frozen['frozenArtifacts'], self.mac['frozenArtifacts'])
        self.assertEqual(args.output.stat().st_mode & 0o777, 0o600)
        module.validate_plans(frozen, self.linux, self.base, production=True)
        with self.assertRaises(RuntimeError):
            self.support.freeze_plan(args, module)
        args.recording_profile, args.output = 'h11', self.base / 'h11-production.json'
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.support.freeze_plan(args, module), 0)
        frozen = module.read_json(args.output)
        self.assertEqual(frozen['recordingProfile'], 'h11')
        self.assertEqual(self.support.recording_times(frozen), (120, 150))


class EvidenceTests(unittest.TestCase):
    recording_seconds = 30

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.support = module.original_support()
        self.now = 100.0
        self.session = self.support.EvidenceSession(Path(self.temporary.name) / 'evidence', module.atomic_json,
                                                    now=lambda: self.now, recording_seconds=self.recording_seconds)
        self.pids = {'core': 100, 'host': 101, 'native': 102}
        self.owners = {role: {'owner': ['1'] * 11, 'identity': 'ab' * 96} for role in self.session.roles}
        self.owners['native'] = {'owner': ['102', '7'] + ['0'] * 9, 'identity': '0' * 192}
        for role in self.session.roles:
            self.session.bind(role, self.pids[role], 7 if role == 'native' else None)
            self.write(role, {'type': 'header', 'schema': 'deskflow-input-evidence-v1', 'role': role,
                              'runId': self.session.run_id, 'pid': str(self.pids[role]),
                              'clockId': role + '-independent-clock', 'queues': []})

    def write(self, role, value):
        path = self.session.readers[role].path
        with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600), 'ab') as stream:
            stream.write(json.dumps(value).encode() + b'\n')

    def ready(self, role=None, candidate=True):
        for selected in (role,) if role else self.session.roles:
            value = {'type': 'readiness', 'timeNs': '100', 'readyCandidate': candidate, 'loss': 0}
            if candidate:
                value.update(self.owners[selected])
            self.write(selected, value)

    def arms(self, role=None, accepted=True):
        for selected in (role,) if role else self.session.roles:
            self.write(selected, {'type': 'arm', 'timeNs': '200', 'accepted': accepted, **self.owners[selected]})

    def arm(self):
        self.session.start_boundary()
        self.request()
        self.ready()
        self.assertTrue(self.session.try_arm())
        self.arms()
        self.session.poll()
        self.assertTrue(self.session.armed)

    def request(self):
        module.atomic_json(self.session.record_request,
                           {'version': 1, 'runId': self.session.run_id, 'command': 'record'})
        self.assertFalse(self.session.try_arm())

    def ends(self):
        end_ns = 200 + self.recording_seconds * 1000000000
        for role in self.session.roles:
            self.write(role, {'type': 'end', 'timeNs': str(end_ns + 250000000), 'sourceEndNs': str(end_ns), 'state': 3,
                              'loss': 0, 'drained': True, 'rows': '0'})

    def test_fresh_matching_readiness_one_atomic_attempt_and_explicit_end(self):
        self.ready()
        self.session.start_boundary()
        self.assertFalse(self.session.try_arm())  # Old file contents cannot arm.
        self.request()
        self.ready()
        self.assertTrue(self.session.try_arm())
        self.assertFalse(self.session.try_arm())
        for role in self.session.roles:
            config = module.read_json(self.session.configs[role])
            self.assertEqual(config['role'], role)
            self.assertEqual(config['durationMs'], self.recording_seconds * 1000)
            self.assertEqual(self.session.configs[role].stat().st_mode & 0o777, 0o600)
            self.assertEqual(module.read_json(self.session.controls[role])['command'], 'arm')
        self.arms()
        self.session.poll()
        self.session.stop()
        self.ends()
        result = self.session.finish()
        self.assertTrue(result['recordersArmed'])
        self.assertEqual(result['recordingSeconds'], self.recording_seconds)
        self.assertEqual(result['physicalAcceptance'], 'not-assessed')
        self.assertEqual(result['nativeFieldPreservation'], 'not-assessed')

    def test_expired_arrival_false_readiness_and_mismatched_owner_do_not_arm(self):
        self.session.start_boundary()
        self.request()
        self.ready()
        self.session.poll()
        self.now += 1
        self.assertFalse(self.session.try_arm())
        self.ready()
        self.ready('host', candidate=False)
        self.assertFalse(self.session.try_arm())
        self.owners['host']['identity'] = 'cd' * 96
        self.ready()
        self.assertFalse(self.session.try_arm())
        self.assertFalse(any(path.exists() for path in self.session.controls.values()))

    def test_v2_whole_core_inventory_can_complete_the_manual_coordinator(self):
        # The coordinator verifies owned process/lifetime, not queue accounting;
        # the strict offline normalizer validates the complete v2 inventory.
        for role in self.session.roles:
            path = self.session.readers[role].path
            value = json.loads(path.read_text())
            value['schema'] = 'deskflow-input-evidence-v2'
            path.write_text(json.dumps(value) + '\n')
        self.arm()
        self.session.stop()
        self.ends()
        result = self.session.finish()
        self.assertTrue(result['recordersArmed'])
        self.assertEqual(result['physicalAcceptance'], 'not-assessed')

    def test_denied_or_changed_arm_cannot_be_retried(self):
        self.session.start_boundary()
        self.request()
        self.ready()
        self.session.try_arm()
        self.arms('core', accepted=False)
        with self.assertRaisesRegex(RuntimeError, 'denied'):
            self.session.poll()
        self.assertTrue(self.session.attempted)

    def test_candidate_change_during_the_three_independent_arm_checks_fails(self):
        self.session.start_boundary()
        self.request()
        self.ready()
        self.session.try_arm()
        self.owners['host']['identity'] = 'cd' * 96
        self.arms()
        with self.assertRaisesRegex(RuntimeError, 'changed between'):
            self.session.poll()

    def test_partial_control_publication_still_stops_all_roles(self):
        self.session.start_boundary()
        self.request()
        self.ready()
        write = self.session.atomic_json
        def fail_middle(path, value):
            if path == self.session.controls['host'] and value['command'] == 'arm':
                raise OSError('fixture write failure')
            write(path, value)
        self.session.atomic_json = fail_middle
        with self.assertRaises(OSError):
            self.session.try_arm()
        self.assertTrue(self.session.attempted)
        self.session.stop()
        self.assertTrue(all(module.read_json(path)['command'] == 'stop' for path in self.session.controls.values()))
        with self.assertRaisesRegex(RuntimeError, 'not fully armed'):
            self.session.finish()

    def test_wrong_process_or_source_is_not_recording_evidence(self):
        self.session.pids['core'] = '999'
        with self.assertRaisesRegex(RuntimeError, 'owned process'):
            self.session.poll()
        self.session.pids['core'] = '100'
        self.session.start_boundary()
        self.request()
        self.owners['native']['owner'][1] = '8'
        self.ready()
        with self.assertRaisesRegex(RuntimeError, 'different source'):
            self.session.try_arm()

    def test_missing_terminal_loss_and_undrained_tail_never_pass(self):
        self.arm()
        self.session.stop()
        with self.assertRaisesRegex(RuntimeError, 'not fully armed'):
            self.session.finish()
        self.ends()
        self.session.poll()
        self.session.readers['host'].end['loss'] = 1
        with self.assertRaisesRegex(RuntimeError, 'not fully armed'):
            self.session.finish()
        self.session.readers['host'].end['loss'] = 0
        self.session.readers['native'].end['drained'] = False
        with self.assertRaisesRegex(RuntimeError, 'not fully armed'):
            self.session.finish()

    def test_append_only_identity_and_malformed_json_fail_closed(self):
        reader = self.session.readers['native']
        self.session.poll()
        replacement = reader.path.with_suffix('.replacement')
        replacement.write_bytes(reader.path.read_bytes())
        replacement.chmod(0o600)
        replacement.replace(reader.path)
        with self.assertRaisesRegex(RuntimeError, 'inode changed'):
            reader.poll()
        core = self.session.readers['core']
        with core.path.open('ab') as stream:
            stream.write(b'{"type":"readiness","type":"arm"}\n')
        with self.assertRaisesRegex(RuntimeError, 'Duplicate'):
            core.poll()

    def test_incomplete_line_is_not_a_terminal_row(self):
        self.arm()
        self.session.stop()
        with self.session.readers['native'].path.open('ab') as stream:
            stream.write(b'{"type":"end"')
        with self.assertRaisesRegex(RuntimeError, 'Incomplete final'):
            self.session.finish()

    def test_native_absolute_duration_survives_a_delayed_coordinator_status_reply(self):
        self.arm()
        self.ends()  # Native runtime reached its own immutable duration first.
        self.now += self.recording_seconds + 10
        self.session.poll()
        self.session.stop()
        self.assertTrue(self.session.finish()['recordersEnded'])

    def test_early_stop_cannot_be_reported_as_the_declared_interval(self):
        self.arm()
        self.session.stop()
        self.ends()
        self.session.poll()
        self.session.readers['core'].end['sourceEndNs'] = '10000200'
        with self.assertRaisesRegex(RuntimeError, 'not fully armed'):
            self.session.finish()

    def test_visible_record_is_required_and_readiness_must_follow_it(self):
        self.session.start_boundary()
        self.ready()
        self.assertFalse(self.session.try_arm())
        self.assertFalse(self.session.attempted)
        self.request()
        self.assertFalse(self.session.try_arm())
        self.ready()
        self.assertTrue(self.session.try_arm())

    def test_replaced_or_wrong_record_request_cannot_arm(self):
        self.session.start_boundary()
        self.request()
        module.atomic_json(self.session.record_request,
                           {'version': 1, 'runId': self.session.run_id, 'command': 'record'})
        self.ready()
        with self.assertRaisesRegex(RuntimeError, 'replaced'):
            self.session.try_arm()

    def test_private_core_stop_checks_kernel_peer_and_exact_identity_before_write(self):
        support = self.support
        endpoint = Mock()
        endpoint.lstat.return_value = SimpleNamespace(st_mode=0o140600, st_uid=os.getuid())
        process = Mock(pid=200)
        process.poll.return_value = None
        process.wait.return_value = 0
        inspector = Mock()
        inspector.live.return_value = True
        connection = Mock()
        connection.__enter__ = Mock(return_value=connection)
        connection.__exit__ = Mock(return_value=False)
        connection.getsockopt.return_value = support.struct.pack('i', 201)
        with patch.object(support.socket, 'socket', return_value=connection):
            with self.assertRaisesRegex(RuntimeError, 'retained owned process'):
                support.stop_core(process, endpoint, inspector, {'pid': 200})
            connection.sendall.assert_not_called()
            connection.getsockopt.return_value = support.struct.pack('i', 200)
            connection.recv.return_value = b'ok\nbye\n'
            support.stop_core(process, endpoint, inspector, {'pid': 200})
            connection.sendall.assert_called_once_with(b'stop\n')
            process.terminate.assert_not_called()
            inspector.live.return_value = False
            with self.assertRaisesRegex(RuntimeError, 'exited before'):
                support.stop_core(process, endpoint, inspector, {'pid': 200})


class H11EvidenceTests(EvidenceTests):
    """The full one-shot, ownership and early-stop contracts also hold at 120 seconds."""

    recording_seconds = 120

    def test_short_h04_tail_cannot_complete_an_h11_run(self):
        self.arm()
        self.session.stop()
        self.ends()
        self.session.poll()
        self.session.readers['native'].end['sourceEndNs'] = '30000000200'
        with self.assertRaisesRegex(RuntimeError, 'not fully armed'):
            self.session.finish()


if __name__ == "__main__":
    unittest.main()
