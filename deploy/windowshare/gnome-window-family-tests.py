#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT

"""Exercise exact attached Viewers in a private headless GNOME compositor.

Uses the production Viewer and provider with owned xdg_toplevel transients.
No real Mac app or physical input is exercised. Host native acceptance is H08/H09.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import traceback

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("qt_fixture", HERE / "gnome-qt-viewer-tests.py")
qt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qt)
common = qt.common
require = common.require


def run(binary, output):
    output.mkdir(parents=True, exist_ok=False)
    report = {"status": "failed", "scope": "owned production Qt Viewer in private headless GNOME50",
              "physicalInput": False, "productionInputAuthority": False}
    processes = provider = None
    try:
        with tempfile.TemporaryDirectory(prefix="deskflow-native-family-") as directory:
            root = Path(directory)
            processes = common.Processes(output, common.private_environment(root))
            shell = qt.start_shell(processes, root, report)
            provider = common.Provider(processes.env["DBUS_SESSION_BUS_ADDRESS"])
            session, generation, fd = provider.create()
            flags = shlex.split(processes.run("ei-cflags", ["pkg-config", "--cflags", "--libs", "libei-1.0"]))
            helper = output / "gnome-ei-fixture"
            processes.run("ei-build", ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                                      str(HERE / "gnome-ei-fixture.c"), "-o", str(helper), *flags], timeout=20)
            try:
                ei = common.EiFixture(processes, helper, fd)
            finally:
                os.close(fd)
            report["eiDevice"] = ei.event("ready")
            ei.sync("M {request} 0 360")
            ei.sync("M {request} 640 360")
            ei.sync("E {request}")
            frame = subprocess.run(
                ["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                 "testsrc2=size=640x480:rate=1", "-frames:v", "1", "-c:v", "libx264", "-preset", "ultrafast",
                 "-tune", "zerolatency", "-pix_fmt", "yuv420p", "-f", "h264", "pipe:1"],
                env=processes.env, capture_output=True, timeout=20, check=True)
            frame_path, commands, events = root / "frame.h264", root / "commands", output / "viewer.jsonl"
            frame_path.write_bytes(frame.stdout)
            token, = provider.call("LaunchViewer", "(susas)",
                                   (session, generation, str(binary), [str(frame_path), str(commands), str(events)]))

            def stage(name):
                require(shell.poll() is None, "Owned compositor exited")
                if not events.exists():
                    return None
                require(events.stat().st_size < 8192, "Oversized fixture events")
                records = [json.loads(line) for line in events.read_text().splitlines()]
                require(not any("error" in record for record in records), f"Viewer fixture failed: {records}")
                return next((record for record in records if record.get("stage") == name), None)

            parent = common.wait_for(processes, lambda: stage("parent"), "painted parent", retry_errors=False)

            def observe(record):
                values, = provider.call("GetViewerWithAssociation", "(suss)",
                                        (session, generation, token, record["association"]))
                return values

            def mapped(record):
                values = observe(record)
                return values if values.get("window") and values.get("surface") and values.get("frameRect") else None

            report["parent"] = common.wait_for(processes, lambda: mapped(parent), "native parent association")
            for role in ("sheet", "menu"):
                commands.write_text(role)
                child = common.wait_for(processes, lambda: stage(role), "painted attached " + role,
                                        retry_errors=False)
                require(child["parentBlocked"], "Attached child did not fence its parent")
                observed = common.wait_for(processes, lambda: mapped(child), "native child association")
                require(observed.get("reason") in ("", "viewer-not-focused", "buttons-held"),
                        f"Owned transient rejected by native provider: {observed}")
                x, y, width, height = observed["frameRect"]
                # This EI sender is confined to this disposable compositor.
                ei.sync("M {request} 0 360")
                ei.sync(f"M {{request}} {int(x + width/2)} {int(y + height/2)}")
                ei.sync("D {request}")
                ei.sync("U {request}")
                def focused():
                    values = observe(child)
                    return values if values.get("available") else None
                available = common.wait_for(processes, focused, "focused owned transient")
                report[role] = {"viewer": child, "native": available}
                require(available["window"] != report["parent"].get("window"), "Child reused parent native identity")
                if role == "sheet":
                    commands.write_text("dismiss")
                    dismissed = common.wait_for(processes, lambda: stage("dismissed"), "retired child", retry_errors=False)
                    require(not dismissed["parentBlocked"] and dismissed["association"] == parent["association"],
                            "Dismissal replaced or left parent blocked")
                    report["dismissed"] = observe(dismissed)
            commands.write_text("reparent")
            changed = common.wait_for(processes, lambda: stage("qt-reparented"), "Qt reparent fence", retry_errors=False)
            require(not changed["childAvailable"], "Reparented child retained Qt native authority")
            report["qtReparentDenied"] = changed
            commands.write_text("native-reparent")
            common.wait_for(processes, lambda: stage("reparented"), "native parent mutation", retry_errors=False)
            def reparent_denied():
                try:
                    observed = observe(child)
                except Exception as error:
                    require("owned viewer parent changed" in str(error),
                            f"Unexpected error while checking native reparent denial: {error}")
                    return {"closed": True, "reason": str(error)}
                # Qt's set_parent request and the provider's D-Bus query are
                # asynchronous. Wait for the native observation, with the
                # harness deadline; never treat an assertion failure as success.
                return None
            report["reparentDenied"] = common.wait_for(processes, reparent_denied,
                                                       "native reparent invalidation", retry_errors=False)
            commands.write_text("stop")
            provider.close()
            provider = None
            processes.close()
            report["status"] = "passed"
    except Exception as error:
        report["failure"] = str(error)
        (output / "failure.log").write_text(traceback.format_exc())
    finally:
        if provider:
            provider.close()
        if processes:
            processes.close()
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    raise SystemExit(run(args.fixture.resolve(), args.output.resolve()))
