#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Unattended failure diagnosis for one explicitly prepared disposable pipeline.

This does not send input or accept H10. It terminates only the authenticated
experimental Host, Core or holding helper after observed hidden placement.
The ordinary coordinator retains its failed result and performs exact cleanup.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_coordinator():
    spec = importlib.util.spec_from_file_location(
        "recovery_physical_coordinator", Path(__file__).with_name("physical-macos.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def select_target(inspector, parent_pid, core_pid, core_path, host_path, fault, helper_path=None):
    """Recheck the exact process tree, including kernel birth identities."""
    require(fault in ("host-kill", "core-kill", "holding-helper-kill"), "Unknown fault")
    core = inspector.snapshot(core_pid)
    require(core is not None and core["parent"] == parent_pid and core["executable"] == core_path,
            "Core is not the coordinator's exact packaged child")
    host = inspector.child(core, host_path)
    require(host is not None and host["parent"] == core_pid and host["executable"] == host_path,
            "The exact packaged companion is unavailable")
    result = {"core": core, "host": host, "target": host if fault == "host-kill" else core}
    if fault == "holding-helper-kill":
        require(helper_path, "The frozen holding helper is required")
        helper = inspector.child(host, helper_path)
        require(helper is not None and helper["parent"] == host["pid"] and helper["executable"] == helper_path,
                "The exact holding helper is unavailable")
        result.update(helper=helper, target=helper)
    require(inspector.live(core) and inspector.live(host) and inspector.live(result["target"]),
            "Owned process identity changed")
    return result


def initial_observation(receipt, original):
    # The publisher can reach Ready before the fixture has published its first
    # native window snapshot. That is pending readiness, not a new identity.
    if (receipt.get("manual", {}).get("state") not in ("ready", "preparing")
            or type(receipt.get("sourceWindowId")) is not int or receipt["sourceWindowId"] <= 0
            or not original.source_ready_for_start(receipt, True)):
        return None
    original.source_identity(receipt, None, True)
    return receipt


def assess(report, pipeline):
    """A fault result cannot qualify as an ordinary pipeline or physical pass."""
    final = pipeline.get("finalSource", {}).get("receipt") or {}
    try:
        remote = json.loads(pipeline.get("remoteCleanup", "null"))
    except (TypeError, ValueError):
        return False
    expected_errors = {
        "Input evidence: Timed out waiting for evidence recorder stop",
        "core: Packaged Core/Host did not drain successfully",
    }
    errors = pipeline.get("cleanupErrors", [])
    return bool(
        report.get("faultSubmitted") is True and report.get("restorationObserved") is True
        and pipeline.get("pipelineCheck") is True
        and pipeline.get("pipelineSyntheticInputDeclared") is False
        and pipeline.get("sourceRelocationObserved") is True
        and pipeline.get("status") == "failed" and pipeline.get("failure")
        and pipeline.get("sourceRestored") is True
        and pipeline.get("companionStopped") is True
        and pipeline.get("localProcessesStopped") is True
        and pipeline.get("sourceExit") == 0
        and final.get("heldKeys") == 0 and final.get("heldButtons") == 0
        and isinstance(pipeline.get("ordinaryMacConfigBefore"), dict) and pipeline["ordinaryMacConfigBefore"]
        and pipeline["ordinaryMacConfigBefore"] == pipeline.get("ordinaryMacConfigAfter")
        and isinstance(remote, dict) and remote.get("ordinaryPreserved") is True
        and remote.get("ownedProcessesStopped") is True and remote.get("desktopRestored") is True
        and isinstance(errors, list) and all(error in expected_errors for error in errors)
        and pipeline.get("coreExit") == (-signal.SIGKILL if report.get("fault") == "core-kill" else 0)
    )


def run(args):
    require(sys.platform == "darwin", "This diagnostic requires the actual Mac host")
    os.umask(0o077)
    coordinator = load_coordinator()
    original = coordinator.original_support()
    mac = coordinator.read_json(args.mac_plan)
    linux = coordinator.read_json(args.linux_plan)
    base = args.mac_plan.parent.resolve(strict=True)
    artifacts = coordinator.validate_plans(mac, linux, base, True, None, True)
    require(mac.get("recordingProfile", "h04") == "h04", "Use a prepared H04 pipeline")
    require(mac.get("linuxEnvironment") == "user-desktop", "The actual Ubuntu desktop is required")
    require(args.ordinary_mac_config, "Declare the ordinary Mac configurations to audit")
    output = args.output.resolve()
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    session = output / "session"
    argv = [sys.executable, str(Path(__file__).with_name("original-input-physical.py")),
            "--mac-plan", str(args.mac_plan.resolve()), "--linux-plan", str(args.linux_plan.resolve()),
            "--ssh-target", args.ssh_target, "--ssh-key", str(args.ssh_key.resolve()),
            "--output", str(session), "--holding-display", "--pipeline-check"]
    for path in args.ordinary_mac_config:
        argv += ["--ordinary-mac-config", str(path.resolve())]
    report = {"schema": "deskflow-pipeline-recovery-v1", "fault": args.fault,
              "diagnosticOnly": True, "physicalAcceptance": "not-assessed",
              "originalInputAcceptance": "not-assessed", "qualifiedPipelinePass": False,
              "faultSubmitted": False, "restorationObserved": False, "recoveryObserved": False,
              "macPlanSha256": coordinator.digest(args.mac_plan),
              "linuxPlanSha256": coordinator.digest(args.linux_plan),
              "procedureSha256": coordinator.digest(Path(__file__))}
    # Retain the exact diagnostic procedure beside its result, including
    # failed attempts, even if later development changes this source file.
    with (output / "procedure.py").open("xb") as saved:
        saved.write(Path(__file__).read_bytes())
    require(coordinator.digest(output / "procedure.py") == report["procedureSha256"],
            "Diagnostic source changed while freezing")
    coordinator.atomic_json(output / "argv.json", argv)
    inspector = original.ProcessInspector()
    process = None
    fault_at = None
    core_pid = None
    initial = None
    identity = None
    pending = b""
    started = time.monotonic()
    # Preparation already has its own unchanged deadlines. This outer limit
    # bounds the complete diagnostic, including coordinator cleanup.
    deadline = started + 180
    try:
        with (output / "coordinator.log").open("xb") as log, selectors.DefaultSelector() as selector:
            process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            os.set_blocking(process.stdout.fileno(), False)
            selector.register(process.stdout, selectors.EVENT_READ)
            while selector.get_map():
                require(time.monotonic() < deadline, "Recovery diagnostic exceeded its outer deadline")
                for key, _ in selector.select(min(0.25, max(0, deadline - time.monotonic()))):
                    block = os.read(key.fd, 65536)
                    if not block:
                        selector.unregister(key.fileobj)
                        continue
                    log.write(block)
                    log.flush()
                    pending += block
                    require(len(pending) <= 262144, "Coordinator output line exceeded its bound")
                    while b"\n" in pending:
                        line, pending = pending.split(b"\n", 1)
                        try:
                            event = json.loads(line)
                        except (ValueError, UnicodeDecodeError):
                            continue  # Raw diagnostic output remains in the log.
                        if not isinstance(event, dict):
                            continue
                        diagnostic = event.get("diagnostic", {})
                        if isinstance(diagnostic, dict) and diagnostic.get("step") == "core process":
                            value = diagnostic.get("details", {}).get("value", {})
                            require(core_pid is None, "Coordinator published a second Core process")
                            require(value.get("log") == str(session / "core.log"), "Unexpected Core log owner")
                            core_pid = value.get("pid")
                            require(type(core_pid) is int and core_pid > 0, "Invalid owned Core PID")
                        if event.get("event") != "active":
                            continue
                        require(not report["faultSubmitted"] and core_pid is not None,
                                "Active was duplicated or preceded Core ownership")
                        receipt = coordinator.read_json(session / "source.json")
                        original.source_identity(receipt, initial, True)
                        require(initial is not None and receipt.get("heldKeys") == receipt.get("heldButtons") == 0,
                                "Fault requires a known source with no held input")
                        require(receipt["frame"] != initial["frame"], "Source was not relocated")
                        for role in ("core", "host", "holdingHelper"):
                            require(coordinator.digest(Path(artifacts[role]["path"])) == artifacts[role]["sha256"],
                                    "A packaged executable changed before the fault")
                        identity = select_target(inspector, process.pid, core_pid,
                                                 artifacts["core"]["path"], artifacts["host"]["path"], args.fault,
                                                 artifacts["holdingHelper"]["path"])
                        report["identities"] = identity
                        report["beforeFault"] = {"frame": receipt["frame"], "initialFrame": initial["frame"],
                                                 "sourcePid": receipt["pid"], "heldKeys": 0, "heldButtons": 0}
                        require(process.poll() is None and inspector.live(identity["target"]),
                                "Owner exited before the fault")
                        fault_at = time.monotonic()
                        os.kill(identity["target"]["pid"], signal.SIGKILL)
                        report["faultSubmitted"] = True
                        coordinator.atomic_json(output / "fault.json", report)
                        print(json.dumps({"step": "owned-fault-submitted", "fault": args.fault,
                                          "pid": identity["target"]["pid"]}), flush=True)
                source = session / "source.json"
                if source.exists():
                    receipt = coordinator.read_json(source)
                    if initial is None:
                        initial = initial_observation(receipt, original)
                    if fault_at is not None and not report["restorationObserved"]:
                        require(receipt["pid"] == initial["pid"] and
                                receipt["sourceWindowId"] == initial["sourceWindowId"], "Source owner changed")
                        journal = session / "restore.json"
                        if (receipt["frame"] == initial["frame"] and receipt["heldKeys"] == receipt["heldButtons"] == 0
                                and journal.exists() and coordinator.read_json(journal) == {"version": 1, "windows": []}
                                and not inspector.live(identity["target"])
                                and (args.fault == "holding-helper-kill" or not inspector.live(identity["host"]))):
                            report["restorationObserved"] = True
                            report["restorationObservedMs"] = round((time.monotonic() - fault_at) * 1000, 3)
                            report["hostAliveAtRestoration"] = inspector.live(identity["host"])
                            print(json.dumps({"step": "restoration-observed",
                                              "milliseconds": report["restorationObservedMs"]}), flush=True)
            require(not pending, "Coordinator ended with an incomplete diagnostic line")
            report["coordinatorExit"] = process.wait(timeout=15)
    except Exception as error:
        report["error"] = str(error)
    finally:
        if process and process.poll() is None:
            # The owned coordinator handles SIGTERM by restoring its source and
            # stopping its exact children. Never kill an unknown process group.
            process.terminate()
            try:
                report["coordinatorExit"] = process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                report["cleanupUnverified"] = True
        if process and process.stdout:
            process.stdout.close()
        result = session / "result.json"
        if result.exists():
            pipeline = coordinator.read_json(result)
            report["pipelineResultSha256"] = coordinator.digest(result)
            report["pipelineStatus"] = pipeline.get("status")
            report["pipelineFailure"] = pipeline.get("failure")
            report["pipelineCleanupErrors"] = pipeline.get("cleanupErrors")
            report["recoveryObserved"] = not report.get("error") and assess(report, pipeline)
        report["elapsedSeconds"] = round(time.monotonic() - started, 3)
        coordinator.atomic_json(output / "recovery.json", report)
    print(json.dumps(report), flush=True)
    return 0 if report["recoveryObserved"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mac-plan", type=Path, required=True)
    parser.add_argument("--linux-plan", type=Path, required=True)
    parser.add_argument("--ssh-target", required=True)
    parser.add_argument("--ssh-key", type=Path, required=True)
    parser.add_argument("--ordinary-mac-config", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fault", choices=("host-kill", "core-kill", "holding-helper-kill"), required=True)
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
