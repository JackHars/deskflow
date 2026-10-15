#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Bounded, view-only images from the fixture's own isolated GNOME desktop.

No input operations or display/bus discovery. Capture is a private, temporary
extension, separate from the production input provider. The SSH image stream
is separate from input and control channels; only the newest frame is retained.
GNOME API: https://github.com/GNOME/gnome-shell/blob/50.1/js/ui/screenshot.js
"""
import json
import os
from pathlib import Path
import selectors
import shutil
import stat
import struct
import time

UUID = 'physical-preview@deskflow.test'
MAX_FRAME = 4 * 1024 * 1024 + 8
PNG = b'\x89PNG\r\n\x1a\n'


def require(value, message):
    if not value:
        raise RuntimeError(message)


def frame_sequence(frame):
    require(41 <= len(frame) <= MAX_FRAME and frame[:4] == b'DFP1' and frame[8:16] == PNG,
            'Invalid bounded desktop preview frame')
    sequence, = struct.unpack('>I', frame[4:8])
    require(1 <= sequence <= 1350 and frame[20:24] == b'IHDR' and
            struct.unpack('>II', frame[24:32]) == (1280, 720), 'Wrong preview sequence or desktop dimensions')
    return sequence


def read_frame(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(descriptor, 'rb') as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode) and before.st_uid == os.getuid() and
                before.st_nlink == 1 and not before.st_mode & 0o077 and 41 <= before.st_size <= MAX_FRAME,
                'Desktop preview is not a private bounded file')
        frame = stream.read(MAX_FRAME + 1)
        after = os.fstat(stream.fileno())
        require((before.st_size, before.st_mtime_ns, before.st_ctime_ns) ==
                (after.st_size, after.st_mtime_ns, after.st_ctime_ns) and len(frame) == before.st_size,
                'Desktop preview changed during its read')
    frame_sequence(frame)
    return frame


def install(processes, root):
    require(Path('/.dockerenv').is_file() and os.getuid() != 0 and
            root.parent == Path('/tmp') and root.name.startswith('df-physical-'),
            'Preview capture requires the disposable container profile')
    target = Path(processes.env['XDG_DATA_HOME']) / 'gnome-shell/extensions' / UUID
    target.mkdir(parents=True, mode=0o700)
    (target / 'metadata.json').write_text(json.dumps({'uuid': UUID, 'name': 'Disposable test desktop preview',
                                                    'description': 'View-only private physical fixture',
                                                    'shell-version': ['50']}))
    shutil.copyfile(Path(__file__).with_suffix('.js'), target / 'extension.js')
    directory = root / 'preview'
    directory.mkdir(mode=0o700)
    processes.env['DESKFLOW_PRIVATE_PREVIEW'] = str(directory / 'frame')
    return directory / 'frame'


def enable(processes, common):
    # Both extensions were copied before the disposable Shell started.
    processes.run('enable-preview', ['gsettings', 'set', 'org.gnome.shell', 'enabled-extensions',
                                    f"['{common.UUID}', '{UUID}']"])


def publish(frame, path):
    frame_sequence(frame)
    temporary = path.with_suffix('.next')
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(frame)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


class Receiver:
    """Nonblocking SSH reader. Freshness is local receipt age, never remote time."""
    def __init__(self, stream, path, now=time.monotonic):
        self.stream, self.path, self.now = stream, path, now
        self.selector = selectors.DefaultSelector()
        self.selector.register(stream, selectors.EVENT_READ)
        self.buffer = b''
        self.sequence = 0
        self.received = None
        self.display_sequence = 0
        self.display_received = None
        self.eof = False

    def poll(self):
        for _ in range(16):
            if not self.selector.select(0):
                break
            block = os.read(self.stream.fileno(), 65536)
            if not block:
                self.eof = True
                break
            self.buffer += block
            require(len(self.buffer) <= MAX_FRAME + 65540, 'Desktop preview stream exceeded its bound')
            while len(self.buffer) >= 4:
                length, = struct.unpack('>I', self.buffer[:4])
                require(41 <= length <= MAX_FRAME, 'Invalid desktop preview packet size')
                if len(self.buffer) < length + 4:
                    break
                frame, self.buffer = self.buffer[4:length + 4], self.buffer[length + 4:]
                sequence = frame_sequence(frame)
                require(sequence > self.sequence, 'Desktop preview replayed or reversed a frame')
                publish(frame, self.path)
                self.sequence, self.received = sequence, self.now()
        require(not self.eof or not self.buffer, 'Desktop preview ended with a partial frame')

    def fresh(self):
        return not self.eof and self.received is not None and self.now() - self.received < 2

    def visible(self, receipt):
        sequence = receipt.get('sequence')
        require(receipt.get('version') == 1 and receipt.get('viewOnly') is True and
                type(sequence) is int and self.display_sequence <= sequence <= self.sequence and
                type(receipt.get('visible')) is bool and type(receipt.get('fresh')) is bool,
                'Invalid desktop preview display receipt')
        if sequence > self.display_sequence:
            self.display_sequence, self.display_received = sequence, self.now()
        return self.fresh() and receipt['visible'] and receipt['fresh'] and \
            self.display_received is not None and self.now() - self.display_received < 2

    def close(self):
        self.selector.close()
