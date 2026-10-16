#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Bounded Linux transport/control tests; no compositor, TLS peer or input."""

import contextlib
import errno
import importlib.util
import json
import os
from pathlib import Path
import socket
import struct
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
from types import SimpleNamespace

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("crossmachine", Path(__file__).with_name("gnome-crossmachine-client.py"))
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class Controls(unittest.TestCase):
    @contextlib.contextmanager
    def reader(self, data):
        read, write = os.pipe()
        try:
            os.write(write, data)
            commands = module.Commands(read)
            try:
                yield commands
            finally:
                commands.close()
        finally:
            os.close(read)
            os.close(write)

    def test_exact_commands(self):
        values = [{"id": 1, "op": "prepare"}, {"id": 2, "op": "move", "x": 32.5, "y": 11},
                  {"id": 3, "op": "key", "code": 66}, {"id": 4, "op": "scroll", "dx": 0, "dy": 60},
                  {"id": 5, "op": "scroll", "dx": -120, "dy": 120}, {"id": 6, "op": "stop"}]
        with self.reader(b"".join(json.dumps(v).encode() + b"\n" for v in values)) as reader:
            self.assertEqual([reader.next() for _ in values], values)

    def test_bad_schema_and_numeric_values(self):
        values = [b'{"id":1,"id":2,"op":"stop"}', b'{"id":true,"op":"stop"}',
                  b'{"id":1,"op":[]}', b'{"id":1,"op":"stop","extra":1}',
                  b'{"id":1,"op":"move","x":NaN,"y":1}',
                  b'{"id":1,"op":"key","code":0}', b'{"id":1,"op":"key","code":768}',
                  b'{"id":1,"op":"scroll","dx":0,"dy":0}',
                  b'{"id":1,"op":"scroll","dx":0,"dy":1201}',
                  b'{"id":1,"op":"scroll","dx":0,"dy":60.5}']
        for value in values:
            with self.subTest(value=value), self.reader(value + b"\n") as reader:
                with self.assertRaises(RuntimeError):
                    reader.next()

    def test_replay_and_input_bound(self):
        with self.reader(b'{"id":1,"op":"stop"}\n' * 2) as reader:
            reader.next()
            with self.assertRaisesRegex(RuntimeError, "Nonmonotonic"):
                reader.next()
        with self.reader(b"x" * 4097) as reader:
            self.assertIsNone(reader.next())
            with self.assertRaisesRegex(RuntimeError, "exceeded"):
                reader.next()

    def test_cleanup_missing_container_case_and_daemon_error(self):
        arguments = SimpleNamespace(container="deskflow-cross-" + "1" * 16, token="2" * 32,
                                    forward_directory=Path("/unused"))
        for diagnostic in ("error: no such object: NAME", "Error: No such object: NAME"):
            result = SimpleNamespace(returncode=1, stdout="[]", stderr=diagnostic)
            with patch.object(module.subprocess, "run", return_value=result), \
                    patch.object(module, "cleanup_socket", return_value=False) as cleanup, \
                    patch.object(module, "emit"):
                self.assertEqual(module.cleanup_container(arguments), 0)
                cleanup.assert_called_once_with(arguments.forward_directory, arguments.container, arguments.token)
        failure = SimpleNamespace(returncode=1, stdout="", stderr="Cannot connect to Docker daemon")
        with patch.object(module.subprocess, "run", return_value=failure), patch.object(module, "cleanup_socket") as cleanup:
            with self.assertRaisesRegex(RuntimeError, "Cannot inspect"):
                module.cleanup_container(arguments)
            cleanup.assert_not_called()


