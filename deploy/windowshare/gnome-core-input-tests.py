#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Exercise production Core -> EiComputer first-click preparation in private GNOME.

The real deskflow-core owns the only libei sender and provider session. A bounded,
mutually authenticated Core protocol peer supplies synthetic capture correlation;
the existing disposable TLS Host supplies video and acknowledges real Viewer focus.
No fixture reimplements emulation, PONG, provider drain, or gate cleanup policy.
This proves the production backend path, not physical Mac capture or original input
delivery. Both buses, compositor, credentials, settings and processes are private.
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
import re
import select
import signal
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import traceback
import unittest

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("core_input_qt_fixture", HERE / "gnome-qt-viewer-tests.py")
qt = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(qt)
common, require = qt.common, qt.require
STOP_SPEC = importlib.util.spec_from_file_location("core_input_private_stop", HERE / "x11-core-smoke.py")
private_stop = importlib.util.module_from_spec(STOP_SPEC)
STOP_SPEC.loader.exec_module(private_stop)
GENERATION = 7
CORE_NAME = b"gnome-core-input-fixture"
PREPARATION = struct.Struct(">QIQQH")
CUTOFF = struct.Struct(">QIQQH6QBQ")
MAX_PACKET = 4096


class Frames:
    def __init__(self):
        self.buffer = bytearray()

    def append(self, data):
        self.buffer.extend(data)
        require(len(self.buffer) <= 65536, "Core receive buffer exceeded its bound")
        result = []
        while len(self.buffer) >= 4:
            size, = struct.unpack_from(">I", self.buffer)
            require(4 <= size <= MAX_PACKET, "Invalid Core packet length")
            if len(self.buffer) < 4 + size:
                break
            result.append(bytes(self.buffer[4:4 + size]))
            del self.buffer[:4 + size]
        return result


def frame(payload):
    require(4 <= len(payload) <= MAX_PACKET, "Outgoing Core packet exceeded its bound")
    return struct.pack(">I", len(payload)) + payload


def preparation_binding(data):
    require(len(data) == PREPARATION.size, "Invalid preparation binding length")
    value = PREPARATION.unpack(data)
    require(all(value) and value[4] <= 16, "Invalid preparation identity")
    return value


def cutoff_binding(data):
    require(len(data) == CUTOFF.size, "Invalid cutoff binding length")
    value = CUTOFF.unpack(data)
    preparation_binding(data[:PREPARATION.size])
    require(all(0 < item < 2**53 for item in value[5:11]) and value[11] == 1 and value[12] > 0,
            "Cutoff omitted the exact All forwarding binding")
    return value


def prepared_result(payload):
    require(payload[:4] == b"CIPF" and len(payload) == 122, "Invalid prepared focus packet")
    body = payload[4:]
    binding = preparation_binding(body[:30])
    point = struct.unpack_from(">ii", body, 30)
    connection, session, epoch, geometry, focus_request, focus_revision = struct.unpack_from(">6Q", body, 38)
    pin = body[86:118].hex()
    require(all(abs(v) <= 10**7 for v in point) and connection > 0 and
            all(0 < v < 2**53 for v in (session, epoch, geometry, focus_request, focus_revision)) and
            int(pin, 16) != 0, "Invalid prepared focus identity or point")
    return {"binding": binding, "point": point, "connection": connection, "session": session,
            "epoch": epoch, "geometry": geometry, "focusRequest": focus_request,
            "focusRevision": focus_revision, "peerFingerprint": pin}


def preparation_receipt(payload):
    require(payload[:4] == b"CIPR" and len(payload) == 36, "Invalid preparation receipt length")
    binding = preparation_binding(payload[4:34])
    state, outcome = payload[34:36]
    require(state in (5, 6) and outcome <= 3 and (state != 6 or outcome != 0),
            "Preparation receipt claimed an intermediate or invalid wire state")
    return {"binding": binding, "state": state, "outcome": outcome}


def sealed_result(payload):
    require(payload[:4] == b"CIMS" and len(payload) == 328, "Invalid sealed mapped packet")
    body = payload[4:]
    binding = cutoff_binding(body[:87])
    point = struct.unpack_from(">ii", body, 87)
    x, y, width, height = struct.unpack_from(">4d", body, 95)
    surface, viewport = struct.unpack_from(">QQ", body, 127)
    native_surface, provider, revision = struct.unpack_from(">III", body, 143)
    pin = body[155:187].hex()
    length = body[187]
    require(0 < length <= 128 and not any(body[188 + length:316]), "Invalid mapped contract padding")
    contract = body[188:188 + length].decode("ascii")
    seal, = struct.unpack_from(">Q", body, 316)
    require(all(math.isfinite(v) for v in (x, y, width, height)) and 0 <= x < width <= 10**7 and
            0 <= y < height <= 10**7 and all((surface, viewport, native_surface, provider, revision)) and
            0 < seal <= binding[0] and int(pin, 16) != 0 and all(abs(v) <= 10**7 for v in point),
            "Invalid mapped point, native identity, or seal")
    require(re.fullmatch(r"[a-zA-Z0-9.:_-]+", contract), "Invalid mapped contract characters")
    return {"binding": binding, "point": point, "source": [x, y, width, height], "seal": seal,
            "surfaceIncarnation": surface, "viewportRevision": viewport, "nativeSurface": native_surface,
            "providerGeneration": provider, "nativeRevision": revision, "peerFingerprint": pin,
            "orderingContract": contract}


