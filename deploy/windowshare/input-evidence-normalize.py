#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Normalize actual opt-in source/native/queue journals without clock guessing.

This does not authenticate the observer or prove AppKit field preservation.
Those remain NOT_RUN until independently observed on the frozen candidate.
No input injection, recorder launch, latency estimate or H11 PASS is performed.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys

LEGACY_QUEUES = ('capture', 'translation', 'completed', 'server-pending', 'server-completed',
          'server-executing', 'coordinator-operations', 'coordinator-batch',
          'coordinator-preview', 'event-outstanding', 'event-dispatch',
          'private-output', 'private-buffered-input', 'private-incoming', 'private-dispatch')
# None is an explicit absence of a production bound, not missing metadata or
# the bounded recorder's capacity. Only these two EventQueue metrics use it.
LEGACY_CAPACITIES = dict(zip(LEGACY_QUEUES, (256, 256, 256, 256, 256, 256, 17, 16, 16, None, None, 16384, 1488, 372, 256)))
QUEUES = LEGACY_QUEUES[:-4] + ('event-all-outstanding', 'event-all-dispatch') + LEGACY_QUEUES[-4:]
CAPACITIES = {**LEGACY_CAPACITIES, **{name: 64 if name.endswith('dispatch') else 4096
                                    for name in QUEUES if name.startswith('event-')}}
KINDS = {'key_down', 'key_up', 'key_repeat', 'button_down', 'button_up', 'drag', 'scroll', 'hover'}
TAG = 0x445357494E503031
MAX_BYTES = 256 * 1024 * 1024
MAX_ROWS = 1_000_000
U64 = 2**64 - 1


class EvidenceError(ValueError):
    pass


def require(value, message):
    if not value:
        raise EvidenceError(message)


def unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON field')
        result[key] = value
    return result


def keys(value, expected, label):
    require(type(value) is dict and set(value) == set(expected), label + ': unexpected/missing fields')


def integer(value, label, *, signed=False, nonzero=False):
    require(type(value) is str and re.fullmatch(r'0|-?[1-9][0-9]*', value), label + ': noncanonical integer')
    result = int(value)
    require((-(2**63) if signed else int(nonzero)) <= result <= (2**63-1 if signed else U64), label + ': out of range')
    return result


def digest(value):
    return hashlib.sha256(value).hexdigest()


def encoded(value):
    return json.dumps(value, separators=(',', ':'), sort_keys=True, allow_nan=False).encode()


def owner(row):
    require(type(row.get('owner')) is list and len(row['owner']) == 11, 'missing exact owner')
    values = tuple(integer(v, 'owner') for v in row['owner'])
    identity = row.get('identity')
    require(type(identity) is str and re.fullmatch('[0-9a-f]{192}', identity), 'missing exact peer incarnations/pins')
    return values, identity


def raw(row):
    timestamp = integer(row.get('cgTimestampNs'), 'native timestamp', nonzero=True)
    flags = integer(row.get('flags'), 'native flags')
    require(type(row.get('raw')) is list and len(row['raw']) == 17, 'incomplete native effect')
    fields = tuple(integer(value, 'native field', signed=True) for value in row['raw'])
    require(fields[0] in (1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 22, 25, 26, 27), 'unsupported native event type')
    return timestamp, flags, fields


def source_kind(row, effect):
    kind = row.get('effect')
    require(kind in KINDS, 'unsupported source effect (including ambiguous modifier toggle)')
    fields = effect[2]
    expected = {1: 'button_down', 2: 'button_up', 3: 'button_down', 4: 'button_up', 25: 'button_down',
                26: 'button_up', 5: 'hover', 6: 'drag', 7: 'drag', 27: 'drag', 22: 'scroll', 11: 'key_up'}
    if fields[0] == 10:
        require(fields[3] in (0, 1) and kind == ('key_repeat' if fields[3] else 'key_down'), 'key kind/raw mismatch')
    elif fields[0] == 12:
        require(kind in ('key_down', 'key_up') and fields[3] == 0, 'unsupported flags-changed transition')
    else:
        require(kind == expected[fields[0]], 'source kind/raw mismatch')
    return kind


