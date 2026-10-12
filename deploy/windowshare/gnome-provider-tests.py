#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT

"""Exercise the real GNOME 50 provider in a disposable native headless Shell.

Ubuntu 26.04 dependencies and the container invocation are in
gnome-provider-container.sh. This script creates its own session AND system
buses, Wayland socket, HOME, settings, and runtime directory. It never attaches
to DISPLAY, WAYLAND_DISPLAY, or a caller's D-Bus session. No DRM/input device
mount, privileged container, portal permission, or installed extension is used.

Evidence is deliberately scoped: the real compositor owns the EIS session and
the launched GTK Wayland client. Pointer moves, EI sync replies, native hit
observations and actual GTK motion are checked. This does not establish a Qt
viewport mapping, pointer-device attribution, or production delivery authority.

Primary sources for the command and fence semantics:
https://github.com/GNOME/mutter/blob/50.0/src/core/meta-context-main.c
https://github.com/GNOME/mutter/blob/50.0/src/backends/native/meta-backend-native.c
https://github.com/GNOME/mutter/blob/50.0/src/backends/native/meta-renderer-native.c
https://libinput.pages.freedesktop.org/libei/api/group__libei.html
https://packages.ubuntu.com/resolute/libeis1
"""

from __future__ import annotations

import argparse
import contextlib
import json
import math
import os
from pathlib import Path
import selectors
import shlex
import shutil
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time
import traceback

UUID = "seamless-windows@deskflow.org"
SERVICE = "org.deskflow.Seamless.Gnome50"
OBJECT = "/org/deskflow/Seamless/Gnome50"
LOG_LIMIT = 16 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def private_environment(root):
    """Use a small allowlist, not a copy of the invoking desktop's environment."""
    paths = {name: root / name for name in ("home", "runtime", "config", "cache", "data", "tmp")}
    for path in paths.values():
        path.mkdir(mode=0o700)
    return {
        "PATH": "/usr/bin:/bin",
        "LANG": "C.UTF-8",
        "HOME": str(paths["home"]),
        "XDG_RUNTIME_DIR": str(paths["runtime"]),
        "XDG_CONFIG_HOME": str(paths["config"]),
        "XDG_CACHE_HOME": str(paths["cache"]),
        "XDG_DATA_HOME": str(paths["data"]),
        "TMPDIR": str(paths["tmp"]),
        "XDG_DATA_DIRS": "/usr/local/share:/usr/share",
        "XDG_CURRENT_DESKTOP": "GNOME",
        "XDG_SESSION_DESKTOP": "gnome",
        "XDG_SESSION_TYPE": "wayland",
        "GNOME_SHELL_SESSION_MODE": "user",
        "GSETTINGS_BACKEND": "keyfile",
        "LIBGL_ALWAYS_SOFTWARE": "1",
        "GALLIUM_DRIVER": "llvmpipe",
        "GSK_RENDERER": "cairo",
        "GDK_BACKEND": "wayland",
        "QT_QPA_PLATFORM": "wayland",
        "NO_AT_BRIDGE": "1",
    }


