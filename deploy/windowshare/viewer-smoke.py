#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Exercise the real viewer against a disposable loopback TLS fixture.

No native host, window export, event tap, input injection, or display mutation.
Run against either a build executable or a packaged executable. All identities
and logs are temporary, and only this script's viewer process is terminated.
"""

import hashlib
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import time


HEADER = struct.Struct(">4sIHHQQQI")


def wait_for(predicate, description, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise RuntimeError(description)


def read_exact(connection, size):
    result = bytearray()
    while len(result) < size:
        block = connection.recv(size - len(result))
        if not block:
            raise RuntimeError("Viewer closed before negotiation")
        result.extend(block)
    return bytes(result)


def read_hello(connection):
    magic, size, kind, flags, session, epoch, sequence, reserved = HEADER.unpack(
        read_exact(connection, HEADER.size)
    )
    if (magic, kind, flags, session, epoch, sequence, reserved) != (
        b"DSW1", 1, 0, 0, 0, 1, 0
    ) or size > 65536:
        raise RuntimeError("Viewer did not start a fresh protocol negotiation")
    hello = json.loads(read_exact(connection, size))
    if hello.pop("capabilities", None) not in (None, ["keyboard-route-v1"]):
        raise RuntimeError("Unexpected viewer capabilities")
    if hello != {
        "version": 1, "role": "viewer", "codec": "h264"
    }:
        raise RuntimeError("Unexpected viewer negotiation")


def send(connection, kind, sequence, payload):
    body = json.dumps(payload, separators=(",", ":")).encode()
    connection.sendall(HEADER.pack(b"DSW1", len(body), kind, 0, 0, 0, sequence, 0) + body)


def main(executable):
    executable = str(Path(executable).resolve(strict=True))
    environment = os.environ.copy()
    for name in ("DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "QT_PLUGIN_PATH", "QML2_IMPORT_PATH"):
        environment.pop(name, None)
    environment["QT_QPA_PLATFORM"] = "offscreen"
    environment["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"

    def command(*arguments):
        result = subprocess.run(
            [executable, *arguments], env=environment, text=True,
            capture_output=True, timeout=30, check=True
        )
        return result.stdout.strip()

    command("--help")
    with tempfile.TemporaryDirectory(prefix="deskflow-viewer-smoke-") as directory:
        root = Path(directory)
        host_identity, viewer_identity = root / "host", root / "viewer"
        host_pin = command("--identity", str(host_identity), "--init-identity")
        viewer_pin = command("--identity", str(viewer_identity), "--init-identity")
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(host_identity / "cert.pem", host_identity / "key.pem")
        context.load_verify_locations(cafile=viewer_identity / "cert.pem")
        context.verify_mode = ssl.CERT_REQUIRED
        # Reserve an ephemeral port, initially without a listener, to exercise
        # an actual refused connection before the fixture becomes available.
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        log_path = root / "viewer.log"
        with log_path.open("w") as log:
            viewer = subprocess.Popen(
                [executable, "--enable-seamless", "--headless", "--connect", "127.0.0.1",
                 "--port", str(port), "--identity", str(viewer_identity), "--peer-pin", host_pin,
                 "--quit-after", "30"],
                env=environment, stdout=log, stderr=log
            )
            try:
                def output():
                    text = log_path.read_text()
                    if viewer.poll() is not None:
                        raise RuntimeError("Viewer exited early: " + text[-2000:])
                    return text

                wait_for(lambda: "Retrying in" in output(), "Viewer did not retry a refused connection")
                with socket.socket() as listener:
                    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    listener.bind(("127.0.0.1", port))
                    listener.listen(1)
                    listener.settimeout(12)
                    for attempt in range(2):
                        socket_peer, _ = listener.accept()
                        socket_peer.settimeout(5)
                        with context.wrap_socket(socket_peer, server_side=True) as connection:
                            actual_pin = hashlib.sha256(connection.getpeercert(binary_form=True)).hexdigest()
                            if actual_pin != viewer_pin.lower().replace(":", ""):
                                raise RuntimeError("Fixture did not authenticate the approved viewer")
                            read_hello(connection)
                            send(connection, 2, 1, {"version": 1, "role": "host", "codec": "h264"})
                            send(connection, 3, 2, {"windows": []})
                            wait_for(
                                lambda: output().count("Connected to approved peer") >= attempt + 1,
                                "Viewer did not accept the fixture host"
                            )
                            if attempt == 1:
                                viewer.terminate()
                                if viewer.wait(timeout=8) != 0:
                                    raise RuntimeError("Viewer did not exit cleanly")
                        # The first authenticated disconnect must yield a new
                        # connection and Hello with a fresh sequence counter.
                print(json.dumps({
                    "viewer_cli": "PASS", "mutually_pinned_tls": "PASS",
                    "refused_then_available": "PASS", "authenticated_reconnect": "PASS",
                    "clean_exit": "PASS", "native_host_started": False,
                }, sort_keys=True))
            except Exception as error:
                raise RuntimeError(str(error) + "\n" + log_path.read_text()[-3000:]) from error
            finally:
                if viewer.poll() is None:
                    viewer.terminate()
                    try:
                        viewer.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        viewer.kill()
                        viewer.wait()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: viewer-smoke.py VIEWER_EXECUTABLE")
    main(sys.argv[1])
