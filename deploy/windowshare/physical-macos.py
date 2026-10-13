#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Record a user-started Mac -> Ubuntu -> Mac physical input session.

The disposable source owns the visible Start/Stop controls. This coordinator
never synthesizes input. Successful teardown records a session; human and
receipt review are still required to establish physical acceptance.
"""

import argparse
import configparser
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import selectors
import shlex
import signal
import socket
import stat
import subprocess
import sys
import threading
import time

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location(
    "crossmachine_mac", Path(__file__).with_name("crossmachine-macos-smoke.py"))
CROSS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CROSS)
require = CROSS.require
MAX_SEQUENCE = 2**53 - 1


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "Duplicate JSON field")
        result[key] = value
    return result


def read_json_with_identity(path, limit=131072):
    with Path(path).open("rb") as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode), "JSON input must be a regular file")
        raw = stream.read(limit + 1)
        after = os.fstat(stream.fileno())
    require(len(raw) <= limit, "JSON file exceeds its bound")
    def identity(info):
        return (info.st_dev, info.st_ino, info.st_mtime_ns, info.st_ctime_ns, info.st_size)
    require(identity(before) == identity(after), "JSON file changed while being read")
    value = json.loads(raw, object_pairs_hook=unique_object,
                       parse_constant=lambda _: require(False, "Non-finite JSON number"))
    return value, identity(after)


def read_json(path, limit=131072):
    return read_json_with_identity(path, limit)[0]


def atomic_json(path, value):
    temporary = path.with_name(path.name + ".next")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        json.dump(value, stream, allow_nan=False, separators=(",", ":"))
        stream.write("\n")
    temporary.replace(path)


class StatusPublisher:
    """A blocked SSH request cannot leave a stale active UI indefinitely."""

    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()
        self.stopping = threading.Event()
        self.state = "preparing"
        self.deadline = None
        self.was_active = False
        self.error = None
        self.thread = threading.Thread(target=self.loop, daemon=True)
        self.thread.start()

    def set(self, state, seconds=0):
        require(state in ("preparing", "ready", "active", "stopped"), "Invalid manual state")
        require(type(seconds) is int and
                (1 <= seconds <= 120 if state == "active" else seconds == 0),
                "Invalid manual countdown")
        with self.lock:
            require(self.state != "stopped" or state == "stopped", "Manual session already stopped")
            require(not self.was_active or state in ("active", "stopped"), "Cannot rearm an active session")
            deadline = time.monotonic() + seconds if state == "active" else None
            if state == "active":
                self.was_active = True
                if self.deadline is not None:
                    deadline = min(self.deadline, deadline)
            self.state = state
            self.deadline = deadline

    def snapshot(self):
        with self.lock:
            remaining = math.ceil(self.deadline - time.monotonic()) if self.deadline else 0
            if self.state == "active" and remaining <= 0:
                self.state, self.deadline = "stopped", None
            return {"version": 1, "state": self.state,
                    "remainingSeconds": max(0, remaining) if self.state == "active" else 0}

    def loop(self):
        try:
            while True:
                atomic_json(self.path, self.snapshot())
                if self.stopping.wait(0.25):
                    break
        except Exception as error:
            self.error = str(error)

    def close(self):
        self.set("stopped")
        self.stopping.set()
        self.thread.join(2)
        require(not self.thread.is_alive(), "Manual status writer failed to stop")
        require(self.error is None, "Manual status writer failed: " + str(self.error))
        atomic_json(self.path, self.snapshot())


class ReceiptHistory:
    """Merge the source's bounded rings without mistaking a gap for delivery."""

    def __init__(self):
        self.last = {"events": 0, "nativeEvents": 0}
        self.gaps = []
        self.records = 0
        self.bytes = 0
        self.manual_sequence = 0
        self.manual_request = "none"

    def merge(self, data):
        require(not data.get("eventSequenceExhausted") and
                not data.get("nativeEventSequenceExhausted"), "Receipt sequence exhausted")
        manual = data.get("manual")
        require(isinstance(manual, dict) and
                set(manual) == {"state", "request", "requestSequence", "statusValid", "remainingSeconds"} and
                manual.get("request") in ("none", "start", "stop") and
                manual.get("state") in ("preparing", "ready", "active", "stopped") and
                type(manual.get("statusValid")) is bool and
                type(manual.get("remainingSeconds")) is int and 0 <= manual["remainingSeconds"] <= 120,
                "Source does not provide manual controls")
        sequence = manual.get("requestSequence")
        require(type(sequence) is int and self.manual_sequence <= sequence <= 2,
                "Manual request sequence reset or exceeded bound")
        require((sequence == 0 and manual["request"] == "none") or
                (sequence == 1 and manual["request"] in ("start", "stop")) or
                (sequence == 2 and manual["request"] == "stop"), "Invalid manual request sequence")
        require(sequence != self.manual_sequence or manual["request"] == self.manual_request,
                "Manual request changed without a new sequence")
        if sequence > self.manual_sequence:
            require(manual["request"] != "none" and self.manual_request != "stop",
                    "Invalid manual request transition")
        self.manual_sequence, self.manual_request = sequence, manual["request"]
        records = []
        for name, total_name in (("events", "eventCount"), ("nativeEvents", "nativeEventCount")):
            total, events = data.get(total_name), data.get(name)
            require(type(total) is int and self.last[name] <= total <= MAX_SEQUENCE,
                    "Receipt total reset or exceeded bound")
            require(isinstance(events, list) and len(events) <= 128, "Invalid receipt ring")
            previous = None
            for event in events:
                sequence = event.get("sequence") if isinstance(event, dict) else None
                require(type(sequence) is int and 1 <= sequence <= total and
                        (previous is None or sequence == previous + 1), "Malformed receipt sequence")
                previous = sequence
                if sequence <= self.last[name]:
                    continue
                if sequence != self.last[name] + 1:
                    self.gaps.append({"stream": name, "after": self.last[name], "next": sequence})
                row = {"stream": name, "observedMonotonic": time.monotonic(), "event": event}
                line = json.dumps(row, allow_nan=False, separators=(",", ":")) + "\n"
                self.records += 1
                self.bytes += len(line.encode())
                require(self.records <= 100000 and self.bytes <= 40 * 1024 * 1024,
                        "Receipt collection exceeded bound")
                records.append(line)
                self.last[name] = sequence
            require(self.last[name] == total, "Receipt total does not match retained tail")
        return records


