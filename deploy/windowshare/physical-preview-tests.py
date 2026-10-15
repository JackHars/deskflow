#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Image framing, private files, freshness and shutdown; no desktop or input."""
import importlib.util
import os
from pathlib import Path
import socket
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('preview', Path(__file__).with_name('physical-preview.py'))
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)


def frame(sequence=1, width=1280):
    # Protocol fixture only. The Qt test separately decodes an actual PNG.
    return b'DFP1' + struct.pack('>I', sequence) + preview.PNG + struct.pack('>I4sII', 13, b'IHDR', width, 720) + b'\0' * 9


class Preview(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / 'frame'
        self.sender, self.reader = socket.socketpair()
        self.addCleanup(self.sender.close)
        self.addCleanup(self.reader.close)
        self.now = 0
        self.receiver = preview.Receiver(self.reader, self.path, lambda: self.now)
        self.addCleanup(self.receiver.close)

    def send(self, data):
        self.sender.sendall(struct.pack('>I', len(data)) + data)
        self.receiver.poll()

    def test_fragmented_frames_publish_only_complete_new_images(self):
        data = frame()
        packet = struct.pack('>I', len(data)) + data
        for byte in packet[:-1]:
            self.sender.sendall(bytes([byte]))
            self.receiver.poll()
            self.assertFalse(self.path.exists())
        self.sender.sendall(packet[-1:])
        self.receiver.poll()
        self.assertEqual(preview.read_frame(self.path), data)
        self.assertTrue(self.receiver.fresh())
        self.send(frame(2))
        self.assertEqual(self.receiver.sequence, 2)
        self.assertEqual(self.path.stat().st_mode & 0o777, 0o600)
        self.assertFalse(self.path.with_suffix('.next').exists())

    def test_stale_and_closed_streams_are_never_live(self):
        self.send(frame())
        self.now = 2
        self.assertFalse(self.receiver.fresh())
        self.send(frame(2))
        self.assertTrue(self.receiver.fresh())
        self.sender.shutdown(socket.SHUT_WR)
        self.receiver.poll()
        self.assertFalse(self.receiver.fresh())

    def test_replay_is_rejected_without_republishing(self):
        self.send(frame(2))
        with self.assertRaisesRegex(RuntimeError, 'replayed'):
            self.send(frame(1))
        self.assertEqual(preview.read_frame(self.path), frame(2))

    def test_frozen_display_cannot_be_renewed_by_new_network_images(self):
        self.send(frame())
        receipt = dict(version=1, sequence=1, viewOnly=True, visible=True, fresh=True)
        self.assertTrue(self.receiver.visible(receipt))
        self.now = 2
        self.send(frame(2))
        self.assertFalse(self.receiver.visible(receipt))
        self.assertTrue(self.receiver.visible({**receipt, 'sequence': 2}))
        self.assertFalse(self.receiver.visible({**receipt, 'sequence': 2, 'visible': False}))
        with self.assertRaises(RuntimeError):
            self.receiver.visible(receipt)
        with self.assertRaises(RuntimeError):
            self.receiver.visible({**receipt, 'sequence': 3})

    def test_invalid_dimensions_sequence_and_size_fail_before_publication(self):
        for data in (frame(0), frame(1351), frame(width=4000), b'other data'):
            with self.subTest(data=data), self.assertRaises(RuntimeError):
                preview.publish(data, self.path)
        self.assertFalse(self.path.exists())
        self.sender.sendall(struct.pack('>I', preview.MAX_FRAME + 1))
        with self.assertRaisesRegex(RuntimeError, 'packet size'):
            self.receiver.poll()

    def test_truncated_stream_cannot_complete(self):
        self.sender.sendall(struct.pack('>I', len(frame())) + frame()[:-1])
        self.sender.shutdown(socket.SHUT_WR)
        with self.assertRaisesRegex(RuntimeError, 'partial frame'):
            self.receiver.poll()
        self.assertFalse(self.path.exists())

    def test_symlink_public_file_and_fifo_are_not_images(self):
        other = self.path.with_name('other')
        preview.publish(frame(), other)
        self.path.symlink_to(other)
        with self.assertRaises(OSError):
            preview.read_frame(self.path)
        self.path.unlink()
        other.chmod(0o644)
        with self.assertRaises(RuntimeError):
            preview.read_frame(other)
        os.mkfifo(self.path, 0o600)
        with self.assertRaises(RuntimeError):
            preview.read_frame(self.path)


if __name__ == '__main__':
    unittest.main()
