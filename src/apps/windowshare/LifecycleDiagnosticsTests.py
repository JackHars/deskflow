# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Exercise the real lifecycle header with private full/closed stderr pipes."""
import json
import os
import subprocess
import sys


def normal(binary):
    result = subprocess.run([binary], capture_output=True, timeout=10, check=True)
    assert result.stdout == b'producer-complete\n'
    rows = [json.loads(line.removeprefix(b'DESKFLOW_STEP ')) for line in result.stderr.splitlines()]
    assert len(rows) == 64
    assert [row['details']['index'] for row in rows] == list(range(64))
    assert all(row['component'] == 'Diagnostic fixture' and row['pid'] > 0 and
               row['utc'] and row['threadId'] and int(row['monotonicNs']) > 0 for row in rows)


def unavailable_reader(binary, closed):
    read_fd, write_fd = os.pipe()
    try:
        if closed:
            os.close(read_fd)
            read_fd = None
        else:
            # Fill only this disposable pipe. No reader will drain it before
            # child exit; no timing assumption is needed to force backpressure.
            os.set_blocking(write_fd, False)
            try:
                while True:
                    os.write(write_fd, b'x' * 4096)
            except BlockingIOError:
                pass
            os.set_blocking(write_fd, True)
        with subprocess.Popen([binary, '--bounded'], stdout=subprocess.PIPE, stderr=write_fd) as child:
            try:
                output, _ = child.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill()
                child.communicate()
                raise AssertionError('Full diagnostic pipe blocked producer or shutdown') from None
            assert child.returncode == 0, ('diagnostic output terminated process', child.returncode)
            assert output == b'producer-complete\n'
    finally:
        os.close(write_fd)
        if read_fd is not None:
            os.close(read_fd)


if __name__ == '__main__':
    binary = sys.argv[1]
    normal(binary)
    unavailable_reader(binary, closed=False)
    unavailable_reader(binary, closed=True)
    print('Lifecycle producer metadata, full-pipe progress, bounded exit and closed-pipe survival pass')
