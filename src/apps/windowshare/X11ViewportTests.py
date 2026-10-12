#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Run native observations on disposable authenticated Xvfb servers only."""

import argparse
import os
import secrets
import selectors
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xvfb", required=True)
    parser.add_argument("--xauth", required=True)
    parser.add_argument("--scale", required=True)
    parser.add_argument("binary")
    args = parser.parse_args()
    processes = []
    logs = []
    try:
        with tempfile.TemporaryDirectory(prefix="deskflow-x11-observation-") as directory:
            auth = str(Path(directory) / "Xauthority")
            cookie = secrets.token_hex(16)
            subprocess.run([args.xauth, "-f", auth, "add", ":999", ".", cookie], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            displays = []
            for index in range(2):
                read_fd, write_fd = os.pipe()
                log_path = Path(directory) / f"xvfb-{index}.log"
                with log_path.open("wb") as log:
                    process = subprocess.Popen(
                        [args.xvfb, "-displayfd", str(write_fd), "-auth", auth, "-nolisten", "tcp",
                         "-screen", "0", "1280x1024x24", "-screen", "1", "1280x1024x24", "-noreset"],
                        pass_fds=(write_fd,), stdout=log, stderr=log)
                processes.append(process)
                logs.append(log_path)
                os.close(write_fd)
                try:
                    with selectors.DefaultSelector() as selector:
                        selector.register(read_fd, selectors.EVENT_READ)
                        if not selector.select(10):
                            raise RuntimeError("Private Xvfb did not report a display within 10 seconds")
                        number = os.read(read_fd, 64).decode("ascii").strip()
                    if not number.isdecimal() or process.poll() is not None:
                        raise RuntimeError("Private Xvfb failed to create a display")
                finally:
                    os.close(read_fd)
                display = ":" + number
                subprocess.run([args.xauth, "-f", auth, "add", display, ".", cookie], check=True,
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                displays.append(display)
            environment = os.environ.copy()
            environment.update(DISPLAY=displays[0], XAUTHORITY=auth, QT_QPA_PLATFORM="xcb",
                               QT_SCALE_FACTOR=args.scale, QT_XCB_GL_INTEGRATION="none",
                               DESKFLOW_X11_TEST_PRIVATE="1")
            environment.pop("WAYLAND_DISPLAY", None)
            try:
                result = subprocess.run([args.binary, "--other-display=" + displays[1]], env=environment, timeout=30)
                if result.returncode:
                    for path in logs:
                        print(path.name + ":\n" + path.read_text(errors="replace"), flush=True)
                return result.returncode
            finally:
                for process in processes:
                    if process.poll() is None:
                        process.terminate()
                for process in processes:
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3)
    finally:
        # Also reap partial startup when the second private server fails.
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=3)


if __name__ == "__main__":
    raise SystemExit(main())
