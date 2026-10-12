#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Focus and forwarding-evidence regressions; no compositor, EIS or native input."""

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


def painted_snapshot():
    return {"window": "fixture-window", "surface": "fixture-surface", "desktopX": 300, "desktopY": 220,
            "sourceX": 300, "sourceY": 220,
            "paint": {"session": "1", "epoch": "2", "focusRequest": "3", "focusRevision": "4",
                      "geometry": "5", "logicalScale": 1, "sourceWidth": 640, "sourceHeight": 480,
                      "videoWidth": 640, "videoHeight": 480, "videoX": 0, "videoY": 0}}


def viewer_packet(kind, payload, session=1, epoch=2):
    return {"direction": "viewer", "kind": kind, "session": session, "epoch": epoch, "payload": payload}


class ForwardingProtocol(unittest.TestCase):
    def setUp(self):
        self.snapshot = painted_snapshot()
        self.request = module.forwarding_request(self.snapshot, 9, "original")
        self.ack = viewer_packet(18, {**self.request["payload"], "action": "ack", "result": "ready"})

    def bare_host(self):
        host = module.TlsHost.__new__(module.TlsHost)
        host.input_sequence = 1
        host.input_route_negotiated = False
        host.pending_route = None
        host.sessions = {}
        host.send = Mock()
        host.opened = Mock()
        return host

    def hello(self, capabilities):
        return {"version": 1, "role": "viewer", "codec": "h264", "capabilities": capabilities}

    def test_request_uses_exact_painted_binding_and_distinct_type(self):
        self.assertEqual(self.request, {"kind": 18, "session": 1, "epoch": 2,
                                       "payload": {"generation": 9, "focusRequest": 3, "focusRevision": 4,
                                                   "geometry": 5, "action": "request", "mode": "original"}})
        resumed = module.forwarding_request(self.snapshot, 9, "viewer")
        self.assertEqual(resumed, {**self.request, "payload": {**self.request["payload"], "mode": "viewer"}})

    def test_invalid_painted_binding_never_creates_a_fence_request(self):
        for field in ("session", "epoch", "focusRequest", "focusRevision", "geometry"):
            for invalid in (None, True, 1, "", "0", "01", "1.5", "-1", "9007199254740992", "١"):
                with self.subTest(field=field, value=invalid):
                    snapshot = painted_snapshot()
                    snapshot["paint"][field] = invalid
                    with self.assertRaisesRegex(RuntimeError, "Invalid painted forwarding"):
                        module.forwarding_request(snapshot, 9, "original")
        for generation in (0, -1, True, 2**53):
            with self.assertRaisesRegex(RuntimeError, "Invalid forwarding-fence"):
                module.forwarding_request(self.snapshot, generation, "original")

    def test_full_capability_requires_legacy_capability_and_no_duplicates(self):
        for capabilities in (None, [], ["keyboard-route-v1"], ["input-route-v1"],
                             ["keyboard-route-v1", "input-route-v1", "input-route-v1"],
                             ["keyboard-route-v1", "input-route-v1", 1]):
            with self.subTest(capabilities=capabilities):
                host = self.bare_host()
                with self.assertRaisesRegex(RuntimeError, "both forwarding capabilities"):
                    host.packet(1, 0, 0, self.hello(capabilities))
                self.assertFalse(host.input_route_negotiated)
                host.send.assert_not_called()
                host.opened.assert_not_called()
        host = self.bare_host()
        host.packet(1, 0, 0, self.hello(["input-route-v1", "keyboard-route-v1"]))
        self.assertTrue(host.input_route_negotiated)
        self.assertEqual(host.send.call_args_list[0].args,
                         (2, {"version": 1, "role": "host", "codec": "h264",
                              "capabilities": ["keyboard-route-v1", "input-route-v1"]}))
        host.opened.assert_called_once_with(1)

    def test_exact_ack_has_an_ordered_log_position(self):
        messages = [viewer_packet(12, {}), self.ack]
        self.assertEqual(module.forwarding_acknowledgement(messages, self.request, 1),
                         {"index": 1, "packet": self.ack})
        self.assertIsNone(module.forwarding_acknowledgement(messages, self.request, 2))

    def test_ack_rejects_wrong_scope_owner_binding_result_and_extra_fields(self):
        changes = [{"kind": 17}, {"session": 99}, {"epoch": 99}]
        changes += [{"payload": {**self.ack["payload"], field: value}}
                    for field, value in (("generation", 10), ("focusRequest", 99), ("focusRevision", 99),
                                         ("geometry", 99), ("mode", "viewer"), ("result", "busy"),
                                         ("action", "withdraw"), ("scope", "all"))]
        for change in changes:
            with self.subTest(change=change), self.assertRaisesRegex(RuntimeError, "acknowledgement changed"):
                module.forwarding_acknowledgement([{**self.ack, **change}], self.request, 0)
        with self.assertRaisesRegex(RuntimeError, "Duplicate forwarding"):
            module.forwarding_acknowledgement([self.ack, self.ack], self.request, 0)

    def test_tls_host_requires_one_requested_negotiated_exact_ack(self):
        host = self.bare_host()
        with self.assertRaisesRegex(RuntimeError, "Unsolicited or unnegotiated"):
            host.packet(18, 1, 2, self.ack["payload"])
        host.input_route_negotiated = True
        host.pending_route = self.request
        with self.assertRaisesRegex(RuntimeError, "exact requested binding"):
            host.packet(18, 1, 2, {**self.ack["payload"], "focusRevision": 99})
        self.assertIs(host.pending_route, self.request)
        host.packet(18, 1, 2, self.ack["payload"])
        self.assertIsNone(host.pending_route)
        with self.assertRaisesRegex(RuntimeError, "Unsolicited or unnegotiated"):
            host.packet(18, 1, 2, self.ack["payload"])


