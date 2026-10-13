#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Actual QtWayland painted-viewport acceptance on a private native GNOME Shell.

The TLS host uses disposable mutually pinned identities and synthetic H264. All
owned-session provider calls are made by GnomeProviderClient in the C++ fixture,
including native hit tests. This observes real legacy viewer pointer messages;
it never grants original-input delivery or attaches to the user's compositor.
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import queue
import select
import selectors
import shutil
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import traceback

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("gnome_fixture_common", HERE / "gnome-provider-tests.py")
common = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(common)
require = common.require
HEADER = struct.Struct(">4sIHHQQQI")
INPUT_ROUTE = 18
FORWARDING_CAPABILITIES = ["keyboard-route-v1", "input-route-v1"]
INPUT_KINDS = {"key", "button", "move", "scroll"}
FOCUS_RECOVERY_MODES = ("prepared-first-click", "ordinary-click")


class TlsHost:
    """One bounded fixture thread owns both TLS reads and writes."""

    def __init__(self, processes, root, viewer):
        self.processes = processes
        self.root = root
        self.messages = []
        self.lock = threading.Lock()
        self.commands = queue.Queue(maxsize=16)
        self.stopping = threading.Event()
        self.failure = None
        self.connection = None
        self.expected_disconnect = False
        self.sessions = {}
        self.output_sequence = 0
        self.input_sequence = 0
        self.pending_route = None
        self.input_route_negotiated = False
        self.host_identity, self.viewer_identity = root / "host-identity", root / "viewer-identity"
        environment = dict(processes.env, QT_QPA_PLATFORM="offscreen")

        def identity(path):
            result = subprocess.run([str(viewer), "--identity", str(path), "--init-identity"],
                                    env=environment, capture_output=True, text=True, timeout=20, check=True)
            pin = result.stdout.strip().lower().replace(":", "")
            require(len(pin) == 64 and all(c in "0123456789abcdef" for c in pin), "Invalid disposable TLS pin")
            return pin

        self.host_pin, self.viewer_pin = identity(self.host_identity), identity(self.viewer_identity)
        frame = subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
             "testsrc2=size=640x480:rate=1", "-frames:v", "1", "-c:v", "libx264", "-preset", "ultrafast",
             "-tune", "zerolatency", "-pix_fmt", "yuv420p", "-f", "h264", "pipe:1"],
            env=processes.env, capture_output=True, timeout=20, check=True,
        )
        self.frame = frame.stdout
        require(0 < len(self.frame) < 1024 * 1024, "Synthetic H264 access unit exceeds fixture bound")
        (processes.output / "synthetic-frame.h264").write_bytes(self.frame)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(self.host_identity / "cert.pem", self.host_identity / "key.pem")
        context.load_verify_locations(cafile=self.viewer_identity / "cert.pem")
        context.verify_mode = ssl.CERT_REQUIRED
        self.context = context
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(0.1)
        self.port = self.listener.getsockname()[1]
        self.log = (processes.output / "tls-packets.jsonl").open("x", buffering=1)
        self.thread = threading.Thread(target=self.run, name="private-pinned-tls-host", daemon=True)
        self.thread.start()

    def check(self):
        self.processes.check()
        require(self.failure is None, f"TLS fixture failed: {self.failure}")

    def record(self, direction, kind, session, epoch, value):
        entry = {"time": time.monotonic(), "direction": direction, "kind": kind,
                 "session": session, "epoch": epoch, "payload": value}
        with self.lock:
            require(len(self.messages) < 4096, "TLS fixture message bound exceeded")
            self.messages.append(entry)
        self.log.write(json.dumps(entry) + "\n")

    def events(self):
        self.check()
        with self.lock:
            return list(self.messages)

    def send(self, kind, payload, session=0, epoch=0, binary=False):
        self.output_sequence += 1
        body = payload if binary else json.dumps(payload, separators=(",", ":")).encode()
        self.connection.sendall(HEADER.pack(b"DSW1", len(body), kind, 0, session, epoch,
                                            self.output_sequence, 0) + body)
        self.record("host", kind, session, epoch,
                    {"bytes": len(body), "sha256": hashlib.sha256(body).hexdigest()} if binary else payload)

    def opened(self, session, *, advance=False, frame=True, resize=None):
        previous = self.sessions.get(session)
        geometry = previous["geometry"] + 1 if advance and previous else 1
        width, height = (resize["width"], resize["height"]) if resize else (960, 540)
        state = {"geometry": geometry, "width": width, "height": height, "focusRevision": 0}
        self.sessions[session] = state
        metadata = {"geometry": geometry, "width": width, "height": height,
                    "title": "Disposable real Qt viewport", "codec": "h264"}
        if previous:
            metadata["phase"] = "remote"
        if resize:
            metadata["resizeRequest"] = resize["request"]
        self.send(5, metadata, session, 1)
        if frame:
            self.send_frame(session)

    def send_frame(self, session):
        self.send(6, struct.pack(">Q", self.sessions[session]["geometry"]) + self.frame, session, 1, binary=True)

    def packet(self, kind, session, epoch, value):
        if kind == 1:
            require(self.input_sequence == 1 and session == epoch == 0
                    and value.get("version") == 1 and value.get("role") == "viewer"
                    and value.get("codec") == "h264", "Invalid real viewer Hello")
            capabilities = value.get("capabilities")
            require(isinstance(capabilities, list) and all(isinstance(item, str) for item in capabilities)
                    and len(capabilities) == len(set(capabilities))
                    and all(item in capabilities for item in FORWARDING_CAPABILITIES),
                    "Real viewer did not negotiate both forwarding capabilities")
            self.input_route_negotiated = True
            self.send(2, {"version": 1, "role": "host", "codec": "h264",
                          "capabilities": FORWARDING_CAPABILITIES})
            self.send(3, {"windows": []})
            self.opened(1)
        elif kind == 11:
            self.send(12, {})
        elif kind == 12 or kind == 3:
            pass
        elif kind == 16:
            require(epoch == 1 and session in self.sessions
                    and value.get("geometry") == self.sessions[session]["geometry"], "Unexpected painted Ready")
            self.send(2, {"kind": "committed"}, session, epoch)
        elif kind == 7:
            require(epoch == 1 and session in self.sessions, "Input for unknown fixture session")
            if value.get("kind") == "focus":
                state = self.sessions[session]
                require(value.get("active") is True and value.get("geometry") == state["geometry"],
                        "Invalid focus request")
                state["focusRevision"] += 1
                self.send(14, {"request": value["request"], "focusRevision": state["focusRevision"]}, session, epoch)
        elif kind == 8:
            require(epoch == 1 and session in self.sessions, "Resize for unknown fixture session")
            self.opened(session, advance=True, resize=value)
        elif kind == INPUT_ROUTE:
            require(self.input_route_negotiated and self.pending_route is not None,
                    "Unsolicited or unnegotiated forwarding-fence reply")
            expected = self.pending_route
            require(session == expected["session"] and epoch == expected["epoch"]
                    and value == {**expected["payload"], "action": "ack", "result": "ready"},
                    "Forwarding-fence reply did not acknowledge the exact requested binding")
            self.pending_route = None
        elif kind in (9, 15):
            require(self.expected_disconnect, "Viewer returned its session unexpectedly")
        else:
            raise RuntimeError(f"Unexpected viewer packet type {kind}")

    def run(self):
        try:
            peer = None
            while not self.stopping.is_set():
                try:
                    peer, _ = self.listener.accept()
                    break
                except TimeoutError:
                    pass
            if peer is None:
                return
            peer.settimeout(5)
            with self.context.wrap_socket(peer, server_side=True) as connection:
                self.connection = connection
                require(hashlib.sha256(connection.getpeercert(binary_form=True)).hexdigest() == self.viewer_pin,
                        "Real viewer did not present the exact approved leaf")
                connection.settimeout(1)
                incoming = bytearray()
                heartbeat = time.monotonic()
                while not self.stopping.is_set():
                    try:
                        operation, session = self.commands.get_nowait()
                        if operation == "advance-without-frame":
                            self.opened(session, advance=True, frame=False)
                        elif operation == "frame":
                            self.send_frame(session)
                        elif operation == "second-window":
                            self.opened(session)
                        elif operation == "close-window":
                            self.send(10, {}, session, 1)
                        elif operation == "input-route":
                            require(self.input_route_negotiated and self.pending_route is None,
                                    "Concurrent or unnegotiated forwarding-fence request")
                            self.pending_route = session
                            self.send(INPUT_ROUTE, session["payload"], session["session"], session["epoch"])
                        else:
                            raise RuntimeError("Unknown TLS fixture command")
                    except queue.Empty:
                        pass
                    if self.input_sequence and time.monotonic() - heartbeat > 0.5:
                        self.send(11, {})
                        heartbeat = time.monotonic()
                    if not connection.pending() and not select.select([connection], [], [], 0.01)[0]:
                        continue
                    block = connection.recv(16384)
                    if not block:
                        require(self.expected_disconnect or self.stopping.is_set(), "Real viewer disconnected early")
                        return
                    incoming.extend(block)
                    require(len(incoming) <= 131072, "Viewer inbound fixture buffer exceeded bound")
                    while len(incoming) >= HEADER.size:
                        magic, size, kind, flags, session, epoch, sequence, reserved = HEADER.unpack_from(incoming)
                        require(magic == b"DSW1" and flags == reserved == 0 and size <= 65536,
                                "Invalid viewer packet framing")
                        if len(incoming) < HEADER.size + size:
                            break
                        require(sequence == self.input_sequence + 1, "Viewer frame sequence lost/replayed")
                        self.input_sequence = sequence
                        payload = json.loads(incoming[HEADER.size:HEADER.size + size])
                        del incoming[:HEADER.size + size]
                        require(isinstance(payload, dict), "Viewer payload was not an object")
                        self.record("viewer", kind, session, epoch, payload)
                        self.packet(kind, session, epoch, payload)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLEOFError):
            # Stopping the owned provider closes its Viewer. TLS may report
            # EOF as a transport exception instead of an empty read; only the
            # explicitly requested shutdown may accept that outcome.
            if not self.expected_disconnect and not self.stopping.is_set():
                self.failure = traceback.format_exc()
        except Exception:
            if not self.stopping.is_set():
                self.failure = traceback.format_exc()
        finally:
            self.connection = None

    def close(self):
        self.stopping.set()
        self.listener.close()
        self.thread.join(timeout=6)
        require(not self.thread.is_alive(), "Owned TLS fixture thread did not stop")
        self.log.close()


