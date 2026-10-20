#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Terminate only one prepared disposable Ubuntu Viewer for recovery diagnosis.

Called by pipeline-recovery.py after the Mac observes Active hidden placement.
No input is sent, and no ordinary process or desktop compositor is signalled.
"""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import selectors
import signal
import sys


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def validate_target(plan, lease, expected, current, parent_pid, boot_id):
    require(plan.get("linuxEnvironment") == "user-desktop"
            and plan.get("routeMode") == "production-original-input",
            "The actual production desktop plan is required")
    require(lease.get("token") == plan["token"] and lease.get("before") == plan["desktopAtPrepare"]
            and lease.get("restored") is False, "No matching active disposable desktop lease")
    require(boot_id == plan["desktopAtPrepare"]["bootId"], "The prepared machine restarted")
    require(set(expected) == {"pid", "startTicks", "executable"}
            and type(expected["pid"]) is int and expected["pid"] > 1
            and expected["executable"] == plan["viewerExecutable"]
            and Path(expected["executable"]).name == "deskflow-windowshare",
            "The receipt is not the exact prepared Viewer")
    require(all(current.get(key) == value for key, value in expected.items()),
            "Viewer lifetime or executable changed")
    require(parent_pid == plan["desktopAtPrepare"]["shell"]["pid"],
            "The Viewer is not the prepared compositor's child")


def signal_owned_viewer(desktop, expected):
    # A PID can be reused between discovery and signalling. Open its kernel
    # lifetime handle first, then recheck the complete observed identity.
    descriptor = os.pidfd_open(expected["pid"])
    try:
        require(desktop.same_process(expected), "Viewer changed before the fault")
        signal.pidfd_send_signal(descriptor, signal.SIGKILL)
        with selectors.DefaultSelector() as observer:
            observer.register(descriptor, selectors.EVENT_READ)
            require(observer.select(5), "The killed Viewer did not exit within five seconds")
    finally:
        os.close(descriptor)


def run(args):
    require(sys.platform == "linux", "This helper requires the actual Ubuntu host")
    os.umask(0o077)
    require(args.plan.name == "linux-plan.json" and args.plan.is_absolute(), "An absolute prepared plan is required")
    require(hashlib.sha256(args.plan.read_bytes()).hexdigest() == args.expected_plan_sha256,
            "The remote plan differs from the approved Mac copy")
    spec = importlib.util.spec_from_file_location("recovery_linux_plan", Path(__file__).with_name("physical-linux.py"))
    api = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(api)
    plan = api.load_plan(args.plan.parent)
    require(api.source_hashes() == plan["sourceHashes"], "Prepared harness sources changed")
    require({role: api.sha(Path(plan[role + "Executable"])) for role in ("core", "viewer")} == plan["binaryHashes"],
            "Prepared runtime executables changed")
    desktop = api.desktop_support()
    require(desktop.same_process(plan["desktopAtPrepare"]["shell"]), "The actual compositor changed")
    lease = api.read_owned_json(args.plan.parent / "desktop-lease.json")
    expected = api.read_owned_json(args.plan.parent / "viewer-process.json")
    current = desktop.identity(expected["pid"])
    parent = int((Path("/proc") / str(expected["pid"]) / "stat").read_text().rsplit(")", 1)[1].split()[1])
    validate_target(plan, lease, expected, current, parent, Path("/proc/sys/kernel/random/boot_id").read_text().strip())
    report = {"schema": "deskflow-viewer-recovery-fault-v1", "fault": "viewer-kill",
              "diagnosticOnly": True, "planSha256": args.expected_plan_sha256,
              "identity": current, "targetExited": False}
    # An exclusive intent records which exact process this attempt may signal
    # and prevents a duplicate invocation from killing a replacement Viewer.
    with (args.plan.parent / "viewer-fault-intent.json").open("x") as output:
        json.dump(report, output)
        output.write("\n")
    signal_owned_viewer(desktop, current)
    report["targetExited"] = True
    desktop.atomic(args.plan.parent / "viewer-fault-result.json", report)
    print(json.dumps(report), flush=True)
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--expected-plan-sha256", required=True)
    raise SystemExit(run(parser.parse_args()))
