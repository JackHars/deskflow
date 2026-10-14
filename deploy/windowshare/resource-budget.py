#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Offline evaluation of v2 resource samples against the fixed H11 pilot budgets.

No processes are started or sampled. A resource component PASS never completes
H11 or verifies native measurement provenance. Short/legacy/interrupted traces
remain INCOMPLETE. Thresholds cannot be overridden on the command line.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import sys

spec = importlib.util.spec_from_file_location('resource_evidence_io', Path(__file__).with_name('h11-evidence.py'))
io = importlib.util.module_from_spec(spec)
spec.loader.exec_module(io)
REVISION = 'pilot-20261013-v1'
MIB = 1024 * 1024
BUDGETS = {'host': (1024 * MIB, 200), 'viewer': (1024 * MIB, 200), 'core': (512 * MIB, 75)}
GROWTH_PER_HOUR = 100 * MIB
NAME = re.compile(r'[a-z][a-z0-9_-]{0,30}\Z')


def number(value, name, low=0, high=1e20):
    io.require(type(value) in (int, float) and math.isfinite(value) and low <= value <= high,
               'invalid ' + name)
    return value


def analyze(path: Path, roles: dict[str, str] | None = None) -> dict:
    report = {'format': 'deskflow-resource-budget-v1', 'status': 'INCOMPLETE',
              'budgetRevision': REVISION, 'h11_complete': False, 'measurement_authenticity_verified': False,
              'workload_verified': False,
              'limits': ['Sampled RSS, not every allocation or true peak',
                         'No GPU, network, frame, input or cross-machine latency evidence']}
    try:
        io.require(not path.is_symlink(), 'resource trace may not be a symlink')
        with io.open_regular(path) as stream:
            before = os.fstat(stream.fileno())
            data = stream.read(32 * MIB + 1)
            after = os.fstat(stream.fileno())
        io.require((before.st_size, before.st_mtime_ns, before.st_ctime_ns) ==
                   (after.st_size, after.st_mtime_ns, after.st_ctime_ns), 'trace changed during read')
        io.require(len(data) <= 32 * MIB and data.endswith(b'\n'), 'oversized or truncated resource trace')
        report['traceSha256'] = hashlib.sha256(data).hexdigest()
        lines = data.splitlines()
        io.require(4 <= len(lines) <= 25002 and all(0 < len(line) <= 16384 for line in lines),
                   'resource row budget or boundaries missing')
        rows = [io.decode_json(line) for line in lines]
        start, summary = rows[0], rows[-1]
        io.keys(start, {'event', 'format', 'created_utc', 'platform', 'duration_s', 'interval_s',
                        'identity_precision', 'cpu_resolution_s', 'targets', 'interpretation'}, 'resource start')
        io.require(start['event'] == 'start' and start['format'] == 'deskflow-resource-soak-v2',
                   'resource v2 timestamps required; legacy evidence is incomplete')
        io.require(start['platform'] in ('darwin', 'linux'), 'unsupported resource platform')
        precision = 'mac-ps-seconds' if start['platform'] == 'darwin' else 'linux-kernel-ticks'
        io.require(start['identity_precision'] == precision, 'resource process identity precision mismatch')
        duration = number(start['duration_s'], 'planned duration', 0.25, 86400)
        interval = number(start['interval_s'], 'sample interval', 1 if start['platform'] == 'darwin' else .25, 30)
        io.require(len(rows) - 2 == math.ceil(duration / interval) + 1,
                   'planned resource sampling slots are missing or duplicated')
        resolution = number(start['cpu_resolution_s'], 'CPU resolution', 1e-9, 1)
        io.require(start['platform'] != 'darwin' or resolution == 1, 'Mac CPU resolution must retain one second')
        targets = start['targets']
        io.require(type(targets) is dict and 1 <= len(targets) <= 8, 'invalid resource targets')
        roles = roles if roles is not None else {name: name for name in targets}
        io.require(set(roles) == set(targets) and all(role in BUDGETS for role in roles.values()),
                   'supply exactly one known host/viewer/core role per target')
        pids = set()
        for name, identity in targets.items():
            io.require(NAME.fullmatch(name) is not None, 'invalid resource target name')
            io.keys(identity, {'pid', 'birth'}, 'resource identity')
            pid = io.integer(identity['pid'], 'resource pid', 1, 2**31-1)
            io.require(pid not in pids and type(identity['birth']) is str and 1 <= len(identity['birth']) <= 96,
                       'duplicate PID or invalid birth identity')
            pids.add(pid)
        io.keys(summary, {'event', 'elapsed_s', 'status', 'targets', 'limits'}, 'resource summary')
        io.require(summary['event'] == 'summary' and summary['status'] == 'complete',
                   'interrupted or incomplete resource collection')
        histories = {name: [] for name in targets}
        elapsed = -1
        for row in rows[1:-1]:
            io.keys(row, {'event', 'elapsed_s', 'targets'}, 'resource sample')
            io.require(row['event'] == 'sample' and type(row['targets']) is dict and set(row['targets']) == set(targets),
                       'resource sample identity or event changed')
            current = number(row['elapsed_s'], 'sample time', 0, 86500)
            io.require(current > elapsed and current - max(0, elapsed) <= 2 * interval + .001,
                       'missing, reordered or late resource sampling slot')
            elapsed = current
            for name, sample in row['targets'].items():
                io.keys(sample, {'observed_start_s', 'observed_end_s', 'rss_bytes', 'cpu_seconds',
                                 'disk_read_bytes', 'disk_write_bytes'}, 'resource reading')
                left = number(sample['observed_start_s'], 'reading start', max(0, current - .0001), 86500)
                right = number(sample['observed_end_s'], 'reading end', left, left + 3.1)
                io.integer(sample['rss_bytes'], 'RSS bytes', 0, 2**63-1)
                number(sample['cpu_seconds'], 'CPU seconds')
                for field in ('disk_read_bytes', 'disk_write_bytes'):
                    if sample[field] is not None:
                        io.integer(sample[field], field, 0, 2**64-1)
                history = histories[name]
                if history:
                    prior = history[-1]
                    io.require(left > prior['observed_end_s'] and
                               sample['cpu_seconds'] >= prior['cpu_seconds'], 'resource clock or CPU counter regressed')
                    for field in ('disk_read_bytes', 'disk_write_bytes'):
                        io.require(prior[field] is None or sample[field] is None or sample[field] >= prior[field],
                                   'disk counter regressed')
                history.append(sample)
        finish = number(summary['elapsed_s'], 'summary time', elapsed, 86500)
        io.require(duration <= elapsed <= duration + 2 * interval + .001 and
                   max(h[-1]['observed_end_s'] for h in histories.values()) <= finish + .0001,
                   'planned interval was shortened or terminal time is inconsistent')
        failures = []
        results = {}
        for name, history in histories.items():
            first, last = history[0], history[-1]
            # Both CPU counter endpoints and each probe's observation window
            # contribute uncertainty. Never turn ps/proc quantization into an
            # exact CPU claim or clamp a counter reset into zero usage.
            span = last['observed_start_s'] - first['observed_end_s']
            io.require(span > 0, 'resource observations have no disjoint interval')
            cpu = (last['cpu_seconds'] - first['cpu_seconds'] + 2 * resolution) * 100 / span
            peak = max(item['rss_bytes'] for item in history)
            delta_rss = last['rss_bytes'] - first['rss_bytes']
            # For a shrinking process the longest interval is the upper
            # (least negative) rate; dividing by the shortest would overstate
            # how fast memory was released.
            growth_span = span if delta_rss >= 0 else last['observed_end_s'] - first['observed_start_s']
            growth = delta_rss * 3600 / growth_span
            limit_rss, limit_cpu = BUDGETS[roles[name]]
            problems = []
            if peak >= limit_rss:
                problems.append('sampled RSS reached pilot limit')
            if cpu >= limit_cpu:
                problems.append('average CPU upper bound reached pilot limit')
            if growth >= GROWTH_PER_HOUR:
                problems.append('net RSS growth reached pilot limit')
            failures.extend(name + ': ' + problem for problem in problems)
            results[name] = {'role': roles[name], 'samples': len(history), 'observedSecondsLower': span,
                             'sampledPeakRssBytes': peak, 'netRssGrowthBytesPerHourUpper': growth,
                             'averageCpuPercentUpper': cpu, 'failures': problems,
                             'budgets': {'rssBytesExclusive': limit_rss, 'cpuPercentExclusive': limit_cpu,
                                         'netRssGrowthBytesPerHourExclusive': GROWTH_PER_HOUR}}
        report.update(targets=results, failures=failures, plannedSeconds=duration)
        if duration < 3600:
            report['reason'] = 'pilot requires at least 3600 planned seconds; short runs are diagnostic only'
        else:
            report['status'] = 'FAIL' if failures else 'PASS'
    except (io.EvidenceError, OSError, ValueError, KeyError, TypeError) as error:
        report['reason'] = str(error)[:500]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--role', action='append', default=[], metavar='NAME=host|viewer|core')
    args = parser.parse_args()
    roles = {}
    for item in args.role:
        name, separator, role = item.partition('=')
        if not separator or not NAME.fullmatch(name) or name in roles or role not in BUDGETS:
            parser.error('roles must name unique targets and host, viewer or core')
        roles[name] = role
    report = analyze(args.trace, roles if args.role else None)
    print(json.dumps(report, sort_keys=True, allow_nan=False))
    return {'PASS': 0, 'FAIL': 1, 'INCOMPLETE': 2}[report['status']]


if __name__ == '__main__':
    sys.exit(main())