class Driver:
    def __init__(self, processes, executable, configuration):
        self.processes = processes
        self.stderr = (processes.output / "production-client.log").open("wb")
        self.transcript = (processes.output / "production-client.jsonl").open("x", buffering=1)
        processes.files.extend((self.stderr, self.transcript))
        self.child = subprocess.Popen([str(executable), str(configuration)], env=processes.env,
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.stderr,
                                      start_new_session=True)
        processes.children.append(self.child)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.child.stdout, selectors.EVENT_READ)
        self.buffer = b""
        self.request = 0

    def event(self, expected, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.processes.check()
            if b"\n" not in self.buffer:
                if not self.selector.select(timeout=0.05):
                    continue
                data = os.read(self.child.stdout.fileno(), 4096)
                require(data, f"Production client exited before {expected}: {self.child.poll()}")
                self.buffer += data
                require(len(self.buffer) <= 65536, "Production fixture output exceeded bound")
                continue
            line, self.buffer = self.buffer.split(b"\n", 1)
            self.transcript.write(line.decode() + "\n")
            message = json.loads(line)
            require(message.get("event") == expected, f"Expected {expected}, got {message}")
            return message
        raise RuntimeError(f"Production client fixture timed out before {expected}")

    def command(self, operation, **values):
        self.request += 1
        command = {"id": self.request, "op": operation, **values}
        self.child.stdin.write((json.dumps(command) + "\n").encode())
        self.child.stdin.flush()
        expected = {"observe": "observed", "drain": "drained", "seal": "drained", "map": "mapped",
                    "map-batch": "batch-mapped", "drain-retained": "drain-retained", "retained": "retained",
                    "idle-state": "idle-state", "prepare": "prepared", "promote": "drained",
                    "preparation-retained": "preparation-retained",
                    "preparation-cleanup": "preparation-cleaned", "stop": "stopped"}.get(operation, "synced")
        reply = self.event(expected)
        require(reply.get("id") == self.request, "Wrong fixture request reply")
        if operation == "map-batch":
            self.request += values["count"] - 1
        return reply


def start_shell(processes, root, report):
    report["shellVersion"] = processes.run("shell-version", ["gnome-shell", "--version"])
    require(report["shellVersion"].split()[-1].split(".")[0] == "50", "This fixture requires actual GNOME50")
    processes.run("libei-version", ["pkg-config", "--atleast-version=1.5.0", "libei-1.0"])
    report["packages"] = processes.run("packages", ["dpkg-query", "-W", "gnome-shell", "libmutter-18-0",
                                                  "libei1", "libeis1", "qt6-wayland", "qt6-base-private-dev",
                                                  "qt6-wayland-private-dev", "ffmpeg"]).splitlines()
    target = Path(processes.env["XDG_DATA_HOME"]) / "gnome-shell/extensions" / common.UUID
    shutil.copytree(HERE / "gnome-provider", target)
    processes.env["DBUS_SESSION_BUS_ADDRESS"] = common.start_bus(processes, root, "session-bus")
    processes.env["DBUS_SYSTEM_BUS_ADDRESS"] = common.start_bus(processes, root, "system-bus")
    processes.env["WAYLAND_DISPLAY"] = "deskflow-real-qt-fixture"
    for schema, key, value in [
        ("org.gnome.shell", "enabled-extensions", "[]"),
        ("org.gnome.shell", "disable-user-extensions", "false"),
        ("org.gnome.shell", "welcome-dialog-last-shown-version", "'50.0'"),
        ("org.gnome.desktop.interface", "enable-animations", "false"),
        ("org.gnome.desktop.session", "idle-delay", "uint32 0"),
        ("org.gnome.desktop.screensaver", "lock-enabled", "false"),
    ]:
        processes.run("setting-" + key, ["gsettings", "set", schema, key, value])
    command = ["gnome-shell", "--wayland", "--headless", "--no-x11", "--virtual-monitor=1280x720",
               "--wayland-display=" + processes.env["WAYLAND_DISPLAY"]]
    report["compositorCommand"] = command
    shell = processes.start("gnome-shell", command)
    control = common.Provider(processes.env["DBUS_SESSION_BUS_ADDRESS"])
    try:
        def discovered():
            require(shell.poll() is None, f"Owned GNOME Shell exited {shell.returncode}")
            info, = control.call("GetExtensionInfo", "(s)", (common.UUID,), destination="org.gnome.Shell",
                                 path="/org/gnome/Shell", interface="org.gnome.Shell.Extensions")
            return info.get("state") in (2, 6)
        common.wait_for(processes, discovered, "disabled fixture extension discovery", timeout=40)
        processes.run("enable-extension", ["gsettings", "set", "org.gnome.shell", "enabled-extensions",
                                           f"['{common.UUID}']"])
        common.wait_for(processes, lambda: control.bus("NameHasOwner", "(s)", (common.SERVICE,)) == (True,),
                        "fixture extension bus owner", timeout=20)
    finally:
        # This bootstrap connection never creates or queries a provider-owned
        # session. Every owner-sensitive call uses the production C++ client.
        control.close()
    return shell


def denied(reply, description):
    require(reply.get("live") is False and "snapshot" not in reply and reply.get("error"),
            f"{description} did not deny: {reply}")
    text = reply["error"].lower()
    require(not any(term in text for term in ("timeout", "protocol", "disconnected", "closed", "connection")),
            f"{description} failed infrastructure instead of denying observation: {reply}")
    return reply["error"]


def forwarding_request(snapshot, generation, mode):
    """Bind the network-only fence to a currently observed painted viewport."""
    require(type(generation) is int and 0 < generation < 2**53 and mode in ("original", "viewer"),
            "Invalid forwarding-fence generation or mode")

    def number(name):
        value = snapshot["paint"].get(name)
        require(isinstance(value, str) and value.isascii() and value.isdecimal()
                and value[0] != "0" and len(value) <= 16 and 0 < int(value) < 2**53,
                f"Invalid painted forwarding binding: {name}")
        return int(value)

    return {"kind": INPUT_ROUTE, "session": number("session"), "epoch": number("epoch"),
            "payload": {"generation": generation, "focusRequest": number("focusRequest"),
                        "focusRevision": number("focusRevision"), "geometry": number("geometry"),
                        "action": "request", "mode": mode}}


def forwarding_acknowledgement(messages, request, start):
    replies = [(index, message) for index, message in enumerate(messages[start:], start)
               if message["direction"] == "viewer" and message["kind"] in (17, INPUT_ROUTE)]
    require(len(replies) <= 1, "Duplicate forwarding-fence acknowledgement")
    if not replies:
        return None
    index, message = replies[0]
    require(message["kind"] == INPUT_ROUTE and message["session"] == request["session"]
            and message["epoch"] == request["epoch"]
            and message["payload"] == {**request["payload"], "action": "ack", "result": "ready"},
            "Forwarding acknowledgement changed scope, owner, binding, mode or result")
    return {"index": index, "packet": message}


def request_forwarding(processes, host, request):
    start = len(host.events())
    host.commands.put_nowait(("input-route", request))
    return common.wait_for(processes, lambda: forwarding_acknowledgement(host.events(), request, start),
                           "exact real viewer full-input forwarding acknowledgement", timeout=3, retry_errors=False)


def forwarding_receipts(messages, request, expected, start, tolerance):
    """Accept only an exact prefix, completing on positive fresh input receipts.

    This checks the observed TLS interval. It does not turn EI PONG, elapsed
    silence or a forwarding acknowledgement into native consumption evidence.
    """
    inputs = [message for message in messages[start:]
              if message["direction"] == "viewer" and message["kind"] == 7
              and message["payload"].get("kind") in INPUT_KINDS]
    require(len(inputs) <= len(expected), "Duplicated or fenced input escaped the forwarding gate")
    for message, wanted in zip(inputs, expected):
        value = message["payload"]
        require(message["session"] == request["session"] and message["epoch"] == request["epoch"]
                and value.get("geometry") == request["payload"]["geometry"]
                and value.get("focusRevision") == request["payload"]["focusRevision"],
                "Fresh input receipt changed its observed forwarding binding")
        for field, required in wanted.items():
            actual = value.get(field)
            if field in ("x", "y"):
                matches = type(actual) in (float, int) and math.isfinite(actual) \
                    and abs(actual - required) <= tolerance[0 if field == "x" else 1]
            elif type(required) is bool:
                matches = actual is required
            else:
                matches = actual == required
            require(matches, f"Fenced, reordered or duplicate input in fresh receipt sequence: {value}")
    return inputs if len(inputs) == len(expected) else None


def forwarding_fence_acceptance(processes, host, driver, initial, desktop):
    paint = initial["paint"]
    request = forwarding_request(initial, 1, "original")
    entered = request_forwarding(processes, host, request)
    interval = entered["index"] + 1
    tolerance = [1 / 256 / paint["logicalScale"] * paint["sourceWidth"] / paint["videoWidth"] + 1e-6,
                 1 / 256 / paint["logicalScale"] * paint["sourceHeight"] / paint["videoHeight"] + 1e-6]

    def point(horizontal, vertical):
        # Integral Qt logical points avoid Qt's separate wheel QPoint rounding.
        return desktop(math.floor((paint["videoX"] + paint["videoWidth"] * horizontal) / 4) * 4,
                       math.floor((paint["videoY"] + paint["videoHeight"] * vertical) / 4) * 4)

    def observe_position(target):
        reply = driver.command("observe")
        require(reply.get("live") is True, f"Forwarding fence lost the native/painted viewport: {reply}")
        snapshot = reply["snapshot"]
        require(snapshot["window"] == initial["window"] and snapshot["surface"] == initial["surface"]
                and snapshot["paint"] == paint, "Forwarding fence changed the exact painted focus or geometry")
        require(abs(snapshot["desktopX"] - target[0]) <= 1 / 256
                and abs(snapshot["desktopY"] - target[1]) <= 1 / 256,
                "Forwarding fence prevented the independently observed Linux cursor movement")
        return snapshot

    fenced_target = point(.6, .4)
    fenced_commands = [driver.command("key", code=65),  # evdev F7, distinct from fresh F8 below.
                       driver.command("click"), driver.command("scroll", dx=120, dy=0),
                       driver.command("move", x=fenced_target[0], y=fenced_target[1])]
    fenced_position = observe_position(fenced_target)
    resumed_request = {**request, "payload": {**request["payload"], "mode": "viewer"}}
    resumed = request_forwarding(processes, host, resumed_request)
    forwarding_receipts(host.events()[:resumed["index"] + 1], request, [], interval, tolerance)

    expected, fresh_commands, observations, positive_receipts = [], [], [], []

    def receive(wanted):
        expected.extend(wanted)
        receipts = common.wait_for(
            processes, lambda: forwarding_receipts(host.events(), request, expected, interval, tolerance),
            "ordered fresh input after exact forwarding resume", timeout=3, retry_errors=False)
        positive_receipts.append(receipts[-1])

    for ordinal, target in enumerate((point(.35, .6), point(.65, .65))):
        fresh_commands.append(driver.command("move", x=target[0], y=target[1]))
        observed = observe_position(target)
        observations.append(observed)
        receive([{"kind": "move", "x": observed["sourceX"], "y": observed["sourceY"]}])
        if ordinal == 0:
            fresh_commands.append(driver.command("key", code=66))  # F8 is independent of keyboard text layout.
            receive([{"kind": "key", "code": 16777271, "down": down, "repeat": False}
                     for down in (True, False)])
            fresh_commands.append(driver.command("click"))
            receive([{"kind": "button", "code": 0, "down": down} for down in (True, False)])
            fresh_commands.append(driver.command("scroll", dx=0, dy=120))
            receive([{"kind": "scroll", "dx": 0, "dy": -40}])

    # A second exact gate bounds the observed network interval after the fresh
    # key/button pairs, scroll and terminal motion have positively arrived.
    # No elapsed quiet period or EIS ping is treated as a queue-consumption fence.
    closing_request = forwarding_request(observations[-1], 2, "original")
    closing = request_forwarding(processes, host, closing_request)
    final_receipts = forwarding_receipts(host.events()[:closing["index"] + 1], request, expected, interval, tolerance)
    require(final_receipts is not None, "Forwarding interval ended without all fresh positive input receipts")
    reopened_request = {**closing_request, "payload": {**closing_request["payload"], "mode": "viewer"}}
    reopened = request_forwarding(processes, host, reopened_request)
    return {"scope": "negotiated-full-input-forwarding-fence", "originalDeliveryAuthority": False,
            "compositorConsumptionBarrier": False, "nativeEventDeviceAttribution": False,
            "capabilities": FORWARDING_CAPABILITIES, "request": request,
            "entered": entered, "fencedCommandPongs": fenced_commands, "fencedCursorObservation": fenced_position,
            "resumed": resumed, "freshCommandPongs": fresh_commands, "freshCursorObservations": observations,
            "freshReceipts": final_receipts, "positiveReceiptBoundaries": positive_receipts,
            "closedInterval": closing, "reopened": reopened, "sourceTolerance": tolerance}


SUPPORTED_ORDERING_CONTRACTS = (
    "sha256:fec9023139ac8eff93fcc1af90413613cdb526e7f02389dc9d8fe90f7abc81e5",
    "sha256:4d7ece1588279dbd3b66de3a28d1bf1098e7905b705d8e44743192b925d66b83",
)


def input_drain_receipt(reply, request, initial, expected_ordering_contract="", *, capture=None):
    """Verify a native segment receipt without treating it as a route permit."""
    require(reply.get("live") is True and not reply.get("error") and type(reply.get("id")) is int
            and reply["id"] > 0 and reply.get("originalDeliveryAuthority") is False,
            f"Exact fenced input drain did not complete: {reply}")
    if expected_ordering_contract:
        require(expected_ordering_contract in SUPPORTED_ORDERING_CONTRACTS,
                "Native test expected an unreviewed sender-ordering contract")
        ordering = reply.get("senderOrdering")
        require(reply.get("orderingLimitation") == "" and isinstance(ordering, dict)
                and ordering.keys() == {"live", "contract"} and ordering.get("live") is True
                and ordering.get("contract") == expected_ordering_contract,
                "Native segment lacked the exact live supported sender-ordering contract")
    else:
        require(reply.get("orderingLimitation") == "mutter-eis-sync-build-unverified"
                and "senderOrdering" in reply and reply["senderOrdering"] is None,
                "Unverified native segment claimed sender ordering")
    expected = {"request": str(reply["id"]), "coreConnectionGeneration": 1,
                "captureEpoch": "1", "captureSequence": str(reply["id"]), "capturePart": 1,
                "viewerConnection": initial["paint"]["connection"],
                "session": str(request["session"]), "epoch": str(request["epoch"]),
                "scope": "all"}
    if capture is not None:
        expected.update({name: capture[name] for name in ("captureEpoch", "captureSequence", "capturePart")})
    expected.update({field: str(request["payload"][field])
                     for field in ("generation", "focusRequest", "focusRevision", "geometry")})
    binding = reply.get("cutoff")
    require(isinstance(binding, dict) and binding.keys() == expected.keys()
            and all(type(binding[field]) is type(value) and binding[field] == value
                    for field, value in expected.items()),
            "Native segment receipt changed the exact cutoff binding")
    observed = reply.get("snapshot", {})
    require(observed.get("window") == initial["window"] and observed.get("surface") == initial["surface"]
            and observed.get("paint") == initial["paint"], "Native segment receipt changed native/paint identity")
    require(all(type(observed.get(field)) in (int, float) and math.isfinite(observed[field])
                and abs(observed[field] - initial[field]) <= 1 / 256
                for field in ("desktopX", "desktopY")), "Native segment receipt changed the frozen routed point")
    return reply


def input_drain_denial(reply, description):
    require(reply.get("live") is False and "snapshot" not in reply and "cutoff" not in reply
            and reply.get("originalDeliveryAuthority") is False and isinstance(reply.get("error"), str)
            and reply["error"], f"{description} did not deny the native drain: {reply}")
    require(not any(term in reply["error"].lower() for term in ("timeout", "protocol", "disconnected", "closed")),
            f"{description} failed infrastructure instead of denying its binding: {reply}")
    return reply["error"]


def input_seal_receipt(reply, origin, expected_contract):
    seal = reply.get("seal")
    actual_origin = seal.get("origin") if isinstance(seal, dict) else None
    require(expected_contract in SUPPORTED_ORDERING_CONTRACTS and isinstance(seal, dict)
            and seal.keys() == {"live", "origin", "contract"} and seal.get("live") is True
            and seal.get("contract") == expected_contract and isinstance(actual_origin, dict)
            and actual_origin.keys() == origin.keys()
            and all(type(actual_origin[field]) is type(value) and actual_origin[field] == value
                    for field, value in origin.items()),
            "Native map did not retain the exact live prefix seal")
    return seal


def input_batch_receipt(reply, count, request, initial, sealed, expected_contract):
    require(not reply.get("error") and reply.get("originalDeliveryAuthority") is False
            and type(reply.get("id")) is int and isinstance(reply.get("segments"), list)
            and len(reply["segments"]) == count and 1 <= count <= 16
            and reply.get("requestedPings") == sealed.get("requestedPings")
            and reply.get("requestedDrains") == sealed.get("requestedDrains"),
            "Native point batch repeated drain or returned a partial result")
    for index, item in enumerate(reply["segments"]):
        point = {**initial, "desktopX": initial["desktopX"] + index % 4,
                 "desktopY": initial["desktopY"] + index // 4}
        input_drain_receipt({**item, "id": reply["id"] + index}, request, point, expected_contract)
        input_seal_receipt(item, sealed["cutoff"], expected_contract)
    return reply


def locate_focused_viewer(processes, driver, session, region):
    # A new GNOME toplevel need not cover the previous window's center. A
    # click there can select that old window again. Move within the known EIS
    # region and accept only the real provider/Qt exact focused-hit receipt.
    x, y, width, height = region
    points = [(round(x + width * fx), round(y + height * fy))
              for fy in (.5, .3, .7, .1, .9) for fx in (.5, .3, .7, .1, .9)]
    cursor = 0

    def observed():
        nonlocal cursor
        point = points[cursor % len(points)]
        cursor += 1
        driver.command("move", x=point[0], y=point[1])
        result = driver.command("observe")
        if not result.get("live"):
            denied(result, "Focused-window point search")
            return None
        return result if result["snapshot"]["paint"]["session"] == str(session) else None

    return common.wait_for(processes, observed, f"explicit window {session} native association",
                           timeout=5, retry_errors=False)["snapshot"]


def native_input_drain_acceptance(processes, host, driver, initial, expected_ordering_contract):
    """Real PONG -> Clutter segment -> exact-All Qt completion on owned GNOME.

    The fixture supplies synthetic Core correlation. Production Core cutoff and
    authenticated sender association are separate prerequisites for authority.
    A verified mapped Mutter build proves only the named local prerequisite.
    """
    paint = initial["paint"]
    request = forwarding_request(initial, 3, "original")
    entered = request_forwarding(processes, host, request)
    prefix = [driver.command("key", code=67), driver.command("scroll", dx=0, dy=120)]

    def drain(generation, connection=None):
        return driver.command("drain", generation=generation,
                              connection=int(paint["connection"]) if connection is None else connection)

    denials = {}
    for reason, generation, connection in (("wrongGate", 4, int(paint["connection"])),
                                           ("wrongConnection", 3, int(paint["connection"]) + 1)):
        reply = drain(generation, connection)
        denials[reason] = input_drain_denial(reply, reason)
    completed = input_drain_receipt(drain(3), request, initial, expected_ordering_contract)
    resumed_request = {**request, "payload": {**request["payload"], "mode": "viewer"}}
    resumed = request_forwarding(processes, host, resumed_request)
    forwarding_receipts(host.events()[:resumed["index"] + 1], request, [], entered["index"] + 1, [0, 0])

    def revoked_before_expiry():
        retained = driver.command("drain-retained")
        require(type(retained.get("remainingMs")) is int and retained["remainingMs"] > 0,
                "Retained drain expired before its gate revocation could be verified")
        if retained.get("live") is False:
            require(retained.get("senderOrderingLive") is False,
                    "Gate revocation left sender-ordering evidence live")
            return retained
        return None

    revoked = common.wait_for(processes, revoked_before_expiry, "retained native drain revocation before expiry",
                              timeout=.25, retry_errors=False)
    reply = drain(3)
    denials["resumedGate"] = input_drain_denial(reply, "resumed gate")

    fresh_request = forwarding_request(initial, 4, "original")
    fresh_entered = request_forwarding(processes, host, fresh_request)
    fresh = input_drain_receipt(drain(4), fresh_request, initial, expected_ordering_contract)
    require(fresh["id"] > completed["id"], "Native drain reused a completed cutoff request")
    fresh_resume = {**fresh_request, "payload": {**fresh_request["payload"], "mode": "viewer"}}
    fresh_resumed = request_forwarding(processes, host, fresh_resume)
    forwarding_receipts(host.events()[:fresh_resumed["index"] + 1], fresh_request, [],
                        fresh_entered["index"] + 1, [0, 0])
    prefix_seal = None
    if expected_ordering_contract:
        seal_request = forwarding_request(initial, 5, "original")
        seal_entered = request_forwarding(processes, host, seal_request)
        sealed = input_drain_receipt(driver.command("seal", generation=5, connection=int(paint["connection"])),
                                     seal_request, initial, expected_ordering_contract)
        input_seal_receipt(sealed, sealed["cutoff"], expected_ordering_contract)
        time.sleep(.3)
        idle = driver.command("drain-retained")
        require(idle.get("live") is False and idle.get("senderOrderingLive") is False
                and idle.get("sealLive") is True and idle.get("remainingMs", 1) < 0,
                "Idle point expiry renewed a point or incorrectly lost the historical prefix seal")
        maps = []
        for index in range(12):
            started = time.monotonic()
            mapped = input_drain_receipt(driver.command("map", generation=5, connection=int(paint["connection"])),
                                         seal_request, initial, expected_ordering_contract)
            elapsed = (time.monotonic() - started) * 1000
            input_seal_receipt(mapped, sealed["cutoff"], expected_ordering_contract)
            require(mapped.get("requestedPings") == sealed.get("requestedPings")
                    and mapped.get("requestedDrains") == sealed.get("requestedDrains")
                    and mapped.get("requestedMaps") == sealed.get("requestedMaps") + index + 1,
                    "Fresh point mapping repeated the old-prefix ping or drain")
            maps.append({"receipt": mapped, "elapsedMs": elapsed})
        batches = []
        for index in range(8):
            started = time.monotonic()
            batch = input_batch_receipt(driver.command("map-batch", generation=5,
                                         connection=int(paint["connection"]), count=16),
                                        16, seal_request, initial, sealed, expected_ordering_contract)
            elapsed = (time.monotonic() - started) * 1000
            require(batch.get("requestedBatches") == sealed.get("requestedBatches", 0) + index + 1,
                    "Native batch request was lost or duplicated")
            batches.append({"receipt": batch, "elapsedMs": elapsed, "pointsPerSecond": 16000 / elapsed})
        seal_resume = {**seal_request, "payload": {**seal_request["payload"], "mode": "viewer"}}
        seal_resumed = request_forwarding(processes, host, seal_resume)
        forwarding_receipts(host.events()[:seal_resumed["index"] + 1], seal_request, [],
                            seal_entered["index"] + 1, [0, 0])

        def seal_revoked():
            result = driver.command("drain-retained")
            return result if (result.get("sealLive") is False and result.get("live") is False
                              and result.get("batchLive") == [False] * 16) else None

        sealed_revoked = common.wait_for(processes, seal_revoked, "retained prefix seal revocation", timeout=.25,
                                        retry_errors=False)
        prefix_seal = {"initial": sealed, "idlePointExpired": idle, "maps": maps, "batches": batches,
                       "resumed": seal_resumed, "revoked": sealed_revoked}
    return {"scope": "owned-pong-compositor-segment-exact-fenced-qt-completion",
            "originalDeliveryAuthority": False, "productionCoreCutoff": False,
            "senderOrderingBuildVerified": bool(expected_ordering_contract),
            "senderOrderingContract": expected_ordering_contract, "entered": entered, "prefixPongs": prefix,
            "completed": completed, "resumed": resumed, "revokedBeforeExpiry": revoked, "denials": denials,
            "freshCompleted": fresh, "freshResumed": fresh_resumed, "retainedPrefix": prefix_seal}


def prepared_focus_receipt(reply, initial, point, expected_contract, host_pin):
    require(expected_contract in SUPPORTED_ORDERING_CONTRACTS and reply.get("live") is True
            and not reply.get("error") and reply.get("outcome") == "none"
            and reply.get("originalDeliveryAuthority") is False and reply.get("contract") == expected_contract
            and type(reply.get("id")) is int and reply["id"] > 0,
            f"Exact pre-All focus preparation did not complete: {reply}")
    expected = {"request": str(reply["id"]), "coreConnectionGeneration": 1,
                "captureEpoch": "1", "captureSequence": str(reply["id"]), "capturePart": 1}
    binding = reply.get("binding")
    require(isinstance(binding, dict) and binding.keys() == expected.keys()
            and all(type(binding[key]) is type(value) and binding[key] == value for key, value in expected.items()),
            "Preparation forged an All binding or changed its exact capture")
    observed = reply.get("snapshot", {})
    paint = observed.get("paint", {})
    require(observed.get("window") == initial["window"] and observed.get("surface") == initial["surface"]
            and all(paint.get(key) == initial["paint"][key]
                    for key in ("session", "epoch", "geometry", "nativeSurface", "windowAssociation", "connection"))
            and paint.get("peerFingerprint") == host_pin
            and int(paint.get("focusRequest", "0")) > int(initial["paint"]["focusRequest"])
            and int(paint.get("focusRevision", "0")) > int(initial["paint"]["focusRevision"]),
            "Prepared focus did not retain the exact owned target and fresh Host acknowledgement")
    require(all(type(observed.get(key)) in (int, float) and math.isfinite(observed[key])
                and abs(observed[key] - value) <= 1 / 256
                for key, value in zip(("desktopX", "desktopY"), point)),
            "Prepared focus changed its frozen routed point")
    return reply


def preparation_input_receipts(messages, begin, end):
    """A preparation activation is focus-only; no source acquisition can escape."""
    require(type(begin) is int and type(end) is int and 0 <= begin <= end <= len(messages),
            "Invalid preparation evidence interval")
    forwarded = [message for message in messages[begin:end]
                 if message["direction"] == "viewer" and message["kind"] == 7
                 and message["payload"].get("kind") in INPUT_KINDS]
    require(not forwarded, f"Preparation gate forwarded source input: {forwarded}")
    return {"begin": begin, "end": end, "forwardedInput": forwarded}


def preparation_retained_receipt(reply, prepared):
    """A completed PONG stops its timer without extending the original 500 ms."""
    age, remaining = reply.get("ageMs"), reply.get("remainingMs")
    require(reply.get("live") is True and reply.get("prefixPending") is False
            and reply.get("originalDeliveryAuthority") is False
            and reply.get("clientRequest") == prepared.get("clientRequest")
            and type(age) is int and type(remaining) is int and age >= 250 and 0 < remaining <= 250
            and 499 <= age + remaining <= 500,
            "Completed preparation prefix retained its PONG timer or renewed its original deadline")
    return reply


def native_first_click_acceptance(processes, host, driver, initial, focus, focus_path, expected_contract):
    """Real idle EI restart + distinct prep drain/activation + genuine All + cleanup.

    The driver uses synthetic Core capture correlation and a disposable TLS
    Host. The real provider, compositor and Qt gates run unchanged. Activation
    must not inject the withheld click; later test strokes probe the closed gate.
    """
    paint = initial["paint"]
    # The independent 360x180 focus owner's center covers the old center point.
    # Choose an interior video corner; the provider must still prove exact hit.
    point = (initial["desktopX"] - initial["surfaceX"] + paint["contentX"] +
             (paint["videoX"] + .12 * paint["videoWidth"]) * paint["logicalScale"],
             initial["desktopY"] - initial["surfaceY"] + paint["contentY"] +
             (paint["videoY"] + .12 * paint["videoHeight"]) * paint["logicalScale"])
    # EI uses floats; freeze their exact represented values before comparing.
    point = tuple(struct.unpack("f", struct.pack("f", value))[0] for value in point)
    driver.command("move", x=point[0], y=point[1])
    before_focus = current_focus(focus_path, focus)
    require(before_focus, "Independent focus owner was not current before preparation")
    time.sleep(4.2)
    processes.check()
    idle = driver.command("idle-state")
    require(idle.get("emulating") is False and idle.get("idleStops", 0) >= 1
            and idle.get("idleMs", 0) >= 4000,
            "Real libei sender did not stop emulating after four seconds idle")
    require(current_focus(focus_path, focus) == before_focus, "Focus changed during the idle prerequisite")
    begin = len(host.events())
    preparation_started = time.monotonic()
    prepared = driver.command("prepare", x=point[0], y=point[1])
    if not expected_contract:
        require(prepared.get("live") is False and prepared.get("outcome") == "unsupported"
                and prepared.get("originalDeliveryAuthority") is False and "snapshot" not in prepared
                and "binding" not in prepared and "contract" not in prepared,
                "Unsupported preparation claimed native focus or a gate")
        require(current_focus(focus_path, focus) == before_focus,
                "Unsupported ordering build changed native focus")
        return {"supported": False, "idle": idle, "denied": prepared,
                "originalDeliveryAuthority": False}
    prepared_focus_receipt(prepared, initial, point, expected_contract, host.host_pin)
    require(prepared.get("idleStops") == idle["idleStops"]
            and prepared.get("emulationRestarts") == idle["emulationRestarts"] + 1,
            "Preparation did not restart the exact idle libei devices once")
    focus_ack = [message for message in host.events()[begin:]
                 if message["direction"] == "host" and message["kind"] == 14
                 and message["session"] == int(paint["session"])
                 and message["payload"] == {"request": int(prepared["snapshot"]["paint"]["focusRequest"]),
                                           "focusRevision": int(prepared["snapshot"]["paint"]["focusRevision"])}]
    require(len(focus_ack) == 1, "Preparation lacked exactly one matching actual TLS Host focus acknowledgement")
    activation = preparation_input_receipts(host.events(), begin, len(host.events()))
    # Cross the now-completed prefix's 250 ms wait limit before promotion.
    # This is inside the unchanged 500 ms preparation lifetime, not a retry
    # or extension; slow/expired preparation still fails the exact live check.
    time.sleep(max(0, preparation_started + .300 - time.monotonic()))
    prefix_retired = preparation_retained_receipt(driver.command("preparation-retained"), prepared)
    request = forwarding_request(prepared["snapshot"], 6, "original")
    entered = request_forwarding(processes, host, request)
    promoted = input_drain_receipt(driver.command("promote", generation=6,
                                      connection=int(prepared["snapshot"]["paint"]["connection"])),
                                   request, prepared["snapshot"], expected_contract, capture=prepared["binding"])
    input_seal_receipt(promoted, promoted["cutoff"], expected_contract)
    resumed_request = {**request, "payload": {**request["payload"], "mode": "viewer"}}
    resumed = request_forwarding(processes, host, resumed_request)
    # Host All can disappear before cleanup. Each complete stroke must still
    # be discarded by the connection preparation gate, including pointer input.
    probes = [driver.command("move", x=point[0] + 2, y=point[1] + 2), driver.command("key", code=67),
              driver.command("scroll", dx=120, dy=120), driver.command("click")]
    cleaned = driver.command("preparation-cleanup")
    require(cleaned.get("released") is True and not cleaned.get("error")
            and cleaned.get("preparedLive") is False and cleaned.get("sealLive") is False
            and cleaned.get("originalDeliveryAuthority") is False
            and cleaned.get("clientRequest") == prepared.get("clientRequest"),
            f"Exact asynchronous preparation cleanup did not release: {cleaned}")
    closed = preparation_input_receipts(host.events(), begin, len(host.events()))
    # Confirm the gate actually reopened by requiring a fresh legacy packet.
    driver.command("key", code=68)
    expected = [{"kind": "key", "code": 16777273, "down": down, "repeat": False} for down in (True, False)]
    reopened = common.wait_for(
        processes, lambda: forwarding_receipts(host.events(), request, expected, begin, [0, 0]),
        "fresh key pair after exact preparation cleanup", timeout=3, retry_errors=False)
    closing_request = forwarding_request(prepared["snapshot"], 7, "original")
    closing = request_forwarding(processes, host, closing_request)
    final_receipts = forwarding_receipts(host.events()[:closing["index"] + 1], request, expected, begin, [0, 0])
    require(final_receipts is not None, "Preparation interval lacked its complete fresh key pair")
    closing_resume = {**closing_request, "payload": {**closing_request["payload"], "mode": "viewer"}}
    final_resume = request_forwarding(processes, host, closing_resume)
    return {"supported": True, "scope": "real-provider-pre-All-focus-promotion-cleanup",
            "productionCoreCutoff": False, "productionEiComputer": False, "originalDeliveryAuthority": False,
            "idle": idle, "prepared": prepared, "focusAck": focus_ack[0], "activationWithoutClick": activation,
            "completedPrefixTimerRetired": prefix_retired,
            "entered": entered, "promoted": promoted, "hostResumedBeforeCleanup": resumed,
            "closedGateProbePongs": probes, "cleanup": cleaned, "closedInterval": closed,
            "freshAfterCleanup": reopened, "closedPositiveInterval": closing,
            "finalReceipts": final_receipts, "finalResume": final_resume}


def recover_independent_focus(processes, host, driver, initial, focus, focus_path, expected_contract,
                              center, initially_ready, report, mode="prepared-first-click"):
    """Compare the old transition without retrying or delaying a failed native operation."""
    require(mode in FOCUS_RECOVERY_MODES, "Unknown diagnostic focus recovery mode")
    if mode == "prepared-first-click":
        report["firstClickPreparation"] = native_first_click_acceptance(
            processes, host, driver, initial, focus, focus_path, expected_contract)
    focus.terminate()
    focus.wait(timeout=5)
    driver.command("move", x=center[0], y=center[1])
    if mode == "ordinary-click" or not expected_contract:
        driver.command("click")
    common.wait_for(processes, initially_ready, "original viewer focus recovery", timeout=5,
                    retry_errors=False)


class FocusState:
    """Publish current GTK state; a previous activation is not current focus."""

    def __init__(self, path):
        self.path = path
        self.previous = None
        self.sequence = 0

    def publish(self, window):
        state = {"mapped": window.get_mapped(), "focused": window.is_active(), "pid": os.getpid()}
        if state == self.previous:
            return
        self.sequence += 1
        receipt = {**state, "sequence": self.sequence}
        temporary = self.path.with_suffix(".tmp")
        temporary.write_text(json.dumps(receipt))
        # Readers see either complete state, including on focus loss/unmap.
        temporary.replace(self.path)
        self.previous = state
        print(json.dumps(receipt), flush=True)


def current_focus(path, child):
    require(child.poll() is None, f"Independent focus child exited {child.returncode}")
    try:
        encoded = path.read_text()
    except FileNotFoundError:
        return None
    require(len(encoded) <= 1024, "Independent focus receipt exceeded bound")
    state = json.loads(encoded)
    require(isinstance(state, dict) and set(state) == {"mapped", "focused", "pid", "sequence"}
            and type(state["mapped"]) is bool and type(state["focused"]) is bool
            and type(state["pid"]) is int and state["pid"] == child.pid
            and type(state["sequence"]) is int and 0 < state["sequence"] <= 2**53 - 1,
            f"Invalid independent focus receipt: {state}")
    return state if state["mapped"] and state["focused"] else None


def independent_focus_denial(processes, driver, child, path):
    before = common.wait_for(processes, lambda: current_focus(path, child),
                             "independent Wayland client current focus", timeout=5, retry_errors=False)
    # Do not click a guessed desktop position: that can focus the original
    # viewer again. Native denial is measured once while GTK owns focus.
    reply = driver.command("observe")
    after = current_focus(path, child)
    require(after == before, "Independent child focus changed during the native denial observation")
    reason = denied(reply, "focus lost to an independent client")
    return {"before": before, "after": after, "reason": reason}


def focus_child(path):
    require(os.environ.get("WAYLAND_DISPLAY") == "deskflow-real-qt-fixture"
            and not os.environ.get("DISPLAY"), "Focus child requires this harness's private Wayland socket")
    import gi
    gi.require_version("Gtk", "4.0")
    from gi.repository import GLib, Gtk
    application = Gtk.Application(application_id="org.deskflow.DisposableViewportFocus")

    def activate(app):
        window = Gtk.ApplicationWindow(application=app, title="Disposable independent focus owner")
        window.set_default_size(360, 180)
        window.set_child(Gtk.Label(label="Only the private test compositor is used"))
        state = FocusState(path)
        window.connect("map", lambda owner: state.publish(owner))
        window.connect("unmap", lambda owner: state.publish(owner))
        window.connect("notify::is-active", lambda owner, _property: state.publish(owner))
        window.present()
        GLib.timeout_add_seconds(30, lambda: app.quit() or False)
    application.connect("activate", activate)
    return application.run([])


def run(arguments):
    require(sys.platform == "linux" and os.geteuid() != 0, "Use an unprivileged disposable Linux container")
    require(arguments.output.is_absolute(), "Output must be absolute")
    arguments.output.mkdir(parents=True, exist_ok=False)
    report = {"schema": 1, "status": "failed", "scope": "actual-qtwayland-painted-viewport-observation",
              "scale": arguments.scale, "scaleSource": "QT_SCALE_FACTOR",
              "focusRecovery": arguments.focus_recovery,
              "compositorMonitorScalingTested": False,
              "originalDeliveryAuthority": False, "userDesktopUsed": False}
    processes = None
    try:
        with contextlib.ExitStack() as cleanup:
            root = Path(cleanup.enter_context(tempfile.TemporaryDirectory(prefix="df-qt-gnome-", dir="/tmp")))
            environment = common.private_environment(root)
            environment["QT_SCALE_FACTOR"] = str(arguments.scale)
            environment["QT_LOGGING_RULES"] = "qt.qpa.wayland.debug=true"
            processes = common.Processes(arguments.output, environment)
            cleanup.callback(processes.close)
            shell = start_shell(processes, root, report)
            host = TlsHost(processes, root, arguments.viewer)
            cleanup.callback(host.close)
            configuration = root / "provider.json"
            configuration.write_text(json.dumps({"version": 1, "viewerExecutable": str(arguments.viewer),
                                                 "connect": "127.0.0.1", "port": host.port,
                                                 "identity": str(host.viewer_identity), "peerPin": host.host_pin}))
            driver = Driver(processes, arguments.fixture, configuration)
            ready = driver.event("ready", timeout=20)
            require(ready.get("eisPeerPid") == shell.pid, "Production client's EIS peer is not our compositor")
            report["productionClientReady"] = ready
            x, y, width, height = ready["region"]
            center = (x + width / 2, y + height / 2)
            driver.command("move", x=x, y=center[1])
            driver.command("move", x=center[0], y=center[1])
            driver.command("escape")

            def initially_ready():
                host.check()
                result = driver.command("observe")
                report["latestInitialObservation"] = result
                if not result.get("live"):
                    denied(result, "Initial readiness")
                return result if result.get("live") else None

            initial = common.wait_for(processes, initially_ready, "first real painted Qt viewport", timeout=10,
                                      retry_errors=False)
            initial = initial["snapshot"]
            paint = initial["paint"]
            require(initial["session"] == ready["session"] and initial["viewer"] == ready["viewer"]
                    and initial["generation"] == ready["generation"], "Joined provider identity changed")
            require(paint["peerFingerprint"] == host.host_pin, "Child mapping not bound to exact TLS host pin")
            require(paint["imageWidth"] == 640 and paint["imageHeight"] == 480, "Actual H264 decode dimensions differ")
            require(paint["contentY"] > 0, "Expected actual QtWayland client-side decoration")
            require(paint["videoX"] > 10, "Fixture did not create real letterboxing")
            require(abs(paint["logicalScale"] - arguments.scale) < 1e-9,
                    f"Actual QtWayland logical scale differs: {paint}")
            report["initialPaintedViewport"] = initial
            origin_x = initial["desktopX"] - initial["surfaceX"]
            origin_y = initial["desktopY"] - initial["surfaceY"]

            def desktop(logical_x, logical_y):
                return (origin_x + paint["contentX"] + logical_x * paint["logicalScale"],
                        origin_y + paint["contentY"] + logical_y * paint["logicalScale"])

            rounds = []
            for dx, dy in ((23, 13), (-17, -19), (37, 31)):
                before = len(host.events())
                target_x, target_y = desktop(paint["videoX"] + paint["videoWidth"] / 2 + dx,
                                             paint["videoY"] + paint["videoHeight"] / 2 + dy)
                driver.command("move", x=target_x, y=target_y)
                # This measured native PONG/current-hit/map observation is
                # immediate and never retried. Only client receipt may wait.
                reply = driver.command("observe")
                require(reply.get("live") is True, f"Measured native/painted join failed: {reply}")
                observed = reply["snapshot"]
                require(abs(observed["desktopX"] - target_x) <= 1 / 256
                        and abs(observed["desktopY"] - target_y) <= 1 / 256,
                        "EI PONG preceded the requested native pointer position")
                require(observed["window"] == initial["window"] and observed["surface"] == initial["surface"]
                        and observed["paint"] == paint, "Measured viewer/paint identity changed")
                tolerance_x = 1 / 256 / paint["logicalScale"] * paint["sourceWidth"] / paint["videoWidth"] + 1e-6
                tolerance_y = 1 / 256 / paint["logicalScale"] * paint["sourceHeight"] / paint["videoHeight"] + 1e-6

                def matching_pointer():
                    for message in host.events()[before:]:
                        value = message["payload"]
                        if (message["direction"] == "viewer" and message["kind"] == 7
                                and value.get("kind") == "move"
                                and abs(value["x"] - observed["sourceX"]) <= tolerance_x
                                and abs(value["y"] - observed["sourceY"]) <= tolerance_y):
                            require(message["session"] == int(paint["session"])
                                    and message["epoch"] == int(paint["epoch"])
                                    and value["geometry"] == int(paint["geometry"])
                                    and value["focusRevision"] == int(paint["focusRevision"]),
                                    "Real Pointer message has a different route binding")
                            return message
                    return None

                pointer = common.wait_for(processes, matching_pointer, "actual viewer TLS Pointer packet", timeout=3,
                                          retry_errors=False)
                rounds.append({"observation": observed, "pointer": pointer,
                               "sourceTolerance": [tolerance_x, tolerance_y]})
            report["measuredMotionPongMappingPointerRounds"] = rounds
            fence_initial = driver.command("observe")
            require(fence_initial.get("live") is True, "Full-input acceptance requires current painted focus")
            report["forwardingFence"] = forwarding_fence_acceptance(
                processes, host, driver, fence_initial["snapshot"], desktop)
            drain_initial = driver.command("observe")
            require(drain_initial.get("live") is True, "Native drain requires current painted focus")
            report["inputDrain"] = native_input_drain_acceptance(
                processes, host, driver, drain_initial["snapshot"], arguments.expected_ordering_contract)
            report["denials"] = {}
            for label, point in (
                ("letterbox", desktop(paint["videoX"] / 2, paint["clientHeight"] / 2)),
                ("decoration", (origin_x + paint["surfaceWidth"] / 2, origin_y + paint["contentY"] / 2)),
            ):
                before = len(host.events())
                driver.command("move", x=point[0], y=point[1])
                report["denials"][label] = denied(driver.command("observe"), label)
                time.sleep(0.1)
                require(not any(m["direction"] == "viewer" and m["kind"] == 7
                                and m["payload"].get("kind") == "move" for m in host.events()[before:]),
                        f"{label} emitted a real viewer Pointer")
            driver.command("move", x=center[0], y=center[1])
            focus_log = root / "independent-focus.json"
            focus = processes.start("independent-focus", [sys.executable, str(Path(__file__).resolve()),
                                                          "--focus-child", str(focus_log)])
            report["independentFocus"] = independent_focus_denial(processes, driver, focus, focus_log)
            report["denials"]["focusLost"] = report["independentFocus"]["reason"]
            recover_independent_focus(
                processes, host, driver, drain_initial["snapshot"], focus, focus_log,
                arguments.expected_ordering_contract, center, initially_ready, report, arguments.focus_recovery)
            before = len(host.events())
            host.commands.put_nowait(("advance-without-frame", 1))
            common.wait_for(processes, lambda: any(m["direction"] == "viewer" and m["kind"] == 7
                                                   and m["payload"].get("kind") == "release"
                                                   for m in host.events()[before:]),
                            "real geometry invalidation release", timeout=3, retry_errors=False)
            report["denials"]["stalePaintGeometry"] = denied(driver.command("observe"), "stale painted geometry")
            host.commands.put_nowait(("frame", 1))
            common.wait_for(processes, initially_ready, "new geometry painted recovery", timeout=5, retry_errors=False)
            host.commands.put_nowait(("second-window", 2))
            common.wait_for(processes, lambda: any(m["direction"] == "viewer" and m["kind"] == 16
                                                   and m["session"] == 2 for m in host.events()),
                            "second actual Qt toplevel painted", timeout=5, retry_errors=False)
            common.wait_for(processes, lambda: any(m["direction"] == "host" and m["kind"] == 14
                                                   and m["session"] == 2 for m in host.events()),
                            "second actual Qt focus acknowledgement", timeout=5, retry_errors=False)
            second = locate_focused_viewer(processes, driver, 2, ready["region"])
            require(second["window"] != initial["window"] and second["surface"] != initial["surface"]
                    and second["paint"]["windowAssociation"] != initial["paint"]["windowAssociation"]
                    and second["paint"]["nativeSurface"] != initial["paint"]["nativeSurface"],
                    "Two real Qt top-levels reused one native role association")
            report["multiWindow"] = {"firstAssociation": initial["paint"]["windowAssociation"],
                                      "secondSnapshot": second,
                                      "secondInputDrain": native_input_drain_acceptance(
                                          processes, host, driver, second, arguments.expected_ordering_contract)}
            host.commands.put_nowait(("close-window", 2))
            common.wait_for(processes, lambda: any(m["direction"] == "host" and m["kind"] == 10
                                                   and m["session"] == 2 for m in host.events()),
                            "second Viewer close sent", timeout=3, retry_errors=False)
            returned = locate_focused_viewer(processes, driver, 1, ready["region"])
            require(returned["paint"]["session"] == "1"
                    and returned["paint"]["windowAssociation"] == initial["paint"]["windowAssociation"],
                    "Closing another top-level lost the original exact association")
            report["multiWindow"]["returnedToFirst"] = returned
            host.expected_disconnect = True
            require(driver.command("stop").get("live") is False, "Stopped production client stayed live")
            require(driver.child.wait(timeout=5) == 0, "Production client fixture did not exit cleanly")
            host.check()
            report["cleanProductionClientStop"] = True
            report["syntheticFrameSha256"] = hashlib.sha256(host.frame).hexdigest()
        # Cleanup (including the bounded TLS worker join) is part of acceptance.
        report["status"] = "passed"
    except Exception as error:
        report["status"] = "failed"
        report["failure"] = str(error)
        (arguments.output / "failure.log").write_text(traceback.format_exc())
    finally:
        if processes:
            processes.close()
        report["qtInputContextMessages"] = [
            {"log": path.name, "message": line}
            for path in sorted(arguments.output.glob("*.log"))
            for line in path.read_text(errors="replace").splitlines()
            if "using input method:" in line
        ]
        (arguments.output / "result.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--viewer", type=lambda s: Path(s).resolve(strict=True))
    parser.add_argument("--fixture", type=lambda s: Path(s).resolve(strict=True))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--scale", type=float, choices=(1, 1.25), default=1)
    parser.add_argument("--focus-recovery", choices=FOCUS_RECOVERY_MODES, default="prepared-first-click",
                        help="Diagnostic transition before the two-window checks; ordinary-click reproduces the "
                             "old focus recovery without running first-click preparation")
    parser.add_argument("--expected-ordering-contract", choices=("", *SUPPORTED_ORDERING_CONTRACTS),
                        default=SUPPORTED_ORDERING_CONTRACTS[1],
                        help="Expected mapped build for this isolated fixture; empty tests unverified no-authority branch")
    parser.add_argument("--focus-child", type=Path, help=argparse.SUPPRESS)
    arguments = parser.parse_args(argv)
    if arguments.focus_child:
        return focus_child(arguments.focus_child)
    if not all((arguments.viewer, arguments.fixture, arguments.output)):
        parser.error("--viewer, --fixture and --output are required")
    return run(arguments)


if __name__ == "__main__":
    raise SystemExit(main())
