#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Offline, bounded H11 input/queue and single-clock latency evidence checks.

This tool neither collects input nor authenticates measurement provenance. It
checks a complete, hashed bundle against fixed targets. A component PASS is not
a complete H11 verdict (GPU, resources, fairness and cleanup are separate).
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys

NS = 1_000_000_000
MAX_TRACE_BYTES = 32 * 1024 * 1024
MAX_RECORDING_BYTES = 16 * 1024 * 1024 * 1024
MAX_ROWS = 100_000
MAX_LINE_BYTES = 4096
MAX_DURATION_NS = 600 * NS
MIN_INPUT_NS = 120 * NS
MAX_INPUT_GAP_NS = (2 * NS + 119) // 120
KINDS = frozenset(('key_down', 'key_up', 'key_repeat', 'button_down', 'button_up',
                   'drag', 'scroll', 'hover'))
REQUIRED_KINDS = KINDS - {'key_repeat'}
IDENTIFIER = re.compile(r'[a-zA-Z0-9][a-zA-Z0-9_.-]{0,95}\Z')
SHA256 = re.compile(r'[0-9a-f]{64}\Z')


class EvidenceError(ValueError):
    """Missing, malformed or internally inconsistent evidence; cannot pass."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise EvidenceError(message)


def keys(value, expected: set[str], label: str) -> None:
    require(type(value) is dict and set(value) == expected, label + ': unexpected/missing fields')


def integer(value, label: str, low=0, high=2**63 - 1) -> int:
    require(type(value) is int and low <= value <= high, label + ': invalid integer')
    return value


def identifier(value, label: str) -> str:
    require(type(value) is str and IDENTIFIER.fullmatch(value) is not None, label + ': invalid identifier')
    return value


def digest(value, label: str) -> str:
    require(type(value) is str and SHA256.fullmatch(value) is not None, label + ': invalid SHA-256')
    return value


def _unique_object(pairs):
    result = {}
    for name, value in pairs:
        require(name not in result, 'duplicate JSON field')
        result[name] = value
    return result


def decode_json(data: bytes):
    try:
        return json.loads(data.decode('utf-8'), object_pairs_hook=_unique_object,
                          parse_constant=lambda _: require(False, 'non-finite JSON number'))
    except (ValueError, UnicodeError, RecursionError) as error:
        raise EvidenceError('invalid strict UTF-8 JSON: ' + str(error)[:100]) from error


def open_regular(path: Path):
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0))
    try:
        require(stat.S_ISREG(os.fstat(fd).st_mode), 'evidence must be a regular file')
        return os.fdopen(fd, 'rb')
    except BaseException:
        os.close(fd)
        raise


class Bundle:
    def __init__(self, path: Path):
        require(not path.is_symlink(), 'manifest may not be a symlink')
        self.directory = path.resolve().parent
        with open_regular(path) as stream:
            data = stream.read(256 * 1024 + 1)
        require(len(data) <= 256 * 1024, 'manifest exceeds bound')
        self.sha256 = hashlib.sha256(data).hexdigest()
        self.manifest = decode_json(data)
        self.used_paths = set()

    def artifact(self, value: dict, *, recording=False) -> bytes | None:
        keys(value, {'path', 'sha256'}, 'artifact')
        name = value['path']
        # Deliberately allow only sibling files: no traversal or parent-symlink
        # ambiguity. Collection can copy selected evidence into this bundle.
        require(type(name) is str and IDENTIFIER.fullmatch(name) is not None,
                'artifact requires a simple sibling filename')
        require(name not in ('.', '..') and name not in self.used_paths, 'artifact reused or invalid path')
        self.used_paths.add(name)
        expected = digest(value['sha256'], 'artifact hash')
        path = self.directory / name
        require(not path.is_symlink(), 'artifact may not be a symlink')
        maximum = MAX_RECORDING_BYTES if recording else MAX_TRACE_BYTES
        with open_regular(path) as stream:
            before = os.fstat(stream.fileno())
            require(0 < before.st_size <= maximum, 'artifact empty or exceeds byte bound')
            if recording:
                hashed = hashlib.sha256()
                count = 0
                while block := stream.read(1024 * 1024):
                    count += len(block)
                    require(count <= maximum, 'recording grew beyond byte bound')
                    hashed.update(block)
                result = None
                actual = hashed.hexdigest()
            else:
                result = stream.read(maximum + 1)
                require(len(result) <= maximum, 'trace exceeds byte bound')
                actual = hashlib.sha256(result).hexdigest()
            after = os.fstat(stream.fileno())
            require((before.st_size, before.st_mtime_ns, before.st_ctime_ns) ==
                    (after.st_size, after.st_mtime_ns, after.st_ctime_ns), 'artifact changed during read')
        require(actual == expected, 'artifact SHA-256 mismatch: ' + name)
        return result


def trace(bundle: Bundle, artifact: dict, format_name: str, identity: dict) -> tuple[dict, list, dict]:
    data = bundle.artifact(artifact)
    require(data.endswith(b'\n'), 'truncated trace (missing final newline)')
    lines = data.splitlines()
    require(2 <= len(lines) <= MAX_ROWS + 2, 'trace row budget exceeded or missing boundaries')
    require(all(0 < len(line) <= MAX_LINE_BYTES for line in lines), 'empty or oversized trace row')
    rows = [decode_json(line) for line in lines]
    first, last = rows[0], rows[-1]
    keys(first, {'event', 'format', 't_ns'} | set(identity), 'trace start')
    require(first['event'] == 'start' and first['format'] == format_name, 'wrong trace format/start')
    require(all(first[k] == v and type(first[k]) is type(v) for k, v in identity.items()),
            'trace run/clock/owner/epoch substitution')
    start = integer(first['t_ns'], 'trace start time')
    keys(last, {'event', 'index', 't_ns', 'count', 'lost_records', 'complete'}, 'trace end')
    require(last['event'] == 'end' and last['complete'] is True, 'trace lacks completed terminal marker')
    require(integer(last['lost_records'], 'lost records') == 0, 'collector reported lost records')
    require(integer(last['count'], 'trace count') == len(rows) - 2 and
            integer(last['index'], 'terminal index', 1) == len(rows) - 1, 'trace terminal count mismatch')
    finish = integer(last['t_ns'], 'trace end time', start + 1)
    require(finish - start <= MAX_DURATION_NS, 'trace duration exceeds ten-minute bound')
    previous = start
    for index, row in enumerate(rows[1:-1], 1):
        require(type(row) is dict and 'index' in row and 't_ns' in row, 'malformed trace record')
        require(integer(row['index'], 'record index', 1) == index, 'record gap, duplicate or reorder')
        previous = integer(row['t_ns'], 'record time', previous, finish)
    return first, rows[1:-1], last


def input_events(rows: list, event: str) -> list:
    for row in rows:
        keys(row, {'event', 'index', 't_ns', 'id', 'kind', 'signature'}, event)
        require(row['event'] == event, 'unexpected input trace event')
        integer(row['id'], 'source event id', 1)
        require(type(row['kind']) is str and row['kind'] in KINDS, 'unknown input kind')
        digest(row['signature'], 'canonical native-effect signature')
    return rows


def queue_metrics(rows: list, first: dict, last: dict, capacity: int) -> tuple[dict, list[str]]:
    require(rows, 'queue has no observations')
    initial = {'enqueued': 0, 'retired': 0, 'depth': 0, 'deadline_revocations': 0, 'overflows': 0}
    previous = initial
    failures = []
    counts = []
    for row in rows:
        keys(row, {'event', 'index', 't_ns'} | set(initial), 'queue record')
        require(row['event'] == 'queue', 'unexpected queue record')
        for counter in initial:
            integer(row[counter], 'queue ' + counter)
            if counter != 'depth':
                require(row[counter] >= previous[counter], 'queue counter reset or reorder')
        require(row['depth'] == row['enqueued'] - row['retired'], 'queue accounting mismatch')
        require(row['enqueued'] == previous['enqueued'] or row['retired'] == previous['retired'],
                'queue row aggregates enqueue and retirement; intermediate depth is unknown')
        previous = row
        counts.append(row['depth'])
    require(all(rows[0][k] == v for k, v in initial.items()) and rows[0]['t_ns'] == first['t_ns'],
            'queue must begin with an observed empty zero-counter baseline')
    if max(counts) > capacity:
        failures.append('queue capacity exceeded')
    if rows[-1]['depth'] != 0:
        failures.append('queue did not drain')
    if rows[-1]['deadline_revocations']:
        failures.append('deadline revocation observed')
    if rows[-1]['overflows']:
        failures.append('queue overflow observed')
    duration = last['t_ns'] - first['t_ns']
    if duration < MIN_INPUT_NS:
        failures.append('queue coverage shorter than 120 seconds')
    # Time-weighted per-second means from every mutation, not observer polling
    # or the count of log rows. Unknown logging loss was already denied.
    seconds = duration // NS
    integrals = [0] * seconds
    for left, right in zip(rows, rows[1:] + [last]):
        begin, end = left['t_ns'] - first['t_ns'], right['t_ns'] - first['t_ns']
        while begin < min(end, seconds * NS):
            bucket = begin // NS
            stop = min(end, (bucket + 1) * NS)
            integrals[bucket] += left['depth'] * (stop - begin)
            begin = stop
    means = [value / NS for value in integrals]
    if len(means) >= 20:
        first_mean, last_mean = sum(means[:10]) / 10, sum(means[-10:]) / 10
        center = (len(means) - 1) / 2
        slope = sum((i - center) * y for i, y in enumerate(means)) / \
            sum((i - center)**2 for i in range(len(means)))
        if last_mean > first_mean + 1e-9 or slope > 1e-9:
            failures.append('pending queue grows (positive trend or final-ten-second mean)')
    else:
        first_mean = last_mean = slope = None
    return {'peak_depth': max(counts), 'enqueued': rows[-1]['enqueued'],
            'retired': rows[-1]['retired'], 'final_depth': rows[-1]['depth'],
            'first_10s_mean': first_mean, 'last_10s_mean': last_mean,
            'linear_slope_per_second': slope,
            'deadline_revocations': rows[-1]['deadline_revocations'],
            'overflows': rows[-1]['overflows']}, failures


def analyze_input(bundle: Bundle, config: dict, run: str) -> dict:
    keys(config, {'source', 'receipts', 'compressions', 'source_clock', 'receipt_clock',
                  'owner', 'epoch', 'queues'}, 'input configuration')
    owner = identifier(config['owner'], 'owner')
    epoch = integer(config['epoch'], 'capture epoch', 1, 2**64 - 1)
    identity = {'run_id': run, 'owner': owner, 'epoch': epoch}
    source_clock = identifier(config['source_clock'], 'source clock')
    receipt_clock = identifier(config['receipt_clock'], 'receipt clock')
    sf, sources, sl = trace(bundle, config['source'], 'deskflow-h11-source-v1',
                            {**identity, 'clock': source_clock})
    _, receipts, _ = trace(bundle, config['receipts'], 'deskflow-h11-receipts-v1',
                            {**identity, 'clock': receipt_clock})
    _, compressed, _ = trace(bundle, config['compressions'], 'deskflow-h11-hover-v1',
                             {**identity, 'clock': source_clock})
    input_events(sources, 'input')
    input_events(receipts, 'receipt')
    require(sources, 'no source input records')
    require([r['id'] for r in sources] == list(range(1, len(sources) + 1)),
            'source IDs must cover complete contiguous capture trace')
    by_id = {row['id']: row for row in sources}
    non_hover_prefix = [0]
    for row in sources:
        non_hover_prefix.append(non_hover_prefix[-1] + (row['kind'] != 'hover'))
    replacements = {}
    for row in compressed:
        keys(row, {'event', 'index', 't_ns', 'id', 'replacement'}, 'hover compression')
        require(row['event'] == 'compress', 'unexpected hover compression event')
        removed = integer(row['id'], 'compressed id', 1, len(sources))
        kept = integer(row['replacement'], 'replacement id', removed + 1, len(sources))
        require(removed not in replacements, 'duplicate hover compression')
        # Compression may cross only hover events, never a button/key/scroll/drag.
        require(non_hover_prefix[kept] == non_hover_prefix[removed - 1],
                'non-hover input hidden by compression')
        require(row['t_ns'] >= by_id[kept]['t_ns'], 'compression predates its replacement capture')
        replacements[removed] = kept
    received_ids = [r['id'] for r in receipts]
    expected_ids = [i for i in by_id if i not in replacements]
    failures = []
    if received_ids != expected_ids:
        failures.append('native receipts have gaps, duplicates, reorders or unexpected IDs')
    for row in receipts:
        original = by_id.get(row['id'])
        if original is None or (original['kind'], original['signature']) != (row['kind'], row['signature']):
            failures.append('native receipt kind/value differs from its source event')
            break
    # Replacement chains are bounded and increase strictly. Every final survivor
    # must actually reach the target; a counter alone cannot excuse lost hover.
    final_replacement = {}
    received_set = set(received_ids)
    for removed in sorted(replacements, reverse=True):
        kept = replacements[removed]
        final_replacement[removed] = final_replacement.get(kept, kept)
        if final_replacement[removed] not in received_set:
            failures.append('compressed hover has no observed final replacement')
            break
    kinds = Counter(row['kind'] for row in sources)
    if not REQUIRED_KINDS <= kinds.keys():
        failures.append('workload lacks required key/button/drag/scroll/hover mixture')
    duration = sl['t_ns'] - sf['t_ns']
    if duration < MIN_INPUT_NS:
        failures.append('input run shorter than 120 seconds')
    seconds = duration // NS
    buckets = [0] * seconds
    for row in sources:
        slot = (row['t_ns'] - sf['t_ns']) // NS
        if slot < seconds:
            buckets[slot] += 1
    if not buckets or min(buckets) < 120 or len(sources) * NS < 120 * duration:
        failures.append('observed workload below 120 Hz (total or a complete one-second bucket)')
    observed_times = [sf['t_ns']] + [row['t_ns'] for row in sources] + [sl['t_ns']]
    maximum_gap = max(b - a for a, b in zip(observed_times, observed_times[1:]))
    if maximum_gap > MAX_INPUT_GAP_NS:
        failures.append('source cadence contains a gap over two 120Hz periods')
    queue_configs = config['queues']
    require(type(queue_configs) is list and 1 <= len(queue_configs) <= 16, 'require bounded queue inventory')
    queues = {}
    for q in queue_configs:
        keys(q, {'name', 'capacity', 'clock', 'trace'}, 'queue configuration')
        name = identifier(q['name'], 'queue name')
        require(name not in queues, 'duplicate queue name')
        capacity = integer(q['capacity'], 'queue capacity', 1, 65536)
        clock = identifier(q['clock'], 'queue clock')
        first, values, last = trace(bundle, q['trace'], 'deskflow-h11-queue-v1', {**identity, 'clock': clock})
        metrics, errors = queue_metrics(values, first, last, capacity)
        queues[name] = metrics
        failures.extend(name + ': ' + message for message in errors)
    return {'status': 'FAIL' if failures else 'PASS', 'failures': failures,
            'duration_s': duration / NS, 'source_records': len(sources), 'native_receipts': len(receipts),
            'non_coalescible_records': sum(v for k, v in kinds.items() if k != 'hover'),
            'hover_captured': kinds['hover'], 'hover_compressed': len(replacements),
            'hover_received': sum(r['kind'] == 'hover' for r in receipts),
            'minimum_one_second_input_count': min(buckets) if buckets else 0,
            'maximum_source_gap_ms': maximum_gap / 1e6,
            'source_kinds': dict(sorted(kinds.items())), 'queues': queues,
            'clock_policy': 'each duration uses its own trace clock; no source-to-receipt time subtraction'}


def percentile(values: list[int], percent: int) -> int:
    # Integer nearest-rank arithmetic keeps threshold boundary decisions exact.
    return sorted(values)[(percent * len(values) + 99) // 100 - 1]


def analyze_latency(bundle: Bundle, config: dict, run: str) -> dict:
    keys(config, {'method', 'boundary', 'clock', 'uncertainty_ns', 'observer_hz', 'recording',
                  'samples', 'planned_frames', 'width', 'height', 'stream_fps', 'streams', 'network'},
         'latency configuration')
    method = config['method']
    require(method in ('filmed', 'shared-instrument-clock'), 'unsupported latency method')
    expected_boundary = 'stimulus-before-capture-to-remote-visible' if method == 'filmed' else 'capture-to-remote-visible'
    require(config['boundary'] == expected_boundary, 'unsupported measurement boundary')
    clock = identifier(config['clock'], 'measurement clock')
    uncertainty = integer(config['uncertainty_ns'], 'total timing uncertainty', 1, 50_000_000)
    observer_hz = integer(config['observer_hz'], 'observer rate', 1, 1_000_000_000)
    # The physical filmed stimulus must precede creation/capture of the marked
    # frame. Merely filming its source display is insufficient: capture may
    # precede physical presentation there. Collection must establish ordering.
    # A filmed transition is bracketed by adjacent camera frames at both ends.
    # Do not let a manually entered zero uncertainty erase this quantization.
    if method == 'filmed':
        require(observer_hz >= 240 and uncertainty * observer_hz >= 2 * NS,
                'film requires >=240 Hz and at least two camera periods of uncertainty')
    else:
        require(uncertainty * observer_hz >= NS, 'instrument uncertainty below one clock period')
    frames = integer(config['planned_frames'], 'predeclared frame count', 600, 100_000)
    for name in ('width', 'height', 'stream_fps', 'streams'):
        integer(config[name], name, 1, 65536)
    require(type(config['network']) is str, 'network must be declared')
    bundle.artifact(config['recording'], recording=True)
    first, rows, last = trace(bundle, config['samples'], 'deskflow-h11-latency-v1',
                              {'run_id': run, 'clock': clock})
    require(len(rows) == frames, 'missing/additional predeclared latency samples')
    nominal, upper = [], []
    dropped = 0
    previous_start = -1
    previous_display = -1
    for row in rows:
        keys(row, {'event', 'index', 't_ns', 'frame_id', 'start_clock', 'display_clock',
                   'start_ns', 'display_ns'}, 'latency sample')
        require(row['event'] == 'frame' and integer(row['frame_id'], 'frame id', 1) == row['index'],
                'latency frame IDs missing, repeated or reordered')
        require(row['start_clock'] == row['display_clock'] == clock,
                'unrelated endpoint clocks cannot be subtracted')
        started = integer(row['start_ns'], 'start observation', max(first['t_ns'], previous_start + 1), last['t_ns'])
        previous_start = started
        require(row['t_ns'] == started, 'sample order time differs from source observation')
        if row['display_ns'] is None:
            dropped += 1
            continue
        displayed = integer(row['display_ns'], 'display observation', max(started, previous_display + 1), last['t_ns'])
        previous_display = displayed
        nominal.append(displayed - started)
        upper.append(displayed - started + uncertainty)
    failures = []
    if (config['width'], config['height'], config['stream_fps'], config['streams'], config['network']) != \
            (1920, 1080, 60, 1, 'wired'):
        failures.append('latency scenario is not one wired 1920x1080 60fps stream')
    if last['t_ns'] - first['t_ns'] < 10 * NS:
        failures.append('latency observation shorter than ten seconds')
    measured_span = rows[-1]['start_ns'] - rows[0]['start_ns']
    # Allow endpoint observation uncertainty, not a configured "60fps" label
    # backed by a short burst or a 30fps source. Source timestamps share the
    # measuring instrument here, never the two computers' unrelated clocks.
    if abs(measured_span * 60 - (frames - 1) * NS) > 2 * uncertainty * 60:
        failures.append('observed source frame cadence does not match 60fps')
    if dropped:
        failures.append('predeclared frames not displayed; may not omit failed samples')
    p95 = percentile(upper, 95) if upper else None
    p99 = percentile(upper, 99) if upper else None
    if p95 is None or p95 > 100_000_000:
        failures.append('p95 upper-bound latency exceeds 100ms or is unavailable')
    if p99 is None or p99 > 150_000_000:
        failures.append('p99 upper-bound latency exceeds 150ms or is unavailable')
    return {'status': 'FAIL' if failures else 'PASS', 'failures': failures,
            'method': method, 'boundary': expected_boundary, 'samples': len(rows), 'undisplayed_frames': dropped,
            'uncertainty_ms': uncertainty / 1e6,
            'observed_stimulus_or_capture_fps': (frames - 1) * NS / measured_span,
            'nominal_p95_ms': percentile(nominal, 95) / 1e6 if nominal else None,
            'nominal_p99_ms': percentile(nominal, 99) / 1e6 if nominal else None,
            'upper_p95_ms': p95 / 1e6 if p95 is not None else None,
            'upper_p99_ms': p99 / 1e6 if p99 is not None else None}


def analyze(path: Path) -> dict:
    bundle = Bundle(path)
    config = bundle.manifest
    keys(config, {'format', 'run_id', 'candidate_sha256', 'collection_artifact', 'input', 'latency'}, 'manifest')
    require(config['format'] == 'deskflow-h11-evidence-v1', 'unsupported manifest format')
    run = identifier(config['run_id'], 'run id')
    digest(config['candidate_sha256'], 'frozen candidate hash')
    # Retain the exact adapter/procedure used to construct canonical records.
    # Hashing it establishes bundle integrity, not truth of the measurements.
    bundle.artifact(config['collection_artifact'])
    report = {'format': 'deskflow-h11-analysis-v1', 'manifest_sha256': bundle.sha256,
              'run_id': run, 'candidate_sha256': config['candidate_sha256'],
              'h11_complete': False, 'measurement_authenticity_verified': False,
              'scope': 'input trace integrity, declared queue traces, and single-clock latency only',
              'limits': ['collection adapters and queue inventory require independent review',
                         'hashes verify retained files, not observation truth or capture completeness',
                         'resources/GPU/bandwidth/fairness/held-input cleanup are separate evidence']}
    for name, operation in (('input', analyze_input), ('latency', analyze_latency)):
        if config[name] is None:
            report[name] = {'status': 'NOT_RUN', 'reason': 'required measurement evidence absent'}
        else:
            try:
                report[name] = operation(bundle, config[name], run)
            except (EvidenceError, OSError) as error:
                report[name] = {'status': 'INCOMPLETE', 'reason': str(error)}
    states = {report[name]['status'] for name in ('input', 'latency')}
    report['status'] = 'INCOMPLETE' if states & {'NOT_RUN', 'INCOMPLETE'} else 'FAIL' if 'FAIL' in states else 'PASS'
    return report


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path, help='explicit manifest, with selected sibling evidence files')
    args = parser.parse_args(argv)
    try:
        report = analyze(args.manifest)
    except (EvidenceError, OSError) as error:
        report = {'format': 'deskflow-h11-analysis-v1', 'status': 'INCOMPLETE',
                  'h11_complete': False, 'reason': str(error)}
    print(json.dumps(report, indent=2, sort_keys=True, allow_nan=False))
    return {'PASS': 0, 'FAIL': 1, 'INCOMPLETE': 2}[report['status']]


if __name__ == '__main__':
    sys.exit(main())
