#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Deskflow Developers
# SPDX-License-Identifier: MIT
"""GJS/GIO descriptor tests with disposable socketpairs; no bus or compositor."""
import contextlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys


def child(reject=False):
    arguments = sys.argv[2:]
    if not arguments:
        return  # Legacy LaunchViewer adds no private descriptor option.
    assert arguments == ["--gnome-viewport-fd=4"], arguments
    if reject:
        try:
            endpoint = socket.socket(fileno=4)
        except OSError:
            return
        try:
            valid = (endpoint.family == socket.AF_UNIX and
                     endpoint.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE) == socket.SOCK_STREAM)
            try:
                endpoint.getpeername()
            except OSError:
                valid = False
            assert not valid, "Invalid endpoint was accepted by native child validation"
        finally:
            endpoint.close()
        return
    endpoint = socket.socket(fileno=4)
    assert endpoint.family == socket.AF_UNIX
    assert endpoint.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE) == socket.SOCK_STREAM
    endpoint.getpeername()
    assert not os.get_blocking(4), "Forwarding changed the core endpoint's nonblocking mode"
    endpoint.settimeout(3)
    endpoint.sendall(b"child-at-fd4")
    assert endpoint.recv(64) == b"parent-response"
    endpoint.close()


def run():
    gjs = shutil.which("gjs")
    if not gjs or not Path("/proc/self/fd").is_dir():
        print("SKIP: Linux GJS and /proc/self/fd are required")
        return 77
    fixture = Path(__file__).resolve()
    script = fixture.with_name("launcher.gjs.js")
    modes = ["forward", "legacy", "missing", "empty", "extra", "wrong-handle", "process-number",
             "header-count", "non-socket", "datagram", "unconnected", "inet", "reserved",
             "reserved-separate", "qt-override", "end-options", "callback-failure", "spawn-failure", "leak"]
    for mode in modes:
        with contextlib.ExitStack() as resources:
            left, right = socket.socketpair()
            resources.enter_context(left)
            resources.enter_context(right)
            right.setblocking(False)
            descriptors = [right.fileno()]
            if mode in ("missing", "empty", "legacy", "leak"):
                descriptors = []
            elif mode == "extra":
                descriptors.append(left.fileno())
            elif mode == "non-socket":
                read_fd, write_fd = os.pipe()
                resources.callback(os.close, read_fd)
                resources.callback(os.close, write_fd)
                descriptors = [read_fd]
            elif mode == "datagram":
                datagram_left, datagram_right = socket.socketpair(type=socket.SOCK_DGRAM)
                resources.enter_context(datagram_left)
                resources.enter_context(datagram_right)
                descriptors = [datagram_right.fileno()]
            elif mode == "unconnected":
                endpoint = resources.enter_context(socket.socket(socket.AF_UNIX, socket.SOCK_STREAM))
                descriptors = [endpoint.fileno()]
            elif mode == "inet":
                endpoint = resources.enter_context(socket.socket(socket.AF_INET, socket.SOCK_STREAM))
                descriptors = [endpoint.fileno()]
            command = [gjs, "-m", str(script), mode, sys.executable, str(fixture), *map(str, descriptors)]
            process = subprocess.Popen(command, pass_fds=descriptors, stdin=subprocess.DEVNULL,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       env={"PATH": "/usr/bin:/bin", "LANG": "C.UTF-8"})
            try:
                if mode == "forward":
                    right.close()
                    left.settimeout(5)
                    assert left.recv(64) == b"child-at-fd4"
                    left.sendall(b"parent-response")
                    left.settimeout(1)
                    assert left.recv(1) == b"", "Provider retained a parent-side descriptor after child close"
                    assert process.poll() is None, "EOF was established only by provider exit"
                output, errors = process.communicate(timeout=10)
                assert process.returncode == 0, (mode, output.decode(), errors.decode())
                assert not errors, (mode, errors.decode())
                assert output.decode().strip() == f"PASS {mode}"
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=3)
    print(json.dumps({"gjsDescriptorCases": len(modes), "desktopActions": False}))
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("--child", "--child-reject"):
        child(sys.argv[1] == "--child-reject")
    else:
        raise SystemExit(run())