def cutoff_receipt(payload):
    require(payload[:4] == b"CICR" and len(payload) == 92, "Invalid cutoff receipt length")
    binding = cutoff_binding(payload[4:91])
    require(payload[-1] in (1, 2, 3), "Local-only cutoff state appeared on the wire")
    return {"binding": binding, "state": payload[-1]}


def route_request(prepared, generation, mode):
    require(mode in ("original", "viewer") and 0 < generation < 2**53, "Invalid forwarding request")
    return {"kind": qt.INPUT_ROUTE, "session": prepared["session"], "epoch": prepared["epoch"],
            "payload": {"generation": generation, "focusRequest": prepared["focusRequest"],
                        "focusRevision": prepared["focusRevision"], "geometry": prepared["geometry"],
                        "action": "request", "mode": mode}}


def mapped_matches(mapped, prepared, binding, expected_contract):
    require(mapped["binding"] == binding and mapped["seal"] == binding[0] and
            mapped["point"] == prepared["point"] and mapped["peerFingerprint"] == prepared["peerFingerprint"] and
            mapped["orderingContract"] == expected_contract and expected_contract in qt.SUPPORTED_ORDERING_CONTRACTS,
            "Mapped result substituted preparation, point, Viewer identity, or audited sender contract")


class CorePeer:
    """One thread owns one TLS connection and every framed read/write."""

    def __init__(self, processes, host):
        self.processes, self.host = processes, host
        self.commands = queue.Queue(maxsize=32)
        self.stopping = threading.Event()
        self.lock = threading.Lock()
        self.messages = []
        self.failure = None
        self.connection = None
        self.expected_disconnect = False
        self.connections = 0
        self.hello = self.configured = False
        self.info = None
        self.core = None
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(.1)
        self.port = self.listener.getsockname()[1]
        self.log = (processes.output / "core-packets.jsonl").open("x", buffering=1)
        self.thread = threading.Thread(target=self.run, name="private-core-tls-peer", daemon=True)
        self.thread.start()

    def check(self):
        if self.core is not None:
            check_core(self.core)
        self.processes.check()
        self.host.check()
        require(self.failure is None, f"Core TLS fixture failed: {self.failure}")

    def events(self):
        self.check()
        with self.lock:
            return list(self.messages)

    def record(self, direction, payload):
        entry = {"time": time.monotonic(), "direction": direction,
                 "kind": "Hello" if payload.startswith(b"Barrier") else payload[:4].decode("ascii"),
                 "hex": payload.hex()}
        with self.lock:
            require(len(self.messages) < 4096, "Core packet evidence exceeded its bound")
            self.messages.append(entry)
        self.log.write(json.dumps(entry) + "\n")
        return entry

    def write(self, payload):
        self.connection.sendall(frame(payload))
        return self.record("server", payload)

    def send(self, payload):
        done, result = threading.Event(), []
        self.commands.put_nowait((payload, done, result))
        require(done.wait(2), "Core write did not complete in its bound")
        self.check()
        require(result, "Core write ended without a record")
        return result[0]

    def packet(self, payload):
        self.record("client", payload)
        if not self.hello:
            require(payload[:7] == b"Barrier" and len(payload) >= 15, "Invalid Core Hello")
            major, minor, size = struct.unpack_from(">HHI", payload, 7)
            require((major, minor) == (1, 8) and payload[15:] == CORE_NAME and size == len(CORE_NAME),
                    "Core Hello changed name or negotiated protocol")
            self.hello = True
            self.write(b"QINF")
            return
        kind = payload[:4]
        if kind == b"DINF":
            require(len(payload) == 18, "Invalid Core desktop info")
            self.info = struct.unpack(">hhHHHhh", payload[4:])
            self.write(b"CIAK")
            if not self.configured:
                options = [int.from_bytes(b"IPC5", "big"), GENERATION, int.from_bytes(b"IPP1", "big"), GENERATION]
                self.write(b"DSOP" + struct.pack(">I4I", len(options), *options))
                self.configured = True
        elif kind in (b"CICB", b"CIPA"):
            require(payload == kind + struct.pack(">I", GENERATION), "Incorrect authenticated capability ACK")
        elif kind == b"CIPF":
            prepared_result(payload)
        elif kind == b"CIPR":
            preparation_receipt(payload)
        elif kind == b"CIMS":
            sealed_result(payload)
        elif kind == b"CICR":
            cutoff_receipt(payload)
        elif kind in (b"CNOP", b"CALV"):
            require(len(payload) == 4, "Unexpected Core control payload")
        elif kind == b"CCLP":
            require(len(payload) == 9, "Unexpected clipboard ownership payload")
        else:
            raise RuntimeError(f"Unexpected Core packet: {kind!r}")

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
            self.listener.close()  # A replacement connection cannot satisfy this run.
            peer.settimeout(3)
            with self.host.context.wrap_socket(peer, server_side=True) as connection:
                self.connection = connection
                self.connections += 1
                require(hashlib.sha256(connection.getpeercert(binary_form=True)).hexdigest() == self.host.viewer_pin,
                        "Core did not present its approved disposable TLS leaf")
                connection.settimeout(1)
                self.write(b"Barrier" + struct.pack(">HH", 1, 8))
                decoder, heartbeat = Frames(), time.monotonic()
                while not self.stopping.is_set():
                    try:
                        payload, done, result = self.commands.get_nowait()
                        try:
                            result.append(self.write(payload))
                        finally:
                            done.set()
                    except queue.Empty:
                        pass
                    if self.hello and time.monotonic() - heartbeat >= .5:
                        self.write(b"CALV")
                        heartbeat = time.monotonic()
                    if not connection.pending() and not select.select([connection], [], [], .005)[0]:
                        continue
                    data = connection.recv(16384)
                    if not data:
                        require(self.expected_disconnect or self.stopping.is_set(), "Production Core disconnected early")
                        require(not decoder.buffer, "Core closed in a partial packet")
                        return
                    for payload in decoder.append(data):
                        self.packet(payload)
        except Exception:
            if not self.stopping.is_set() and not self.expected_disconnect:
                self.failure = traceback.format_exc()
        finally:
            self.connection = None

    def close(self):
        self.stopping.set()
        self.listener.close()
        self.thread.join(timeout=4)
        require(not self.thread.is_alive(), "Owned Core TLS thread did not stop")
        self.log.close()