class Collector:
    def __init__(self, path, pid, title, output):
        self.path, self.pid, self.title = path, pid, title
        self.history = ReceiptHistory()
        self.latest = None
        self.error = None
        self.publication_identity = None
        self.publication_observed = None
        self.lock = threading.Lock()
        self.stopping = threading.Event()
        self.stream = output.open("x", buffering=1)
        self.thread = threading.Thread(target=self.loop, daemon=True)
        self.thread.start()

    def loop(self):
        try:
            while not self.stopping.is_set():
                try:
                    data, identity = read_json_with_identity(self.path)
                except FileNotFoundError:
                    # The owner may atomically remove/replace a publication
                    # between stat and open during teardown. Absence never
                    # renews its freshness or authorizes another owner, but
                    # it is not a corrupt receipt or a terminal worker fault.
                    self.stopping.wait(0.025)
                    continue
                require(data.get("pid") == self.pid, "Source PID mismatch")
                if data.get("ready") or data.get("windowId"):
                    require(data.get("title") == self.title, "Source title mismatch")
                    with self.lock:
                        for line in self.history.merge(data):
                            self.stream.write(line)
                        self.latest = data
                        if identity != self.publication_identity:
                            self.publication_identity = identity
                            self.publication_observed = time.monotonic()
                self.stopping.wait(0.025)
        except Exception as error:
            self.error = str(error)

    def snapshot(self):
        require(self.error is None, "Receipt collector failed: " + str(self.error))
        with self.lock:
            if self.latest is not None:
                require(self.path.exists() and self.publication_observed is not None and
                        time.monotonic() - self.publication_observed < 2,
                        "Source receipt publication is missing or stale")
            return {"receipt": self.latest, "lastSequences": dict(self.history.last),
                    "gaps": list(self.history.gaps), "records": self.history.records,
                    "publicationObservedMonotonic": self.publication_observed}

    def close(self):
        self.stopping.set()
        self.thread.join(2)
        require(not self.thread.is_alive(), "Receipt collector failed to stop")
        self.stream.close()


