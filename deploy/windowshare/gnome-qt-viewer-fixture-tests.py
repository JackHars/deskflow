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
from copy import deepcopy

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


class PreparationEvidence(unittest.TestCase):
    def setUp(self):
        self.initial = painted_snapshot()
        self.initial["paint"].update({"nativeSurface": 42, "windowAssociation": "org.deskflow.window." + "b" * 64,
                                      "connection": "7", "peerFingerprint": "a" * 64})
        self.prepared_snapshot = deepcopy(self.initial)
        self.prepared_snapshot["paint"].update({"focusRequest": "5", "focusRevision": "6"})
        self.binding = {"request": "30", "coreConnectionGeneration": 1,
                        "captureEpoch": "1", "captureSequence": "30", "capturePart": 1}
        self.contract = module.SUPPORTED_ORDERING_CONTRACTS[1]
        self.reply = {"id": 30, "live": True, "error": "", "outcome": "none",
                      "binding": self.binding, "snapshot": self.prepared_snapshot,
                      "contract": self.contract, "originalDeliveryAuthority": False}

    def verify(self, reply):
        return module.prepared_focus_receipt(reply, self.initial, (300, 220), self.contract, "a" * 64)

    def test_preparation_has_distinct_exact_capture_and_fresh_real_focus(self):
        self.assertEqual(self.verify(self.reply), self.reply)
        for field, value in (("request", "31"), ("captureSequence", "31"), ("capturePart", True),
                             ("scope", "all"), ("session", "1"), ("viewerConnection", "7")):
            bad = deepcopy(self.reply)
            bad["binding"][field] = value
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                self.verify(bad)
        for field, value in (("focusRequest", "3"), ("focusRevision", "4"), ("nativeSurface", 99),
                             ("connection", "8"), ("peerFingerprint", "c" * 64),
                             ("windowAssociation", "org.deskflow.window." + "d" * 64)):
            bad = deepcopy(self.reply)
            bad["snapshot"]["paint"][field] = value
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                self.verify(bad)
        for change in ({"contract": "GNOME50"}, {"originalDeliveryAuthority": True}, {"outcome": "unsupported"},
                       {"live": False}, {"snapshot": {**self.prepared_snapshot, "desktopX": 301}}):
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                self.verify({**self.reply, **change})

    def test_focus_only_interval_rejects_acquisitions_after_all_has_resumed(self):
        focus = viewer_packet(7, {"kind": "focus", "request": 5, "active": True})
        resumed = viewer_packet(18, {"action": "ack", "mode": "viewer", "result": "ready"})
        messages = [focus, resumed, viewer_packet(7, {"kind": "release"})]
        self.assertEqual(module.preparation_input_receipts(messages, 0, 3)["forwardedInput"], [])
        for kind in module.INPUT_KINDS:
            with self.subTest(kind=kind), self.assertRaisesRegex(RuntimeError, "forwarded source input"):
                module.preparation_input_receipts(messages + [viewer_packet(7, {"kind": kind})], 0, 4)
        with self.assertRaisesRegex(RuntimeError, "evidence interval"):
            module.preparation_input_receipts(messages, 2, 1)

    def test_completed_prefix_timer_retires_before_promotion_without_renewing_preparation(self):
        prepared = {"clientRequest": "42"}
        retained = {"clientRequest": "42", "live": True, "prefixPending": False,
                    "ageMs": 300, "remainingMs": 199, "originalDeliveryAuthority": False}
        self.assertEqual(module.preparation_retained_receipt(retained, prepared), retained)
        for change in ({"prefixPending": True}, {"prefixPending": 0}, {"clientRequest": "43"},
                       {"live": False}, {"ageMs": 200, "remainingMs": 299},
                       {"ageMs": 300, "remainingMs": 499}, {"ageMs": 500, "remainingMs": 0},
                       {"ageMs": True}, {"remainingMs": True}, {"originalDeliveryAuthority": True}):
            with self.subTest(change=change), self.assertRaisesRegex(RuntimeError, "PONG timer or renewed"):
                module.preparation_retained_receipt({**retained, **change}, prepared)

    def test_retained_preparation_uses_its_own_reply_without_requesting_a_new_ping(self):
        driver = module.Driver.__new__(module.Driver)
        driver.request = 41
        driver.child = SimpleNamespace(stdin=io.BytesIO())
        driver.event = Mock(return_value={"id": 42, "event": "preparation-retained"})
        driver.command("preparation-retained")
        self.assertEqual(json.loads(driver.child.stdin.getvalue()), {"id": 42, "op": "preparation-retained"})
        driver.event.assert_called_once_with("preparation-retained")

    def test_promotion_keeps_original_capture_under_a_new_genuine_all_request(self):
        request = module.forwarding_request(self.prepared_snapshot, 6, "original")
        binding = {"request": "31", **{key: value for key, value in self.binding.items() if key != "request"},
                   "viewerConnection": "7", "session": "1", "epoch": "2", "generation": "6",
                   "focusRequest": "5", "focusRevision": "6", "geometry": "5", "scope": "all"}
        reply = {"id": 31, "live": True, "error": "", "originalDeliveryAuthority": False,
                 "orderingLimitation": "", "senderOrdering": {"live": True, "contract": self.contract},
                 "cutoff": binding, "snapshot": self.prepared_snapshot}
        self.assertEqual(module.input_drain_receipt(reply, request, self.prepared_snapshot, self.contract,
                                                   capture=self.binding), reply)
        with self.assertRaisesRegex(RuntimeError, "exact cutoff binding"):
            module.input_drain_receipt(reply, request, self.prepared_snapshot, self.contract)
        bad = deepcopy(reply)
        bad["cutoff"]["captureSequence"] = "31"
        with self.assertRaisesRegex(RuntimeError, "exact cutoff binding"):
            module.input_drain_receipt(bad, request, self.prepared_snapshot, self.contract, capture=self.binding)