class ForwardingReceipts(unittest.TestCase):
    def setUp(self):
        self.request = module.forwarding_request(painted_snapshot(), 9, "original")
        self.expected = [{"kind": "move", "x": 320.0, "y": 240.0},
                         {"kind": "key", "code": 16777271, "down": True, "repeat": False},
                         {"kind": "key", "code": 16777271, "down": False, "repeat": False},
                         {"kind": "button", "code": 0, "down": True},
                         {"kind": "button", "code": 0, "down": False},
                         {"kind": "scroll", "dx": 0, "dy": -40}]
        self.messages = [viewer_packet(7, {**value, "geometry": 5, "focusRevision": 4})
                         for value in self.expected]

    def observe(self, messages):
        return module.forwarding_receipts(messages, self.request, self.expected, 0, [1 / 256, 1 / 256])

    def test_only_complete_positive_fresh_receipts_finish_acceptance(self):
        self.assertIsNone(self.observe([]))
        for length in range(1, len(self.messages)):
            self.assertIsNone(self.observe(self.messages[:length]))
        self.assertEqual(self.observe(self.messages), self.messages)

    def test_delayed_fenced_input_and_duplicates_are_terminal(self):
        for value in ({"kind": "key", "code": 16777270, "down": True},
                      {"kind": "button", "code": 0, "down": True},
                      {"kind": "move", "x": 384, "y": 192},
                      {"kind": "scroll", "dx": -40, "dy": 0}):
            leak = viewer_packet(7, {**value, "geometry": 5, "focusRevision": 4})
            with self.subTest(value=value), self.assertRaisesRegex(RuntimeError, "Fenced, reordered"):
                self.observe([leak])
        with self.assertRaisesRegex(RuntimeError, "Duplicated or fenced"):
            self.observe([*self.messages, self.messages[-1]])
        with self.assertRaisesRegex(RuntimeError, "Fenced, reordered"):
            self.observe([self.messages[0], self.messages[0]])

    def test_old_binding_reordered_pairs_or_false_boolean_cannot_pass(self):
        for change in ({"session": 2}, {"epoch": 3},
                       {"payload": {**self.messages[0]["payload"], "geometry": 99}},
                       {"payload": {**self.messages[0]["payload"], "focusRevision": 99}}):
            with self.subTest(change=change), self.assertRaisesRegex(RuntimeError, "changed its observed"):
                self.observe([{**self.messages[0], **change}])
        with self.assertRaisesRegex(RuntimeError, "Fenced, reordered"):
            self.observe([self.messages[0], self.messages[2]])
        with self.assertRaisesRegex(RuntimeError, "Fenced, reordered"):
            self.observe([self.messages[0], {**self.messages[1],
                                          "payload": {**self.messages[1]["payload"], "down": 1}}])

    def test_pre_resume_interval_rejects_every_forwarded_input_family(self):
        for message in self.messages:
            with self.subTest(message=message), self.assertRaisesRegex(RuntimeError, "Duplicated or fenced"):
                module.forwarding_receipts([message], self.request, [], 0, [1 / 256, 1 / 256])


