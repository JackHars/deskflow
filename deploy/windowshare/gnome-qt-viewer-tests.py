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
            self.send(2, {"version": 1, "role": "host", "codec": "h264", "capabilities": []})
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
                require(len(self.buffer) <= 32768, "Production fixture output exceeded bound")
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
        expected = {"observe": "observed", "retained": "retained", "stop": "stopped"}.get(operation, "synced")
        reply = self.event(expected)
        require(reply.get("id") == self.request, "Wrong fixture request reply")
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
        window.connect("map", lambda owner: path.write_text(json.dumps(
            {"mapped": True, "focused": owner.is_active(), "pid": os.getpid()})))
        window.connect("notify::is-active", lambda owner, _property:
                       path.write_text(json.dumps({"mapped": True, "focused": True, "pid": os.getpid()}))
                       if owner.is_active() else None)
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
              "compositorMonitorScalingTested": False,
              "originalDeliveryAuthority": False, "userDesktopUsed": False}
    processes = None
    try:
        with contextlib.ExitStack() as cleanup:
            root = Path(cleanup.enter_context(tempfile.TemporaryDirectory(prefix="df-qt-gnome-", dir="/tmp")))
            environment = common.private_environment(root)
            environment["QT_SCALE_FACTOR"] = str(arguments.scale)
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
            common.wait_for(processes, focus_log.exists, "independent private Wayland focus owner", timeout=5)
            driver.command("move", x=center[0], y=center[1])
            driver.command("click")
            common.wait_for(processes, lambda: json.loads(focus_log.read_text()).get("focused") is True,
                            "independent Wayland client focus", timeout=5)
            report["denials"]["focusLost"] = denied(driver.command("observe"), "focus lost to an independent client")
            focus.terminate()
            focus.wait(timeout=5)
            driver.command("move", x=center[0], y=center[1])
            driver.command("click")
            common.wait_for(processes, initially_ready, "original viewer focus recovery", timeout=5,
                            retry_errors=False)
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
            report["denials"]["secondWindow"] = denied(driver.command("observe"), "second actual viewer window")
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
        (arguments.output / "result.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--viewer", type=lambda s: Path(s).resolve(strict=True))
    parser.add_argument("--fixture", type=lambda s: Path(s).resolve(strict=True))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--scale", type=float, choices=(1, 1.25), default=1)
    parser.add_argument("--focus-child", type=Path, help=argparse.SUPPRESS)
    arguments = parser.parse_args()
    if arguments.focus_child:
        return focus_child(arguments.focus_child)
    if not all((arguments.viewer, arguments.fixture, arguments.output)):
        parser.error("--viewer, --fixture and --output are required")
    return run(arguments)


if __name__ == "__main__":
    raise SystemExit(main())
