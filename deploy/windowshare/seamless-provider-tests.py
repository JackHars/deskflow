#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Disposable provider setup and readiness checks; no desktop connection."""
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('support', Path(__file__).with_name('seamless-provider.py'))
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)


class Support(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='provider-setup-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.source = self.root / 'source'
        self.source.mkdir()
        for name in support.RUNTIME_FILES:
            (self.source / name).write_text(name)
        (self.source / 'metadata.json').write_text(json.dumps({'uuid': support.UUID, 'shell-version': ['50']}))
        self.target = self.root / 'extensions' / support.UUID
        self.target.parent.mkdir()
        shutil.copytree(self.source, self.target)
        self.info = dict(uuid=support.UUID, path=str(self.target), enabled=True, state=1, error='')
        self.caps = dict(providerRevision=support.revision(support.runtime_files(self.source)),
                         backend='MetaBackendNative', nativeHitTesting=True, exactWaylandClientBinding=True,
                         perWindowAssociation=True, routedPointsObservation=True, inputPreparation=True,
                         inputDrainSegment=True, windowDragPlacement=True, inputDrainOrderingContract='reviewed-fixture')
        self.desktop = mock.Mock()
        self.desktop.info.side_effect = lambda: self.info.copy()
        self.desktop.capabilities.side_effect = lambda: (1, '50.1', self.caps.copy())
        self.desktop.shell.return_value = (True,)

    def status(self):
        return support.operate('status', self.source, self.target, self.desktop)

    def test_ready_is_read_only(self):
        self.assertTrue(self.status()['ready'])
        self.desktop.shell.assert_not_called()
        self.assertEqual(list(self.target.parent.iterdir()), [self.target])

    def test_missing_installation(self):
        shutil.rmtree(self.target)
        self.info = {}
        self.assertEqual(self.status()['action'], 'install')
        self.desktop.capabilities.assert_not_called()

    def test_undiscovered_files_need_login(self):
        self.info = {}
        self.assertEqual(self.status()['code'], 'not-discovered')

    def test_disabled_requires_explicit_enable(self):
        for state in (2, 6):
            self.info.update(enabled=False, state=state)
            self.assertEqual(self.status()['action'], 'enable')
            self.desktop.shell.assert_not_called()

    def test_enable_is_not_readiness_or_input(self):
        self.info.update(enabled=False, state=2)
        result = support.operate('enable', self.source, self.target, self.desktop)
        self.assertFalse(result['ready'])
        self.assertFalse(result['inputStarted'])
        self.desktop.shell.assert_called_once_with('EnableExtension')

    def test_enable_refusal(self):
        self.info.update(enabled=False, state=2)
        self.desktop.shell.return_value = (False,)
        with self.assertRaisesRegex(ValueError, 'did not enable'):
            support.operate('enable', self.source, self.target, self.desktop)

    def test_old_loaded_module_is_not_ready(self):
        self.caps['providerRevision'] = 'a' * 64
        self.assertEqual(self.status()['code'], 'restart-needed')

    def test_old_installation_cannot_enable(self):
        (self.target / 'logic.js').write_text('older')
        self.info.update(enabled=False, state=2)
        self.assertEqual(self.status()['action'], 'install')
        with self.assertRaisesRegex(ValueError, 'updating'):
            support.operate('enable', self.source, self.target, self.desktop)
        self.desktop.shell.assert_not_called()

    def test_enabled_old_installation_requires_disable(self):
        (self.target / 'logic.js').write_text('older')
        self.assertEqual(self.status()['action'], '')
        with self.assertRaisesRegex(ValueError, 'Turn off'):
            support.install(self.source, self.target, self.desktop)
        self.assertEqual((self.target / 'logic.js').read_text(), 'older')

    def test_capability_absence_refuses_start(self):
        for key in ('nativeHitTesting', 'exactWaylandClientBinding', 'perWindowAssociation',
                    'routedPointsObservation', 'inputPreparation', 'inputDrainSegment', 'windowDragPlacement'):
            with self.subTest(key=key):
                self.caps[key] = False
                self.assertEqual(self.status()['code'], 'unsupported-runtime')
                self.caps[key] = True
        self.caps['inputDrainOrderingContract'] = ''
        self.assertFalse(self.status()['ready'])

    def test_backend_version_and_error(self):
        self.caps['backend'] = 'MetaBackendX11'
        self.assertEqual(self.status()['code'], 'unsupported-desktop')
        self.caps['backend'] = 'MetaBackendNative'
        self.desktop.capabilities.side_effect = lambda: (1, '51.0', self.caps.copy())
        self.assertEqual(self.status()['code'], 'unsupported-desktop')
        self.info['error'] = 'Cannot load'
        self.assertEqual(self.status()['code'], 'extension-error')

    def test_wrong_discovered_path_is_not_ready(self):
        self.info['path'] = '/other/extension'
        with self.assertRaisesRegex(ValueError, 'different copy'):
            self.status()

    def test_desktop_restart_refuses_readiness(self):
        self.desktop.current.side_effect = ValueError('desktop restarted')
        with self.assertRaisesRegex(ValueError, 'restarted'):
            self.status()

    def test_install_preserves_old_tree_and_never_enables(self):
        self.info.update(enabled=False, state=2)
        (self.target / 'logic.js').write_text('old')
        (self.target / 'user-note').write_text('keep')
        result = support.install(self.source, self.target, self.desktop)
        backup = Path(result['backup'])
        self.assertEqual((backup / 'logic.js').read_text(), 'old')
        self.assertEqual((backup / 'user-note').read_text(), 'keep')
        self.assertEqual(support.runtime_files(self.target), support.runtime_files(self.source))
        self.assertFalse(result['ready'])
        self.desktop.shell.assert_not_called()

    def test_install_failure_restores_old_directory(self):
        self.info.update(enabled=False, state=2)
        (self.target / 'logic.js').write_text('old')
        original = Path.rename
        def fail_publish(path, destination):
            if path.name.startswith('.deskflow-stage-'):
                raise OSError('publish failed')
            return original(path, destination)
        with mock.patch.object(Path, 'rename', fail_publish), self.assertRaisesRegex(OSError, 'publish failed'):
            support.install(self.source, self.target, self.desktop)
        self.assertEqual((self.target / 'logic.js').read_text(), 'old')
        self.assertEqual(list(self.target.parent.iterdir()), [self.target])

    def test_changed_enablement_refuses_install(self):
        self.info.update(enabled=False, state=2)
        self.desktop.info.side_effect = [self.info, {**self.info, 'enabled': True, 'state': 1}]
        with self.assertRaisesRegex(ValueError, 'changed during'):
            support.install(self.source, self.target, self.desktop)
        self.assertEqual(list(self.target.parent.iterdir()), [self.target])

    def test_install_rejects_symbolic_target(self):
        self.info.update(enabled=False, state=2)
        moved = self.root / 'elsewhere'
        self.target.rename(moved)
        self.target.symlink_to(moved)
        with self.assertRaisesRegex(ValueError, 'belong'):
            support.install(self.source, self.target, self.desktop)
        self.assertEqual(support.runtime_files(moved), support.runtime_files(self.source))

    def test_runtime_rejects_symlink_file_and_missing_import(self):
        (self.target / 'logic.js').unlink()
        (self.target / 'logic.js').symlink_to(self.source / 'logic.js')
        self.assertEqual(self.status()['code'], 'invalid-installation')
        (self.target / 'logic.js').unlink()
        self.assertEqual(self.status()['code'], 'invalid-installation')

    def test_source_is_exactly_the_same_inventory_as_extension_checksum(self):
        source = Path(__file__).with_name('gnome-provider')
        files = support.runtime_files(source)
        code = files['extension.js'].decode()
        start = code.index('for (const name of [')
        inventory = code[start:code.index('])', start)]
        positions = [inventory.index("'" + name + "'") for name in support.RUNTIME_FILES]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(len(support.revision(files)), 64)


if __name__ == '__main__':
    unittest.main()
