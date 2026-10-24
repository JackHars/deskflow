#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Profile/configuration and owned-process lifecycle checks; no desktop capture."""
import configparser
import importlib.util
import json
import logging
import os
from pathlib import Path
import plistlib
import selectors
import signal
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('seamless_profile', Path(__file__).with_name('seamless-profile.py'))
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)
real_mac_permissions = profile.mac_permissions


class Profiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cert_dir = tempfile.TemporaryDirectory(prefix='seamless-certificate-')
        root = Path(cls.cert_dir.name)
        cls.identities = {}
        for endpoint in ('mac', 'ubuntu'):
            for channel in ('core', 'window'):
                subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                                '-subj', '/CN=disposable-profile-unit-check', '-keyout', str(root / 'key.pem'),
                                '-out', str(root / 'cert.pem')], check=True, capture_output=True)
                cls.identities[endpoint, channel] = ((root / 'cert.pem').read_bytes(), (root / 'key.pem').read_bytes())

    @classmethod
    def tearDownClass(cls):
        cls.cert_dir.cleanup()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='profile-')
        self.addCleanup(self.directory.cleanup)
        self.base = Path(self.directory.name)
        lock_root = mock.patch.object(profile, 'session_lock_root', return_value=self.base / 'shared-session-lock')
        lock_root.start()
        self.addCleanup(lock_root.stop)
        support = mock.patch.object(profile, 'provider_operation', return_value={'ready': True, 'detail': 'fixture'})
        self.provider_call = support.start()
        self.addCleanup(support.stop)
        permissions = mock.patch.object(profile, 'mac_permissions', return_value={'ready': True, 'detail': 'fixture'})
        self.permissions_call = permissions.start()
        self.addCleanup(permissions.stop)
        self.package = self.base / 'Experimental.app/Contents/MacOS'
        self.package.mkdir(parents=True)
        for name in ('deskflow-core', 'deskflow-windowshare'):
            p = self.package / name
            p.write_text('#!/bin/sh\nexit 0\n')
            p.chmod(0o700)
        (self.package.parent / 'Info.plist').write_bytes(plistlib.dumps({
            'CFBundleExecutable': 'deskflow-core', 'CFBundleIdentifier': 'org.deskflow.seamless.core.experimental'}))
        self.options = {'macAddress': '192.168.1.101', 'corePort': 24870, 'windowPort': 24871,
                        'screen': 0, 'clipboardText': False}

    def initialize(self, role, root=None):
        root = self.base / role if root is None else root
        def identity(args, **kwargs):
            self.assertEqual(args[-1], '--init-identity')
            self.assertEqual(kwargs['env']['QT_QPA_PLATFORM'], 'offscreen')
            destination = Path(args[args.index('--identity') + 1])
            destination.mkdir(mode=0o700)
            cert, key = self.identities[role, destination.name.split('-')[0]]
            (destination / 'cert.pem').write_bytes(cert)
            (destination / 'key.pem').write_bytes(key)
            return subprocess.CompletedProcess(args, 0, b'created\n', b'')
        with mock.patch.object(profile.subprocess, 'run', side_effect=identity) as calls:
            result = profile.initialize(root, role, 'MacBook' if role == 'mac' else 'JackUbuntu',
                                        self.package / 'deskflow-core', self.package / 'deskflow-windowshare')
        self.assertEqual(calls.call_count, 2)
        self.assertFalse(result['inputStarted'])
        return root

    def endpoints(self):
        mac, ubuntu = self.initialize('mac'), self.initialize('ubuntu')
        profile.pair(mac, ubuntu / 'pairing.json', self.options)
        profile.pair(ubuntu, mac / 'pairing.json', self.options)
        return mac, ubuntu

    def test_setup_child_retains_lock_after_controller_releases_it(self):
        root = self.initialize('ubuntu')
        with profile.locked(root) as descriptor:
            child = subprocess.Popen([sys.executable, '-I', '-c', 'import sys;sys.stdin.buffer.read()'],
                                     stdin=subprocess.PIPE, pass_fds=(descriptor,))
        try:
            with self.assertRaisesRegex(ValueError, 'already running'):
                with profile.locked(root):
                    self.fail('inherited lock was lost')
        finally:
            child.communicate(timeout=5)
        with profile.locked(root):
            pass

    def test_provider_mutation_inherits_both_locks(self):
        root = self.initialize('ubuntu')
        def child(operation, descriptors):
            self.assertEqual(operation, 'install')
            self.assertEqual(len(descriptors), 2)
            for descriptor in descriptors:
                self.assertGreaterEqual(os.fstat(descriptor).st_size, 0)
            with self.assertRaisesRegex(ValueError, 'already running'):
                with profile.locked(root):
                    self.fail('profile lock missing')
            with self.assertRaisesRegex(ValueError, 'already running'):
                with profile.session_lock():
                    self.fail('global lock missing')
            return {'ready': False}
        self.provider_call.side_effect = child
        with mock.patch.object(profile, 'running_cores', return_value=[]):
            self.assertFalse(profile.configure_provider(root, 'install')['ready'])

    def test_provider_mutation_refuses_running_core_but_status_does_not(self):
        root = self.initialize('ubuntu')
        with mock.patch.object(profile, 'running_cores', return_value=[123]) as cores:
            self.assertTrue(profile.configure_provider(root, 'status')['ready'])
            cores.assert_not_called()
            for operation in ('install', 'enable'):
                with self.assertRaisesRegex(ValueError, 'Stop Deskflow'):
                    profile.configure_provider(root, operation)
        self.assertEqual(self.provider_call.call_args_list, [mock.call('status')])

    def test_provider_setup_respects_active_profile_lock_and_platform(self):
        root = self.initialize('ubuntu')
        with profile.locked(root), self.assertRaisesRegex(ValueError, 'already running'):
            profile.configure_provider(root, 'install')
        mac = self.initialize('mac')
        with self.assertRaisesRegex(ValueError, 'Ubuntu computer'):
            profile.configure_provider(mac, 'status')
        self.provider_call.assert_not_called()

    @unittest.skipUnless(sys.platform == 'linux', 'Ubuntu launch preflight')
    def test_launch_rechecks_provider_before_starting_core(self):
        _, root = self.endpoints()
        self.provider_call.return_value = {'ready': False, 'detail': 'GNOME needs a new login'}
        with mock.patch.object(profile, 'running_cores', return_value=[]), \
             mock.patch.dict(os.environ, {'XDG_SESSION_TYPE': 'wayland', 'DBUS_SESSION_BUS_ADDRESS': 'unix:path=/unused'}), \
             mock.patch.object(profile.subprocess, 'Popen', side_effect=AssertionError('launched')), \
             self.assertRaisesRegex(ValueError, 'new login'):
            profile.run(root)
        self.provider_call.assert_called_once_with('status')

    def test_mac_permission_probe_uses_both_packaged_identities(self):
        root = self.initialize('mac')
        value = profile.load(root)
        core = {'supported': True, 'accessibility': True, 'inputStarted': False}
        host = {'supported': True, 'accessibility': True, 'screenCapture': True,
                'targetedPointer': True, 'exactWindowIdentity': True, 'holdingDisplay': True}
        def output(command, **kwargs):
            self.assertEqual(kwargs['pass_fds'], ())
            self.assertNotIn('DYLD_INSERT_LIBRARIES', kwargs['env'])
            data = core if command[0] == value['core'] else host
            return subprocess.CompletedProcess(command, 0, json.dumps(data).encode(), b'')
        with mock.patch.object(profile.sys, 'platform', 'darwin'), \
             mock.patch.object(profile.subprocess, 'run', side_effect=output) as calls:
            result = real_mac_permissions(value, root)
            self.assertTrue(result['ready'])
            self.assertFalse(result['inputStarted'])
            self.assertEqual([c.args[0] for c in calls.call_args_list],
                             [[value['core'], '--windowshare-permissions'], [value['windowshare'], '--probe']])
            for data, field in [(core, 'accessibility'), (host, 'accessibility'), (host, 'screenCapture'),
                                (host, 'targetedPointer'), (host, 'exactWindowIdentity'), (host, 'holdingDisplay')]:
                data[field] = False
                result = real_mac_permissions(value, root)
                self.assertFalse(result['ready'])
                self.assertEqual(result['action'], 'request' if field in ('accessibility', 'screenCapture') else '')
                data[field] = True
            core['inputStarted'] = True
            with self.assertRaisesRegex(ValueError, 'unexpectedly started input'):
                real_mac_permissions(value, root)
            core['inputStarted'] = False
            host['screenCapture'] = 'true'
            with self.assertRaisesRegex(ValueError, 'invalid result'):
                real_mac_permissions(value, root)

    def test_mac_permission_request_is_not_readiness(self):
        root = self.initialize('mac')
        value = profile.load(root)
        results = [dict(supported=True, accessibility=True, inputStarted=False),
                   dict(supported=True, accessibility=True, screenCapture=True, targetedPointer=True,
                        exactWindowIdentity=True, holdingDisplay=True)]
        with mock.patch.object(profile.sys, 'platform', 'darwin'), \
             mock.patch.object(profile.subprocess, 'run', side_effect=[
                 subprocess.CompletedProcess([], 0, json.dumps(item).encode(), b'') for item in results]) as calls:
            result = real_mac_permissions(value, root, 'request', (21, 22))
        self.assertFalse(result['ready'])
        self.assertFalse(result['inputStarted'])
        self.assertEqual([c.args[0][1] for c in calls.call_args_list],
                         ['--windowshare-request-permissions', '--request-permissions'])
        self.assertTrue(all(c.kwargs['pass_fds'] == (21, 22) for c in calls.call_args_list))

    def test_mac_permission_request_retains_locks_and_refuses_running_core(self):
        root = self.initialize('mac')
        with mock.patch.object(profile, 'running_cores', return_value=[123]) as cores:
            self.assertTrue(profile.configure_mac_permissions(root, 'status')['ready'])
            cores.assert_not_called()
            with self.assertRaisesRegex(ValueError, 'Stop Deskflow'):
                profile.configure_mac_permissions(root, 'request')
        def child(value, directory, operation, fds):
            self.assertEqual(directory, root)
            self.assertEqual(operation, 'request')
            self.assertEqual(len(fds), 2)
            for fd in fds:
                os.fstat(fd)
            with self.assertRaisesRegex(ValueError, 'already running'):
                with profile.locked(root):
                    self.fail('missing profile lock')
            with self.assertRaisesRegex(ValueError, 'already running'):
                with profile.session_lock():
                    self.fail('missing session lock')
            return {'ready': False}
        self.permissions_call.side_effect = child
        with mock.patch.object(profile, 'running_cores', return_value=[]):
            self.assertFalse(profile.configure_mac_permissions(root, 'request')['ready'])
        ubuntu = self.initialize('ubuntu')
        with self.assertRaisesRegex(ValueError, 'Mac computer'):
            profile.configure_mac_permissions(ubuntu, 'status')

    def test_launch_rechecks_mac_permissions_before_starting_core(self):
        root, _ = self.endpoints()
        self.permissions_call.return_value = {'ready': False, 'detail': 'Allow Screen Recording'}
        with mock.patch.object(profile.sys, 'platform', 'darwin'), \
             mock.patch.object(profile, 'running_cores', return_value=[]), \
             mock.patch.object(profile.subprocess, 'Popen', side_effect=AssertionError('launched')), \
             self.assertRaisesRegex(ValueError, 'Screen Recording'):
            profile.run(root)
        self.permissions_call.assert_called_once()

    def test_top_edge_is_only_export_trigger(self):
        mac, ubuntu = self.endpoints()
        server = (mac / 'config/server.conf').read_text()
        self.assertIn('MacBook:\n        up = JackUbuntu', server)
        self.assertIn('JackUbuntu:\n        down = MacBook', server)
        self.assertIn('switchToScreen(MacBook)', server)
        host = profile.read_json(mac / 'config/host.json')
        viewer = profile.read_json(ubuntu / 'config/provider.json')
        self.assertTrue(host['holdingDisplay'])
        self.assertNotIn('exportWindow', viewer)
        self.assertFalse(host['shareClipboardText'])
        self.assertFalse(viewer['shareClipboardText'])
        self.assertEqual(host['inputPeerPairs'], [{'deskflowPeerPin': profile.read_json(ubuntu / 'pairing.json')['coreFingerprint'],
                                                 'windowSharePeerPin': profile.read_json(ubuntu / 'pairing.json')['windowFingerprint']}])
        self.assertEqual(host['peerPin'], profile.read_json(ubuntu / 'pairing.json')['windowFingerprint'])
        self.assertEqual(viewer['peerPin'], profile.read_json(mac / 'pairing.json')['windowFingerprint'])
        self.assertNotEqual(host['peerPin'], viewer['peerPin'])
        settings = configparser.ConfigParser()
        settings.read(mac / 'config/core.ini')
        self.assertEqual(settings['core']['computername'], 'MacBook')
        self.assertEqual(settings['security']['checkpeerfingerprints'], 'true')
        self.assertEqual(settings['server']['enableclipboard'], 'false')
        self.assertEqual((mac / 'config/tls/trusted-clients').read_text(),
                         'v2:sha256:' + profile.read_json(ubuntu / 'pairing.json')['coreFingerprint'] + '\n')

    def test_public_pairing_file_has_no_private_material(self):
        root = self.initialize('mac')
        public = profile.read_json(root / 'pairing.json')
        self.assertEqual(set(public), {'schema', 'role', 'name', 'coreFingerprint', 'windowFingerprint'})
        self.assertEqual(public['name'], 'MacBook')
        self.assertEqual(root.stat().st_mode & 0o777, 0o700)
        self.assertEqual((root / 'profile.json').stat().st_mode & 0o777, 0o600)

    def test_inspect_uses_validated_public_identity_and_never_launches(self):
        root = self.initialize('mac')
        # Export comes from the validated profile, not a mutable copied file.
        (root / 'pairing.json').write_text('untrusted replacement')
        with mock.patch.object(profile.subprocess, 'Popen', side_effect=AssertionError('launched')):
            result = profile.inspect_profile(root)
        self.assertEqual(result['pairing'], profile.load(root)['local'])
        self.assertFalse(result['paired'])

    def test_long_folder_rejected_before_creating_identity(self):
        for leaf in ('x' * 104, '\u00e9' * 52):
            with self.subTest(leaf=leaf), mock.patch.object(profile.subprocess, 'run',
                    side_effect=AssertionError('identity process launched')) as create:
                root = self.base / leaf
                with self.assertRaisesRegex(ValueError, 'shorter setup folder'):
                    profile.initialize(root, 'ubuntu', 'JackUbuntu', self.package / 'deskflow-core',
                                       self.package / 'deskflow-windowshare')
                create.assert_not_called()
                self.assertFalse(root.exists())

    def test_folder_limit_uses_resolved_path_and_socket_suffix(self):
        prefix = len(os.fsencode(self.base.resolve() / 'runtime/deskflow-core')) + 1
        allowed = self.base / ('x' * (103 - prefix))
        self.initialize('ubuntu', allowed)
        self.assertTrue(profile.inspect_profile(allowed)['packageReady'])
        rejected = self.base / ('y' * (104 - prefix))
        with mock.patch.object(profile.subprocess, 'run', side_effect=AssertionError('identity process launched')) as create, \
             self.assertRaisesRegex(ValueError, 'shorter setup folder'):
            profile.initialize(rejected, 'ubuntu', 'JackUbuntu', self.package / 'deskflow-core',
                               self.package / 'deskflow-windowshare')
        create.assert_not_called()
        self.assertFalse(rejected.exists())

        parent = self.base / ('z' * 100)
        parent.mkdir()
        alias = self.base / 'link'
        alias.symlink_to(parent, target_is_directory=True)
        with mock.patch.object(profile.subprocess, 'run', side_effect=AssertionError('identity process launched')) as create, \
             self.assertRaisesRegex(ValueError, 'shorter setup folder'):
            profile.initialize(alias / 'new', 'ubuntu', 'JackUbuntu', self.package / 'deskflow-core',
                               self.package / 'deskflow-windowshare')
        create.assert_not_called()
        self.assertFalse((parent / 'new').exists())

    def test_existing_long_folder_cannot_report_ready_or_produce_launch_plan(self):
        root, _ = self.endpoints()
        legacy = self.base / ('legacy-' + 'x' * 104)
        shutil.copytree(root, legacy)
        legacy.chmod(0o700)
        value = profile.read_json(legacy / 'profile.json')
        connection = profile.read_json(legacy / 'connection.json')
        # Retain a complete old-version setup whose absolute paths really
        # target this long folder, rather than a broken moved configuration.
        files = profile.configuration(legacy, value, connection['peer'], connection['options'])
        for name, content in files.items():
            (profile.write_json if isinstance(content, dict) else profile.write)(legacy / 'config' / name, content)
        connection['files'] = {name: profile.sha(legacy / 'config' / name) for name in files}
        profile.write_json(legacy / 'connection.json', connection)
        before = {str(p.relative_to(legacy)): p.read_bytes() for p in legacy.rglob('*') if p.is_file()}
        with mock.patch.object(profile.subprocess, 'Popen', side_effect=AssertionError('launched')), \
             mock.patch.object(profile, 'running_cores', side_effect=AssertionError('desktop inspected')):
            for action in (lambda: profile.inspect_profile(legacy), lambda: profile.run(legacy, dry_run=True)):
                with self.subTest(action=action), self.assertRaisesRegex(ValueError, 'shorter setup folder'):
                    action()
        for name, data in before.items():
            self.assertEqual((legacy / name).read_bytes(), data)

    def replacement_package(self):
        target = self.base / 'Updated.app/Contents/MacOS'
        shutil.copytree(self.package.parent, target.parent)
        for name in ('deskflow-core', 'deskflow-windowshare'):
            (target / name).write_text('#!/bin/sh\n# new package\nexit 0\n')
        return target.resolve()

    def test_package_update_preserves_pairing_keys_and_recovery_on_both_roles(self):
        endpoints = self.endpoints()
        target = self.replacement_package()
        for root in endpoints:
            original = profile.load(root)
            old_plan = profile.launch_plan(root)
            retained = ['pairing.json', 'core-identity/key.pem', 'core-identity/cert.pem',
                        'window-identity/key.pem', 'window-identity/cert.pem', 'restore.json']
            (root / 'restore.json').write_text('retained recovery journal')
            before = {name: (root / name).read_bytes() for name in retained}
            with mock.patch.object(profile.subprocess, 'Popen', side_effect=AssertionError('launched')):
                result = profile.update(root, target / 'deskflow-core', target / 'deskflow-windowshare')
            self.assertFalse(result['inputStarted'])
            updated = profile.load(root)
            self.assertEqual(updated['local'], original['local'])
            self.assertEqual(updated['schema'], profile.UPDATED_SCHEMA)
            self.assertEqual(updated['connection']['peer'], profile.read_json(root / 'connection.json')['peer'])
            self.assertEqual(profile.read_json(root / 'profile.previous.json'), original)
            self.assertEqual({name: (root / name).read_bytes() for name in retained}, before)
            plan = profile.launch_plan(root)
            self.assertEqual(plan['command'][0], str(target / 'deskflow-core'))
            self.assertEqual(plan['journal'], old_plan['journal'])
            directory = profile.configuration_root(root, updated)
            self.assertIn(str(directory / 'core.ini'), plan['command'])
            if original['local']['role'] == 'ubuntu':
                self.assertEqual(profile.read_json(directory / 'provider.json')['viewerExecutable'],
                                 str(target / 'deskflow-windowshare'))
            self.assertTrue(profile.inspect_profile(root, target / 'deskflow-core',
                                                    target / 'deskflow-windowshare')['packageReady'])

    def test_update_works_after_old_package_removed_and_inspect_never_approves_new_bytes(self):
        root = self.initialize('mac')
        target = self.replacement_package()
        shutil.rmtree(self.package.parent)
        state = profile.inspect_profile(root, target / 'deskflow-core', target / 'deskflow-windowshare')
        self.assertFalse(state['packageReady'])
        with self.assertRaises(OSError):
            profile.load(root)
        profile.update(root, target / 'deskflow-core', target / 'deskflow-windowshare')
        self.assertTrue(profile.inspect_profile(root)['packageReady'])
        # Updating an unpaired setup still permits its later first pairing.
        ubuntu = self.base / 'peer.json'
        public = {**profile.load(root)['local'], 'role': 'ubuntu', 'name': 'Ubuntu'}
        profile.write_json(ubuntu, public)
        profile.pair(root, ubuntu, self.options)
        self.assertEqual(profile.launch_plan(root)['peer'], 'Ubuntu')

    def test_option_update_is_atomic_and_keeps_exact_peer(self):
        _, root = self.endpoints()
        peer = profile.validate_connection(root, profile.load(root))['peer']
        profile.update(root, self.package / 'deskflow-core', self.package / 'deskflow-windowshare',
                       {'macAddress': '192.168.1.102', 'screen': 1, 'clipboardText': True})
        value = profile.load(root)
        self.assertEqual(value['connection']['peer'], peer)
        options = value['connection']['options']
        self.assertEqual(options['macAddress'], '192.168.1.102')
        self.assertEqual(options['screen'], 1)
        self.assertTrue(options['clipboardText'])
        profile.launch_plan(root)
        # A second update selects another complete generation, retaining the first.
        old_directory = profile.configuration_root(root, value)
        profile.update(root, self.package / 'deskflow-core', self.package / 'deskflow-windowshare', {'screen': 2})
        self.assertTrue(old_directory.is_dir())
        self.assertNotEqual(old_directory, profile.configuration_root(root, profile.load(root)))

    def test_failed_snapshot_selection_keeps_previous_launch_and_identity(self):
        root, _ = self.endpoints()
        target = self.replacement_package()
        before = (root / 'profile.json').read_bytes()
        old_plan = profile.launch_plan(root)
        original = profile.os.replace
        def reject_selection(source, destination):
            if Path(destination) == root / 'profile.json':
                raise OSError('simulated selection failure')
            return original(source, destination)
        with mock.patch.object(profile.os, 'replace', side_effect=reject_selection):
            with self.assertRaisesRegex(OSError, 'selection failure'):
                profile.update(root, target / 'deskflow-core', target / 'deskflow-windowshare')
        self.assertEqual((root / 'profile.json').read_bytes(), before)
        self.assertEqual(profile.launch_plan(root), old_plan)

    def test_update_cannot_bypass_lock_tampered_configuration_identity_or_package(self):
        root, other = self.endpoints()
        target = self.replacement_package()
        args = (root, target / 'deskflow-core', target / 'deskflow-windowshare')
        before = (root / 'profile.json').read_bytes()
        with profile.locked(root), self.assertRaisesRegex(ValueError, 'already running'):
            profile.update(*args)
        configuration = root / 'config/server.conf'
        original = configuration.read_bytes()
        configuration.write_bytes(original + b'# changed\n')
        with self.assertRaisesRegex(ValueError, 'configuration changed'):
            profile.update(*args)
        configuration.write_bytes(original)
        key = root / 'window-identity/key.pem'
        original = key.read_bytes()
        key.write_bytes((other / 'window-identity/key.pem').read_bytes())
        with self.assertRaises(profile.ssl.SSLError):
            profile.update(*args)
        key.write_bytes(original)
        (target.parent / 'Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable': 'wrong'}))
        with self.assertRaisesRegex(ValueError, 'separately packaged'):
            profile.update(*args)
        self.assertEqual((root / 'profile.json').read_bytes(), before)

    def test_update_rejects_generation_symlink_and_malformed_generation(self):
        root = self.initialize('mac')
        outside = self.base / 'outside'
        outside.mkdir(mode=0o700)
        (root / 'configurations').symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'private directory'):
            profile.update(root, self.package / 'deskflow-core', self.package / 'deskflow-windowshare')
        self.assertEqual(list(outside.iterdir()), [])
        (root / 'configurations').unlink()
        profile.update(root, self.package / 'deskflow-core', self.package / 'deskflow-windowshare')
        value = profile.load(root)
        for generation in ['../config', '/tmp/escape', 'a' * 33]:
            with self.subTest(generation=generation), self.assertRaisesRegex(ValueError, 'generation'):
                profile.validate_profile(root, {**value, 'configuration': generation})

    def test_controller_closed_before_launch_does_not_start_core(self):
        mac, ubuntu = self.endpoints()
        root = mac if sys.platform == 'darwin' else ubuntu
        read_end, write_end = os.pipe()
        os.close(write_end)
        self.addCleanup(os.close, read_end)
        with mock.patch.object(profile, 'running_cores', return_value=[]), \
             mock.patch.dict(os.environ, {'XDG_SESSION_TYPE': 'wayland', 'DBUS_SESSION_BUS_ADDRESS': 'unix:path=/unused'}), \
             mock.patch.object(profile.subprocess, 'Popen', side_effect=AssertionError('launched')):
            result = profile.run(root, control_fd=read_end)
        self.assertTrue(result['cancelled'])
        self.assertFalse(result['inputStarted'])
        self.assertEqual(result['stopReason'], 'controller-closed-before-start')

    def test_dry_run_never_launches_or_checks_desktop(self):
        mac, ubuntu = self.endpoints()
        with mock.patch.object(profile.subprocess, 'Popen', side_effect=AssertionError('launched')), \
             mock.patch.object(profile, 'running_cores', side_effect=AssertionError('desktop inspected')):
            for root in (mac, ubuntu):
                plan = profile.run(root, dry_run=True)
                self.assertFalse(plan['automaticExport'])
                self.assertIn('--new-instance', plan['command'])
                self.assertNotIn('--export-window', ' '.join(plan['command']))

    def test_existing_session_is_preserved(self):
        mac, ubuntu = self.endpoints()
        root = mac if sys.platform == 'darwin' else ubuntu
        with mock.patch.object(profile, 'running_cores', return_value=[123]), \
             mock.patch.object(profile.subprocess, 'Popen', side_effect=AssertionError('launched')):
            with self.assertRaisesRegex(ValueError, 'Stop the regular'):
                profile.run(root)

    def test_profile_lock_blocks_pair_and_run(self):
        root = self.initialize('mac')
        with profile.locked(root):
            with self.assertRaisesRegex(ValueError, 'already running'):
                with profile.locked(root):
                    self.fail('second lock acquired')

    def test_two_different_profiles_cannot_start_competing_sessions(self):
        mac, ubuntu = self.endpoints()
        root = mac if sys.platform == 'darwin' else ubuntu
        with profile.session_lock(), \
             mock.patch.object(profile, 'running_cores', side_effect=AssertionError('lock bypassed')):
            with self.assertRaisesRegex(ValueError, 'Another window-sharing session'):
                profile.run(root)

    def test_init_never_overwrites(self):
        root = self.initialize('mac')
        before = (root / 'profile.json').read_bytes()
        with self.assertRaises(FileExistsError):
            profile.initialize(root, 'mac', 'NewName', self.package / 'deskflow-core', self.package / 'deskflow-windowshare')
        self.assertEqual(before, (root / 'profile.json').read_bytes())

    def test_pair_never_silently_replaces_peer(self):
        mac, ubuntu = self.endpoints()
        before = (mac / 'connection.json').read_bytes()
        with self.assertRaisesRegex(ValueError, 'already paired'):
            profile.pair(mac, ubuntu / 'pairing.json', self.options)
        self.assertEqual(before, (mac / 'connection.json').read_bytes())

    def test_changed_binary_or_config_is_rejected(self):
        mac, _ = self.endpoints()
        path = mac / 'config/server.conf'
        original = path.read_text()
        path.write_text(original + '# edited\n')
        with self.assertRaisesRegex(ValueError, 'configuration changed'):
            profile.launch_plan(mac)
        path.write_text(original)
        (self.package / 'deskflow-core').write_text('#!/bin/sh\nexit 1\n')
        with self.assertRaisesRegex(ValueError, 'binaries changed'):
            profile.launch_plan(mac)

    def test_replaced_core_tls_file_is_rejected_before_launch(self):
        mac, ubuntu = self.endpoints()
        (mac / 'config/tls/deskflow.pem').write_bytes((ubuntu / 'config/tls/deskflow.pem').read_bytes())
        with self.assertRaisesRegex(ValueError, 'Core TLS identity has changed'):
            profile.launch_plan(mac)

    def test_window_private_key_must_match_its_public_pairing_identity(self):
        mac, ubuntu = self.endpoints()
        (mac / 'window-identity/key.pem').write_bytes((ubuntu / 'window-identity/key.pem').read_bytes())
        with self.assertRaises(profile.ssl.SSLError):
            profile.launch_plan(mac)

    def test_duplicate_fields_and_oversize_files_rejected(self):
        path = self.base / 'invalid.json'
        for raw in ['{"name":"a","name":"b"}', ' ' * (profile.MAX_JSON + 1)]:
            path.write_text(raw)
            with self.assertRaises(ValueError):
                profile.read_json(path)

    def test_injected_names_and_invalid_peers_rejected(self):
        root = self.initialize('mac')
        value = profile.read_json(root / 'pairing.json')
        for field, candidate in [('name', 'Mac\nend'), ('name', 'Mac:bad'), ('role', 'windows'),
                                 ('coreFingerprint', '0' * 64), ('windowFingerprint', '$(echo bad)')]:
            with self.subTest(field=field, candidate=candidate), self.assertRaises(ValueError):
                profile.public_identity({**value, field: candidate})

    def test_connection_limits(self):
        for key, value in [('macAddress', '127.0.0.1'), ('macAddress', '0.0.0.0'), ('corePort', True),
                           ('corePort', 24871), ('windowPort', 65536), ('screen', -1), ('clipboardText', 1)]:
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                profile.connection_options({**self.options, key: value})

    def test_ambient_input_plugin_overrides_removed(self):
        with mock.patch.dict(os.environ, {'DYLD_INSERT_LIBRARIES': '/bad', 'QT_PLUGIN_PATH': '/bad',
                                        'QT_QPA_PLATFORM': 'xcb', 'DBUS_SESSION_BUS_ADDRESS': 'unix:path=/user/bus'}):
            env = profile.clean_environment(self.base)
        self.assertNotIn('DYLD_INSERT_LIBRARIES', env)
        self.assertNotIn('QT_PLUGIN_PATH', env)
        self.assertNotIn('QT_QPA_PLATFORM', env)
        self.assertEqual(env['DBUS_SESSION_BUS_ADDRESS'], 'unix:path=/user/bus')
        self.assertEqual(env['TMPDIR'], str(self.base / 'runtime'))

    def test_session_records_startup_failure_and_saved_output(self):
        (self.package / 'deskflow-core').write_text('#!/bin/sh\necho intentional-startup-failure\nexit 7\n')
        mac, ubuntu = self.endpoints()
        root = mac if sys.platform == 'darwin' else ubuntu
        with mock.patch.object(profile, 'running_cores', return_value=[]), \
             mock.patch.dict(os.environ, {'XDG_SESSION_TYPE': 'wayland', 'DBUS_SESSION_BUS_ADDRESS': 'unix:path=/unused'}), \
             mock.patch('builtins.print'):
            result = profile.run(root)
        self.assertEqual(result['exitCode'], 7)
        self.assertFalse(result['running'])
        self.assertEqual(profile.read_json(root / 'session.json'), result)
        self.assertIn('intentional-startup-failure', (root / 'logs/session.log').read_text())

    def test_session_stop_is_owned_and_keeps_recovery_journal(self):
        self.stoppable_session()

    def test_controller_disappearance_stops_the_owned_core(self):
        self.stoppable_session(controller=True)

    def stoppable_session(self, controller=False):
        (self.package / 'deskflow-core').write_text('#!' + sys.executable + '\n' + r'''
import os,socket
with socket.socket(socket.AF_UNIX) as server:
 server.bind(os.path.join(os.environ['TMPDIR'],'deskflow-core'));server.listen(1)
 print('owned-fixture-ready',flush=True)
 client,_=server.accept()
 with client:
  assert client.recv(64)==b'stop\n'
 print('owned-fixture-stopped',flush=True)
''')
        mac, ubuntu = self.endpoints()
        root = mac if sys.platform == 'darwin' else ubuntu
        (root / 'restore.json').write_text('retained-recovery-evidence')
        read_end, write_end = os.pipe() if controller else (None, None)
        if controller:
            self.addCleanup(os.close, read_end)
        class StopOnReadiness(logging.Handler):
            def emit(self, record):
                if record.getMessage() == 'owned-fixture-ready':
                    if controller:
                        os.close(write_end)
                    else:
                        os.kill(os.getpid(), signal.SIGHUP)
        logger, handler = logging.getLogger('seamless-session'), StopOnReadiness()
        logger.addHandler(handler)
        self.addCleanup(logger.removeHandler, handler)
        before = signal.getsignal(signal.SIGHUP)
        with mock.patch.object(profile, 'running_cores', return_value=[]), \
             mock.patch.dict(os.environ, {'XDG_SESSION_TYPE': 'wayland', 'DBUS_SESSION_BUS_ADDRESS': 'unix:path=/unused'}), \
             mock.patch('builtins.print'):
            result = profile.run(root, control_fd=read_end)
        self.assertEqual(signal.getsignal(signal.SIGHUP), before)
        self.assertEqual(result['exitCode'], 0)
        self.assertTrue(result['stopRequested'])
        self.assertEqual(result['stop']['method'], 'private-ipc')
        if controller:
            self.assertEqual(result['stopReason'], 'controller-closed')
        self.assertFalse(result['running'])
        self.assertIn('owned-fixture-stopped', (root / 'logs/session.log').read_text())
        self.assertEqual((root / 'restore.json').read_text(), 'retained-recovery-evidence')

    def test_stop_drains_verbose_owned_child_without_deadlock(self):
        endpoint = self.base / 'ipc'
        child = subprocess.Popen([sys.executable, '-u', '-c', r'''
import socket,sys
with socket.socket(socket.AF_UNIX) as server:
 server.bind(sys.argv[1]);server.listen(1);print('ready',flush=True)
 client,_=server.accept()
 with client:
  assert client.recv(64)==b'stop\n'
 # More than pipe capacity: shutdown must keep draining, not wait while blocked.
 sys.stdout.write(('shutdown detail\n'*80000));sys.stdout.flush()
''', str(endpoint)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.addCleanup(lambda: child.poll() is None and (child.kill(), child.wait()))
        with selectors.DefaultSelector() as selector:
            selector.register(child.stdout, selectors.EVENT_READ)
            self.assertTrue(selector.select(timeout=5))
            self.assertEqual(child.stdout.readline(), b'ready\n')
        logger = logging.getLogger('profile-owned-child-test')
        logger.addHandler(logging.NullHandler())
        output = profile.ProcessLog(child, logger)
        outcome = profile.stop_child(child, endpoint, output)
        self.assertEqual(outcome['method'], 'private-ipc')
        self.assertEqual(child.returncode, 0)
        child.stdout.close()


if __name__ == '__main__':
    unittest.main()