class ForwardingAcceptance(unittest.TestCase):
    def exercise(self, *, delayed_leak=False, drop_fresh_key=False):
        snapshot = painted_snapshot()
        messages, requests, commands = [], [], []
        state = {"fenced": False}

        def packet(value):
            messages.append(viewer_packet(7, {**value, "geometry": 5, "focusRevision": 4}))

        def route(operation):
            name, request = operation
            self.assertEqual(name, "input-route")
            requests.append(request)
            state["fenced"] = request["payload"]["mode"] == "original"
            messages.append(viewer_packet(18, {**request["payload"], "action": "ack", "result": "ready"}))
            if delayed_leak and request["payload"]["mode"] == "viewer":
                packet({"kind": "key", "code": 16777270, "down": True})

        def command(operation, **values):
            commands.append({"op": operation, **values})
            if operation == "observe":
                return {"live": True, "snapshot": {**snapshot, "paint": dict(snapshot["paint"])}}
            if operation == "move":
                snapshot.update(desktopX=values["x"], desktopY=values["y"], sourceX=values["x"], sourceY=values["y"])
            if not state["fenced"]:
                if operation == "move":
                    packet({"kind": "move", **values})
                elif operation == "key" and not drop_fresh_key:
                    for down in (True, False):
                        packet({"kind": "key", "code": 16777271, "down": down, "repeat": False})
                elif operation == "click":
                    for down in (True, False):
                        packet({"kind": "button", "code": 0, "down": down})
                elif operation == "scroll":
                    packet({"kind": "scroll", "dx": -values["dx"] / 3, "dy": -values["dy"] / 3})
            return {"event": "synced", "id": len(commands)}

        host = SimpleNamespace(events=lambda: list(messages), commands=SimpleNamespace(put_nowait=route))
        driver = SimpleNamespace(command=command)
        processes = SimpleNamespace(check=Mock())
        now = [0.0]
        with patch.object(module.common.time, "monotonic", side_effect=lambda: now[0]), \
                patch.object(module.common.time, "sleep", side_effect=lambda value: now.__setitem__(0, now[0] + value)):
            result = module.forwarding_fence_acceptance(processes, host, driver, snapshot, lambda x, y: (x, y))
        return result, requests, commands

    def test_positive_sequence_bounds_suppression_without_claiming_consumption(self):
        result, requests, commands = self.exercise()
        self.assertEqual([(r["payload"]["generation"], r["payload"]["mode"]) for r in requests],
                         [(1, "original"), (1, "viewer"), (2, "original"), (2, "viewer")])
        self.assertEqual(len(result["freshReceipts"]), 7)
        self.assertEqual([r["payload"]["kind"] for r in result["freshReceipts"]],
                         ["move", "key", "key", "button", "button", "scroll", "move"])
        self.assertFalse(result["originalDeliveryAuthority"])
        self.assertFalse(result["compositorConsumptionBarrier"])
        self.assertFalse(result["nativeEventDeviceAttribution"])
        self.assertEqual([c["op"] for c in commands[:5]], ["key", "click", "scroll", "move", "observe"])
        self.assertEqual(len(result["positiveReceiptBoundaries"]), 5)

    def test_leak_arriving_after_resume_cannot_hide_behind_the_ack_boundary(self):
        with self.assertRaisesRegex(RuntimeError, "Duplicated or fenced|Fenced, reordered"):
            self.exercise(delayed_leak=True)

    def test_native_pongs_and_elapsed_time_cannot_replace_fresh_input_receipts(self):
        with self.assertRaisesRegex(RuntimeError, "Timed out waiting for ordered fresh input"):
            self.exercise(drop_fresh_key=True)


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
