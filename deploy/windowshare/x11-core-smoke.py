#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Exercise the real Linux core only on a newly created, authenticated Xvfb.

Requires Python 3, Xvfb and xauth. Never reuses DISPLAY or a user's settings,
IPC directory, session bus or X authority. The ordinary and opt-in clients
connect only to a loopback TCP port held open without listen(), so no server
can supply input. Normal XWindowsComputer initialization and teardown occur
inside the disposable display; this is not a desktop or delivery test. Core
shutdown uses the private IPC stop command, with the child PID checked using
SO_PEERCRED before any command is sent. Core signals are only fallback cleanup.
"""

import json
import os
from pathlib import Path
import re
import secrets
import selectors
import shutil
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time


START_SECONDS = 15
STOP_SECONDS = 8
MAX_LOG_BYTES = 4 * 1024 * 1024
ENABLED = "experimental X11 coordinate observation enabled; original input routing remains disabled"
OBSERVATION = re.compile(
    r"experimental X11 observation: root=(\d+) screen=(\d+) pointer=(\d+) outputs=(\d+); local evidence only"
)


def private_environment(root):
    environment = os.environ.copy()
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "XAUTHORITY",
                 "SESSION_MANAGER", "DESKTOP_STARTUP_ID", "QT_QPA_PLATFORMTHEME",
                 "QT_LOGGING_RULES", "QT_MESSAGE_PATTERN", "QT_FATAL_WARNINGS",
                 "QT_FATAL_CRITICALS", "QT_DEBUG_PLUGINS"):
        environment.pop(name, None)
    directories = {
        "HOME": "home", "TMPDIR": "tmp", "XDG_RUNTIME_DIR": "runtime",
        "XDG_CONFIG_HOME": "config", "XDG_CONFIG_DIRS": "config-dirs",
        "XDG_CACHE_HOME": "cache", "XDG_DATA_HOME": "data",
        "XDG_DATA_DIRS": "data-dirs", "XDG_STATE_HOME": "state",
    }
    for name, directory in directories.items():
        path = root / directory
        path.mkdir(mode=0o700, parents=True)
        environment[name] = str(path)
    environment.update({
        "LC_ALL": "C", "QT_QPA_PLATFORM": "xcb", "XDG_SESSION_TYPE": "x11",
        "DBUS_SESSION_BUS_ADDRESS": "unix:path=" + str(root / "no-session-bus"),
    })
    return environment


def log_text(path, *, tail=False):
    if not path.exists():
        return ""
    size = path.stat().st_size
    if not tail and size > MAX_LOG_BYTES:
        raise RuntimeError(f"{path.name} exceeded the bounded smoke log limit")
    with path.open("rb") as stream:
        if tail:
            stream.seek(max(0, size - 6000))
        return stream.read(6000 if tail else MAX_LOG_BYTES).decode("utf-8", errors="replace")


def terminate_owned(process, name, *, require_clean=False):
    """Signal only a Popen-owned process, always wait with finite deadlines."""
    if process is None:
        return
    if require_clean and process.poll() is not None:
        raise RuntimeError(f"{name} exited before the requested SIGTERM (exit {process.returncode})")
    if process.poll() is None:
        process.terminate()
    try:
        result = process.wait(timeout=STOP_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)
        raise RuntimeError(f"{name} required SIGKILL after the SIGTERM deadline") from None
    if require_clean and result != 0:
        raise RuntimeError(f"{name} did not exit normally after SIGTERM (exit {result})")


def stop_core(process, temporary_directory, name):
    """Use only this child's private Qt IPC socket, never the user's core."""
    if process.poll() is not None:
        raise RuntimeError(f"{name} exited before its IPC stop request (exit {process.returncode})")
    endpoint = temporary_directory / "deskflow-core"
    metadata = endpoint.lstat()
    if not stat.S_ISSOCK(metadata.st_mode) or metadata.st_uid != os.geteuid():
        raise RuntimeError(f"{name} did not create its owned private IPC socket")
    deadline = time.monotonic() + STOP_SECONDS
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(STOP_SECONDS)
        connection.connect(str(endpoint))
        credentials = connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("3i"))
        peer_pid, peer_uid, _peer_gid = struct.unpack("3i", credentials)
        if peer_pid != process.pid or peer_uid != os.geteuid() or process.poll() is not None:
            raise RuntimeError(f"Refusing to send a stop command to an IPC peer other than the owned {name}")
        # IpcServer::processMessage dispatches newline-delimited commands;
        # CoreIpcServer accepts stop without a version hello and replies ok/bye.
        connection.sendall(b"stop\n")
        reply = bytearray()
        while b"ok\n" not in reply:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or len(reply) >= 1024:
                raise RuntimeError(f"{name} did not acknowledge its bounded IPC stop request")
            connection.settimeout(remaining)
            block = connection.recv(1024 - len(reply))
            if not block:
                raise RuntimeError(f"{name} closed IPC without acknowledging its stop request")
            reply.extend(block)
    try:
        result = process.wait(timeout=max(0.001, deadline - time.monotonic()))
    except subprocess.TimeoutExpired:
        raise RuntimeError(f"{name} did not exit before its IPC stop deadline") from None
    if result != 0:
        raise RuntimeError(f"{name} did not exit normally after IPC stop (exit {result})")


def write_authority(xauth, path, display, cookie, environment):
    path.touch(mode=0o600)
    # Keep the disposable cookie out of process arguments and diagnostics.
    result = subprocess.run(
        [xauth, "-q", "-f", str(path)],
        input=f"add {display} MIT-MAGIC-COOKIE-1 {cookie}\n", text=True,
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=environment,
        timeout=5, check=False,
    )
    if result.returncode:
        raise RuntimeError(f"xauth failed (exit {result.returncode})")


def allocated_display(process, descriptor):
    deadline = time.monotonic() + START_SECONDS
    value = bytearray()
    with selectors.DefaultSelector() as selector:
        selector.register(descriptor, selectors.EVENT_READ)
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f"Xvfb exited before allocating a display (exit {process.returncode})")
            if not selector.select(timeout=min(0.1, max(0, deadline - time.monotonic()))):
                continue
            block = os.read(descriptor, 32)
            if not block:
                raise RuntimeError("Xvfb closed its displayfd before reporting readiness")
            value.extend(block)
            if len(value) > 12:
                raise RuntimeError("Xvfb reported an invalid display number")
            if b"\n" in value:
                if not re.fullmatch(rb"[0-9]{1,10}\n", value):
                    raise RuntimeError("Xvfb reported an invalid display number")
                return ":" + value[:-1].decode("ascii")
    raise RuntimeError("Xvfb did not report a private display before the deadline")


def run_core(executable, root, display, authority, port, xvfb, *, observe):
    label = "observer" if observe else "ordinary"
    case = root / label
    case.mkdir(mode=0o700)
    environment = private_environment(case)
    environment.update({"DISPLAY": display, "XAUTHORITY": str(authority)})
    settings = case / "core.ini"
    settings.write_text(
        "[client]\nremoteHost=127.0.0.1\n"
        f"[core]\nport={port}\ndisplay={display}\ncomputerName=windowshare-x11-smoke\n"
        "preventSleep=false\nenableEnterCommand=false\nenableExitCommand=false\n"
        "[log]\nlevel=DEBUG\ntoFile=false\n", encoding="utf-8"
    )
    arguments = [str(executable), "client", "--new-instance", "--settings", str(settings)]
    if observe:
        arguments.append("--windowshare-observe-x11")
    log_path = root / (label + ".log")
    with log_path.open("wb") as log:
        process = subprocess.Popen(
            arguments, env=environment, cwd=case, stdin=subprocess.DEVNULL,
            stdout=log, stderr=log, start_new_session=True, close_fds=True,
        )
        try:
            deadline = time.monotonic() + START_SECONDS
            ready_at = None
            observation = None
            while time.monotonic() < deadline:
                if xvfb.poll() is not None:
                    raise RuntimeError("The private Xvfb exited while the core was running")
                if process.poll() is not None:
                    raise RuntimeError(f"{label} core exited before the test completed (exit {process.returncode})")
                output = log_text(log_path)
                if not observe and "experimental X11" in output:
                    raise RuntimeError("The ordinary client unexpectedly enabled X11 observation")
                ready = all(message in output for message in (
                    "detected X11 platform", "started client", "event queue is ready", f"127.0.0.1:{port}"
                ))
                if observe:
                    observation = OBSERVATION.search(output)
                    ready = ready and ENABLED in output and observation is not None
                    if observation:
                        root_id, screen, pointer, outputs = map(int, observation.groups())
                        if root_id == 0 or screen != 0 or pointer == 0 or outputs == 0:
                            raise RuntimeError("The core reported incomplete private X11 observation")
                if ready:
                    if ready_at is None:
                        ready_at = time.monotonic()
                    # Keep both paths alive for many 20 ms observer timer turns.
                    if time.monotonic() - ready_at >= 1:
                        break
                time.sleep(0.025)
            else:
                raise RuntimeError(f"{label} core did not reach its expected X11 state before the deadline")
            stop_core(process, Path(environment["TMPDIR"]), label + " core")
            output = log_text(log_path)
            if not all(message in output for message in (
                "core ipc server got stop message", "stopped client", "core stopped successfully"
            )):
                raise RuntimeError(f"{label} core omitted normal client/core shutdown evidence")
            if not observe and "experimental X11" in output:
                raise RuntimeError("The ordinary client started X11 observation during shutdown")
            if observation:
                return dict(zip(("root", "screen", "pointer", "outputs"), map(int, observation.groups())))
            return None
        finally:
            terminate_owned(process, label + " core")


def main(executable):
    if sys.platform != "linux":
        raise RuntimeError("This smoke test only runs on Linux and always creates its own Xvfb")
    executable = Path(executable).resolve(strict=True)
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise RuntimeError("CORE_EXECUTABLE must be an executable regular file")
    xvfb_program, xauth = shutil.which("Xvfb"), shutil.which("xauth")
    if not xvfb_program or not xauth:
        raise RuntimeError("This smoke test requires Xvfb and xauth on PATH")
    os.umask(0o077)
    # Explicit /tmp also keeps Unix socket paths short and ignores user TMPDIR.
    with tempfile.TemporaryDirectory(prefix="df-x11-", dir="/tmp") as directory:
        root = Path(directory)
        environment = private_environment(root / "xserver")
        server_authority, client_authority = root / "server.auth", root / "client.auth"
        cookie = secrets.token_hex(16)
        # Xvfb reads the cookie from this file; its own -displayfd chooses the
        # unused display. Only after that do we create the matching client row.
        write_authority(xauth, server_authority, ":0", cookie, environment)
        xvfb = None
        try:
            read_fd, write_fd = os.pipe()
            with (root / "xvfb.log").open("wb") as log:
                try:
                    try:
                        xvfb = subprocess.Popen(
                            [xvfb_program, "-displayfd", str(write_fd), "-screen", "0", "1280x800x24",
                             "-nolisten", "tcp", "-noreset", "-auth", str(server_authority)],
                            env=environment, stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                            pass_fds=(write_fd,), start_new_session=True,
                        )
                    finally:
                        os.close(write_fd)
                    display = allocated_display(xvfb, read_fd)
                finally:
                    os.close(read_fd)
                write_authority(xauth, client_authority, display, cookie, environment)
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reservation:
                    reservation.bind(("127.0.0.1", 0))
                    # Keep ownership without listen(): every connection is
                    # refused and another process cannot claim this endpoint.
                    port = reservation.getsockname()[1]
                    run_core(executable, root, display, client_authority, port, xvfb, observe=False)
                    snapshot = run_core(executable, root, display, client_authority, port, xvfb, observe=True)
                terminate_owned(xvfb, "Xvfb", require_clean=True)
        except BaseException:
            for path in sorted(root.glob("*.log")):
                print(f"\n{path.name} (tail):\n{log_text(path, tail=True)}", file=sys.stderr)
            raise
        finally:
            terminate_owned(xvfb, "Xvfb")
    print(json.dumps({
        "ordinary_observer_disabled": True, "opt_in_observation": snapshot,
        "core_ipc_clean_exit": True, "private_xvfb_clean_exit": True,
        "network_server_started": False, "user_display_used": False,
    }, sort_keys=True))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: x11-core-smoke.py CORE_EXECUTABLE")
    # A terminated CI step must still enter the owned-process cleanup paths.
    def interrupted(signum, _frame):
        raise KeyboardInterrupt(f"Smoke interrupted by signal {signum}")

    signal.signal(signal.SIGTERM, interrupted)
    main(sys.argv[1])
