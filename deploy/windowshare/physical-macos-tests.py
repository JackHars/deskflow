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
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

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
        publisher = module.StatusPublisher.__new__(module.StatusPublisher)
        publisher.lock = threading.Lock()
        publisher.state, publisher.deadline, publisher.was_active = "preparing", None, False
        now = [100.0]
        with patch.object(module.time, "monotonic", side_effect=lambda: now[0]):
            publisher.set("ready")
            publisher.set("active", 120)
            now[0] += 30
            publisher.set("active", 120)
            self.assertEqual(publisher.snapshot()["remainingSeconds"], 90)
            with self.assertRaises(RuntimeError):
                publisher.set("preparing")
            now[0] += 100
            self.assertEqual(publisher.snapshot(), {"version": 1, "state": "stopped", "remainingSeconds": 0})
            with self.assertRaises(RuntimeError):
                publisher.set("active", 120)

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


if __name__ == "__main__":
    unittest.main()