def log_text(path):
    require(not path.exists() or path.stat().st_size <= common.LOG_LIMIT, "Core log exceeded its bound")
    return path.read_text(errors="replace") if path.exists() else ""


def process_state(process):
    """Inspect a Popen-owned process before teardown changes its exit status."""
    status = process.poll()
    result = {"pid": process.pid, "running": status is None, "returncode": status}
    if status is not None and status < 0:
        try:
            result["signal"] = signal.Signals(-status).name
        except ValueError:
            result["signal"] = str(-status)
    return result


def check_core(core):
    state = process_state(core)
    require(state["running"], "Production Core exited before fixture cleanup: " + json.dumps(state, sort_keys=True))


def before_cleanup(report, processes, core, shell, peer, host):
    """Called first by ExitStack, before closing either TLS peer or any child.

    A Viewer EOF can precede waitpid observing its parent's crash. Preserve
    the independently polled Core status and both peer errors at this exact
    boundary; never infer that our subsequent SIGTERM was the initial cause.
    """
    state = {"time": time.monotonic(), "core": process_state(core), "shell": process_state(shell),
             "corePeerFailure": peer.failure, "viewerPeerFailure": host.failure,
             "coreTlsConnections": peer.connections,
             "ownedProcesses": [process_state(child) for child in processes.children]}
    report["beforeCleanup"] = state
    (processes.output / "process-state-before-cleanup.json").write_text(
        json.dumps(state, indent=2, sort_keys=True) + "\n")


def wait(peer, core, predicate, description, timeout=5):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        require(core.poll() is None, f"Production Core exited before {description}: {core.returncode}")
        peer.check()
        result = predicate()
        if result:
            return result
        time.sleep(.005)
    raise RuntimeError(f"Timed out waiting for {description}")


def received(peer, kinds, after=0):
    return [entry for entry in peer.events()[after:] if entry["direction"] == "client" and entry["kind"] in kinds]


def next_packet(peer, core, kinds, after, description, timeout=2):
    values = wait(peer, core, lambda: received(peer, kinds, after), description, timeout)
    # A legitimate Failed -> Released pair may arrive in one TLS read. The
    # final transcript check rejects duplicates and contradictory outcomes.
    return bytes.fromhex(values[0]["hex"]), values[0]


def exact_transcript(messages, origin, binding=None):
    """Validate the complete observed response sequence, including cleanup."""
    kinds = {"CICB", "CIPA", "CIPF", "CIPR", "CIMS", "CICR"}
    replies = [entry for entry in messages if entry["direction"] == "client" and entry["kind"] in kinds]
    require([entry["kind"] for entry in replies[:2]] == ["CICB", "CIPA"] and
            all(bytes.fromhex(entry["hex"]) == entry["kind"].encode() + struct.pack(">I", GENERATION)
                for entry in replies[:2]), "Capability acknowledgement was omitted, repeated or reordered")
    transaction = replies[2:]
    if binding is None:
        require([entry["kind"] for entry in transaction] in (["CIPR"], ["CIPR", "CIPR"]),
                "Unsupported preparation emitted an unexpected response")
        states = []
        for entry in transaction:
            receipt = preparation_receipt(bytes.fromhex(entry["hex"]))
            require(receipt["binding"] == origin and receipt["outcome"] == 2,
                    "Unsupported cleanup substituted capture or outcome")
            states.append(receipt["state"])
        require(states in ([5], [6, 5]), "Unsupported cleanup lacked exactly one final Released")
    else:
        require([entry["kind"] for entry in transaction] in
                (["CIPF", "CIMS", "CICR"], ["CIPF", "CIMS", "CICR", "CICR"]),
                "Successful preparation duplicated or reordered a focus, mapped or cleanup reply")
        require(prepared_result(bytes.fromhex(transaction[0]["hex"]))["binding"] == origin and
                sealed_result(bytes.fromhex(transaction[1]["hex"]))["binding"] == binding,
                "Preparation transcript substituted its retained identity")
        states = []
        for entry in transaction[2:]:
            receipt = cutoff_receipt(bytes.fromhex(entry["hex"]))
            require(receipt["binding"] == binding, "Cleanup changed the exact promoted cutoff")
            states.append(receipt["state"])
        # Resuming Host All may revoke the seal before DICX reaches Core.
        # Denied is revocation only; the separate exact Resumed is required.
        require(states in ([3], [2, 3]), "Cutoff cleanup lacked exactly one final Resumed")
    return replies


