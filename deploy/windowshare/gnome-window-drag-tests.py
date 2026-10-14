#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT

"""Check the production Viewer's provisional-to-committed native window.

Run only in the disposable GNOME 50 container described by
gnome-provider-container.sh. This creates private buses, settings and a headless
compositor; fixture input never reaches the user's desktop. A native placement
must survive commit without replacing the exact owned Wayland window/surface.
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
        with tempfile.TemporaryDirectory(prefix="deskflow-native-drag-") as directory:
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

            prepared = common.wait_for(processes, lambda: stage("prepared"), "painted drag preview", retry_errors=False)
            association = prepared["association"]
            placed, = provider.call("WindowDragPreview", "(sussttsii)",
                                    (session, generation, token, association, 1, 1, "place", 160, 90))
            require(placed.get("available") and (placed["x"], placed["y"]) == (160, 90),
                    f"Native preview placement failed: {placed}")
            provider.call("WindowDragPreview", "(sussttsii)",
                          (session, generation, token, association, 1, 1, "finish", 0, 0))
            before, = provider.call("GetViewer", "(sus)", (session, generation, token))
            report["before"] = {"viewer": prepared, "native": before, "placement": placed}
            commands.write_text("commit")
            committed = common.wait_for(processes, lambda: stage("committed"), "painted committed viewer",
                                        retry_errors=False)
            after, = provider.call("GetViewer", "(sus)", (session, generation, token))
            report["after"] = {"viewer": committed, "native": after}
            require(all(prepared[key] == committed[key] for key in ("association", "surface", "resource")),
                    "Commit replaced the painted Wayland surface or association")
            require(all(before.get(key) and before[key] == after.get(key) for key in ("window", "surface", "frameRect")),
                    "Commit replaced or moved the native preview window")
            provider.call("WatchWindowDrag", "(sussb)", (session, generation, token, association, True))
            provider.call("WatchWindowDrag", "(sussb)", (session, generation, token, association, False))
            report["committedWindowCanBeWatched"] = True
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
