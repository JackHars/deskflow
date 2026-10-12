#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Correlate real Ubuntu viewer input with an owned disposable Mac window.

Requires an explicitly prepared Linux harness plan and existing SSH key access.
The Mac host listens only on loopback; SSH forwards one new remote Unix socket.
Only the test source window is exported. This does not test physical input,
holding displays, ordinary Deskflow routing, or drag handoff.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import selectors
import shlex
import signal
import socket
import subprocess
import time
import uuid


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def progress(phase):
    print(json.dumps({"phase": phase}), flush=True)


def wait_for(predicate, description, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.025)
    raise RuntimeError("Timed out waiting for " + description)


def stop(process):
    if process is None:
        return None
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=3)
    return process.returncode


class Remote:
    def __init__(self, process, transcript):
        self.process = process
        self.transcript = transcript
        self.selector = selectors.DefaultSelector()
        self.selector.register(process.stdout, selectors.EVENT_READ)
        self.buffer = b""
        self.sequence = 0

    def event(self, expected, timeout=25):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if b"\n" not in self.buffer:
                if not self.selector.select(timeout=0.05):
                    continue
                data = os.read(self.process.stdout.fileno(), 4096)
                require(data, f"Linux harness exited before {expected}")
                self.buffer += data
                require(len(self.buffer) <= 131072, "Linux control output exceeded bound")
                continue
            line, self.buffer = self.buffer.split(b"\n", 1)
            self.transcript.write(line.decode() + "\n")
            self.transcript.flush()
            result = json.loads(line)
            require(result.get("event") == expected, f"Expected {expected}, received {result}")
            return result
        raise RuntimeError("Linux harness timed out before " + expected)

    def command(self, operation, **values):
        self.sequence += 1
        request = {"id": self.sequence, "op": operation, **values}
        self.transcript.write(json.dumps({"sent": request}) + "\n")
        self.process.stdin.write((json.dumps(request) + "\n").encode())
        self.process.stdin.flush()
        event = {"prepare": "prepared", "observe": "observed", "stop": "stopped"}.get(operation, "synced")
        result = self.event(event)
        require(result.get("id") == self.sequence, "Linux command reply has the wrong ID")
        return result