class FocusRecoveryDiagnostic(unittest.TestCase):
    def setUp(self):
        self.processes, self.host, self.driver, self.focus = Mock(), Mock(), Mock(), Mock()
        self.initial, self.focus_path, self.center = painted_snapshot(), Path("/fixture/focus.json"), (640, 360)
        self.ready, self.report = Mock(), {}
        self.steps = []
        self.focus.terminate.side_effect = lambda: self.steps.append("terminate")
        self.focus.wait.side_effect = lambda **kwargs: self.steps.append(("wait", kwargs))
        self.driver.command.side_effect = lambda operation, **kwargs: self.steps.append((operation, kwargs))

    def recover(self, contract="verified-build", mode=None):
        kwargs = {} if mode is None else {"mode": mode}
        return module.recover_independent_focus(
            self.processes, self.host, self.driver, self.initial, self.focus, self.focus_path, contract,
            self.center, self.ready, self.report, **kwargs)

    def test_default_prepared_path_keeps_its_native_chain_and_adds_no_ordinary_click(self):
        prepared = {"supported": True, "scope": "fixture-result"}
        with patch.object(module, "native_first_click_acceptance",
                          side_effect=lambda *args: self.steps.append("prepared") or prepared) as native, \
                patch.object(module.common, "wait_for") as readiness, \
                patch.object(module.time, "sleep", side_effect=AssertionError("No new settling delay")):
            self.recover()
        native.assert_called_once_with(self.processes, self.host, self.driver, self.initial,
                                       self.focus, self.focus_path, "verified-build")
        self.assertEqual(self.report, {"firstClickPreparation": prepared})
        self.assertEqual(self.steps, ["prepared", "terminate", ("wait", {"timeout": 5}),
                                      ("move", {"x": 640, "y": 360})])
        readiness.assert_called_once_with(self.processes, self.ready, "original viewer focus recovery",
                                          timeout=5, retry_errors=False)

    def test_ordinary_click_path_closes_focus_child_then_moves_and_clicks_exactly_once(self):
        with patch.object(module, "native_first_click_acceptance") as native, \
                patch.object(module.common, "wait_for") as readiness, \
                patch.object(module.time, "sleep", side_effect=AssertionError("No settling delay")):
            self.recover(mode="ordinary-click")
        native.assert_not_called()
        self.assertNotIn("firstClickPreparation", self.report)
        self.assertEqual(self.steps, ["terminate", ("wait", {"timeout": 5}),
                                      ("move", {"x": 640, "y": 360}), ("click", {})])
        readiness.assert_called_once_with(self.processes, self.ready, "original viewer focus recovery",
                                          timeout=5, retry_errors=False)

    def test_unverified_default_keeps_existing_explicit_preparation_denial_then_ordinary_recovery(self):
        denied = {"supported": False, "originalDeliveryAuthority": False}
        with patch.object(module, "native_first_click_acceptance", return_value=denied) as native, \
                patch.object(module.common, "wait_for"):
            self.recover(contract="")
        native.assert_called_once()
        self.assertEqual(self.report["firstClickPreparation"], denied)
        self.assertEqual(self.steps[-1], ("click", {}))

    def test_failed_preparation_is_terminal_without_ordinary_fallback_or_retry(self):
        with patch.object(module, "native_first_click_acceptance", side_effect=RuntimeError("stale-revision")) as native, \
                patch.object(module.common, "wait_for") as readiness:
            with self.assertRaisesRegex(RuntimeError, "stale-revision"):
                self.recover()
        native.assert_called_once()
        readiness.assert_not_called()
        self.driver.command.assert_not_called()
        self.focus.terminate.assert_not_called()

    def test_invalid_mode_fails_before_any_transition(self):
        with patch.object(module, "native_first_click_acceptance") as native:
            with self.assertRaisesRegex(RuntimeError, "Unknown diagnostic"):
                self.recover(mode="retry-after-sleep")
        native.assert_not_called()
        self.driver.command.assert_not_called()

    def test_cli_default_and_explicit_modes_are_preserved_in_run_arguments(self):
        executable = str(Path(__file__).resolve())
        required = ["--viewer", executable, "--fixture", executable, "--output", "/fixture/output"]
        for value in (None, "prepared-first-click", "ordinary-click"):
            with self.subTest(value=value), patch.object(module, "run", return_value=0) as run:
                self.assertEqual(module.main(required + ([] if value is None else ["--focus-recovery", value])), 0)
                self.assertEqual(run.call_args.args[0].focus_recovery, value or "prepared-first-click")
        with contextlib.redirect_stderr(io.StringIO()), patch.object(module, "run") as run:
            with self.assertRaises(SystemExit) as result:
                module.main(required + ["--focus-recovery", "retry"])
            self.assertEqual(result.exception.code, 2)
            run.assert_not_called()