@unittest.skipUnless(sys.platform == "linux", "SO_PEERCRED relay checks require Linux")
class Relay(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="df-cross-relay-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def listener(self):
        endpoint = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.addCleanup(endpoint.close)
        path = self.root / "mac.sock"
        endpoint.bind(str(path))
        path.chmod(0o600)
        endpoint.listen(1)
        endpoint.settimeout(3)
        return path, endpoint

    def test_half_close_drains_both_bounded_directions(self):
        self.half_close()

    def test_empty_queue_shutdown_already_closed_race(self):
        self.half_close(already_closed=True)

    def half_close(self, already_closed=False):
        path, listener = self.listener()
        request, response = b"request-" * 100000, b"response-" * 100000
        received, errors = [], []

        def server():
            try:
                with listener.accept()[0] as peer:
                    peer.settimeout(3)
                    chunks = []
                    while data := peer.recv(32768):
                        chunks.append(data)
                    received.append(b"".join(chunks))
                    peer.sendall(response)
                    peer.shutdown(socket.SHUT_WR)
            except Exception as error:
                errors.append(error)

        original_socket = socket.socket

        class ConcurrentlyClosedSocket(original_socket):
            def shutdown(self, how):
                super().shutdown(how)
                # Deterministic syscall-result injection after the real
                # half-close: model closure racing the empty-queue shutdown.
                raise OSError(errno.ENOTCONN, "Already closed by peer")

        def factory(family=socket.AF_INET, *args, **kwargs):
            kind = ConcurrentlyClosedSocket if already_closed and family == socket.AF_UNIX else original_socket
            return kind(family, *args, **kwargs)

        with patch.object(module.socket, "socket", side_effect=factory):
            worker = threading.Thread(target=server, daemon=True)
            worker.start()
            relay = module.UnixRelay(path)
            self.addCleanup(relay.close)
            relay.expected_disconnect.set()
            with socket.create_connection(("127.0.0.1", relay.port), timeout=3) as client:
                client.sendall(request)
                client.shutdown(socket.SHUT_WR)
                chunks = []
                while data := client.recv(32768):
                    chunks.append(data)
            worker.join(timeout=3)
            relay.finish()
        self.assertFalse(worker.is_alive())
        self.assertFalse([error for error in errors if not already_closed or error.errno != errno.ENOTCONN])
        self.assertEqual(received, [request])
        self.assertEqual(b"".join(chunks), response)
        self.assertTrue(relay.stats["tcpLowDelay"])
        self.assertEqual(relay.stats["toMacBytes"], len(request))
        self.assertEqual(relay.stats["fromMacBytes"], len(response))
        self.assertLessEqual(relay.stats["maximumQueuedBytes"], 256 * 1024)
        self.assertTrue(relay.stats["peerUidVerified"] and relay.stats["coordinatedCloseComplete"])
        self.assertEqual(relay.stats["drainedAtEof"], not already_closed)
        self.assertEqual(relay.stats["coordinatedPeerClose"], already_closed)
        if already_closed:
            self.assertEqual(relay.stats["peerCloseErrors"][0]["operation"], "empty-queue-shutdown")
            self.assertEqual(relay.stats["peerCloseErrors"][0]["queuedToMacBytes"], 0)

    def test_unexpected_eof_is_failure(self):
        path, listener = self.listener()
        relay = module.UnixRelay(path)
        with socket.create_connection(("127.0.0.1", relay.port), timeout=3):
            peer, _ = listener.accept()
            peer.close()
            relay.thread.join(timeout=3)
        self.assertFalse(relay.thread.is_alive())
        with self.assertRaisesRegex(RuntimeError, "before coordinated stop"):
            relay.close()

    def test_unexpected_viewer_reset_is_failure(self):
        path, listener = self.listener()
        relay = module.UnixRelay(path)
        with socket.create_connection(("127.0.0.1", relay.port), timeout=3) as client:
            peer, _ = listener.accept()
            peer.sendall(b"unread-video")
            deadline = time.monotonic() + 3
            while relay.stats["fromMacForwardedBytes"] == 0 and time.monotonic() < deadline:
                time.sleep(0.001)
            self.assertGreater(relay.stats["fromMacForwardedBytes"], 0)
            client.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        relay.thread.join(timeout=3)
        peer.close()
        self.assertFalse(relay.thread.is_alive())
        with self.assertRaisesRegex(RuntimeError, "before coordinated stop"):
            relay.close()
        self.assertFalse(relay.stats["coordinatedPeerClose"])

    def test_coordinated_full_close_records_undeliverable_video_tail(self):
        path, listener = self.listener()
        request, video = b"already-measured-input", b"v" * (8 * 1024 * 1024)
        received, errors = [], []

        def server():
            try:
                with listener.accept()[0] as peer:
                    peer.settimeout(3)
                    peer.sendall(video)
                    chunks = []
                    while data := peer.recv(32768):
                        chunks.append(data)
                    received.append(b"".join(chunks))
                    peer.shutdown(socket.SHUT_WR)
            except Exception as error:
                errors.append(error)

        worker = threading.Thread(target=server, daemon=True)
        worker.start()
        relay = module.UnixRelay(path)
        self.addCleanup(relay.close)
        relay.expected_disconnect.set()
        with socket.socket() as client:
            client.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            client.settimeout(3)
            client.connect(("127.0.0.1", relay.port))
            client.sendall(request)
            deadline = time.monotonic() + 3
            while relay.stats["fromMacBytes"] < 65536 and time.monotonic() < deadline:
                time.sleep(0.001)
            self.assertGreaterEqual(relay.stats["fromMacBytes"], 65536)
            # Mimic terminating a viewer with unread video in its TCP receive
            # queue, rather than the clean half-close of the other regression.
            client.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        worker.join(timeout=4)
        relay.finish()
        self.assertFalse(worker.is_alive())
        self.assertFalse(errors)
        self.assertEqual(received, [request])
        self.assertTrue(relay.stats["coordinatedPeerClose"] and relay.stats["coordinatedCloseComplete"])
        self.assertFalse(relay.stats["drainedAtEof"])
        self.assertTrue(relay.stats["kernelBufferedTailLossUnknown"])
        self.assertGreater(relay.stats["droppedFromMacTailBytes"], 0)
        self.assertEqual(relay.stats["queuedToMacBytes"], 0)
        self.assertEqual(relay.stats["queuedFromMacBytes"], 0)
        self.assertEqual(relay.stats["droppedToMacTailBytes"], 0)
        self.assertEqual(relay.stats["toMacForwardedBytes"], len(request))
        self.assertEqual(relay.stats["fromMacForwardedBytes"] + relay.stats["droppedFromMacTailBytes"], len(video))

    def test_cleanup_requires_exact_private_owner(self):
        path, listener = self.listener()
        owner = self.root / "owner.json"
        owner.write_text(json.dumps({"schema": 1, "container": "container", "token": "token"}))
        owner.chmod(0o600)
        with self.assertRaisesRegex(RuntimeError, "another run"):
            module.cleanup_socket(self.root, "container", "wrong")
        self.assertTrue(path.is_socket())
        listener.close()  # Mirrors coordinator: tunnel exits before unlink.
        self.assertTrue(module.cleanup_socket(self.root, "container", "token"))
        self.assertFalse(module.cleanup_socket(self.root, "container", "token"))
        path.symlink_to(owner)
        with self.assertRaisesRegex(RuntimeError, "different forward endpoint"):
            module.cleanup_socket(self.root, "container", "token")
        self.assertTrue(owner.is_file())


if __name__ == "__main__":
    unittest.main(verbosity=2)