class Journal:
    def __init__(self, path: Path, role: str):
        self.path = path
        self.rows = defaultdict(list)
        self.queues = defaultdict(list)
        self.arm = None
        self.end = None
        self.header = None
        hashed = hashlib.sha256()
        fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0))
        with os.fdopen(fd, 'rb') as stream:
            before = os.fstat(stream.fileno())
            require(stat.S_ISREG(before.st_mode) and before.st_uid == os.geteuid() and before.st_nlink == 1 and
                    before.st_mode & 0o077 == 0, 'journal must be an owned private regular file')
            require(0 < before.st_size <= MAX_BYTES, 'journal size exceeds bound')
            count = 0
            for count, line in enumerate(stream, 1):
                require(count <= MAX_ROWS and line.endswith(b'\n') and 0 < len(line) <= 4096,
                        'truncated/oversized journal row')
                hashed.update(line)
                try:
                    value = json.loads(line, object_pairs_hook=unique,
                                       parse_constant=lambda _: require(False, 'nonfinite JSON'))
                except (ValueError, UnicodeError, RecursionError) as error:
                    raise EvidenceError('invalid journal JSON') from error
                self.accept(value, count, role)
            after = os.fstat(stream.fileno())
            require((before.st_size, before.st_mtime_ns, before.st_ctime_ns) ==
                    (after.st_size, after.st_mtime_ns, after.st_ctime_ns), 'journal changed during read')
        require(count > 2 and self.header and self.arm and self.end, 'missing journal header/arm/end')
        require(self.arm['accepted'] is True, 'recorder refused measured start')
        require(type(self.end['loss']) is int and self.end['loss'] == 0 and type(self.end['state']) is int and self.end['state'] == 3 and
                self.end['drained'] is True, 'recorder reported evidence loss or incomplete stop')
        self.start = integer(self.arm['timeNs'], 'arm time', nonzero=True)
        self.finish = integer(self.end['timeNs'], 'stop time', nonzero=True)
        self.source_end = integer(self.end['sourceEndNs'], 'source end time', nonzero=True)
        require(0 < self.source_end - self.start <= 180_000_000_000 and
                self.source_end <= self.finish <= self.source_end + 300_000_000, 'unbounded or inverted recording/drain interval')
        self.binding = owner(self.arm)
        require(integer(self.end['rows'], 'row count') == sum(map(len, self.rows.values())) + sum(map(len, self.queues.values())),
                'recorder count does not cover complete payload/queue trace')
        for rows in list(self.rows.values()) + list(self.queues.values()):
            previous = self.start
            for row in rows:
                at = integer(row['timeNs'], 'observation time')
                require(previous <= at <= self.finish, 'observer order/time escaped armed interval')
                previous = at
        require(all(integer(row['timeNs'], 'source time') <= self.source_end for row in self.rows['source']),
                'source payload escaped explicit stop boundary')
        require(set(self.queues) == set(self.header['queues']), 'queue inventory is missing or substituted')
        self.sha256 = hashed.hexdigest()

    def accept(self, row, index, role):
        require(type(row) is dict and type(row.get('type')) is str and self.end is None, 'data after terminal marker')
        kind = row['type']
        if index == 1:
            keys(row, ('type', 'schema', 'runId', 'role', 'pid', 'clockId', 'queues', 'nativeFieldPreservation', 'observerProvenance'), 'header')
            require(kind == 'header' and row['schema'] in ('deskflow-input-evidence-v1', 'deskflow-input-evidence-v2') and row['role'] == role, 'wrong journal schema/role')
            require(type(row['runId']) is str and re.fullmatch('[A-Za-z0-9_-]{1,64}', row['runId']), 'bad run identity')
            require(type(row['clockId']) is str and re.fullmatch('[A-Za-z0-9_-]{1,96}', row['clockId']), 'bad observer clock')
            inventory = LEGACY_QUEUES if row['schema'].endswith('-v1') else QUEUES
            require(row['queues'] == (list(inventory) if role == 'core' else list(inventory[-4:]) if role == 'host' else []), 'undeclared or incomplete queue inventory')
            require(row['nativeFieldPreservation'] == row['observerProvenance'] == 'NOT_RUN', 'collector cannot certify its own physical provenance')
            integer(row['pid'], 'observer pid', nonzero=True)
            self.header = row
        elif kind == 'readiness':
            require(self.arm is None and type(row.get('readyCandidate')) is bool, 'late/malformed readiness')
            keys(row, ('type', 'timeNs', 'readyCandidate', 'loss', 'owner', 'identity') if row['readyCandidate'] else
                 ('type', 'timeNs', 'readyCandidate', 'loss'), 'readiness')
            integer(row['timeNs'], 'readiness time')
            require(type(row['loss']) is int and row['loss'] == 0, 'pre-arm evidence continuity was lost')
            if row['readyCandidate']:
                owner(row)
        elif kind == 'arm':
            keys(row, ('type', 'timeNs', 'accepted', 'owner', 'identity'), 'arm')
            require(self.arm is None and type(row['accepted']) is bool, 'duplicate or malformed arm')
            self.arm = row
        elif kind == 'end':
            keys(row, ('type', 'timeNs', 'sourceEndNs', 'state', 'loss', 'rows', 'drained'), 'end')
            self.end = row
        else:
            require(self.arm is not None and self.arm['accepted'] is True, 'payload outside accepted run')
            if kind == 'queue':
                keys(row, ('type', 'timeNs', 'queue', 'enqueued', 'retired', 'depth', 'capacity'), 'queue')
                require(row['queue'] in self.header['queues'], 'unknown queue')
                self.queues[row['queue']].append(row)
            elif kind == 'source':
                require(role == 'core', 'source from wrong observer')
                keys(row, ('type', 'timeNs', 'epoch', 'sequence', 'effect', 'cgTimestampNs', 'flags', 'raw'), 'source')
                self.rows[kind].append(row)
            elif kind == 'native':
                require(role == 'native', 'receipt from wrong observer')
                keys(row, ('type', 'timeNs', 'window', 'sourcePid', 'tag', 'cgTimestampNs', 'flags', 'raw'), 'native')
                self.rows[kind].append(row)
            elif kind == 'hover':
                require(role == 'core', 'compression from wrong observer')
                keys(row, ('type', 'timeNs', 'epoch', 'sequence', 'retainedSequence'), 'hover')
                self.rows[kind].append(row)
            else:
                raise EvidenceError('unknown journal record')