class Processes:
    def __init__(self, output, env):
        self.output = output
        self.env = env
        self.children = []
        self.files = []
        self.deadline = time.monotonic() + 180
        self.closed = False

    def check(self):
        require(time.monotonic() < self.deadline, "Whole fixture exceeded 180 seconds")
        for path in self.output.glob("*.log"):
            require(path.stat().st_size <= LOG_LIMIT, f"Log exceeded {LOG_LIMIT} bytes: {path.name}")

    def start(self, name, command, **kwargs):
        self.check()
        log = (self.output / f"{name}.log").open("wb")
        self.files.append(log)
        child = subprocess.Popen(
            command,
            env=self.env,
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
            **kwargs,
        )
        self.children.append(child)
        return child

    def run(self, name, command, timeout=10):
        self.check()
        result = subprocess.run(command, env=self.env, capture_output=True, timeout=timeout)
        (self.output / f"{name}.log").write_bytes(result.stdout + result.stderr)
        require(result.returncode == 0, f"{name} exited {result.returncode}; see {name}.log")
        self.check()
        return result.stdout.decode("utf-8", errors="replace").strip()

    def close(self):
        if self.closed:
            return
        self.closed = True
        # Every signal is to a process group that this invocation created.
        for child in reversed(self.children):
            try:
                os.killpg(child.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        deadline = time.monotonic() + 5
        for child in reversed(self.children):
            try:
                child.wait(timeout=max(0.01, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait(timeout=3)
        for file in self.files:
            file.close()


def wait_for(processes, predicate, description, timeout=12, retry_errors=True):
    deadline = time.monotonic() + timeout
    last_error = "not ready"
    while time.monotonic() < deadline:
        processes.check()
        try:
            value = predicate()
            if value:
                return value
        except Exception as error:
            if not retry_errors:
                raise
            last_error = str(error)
        time.sleep(0.05)
    raise RuntimeError(f"Timed out waiting for {description}: {last_error}")


def wait_for_disabled_extension(processes, shell, provider, timeout=40):
    responded = False

    def initialized():
        nonlocal responded
        require(shell.poll() is None, f"GNOME Shell exited {shell.returncode}")
        if provider.bus("NameHasOwner", "(s)", ("org.gnome.Shell",)) == (False,):
            require(not responded, "Shell D-Bus owner vanished during extension discovery")
            return None
        try:
            info, = provider.call(
                "GetExtensionInfo", "(s)", (UUID,), destination="org.gnome.Shell",
                path="/org/gnome/Shell", interface="org.gnome.Shell.Extensions",
            )
        except provider.GLib.Error as error:
            # Shell owns its name before exporting this object/interface.
            # Once it has replied, disappearance is no longer startup absence.
            if not responded and error.matches(provider.Gio.dbus_error_quark(), provider.Gio.DBusError.UNKNOWN_METHOD):
                return None
            raise
        responded = True
        require(isinstance(info, dict), f"Invalid extension metadata: {info}")
        if not info:
            return None
        require(info.get("uuid") == UUID, f"Unexpected extension identity: {info}")
        require(info.get("enabled") is False and info.get("error") == "",
                f"Extension was not initially disabled and error-free: {info}")
        # Shell 50.1 publishes createExtensionObject() before awaiting
        # _handleMajorUpdate(); loadExtension() then assigns INITIALIZED.
        # Its D-Bus serializer omits the still-undefined state in that gap.
        # https://github.com/GNOME/gnome-shell/blob/50.1/js/ui/extensionSystem.js
        if "state" not in info:
            return None
        require(type(info["state"]) in (int, float) and info["state"] in (2, 6),
                f"Extension was not initially disabled: {info}")
        return info

    # Only explicit startup absence is pending. Never retry a Shell exit,
    # other D-Bus error, enabled extension, error state or malformed response.
    return wait_for(processes, initialized, "initially disabled Shell extension", timeout=timeout, retry_errors=False)


def start_bus(processes, root, name):
    address = "unix:path=" + str(root / f"{name}.sock")
    # No service directories: activation cannot escape into a caller's user
    # services or launch unrelated desktop daemons. Missing optional GNOME
    # services are real absence, not mocked compositor/input implementations.
    config = root / f"{name}.conf"
    config.write_text(
        '<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" '
        '"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">\n'
        f'<busconfig><type>session</type><listen>{address}</listen><auth>EXTERNAL</auth>'
        '<policy context="default"><allow send_destination="*"/>'
        '<allow receive_sender="*"/><allow own="*"/></policy></busconfig>\n'
    )
    child = processes.start(
        name, ["dbus-daemon", f"--config-file={config}", f"--address={address}", "--nofork", "--nopidfile"]
    )
    wait_for(
        processes,
        lambda: child.poll() is None and (root / f"{name}.sock").exists(),
        f"private {name}",
        timeout=5,
    )
    return address


def load_gio():
    import gi

    gi.require_version("Gio", "2.0")
    from gi.repository import Gio, GLib

    return Gio, GLib


class Provider:
    def __init__(self, address):
        self.Gio, self.GLib = load_gio()
        self.connection = self.Gio.DBusConnection.new_for_address_sync(
            address,
            self.Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | self.Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None,
            None,
        )
        self.connection.set_exit_on_close(False)

    def call(self, method, signature=None, values=(), *, destination=SERVICE, path=OBJECT, interface=SERVICE):
        value = None if signature is None else self.GLib.Variant(signature, values)
        return self.connection.call_sync(
            destination, path, interface, method, value, None, self.Gio.DBusCallFlags.NO_AUTO_START, 3000, None
        ).unpack()

    def bus(self, method, signature, values):
        return self.call(
            method, signature, values,
            destination="org.freedesktop.DBus", path="/org/freedesktop/DBus", interface="org.freedesktop.DBus",
        )

    def create(self):
        result, descriptors = self.connection.call_with_unix_fd_list_sync(
            SERVICE, OBJECT, SERVICE, "CreateSession", None, self.GLib.VariantType.new("(suh)"),
            self.Gio.DBusCallFlags.NO_AUTO_START, 3000, None, None,
        )
        session, generation, index = result.unpack()
        require(isinstance(session, str) and len(session) > 0 and generation > 0, "Invalid session binding")
        require(descriptors is not None and descriptors.get_length() == 1 and index == 0, "Expected exactly one EIS FD")
        return session, generation, descriptors.get(index)

    def close(self):
        if not self.connection.is_closed():
            self.connection.close_sync(None)


class EiFixture:
    def __init__(self, processes, executable, fd):
        self.processes = processes
        self.log = (processes.output / "ei.log").open("ab")
        processes.files.append(self.log)
        self.child = subprocess.Popen(
            [str(executable), str(fd)], env=processes.env, pass_fds=(fd,),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log, start_new_session=True,
        )
        processes.children.append(self.child)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.child.stdout, selectors.EVENT_READ)
        self.buffer = b""
        self.request = 0

    def event(self, expected, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.processes.check()
            if b"\n" not in self.buffer:
                if not self.selector.select(timeout=0.1):
                    continue
                chunk = os.read(self.child.stdout.fileno(), 4096)
                require(chunk, f"EI fixture exited before {expected} (exit {self.child.poll()})")
                self.buffer += chunk
                require(len(self.buffer) <= 8192, "Oversized EI fixture output")
                continue
            line, self.buffer = self.buffer.split(b"\n", 1)
            self.log.write(line + b"\n")
            self.log.flush()
            message = json.loads(line)
            require(message.get("event") == expected, f"Expected EI {expected}, got {message}")
            return message
        raise RuntimeError(f"EI fixture timed out waiting for {expected}")

    def sync(self, command):
        self.request += 1
        self.child.stdin.write((command.format(request=self.request) + "\n").encode("ascii"))
        self.child.stdin.flush()
        require(self.event("synced").get("request") == self.request, "Wrong EI fence reply")


def expect_rejected(function, description):
    try:
        function()
    except Exception as error:
        # A compositor crash, transport timeout or missing method is not proof
        # that the provider performed an authorization check.
        name = type(error).__name__
        text = str(error)
        require(name == "Error" and "GDBus.Error:" in text, f"{description}: not a provider rejection: {text}")
        require("org.deskflow." in text or "org.freedesktop.DBus.Error.AccessDenied" in text,
                f"{description}: unexpected D-Bus rejection: {text}")
        return
    raise RuntimeError(f"{description} unexpectedly succeeded")


def observation_only(observation):
    require(observation.get("observationOnly") is True, "Provider did not mark its observation-only scope")
    require(observation.get("deliveryReady") is False, "Observation accidentally claimed delivery authority")


def viewer_child(log_path):
    """Only launched by Meta.WaylandClient on this fixture's private compositor."""
    require(sys.platform == "linux" and os.environ.get("WAYLAND_SOCKET"), "Viewer requires a provider-inherited Wayland FD")
    os.environ["GDK_BACKEND"] = "wayland"
    import gi

    gi.require_version("Gtk", "4.0")
    gi.require_version("Gdk", "4.0")
    from gi.repository import Gdk, GLib, GObject, Gtk

    output = Path(log_path).open("x", buffering=1)

    def log(event, **values):
        output.write(json.dumps({"event": event, **values}) + "\n")

    application = Gtk.Application(application_id="org.deskflow.PrivateGnomeFixture")

    def activate(app):
        display = Gdk.Display.get_default()
        require(display is not None and GObject.type_name(display.__gtype__) == "GdkWaylandDisplay",
                "GTK did not use Wayland")
        window = Gtk.ApplicationWindow(application=app, title="Deskflow disposable provider fixture")
        window.set_default_size(640, 400)
        window.set_child(Gtk.Label(label="Private GNOME provider fixture"))

        def pointer_event(controller, event_name, x, y):
            event = controller.get_current_event()
            require(event is not None, "Missing real GDK event")
            ok, surface_x, surface_y = event.get_position()
            require(ok, "GDK event has no surface-relative position")
            log(event_name, x=x, y=y, surfaceX=surface_x, surfaceY=surface_y)

        motion = Gtk.EventControllerMotion.new()
        motion.connect("enter", lambda controller, x, y: pointer_event(controller, "enter", x, y))
        motion.connect("motion", lambda controller, x, y: pointer_event(controller, "motion", x, y))
        window.add_controller(motion)
        window.connect("map", lambda _window: log("mapped", pid=os.getpid()))
        window.present()
        GLib.timeout_add_seconds(100, lambda: app.quit() or False)

    application.connect("activate", activate)
    return application.run([])


def run_fixture(arguments):
    require(sys.platform == "linux", "The real compositor harness requires Linux")
    require(os.geteuid() != 0, "Run as an unprivileged disposable container user, not root")
    require(arguments.output.is_absolute(), "--output must be absolute")
    arguments.output.mkdir(parents=True, exist_ok=False)
    output = arguments.output
    report = {"schema": 1, "status": "failed", "scope": "real-native-headless-gnome-provider-observations"}
    processes = None
    provider = None
    foreign = None
    try:
        with contextlib.ExitStack() as cleanup:
            temporary = cleanup.enter_context(tempfile.TemporaryDirectory(prefix="df-gnome-", dir="/tmp"))
            root = Path(temporary)
            env = private_environment(root)
            processes = Processes(output, env)
            cleanup.callback(processes.close)
            report["shellVersion"] = processes.run("shell-version", ["gnome-shell", "--version"])
            require(report["shellVersion"].split()[-1].split(".")[0] == "50", "This fixture requires GNOME Shell 50")
            processes.run("libei-version-check", ["pkg-config", "--atleast-version=1.5.0", "libei-1.0"])
            report["libeiVersion"] = processes.run("libei-version", ["pkg-config", "--modversion", "libei-1.0"])
            report["packages"] = processes.run(
                "packages", ["dpkg-query", "-W", "gnome-shell", "libmutter-18-0", "libei1", "libeis1", "libgl1-mesa-dri"]
            ).splitlines()
            source = Path(__file__).resolve().parent
            extension = source / "gnome-provider"
            metadata = json.loads((extension / "metadata.json").read_text())
            require(metadata.get("uuid") == UUID and metadata.get("shell-version") == ["50"], "Unexpected extension metadata")
            target = Path(env["XDG_DATA_HOME"]) / "gnome-shell" / "extensions" / UUID
            shutil.copytree(extension, target)
            env["DBUS_SESSION_BUS_ADDRESS"] = start_bus(processes, root, "session-bus")
            env["DBUS_SYSTEM_BUS_ADDRESS"] = start_bus(processes, root, "system-bus")
            env["WAYLAND_DISPLAY"] = "deskflow-provider-fixture"
            for schema, key, value in [
                ("org.gnome.shell", "enabled-extensions", "[]"),
                ("org.gnome.shell", "disable-user-extensions", "false"),
                ("org.gnome.shell", "welcome-dialog-last-shown-version", "'50.0'"),
                ("org.gnome.desktop.interface", "enable-animations", "false"),
                ("org.gnome.desktop.session", "idle-delay", "uint32 0"),
                ("org.gnome.desktop.screensaver", "lock-enabled", "false"),
            ]:
                processes.run("setting-" + key, ["gsettings", "set", schema, key, value])
            flags = shlex.split(processes.run("ei-cflags", ["pkg-config", "--cflags", "--libs", "libei-1.0"]))
            helper = output / "gnome-ei-fixture"
            processes.run(
                "ei-build", ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                             str(source / "gnome-ei-fixture.c"), "-o", str(helper), *flags], timeout=20
            )
            command = ["gnome-shell", "--wayland", "--headless", "--no-x11", "--virtual-monitor=1280x720",
                       "--wayland-display=" + env["WAYLAND_DISPLAY"]]
            report["compositorCommand"] = command
            shell = processes.start("gnome-shell", command)
            provider = Provider(env["DBUS_SESSION_BUS_ADDRESS"])
            foreign = Provider(env["DBUS_SESSION_BUS_ADDRESS"])

            report["initialExtensionState"] = wait_for_disabled_extension(processes, shell, provider)["state"]
            require(provider.bus("NameHasOwner", "(s)", (SERVICE,)) == (False,), "Default-off provider unexpectedly active")
            report["defaultDisabled"] = True
            processes.run("enable-extension", ["gsettings", "set", "org.gnome.shell", "enabled-extensions", f"['{UUID}']"])
            capabilities = wait_for(processes, lambda: provider.call("GetCapabilities"), "provider activation", timeout=20)
            version, shell_version, caps = capabilities
            require(version == 1 and shell_version.split(".")[0] == "50", "Unexpected provider protocol/version")
            for key in ("nativeHitTesting", "exactWaylandClientBinding", "eisSessionOwned"):
                require(caps.get(key) is True, f"Required real compositor capability absent: {key}: {caps}")
            for key in ("syncFenceVerified", "pointerDeviceAttribution", "qtViewportMapping"):
                require(caps.get(key) is False, f"Provider made an unsupported capability claim: {key}")
            report["capabilities"] = caps
            unique_owner, = provider.bus("GetNameOwner", "(s)", (SERVICE,))
            owner_pid, = provider.bus("GetConnectionUnixProcessID", "(s)", (unique_owner,))
            require(owner_pid == shell.pid, "Provider D-Bus owner is not the compositor this fixture launched")
            report["providerOwnedByCompositor"] = True
            session, generation, fd = provider.create()
            try:
                require(stat.S_ISSOCK(os.fstat(fd).st_mode), "EIS FD is not a socket")
                endpoint = socket.socket(fileno=fd)
                try:
                    require(endpoint.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE) == socket.SOCK_STREAM, "EIS FD not stream")
                    pid, uid, _gid = struct.unpack("3i", endpoint.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
                    require(pid == shell.pid and uid == os.getuid(), "EIS endpoint was not created by this compositor")
                finally:
                    endpoint.detach()
                ei = EiFixture(processes, helper, fd)
            finally:
                os.close(fd)
            report["eisEndpointOwnedByCompositor"] = True
            report["eiDevice"] = ei.event("ready")
            ei.sync("S {request}")
            expect_rejected(lambda: foreign.call("CloseSession", "(su)", (session, generation)), "Foreign caller close")
            expect_rejected(lambda: foreign.call("GetViewer", "(sus)", (session, generation, "unknown")), "Foreign caller query")
            expect_rejected(lambda: provider.call("CloseSession", "(su)", (session, generation + 1)), "Wrong-generation close")
            provider.call("GetCapabilities")
            ei.sync("S {request}")
            report["callerAndGenerationIsolation"] = True
            # A fresh headless seat starts at the Shell hot corner (0, 0).
            # Its real pressure barrier may consume the first x movement and
            # open the overview. Establish a cursor in the monitor interior
            # before the measured rounds; do not disable or bypass barriers.
            region_x, region_y, region_width, region_height = report["eiDevice"]["region"]
            initial_x = region_x + region_width / 2
            initial_y = region_y + region_height / 2
            ei.sync(f"M {{request}} {region_x} {initial_y}")
            ei.sync(f"M {{request}} {initial_x} {initial_y}")
            ei.sync("E {request}")
            viewer_log = output / "viewer.jsonl"
            token, = provider.call(
                "LaunchViewer", "(susas)", (session, generation, "/usr/bin/python3",
                                              [str(Path(__file__).resolve()), "--viewer-child", str(viewer_log)])
            )
            require(isinstance(token, str) and token, "Viewer binding token absent")

            def viewer_observation():
                observation, = provider.call("GetViewer", "(sus)", (session, generation, token))
                observation_only(observation)
                report["latestViewerObservation"] = observation
                return observation if observation.get("available") is True else None

            observation = wait_for(processes, viewer_observation, "provider-owned native Wayland window")
            require(observation.get("viewer") == token and observation.get("session") == session
                    and observation.get("generation") == generation, "Viewer observation binding mismatch")
            require(observation.get("window") and observation.get("surface"), "Native window/surface identity absent")
            require(observation.get("pointerX") == initial_x and observation.get("pointerY") == initial_y,
                    f"Fixture cursor did not leave the initial hot corner: {observation}")
            report["nativeWaylandClientBound"] = True

            def initial_native_hit():
                # Mapping a Wayland window exposes its geometry before the
                # first frame necessarily reaches the stage's pick tree. This
                # is a startup precondition, not a retry of a measured move.
                current, = provider.call("GetViewer", "(sus)", (session, generation, token))
                observation_only(current)
                require(current.get("available") is True and current.get("focused") is True,
                        f"Viewer lost readiness before the initial native hit: {current}")
                require(all(current.get(key) == observation.get(key)
                            for key in ("session", "generation", "viewer", "window", "surface")),
                        "Native viewer identity changed during initial readiness")
                require(current.get("pointerX") == initial_x and current.get("pointerY") == initial_y,
                        f"Initial pointer moved while waiting for the first native hit: {current}")
                hit, = provider.call("ValidateCurrentHit", "(sussddu)",
                                     (session, generation, token, "initial-ready", initial_x, initial_y,
                                      current["revision"]))
                observation_only(hit)
                report["latestInitialHit"] = hit
                require(hit.get("request") == "initial-ready" and hit.get("focused") is True
                        and all(hit.get(key) == current.get(key)
                                for key in ("session", "generation", "revision", "viewer", "window", "surface")),
                        "Initial hit did not retain the exact focused viewer binding")
                if hit.get("available") is not True:
                    require(hit.get("reason") == "not-viewer-surface", f"Initial native hit failed: {hit}")
                    return None
                require(all(isinstance(hit.get(key), (int, float)) and math.isfinite(hit[key])
                            for key in ("localX", "localY")), "Initial native hit omitted finite surface coordinates")
                return current

            observation = wait_for(processes, initial_native_hit, "initial native viewer hit readiness", timeout=5,
                                   retry_errors=False)
            report["initialViewerObservation"] = observation
            frame = observation.get("frameRect")
            require(isinstance(frame, (tuple, list)) and len(frame) == 4
                    and all(math.isfinite(value) for value in frame) and frame[2] > 100 and frame[3] > 100,
                    "Invalid native frame rectangle")

            def viewer_events():
                if not viewer_log.exists():
                    return []
                require(viewer_log.stat().st_size < 1024 * 1024, "Viewer log exceeded bound")
                return [json.loads(line) for line in viewer_log.read_text().splitlines()]

            rounds = []
            for index, offset in enumerate((23, -17, 37), start=1):
                previous = len(viewer_events())
                x, y = frame[0] + frame[2] / 2 + offset, frame[1] + frame[3] / 2 + offset
                ei.sync(f"M {{request}} {x} {y}")
                current, = provider.call("GetViewer", "(sus)", (session, generation, token))
                observation_only(current)
                report["latestMotionObservation"] = {"request": index, "x": x, "y": y, "viewer": current}
                require(current.get("available") is True, f"Viewer invalidated after EI motion: {current}")
                require(abs(current.get("pointerX", -100000) - x) <= 1 and abs(current.get("pointerY", -100000) - y) <= 1,
                        "EI PONG preceded the requested real compositor pointer state")
                hit, = provider.call("ValidateCurrentHit", "(sussddu)",
                                     (session, generation, token, str(index), x, y, current["revision"]))
                observation_only(hit)
                require(hit.get("available") is True and hit.get("request") == str(index), f"Native current hit failed: {hit}")
                require(hit.get("window") == observation["window"] and hit.get("surface") == observation["surface"],
                        "Hit resolved to a different native client")
                require(all(isinstance(hit.get(key), (int, float)) and math.isfinite(hit[key])
                            for key in ("localX", "localY")), "Native hit omitted finite surface coordinates")
                # Wait for this dedicated application's receipt before sending
                # the next move; legitimate compositor coalescing is not a loss.
                # Raw GDK event positions (not widget-local coordinates) are
                # surface-relative. Allow only Wayland's 24.8 quantization.
                wait_for(processes, lambda: any(event.get("event") in ("enter", "motion")
                                                and abs(event.get("surfaceX", -100000) - hit["localX"]) <= 1 / 256
                                                and abs(event.get("surfaceY", -100000) - hit["localY"]) <= 1 / 256
                                                for event in viewer_events()[previous:]),
                         "matching actual GTK Wayland surface coordinates", timeout=5)
                rounds.append({"x": x, "y": y, "revision": current["revision"], "request": index,
                               "surfaceX": hit["localX"], "surfaceY": hit["localY"]})
            report["motionPongCurrentHitRounds"] = rounds
            events = viewer_events()
            report["gtkMotionEvents"] = sum(event.get("event") == "motion" for event in events)
            require(report["gtkMotionEvents"] >= 2, "Dedicated Wayland client did not receive real pointer motion")
            viewer_pid = next(event["pid"] for event in events if event.get("event") == "mapped")
            process_status = Path(f"/proc/{viewer_pid}/status").read_text()
            parent_line = next(line for line in process_status.splitlines() if line.startswith("PPid:"))
            require(int(parent_line.split()[1]) == shell.pid, "Fixture viewer is not the compositor's actual child")
            viewer_pidfd = os.pidfd_open(viewer_pid)
            cleanup.callback(os.close, viewer_pidfd)
            stale, = provider.call("ValidateCurrentHit", "(sussddu)",
                                   (session, generation, token, "stale", rounds[-1]["x"], rounds[-1]["y"],
                                    rounds[-1]["revision"] + 1))
            observation_only(stale)
            require(stale.get("available") is False, "Stale revision unexpectedly validated")
            report["staleRevisionDenied"] = True
            # Drop the unique D-Bus caller while its EIS connection is alive.
            # The compositor must revoke its caller-bound session on name loss.
            provider.close()
            provider = None
            ei.event("disconnected")
            require(ei.child.wait(timeout=3) == 0, "EI teardown fixture failed")
            report["callerDisconnectRevokesEis"] = True
            with selectors.DefaultSelector() as exited:
                exited.register(viewer_pidfd, selectors.EVENT_READ)
                require(exited.select(timeout=3), "Provider did not terminate its owned viewer on caller loss")
            report["callerDisconnectTerminatesOwnedViewer"] = True
            # A fresh, independently owned session must remain usable afterward.
            new_session, new_generation, new_fd = foreign.create()
            try:
                require(new_session != session or new_generation != generation, "Session identity reused after caller loss")
                foreign.call("CloseSession", "(su)", (new_session, new_generation))
            finally:
                os.close(new_fd)
            report["freshSessionAfterRevocation"] = True
            report["status"] = "passed"
            report["deliveryAuthorityEstablished"] = False
    except Exception as error:
        report["failure"] = str(error)
        (output / "failure.log").write_text(traceback.format_exc())
    finally:
        for connection in (provider, foreign):
            if connection is not None:
                try:
                    connection.close()
                except Exception:
                    pass
        if processes is not None:
            processes.close()
        (output / "result.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    if report["status"] != "passed":
        for path in sorted(output.glob("*.log")):
            if path.name in ("gnome-shell.log", "failure.log", "ei.log"):
                print(f"--- {path.name} (tail) ---", file=sys.stderr)
                with path.open("rb") as file:
                    file.seek(max(0, path.stat().st_size - 8000))
                    print(file.read().decode("utf-8", errors="replace"), file=sys.stderr)
        return 1
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, help="New absolute output directory; retains logs and result.json")
    parser.add_argument("--viewer-child", type=Path, help=argparse.SUPPRESS)
    arguments = parser.parse_args()
    if arguments.viewer_child is not None:
        return viewer_child(arguments.viewer_child)
    if arguments.output is None:
        parser.error("--output is required")
    return run_fixture(arguments)


if __name__ == "__main__":
    sys.exit(main())
