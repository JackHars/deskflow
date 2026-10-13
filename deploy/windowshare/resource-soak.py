#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Read-only, bounded process resource evidence for physical windowshare H11.

Run separately on macOS and Ubuntu. Monotonic CPU sampling never asserts that
capture, transport, compositor, Qt paint or target application input completed.
No input events, command lines, paths to executables or clipboard contents are
collected. Output is exclusive, mode 0600 and never follows an output symlink.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from typing import Mapping

MAX_TARGETS = 8
MAX_SAMPLES = 25000
# ps on macOS reports one-second CPU totals; >=1 second yields measurable deltas.
MIN_INTERVAL = 1.0 if sys.platform == 'darwin' else 0.25
MAX_INTERVAL = 30.0
MAX_DURATION = 24 * 60 * 60
_TARGET = re.compile(r'^[a-z][a-z0-9_-]{0,30}$')


class LostProcess(RuntimeError):
    """The original process vanished (never follow a reused PID)."""


class ChangedProcess(RuntimeError):
    """A PID has been rebound, possibly to an unrelated process."""


@dataclass(frozen=True)
class Reading:
    birth: str
    rss_bytes: int
    cpu_seconds: float
    io_read_bytes: int | None = None
    io_write_bytes: int | None = None


def parse_linux_stat(text: str, pagesize: int, ticks_per_second: int) -> Reading:
    # The task name in Linux /proc/PID/stat is between parentheses and may
    # contain arbitrary whitespace or closing-parenthesis characters.
    ending = text.rfind(') ')
    if ending < 1 or not text[:text.find(' (')].isdigit():
        raise ValueError('Malformed Linux process stat')
    fields = text[ending + 2:].split()
    # Index zero is field 3 (state); kernel docs: utime 14, stime 15,
    # starttime 22, resident set size 24.
    if len(fields) < 22:
        raise ValueError('Incomplete Linux process stat')
    cpu = (int(fields[11]) + int(fields[12])) / ticks_per_second
    birth = fields[19]
    rss = int(fields[21]) * pagesize
    if not birth.isdigit() or not int(birth) or rss < 0 or cpu < 0:
        raise ValueError('Invalid Linux process lifetime or usage')
    return Reading('linux-ticks:' + birth, rss, cpu)


def probe_linux(pid: int) -> Reading:
    stat = Path(f'/proc/{pid}/stat').read_text(encoding='ascii')
    original = parse_linux_stat(stat, os.sysconf('SC_PAGE_SIZE'), os.sysconf('SC_CLK_TCK'))
    reads, writes = None, None
    try:
        for line in Path(f'/proc/{pid}/io').read_text(encoding='ascii').splitlines():
            key, _, value = line.partition(':')
            if key == 'read_bytes':
                reads = int(value)
            elif key == 'write_bytes':
                writes = int(value)
    except (FileNotFoundError, PermissionError):
        # Disk-I/O counters are optional. A vanished process is detected by
        # comparing the process' kernel birth identity again immediately.
        pass
    actual = parse_linux_stat(Path(f'/proc/{pid}/stat').read_text(encoding='ascii'),
                              os.sysconf('SC_PAGE_SIZE'), os.sysconf('SC_CLK_TCK'))
    if actual.birth != original.birth:
        raise ChangedProcess(f'PID {pid} changed identity during sampling')
    return Reading(actual.birth, actual.rss_bytes, actual.cpu_seconds, reads, writes)


def mac_cpu_seconds(cpu: str) -> float:
    # ps -o time= is cumulative CPU time. It supports "HH:MM:SS" and
    # "days-HH:MM:SS"; never use a wall-clock delta from separate computers.
    segments = cpu.split('-', 1)
    days, clock = (int(segments[0]), segments[1]) if len(segments) == 2 else (0, segments[0])
    chunks = clock.split(':')
    if len(chunks) not in (2, 3) or any(not part.isdigit() for part in chunks[:-1]):
        raise ValueError('Invalid ps CPU clock')
    try:
        seconds = float(chunks[-1]) + sum(int(p) * 60**i for i, p in enumerate(reversed(chunks[:-1]), 1))
    except ValueError as error:
        raise ValueError('Invalid ps CPU clock') from error
    if not math.isfinite(seconds) or seconds < 0:
        raise ValueError('Invalid ps CPU clock')
    return days * 86400 + seconds