class Cancelled(RuntimeError):
    pass


class Driver:
    def __init__(self, process, transcript, check):
        self.process, self.transcript, self.check = process, transcript, check
        self.selector = selectors.DefaultSelector()
        self.selector.register(process.stdout, selectors.EVENT_READ)
        self.buffer = b""
        self.sequence = 0
        self.rows = 0
        self.pending = {}
        self.prepared_seen = False

    def event(self, expected, timeout=30, check=True, request_id=None):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if check:
                self.check()
            if b"\n" not in self.buffer:
                if not self.selector.select(0.05):
                    continue
                data = os.read(self.process.stdout.fileno(), 16384)
                require(data, "Linux controller closed before " + expected)
                self.buffer += data
                require(len(self.buffer) <= 262144, "Linux control reply exceeded bound")
                continue
            line, self.buffer = self.buffer.split(b"\n", 1)
            self.rows += 1
            require(self.rows <= 256, "Too many Linux replies")
            value = json.loads(line, object_pairs_hook=unique_object)
            require(isinstance(value, dict), "Linux reply is not an object")
            self.transcript.write(json.dumps({"received": value}) + "\n")
            self.transcript.flush()
            kind = value.get("event")
            if kind == "prepared":
                require(not self.prepared_seen and "id" not in value, "Repeated Linux preparation")
                self.prepared_seen = True
                if expected == "prepared" and request_id is None:
                    return value
                require(expected == "stopped", "Unexpected Linux preparation reply")
                continue
            sequence = value.get("id")
            require(type(sequence) is int and sequence in self.pending,
                    "Unknown Linux reply ID")
            require(kind == self.pending[sequence], "Unexpected Linux reply: " + str(kind))
            del self.pending[sequence]
            if sequence != request_id:
                # Visible Stop can cancel an in-flight start/status request.
                # Drain its exact late reply before accepting Stop's own ack.
                require(expected == "stopped" and request_id is not None and sequence < request_id,
                        "Unrelated Linux reply")
                continue
            require(kind == expected, "Linux reply kind mismatch")
            return value
        raise RuntimeError("Linux control timed out waiting for " + expected)

    def command(self, operation, check=True):
        self.sequence += 1
        value = {"id": self.sequence, "op": operation}
        self.transcript.write(json.dumps({"sent": value}) + "\n")
        self.transcript.flush()
        self.process.stdin.write((json.dumps(value) + "\n").encode())
        self.process.stdin.flush()
        expected = {"start": "ready", "stop": "stopped"}.get(operation, operation)
        self.pending[self.sequence] = expected
        reply = self.event(expected, timeout=85 if operation == "start" else 35,
                           check=check, request_id=self.sequence)
        require(reply.get("id") == self.sequence, "Linux reply ID mismatch")
        return reply


def stop_owned(process, name, output, report):
    if process is None:
        return None
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            report[name + "TerminationTimedOut"] = True
            if name == "host" and sys.platform == "darwin":
                try:
                    sampled = subprocess.run(
                        ["/usr/bin/sample", str(process.pid), "1", "1", "-file",
                         str(output / "host-shutdown-sample.txt")],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=4)
                    report["hostSampleExit"] = sampled.returncode
                except subprocess.TimeoutExpired:
                    report["hostSampleTimedOut"] = True
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=3)
    return process.returncode


def private_path(value, base):
    path = Path(value)
    require(path.is_absolute() and path.resolve().is_relative_to(base) and path.resolve() != base,
            "Test settings must remain inside the prepared private directory")
    return path