class FixtureBatchTransport(unittest.TestCase):
    def test_second_window_search_never_clicks_the_old_window_point(self):
        driver = Mock()
        wrong = {"live": True, "snapshot": {"paint": {"session": "1"}}}
        expected = {"live": True, "snapshot": {"paint": {"session": "2"}, "window": "actual-second"}}
        replies = iter((wrong, expected))
        driver.command.side_effect = lambda operation, **kwargs: next(replies) if operation == "observe" else {"event": "synced"}

        def wait(_processes, predicate, _description, **kwargs):
            self.assertIsNone(predicate())
            return predicate()

        with patch.object(module.common, "wait_for", side_effect=wait):
            self.assertEqual(module.locate_focused_viewer(Mock(), driver, 2, [0, 0, 1280, 720]), expected["snapshot"])
        self.assertEqual([call.args[0] for call in driver.command.call_args_list], ["move", "observe", "move", "observe"])

    def driver(self):
        driver = module.Driver.__new__(module.Driver)
        driver.processes = Mock()
        driver.child = Mock()
        driver.selector = Mock()
        driver.selector.select.return_value = [True]
        driver.transcript = io.StringIO()
        driver.buffer = b""
        return driver

    def test_complete_sixteen_point_receipts_cross_scalar_output_limit(self):
        # Native receipts include a full painted state, ordering contract and
        # origin seal for every independently stamped point. Exercise the real
        # line reader in the same bounded chunks as an actual subprocess pipe.
        reply = {"event": "batch-mapped", "id": 50, "segments": [
            {"cutoff": {"request": str(50 + i)}, "snapshot": {"paint": {
                "peerFingerprint": "a" * 64, "windowAssociation": "org.deskflow.window." + "b" * 64,
                "nativeEvidence": "x" * 1800}}, "live": True}
            for i in range(16)]}
        wire = json.dumps(reply, separators=(",", ":")).encode() + b"\n"
        self.assertGreater(len(wire), 16384)
        self.assertLessEqual(len(wire), 65536)
        driver = self.driver()
        with patch.object(module.os, "read", side_effect=[wire[i:i + 4096] for i in range(0, len(wire), 4096)]):
            self.assertEqual(driver.event("batch-mapped"), reply)
        self.assertEqual(driver.buffer, b"")
        self.assertEqual(driver.transcript.getvalue().encode(), wire)

    def test_oversized_batch_still_fails_closed(self):
        wire = b'{"event":"batch-mapped","oversized":"' + b"x" * 65536 + b'"}\n'
        driver = self.driver()
        with patch.object(module.os, "read", side_effect=[wire[i:i + 4096] for i in range(0, len(wire), 4096)]):
            with self.assertRaisesRegex(RuntimeError, "output exceeded bound"):
                driver.event("batch-mapped")
        self.assertEqual(driver.transcript.getvalue(), "")