def parse_macos_ps(line: str) -> Reading:
    # ps -o lstart= -o rss= -o time= -p PID under LC_ALL=C:
    # Wed Oct 14 01:02:03 2026    14628   01:23:45
    fields = line.strip().split()
    if len(fields) != 7:
        raise ValueError('Malformed Mac process sample')
    # Reject truncated/unparseable birth timestamps and PID reuse. macOS ps
    # reports seconds resolution; this is a diagnostic, not a trust/permit.
    born = datetime.strptime(' '.join(fields[:5]), '%a %b %d %H:%M:%S %Y')
    rss = int(fields[5]) * 1024
    cpu = mac_cpu_seconds(fields[6])
    if rss < 0 or cpu < 0:
        raise ValueError('Invalid Mac process resource sample')
    return Reading('mac-ps-seconds:' + born.isoformat(), rss, cpu)


def probe_macos(pid: int) -> Reading:
    result = subprocess.run(['ps', '-o', 'lstart=', '-o', 'rss=', '-o', 'time=', '-p', str(pid)],
                            capture_output=True, text=True, check=False,
                            env={**os.environ, 'LC_ALL': 'C'}, timeout=3)
    if result.returncode != 0 or not result.stdout.strip():
        raise LostProcess(f'PID {pid} is no longer observable by ps')
    return parse_macos_ps(result.stdout)


def probe(pid: int) -> Reading:
    try:
        return probe_macos(pid) if sys.platform == 'darwin' else probe_linux(pid)
    except FileNotFoundError as error:
        raise LostProcess(f'PID {pid} is no longer present') from error


def target(value: str) -> tuple[str, int]:
    name, separator, number = value.partition('=')
    if not separator or not _TARGET.fullmatch(name) or not number.isdecimal():
        raise argparse.ArgumentTypeError('Targets must be name=positive_pid (ASCII lowercase names)')
    pid = int(number)
    if pid < 1 or pid > 2**31 - 1:
        raise argparse.ArgumentTypeError('PID outside supported range')
    return name, pid


def positive_seconds(value: str) -> float:
    try:
        number = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError('Expected seconds') from error
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError('Seconds must be positive and finite')
    return number


def percentile(samples: list[float], portion: float) -> float | None:
    if not samples:
        return None
    ordered = sorted(samples)
    rank = max(0, math.ceil(portion * len(ordered)) - 1)
    return round(ordered[rank], 2)


def summarize(name: str, readings: list[tuple[float, Reading]]) -> dict:
    start, first = readings[0]
    finish, last = readings[-1]
    elapsed = finish - start
    cpu_percent = [max(0.0, (b.cpu_seconds - a.cpu_seconds) * 100 / (tb - ta))
                   for (ta, a), (tb, b) in zip(readings, readings[1:]) if tb > ta]
    return {
        'name': name,
        'samples': len(readings),
        'peak_rss_bytes': max(item.rss_bytes for _, item in readings),
        'rss_change_bytes': last.rss_bytes - first.rss_bytes,
        'cpu_percent_p50': percentile(cpu_percent, .5),
        'cpu_percent_p95': percentile(cpu_percent, .95),
        'cpu_percent_max': percentile(cpu_percent, 1),
        'average_cpu_percent': round(max(0., (last.cpu_seconds-first.cpu_seconds)) * 100/elapsed,2)
        if elapsed else None,
        'process_lifetime_changed': False,
    }