def validate_plans(mac, plan, base):
    require(mac.get("version") == 1 and mac.get("preparedOnly") is True, "Wrong Mac preparation plan")
    require(plan.get("version") == 2 and plan.get("controlProtocol") == 2, "Use a fresh version 2 Linux plan")
    require(mac.get("originalInputEnabled") is False and mac.get("holdingDisplayEnabled") is False and
            plan.get("syntheticStartupInput") is False, "Wrong test route mode")
    for field in ("macCorePin", "macViewerPin", "corePin", "viewerPin"):
        require(isinstance(plan.get(field), str) and re.fullmatch("[0-9a-f]{64}", plan[field]),
                "Invalid public peer pin")
    require(mac["coreFingerprint"] == plan["macCorePin"] and
            mac["hostFingerprint"] == plan["macViewerPin"], "Prepared Mac identity mismatch")
    require(mac["linuxScreen"] == plan["linuxScreen"], "Linux screen mismatch")
    require(plan["macLoopbackPorts"] == {"core": mac["corePort"], "viewer": mac["hostPort"]},
            "Loopback port mismatch")
    require(set(plan["remoteSockets"]) == {"core", "viewer"} and
            len(set(plan["remoteSockets"].values())) == 2, "Invalid remote forwarding sockets")
    for key in ("runArgv", "cleanupArgv"):
        require(isinstance(plan.get(key), list) and plan[key] and
                all(isinstance(arg, str) and arg and "\0" not in arg for arg in plan[key]),
                "Invalid Linux command arguments")
    allowed = {"XDG_CONFIG_HOME", "XDG_CONFIG_DIRS", "XDG_STATE_HOME", "XDG_CACHE_HOME",
               "XDG_DATA_HOME", "XDG_DATA_DIRS", "XDG_RUNTIME_DIR", "TMPDIR"}
    require(set(mac["environmentOverrides"]) == allowed, "Unexpected test environment override")
    for path in mac["environmentOverrides"].values():
        private_path(path, base)
    require(mac["coreArgv"][1:4] == ["server", "--new-instance", "--settings"] and
            len(mac["coreArgv"]) == 5, "Unexpected Mac core invocation")
    settings = private_path(mac["coreArgv"][4], base)
    config = configparser.ConfigParser(interpolation=None)
    require(config.read(settings), "Cannot read private Mac core settings")
    for section, key, expected in (
            ("core", "interface", "127.0.0.1"), ("core", "computerName", mac["coreScreen"]),
            ("core", "port", str(mac["corePort"])), ("core", "enableEnterCommand", "false"),
            ("core", "enableExitCommand", "false"), ("server", "externalConfig", "true"),
            ("server", "enableClipboard", "false"), ("server", "relativeMouseMoves", "false"),
            ("security", "tlsEnabled", "true"), ("security", "checkPeerFingerprints", "true"),
            ("log", "level", "DEBUG")):
        require(config.get(section, key, fallback="") == expected, "Unexpected isolated Mac setting: " + key)
    server_config = private_path(config["server"]["externalConfigFile"], base)
    lines = [line.strip() for line in server_config.read_text().splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    require(lines == ["section: options",
                      "keystroke(control+alt+shift+Right) = switchToScreen(" + mac["linuxScreen"] + ")",
                      "keystroke(control+alt+shift+Left) = switchToScreen(" + mac["coreScreen"] + ")", "end"],
            "Test must use only the two explicit Option hotkeys and no screen-edge links")
    private_path(config["security"]["certificate"], base)
    private_path(mac["coreIpcExpectedPath"], base)
    for screen in (mac["coreScreen"], mac["linuxScreen"]):
        for key, value in (("Shift", "shift"), ("Ctrl", "ctrl"), ("Alt", "alt"),
                           ("AltGr", "altgr"), ("Meta", "meta"), ("Super", "super")):
            require(config.get("computer_" + screen, "modifier" + key, fallback="") == value,
                    "Physical test requires identity modifier maps")


def run(arguments):
    os.umask(0o077)
    base = arguments.mac_plan.parent.resolve(strict=True)
    mac, plan = read_json(arguments.mac_plan), read_json(arguments.linux_plan)
    validate_plans(mac, plan, base)
    output = arguments.output.resolve()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    report = {"version": 1, "status": "failed", "physicalAcceptance": "not-assessed",
              "syntheticInputSent": False, "holdingDisplayTested": False,
              "twoPhysicalMonitorsTested": False, "planSha256": digest(arguments.linux_plan),
              "macPlanSha256": digest(arguments.mac_plan)}
    atomic_json(output / "linux-plan.json", plan)
    atomic_json(output / "mac-plan.json", mac)
    ssh = ["ssh", "-F", "/dev/null", "-T", "-i", str(arguments.ssh_key), "-o", "BatchMode=yes",
           "-o", "StrictHostKeyChecking=yes", "-o", "ConnectTimeout=8",
           "-o", "ServerAliveInterval=5", "-o", "ServerAliveCountMax=2"]
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith("DYLD_") and key not in
                   ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH")}
    environment.update(mac["environmentOverrides"])
    environment["QT_QPA_PLATFORM"] = "cocoa"
    source_path, journal = output / "source.json", output / "restore.json"
    source_executable = arguments.source or Path(mac["sourceExecutable"])
    title = "Deskflow physical input test"
    streams, owned, socket_identities, cleanup_errors = [], {}, {}, []
    collector = publisher = driver = None
    linux_stopped = False
    started = False

    def emit(value):
        print(json.dumps(value, allow_nan=False), flush=True)

    def command(argv, timeout=15, env=None):
        result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, env=env)
        require(result.returncode == 0, "Command failed: " + shlex.join(argv) +
                "\n" + (result.stderr or result.stdout)[-2500:])
        return result.stdout

    def remote(argv, timeout=15):
        return command([*ssh, arguments.ssh_target, shlex.join(argv)], timeout)

    def launch(name, argv, pipe=False, env=None):
        log = (output / (name + ".log")).open("xb")
        streams.append(log)
        process = subprocess.Popen(argv, env=env, stdin=subprocess.PIPE if pipe else subprocess.DEVNULL,
                                   stdout=subprocess.PIPE if pipe else log, stderr=log, start_new_session=True)
        owned[name] = process
        return process

    def check(allow_stop=False):
        for name, process in owned.items():
            if name == "linux" and linux_stopped:
                continue
            require(process.poll() is None, name + " exited early: " + str(process.returncode))
        if publisher:
            require(publisher.error is None, "Manual status publisher failed")
        if collector:
            state = collector.snapshot()
            if not allow_stop and state["receipt"] and state["receipt"]["manual"]["request"] == "stop":
                raise Cancelled("Source requested Stop in the test window")
            return state["receipt"]
        return None

    def wait(predicate, description, seconds=10, allow_stop=False):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            check(allow_stop)
            value = predicate()
            if value:
                return value
            time.sleep(0.025)
        raise RuntimeError("Timed out waiting for " + description)

    def no_other_primary():
        rows = command(["ps", "-axo", "pid=,comm="]).splitlines()
        require(not any(row.split() and row.split()[-1].endswith("/deskflow-core") for row in rows),
                "Another Mac primary is running; overlapping capture refused")

    try:
        no_other_primary()
        report["ordinaryMacConfigBefore"] = {str(path): digest(path) for path in arguments.ordinary_mac_config}
        report["remoteHostname"] = remote(["hostname"]).strip()
        host_executable = mac["hostArgv"][0]
        report["permissions"] = json.loads(command([host_executable, "--probe"], env=environment))
        require(all(report["permissions"].get(key) is True
                    for key in ("accessibility", "screenCapture", "targetedPointer")),
                "Mac permission or native input capability is unavailable")
        report["executableHashes"] = {name: digest(path) for name, path in (
            ("core", mac["coreArgv"][0]), ("host", host_executable),
            ("source", source_executable), ("coordinator", Path(__file__)))}
        for port in (mac["corePort"], mac["hostPort"]):
            with socket.socket() as reservation:
                reservation.bind(("127.0.0.1", port))
        for endpoint in plan["remoteSockets"].values():
            remote(["test", "!", "-e", endpoint])
        publisher = StatusPublisher(output / "manual-status.json")
        source = launch("source", [str(source_executable), title, str(source_path),
                                  "--manual-status", str(publisher.path)], env=environment)
        collector = Collector(source_path, source.pid, title, output / "receipts.jsonl")
        def own_window():
            data = collector.snapshot()["receipt"]
            return data if data and data.get("ready") is True and data.get("windowId", 0) > 0 and \
                data.get("frame") and data["frame"] == data.get("originalFrame") else None

        initial = wait(own_window, "stable visible source readiness")
        report["initialReceipt"] = initial
        argv = [str(initial["windowId"]) if arg == "{windowId}" else arg for arg in plan["runArgv"]]
        require(all("{" not in arg and "}" not in arg for arg in argv), "Unresolved Linux plan argument")
        linux = launch("linux", [*ssh, arguments.ssh_target, shlex.join(argv)], pipe=True)
        transcript = (output / "linux-control.jsonl").open("x", buffering=1)
        streams.append(transcript)
        driver = Driver(linux, transcript, check)
        report["prepared"] = driver.event("prepared", timeout=30)
        publisher.set("ready")
        emit({"event": "waiting-for-start", "title": title, "routingStarted": False})
        wait(lambda: collector.snapshot()["receipt"]["manual"]["request"] == "start",
             "the visible Start button", seconds=300)
        publisher.set("preparing")
        started = True
        no_other_primary()
        trust = private_path(str(Path(mac["environmentOverrides"]["XDG_CONFIG_HOME"]) /
                                 "Deskflow/tls/trusted-clients"), base)
        temporary = trust.with_name(trust.name + ".physical-next")
        with temporary.open("x") as stream:
            stream.write("v2:sha256:" + plan["corePin"] + "\n")
        temporary.replace(trust)
        host_identity = private_path(mac["hostArgv"][mac["hostArgv"].index("--identity") + 1], base)
        host = launch("host", [host_executable, "--enable-seamless", "--headless", "--listen", "127.0.0.1",
                              "--port", str(mac["hostPort"]), "--identity", str(host_identity),
                              "--peer-pin", plan["viewerPin"], "--journal", str(journal),
                              "--quit-after", "300"], env=environment)
        wait(lambda: "Listening on 127.0.0.1:" + str(mac["hostPort"]) in
             (output / "host.log").read_text(), "Mac host listener")
        launch("core", mac["coreArgv"], env=environment)

        def core_ready():
            text = (output / "core.log").read_text()
            return Path(mac["coreIpcExpectedPath"]).is_socket() and all(
                re.search(r"registered hotkey .*macKey=" + str(key) + r" macMask=1a00\)", text)
                for key in (123, 124))

        wait(core_ready, "fixed Option hotkeys and private Mac core IPC")
        tunnel_argv = [*ssh, "-N", "-o", "ExitOnForwardFailure=yes", "-o", "StreamLocalBindUnlink=no"]
        for name, key in (("core", "corePort"), ("viewer", "hostPort")):
            tunnel_argv += ["-R", plan["remoteSockets"][name] + ":127.0.0.1:" + str(mac[key])]
        launch("tunnel", [*tunnel_argv, arguments.ssh_target])
        probe = ("import json,os,stat,sys; out={};\nfor p in sys.argv[1:]:\n"
                 " s=os.lstat(p); assert stat.S_ISSOCK(s.st_mode) and s.st_uid==os.getuid(); "
                 "out[p]=dict(device=s.st_dev,inode=s.st_ino,uid=s.st_uid)\nprint(json.dumps(out))")

        def forwards():
            try:
                return json.loads(remote(["python3", "-c", probe, *plan["remoteSockets"].values()], 8))
            except RuntimeError:
                return None

        socket_identities = wait(forwards, "owned forwarding sockets", 15)
        report["socketIdentities"] = socket_identities
        report["linuxReady"] = driver.command("start")
        require(report["linuxReady"]["windowId"] == initial["windowId"] and
                report["linuxReady"]["originalInputAuthorityGranted"] is False and
                report["linuxReady"]["maximumRemainingSeconds"] >= 135, "Wrong or expired physical route")
        report["baseline"] = {"source": collector.snapshot(), "linux": driver.command("baseline")}
        publisher.set("active", 120)
        emit({"event": "active", "secondsRemaining": 120, "title": title})
        deadline, next_status = time.monotonic() + 120, time.monotonic() + 5
        while time.monotonic() < deadline:
            receipt = check(allow_stop=True)
            if receipt["manual"]["request"] == "stop":
                report["sourceRequestedStop"] = True
                break
            if time.monotonic() >= next_status:
                report["lastLinuxStatus"] = driver.command("status")
                next_status = time.monotonic() + 5
            time.sleep(0.025)
        else:
            report["autoStoppedAtDeadline"] = True
        publisher.set("stopped")
        report["beforeStop"] = collector.snapshot()
        report["linuxStop"] = driver.command("stop", check=False)
        linux_stopped = True
        require(linux.wait(timeout=15) == 0, "Linux controller did not exit successfully")
        require(report["linuxStop"].get("lifecycleCompleted") is True and
                report["linuxStop"].get("cleanup", {}).get("ordinaryPreserved") is True and
                report["linuxStop"]["cleanup"].get("ownedContainerStopped") is True and
                report["linuxStop"]["cleanup"].get("containerExit") == 0,
                "Linux lifecycle or ordinary installation audit failed")
        disconnected_at = time.monotonic()

        def restored_on_disconnect():
            current = collector.snapshot()
            data, observed = current["receipt"], current["publicationObservedMonotonic"]
            if observed is None or observed <= disconnected_at:
                return None
            return current if data["heldKeys"] == 0 and data["heldButtons"] == 0 and \
                data["frame"] == initial["frame"] and journal.exists() and \
                read_json(journal) == {"version": 1, "windows": []} else None

        report["disconnectRestoration"] = wait(
            restored_on_disconnect, "source restoration before host termination", allow_stop=True)
        report["disconnectRestoredWhileHostAlive"] = True
        require(not collector.snapshot()["gaps"], "Input receipt history has gaps")
        report["status"] = "session-recorded-awaiting-human-and-receipt-review"
    except Cancelled as error:
        report["status"], report["failure"] = "cancelled", str(error)
    except Exception as error:
        report["failure"] = str(error)
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        if publisher:
            publisher.set("stopped")
        if driver and not linux_stopped and owned["linux"].poll() is None:
            try:
                report["cleanupLinuxStop"] = driver.command("stop", check=False)
            except Exception as error:
                cleanup_errors.append("Linux stop: " + str(error))
        for name in ("linux", "core", "host", "tunnel"):
            try:
                report[name + "Exit"] = stop_owned(owned.get(name), name, output, report)
                if name == "host" and owned.get(name) and report[name + "Exit"] != 0:
                    cleanup_errors.append("Mac host did not exit gracefully")
                if report.get(name + "TerminationTimedOut"):
                    cleanup_errors.append(name + " exceeded the termination deadline")
            except Exception as error:
                cleanup_errors.append(name + ": " + str(error))
        for endpoint, expected in socket_identities.items():
            try:
                cleanup = ("import json,os,stat,sys; p=sys.argv[1]; expected=json.loads(sys.argv[2]);\n"
                           "if os.path.lexists(p):\n s=os.lstat(p); assert stat.S_ISSOCK(s.st_mode) and "
                           "dict(device=s.st_dev,inode=s.st_ino,uid=s.st_uid)==expected; os.unlink(p)\n"
                           "assert not os.path.lexists(p)")
                remote(["python3", "-c", cleanup, endpoint, json.dumps(expected)])
            except Exception as error:
                cleanup_errors.append("Owned socket: " + str(error))
        try:
            report["remoteCleanup"] = remote(plan["cleanupArgv"], 35)
        except Exception as error:
            cleanup_errors.append("Remote cleanup: " + str(error))
        if collector:
            try:
                after_cleanup = time.monotonic()
                if started:
                    def fresh_source():
                        require(owned["source"].poll() is None, "Source exited before final restoration evidence")
                        current = collector.snapshot()
                        observed = current["publicationObservedMonotonic"]
                        return current if observed is not None and observed > after_cleanup else None
                    CROSS.wait_for(fresh_source, "post-cleanup source publication", timeout=3)
                final = collector.snapshot()
                report["finalSource"] = final
                if started and final["receipt"]:
                    data = final["receipt"]
                    require(not final["gaps"], "Source receipt history has gaps")
                    require(data["heldKeys"] == 0 and data["heldButtons"] == 0 and
                            data["frame"] == report["initialReceipt"]["frame"],
                            "Source input or window frame was not restored")
                    require(not journal.exists() or read_json(journal) == {"version": 1, "windows": []},
                            "Source restore journal remains unresolved")
                    report["sourceRestored"] = True
            except Exception as error:
                cleanup_errors.append("Source restoration: " + str(error))
            try:
                collector.close()
            except Exception as error:
                cleanup_errors.append("Receipt collector cleanup: " + str(error))
        try:
            report["sourceExit"] = stop_owned(owned.get("source"), "source", output, report)
            if report.get("sourceTerminationTimedOut"):
                cleanup_errors.append("Source exceeded the termination deadline")
            report["ordinaryMacConfigAfter"] = {str(path): digest(path) for path in arguments.ordinary_mac_config}
            require(report["ordinaryMacConfigAfter"] == report.get("ordinaryMacConfigBefore"),
                    "Ordinary Mac configuration changed")
        except Exception as error:
            cleanup_errors.append("Mac cleanup: " + str(error))
        if publisher:
            try:
                publisher.close()
            except Exception as error:
                cleanup_errors.append(str(error))
        if driver:
            try:
                driver.selector.close()
            except Exception as error:
                cleanup_errors.append("Control reader cleanup: " + str(error))
        for stream in streams:
            try:
                stream.close()
            except Exception as error:
                cleanup_errors.append("Log cleanup: " + str(error))
        report["localProcessesStopped"] = all(process.poll() is not None for process in owned.values())
        if cleanup_errors or not report["localProcessesStopped"]:
            report["cleanupErrors"], report["status"] = cleanup_errors, "failed"
        atomic_json(output / "result.json", report)
        emit({"event": "finished", "status": report["status"], "result": str(output / "result.json"),
              "physicalAcceptance": "not-assessed", "failure": report.get("failure")})
    return 0 if report["status"].startswith("session-recorded") else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mac-plan", type=lambda value: Path(value).resolve(strict=True), required=True)
    parser.add_argument("--linux-plan", type=lambda value: Path(value).resolve(strict=True), required=True)
    parser.add_argument("--ssh-target", required=True)
    parser.add_argument("--ssh-key", type=lambda value: Path(value).resolve(strict=True), required=True)
    parser.add_argument("--source", type=lambda value: Path(value).resolve(strict=True))
    parser.add_argument("--ordinary-mac-config", action="append", default=[],
                        type=lambda value: Path(value).resolve(strict=True), required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    require(sys.platform == "darwin", "This coordinator must run on the Mac")
    require(not arguments.ssh_target.startswith("-"), "Invalid SSH target")

    def interrupted(signum, _frame):
        raise Cancelled("Coordinator interrupted by signal " + str(signum))

    for signum in (signal.SIGTERM, signal.SIGINT, signal.SIGALRM):
        signal.signal(signum, interrupted)
    signal.alarm(600)
    return run(arguments)


if __name__ == "__main__":
    raise SystemExit(main())
