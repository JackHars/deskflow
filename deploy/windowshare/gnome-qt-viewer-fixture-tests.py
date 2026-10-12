#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Focus-precondition regressions; no compositor, EIS or native input."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("qt_viewer_fixture", Path(__file__).with_name("gnome-qt-viewer-tests.py"))
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class CurrentFocus(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name) / "focus.json"
        self.window = SimpleNamespace(get_mapped=Mock(return_value=True), is_active=Mock(return_value=True))
        self.child = SimpleNamespace(pid=os.getpid(), poll=Mock(return_value=None), returncode=None)
        self.processes = SimpleNamespace(check=Mock())
        self.driver = SimpleNamespace(command=Mock(return_value={"live": False, "error": "viewer-not-focused"}))
        self.state = module.FocusState(self.path)
        output = contextlib.redirect_stdout(io.StringIO())
        output.__enter__()
        self.addCleanup(output.__exit__, None, None, None)
        self.state.publish(self.window)

    def observe(self):
        return module.independent_focus_denial(self.processes, self.driver, self.child, self.path)

    def test_activation_receipt_is_replaced_on_focus_loss_and_unmap(self):
        activated = module.current_focus(self.path, self.child)
        self.window.is_active.return_value = False
        self.state.publish(self.window)
        self.assertIsNone(module.current_focus(self.path, self.child))
        inactive = json.loads(self.path.read_text())
        self.assertFalse(inactive["focused"])
        self.assertGreater(inactive["sequence"], activated["sequence"])
        self.window.is_active.return_value = True
        self.state.publish(self.window)
        self.window.get_mapped.return_value = False
        self.state.publish(self.window)
        self.assertIsNone(module.current_focus(self.path, self.child))
        self.assertFalse(json.loads(self.path.read_text())["mapped"])

    def test_atomic_receipt_never_exposes_partial_new_state(self):
        before = self.path.read_bytes()
        original_replace = Path.replace
        observations = []

        def replace(temporary, target):
            observations.append((Path(target).read_bytes(), json.loads(temporary.read_text())))
            return original_replace(temporary, target)

        self.window.is_active.return_value = False
        with patch.object(Path, "replace", replace):
            self.state.publish(self.window)
        self.assertEqual(len(observations), 1)
        self.assertEqual(observations[0][0], before)
        self.assertIs(observations[0][1]["focused"], False)
        self.assertEqual(json.loads(self.path.read_text()), observations[0][1])
        self.assertEqual(list(self.path.parent.iterdir()), [self.path])

    def test_stale_activation_cannot_reach_observation(self):
        self.window.is_active.return_value = False
        self.state.publish(self.window)
        now = [0.0]
        with patch.object(module.common.time, "monotonic", side_effect=lambda: now[0]), \
                patch.object(module.common.time, "sleep", side_effect=lambda value: now.__setitem__(0, now[0] + value)):
            with self.assertRaisesRegex(RuntimeError, "Timed out waiting for independent"):
                self.observe()
        self.driver.command.assert_not_called()

    def test_focus_loss_during_native_check_is_terminal_without_retry(self):
        def loses_focus(_operation):
            self.window.is_active.return_value = False
            self.state.publish(self.window)
            return {"live": False, "error": "viewer-not-focused"}
        self.driver.command.side_effect = loses_focus
        with self.assertRaisesRegex(RuntimeError, "focus changed during"):
            self.observe()
        self.driver.command.assert_called_once_with("observe")

    def test_focus_loss_and_reacquisition_do_not_pass_as_stable_focus(self):
        def changes_focus(_operation):
            self.window.is_active.return_value = False
            self.state.publish(self.window)
            self.window.is_active.return_value = True
            self.state.publish(self.window)
            return {"live": False, "error": "viewer-not-focused"}
        self.driver.command.side_effect = changes_focus
        with self.assertRaisesRegex(RuntimeError, "focus changed during"):
            self.observe()
        self.driver.command.assert_called_once_with("observe")

    def test_actual_native_acceptance_still_fails_once(self):
        self.driver.command.return_value = {"live": True, "error": "", "snapshot": {}}
        with self.assertRaisesRegex(RuntimeError, "did not deny"):
            self.observe()
        self.driver.command.assert_called_once_with("observe")

    def test_stable_current_focus_surrounds_exactly_one_native_denial(self):
        result = self.observe()
        self.assertEqual(result["before"], result["after"])
        self.assertIs(result["before"]["focused"], True)
        self.assertEqual(result["reason"], "viewer-not-focused")
        self.driver.command.assert_called_once_with("observe")

    def test_exited_or_wrong_child_is_not_focus_evidence(self):
        self.child.poll.return_value = 0
        self.child.returncode = 0
        with self.assertRaisesRegex(RuntimeError, "focus child exited"):
            self.observe()
        self.child.poll.return_value = None
        self.child.pid += 1
        with self.assertRaisesRegex(RuntimeError, "Invalid independent focus receipt"):
            self.observe()
        self.driver.command.assert_not_called()

    def test_malformed_receipt_is_terminal_not_pending_readiness(self):
        self.path.write_text('{"mapped":true')
        with self.assertRaises(json.JSONDecodeError):
            self.observe()
        self.driver.command.assert_not_called()


if __name__ == "__main__":
    unittest.main(verbosity=2)