def resumed_packet(peer, core, binding, after):
    def complete():
        values = received(peer, ("CICR", "CIPR"), after)
        states = []
        for entry in values:
            receipt = cutoff_receipt(bytes.fromhex(entry["hex"]))
            require(receipt["binding"] == binding, "Cleanup reply substituted its cutoff identity")
            states.append(receipt["state"])
        require(states in ([], [2], [3], [2, 3]), "Unexpected, repeated or reordered cutoff cleanup reply")
        return values[-1] if states and states[-1] == 3 else None
    return wait(peer, core, complete, "exact production Core cleanup acknowledgement", 2)


def file_digest(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def exact_forwarding(peer, core, host, request):
    start = len(host.events())
    host.commands.put_nowait(("input-route", request))
    return wait(peer, core, lambda: qt.forwarding_acknowledgement(host.events(), request, start),
                "genuine Viewer All acknowledgement", 2)


def assert_no_acquisition(messages, start, end):
    return qt.preparation_input_receipts(messages, start, end)


def idle_generation(text):
    values = re.findall(r"GNOME EI idle stop generation=(\d+)", text)
    require(len(values) <= 1, "Unexpected repeated idle stop in one interval")
    return int(values[0]) if values else None


def run(arguments):
    require(sys.platform == "linux" and os.geteuid() != 0 and Path("/.dockerenv").is_file(),
            "Run unprivileged in the dedicated disposable Linux container")
    require(arguments.output.is_absolute(), "Output must be an absolute new directory")
    arguments.output.mkdir(parents=True, exist_ok=False)
    os.umask(0o077)
    report = {"schema": 1, "status": "failed", "scope": "production-core-ei-first-click",
              "productionCoreCutoff": True, "productionEiComputer": True, "originalDeliveryAuthority": False,
              "captureCorrelation": "synthetic-authenticated-core-protocol", "physicalMacCapture": False,
              "userDesktopUsed": False, "scale": arguments.scale, "scaleSource": "QT_SCALE_FACTOR"}
    processes = core = None
    try:
        with contextlib.ExitStack() as cleanup:
            report["artifacts"] = {label: {"path": str(path), "sha256": file_digest(path)} for label, path in
                                   (("core", arguments.core), ("viewer", arguments.viewer),
                                    ("harness", Path(__file__).resolve()),
                                    ("qtHarness", HERE / "gnome-qt-viewer-tests.py"),
                                    ("privateStopHarness", HERE / "x11-core-smoke.py"),
                                    ("commonHarness", HERE / "gnome-provider-tests.py"))}
            root = Path(cleanup.enter_context(tempfile.TemporaryDirectory(prefix="df-core-gnome-", dir="/tmp")))
            environment = common.private_environment(root)
            environment["QT_SCALE_FACTOR"] = str(arguments.scale)
            processes = common.Processes(arguments.output, environment, lifetime_seconds=120)
            cleanup.callback(processes.close)
            shell = qt.start_shell(processes, root, report)
            control = common.Provider(processes.env["DBUS_SESSION_BUS_ADDRESS"])
            try:
                _, _, capabilities = control.call("GetCapabilities")
            finally:
                control.close()
            contract = capabilities.get("inputDrainOrderingContract", "")
            require(contract == "" or contract in qt.SUPPORTED_ORDERING_CONTRACTS,
                    "Provider claimed an unreviewed mapped sender contract")
            require(capabilities.get("inputPreparation") is bool(contract),
                    "Provider preparation capability disagrees with its audited sender contract")
            if arguments.expected_ordering_contract is not None:
                require(contract == arguments.expected_ordering_contract, "Unexpected native ordering build")
            report["capabilities"] = capabilities
            host = qt.TlsHost(processes, root, arguments.viewer)
            cleanup.callback(host.close)
            peer = CorePeer(processes, host)
            cleanup.callback(peer.close)
            tls = root / "tls"
            tls.mkdir(mode=0o700)
            pem = tls / "core.pem"
            pem.write_bytes((host.viewer_identity / "cert.pem").read_bytes() +
                            (host.viewer_identity / "key.pem").read_bytes())
            pem.chmod(0o600)
            (tls / "trusted-servers").write_text("v2:sha256:" + host.host_pin + "\n")
            settings = root / "core.ini"
            settings.write_text(
                f"[client]\nremoteHost=127.0.0.1\nlanguageSync=false\n[core]\nport={peer.port}\n"
                f"computerName={CORE_NAME.decode()}\npreventSleep=false\nenableEnterCommand=false\n"
                "enableExitCommand=false\n[security]\ntlsEnabled=true\ncheckPeerFingerprints=true\n"
                f"certificate={pem}\n[log]\nlevel=DEBUG\ntoFile=false\n")
            configuration = root / "provider.json"
            configuration.write_text(json.dumps({"version": 1, "viewerExecutable": str(arguments.viewer),
                                                 "connect": "127.0.0.1", "port": host.port,
                                                 "identity": str(host.viewer_identity), "peerPin": host.host_pin}))
            core = processes.start("core", [str(arguments.core), "client", "--new-instance", "--settings",
                                             str(settings), "--windowshare-gnome-config=" + str(configuration)])
            peer.core = core
            cleanup.callback(before_cleanup, report, processes, core, shell, peer, host)
            core_log = arguments.output / "core.log"
            wait(peer, core, lambda: received(peer, ("CICB",)) and received(peer, ("CIPA",)),
                 "authenticated IPC5 and IPP1 negotiation", 15)
            wait(peer, core, lambda: peer.info and peer.info[2] >= 1000 and peer.info[3] >= 600,
                 "actual EI desktop dimensions", 15)
            wait(peer, core, lambda: any(e["direction"] == "viewer" and e["kind"] == 16 for e in host.events()),
                 "actual Viewer painted Ready", 15)
            # The fixture requests a deterministic point inside its centered
            # video, outside the separate centered 360x180 focus owner's bounds.
            # Only the production native hit/map can accept this candidate.
            x, y, width, height, _, _, _ = peer.info
            point = (x + width // 4, y + height // 4)
            peer.send(b"CINN" + struct.pack(">hhIH", *point, 1, 0))
            peer.send(b"DMMV" + struct.pack(">hh", *point))
            wait(peer, core, lambda: "connected experimental GNOME input session and owned Wayland viewer" in
                 log_text(core_log), "production provider ownership", 10)
            focus_path = root / "independent-focus.json"
            focus = processes.start("independent-focus", [sys.executable, str(HERE / "gnome-qt-viewer-tests.py"),
                                                           "--focus-child", str(focus_path)])
            before_focus = wait(peer, core, lambda: qt.current_focus(focus_path, focus),
                                "independent native focus owner", 5)
            log_offset = len(log_text(core_log))
            last_motion = peer.send(b"DMMV" + struct.pack(">hh", *point))
            stopped = wait(peer, core, lambda: idle_generation(log_text(core_log)[log_offset:])
                           if time.monotonic() - last_motion["time"] > 4.05 else None,
                           "actual production four-second idle stop", 7)
            require(qt.current_focus(focus_path, focus) == before_focus, "Focus changed during the idle interval")
            require(all(e["kind"] in ("CALV", "DMMV") for e in peer.events()
                        if e["direction"] == "server" and e["time"] >= last_motion["time"]),
                    "Unexpected submission renewed the idle interval")
            report["idle"] = {"generation": stopped, "waitedSeconds": time.monotonic() - last_motion["time"],
                              "diagnostic": "actual EiComputer idle timer", "sameCoreConnections": peer.connections}
            begin = len(host.events())
            start = len(peer.events())
            origin = (1, GENERATION, 31, 41, 1)
            restart_offset = len(log_text(core_log))
            sent = peer.send(b"DIPP" + PREPARATION.pack(*origin) + struct.pack(">iiH", *point, 500))
            payload, receipt = next_packet(peer, core, ("CIPF", "CIPR"), start, "prepared focus or explicit refusal")
            restart = re.findall(r"GNOME EI cutoff idle restart generation=(\d+)", log_text(core_log)[restart_offset:])
            require(restart == [str(stopped + 1)], "Preparation did not restart the exact idle production sender once")
            report["idle"]["restartedGeneration"] = int(restart[0])
            if not contract:
                denied = preparation_receipt(payload)
                require(denied["binding"] == origin and denied["outcome"] == 2,
                        "Unknown native ordering build did not explicitly refuse as unsupported")
                if denied["state"] != 5:
                    payload, _ = next_packet(peer, core, ("CIPR",), peer.events().index(receipt) + 1,
                                             "unsupported preparation Released")
                    denied = preparation_receipt(payload)
                require(denied == {"binding": origin, "state": 5, "outcome": 2} and
                        qt.current_focus(focus_path, focus) == before_focus, "Unsupported preparation changed focus")
                require(not received(peer, ("CIPF", "CIMS"), start), "Unknown platform issued a mapped authority receipt")
                report["unsupported"] = denied
                report["focusOnly"] = assert_no_acquisition(host.events(), begin, len(host.events()))
                report["coreResponses"] = exact_transcript(peer.events(), origin)
            else:
                prepared = prepared_result(payload)
                require(prepared["binding"] == origin and prepared["point"] == point and
                        prepared["peerFingerprint"] == host.host_pin and receipt["time"] < sent["time"] + .5,
                        "Prepared result changed exact capture, point, peer, or inherited deadline")
                focus_acks = [e for e in host.events()[begin:] if e["direction"] == "host" and e["kind"] == 14
                              and e["session"] == prepared["session"] and e["epoch"] == prepared["epoch"] and
                              e["payload"] == {"request": prepared["focusRequest"],
                                               "focusRevision": prepared["focusRevision"]}]
                require(len(focus_acks) == 1, "Prepared result lacked one exact real Host focus ACK")
                report["prepared"], report["focusAck"] = prepared, focus_acks[0]
                request = route_request(prepared, 1, "original")
                report["allAcknowledged"] = exact_forwarding(peer, core, host, request)
                binding = (2, *origin[1:], prepared["session"], prepared["epoch"], 1,
                           prepared["focusRequest"], prepared["focusRevision"], prepared["geometry"],
                           1, prepared["connection"])
                budget = min(250, int((sent["time"] + .5 - time.monotonic()) * 1000))
                require(budget > 0, "Original preparation budget expired before promotion")
                start = len(peer.events())
                peer.send(b"DIPM" + PREPARATION.pack(*origin) + CUTOFF.pack(*binding) + struct.pack(">H", budget))
                payload, mapped_receipt = next_packet(peer, core, ("CIMS", "CICR", "CIPR"), start,
                                                      "production promoted mapped seal")
                mapped = sealed_result(payload)
                mapped_matches(mapped, prepared, binding, contract)
                require(mapped_receipt["time"] < sent["time"] + .5, "Promotion renewed the original deadline")
                report["mapped"] = mapped
                report["focusOnly"] = assert_no_acquisition(host.events(), begin, len(host.events()))
                # Host All resumes first. The distinct preparation gate still
                # belongs to Core's exact linked asynchronous cleanup.
                resumed_request = route_request(prepared, 1, "viewer")
                report["hostResumedBeforeCleanup"] = exact_forwarding(peer, core, host, resumed_request)
                start = len(peer.events())
                cleanup_sent = peer.send(b"DICX" + CUTOFF.pack(*binding))
                cleanup_receipt = resumed_packet(peer, core, binding, start)
                resumed = cutoff_receipt(bytes.fromhex(cleanup_receipt["hex"]))
                require(resumed == {"binding": binding, "state": 3}, "Cleanup did not acknowledge exact Resumed")
                report["cleanup"] = {**resumed, "elapsedMs": 1000 * (cleanup_receipt["time"] - cleanup_sent["time"])}
                report["closedInterval"] = assert_no_acquisition(host.events(), begin, len(host.events()))
                peer.send(b"DKDL" + struct.pack(">HHHI", 0xEFC6, 0, 67, 0))
                peer.send(b"DKUP" + struct.pack(">HHH", 0xEFC6, 0, 67))
                expected = [{"kind": "key", "code": 16777272, "down": down, "repeat": False}
                            for down in (True, False)]
                fresh = wait(peer, core, lambda: qt.forwarding_receipts(host.events(), request, expected,
                                                                       begin, [0, 0]),
                             "fresh production Core key pair after exact cleanup", 3)
                # A following actual All ACK closes the positive evidence
                # interval, so delayed/duplicate queued input is not ignored.
                closing = route_request(prepared, 2, "original")
                closed = exact_forwarding(peer, core, host, closing)
                require(qt.forwarding_receipts(host.events()[:closed["index"] + 1], request, expected,
                                              begin, [0, 0]) is not None,
                        "Final closed interval lost the complete fresh input prefix")
                report["freshAfterCleanup"], report["positiveIntervalClosed"] = fresh, closed
                report["finalHostResume"] = exact_forwarding(peer, core, host, route_request(prepared, 2, "viewer"))
                report["coreResponses"] = exact_transcript(peer.events(), origin, binding)
            logs = log_text(core_log)
            require(peer.connections == 1 and logs.count("connected experimental GNOME input session and owned Wayland viewer") == 1
                    and logs.count("connected to server") == 1 and "experimental GNOME session stopped:" not in logs
                    and "disconnected from server" not in logs and shell.poll() is None,
                    "TLS/provider/compositor lifecycle changed during the transaction")
            report["sameLifecycle"] = {"coreTlsConnections": 1, "providerConnections": 1,
                                       "corePeerPin": host.host_pin, "viewerPeerPin": host.viewer_pin,
                                       "shellPid": shell.pid}
            peer.expected_disconnect = host.expected_disconnect = True
            report["beforeRequestedStop"] = process_state(core)
            # Core's supported graceful shutdown is its Qt IPC command. The
            # shared helper validates this private socket's SO_PEERCRED against
            # the Popen-owned child before sending anything, then bounds both
            # the acknowledgement and normal exit. SIGTERM is failure cleanup,
            # not evidence that the production teardown path ran.
            private_stop.stop_core(core, Path(processes.env["TMPDIR"]), "GNOME input fixture Core")
            report["coreExitCode"] = core.returncode
            require(core.returncode == 0 and all(message in log_text(core_log) for message in (
                        "core ipc server got stop message", "stopped client", "core stopped successfully")),
                    "Production Core failed during owned teardown")
            report["coreShutdown"] = {"method": "private-ipc-stop", "peerPid": core.pid,
                                       "peerCredentialsVerified": True, "normalExit": True}
            report["status"] = "passed"
    except Exception as error:
        report["failure"] = str(error)
        (arguments.output / "failure.log").write_text(traceback.format_exc())
    finally:
        if processes:
            processes.close()
        if core is not None:
            report["coreAfterCleanup"] = process_state(core)
        (arguments.output / "result.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


class WireTests(unittest.TestCase):
    origin = (1, GENERATION, 31, 41, 1)
    cutoff = (2, GENERATION, 31, 41, 1, 2, 3, 4, 5, 6, 7, 1, 8)

    def prepared(self):
        return (b"CIPF" + PREPARATION.pack(*self.origin) +
                struct.pack(">ii6Q", -7, 41, 8, 2, 3, 7, 5, 6) + b"a" * 32)

    def sealed(self):
        contract = qt.SUPPORTED_ORDERING_CONTRACTS[0].encode()
        return (b"CIMS" + CUTOFF.pack(*self.cutoff) + struct.pack(">ii4dQQIII", -7, 41, 5, 6, 640, 480,
                                                             12, 13, 14, 15, 16) + b"a" * 32 +
                bytes((len(contract),)) + contract + bytes(128 - len(contract)) + struct.pack(">Q", 2))

    @staticmethod
    def event(payload):
        return {"direction": "client", "kind": payload[:4].decode(), "hex": payload.hex()}

    def negotiation(self):
        return [self.event(kind + struct.pack(">I", GENERATION)) for kind in (b"CICB", b"CIPA")]

    def test_packet_boundaries_and_every_fragment(self):
        data = frame(b"CNOP") + frame(b"CIPA" + struct.pack(">I", GENERATION))
        for split in range(len(data) + 1):
            decoder = Frames()
            self.assertEqual(decoder.append(data[:split]) + decoder.append(data[split:]),
                             [b"CNOP", b"CIPA" + struct.pack(">I", GENERATION)])
            self.assertFalse(decoder.buffer)
        for length in (0, 3, MAX_PACKET + 1, 0xffffffff):
            with self.assertRaises(RuntimeError):
                Frames().append(struct.pack(">I", length))

    def test_preparation_full_width_and_closed_schema(self):
        binding = (2**64 - 2, 2**32 - 1, 2**64 - 3, 2**64 - 4, 16)
        value = b"CIPF" + PREPARATION.pack(*binding) + struct.pack(">ii6Q", -7, 41, 12, 2, 3, 4, 5, 6) + b"a" * 32
        parsed = prepared_result(value)
        self.assertEqual(parsed["binding"], binding)
        self.assertEqual(parsed["point"], (-7, 41))
        for altered in [value[:i] for i in range(len(value))] + [value + b"x", value[:-32] + bytes(32)]:
            with self.assertRaises(RuntimeError):
                prepared_result(altered)

    def test_only_terminal_wire_cleanup_states(self):
        binding = PREPARATION.pack(1, 7, 31, 41, 1)
        for state in range(8):
            packet = b"CIPR" + binding + bytes((state, 2))
            if state in (5, 6):
                self.assertEqual(preparation_receipt(packet)["state"], state)
            else:
                with self.assertRaises(RuntimeError):
                    preparation_receipt(packet)
        for value in (0, 4, 5, 6, 255):
            with self.assertRaises(RuntimeError):
                cutoff_receipt(b"CICR" + CUTOFF.pack(2, 7, 31, 41, 1, 2, 3, 4, 5, 6, 7, 1, 8) + bytes((value,)))

    def test_mapped_schema_and_exact_prepared_correlation(self):
        packet = self.sealed()
        mapped, prepared = sealed_result(packet), prepared_result(self.prepared())
        mapped_matches(mapped, prepared, self.cutoff, qt.SUPPORTED_ORDERING_CONTRACTS[0])
        for altered in [packet[:i] for i in range(len(packet))] + [packet + b"x"]:
            with self.assertRaises(RuntimeError):
                sealed_result(altered)
        for offset, replacement in ((99, struct.pack(">d", float("nan"))),
                                    (4 + 316 - 1, b"x"), (4 + 316, bytes(8)),
                                    (4 + 155, bytes(32)), (4 + 78, b"\0")):
            invalid = packet[:offset] + replacement + packet[offset + len(replacement):]
            with self.assertRaises(RuntimeError):
                sealed_result(invalid)
        for key, wrong in (("point", (0, 0)), ("binding", (3, *self.cutoff[1:])),
                           ("peerFingerprint", "62" * 32), ("orderingContract", "unreviewed"), ("seal", 1)):
            with self.assertRaises(RuntimeError):
                mapped_matches({**mapped, key: wrong}, prepared, self.cutoff, qt.SUPPORTED_ORDERING_CONTRACTS[0])

    def test_unsupported_failed_released_same_read_and_no_duplicates(self):
        failed = b"CIPR" + PREPARATION.pack(*self.origin) + bytes((6, 2))
        released = b"CIPR" + PREPARATION.pack(*self.origin) + bytes((5, 2))
        packets = Frames().append(frame(failed) + frame(released))
        prefix = self.negotiation()
        for values in ((released,), tuple(packets)):
            exact_transcript(prefix + [self.event(value) for value in values], self.origin)
        for values in ((failed,), (released, released), (released, failed), (failed, failed, released)):
            with self.assertRaises(RuntimeError):
                exact_transcript(prefix + [self.event(value) for value in values], self.origin)

    def test_cutoff_revocation_is_not_cleanup_and_no_duplicate_authority(self):
        prefix = self.negotiation() + [self.event(self.prepared()), self.event(self.sealed())]
        denied = self.event(b"CICR" + CUTOFF.pack(*self.cutoff) + b"\2")
        resumed = self.event(b"CICR" + CUTOFF.pack(*self.cutoff) + b"\3")
        for suffix in ([resumed], [denied, resumed]):
            exact_transcript(prefix + suffix, self.origin, self.cutoff)
        wrong = list(self.cutoff)
        wrong[-1] += 1
        for replies in (prefix, prefix + [denied], prefix + [resumed, resumed],
                        prefix + [resumed, denied], prefix + [denied, denied, resumed],
                        prefix + [self.event(self.sealed()), resumed],
                        prefix + [self.event(b"CICR" + CUTOFF.pack(*wrong) + b"\3")],
                        prefix[:2] + [self.event(self.prepared()), self.event(self.prepared()), *prefix[3:], resumed]):
            with self.assertRaises(RuntimeError):
                exact_transcript(replies, self.origin, self.cutoff)

    def test_positive_interval_rejects_any_forwarded_preparation_input(self):
        self.assertEqual(assert_no_acquisition([], 0, 0)["forwardedInput"], [])
        for kind in ("key", "button", "move", "scroll"):
            with self.assertRaises(RuntimeError):
                assert_no_acquisition([{"direction": "viewer", "kind": 7, "payload": {"kind": kind}}], 0, 1)
        self.assertEqual(idle_generation("GNOME EI idle stop generation=19"), 19)
        self.assertIsNone(idle_generation("device stopped emulating"))
        with self.assertRaises(RuntimeError):
            idle_generation("GNOME EI idle stop generation=19\nGNOME EI idle stop generation=20")

    def test_core_exit_is_checked_before_secondary_viewer_failure(self):
        class Process:
            pid, returncode = 41, -signal.SIGSEGV

            def poll(self):
                return self.returncode

        class Peer:
            checked = False

            def check(self):
                self.checked = True
                raise RuntimeError("secondary Viewer EOF")

        peer = Peer()
        with self.assertRaisesRegex(RuntimeError, "Production Core exited.*-11"):
            wait(peer, Process(), lambda: False, "negotiation")
        self.assertFalse(peer.checked)
        with self.assertRaisesRegex(RuntimeError, "SIGSEGV"):
            check_core(Process())

    def test_cleanup_snapshot_preserves_original_exit_and_live_core(self):
        from types import SimpleNamespace

        class Process:
            def __init__(self, pid, status):
                self.pid, self.returncode = pid, status

            def poll(self):
                return self.returncode

        for status in (-signal.SIGSEGV, 0, None):
            with self.subTest(status=status), tempfile.TemporaryDirectory() as directory:
                core, shell = Process(41, status), Process(42, None)
                processes = SimpleNamespace(output=Path(directory), children=[shell, core])
                peer = SimpleNamespace(failure="Core EOF", connections=1)
                host = SimpleNamespace(failure="Viewer EOF")
                report = {}
                with contextlib.ExitStack() as cleanup:
                    cleanup.callback(setattr, core, "returncode", -signal.SIGTERM)
                    cleanup.callback(before_cleanup, report, processes, core, shell, peer, host)
                saved = json.loads((Path(directory) / "process-state-before-cleanup.json").read_text())
                self.assertEqual(saved, report["beforeCleanup"])
                self.assertEqual(saved["core"]["returncode"], status)
                self.assertEqual(saved["core"]["running"], status is None)
                self.assertEqual(saved["corePeerFailure"], "Core EOF")
                self.assertEqual(saved["viewerPeerFailure"], "Viewer EOF")
                self.assertEqual(core.returncode, -signal.SIGTERM)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=lambda value: Path(value).resolve(strict=True))
    parser.add_argument("--viewer", type=lambda value: Path(value).resolve(strict=True))
    parser.add_argument("--scale", type=float, choices=(1, 1.25), default=1)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expected-ordering-contract", choices=("", *qt.SUPPORTED_ORDERING_CONTRACTS),
                        help="Require an exact audited build; empty requires explicit unsupported refusal")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        return 0 if unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(WireTests)).wasSuccessful() else 1
    if not all((arguments.core, arguments.viewer, arguments.output)):
        parser.error("--core, --viewer and --output are required")
    return run(arguments)


if __name__ == "__main__":
    raise SystemExit(main())
