#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Linux half of an explicitly coordinated disposable Mac/Wayland acceptance run.

No Mac process is started here. The production viewer performs mutual pinned
TLS through a byte-only relay to an SSH-owned Unix forwarding socket. All
compositor/EIS activity stays in a new native headless GNOME session inside a
network-isolated container. JSON stdin is controlled by the Mac coordinator.
"""

from __future__ import annotations

import argparse
import contextlib
import errno
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import select
import selectors
import socket
import ssl
import stat
import struct
import subprocess
import sys
import tempfile
import threading
import time
import traceback

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("crossmachine_qt_common", HERE / "gnome-qt-viewer-tests.py")
qt = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(qt)
common, require = qt.common, qt.require


def emit(value):
    encoded = json.dumps(value, separators=(",", ":"), allow_nan=False)
    require(len(encoded) <= 32768, "Coordinator response exceeded its bound")
    print(encoded, flush=True)


def fingerprint(identity):
    certificate = identity / "cert.pem"
    key = identity / "key.pem"
    for file in (certificate, key):
        info = file.lstat()
        require(stat.S_ISREG(info.st_mode) and info.st_uid == os.getuid(), "Identity must be owned regular files")
    require(not key.stat().st_mode & 0o077, "Private identity key permissions are too broad")
    der = ssl.PEM_cert_to_DER_cert(certificate.read_text())
    return hashlib.sha256(der).hexdigest()


def isolated_linux():
    require(sys.platform == "linux" and os.geteuid() != 0 and Path("/.dockerenv").is_file(),
            "Run as an unprivileged user in the dedicated disposable Linux container")


class UnixRelay:
    """One TCP client, bounded buffers, no TLS termination or payload logging."""

    def __init__(self, path, *, lifetime_seconds=180):
        require(type(lifetime_seconds) is int and 30 <= lifetime_seconds <= 600,
                "Relay lifetime must be an integer from 30 to 600 seconds")
        self.lifetime_seconds = lifetime_seconds
        info = path.lstat()
        require(stat.S_ISSOCK(info.st_mode) and info.st_uid == os.getuid() and not info.st_mode & 0o077,
                "Forward endpoint must be an owned private Unix socket")
        self.path = path
        self.identity = (info.st_dev, info.st_ino)
        self.stopping = threading.Event()
        self.expected_disconnect = threading.Event()
        self.failure = None
        self.stats = {"connections": 0, "toMacBytes": 0, "fromMacBytes": 0,
                      "toMacForwardedBytes": 0, "fromMacForwardedBytes": 0,
                      "droppedFromMacTailBytes": 0, "droppedToMacTailBytes": 0,
                      "queuedToMacBytes": 0, "queuedFromMacBytes": 0,
                      "coordinatedPeerClose": False, "coordinatedCloseComplete": False,
                      "peerCloseErrors": [], "kernelBufferedTailLossUnknown": False,
                      "maximumQueuedBytes": 0, "peerUidVerified": False, "drainedAtEof": False}
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(0.1)
        self.port = self.listener.getsockname()[1]
        self.thread = threading.Thread(target=self.run, name="private-tls-unix-relay", daemon=True)
        self.thread.start()

    def run(self):
        deadline = time.monotonic() + self.lifetime_seconds
        queues = None
        try:
            with contextlib.ExitStack() as cleanup:
                client = None
                while not self.stopping.is_set() and time.monotonic() < deadline:
                    try:
                        client, _ = self.listener.accept()
                        break
                    except TimeoutError:
                        pass
                if self.stopping.is_set():
                    return
                require(client is not None, "Relay acceptance deadline expired")
                cleanup.callback(client.close)
                self.listener.close()  # A reconnect is a new coordinated run.
                info = self.path.lstat()
                require((info.st_dev, info.st_ino) == self.identity, "Forward socket changed before connection")
                remote = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                cleanup.callback(remote.close)
                remote.settimeout(5)
                remote.connect(str(self.path))
                credentials = remote.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12)
                _, uid, _ = struct.unpack("3i", credentials)
                require(uid == os.getuid(), "Forward socket peer has a different user")
                # A host PID can be zero inside this PID namespace. The socket
                # owner/user check is transport isolation, never TLS authority.
                self.stats["peerUidVerified"] = True
                self.stats["connections"] = 1
                client.setblocking(False)
                remote.setblocking(False)
                queues = {client: bytearray(), remote: bytearray()}
                peers = {client: remote, remote: client}
                eof, write_closed = set(), set()
                closed_viewer_sink = False
                drain_deadline = deadline
                limit = 256 * 1024

                def viewer_closed(error, operation):
                    nonlocal closed_viewer_sink, drain_deadline
                    require(self.expected_disconnect.is_set(), "Actual TLS viewer closed before coordinated stop")
                    closed_viewer_sink = True
                    write_closed.add(client)
                    self.stats["coordinatedPeerClose"] = True
                    self.stats["kernelBufferedTailLossUnknown"] = True
                    self.stats["peerCloseErrors"].append({"side": "viewer", "operation": operation,
                                                          "errno": error.errno,
                                                          "queuedToMacBytes": len(queues[remote]),
                                                          "queuedFromMacBytes": len(queues[client])})
                    self.stats["droppedFromMacTailBytes"] += len(queues[client])
                    queues[client].clear()
                    drain_deadline = min(drain_deadline, time.monotonic() + 3)

                while not self.stopping.is_set() and time.monotonic() < deadline:
                    if len(eof) == 2 and not any(queues.values()):
                        self.stats["drainedAtEof"] = not self.stats["coordinatedPeerClose"]
                        self.stats["coordinatedCloseComplete"] = True
                        return
                    # Forward every already-read byte before propagating EOF.
                    # A TLS close can arrive in the same read batch as payload.
                    for endpoint in eof:
                        target = peers[endpoint]
                        if not queues[target] and target not in write_closed:
                            try:
                                target.shutdown(socket.SHUT_WR)
                            except OSError as error:
                                # The other direction may already have fully
                                # closed between poll/read and this empty-queue
                                # half-close. No buffered byte is waived here.
                                if error.errno != errno.ENOTCONN or not self.expected_disconnect.is_set():
                                    raise
                                self.stats["coordinatedPeerClose"] = True
                                self.stats["kernelBufferedTailLossUnknown"] = True
                                self.stats["peerCloseErrors"].append({
                                    "side": "viewer" if target is client else "mac-forward",
                                    "operation": "empty-queue-shutdown", "errno": error.errno,
                                    "queuedToMacBytes": len(queues[remote]), "queuedFromMacBytes": len(queues[client])})
                            write_closed.add(target)
                    require(time.monotonic() < drain_deadline, "Relay EOF drain exceeded three seconds")
                    reading = [s for s in peers if s not in eof and len(queues[peers[s]]) < limit]
                    writing = [s for s in peers if queues[s]]
                    readable, writable, _ = select.select(reading, writing, [], 0.05)
                    for endpoint in writable:
                        try:
                            count = endpoint.send(queues[endpoint])
                        except BlockingIOError:
                            continue
                        except (BrokenPipeError, ConnectionResetError) as error:
                            # CloseSession terminates the owned viewer. Its
                            # unread encrypted tail is not input delivery evidence.
                            # Never discard bytes queued toward the Mac host.
                            if endpoint is not client:
                                raise
                            viewer_closed(error, "send")
                            continue
                        require(count > 0, "Relay write ended unexpectedly")
                        del queues[endpoint][:count]
                        self.stats["toMacForwardedBytes" if endpoint is remote else "fromMacForwardedBytes"] += count
                    for endpoint in readable:
                        target = peers[endpoint]
                        try:
                            data = endpoint.recv(min(16384, limit - len(queues[target])))
                        except BlockingIOError:
                            continue
                        except ConnectionResetError as error:
                            if endpoint is not client:
                                raise
                            viewer_closed(error, "receive")
                            data = b""
                        if not data:
                            require(self.expected_disconnect.is_set() or self.stopping.is_set(),
                                    "Actual TLS connection ended before coordinated stop")
                            eof.add(endpoint)
                            drain_deadline = min(drain_deadline, time.monotonic() + 3)
                            continue
                        field = "toMacBytes" if endpoint is client else "fromMacBytes"
                        self.stats[field] += len(data)
                        require(self.stats[field] <= 512 * 1024 * 1024, "Relay byte budget exceeded")
                        if target is client and closed_viewer_sink:
                            self.stats["droppedFromMacTailBytes"] += len(data)
                            continue
                        queues[target].extend(data)
                        self.stats["maximumQueuedBytes"] = max(self.stats["maximumQueuedBytes"], len(queues[target]))
                require(self.stopping.is_set(), f"Relay lifetime exceeded {self.lifetime_seconds} seconds")
        except Exception:
            if not self.stopping.is_set():
                self.failure = traceback.format_exc()
        finally:
            if queues is not None:
                self.stats["queuedToMacBytes"] = len(queues[remote])
                self.stats["queuedFromMacBytes"] = len(queues[client])

    def check(self):
        require(self.failure is None, f"Private relay failed: {self.failure}")

    def close(self):
        self.stopping.set()
        self.listener.close()
        self.thread.join(timeout=2)
        require(not self.thread.is_alive(), "Private relay did not stop within its bound")
        self.check()

    def finish(self):
        self.thread.join(timeout=4)
        self.check()
        require(not self.thread.is_alive() and self.stats["coordinatedCloseComplete"]
                and self.stats["queuedToMacBytes"] == self.stats["droppedToMacTailBytes"] == 0,
                "Private relay did not complete bounded coordinated closure")


class Commands:
    def __init__(self, descriptor=None):
        self.descriptor = sys.stdin.fileno() if descriptor is None else descriptor
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.descriptor, selectors.EVENT_READ)
        self.buffer = b""
        self.last = 0
        self.count = 0

    def close(self):
        self.selector.close()

    def next(self):
        if b"\n" not in self.buffer:
            if not self.selector.select(0.05):
                return None
            data = os.read(self.descriptor, 4096)
            require(data, "Coordinator ended control before a clean stop")
            self.buffer += data
            require(len(self.buffer) <= 4096, "Coordinator input exceeded its bound")
            if b"\n" not in self.buffer:
                return None
        line, self.buffer = self.buffer.split(b"\n", 1)

        def unique(pairs):
            result = {}
            for name, value in pairs:
                require(name not in result, "Duplicate coordinator field")
                result[name] = value
            return result
        value = json.loads(line, object_pairs_hook=unique)
        require(isinstance(value, dict), "Coordinator command must be an object")
        request = value.get("id")
        require(type(request) is int and self.last < request <= 2**53 - 1, "Nonmonotonic coordinator id")
        self.last = request
        self.count += 1
        require(self.count <= 128, "Coordinator command budget exceeded")
        operation = value.get("op")
        fields = {"move": {"x", "y"}, "key": {"code"}, "scroll": {"dx", "dy"},
                  "prepare": set(), "observe": set(), "retained": set(), "click": set(),
                  "sync": set(), "stop": set()}
        require(isinstance(operation, str) and operation in fields and set(value) == fields[operation] | {"id", "op"},
                "Invalid coordinator command fields")
        for key in ("x", "y") if operation == "move" else ():
            require(type(value[key]) in (int, float) and math.isfinite(value[key]), "Invalid motion coordinate")
        if operation == "key":
            require(type(value["code"]) is int and 1 <= value["code"] <= 767, "Invalid evdev key stroke")
        if operation == "scroll":
            require(all(type(value[k]) is int and -1200 <= value[k] <= 1200
                        for k in ("dx", "dy")) and (value["dx"] or value["dy"]), "Invalid discrete scroll")
        return value


def run(arguments):
    isolated_linux()
    require(arguments.output.is_absolute() and not arguments.output.exists(), "Output must be a new absolute path")
    require(re.fullmatch("[0-9a-f]{64}", arguments.peer_pin) and arguments.peer_pin != "0" * 64,
            "Mac pin must be explicit canonical nonzero SHA256")
    arguments.output.mkdir(mode=0o700, parents=True)
    report = {"schema": 1, "status": "failed", "scope": "real-mac-to-private-gnome-viewer",
              "originalInputAuthorityGranted": False, "userLinuxDesktopUsed": False,
              "macReceiptVerification": "external-coordinator-required", "scaleSource": "QT_SCALE_FACTOR",
              "qtScaleFactor": arguments.scale, "compositorMonitorScalingTested": False, "commands": []}
    stop_reply = None
    relay = None
    try:
        with contextlib.ExitStack() as cleanup:
            root = Path(cleanup.enter_context(tempfile.TemporaryDirectory(prefix="df-cross-gnome-", dir="/tmp")))
            environment = common.private_environment(root)
            environment["QT_SCALE_FACTOR"] = str(arguments.scale)
            processes = common.Processes(arguments.output, environment)
            cleanup.callback(processes.close)
            relay = UnixRelay(arguments.forward_socket)
            cleanup.callback(relay.close)
            shell = qt.start_shell(processes, root, report)
            configuration = root / "provider.json"
            configuration.write_text(json.dumps({"version": 1, "viewerExecutable": str(arguments.viewer),
                                                 "connect": "127.0.0.1", "port": relay.port,
                                                 "identity": str(arguments.identity), "peerPin": arguments.peer_pin,
                                                 "exportWindow": str(arguments.window_id)}))
            driver = qt.Driver(processes, arguments.fixture, configuration)
            ready = driver.event("ready", timeout=20)
            require(ready.get("eisPeerPid") == shell.pid, "EIS peer is not this private compositor")
            report["ready"] = ready
            report["viewerPin"] = fingerprint(arguments.identity)
            emit({**ready, "schema": 1, "viewerPin": report["viewerPin"], "peerPin": arguments.peer_pin,
                  "windowId": arguments.window_id})
            controls = Commands()
            cleanup.callback(controls.close)
            prepared = False

            def checked_observation():
                processes.check()
                relay.check()
                reply = driver.command("observe")
                if reply.get("live"):
                    view = reply["snapshot"]
                    require(view["session"] == ready["session"] and view["generation"] == ready["generation"]
                            and view["viewer"] == ready["viewer"], "Provider identity changed")
                    require(view["paint"]["peerFingerprint"] == arguments.peer_pin,
                            "Painted viewer has a different authenticated Mac peer")
                return reply

            while True:
                processes.check()
                relay.check()
                require(driver.child.poll() is None, "Production provider fixture exited before stop")
                command = controls.next()
                if command is None:
                    continue
                operation = command["op"]
                report["commands"].append({"request": command})
                if operation == "stop":
                    relay.expected_disconnect.set()
                    reply = driver.command("stop")
                    require(reply.get("live") is False and driver.child.wait(timeout=5) == 0,
                            "Production client did not stop cleanly")
                    relay.finish()
                    stop_reply = {"event": "stopped", "id": command["id"], "live": False}
                    report["commands"][-1]["reply"] = stop_reply
                    report["relay"] = dict(relay.stats)
                    report["productionClientExit"] = 0
                    break
                if operation == "prepare":
                    require(not prepared, "Prepare is allowed only once")
                    x, y, width, height = ready["region"]
                    driver.command("move", x=x, y=y + height / 2)
                    driver.command("move", x=x + width / 2, y=y + height / 2)
                    driver.command("escape")

                    def first_paint():
                        value = checked_observation()
                        report["lastStartupObservation"] = value
                        if not value.get("live"):
                            qt.denied(value, "Initial real Mac painted readiness")
                            return None
                        return value
                    reply = common.wait_for(processes, first_paint, "actual Mac captured painted viewport",
                                            timeout=10, retry_errors=False)
                    reply["event"] = "prepared"
                    view = reply["snapshot"]
                    reply["surfaceDesktopOrigin"] = {"x": view["desktopX"] - view["surfaceX"],
                                                       "y": view["desktopY"] - view["surfaceY"]}
                    reply["setupInputs"] = "private-corner-exit-center-escape; baseline Mac receipts after this reply"
                    prepared = True
                else:
                    require(prepared, "Explicit prepare is required before measured commands")
                    reply = checked_observation() if operation == "observe" else driver.command(
                        operation, **{k: v for k, v in command.items() if k not in ("op", "id")})
                reply["fixtureId"] = reply["id"]
                reply["id"] = command["id"]
                report["commands"][-1]["reply"] = reply
                emit(reply)
        report["status"] = "passed"
        report["cleanOwnedProcessAndRelayStop"] = True
    except Exception as error:
        report["status"] = "failed"
        report["failure"] = str(error)
        (arguments.output / "failure.log").write_text(traceback.format_exc())
    if relay is not None:
        report["relay"] = dict(relay.stats)
    (arguments.output / "result.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if report["status"] == "passed":
        emit({**stop_reply, "cleanOwnedProcessAndRelayStop": True, "relay": report["relay"]})
        return 0
    emit({"event": "failed", "error": report["failure"]})
    return 1


def initialize(arguments):
    isolated_linux()
    require(arguments.identity.is_absolute() and not arguments.identity.exists(), "Identity path must be new")
    environment = {"PATH": "/usr/bin:/bin", "LANG": "C.UTF-8", "QT_QPA_PLATFORM": "offscreen", "HOME": "/tmp"}
    result = subprocess.run([str(arguments.viewer), "--identity", str(arguments.identity), "--init-identity"],
                            env=environment, capture_output=True, text=True, timeout=20, check=True)
    pin = fingerprint(arguments.identity)
    require(result.stdout.strip().lower().replace(":", "") == pin, "Created identity fingerprint differs")
    emit({"event": "identity", "viewerPin": pin})
    return 0


def cleanup_container(arguments):
    require(re.fullmatch(r"deskflow-cross-[0-9a-f]{16}", arguments.container)
            and re.fullmatch(r"[0-9a-f]{32}", arguments.token), "Invalid exact-owned container identity")
    result = subprocess.run(["docker", "inspect", arguments.container], capture_output=True, text=True, timeout=10)
    if result.returncode:
        require("no such object:" in result.stderr.lower(), "Cannot inspect the owned container")
    else:
        info, = json.loads(result.stdout)
        require((info["Config"]["Labels"] or {}).get("org.deskflow.crossmachine.run") == arguments.token,
                "Refusing cleanup of a container with different ownership")
        subprocess.run(["docker", "rm", "-f", info["Id"]], check=True, capture_output=True, timeout=15)
    removed = cleanup_socket(arguments.forward_directory, arguments.container, arguments.token)
    emit({"event": "cleaned", "container": arguments.container, "forwardSocketRemoved": removed})
    return 0


def cleanup_socket(directory, container, token):
    """After SSH exits, unlink only the socket in this token-owned directory."""
    require(directory.is_absolute(), "Forward directory must be absolute")
    descriptor = os.open(directory, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        info = os.fstat(descriptor)
        require(info.st_uid == os.getuid() and not info.st_mode & 0o077, "Forward directory is not private/owned")
        owner = os.open("owner.json", os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=descriptor)
        try:
            info = os.fstat(owner)
            require(stat.S_ISREG(info.st_mode) and info.st_uid == os.getuid() and info.st_nlink == 1
                    and not info.st_mode & 0o077 and info.st_size <= 512, "Invalid forward ownership record")
            record = os.read(owner, 513)
            require(len(record) <= 512 and json.loads(record) == {"schema": 1, "container": container, "token": token},
                    "Forward directory belongs to another run")
        finally:
            os.close(owner)
        try:
            socket_info = os.stat("mac.sock", dir_fd=descriptor, follow_symlinks=False)
        except FileNotFoundError:
            return False
        require(stat.S_ISSOCK(socket_info.st_mode) and socket_info.st_uid == os.getuid()
                and not socket_info.st_mode & 0o077, "Refusing removal of a different forward endpoint")
        os.unlink("mac.sock", dir_fd=descriptor)
        return True
    finally:
        os.close(descriptor)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    init = modes.add_parser("init-identity")
    init.add_argument("--viewer", type=lambda p: Path(p).resolve(strict=True), required=True)
    init.add_argument("--identity", type=lambda p: Path(p).absolute(), required=True)
    test = modes.add_parser("run")
    test.add_argument("--viewer", type=lambda p: Path(p).resolve(strict=True), required=True)
    test.add_argument("--fixture", type=lambda p: Path(p).resolve(strict=True), required=True)
    test.add_argument("--identity", type=lambda p: Path(p).resolve(strict=True), required=True)
    test.add_argument("--forward-socket", type=lambda p: Path(p).absolute(), required=True)
    test.add_argument("--peer-pin", required=True)
    test.add_argument("--window-id", type=int, required=True)
    test.add_argument("--output", type=lambda p: Path(p).absolute(), required=True)
    test.add_argument("--scale", type=float, choices=(1, 1.25), default=1)
    clean = modes.add_parser("cleanup")
    clean.add_argument("--container", required=True)
    clean.add_argument("--token", required=True)
    clean.add_argument("--forward-directory", type=lambda p: Path(p).absolute(), required=True)
    args = parser.parse_args()
    if args.mode == "run":
        require(1 <= args.window_id <= 2**32 - 1, "Window id must be an explicit nonzero uint32")
        return run(args)
    return initialize(args) if args.mode == "init-identity" else cleanup_container(args)


if __name__ == "__main__":
    raise SystemExit(main())
