#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Desktop lifecycle regressions; never access a real desktop or send input."""
import importlib.util
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
spec = importlib.util.spec_from_file_location('desktop', Path(__file__).with_name('physical-desktop.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DesktopRestoration(unittest.TestCase):
    def test_restore_only_test_extension_membership(self):
        uuid = module.UUID
        self.assertEqual(module.restore_membership(['dock', uuid, 'user-new'], ['dock']), ['dock', 'user-new'])
        self.assertEqual(module.restore_membership(['dock', 'user-new'], ['dock', uuid]), ['dock', uuid, 'user-new'])
        self.assertEqual(module.restore_membership(['user-new'], ['dock']), ['user-new'])

    def fixture(self, directory, *, extra=False, default=False):
        before = {'provider': {'hashes': {}}, 'extensionMembership': {
            'enabled-extensions': ['dock'], 'disabled-extensions': []},
            'extensionUserValues': {'enabled-extensions': None if default else ['dock'], 'disabled-extensions': []},
            'environment': {'DBUS_SESSION_BUS_ADDRESS': 'test-fixture-not-a-real-bus'}}
        path = Path(directory) / 'desktop-lease.json'
        plan = {'runDirectory': directory, 'token': 'owned', 'desktopAtPrepare': before}
        module.atomic(path, {'token': 'owned', 'before': before, 'restored': False})
        values = {'enabled-extensions': ['dock', module.UUID] + (['user-new'] if extra else []),
                  'disabled-extensions': []}
        store = Mock()
        store.get_strv.side_effect = lambda key: values[key]
        def set_value(key, value):
            values[key] = list(value)
            return True
        store.set_strv.side_effect = set_value
        store.reset.side_effect = lambda key: values.update({key: before['extensionMembership'][key]})
        observer = Mock()
        observer.bus.return_value = (False,)
        common = SimpleNamespace(Provider=Mock(return_value=observer), SERVICE='fixture-provider',
                                 load_gio=lambda: (SimpleNamespace(Settings=Mock()), None))
        api = SimpleNamespace(read_owned_json=lambda p: json.loads(p.read_text()))
        return path, plan, store, values, common, api

    def test_rollback_is_durable_idempotent_and_restores_default_value(self):
        with tempfile.TemporaryDirectory() as directory:
            path, plan, store, values, common, api = self.fixture(directory, default=True)
            with patch.object(module, 'settings', return_value=store), \
                    patch.object(module, 'provider_files', return_value={'hashes': {}}):
                result = module.restore(api, common, plan)
                self.assertTrue(result['desktopRestored'])
                store.reset.assert_called_once_with('enabled-extensions')
                self.assertEqual(values['enabled-extensions'], ['dock'])
                self.assertTrue(json.loads(path.read_text())['restored'])
                calls = store.set_strv.call_count
                self.assertEqual(module.restore(api, common, plan), result)
                self.assertEqual(store.set_strv.call_count, calls)

    def test_unrelated_user_edits_survive_and_prevent_clean_audit(self):
        with tempfile.TemporaryDirectory() as directory:
            _, plan, store, values, common, api = self.fixture(directory, extra=True)
            with patch.object(module, 'settings', return_value=store), \
                    patch.object(module, 'provider_files', return_value={'hashes': {}}):
                result = module.restore(api, common, plan)
            self.assertFalse(result['desktopRestored'])
            self.assertEqual(values['enabled-extensions'], ['dock', 'user-new'])

    def test_foreign_rollback_or_changed_provider_never_edits_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            _, plan, store, _, common, api = self.fixture(directory)
            with patch.object(module, 'settings', return_value=store) as opened, \
                    patch.object(module, 'provider_files', return_value={'hashes': {'changed': 'yes'}}):
                with self.assertRaisesRegex(RuntimeError, 'different session'):
                    module.restore(api, common, {**plan, 'token': 'foreign'})
                with self.assertRaisesRegex(RuntimeError, 'changed before rollback'):
                    module.restore(api, common, plan)
                opened.assert_not_called()

    def test_missing_lease_does_not_touch_desktop(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(module, 'settings') as settings:
            self.assertTrue(module.restore(None, None, {'runDirectory': directory})['desktopRestored'])
            settings.assert_not_called()

    def test_replaced_pid_never_receives_a_signal(self):
        expected = {'pid': 77, 'executable': '/private/deskflow-core', 'startTicks': '123'}
        with patch.object(module.os, 'pidfd_open', return_value=99, create=True), \
                patch.object(module.os, 'close') as close, \
                patch.object(module, 'same_process', return_value=False), \
                patch.object(module.signal, 'pidfd_send_signal', create=True) as send:
            module.stop_process(expected)
            send.assert_not_called()
            close.assert_called_once_with(99)

    def test_compositor_is_never_a_cleanup_target(self):
        with patch.object(module.os, 'pidfd_open', return_value=99, create=True), \
                patch.object(module.os, 'close'), patch.object(module, 'same_process', return_value=True), \
                patch.object(module.signal, 'pidfd_send_signal', create=True) as send:
            with self.assertRaisesRegex(RuntimeError, 'outside the two experimental roles'):
                module.stop_process({'pid': 77, 'executable': '/usr/bin/gnome-shell'})
            send.assert_not_called()

    def test_changed_desktop_denies_before_provider_enable(self):
        before = {'shell': {'pid': 42}, 'sessionId': '2'}
        with patch.object(module, 'inspect', return_value={**before, 'sessionId': 'other'}), \
                patch.object(module, 'settings') as settings, patch.object(module, 'atomic') as write:
            with self.assertRaisesRegex(RuntimeError, 'changed since preparation'):
                module.DesktopLease(None, None, {'desktopAtPrepare': before}, None, None)
            settings.assert_not_called()
            write.assert_not_called()


class ViewerExec(unittest.TestCase):
    @unittest.skipUnless(sys.platform == 'linux', 'Real Linux exec /proc identity check')
    def test_exec_preserves_pid_and_native_descriptors_and_only_changes_private_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'results').mkdir()
            first, second = run / 'first', run / 'second'
            first.write_bytes(b'W')
            second.write_bytes(b'V')
            binary = run / 'fake-viewer'
            binary.write_text('#!/usr/bin/python3\nimport json,os,sys\n'
                              'print(json.dumps(dict(pid=os.getpid(),wayland=os.read(3,1).decode(),'
                              'viewport=os.read(4,1).decode(),home=os.environ["HOME"],'
                              'bus=os.environ["DBUS_SESSION_BUS_ADDRESS"],args=sys.argv[1:])))\n')
            binary.chmod(0o700)
            wrapper = module.make_viewer_wrapper(None, {'runDirectory': directory, 'viewerExecutable': str(binary)},
                                                run, {'HOME': '/private/test', 'DBUS_SESSION_BUS_ADDRESS': 'ignored'})
            script = ('import os,sys; a=os.open(sys.argv[1],os.O_RDONLY); b=os.open(sys.argv[2],os.O_RDONLY); '
                      'assert a==3 and b==4; os.set_inheritable(a,True); os.set_inheritable(b,True); '
                      'os.execv(sys.argv[3],[sys.argv[3],"--gnome-viewport-fd=4"])')
            child = subprocess.Popen([sys.executable, '-c', script, str(first), str(second), str(wrapper)],
                                     env={**os.environ, 'DBUS_SESSION_BUS_ADDRESS': 'original-bus'},
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            _, errors = child.communicate(timeout=10)
            self.assertEqual(child.returncode, 0, errors)
            observed = json.loads((run / 'results/viewer.log').read_text())
            receipt = json.loads((run / 'viewer-process.json').read_text())
            self.assertEqual(observed['pid'], child.pid)
            self.assertEqual(receipt['pid'], child.pid)
            self.assertEqual((observed['wayland'], observed['viewport']), ('W', 'V'))
            self.assertEqual((observed['home'], observed['bus']), ('/private/test', 'original-bus'))
            self.assertEqual(observed['args'], ['--gnome-viewport-fd=4', '--quit-after', '270'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
