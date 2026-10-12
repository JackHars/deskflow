#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Check the real Linux core's explicit GNOME-provider startup failure path.

No compositor, provider, portal, or input server is started. A new session bus
has no activation directories, and its traffic is monitored before launching
the core. HOME, settings, IPC, runtime and both D-Bus addresses are private.
XDG_SESSION_TYPE selects EiComputer; Qt itself uses the offscreen plugin and
there is no Wayland socket. A bound, non-listening loopback port prevents any
Deskflow server from supplying input. Only Popen-owned children are signalled.

This checks startup, EventQueue ordering, terminal failure and absence of
portal fallback. It is not a compositor or original-input delivery test.
"""

import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import time


START_SECONDS = 15
STOP_SECONDS = 3
MAX_LOG_BYTES = 4 * 1024 * 1024
PROVIDER = "org.deskflow.Seamless.Gnome50"
TERMINAL = "experimental GNOME session stopped:"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def private_environment(root):
    """No inherited display, bus, loader, Qt-plugin, libei or settings override."""
    environment = {"PATH": "/usr/bin:/bin", "LC_ALL": "C", "QT_QPA_PLATFORM": "offscreen",
                   "XDG_SESSION_TYPE": "wayland", "XDG_CURRENT_DESKTOP": "GNOME",
                   "WAYLAND_DISPLAY": "unconnected-private-wayland", "NO_AT_BRIDGE": "1"}
    for name, directory in {
        "HOME": "home", "TMPDIR": "tmp", "XDG_RUNTIME_DIR": "runtime",
        "XDG_CONFIG_HOME": "config", "XDG_CONFIG_DIRS": "config-dirs",
        "XDG_CACHE_HOME": "cache", "XDG_DATA_HOME": "data",
        "XDG_DATA_DIRS": "data-dirs", "XDG_STATE_HOME": "state",
    }.items():
        path = root / directory
        path.mkdir(mode=0o700)
        environment[name] = str(path)
    environment["DBUS_SESSION_BUS_ADDRESS"] = "unix:path=" + str(root / "session.sock")
    environment["DBUS_SYSTEM_BUS_ADDRESS"] = "unix:path=" + str(root / "no-system-bus.sock")
    return environment


def log_text(path, *, tail=False):
    if not path.exists():
        return ""
    size = path.stat().st_size
    require(tail or size <= MAX_LOG_BYTES, f"{path.name} exceeded the bounded smoke log limit")
    with path.open("rb") as stream:
        if tail:
            stream.seek(max(0, size - 8000))
        return stream.read(8000 if tail else MAX_LOG_BYTES).decode("utf-8", errors="replace")


def terminate_owned(process):
    """Never signal a bus name, process group, pidfile or discovered process."""
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=STOP_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=STOP_SECONDS)


def wait_until(predicate, message, children, logs, timeout=3):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for label, child in children:
            require(child.poll() is None, f"Owned {label} exited unexpectedly ({child.returncode})")
        for path in logs:
            log_text(path)
        if predicate():
            return
        time.sleep(0.01)
    raise RuntimeError(message)


def method_calls(output):
    """Keep the arguments with each monitor header; do not match unrelated signals."""
    records = []
    current = []
    for line in output.splitlines():
        if line.startswith(("method call ", "method return ", "signal ", "error ")):
            if current and current[0].startswith("method call "):
                records.append("\n".join(current))
            current = [line]
        elif current:
            current.append(line)
    if current and current[0].startswith("method call "):
        records.append("\n".join(current))
    return records


def monitor_marker(send, environment, monitor_path, children, logs):
    """Prove monitoring is active, and later drain it beyond the core's exit."""
    before = sum("member=GetId" in record for record in method_calls(log_text(monitor_path)))
    result = subprocess.run(
        [send, "--bus=" + environment["DBUS_SESSION_BUS_ADDRESS"], "--type=method_call", "--print-reply",
         "--reply-timeout=2000", "--dest=org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus.GetId"],
        env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        close_fds=True, timeout=3, check=False,
    )
    require(result.returncode == 0, "Private bus marker request failed")
    wait_until(
        lambda: sum("member=GetId" in record for record in method_calls(log_text(monitor_path))) > before,
        "Private bus monitor did not observe the marker request", children, logs,
    )


def verify_result(output, traffic, returncode, port, viewer_started):
    require(returncode == 1, f"Core must exit normally with s_exitFailed=1, got {returncode}")
    require(not viewer_started, "Provider absence unexpectedly launched the viewer fixture")
    for expected in ("detected wayland platform", "started client", "event queue is ready",
                     f"127.0.0.1:{port}", "stopped client", "core stopped with error code: 1"):
        require(expected in output, f"Core omitted expected lifecycle evidence: {expected}")
    require(output.count(TERMINAL) == 1, "GNOME provider failure must be terminal and reported exactly once")
    require(output.index("event queue is ready") < output.index(TERMINAL) < output.index("stopped client"),
            "GNOME provider failure was delivered before EventQueue readiness or after teardown")
    require("NameHasNoOwner" in output and PROVIDER in output, "Failure was not caused by the absent provider")
    require("connected experimental GNOME input session" not in output, "Absent provider became connected")
    require("core stopped successfully" not in output, "Provider failure became a successful core exit")
    for forbidden in ("setting up remote desktop session", "portal remote desktop", "portal input capture",
                      "failed to initialize remote desktop session"):
        require(forbidden not in output.lower(), f"Core entered the ordinary portal path: {forbidden}")
    calls = method_calls(traffic)
    lookups = [record for record in calls if "member=GetNameOwner" in record and f'string "{PROVIDER}"' in record]
    require(len(lookups) == 1, "Core must look up the absent explicit provider exactly once")
    require(not any("org.freedesktop.portal" in record or "/org/freedesktop/portal" in record for record in calls),
            "Private bus audit observed portal fallback")