def run(arguments):
    output = arguments.output.resolve()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    plan = json.loads(arguments.plan.read_text())
    require(plan.get("version") == 1, "Unsupported Linux harness plan")
    viewer_pin = plan["viewerPin"].lower()
    require(re.fullmatch(r"[0-9a-f]{64}", viewer_pin), "Invalid Linux public certificate pin")
    remote_socket = plan["remoteSocket"]
    require(isinstance(remote_socket, str) and remote_socket.startswith("/")
            and len(remote_socket.encode()) < 100 and ":" not in remote_socket,
            "Invalid dedicated remote Unix socket path")
    require(plan["runArgv"] and plan["cleanupArgv"]
            and all(isinstance(value, str) and value for value in plan["runArgv"] + plan["cleanupArgv"]),
            "Plan must contain explicit Linux run and cleanup argument lists")
    for option, token in (("--window-id", "{windowId}"), ("--peer-pin", "{macPin}")):
        argv = plan["runArgv"]
        require(argv.count(option) == 1 and argv.count(token) == 1
                and argv.index(option) + 1 < len(argv) and argv[argv.index(option) + 1] == token
                and not any(value.startswith(option + "=") for value in argv),
                "Linux plan must select this run's exact owned source and Mac identity: " + option)
    # An ambient ControlPath could attach our forward to an unrelated persistent
    # master. Use explicit configuration and a fresh connection for each owner.
    ssh = ["ssh", "-F", "/dev/null", "-T", "-i", str(arguments.ssh_key), "-o", "BatchMode=yes",
           "-o", "StrictHostKeyChecking=yes", "-o", "ConnectTimeout=8",
           "-o", "ServerAliveInterval=5", "-o", "ServerAliveCountMax=2"]
    environment = os.environ.copy()
    for name in ("DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "QT_PLUGIN_PATH", "QML2_IMPORT_PATH"):
        environment.pop(name, None)
    environment["QT_QPA_PLATFORM"] = "cocoa"
    host, source, tunnel, linux = None, None, None, None
    socket_identity = None
    streams = []
    report = {"version": 1, "status": "failed", "scope": "actual-two-machine-synthetic-input",
              "physicalInputTested": False, "holdingDisplayTested": False,
              "planSha256": hashlib.sha256(arguments.plan.read_bytes()).hexdigest()}
    source_file = output / "source-events.json"
    journal = output / "restore.json"
    title = "Deskflow cross-machine smoke " + uuid.uuid4().hex

    def log(name):
        stream = (output / name).open("xb")
        streams.append(stream)
        return stream

    def command(argv, timeout=15, env=None):
        result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, env=env)
        require(result.returncode == 0,
                f"Command exited {result.returncode}: {shlex.join(argv)}\n"
                + (result.stderr or result.stdout)[-4000:])
        return result

    def remote_command(argv, timeout=15):
        return command([*ssh, arguments.ssh_target, shlex.join(argv)], timeout=timeout)

    def receipt():
        require(source is not None and source.poll() is None, "Own Mac source exited early")
        if not source_file.exists():
            return None
        require(source_file.stat().st_size <= 131072, "Own source receipt exceeded bound")
        data = json.loads(source_file.read_text())
        require(data.get("pid") == source.pid, "Own source receipt PID mismatch")
        # Recorder publishes one empty-window record before QWidget is created.
        # Only startup readiness may wait through that owned, unready record.
        if "source" not in report and not data.get("ready") and not data.get("windowId"):
            return None
        require(data.get("title") == title, "Own source receipt title mismatch")
        return data

    def native_after(baseline, types, x=None, y=None):
        data = receipt()
        events = data.get("nativeEvents", [])[len(baseline.get("nativeEvents", [])):]
        found = []
        after = -1
        for event_type in types:
            matches = [(index, event) for index, event in enumerate(events) if index > after
                       and event.get("type") == event_type
                       and event.get("windowNumber") == report["source"]["windowId"]
                       and event.get("windowResolved") is True
                       and event.get("synthetic") is True and event.get("sourcePid") == host.pid
                       and event.get("tag") == "445357494e503031"
                       and (x is None or abs(event["x"] - x) < 0.05)
                       and (y is None or abs(event["y"] - y) < 0.05)]
            if not matches:
                return None
            after, event = matches[0]
            found.append(event)
        return {"native": found, "receipt": data}

    try:
        progress("checking Mac permissions and SSH")
        probe = json.loads(command([str(arguments.host), "--probe"], env=environment).stdout)
        require(probe.get("screenCapture") and probe.get("accessibility") and probe.get("targetedPointer"),
                "Mac host lacks native capture/input capability; no permission prompt was requested")
        report["permissions"] = probe
        report["remoteHostname"] = remote_command(["hostname"]).stdout.strip()
        remote_command(["test", "!", "-e", remote_socket])
        identity = output / "host-identity"
        mac_pin = command([str(arguments.host), "--init-identity", "--identity", str(identity)],
                          env=environment).stdout.strip().lower()
        require(re.fullmatch(r"[0-9a-f]{64}", mac_pin), "Invalid Mac public certificate pin")
        report["pins"] = {"mac": mac_pin, "ubuntu": viewer_pin}
        report["executableHashes"] = {
            name: hashlib.sha256(path.read_bytes()).hexdigest()
            for name, path in (("host", arguments.host), ("source", arguments.source),
                               ("coordinator", Path(__file__).resolve()))
        }
        source_log = log("source.log")
        source = subprocess.Popen([str(arguments.source), title, str(source_file)], env=environment,
                                  stdout=source_log, stderr=source_log, start_new_session=True)

        def own_window():
            data = receipt()
            return data if data and data.get("ready") is True and data.get("windowId", 0) > 0 and data.get("frame") else None

        initial = wait_for(own_window, "own source native window")
        report["source"] = {key: initial[key] for key in ("pid", "windowId", "title", "frame", "originalFrame")}
        require(initial["frame"] == initial["originalFrame"], "Source frame is not stable at startup")
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        host_log = log("host.log")
        host = subprocess.Popen([str(arguments.host), "--enable-seamless", "--headless", "--listen", "127.0.0.1",
                                 "--port", str(port), "--identity", str(identity), "--peer-pin", viewer_pin,
                                 "--journal", str(journal), "--quit-after", "180"],
                                env=environment, stdout=host_log, stderr=host_log, start_new_session=True)

        def listening():
            require(host.poll() is None, "Mac host exited before listening")
            return f"Listening on 127.0.0.1:{port}" in (output / "host.log").read_text()

        wait_for(listening, "Mac loopback listener")
        tunnel_log = log("tunnel.log")
        tunnel = subprocess.Popen([*ssh, "-N", "-o", "ExitOnForwardFailure=yes", "-o", "StreamLocalBindUnlink=no",
                                   "-R", f"{remote_socket}:127.0.0.1:{port}", arguments.ssh_target],
                                  stdout=tunnel_log, stderr=tunnel_log, start_new_session=True)

        def forwarded():
            require(tunnel.poll() is None, "SSH reverse-forward exited")
            return subprocess.run([*ssh, arguments.ssh_target, shlex.join(["test", "-S", remote_socket])],
                                  capture_output=True, timeout=10).returncode == 0

        wait_for(forwarded, "owned remote Unix forwarding socket")
        socket_probe = (
            "import json,os,stat,sys; s=os.lstat(sys.argv[1]); "
            "assert stat.S_ISSOCK(s.st_mode) and s.st_uid==os.getuid(); "
            "print(json.dumps(dict(device=s.st_dev,inode=s.st_ino,uid=s.st_uid)))"
        )
        socket_identity = json.loads(remote_command(["python3", "-c", socket_probe, remote_socket]).stdout)
        report["forwardSocket"] = socket_identity
        replacements = {"{macPin}": mac_pin, "{windowId}": str(initial["windowId"])}
        run_argv = [replacements.get(value, value) for value in plan["runArgv"]]
        linux_log = log("linux-stderr.log")
        linux = subprocess.Popen([*ssh, arguments.ssh_target, shlex.join(run_argv)], stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=linux_log, start_new_session=True)
        transcript = (output / "linux-control.jsonl").open("x", buffering=1)
        streams.append(transcript)
        driver = Remote(linux, transcript)
        report["linuxReady"] = driver.event("ready")
        require(report["linuxReady"].get("windowId") == initial["windowId"]
                and report["linuxReady"].get("viewerPin", "").lower() == viewer_pin,
                "Linux harness selected a different source window or TLS identity")
        progress("Ubuntu viewer connected; waiting for actual Mac frame")
        prepared = driver.command("prepare")
        snapshot = prepared["snapshot"]
        paint = snapshot["paint"]
        require(paint["peerFingerprint"].lower() == mac_pin, "Paint is not bound to this Mac TLS identity")
        require(paint["imageWidth"] > 0 and paint["imageHeight"] > 0, "No decoded native Mac frame")
        require(abs(paint["sourceWidth"] - initial["frame"]["width"]) < 0.05
                and abs(paint["sourceHeight"] - initial["frame"]["height"]) < 0.05,
                "Paint source dimensions do not match the owned Mac window")
        report["initialPaint"] = snapshot
        active_journal = json.loads(journal.read_text())
        require(active_journal.get("version") == 1 and len(active_journal.get("windows", [])) == 1
                and active_journal["windows"][0].get("windowId") == initial["windowId"]
                and active_journal["windows"][0].get("pid") == source.pid,
                "Live export is not journaled as exactly our owned Mac source")
        report["activeJournal"] = active_journal

        def focused():
            data = receipt()
            return data.get("nativeActive") and data.get("nativeKeyWindow") == initial["windowId"]

        wait_for(focused, "own Mac source native focus")
        origin_x = snapshot["desktopX"] - snapshot["surfaceX"]
        origin_y = snapshot["desktopY"] - snapshot["surfaceY"]
        report["motions"] = []
        for fraction_x, fraction_y in ((0.24, 0.28), (0.42, 0.43), (0.63, 0.62)):
            before = receipt()
            target_x = origin_x + paint["contentX"] + (paint["videoX"] + paint["videoWidth"] * fraction_x) * paint["logicalScale"]
            target_y = origin_y + paint["contentY"] + (paint["videoY"] + paint["videoHeight"] * fraction_y) * paint["logicalScale"]
            driver.command("move", x=target_x, y=target_y)
            observed = driver.command("observe")
            require(observed.get("live") is True, "Immediate actual viewer mapping was not live")
            current = observed["snapshot"]
            require(abs(current["desktopX"] - target_x) <= 1 / 256
                    and abs(current["desktopY"] - target_y) <= 1 / 256,
                    "Native pointer observation did not match requested desktop coordinates")
            require(current["window"] == snapshot["window"] and current["surface"] == snapshot["surface"]
                    and current["paint"] == paint, "Measured viewer/surface/paint binding changed")
            for key, fraction, dimension, span in (
                    ("sourceX", fraction_x, "sourceWidth", "videoWidth"),
                    ("sourceY", fraction_y, "sourceHeight", "videoHeight")):
                tolerance = 1 / 256 / paint["logicalScale"] * paint[dimension] / paint[span] + 1e-6
                require(abs(current[key] - fraction * paint[dimension]) <= tolerance,
                        "Mapped source point differs from the independently requested fractional position")
            x, y = current["sourceX"], initial["frame"]["height"] - current["sourceY"]
            delivered = wait_for(lambda: native_after(before, [5], x, y), "exact Mac native pointer receipt", timeout=5)
            report["motions"].append({"observation": current, "received": delivered["native"]})
        progress("Mac capture and three actual cross-machine pointer positions verified")
        # Qt Widgets dispatch rounds wheel position via QPoint; the first
        # mouse receiver preserves QPointF (Qt 6.10.2 qapplication.cpp).
        # Use an exact logical point (also integral at both fixture scales)
        # for paired button/wheel/key checks, retaining the fractional motion
        # assertions above as independent coordinate evidence.
        logical_x = math.floor((paint["videoX"] + paint["videoWidth"] / 2) / 4) * 4
        logical_y = math.floor((paint["videoY"] + paint["videoHeight"] / 2) / 4) * 4
        before = receipt()
        driver.command("move", x=origin_x + paint["contentX"] + logical_x * paint["logicalScale"],
                       y=origin_y + paint["contentY"] + logical_y * paint["logicalScale"])
        control = driver.command("observe")
        require(control.get("live") is True, "Exact control point did not map to the actual viewer")
        x = control["snapshot"]["sourceX"]
        y = initial["frame"]["height"] - control["snapshot"]["sourceY"]
        moved = wait_for(lambda: native_after(before, [5], x, y), "exact Mac native control point", timeout=5)
        report["controlPoint"] = {"observation": control["snapshot"], "native": moved["native"]}
        before = receipt()
        driver.command("click")
        clicked = wait_for(lambda: native_after(before, [1, 2], x, y), "Mac native click down/up", timeout=5)
        require(clicked["receipt"]["mouseDown"] > before["mouseDown"]
                and clicked["receipt"]["mouseUp"] > before["mouseUp"], "Qt did not receive the native click")
        report["click"] = clicked["native"]
        before = receipt()
        driver.command("scroll", dx=0, dy=120)
        scrolled = wait_for(lambda: native_after(before, [22], x, y), "Mac native scroll", timeout=5)
        require(scrolled["receipt"]["scroll"] > before["scroll"]
                and math.isfinite(scrolled["native"][0]["dy"]) and scrolled["native"][0]["dy"] == -40
                and scrolled["native"][0]["dx"] == 0,
                "Qt did not receive the expected native scroll direction and magnitude")
        report["scroll"] = scrolled["native"]
        before = receipt()
        driver.command("key", code=66)

        def key_received():
            data = receipt()
            events = data.get("events", [])[len(before.get("events", [])):]
            return data if all(any(event.get("type") == kind and event.get("code") == 16777271
                                   for event in events) for kind in (6, 7)) else None

        keyed = wait_for(key_received, "Mac Qt F8 down/up", timeout=5)
        native_keys = wait_for(lambda: native_after(before, [10, 11]), "tagged Mac native F8 down/up", timeout=5)
        require(all(event.get("keyCode") == 100 for event in native_keys["native"]),
                "Mac native key receipts are not F8")
        require(keyed["heldKeys"] == 0 and keyed["heldButtons"] == 0, "Input remains held after paired events")
        report["keyboard"] = {"qtCode": 16777271, "linuxCode": 66, "downAndUp": True,
                              "native": native_keys["native"]}
        report["linuxStop"] = driver.command("stop")
        require(linux.wait(timeout=12) == 0, "Linux harness did not exit successfully")

        def restored_on_disconnect():
            require(host.poll() is None, "Mac host exited before disconnect restoration was observed")
            data = receipt()
            restored_journal = json.loads(journal.read_text())
            return data if data["frame"] == initial["frame"] and data["heldKeys"] == 0 and data["heldButtons"] == 0 \
                and restored_journal == {"version": 1, "windows": []} else None

        report["finalReceipt"] = wait_for(restored_on_disconnect, "disconnect restoration with the Mac host still alive")
        report["disconnectRestoredWithoutRecovery"] = True
        report["macHostExit"] = stop(host)
        require(report["macHostExit"] == 0, "Mac host did not exit cleanly")
        # This is an additional empty-journal check, after disconnect restoration
        # was independently observed. It cannot repair a passing assertion.
        command([str(arguments.host), "--recover", "--journal", str(journal)], env=environment)
        report["recoveryExit"] = 0
        report["status"] = "passed"
        progress("Keyboard, click, scroll, disconnect and source restoration verified")
    except Exception as error:
        report["failure"] = str(error)
    finally:
        # A termination request becomes an exception in main, so this cleanup
        # runs. A second request must not interrupt the bounded restoration.
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        cleanup_errors = []
        if linux is not None and linux.stdin:
            try:
                linux.stdin.close()
            except OSError as error:
                cleanup_errors.append("Linux stdin: " + str(error))
        try:
            report["linuxExit"] = stop(linux)
        except Exception as error:
            cleanup_errors.append("Linux controller: " + str(error))
        for name, process in (("host", host), ("tunnel", tunnel)):
            try:
                report[name + "Exit"] = stop(process)
            except Exception as error:
                cleanup_errors.append(name + ": " + str(error))
        if socket_identity is not None:
            try:
                socket_cleanup = (
                    "import json,os,stat,sys; p=sys.argv[1]; expected=json.loads(sys.argv[2]); s=os.lstat(p); "
                    "assert stat.S_ISSOCK(s.st_mode) and s.st_uid==os.getuid(); "
                    "assert dict(device=s.st_dev,inode=s.st_ino,uid=s.st_uid)==expected; "
                    "os.unlink(p); assert not os.path.lexists(p); print('removed owned socket')"
                )
                remote_command(["python3", "-c", socket_cleanup, remote_socket, json.dumps(socket_identity)])
                report["ownedSocketRemoved"] = True
            except Exception as error:
                cleanup_errors.append("Owned forwarding socket: " + str(error))
        # The plan names only this prepared disposable container/run, never an
        # ordinary client. Stop the tunnel before the token-checked cleanup.
        try:
            report["remoteCleanupExit"] = remote_command(plan["cleanupArgv"], timeout=35).returncode
        except Exception as error:
            report["remoteCleanupError"] = str(error)
            cleanup_errors.append("Remote cleanup: " + str(error))
        try:
            if source is not None and source.poll() is None and source_file.exists():
                report["cleanupReceipt"] = receipt()
        except Exception as error:
            cleanup_errors.append("Source receipt: " + str(error))
        try:
            report["sourceExit"] = stop(source)
        except Exception as error:
            cleanup_errors.append("Source: " + str(error))
        for stream in streams:
            stream.close()
        report["localProcessesStopped"] = all(process is None or process.poll() is not None
                                             for process in (linux, host, tunnel, source))
        if cleanup_errors or not report["localProcessesStopped"]:
            report["status"] = "failed"
            report["cleanupErrors"] = cleanup_errors
        (output / "result.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"status": report["status"], "result": str(output / "result.json"),
                      "failure": report.get("failure")}), flush=True)
    return 0 if report["status"] == "passed" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=lambda value: Path(value).resolve(strict=True), required=True)
    parser.add_argument("--source", type=lambda value: Path(value).resolve(strict=True), required=True)
    parser.add_argument("--ssh-target", required=True)
    parser.add_argument("--ssh-key", type=lambda value: Path(value).resolve(strict=True), required=True)
    parser.add_argument("--plan", type=lambda value: Path(value).resolve(strict=True), required=True)
    parser.add_argument("--output", type=Path, required=True)
    def terminated(signum, _frame):
        raise RuntimeError(f"Cross-machine coordinator interrupted by signal {signum}")

    signal.signal(signal.SIGTERM, terminated)
    signal.signal(signal.SIGINT, terminated)
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
