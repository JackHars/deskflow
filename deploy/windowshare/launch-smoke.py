#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Exercise the packaged core/host handshake without native hooks or listeners.

No ordinary server or window host is started. Rejected option combinations are
chosen to fail in bootstrap before normal core or NativeHost initialization.
"""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile


def run(executable, arguments, *, success, descriptors=(), rejection=None):
    environment = os.environ.copy()
    for key in list(environment):
        if key.startswith("DYLD_") or key in {"QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                                              "QT_QML_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"}:
            del environment[key]
    environment["QT_QPA_PLATFORM"] = "offscreen"
    result = subprocess.run([str(executable), *arguments], env=environment,
                            pass_fds=descriptors, capture_output=True, text=True, timeout=45)
    if result.returncode < 0 or (result.returncode == 0) != success:
        raise RuntimeError(f"Unexpected exit {result.returncode}: {result.stdout[-1000:]} {result.stderr[-1000:]}")
    if rejection is not None and rejection not in result.stderr:
        raise RuntimeError(f"Expected bootstrap rejection was missing: {rejection}")
    return result


def main():
    if len(sys.argv) != 2:
        raise RuntimeError("Usage: launch-smoke.py CORE_BUNDLE.app")
    bundle = Path(sys.argv[1]).resolve()
    core = bundle / "Contents/MacOS/deskflow-core"
    host = bundle / "Contents/MacOS/deskflow-windowshare"
    result = run(core, ["--windowshare-ipc-probe"], success=True)
    report = json.loads(result.stdout)
    if report != {"privateInputHandshake": True, "companionStopped": True, "originalInputEnabled": False}:
        raise RuntimeError("Probe did not confirm a handshake and clean stop with input disabled")

    for arguments in (["--windowshare-ipc-probe", "server"],
                      ["--windowshare-ipc-probe", "--new-instance"],
                      ["-windowshare-ipc-probe"],
                      ["--windowshare-ipc-probe=true"],
                      ["client", "--windowshare-host-config=/nonexistent"],
                      ["--help", "--windowshare-host-config=/nonexistent"],
                      ["--windowshare-host-config=/a", "--windowshare-host-config=/b"]):
        run(core, arguments, success=False)
    run(host, ["--core-input-probe"], success=False)
    run(host, ["--core-input-fd=2", "--core-input-probe"], success=False)
    with tempfile.TemporaryDirectory(prefix="deskflow-launch-smoke-") as temporary:
        identity = Path(temporary) / "must-not-be-created"
        policy = "--core-input-peer-pairs=" + json.dumps([
            {"deskflowPeerPin": "12" * 32, "windowSharePeerPin": "34" * 32}
        ], separators=(",", ":"))
        # Public pins may be passed only by the authenticated inherited core
        # launch. A standalone policy option must fail even beside an action
        # that would otherwise write an identity or bypass normal startup.
        for arguments in ([policy, "--init-identity", "--identity", str(identity)],
                          [policy, "--help"]):
            run(host, arguments, success=False,
                rejection="Input peer pairs require a non-probe inherited companion")
            if identity.exists():
                raise RuntimeError("Standalone policy injection created an identity")
        first, second = socket.socketpair()
        with first, second:
            descriptor = second.fileno()
            # Python's PID/argv and possession of a socketpair are not code trust.
            result = run(host, [f"--core-input-fd={descriptor}", "--core-input-probe"],
                         success=False, descriptors=(descriptor,))
            if "packaged-code verification" not in result.stderr:
                raise RuntimeError("Untrusted parent was not rejected by code verification")
            run(host, [f"--core-input-fd={descriptor}", "--core-input-probe", policy],
                success=False, descriptors=(descriptor,),
                rejection="Private input probe requires only its inherited descriptor")
            run(host, [f"--core-input-fd={descriptor}", "--init-identity", "--identity", str(identity)],
                success=False, descriptors=(descriptor,))
            run(host, [f"--core-input-fd={descriptor}", policy, "--init-identity", "--identity", str(identity)],
                success=False, descriptors=(descriptor,), rejection="Invalid companion host option")
            if identity.exists():
                raise RuntimeError("An inherited descriptor bypassed bootstrap action validation")
    print("Packaged core/host handshake and bootstrap rejection smoke passed; original input remained disabled")


if __name__ == "__main__":
    main()