class FixtureTransportShutdown(unittest.TestCase):
    def run_failure(self, error, expected):
        host = module.TlsHost.__new__(module.TlsHost)
        host.stopping = module.threading.Event()
        host.expected_disconnect = expected
        host.failure = None
        host.input_sequence = 1
        host.commands = module.queue.Queue()
        host.listener = Mock()
        host.listener.accept.return_value = (Mock(), None)
        connection = Mock()
        connection.getpeercert.return_value = b"fixture-certificate"
        connection.pending.return_value = True
        connection.recv.side_effect = error
        host.viewer_pin = module.hashlib.sha256(b"fixture-certificate").hexdigest()
        host.context = Mock()
        host.context.wrap_socket.return_value = contextlib.nullcontext(connection)
        host.run()
        self.assertIsNone(host.connection)
        return host.failure

    def test_only_requested_shutdown_accepts_transport_close_errors(self):
        for error in (BrokenPipeError("closed"), ConnectionResetError("reset"), module.ssl.SSLEOFError("EOF")):
            with self.subTest(error=type(error).__name__):
                self.assertIsNone(self.run_failure(error, True))
                self.assertIn(type(error).__name__, self.run_failure(error, False))

    def test_expected_disconnect_does_not_hide_protocol_or_timeout_failures(self):
        for error in (RuntimeError("malformed fixture packet"), TimeoutError("stalled peer"),
                      module.ssl.SSLError("invalid TLS data")):
            with self.subTest(error=type(error).__name__):
                self.assertIn(type(error).__name__, self.run_failure(error, True))


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