def measure(pids: Mapping[str, int], duration: float, interval: float, output: Path) -> int:
    if not output.is_absolute():
        raise ValueError('Require absolute, explicit evidence file path')
    # Verify all requested *existing* process lifetimes before creating the
    # output. Every sample rechecks the same birth identity; no orphan adopts
    # another process that happens to reuse a numeric PID.
    originals = {name: probe(pid) for name, pid in pids.items()}
    fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    history: dict[str, list[tuple[float, Reading]]] = {name: [] for name in pids}
    started = time.monotonic()
    exit_code = 0
    with os.fdopen(fd, 'w', encoding='utf-8', buffering=1) as stream:
        def event(obj: dict) -> None:
            stream.write(json.dumps(obj, separators=(',', ':'), sort_keys=True, allow_nan=False) + '\n')
        event({'event': 'start', 'format': 'deskflow-resource-soak-v1',
               'created_utc': datetime.now(timezone.utc).isoformat(),
               'platform': sys.platform, 'duration_s': duration, 'interval_s': interval,
               'identity_precision': 'linux-kernel-ticks' if sys.platform != 'darwin' else 'mac-ps-seconds',
               'targets': {name: {'pid': pid, 'birth': originals[name].birth} for name,pid in pids.items()},
               'interpretation': 'passive CPU/RSS/disk I/O only; no input or frame latency measured'})
        try:
            while True:
                now = time.monotonic()
                elapsed = now - started
                snapshots = {}
                for name, pid in pids.items():
                    reading = probe(pid)
                    if reading.birth != originals[name].birth:
                        raise ChangedProcess(f'{name} PID {pid} no longer matches its recorded birth')
                    history[name].append((now, reading))
                    snapshots[name] = {
                        'rss_bytes': reading.rss_bytes, 'cpu_seconds': reading.cpu_seconds,
                        'disk_read_bytes': reading.io_read_bytes, 'disk_write_bytes': reading.io_write_bytes}
                event({'event': 'sample', 'elapsed_s': round(elapsed, 4), 'targets': snapshots})
                if elapsed >= duration:
                    break
                if len(history[next(iter(pids))]) >= MAX_SAMPLES:
                    raise RuntimeError('Sampler exhausted its hard sample capacity')
                # Never busy-loop or pile up samples when the host is briefly
                # descheduled. Skip late slots; never expand the run deadline.
                next_due = started + min(duration, (math.floor(elapsed / interval) + 1) * interval)
                time.sleep(max(0, next_due - time.monotonic()))
        except (LostProcess, ChangedProcess, OSError, ValueError, RuntimeError) as error:
            exit_code = 3
            event({'event': 'failure', 'elapsed_s': round(time.monotonic() - started, 4),
                   'reason': type(error).__name__, 'detail': str(error)[:256]})
        except KeyboardInterrupt:
            exit_code = 130
            event({'event': 'interrupted', 'elapsed_s': round(time.monotonic() - started, 4)})
        event({'event': 'summary', 'elapsed_s': round(time.monotonic()-started,4),
               'status': 'complete' if exit_code == 0 else 'incomplete',
               'targets': [summarize(name, values) for name, values in history.items() if values],
               'limits': ['macOS cumulative CPU time has one-second resolution',
                          'No cross-machine clock correlation or latency proof',
                          'No GPU, frame, network or compositor sampling']})
    return exit_code


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=target, action='append', required=True,
                        metavar='NAME=PID', help='one to eight known host/viewer/core process IDs')
    parser.add_argument('--duration', type=positive_seconds, default=3600.0,
                        help='soak duration in seconds, max 24 hours (default: 3600)')
    parser.add_argument('--interval', type=positive_seconds, default=2.0,
                        help='sample interval, >=0.25 Linux or >=1 macOS (default: 2)')
    parser.add_argument('--output', type=Path, required=True,
                        help='exclusive absolute JSONL evidence path, never overwritten')
    args = parser.parse_args(argv)
    if len(args.pid) > MAX_TARGETS or len(set(args.pid)) != len(args.pid) or \
       len({name for name,_ in args.pid}) != len(args.pid) or \
       len({pid for _,pid in args.pid}) != len(args.pid):
        parser.error('Require one to eight unique names and unique process IDs')
    if args.duration > MAX_DURATION or not MIN_INTERVAL <= args.interval <= MAX_INTERVAL or \
       math.ceil(args.duration/args.interval) + 1 > MAX_SAMPLES:
        parser.error('Unbounded sample budget: adjust duration/interval within limits')
    try:
        return measure(dict(args.pid), args.duration, args.interval, args.output)
    except (OSError, ValueError, LostProcess, ChangedProcess) as error:
        print(f'Resource soak could not start: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
