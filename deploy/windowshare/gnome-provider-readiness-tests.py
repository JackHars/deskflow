#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Deterministic startup-readiness regressions; no D-Bus or compositor."""

import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("provider_fixture", Path(__file__).with_name("gnome-provider-tests.py"))
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class DiscoveryError(Exception):
    def __init__(self, domain="dbus", code=19):
        super().__init__("startup D-Bus failure")
        self.domain, self.code = domain, code

    def matches(self, domain, code):
        return self.domain == domain and self.code == code


class StartupReadiness(unittest.TestCase):
    def setUp(self):
        self.processes = SimpleNamespace(check=Mock())
        self.shell = SimpleNamespace(poll=Mock(return_value=None), returncode=None)
        self.provider = SimpleNamespace(
            bus=Mock(return_value=(True,)), call=Mock(), GLib=SimpleNamespace(Error=DiscoveryError),
            Gio=SimpleNamespace(dbus_error_quark=lambda: "dbus", DBusError=SimpleNamespace(UNKNOWN_METHOD=19)),
        )
        self.now = 0.0
        self.clock = patch.object(module.time, "monotonic", side_effect=lambda: self.now)
        self.sleep = patch.object(module.time, "sleep", side_effect=self.advance)
        self.clock.start()
        self.sleep.start()
        self.addCleanup(self.clock.stop)
        self.addCleanup(self.sleep.stop)

    def advance(self, seconds):
        self.now += seconds

    def metadata(self, **values):
        return {"uuid": module.UUID, "enabled": False, "error": "", **values}

    def wait(self, timeout=1):
        return module.wait_for_disabled_extension(self.processes, self.shell, self.provider, timeout=timeout)

    def test_discovery_then_missing_state_then_initialized(self):
        ready = self.metadata(state=6.0)
        self.provider.bus.side_effect = [(False,), (True,), (True,), (True,)]
        self.provider.call.side_effect = [({},), (self.metadata(),), (ready,)]
        self.assertEqual(self.wait(), ready)
        self.assertEqual(self.provider.bus.call_count, 4)
        self.assertEqual(self.provider.call.call_count, 3)
        self.assertAlmostEqual(self.now, 0.15)

    def test_both_existing_disabled_states_are_ready(self):
        for state in (2, 2.0, 6, 6.0):
            with self.subTest(state=state):
                ready = self.metadata(state=state)
                self.provider.call.return_value = (ready,)
                self.assertEqual(self.wait(), ready)
                self.assertEqual(self.now, 0)

    def test_missing_state_has_an_absolute_deadline(self):
        self.provider.call.return_value = (self.metadata(),)
        with self.assertRaisesRegex(RuntimeError, "Timed out waiting for initially disabled"):
            self.wait(timeout=0.15)
        self.assertEqual(self.provider.call.call_count, 3)
        self.assertLess(self.now, 0.2)

    def test_unexpected_states_fail_without_consuming_later_good_reply(self):
        for state in (0, 1, 3, 4, 5, 7, 8, 99, None, "6", True, False, float("nan"), 2.5):
            with self.subTest(state=state):
                self.provider.call.reset_mock()
                self.provider.call.side_effect = [(self.metadata(state=state),), (self.metadata(state=6),)]
                with self.assertRaisesRegex(RuntimeError, "not initially disabled"):
                    self.wait()
                self.assertEqual(self.provider.call.call_count, 1)
                self.assertEqual(self.now, 0)

    def test_enablement_and_errors_are_terminal_even_without_state(self):
        invalid = [self.metadata(enabled=True), self.metadata(enabled=0), self.metadata(error="load failed"),
                   self.metadata(error=None), self.metadata(state=6, enabled=True),
                   self.metadata(state=2, error="load failed")]
        for key in ("enabled", "error"):
            info = self.metadata()
            del info[key]
            invalid.append(info)
        for info in invalid:
            with self.subTest(info=info):
                self.provider.call.reset_mock()
                self.provider.call.side_effect = [(info,), (self.metadata(state=6),)]
                with self.assertRaisesRegex(RuntimeError, "not initially disabled and error-free"):
                    self.wait()
                self.assertEqual(self.provider.call.call_count, 1)
                self.assertEqual(self.now, 0)

    def test_malformed_metadata_and_identity_are_terminal(self):
        for info in (None, [], "metadata", {"enabled": False, "error": ""}, self.metadata(uuid="different")):
            with self.subTest(info=info):
                self.provider.call.reset_mock()
                self.provider.call.side_effect = [(info,), (self.metadata(state=6),)]
                with self.assertRaisesRegex(RuntimeError, "Invalid extension metadata|Unexpected extension identity"):
                    self.wait()
                self.assertEqual(self.provider.call.call_count, 1)
                self.assertEqual(self.now, 0)

    def test_dbus_failure_is_not_retried(self):
        for method in (self.provider.bus, self.provider.call):
            with self.subTest(method=method):
                method.side_effect = RuntimeError("owned bus failed")
                with self.assertRaisesRegex(RuntimeError, "owned bus failed"):
                    self.wait()
                self.assertEqual(self.now, 0)
                method.side_effect = None

    def test_object_not_exported_is_pending_only_before_first_response(self):
        ready = self.metadata(state=6)
        self.provider.call.side_effect = [DiscoveryError(), (self.metadata(),), (ready,)]
        self.assertEqual(self.wait(), ready)
        self.assertEqual(self.provider.call.call_count, 3)
        self.assertAlmostEqual(self.now, 0.1)

    def test_method_or_owner_disappearance_after_response_is_terminal(self):
        for response in ({}, self.metadata()):
            with self.subTest(response=response):
                self.provider.call.reset_mock()
                self.provider.call.side_effect = [(response,), DiscoveryError(), (self.metadata(state=6),)]
                with self.assertRaises(DiscoveryError):
                    self.wait()
                self.assertEqual(self.provider.call.call_count, 2)
        self.provider.call.side_effect = [(self.metadata(),)]
        self.provider.bus.side_effect = [(True,), (False,), (True,)]
        with self.assertRaisesRegex(RuntimeError, "owner vanished"):
            self.wait()

    def test_other_dbus_error_codes_and_domains_are_terminal(self):
        for domain, code in (("other", 19), ("dbus", 1), ("dbus", 2), ("dbus", 18)):
            with self.subTest(domain=domain, code=code):
                self.provider.call.reset_mock()
                self.provider.call.side_effect = [DiscoveryError(domain, code), (self.metadata(state=6),)]
                with self.assertRaises(DiscoveryError):
                    self.wait()
                self.assertEqual(self.provider.call.call_count, 1)
                self.assertEqual(self.now, 0)

    def test_shell_exit_is_terminal_before_bus_query(self):
        self.shell.poll.return_value = 1
        self.shell.returncode = 1
        with self.assertRaisesRegex(RuntimeError, "GNOME Shell exited 1"):
            self.wait()
        self.provider.bus.assert_not_called()
        self.provider.call.assert_not_called()


if __name__ == "__main__":
    unittest.main(verbosity=2)