def checked_queues(journal):
    for name, rows in journal.queues.items():
        previous = (0, 0, 0)
        capacity = (LEGACY_CAPACITIES if journal.header['schema'].endswith('-v1') else CAPACITIES)[name]
        for index, row in enumerate(rows):
            added, retired, depth = (integer(row[key], 'queue ' + key) for key in ('enqueued', 'retired', 'depth'))
            if capacity is None:
                require(row['capacity'] is None, 'unbounded event metric must declare capacity:null')
            else:
                require(integer(row['capacity'], 'queue capacity', nonzero=True) == capacity,
                        'queue capacity changed/substituted')
            require(added >= previous[0] and retired >= previous[1] and depth == added-retired and
                    (capacity is None or depth <= capacity),
                    'queue accounting, overflow or reset')
            require(added == previous[0] or retired == previous[1], 'queue aggregates additions and retirements')
            if index == 0:
                require((added, retired, depth) == (0, 0, 0) and integer(row['timeNs'], 'baseline') == journal.start,
                        'queue lacks observed empty baseline')
            previous = (added, retired, depth)
        require(rows and previous[2] == 0 and integer(rows[-1]['timeNs'], 'queue end') == journal.finish,
                'queue did not reach an observed empty terminal boundary')
        yield name, capacity, rows