def main(executable):
    require(sys.platform == "linux", "This smoke test runs only on Linux and never uses a desktop session")
    executable = Path(executable).resolve(strict=True)
    require(executable.is_file() and os.access(executable, os.X_OK), "CORE_EXECUTABLE must be an executable regular file")
    programs = {name: shutil.which(name) for name in ("dbus-daemon", "dbus-monitor", "dbus-send")}
    require(all(programs.values()), "This smoke requires dbus-daemon and dbus-bin (dbus-monitor, dbus-send)")
    os.umask(0o077)
    with tempfile.TemporaryDirectory(prefix="df-gnome-core-", dir="/tmp") as directory:
        root = Path(directory)
        environment = private_environment(root)
        bus_config = root / "bus.conf"
        bus_config.write_text(
            '<busconfig><type>session</type><listen>' + environment["DBUS_SESSION_BUS_ADDRESS"] + '</listen>'
            '<auth>EXTERNAL</auth><policy context="default"><allow send_destination="*" eavesdrop="true"/>'
            '<allow receive_sender="*" eavesdrop="true"/><allow own="*"/></policy></busconfig>\n', encoding="utf-8"
        )
        bus_path, monitor_path, core_path = (root / name for name in ("bus.log", "monitor.log", "core.log"))
        logs = [bus_path, monitor_path, core_path]
        bus = monitor = core = None
        files = []
        try:
            def start(arguments, path):
                log = path.open("wb")
                files.append(log)
                return subprocess.Popen(
                    arguments, env=environment, cwd=root, stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                    start_new_session=True, close_fds=True,
                )

            bus = start([programs["dbus-daemon"], "--nofork", "--nopidfile", "--config-file=" + str(bus_config)], bus_path)
            endpoint = root / "session.sock"
            wait_until(lambda: endpoint.exists(), "Private session bus did not start", [("bus", bus)], logs)
            metadata = endpoint.lstat()
            require(stat.S_ISSOCK(metadata.st_mode) and metadata.st_uid == os.geteuid(), "Private bus socket is not owned")
            monitor = start(
                [programs["dbus-monitor"], "--address", environment["DBUS_SESSION_BUS_ADDRESS"], "--monitor", "type='method_call'"],
                monitor_path,
            )
            children = [("bus", bus), ("monitor", monitor)]
            # BecomeMonitor retires the monitor's original unique bus name.
            # Its NameLost notification precedes our independent GetId marker.
            wait_until(lambda: "member=NameLost" in log_text(monitor_path), "Private monitor did not become ready", children, logs)
            monitor_marker(programs["dbus-send"], environment, monitor_path, children, logs)
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reservation:
                reservation.bind(("127.0.0.1", 0))
                # No listen(), and retain the bind until after the core exits.
                port = reservation.getsockname()[1]
                settings = root / "core.ini"
                settings.write_text(
                    "[client]\nremoteHost=127.0.0.1\n" + f"[core]\nport={port}\ncomputerName=windowshare-gnome-smoke\n"
                    "preventSleep=false\nenableEnterCommand=false\nenableExitCommand=false\n"
                    "[log]\nlevel=DEBUG\ntoFile=false\n", encoding="utf-8"
                )
                viewer_started = root / "viewer-started"
                viewer = root / "never-started-viewer"
                viewer.write_text("#!/bin/sh\nprintf invoked > " + shlex.quote(str(viewer_started)) + "\nexit 97\n", encoding="utf-8")
                viewer.chmod(0o700)
                config = root / "gnome.json"
                config.write_text(json.dumps({
                    "version": 1, "viewerExecutable": str(viewer), "connect": "127.0.0.1", "port": port,
                    "identity": str(root / "identity"), "peerPin": "a" * 64,
                }), encoding="utf-8")
                core = start(
                    [str(executable), "client", "--new-instance", "--settings", str(settings),
                     "--windowshare-gnome-config=" + str(config)], core_path,
                )
                wait_until(lambda: core.poll() is not None, "Core did not terminate on provider absence", children, logs, START_SECONDS)
                result = core.wait(timeout=1)
                monitor_marker(programs["dbus-send"], environment, monitor_path, children, logs)
                verify_result(log_text(core_path), log_text(monitor_path), result, port, viewer_started.exists())
        except BaseException:
            for path in logs:
                print(f"\n{path.name} (tail):\n{log_text(path, tail=True)}", file=sys.stderr)
            raise
        finally:
            # No dependency is torn down until the owned core is reaped.
            try:
                terminate_owned(core)
            finally:
                try:
                    terminate_owned(monitor)
                finally:
                    terminate_owned(bus)
                    for file in files:
                        file.close()
    print(json.dumps({
        "provider_absence_exit_code": result, "failure_after_event_queue_ready": True,
        "private_bus_audited": True, "portal_fallback": False, "network_server_started": False,
        "viewer_started": False, "user_session_used": False,
    }, sort_keys=True))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: gnome-core-smoke.py CORE_EXECUTABLE")

    def interrupted(signum, _frame):
        raise KeyboardInterrupt(f"Smoke interrupted by signal {signum}")

    signal.signal(signal.SIGTERM, interrupted)
    main(sys.argv[1])