class NativeDrainReceipts(unittest.TestCase):
    def setUp(self):
        self.snapshot = painted_snapshot()
        self.snapshot["paint"]["connection"] = "11"
        self.request = module.forwarding_request(self.snapshot, 9, "original")
        self.reply = {"live": True, "error": "", "id": 12, "originalDeliveryAuthority": False,
                      "orderingLimitation": "mutter-eis-sync-build-unverified", "senderOrdering": None,
                      "snapshot": deepcopy(self.snapshot),
                      "cutoff": {"request": "12", "coreConnectionGeneration": 1, "captureEpoch": "1",
                                 "captureSequence": "12", "capturePart": 1, "viewerConnection": "11",
                                 "session": "1", "epoch": "2", "generation": "9", "focusRequest": "3",
                                 "focusRevision": "4", "geometry": "5", "scope": "all"}}

    def verify(self, reply):
        return module.input_drain_receipt(reply, self.request, self.snapshot)

    def test_complete_exact_receipt_retains_explicit_authority_limitation(self):
        self.assertEqual(self.verify(self.reply), self.reply)

    def test_supported_receipt_requires_exact_live_build_and_still_has_no_delivery_authority(self):
        for contract in module.SUPPORTED_ORDERING_CONTRACTS:
            reply = {**self.reply, "orderingLimitation": "",
                     "senderOrdering": {"live": True, "contract": contract}}
            self.assertEqual(module.input_drain_receipt(reply, self.request, self.snapshot, contract), reply)
            for change in ({"live": False}, {"contract": "sha256:" + "0" * 64},
                           {"live": 1}, {"extra": False}):
                wrong = {**reply, "senderOrdering": {**reply["senderOrdering"], **change}}
                with self.subTest(change=change), self.assertRaises(RuntimeError):
                    module.input_drain_receipt(wrong, self.request, self.snapshot, contract)
            with self.assertRaises(RuntimeError):
                module.input_drain_receipt({**reply, "originalDeliveryAuthority": True},
                                           self.request, self.snapshot, contract)
            with self.assertRaises(RuntimeError):
                self.verify(reply)
        for value in ({}, {"live": True, "contract": ""}, False):
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                self.verify({**self.reply, "senderOrdering": value})
        with self.assertRaises(RuntimeError):
            module.input_drain_receipt(self.reply, self.request, self.snapshot, "GNOME-50")

    def test_each_cutoff_field_and_unknown_fields_are_checked(self):
        for field in self.reply["cutoff"]:
            for invalid in (None, True, "99", 99):
                value = deepcopy(self.reply)
                value["cutoff"][field] = invalid
                with self.subTest(field=field, invalid=invalid), self.assertRaisesRegex(RuntimeError, "exact cutoff"):
                    self.verify(value)
        value = deepcopy(self.reply)
        value["cutoff"]["authority"] = True
        with self.assertRaisesRegex(RuntimeError, "exact cutoff"):
            self.verify(value)

    def test_prefix_seal_is_exact_live_and_supported_independently_of_point(self):
        contract = module.SUPPORTED_ORDERING_CONTRACTS[0]
        origin = self.reply["cutoff"]
        seal = {"live": True, "origin": deepcopy(origin), "contract": contract}
        self.assertEqual(module.input_seal_receipt({"seal": seal}, origin, contract), seal)
        for change in ({"live": False}, {"live": 1}, {"origin": {}}, {"contract": "GNOME50"}, {"extra": True}):
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                module.input_seal_receipt({"seal": {**seal, **change}}, origin, contract)
        for field in origin:
            changed = {**origin, field: True}
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                module.input_seal_receipt({"seal": {**seal, "origin": changed}}, origin, contract)
        with self.assertRaises(RuntimeError):
            module.input_seal_receipt({"seal": seal}, origin, "GNOME50")

    def test_batch_requires_all_ordered_captures_without_another_ping_or_drain(self):
        contract = module.SUPPORTED_ORDERING_CONTRACTS[0]
        sealed = {**self.reply, "requestedPings": 2, "requestedDrains": 1}
        items = []
        for index in range(16):
            item = deepcopy(self.reply)
            item["orderingLimitation"] = ""
            item["senderOrdering"] = {"live": True, "contract": contract}
            item["seal"] = {"live": True, "origin": deepcopy(sealed["cutoff"]), "contract": contract}
            item["cutoff"]["request"] = item["cutoff"]["captureSequence"] = str(13 + index)
            item["snapshot"]["desktopX"] += index % 4
            item["snapshot"]["desktopY"] += index // 4
            items.append(item)
        reply = {"id": 13, "segments": items, "error": "", "requestedPings": 2, "requestedDrains": 1,
                 "originalDeliveryAuthority": False}
        verify = lambda value: module.input_batch_receipt(value, 16, self.request, self.snapshot, sealed, contract)
        self.assertEqual(verify(reply), reply)
        for change in ({"segments": items[:15]}, {"segments": items + [items[0]]}, {"segments": list(reversed(items))},
                       {"requestedPings": 3}, {"requestedDrains": 2}, {"error": "failed"},
                       {"originalDeliveryAuthority": True}):
            with self.subTest(change=list(change)), self.assertRaises(RuntimeError):
                verify({**reply, **change})
        wrong = deepcopy(reply)
        wrong["segments"][7]["cutoff"]["captureSequence"] = "999"
        with self.assertRaises(RuntimeError):
            verify(wrong)

    def test_native_identity_point_and_authority_cannot_be_substituted(self):
        for field, invalid in (("window", "other"), ("surface", "other"), ("paint", {}),
                               ("desktopX", float("nan")), ("desktopY", 0)):
            value = deepcopy(self.reply)
            value["snapshot"][field] = invalid
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                self.verify(value)
        for field, invalid in (("live", False), ("id", True), ("error", "failed"),
                               ("originalDeliveryAuthority", True), ("originalDeliveryAuthority", 0),
                               ("orderingLimitation", "")):
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                self.verify({**self.reply, field: invalid})

    def test_binding_denial_is_distinct_from_failed_infrastructure(self):
        reply = {"live": False, "originalDeliveryAuthority": False, "error": "viewer-connection-mismatch"}
        self.assertEqual(module.input_drain_denial(reply, "fixture"), reply["error"])
        for change in ({"live": True}, {"snapshot": {}}, {"cutoff": {}}, {"originalDeliveryAuthority": True},
                       {"error": ""}, {"error": "timeout"}, {"error": "protocol-error"},
                       {"error": "disconnected"}):
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                module.input_drain_denial({**reply, **change}, "fixture")


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