def normalize(core: Journal, host: Journal, native: Journal):
    run = core.header['runId']
    require(host.header['runId'] == native.header['runId'] == run, 'run substitution')
    require(len({journal.header['schema'] for journal in (core, host, native)}) == 1, 'mixed evidence schema versions')
    require(core.binding == host.binding and all(core.binding[0]) and core.binding[1] != '0'*192, 'Core/Host exact lease mismatch')
    require(len({v.header['clockId'] for v in (core, host, native)}) == 3, 'observer clocks must remain independent')
    require(len({integer(v.header['pid'], 'observer pid', nonzero=True) for v in (core, host, native)}) == 3,
            'Core, Host and owned native fixture must be distinct processes')
    native_owner = native.binding[0]
    require(native_owner[0] == integer(native.header['pid'], 'native pid') and native_owner[1] > 0 and
            not any(native_owner[2:]) and native.binding[1] == '0'*192,
            'native target is not its exact owned fixture window')
    epoch = core.binding[0][4]
    sources = core.rows['source']
    require(sources, 'no actual captured source input')
    first = integer(sources[0]['sequence'], 'first capture sequence', nonzero=True)
    by_raw = {}
    source_rows, identities, nonhover_prefix = [], [], [0]
    for index, row in enumerate(sources, 1):
        sequence = integer(row['sequence'], 'source sequence', nonzero=True)
        require(sequence == first + index - 1 and integer(row['epoch'], 'source epoch') == epoch, 'source gap/reorder/epoch replacement')
        effect = raw(row)
        require(effect not in by_raw, 'ambiguous source timestamp/raw-effect tuple')
        kind = source_kind(row, effect)
        nonhover_prefix.append(nonhover_prefix[-1] + (kind != 'hover'))
        signature = digest(encoded((effect[1], effect[2])))
        by_raw[effect] = index
        source_rows.append({'event': 'input', 't_ns': integer(row['timeNs'], 'source time'), 'id': index,
                            'kind': kind, 'signature': signature})
        identities.append({'id': index, 'capture_epoch': str(epoch), 'capture_sequence': str(sequence),
                           'cg_timestamp_ns': str(effect[0]), 'signature': signature})
    replacements = {}
    compressions = []
    for row in core.rows['hover']:
        require(integer(row['epoch'], 'hover epoch') == epoch, 'hover epoch mismatch')
        removed = integer(row['sequence'], 'compressed source') - first + 1
        kept = integer(row['retainedSequence'], 'retained source') - first + 1
        require(1 <= removed < kept <= len(sources) and removed not in replacements, 'invalid/repeated hover replacement')
        require(nonhover_prefix[kept] == nonhover_prefix[removed-1], 'compression hides non-hover input')
        time = integer(row['timeNs'], 'compression time')
        require(time >= source_rows[kept-1]['t_ns'], 'compression predates retained capture')
        replacements[removed] = kept
        compressions.append({'event': 'compress', 't_ns': time, 'id': removed, 'replacement': kept})
    receipts, received = [], set()
    host_pid = integer(host.header['pid'], 'host pid')
    for row in native.rows['native']:
        require(integer(row['window'], 'native window') == native_owner[1] and
                integer(row['sourcePid'], 'native source pid') == host_pid and integer(row['tag'], 'native tag') == TAG,
                'native destination/source process/marker not preserved or unrelated input')
        effect = raw(row)
        require(effect in by_raw, 'native timestamp/raw effect does not exactly match source; unsupported preservation or extra input')
        event_id = by_raw[effect]
        require(event_id not in received, 'duplicate native receipt')
        received.add(event_id)
        original = source_rows[event_id-1]
        receipts.append({'event': 'receipt', 't_ns': integer(row['timeNs'], 'receipt time'), 'id': event_id,
                         'kind': original['kind'], 'signature': original['signature']})
    require([row['id'] for row in receipts] == [i for i in range(1, len(sources)+1) if i not in replacements],
            'native receipts contain missing/reordered input or unexplained hover loss')
    for removed in sorted(replacements, reverse=True):
        retained = replacements[removed]
        require(retained in received or retained in replacements, 'hover replacement lacks a native survivor')
    queues = [(journal, name, capacity, rows) for journal in (core, host) for name, capacity, rows in checked_queues(journal)]
    return {'run': run, 'epoch': epoch, 'owner': 'lease-' + digest(encoded(core.binding))[:32],
            'source': source_rows, 'receipts': receipts, 'compressions': compressions,
            'identities': identities, 'queues': queues}


def write_normalized(output: Path, journals, data):
    require(not output.exists() and not output.is_symlink(), 'normalized output directory must be new')
    output.mkdir(mode=0o700)
    core, host, native = journals
    identity = {'run_id': data['run'], 'owner': data['owner'], 'epoch': data['epoch']}
    def artifact(name, contents):
        path = output / name
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(contents)
        return {'path': name, 'sha256': digest(contents)}
    def trace(name, format_name, journal, rows, source_boundary=False):
        values = [{'event': 'start', 'format': format_name, 't_ns': journal.start,
                   **identity, 'clock': journal.header['clockId']}]
        values += [{**row, 'index': index} for index, row in enumerate(rows, 1)]
        values.append({'event': 'end', 'index': len(rows)+1, 't_ns': journal.source_end if source_boundary else journal.finish,
                       'count': len(rows), 'lost_records': 0, 'complete': True})
        return artifact(name, b''.join(encoded(row) + b'\n' for row in values))
    input_config = {'owner': data['owner'], 'epoch': data['epoch'],
                    'source_clock': core.header['clockId'], 'receipt_clock': native.header['clockId'],
                    'source': trace('source.jsonl', 'deskflow-h11-source-v1', core, data['source'], source_boundary=True),
                    'receipts': trace('receipts.jsonl', 'deskflow-h11-receipts-v1', native, data['receipts']),
                    'compressions': trace('hover.jsonl', 'deskflow-h11-hover-v1', core, data['compressions']), 'queues': []}
    for journal, name, capacity, rows in data['queues']:
        scoped_name = journal.header['role'] + '-' + name
        converted = [{'event': 'queue', 't_ns': integer(row['timeNs'], 'queue time'),
                      **{key: integer(row[key], key) for key in ('depth', 'enqueued', 'retired')},
                      'deadline_revocations': 0, 'overflows': 0} for row in rows]
        input_config['queues'].append({'name': scoped_name, 'capacity': capacity, 'clock': journal.header['clockId'],
             'trace': trace(scoped_name + '.jsonl', 'deskflow-h11-queue-v1', journal, converted)})
    mapping = artifact('capture-identities.json', encoded(data['identities']) + b'\n')
    unbounded = [q['name'] for q in input_config['queues'] if q['capacity'] is None]
    all_events = not core.header['schema'].endswith('-v1')
    report = {'format': 'deskflow-normalized-input-v1', 'status': 'NORMALIZED_UNVERIFIED', 'run_id': data['run'],
              'h11_complete': False, 'observer_provenance': 'NOT_RUN', 'native_field_preservation': 'NOT_RUN',
              'clock_policy': 'independent observer clocks retained; no endpoint clock subtraction',
              'match_policy': 'unique exact CG timestamp, flags and raw effect; no ordinal joins or value repair',
              'raw_journals': {journal.header['role']: {'path': str(journal.path), 'sha256': journal.sha256} for journal in journals},
              'capture_identities': mapping, 'input': input_config, 'latency': None,
              'pending_queue_coverage': {'status': 'INCOMPLETE', 'full_component_eligible': False,
                  'observed_event_scope': ('all Core Event lifetimes, with a separate original-stamped subset' if all_events else 'original-stamped input views') + ' from admission through pending/saved/retrieved storage and dispatch completion',
                  'unbounded_metrics': unbounded,
                  'capacity_certification': 'INCOMPLETE' if unbounded else 'DECLARED',
                  'unmeasured': ([] if all_events else ['unstamped/configured Core events']) + ['OS/library input buffers before Core admission; compositor and device queues']},
              'limits': ['queue inventory covers only the declared Deskflow structures; complete pending-input coverage remains unverified',
                         'Declared application limits do not certify OS/library/kernel buffers outside admission',
                         'AppKit receipt is an owned app observation, not physical hardware consumption',
                         'raw capture before first queue is stream observation, not a physical-neutral assertion']}
    artifact('normalized-input.json', encoded(report) + b'\n')
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', required=True, type=Path)
    parser.add_argument('--host', required=True, type=Path)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        journals = tuple(Journal(path, role) for path, role in ((args.core, 'core'), (args.host, 'host'), (args.native, 'native')))
        report = write_normalized(args.output, journals, normalize(*journals))
    except (EvidenceError, OSError) as error:
        print(json.dumps({'status': 'INCOMPLETE', 'h11_complete': False, 'reason': str(error)}))
        return 2
    print(json.dumps({'status': report['status'], 'h11_complete': False, 'output': str(args.output)}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
