#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Synthetic, offline parser tests. No measurements, OS input or windows."""

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).with_name('h11-evidence.py')
SPEC = importlib.util.spec_from_file_location('h11_evidence', SCRIPT)
H11 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(H11)
NS = H11.NS


def encoded(value):
    return json.dumps(value, separators=(',', ':'), allow_nan=False).encode() + b'\n'


class Fixture:
    """Explicit synthetic test data; NEVER use these generated rows as H11 evidence."""
    def __init__(self, directory):
        self.root = Path(directory)
        self.manifest = {'format': 'deskflow-h11-evidence-v1', 'run_id': 'SYNTHETIC-UNIT-TEST',
                         'candidate_sha256': 'a' * 64, 'input': None, 'latency': None}
        self.manifest['collection_artifact'] = self.write('collection.txt', b'SYNTHETIC TEST ONLY\n')

    def write(self, name, data):
        (self.root / name).write_bytes(data)
        return {'path': name, 'sha256': hashlib.sha256(data).hexdigest()}

    def rows(self, name, format_name, clock, body, duration=120 * NS, start=0, input_identity=True):
        header = {'event': 'start', 'format': format_name, 'run_id': self.manifest['run_id'],
                  'clock': clock, 't_ns': start}
        if input_identity:
            header.update(owner='fixture-owner-1', epoch=17)
        for index, row in enumerate(body, 1):
            row['index'] = index
        end = {'event': 'end', 'index': len(body) + 1, 't_ns': start + duration,
               'count': len(body), 'lost_records': 0, 'complete': True}
        return self.write(name, b''.join(encoded(r) for r in [header] + body + [end]))

    def full(self, records=14400, seconds=120):
        kinds = ('key_down', 'key_repeat', 'key_up', 'button_down', 'drag', 'button_up', 'scroll', 'hover', 'hover')
        source_start, native_start, queue_start = 10**12, 9 * 10**12, 3 * 10**12
        source, native, compressions, queue = [], [], [], []
        def queue_row(t, enqueued, retired):
            return {'event': 'queue', 't_ns': t, 'enqueued': enqueued, 'retired': retired,
                    'depth': enqueued - retired, 'deadline_revocations': 0, 'overflows': 0}
        queue.append(queue_row(queue_start, 0, 0))
        for index in range(records):
            when = index * NS // 120
            row = {'event': 'input', 't_ns': source_start + when, 'id': index + 1,
                   'kind': kinds[index % len(kinds)], 'signature': hashlib.sha256(str(index).encode()).hexdigest()}
            source.append(row)
            if index % len(kinds) == 7:
                compressions.append({'event': 'compress', 't_ns': source_start + (index + 1) * NS // 120,
                                     'id': index + 1, 'replacement': index + 2})
            else:
                native.append({**row, 'event': 'receipt', 't_ns': native_start + when + 1_000_000})
            queue.extend((queue_row(queue_start + when, index + 1, index),
                          queue_row(queue_start + when + 100_000, index + 1, index + 1)))
        self.manifest['input'] = {
            'owner': 'fixture-owner-1', 'epoch': 17, 'source_clock': 'mac-local', 'receipt_clock': 'target-local',
            'source': self.rows('source.jsonl', 'deskflow-h11-source-v1', 'mac-local', source,
                                start=source_start, duration=seconds * NS),
            'receipts': self.rows('receipts.jsonl', 'deskflow-h11-receipts-v1', 'target-local', native,
                                  start=native_start, duration=seconds * NS),
            'compressions': self.rows('hover.jsonl', 'deskflow-h11-hover-v1', 'mac-local', compressions,
                                      start=source_start, duration=seconds * NS),
            'queues': [{'name': 'fixture-pending', 'capacity': 32, 'clock': 'queue-local',
                        'trace': self.rows('queue.jsonl', 'deskflow-h11-queue-v1', 'queue-local', queue,
                                           start=queue_start, duration=seconds * NS)}]}
        self.latency()
        self.save()

    def latency(self):
        clock = 'single-camera-pts'
        frames = [{'event': 'frame', 't_ns': i * NS // 60, 'frame_id': i + 1,
                   'start_clock': clock, 'display_clock': clock,
                   'start_ns': i * NS // 60, 'display_ns': i * NS // 60 + 80_000_000}
                  for i in range(600)]
        self.manifest['latency'] = {
            'method': 'filmed', 'boundary': 'stimulus-before-capture-to-remote-visible', 'clock': clock,
            'uncertainty_ns': 10_000_000, 'observer_hz': 240, 'planned_frames': 600,
            'width': 1920, 'height': 1080, 'stream_fps': 60, 'streams': 1, 'network': 'wired',
            'recording': self.write('recording.txt', b'SYNTHETIC TEST DATA, NOT A RECORDING\n'),
            'samples': self.rows('latency.jsonl', 'deskflow-h11-latency-v1', clock, frames,
                                 duration=11 * NS, input_identity=False)}

    def save(self):
        self.write('manifest.json', encoded(self.manifest))
        return self.root / 'manifest.json'

    def rewrite(self, artifact, callback):
        rows = [json.loads(line) for line in (self.root / artifact['path']).read_bytes().splitlines()]
        callback(rows)
        artifact.update(self.write(artifact['path'], b''.join(encoded(row) for row in rows)))
        self.save()

    def analyze(self):
        return H11.analyze(self.save())


class H11EvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = tempfile.TemporaryDirectory()
        fixture = Fixture(cls.base.name)
        fixture.full()
        cls.short = tempfile.TemporaryDirectory()
        fixture = Fixture(cls.short.name)
        # Parser/integrity negatives need not replay a two-minute workload.
        # This is explicitly below acceptance duration; no test may call it a
        # positive acceptance run or rely on its short-duration failure alone.
        fixture.full(records=240, seconds=2)

    @classmethod
    def tearDownClass(cls):
        cls.base.cleanup()
        cls.short.cleanup()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        shutil.copytree(self.short.name, self.directory.name, dirs_exist_ok=True)
        self.f = Fixture.__new__(Fixture)
        self.f.root = Path(self.directory.name)
        self.f.manifest = json.loads((self.f.root / 'manifest.json').read_text())

    def tearDown(self):
        self.directory.cleanup()

    def test_complete_synthetic_input_and_film_components(self):
        shutil.copytree(self.base.name, self.directory.name, dirs_exist_ok=True)
        self.f.manifest = json.loads((self.f.root / 'manifest.json').read_text())
        report = self.f.analyze()
        self.assertEqual(report['status'], 'PASS', report)
        self.assertFalse(report['h11_complete'])
        self.assertFalse(report['measurement_authenticity_verified'])
        self.assertEqual(report['input']['source_records'], 14400)
        self.assertEqual(report['input']['hover_compressed'], 1600)
        self.assertEqual(report['input']['hover_received'], 1600)
        self.assertEqual(report['input']['minimum_one_second_input_count'], 120)
        self.assertEqual(report['input']['queues']['fixture-pending']['final_depth'], 0)
        self.assertEqual(report['latency']['upper_p95_ms'], 90)

    def test_missing_measurements_are_not_a_pass(self):
        self.f.manifest['input'] = self.f.manifest['latency'] = None
        report = self.f.analyze()
        self.assertEqual(report['status'], 'INCOMPLETE')
        self.assertEqual(report['input']['status'], 'NOT_RUN')
        self.assertEqual(report['latency']['status'], 'NOT_RUN')

    def test_native_gap_duplicate_reorder_and_value_mismatch_fail(self):
        artifact = self.f.manifest['input']['receipts']
        original = (self.f.root / artifact['path']).read_bytes()
        for mutation in ('gap', 'duplicate', 'reorder', 'value'):
            with self.subTest(mutation=mutation):
                artifact.update(self.f.write(artifact['path'], original))
                def alter(rows):
                    if mutation == 'gap':
                        rows[3]['id'] += 1
                    elif mutation == 'duplicate':
                        rows[3]['id'] = rows[2]['id']
                    elif mutation == 'reorder':
                        for field in ('id', 'kind', 'signature'):
                            rows[2][field], rows[3][field] = rows[3][field], rows[2][field]
                    else:
                        rows[3]['signature'] = 'b' * 64
                self.f.rewrite(artifact, alter)
                result = self.f.analyze()['input']
                self.assertEqual(result['status'], 'FAIL')
                reason = 'kind/value differs' if mutation == 'value' else 'receipts have gaps'
                self.assertTrue(any(reason in failure for failure in result['failures']), result)

    def test_source_gap_cannot_be_hidden_by_consistent_receipts(self):
        self.f.rewrite(self.f.manifest['input']['source'], lambda rows: rows[5].update(id=7))
        self.assertEqual(self.f.analyze()['input']['status'], 'INCOMPLETE')

    def test_non_hover_compression_and_duplicate_compression_deny(self):
        artifact = self.f.manifest['input']['compressions']
        original = (self.f.root / artifact['path']).read_bytes()
        for changes in ({'id': 1}, {'replacement': 10}, {'replacement': 8}):
            artifact.update(self.f.write(artifact['path'], original))
            self.f.rewrite(artifact, lambda rows: rows[1].update(changes))
            self.assertEqual(self.f.analyze()['input']['status'], 'INCOMPLETE')
        artifact.update(self.f.write(artifact['path'], original))
        self.f.rewrite(artifact, lambda rows: rows[2].update(id=rows[1]['id'], replacement=rows[1]['replacement']))
        result = self.f.analyze()['input']
        self.assertEqual(result['status'], 'INCOMPLETE')
        self.assertIn('duplicate hover', result['reason'])

    def test_compression_requires_captured_replacement_and_receipt(self):
        self.f.rewrite(self.f.manifest['input']['compressions'], lambda rows: rows[1].update(t_ns=rows[0]['t_ns']))
        self.assertEqual(self.f.analyze()['input']['status'], 'INCOMPLETE')

    def test_per_second_rate_cannot_be_replaced_by_average_burst(self):
        # Move a single source event into the next bucket without changing ID
        # order or trace duration; now one bucket contains only 119 events.
        self.f.rewrite(self.f.manifest['input']['source'],
                       lambda rows: rows[120].update(t_ns=rows[0]['t_ns'] + NS))
        report = self.f.analyze()['input']
        self.assertEqual(report['status'], 'FAIL', report)
        self.assertEqual(report['minimum_one_second_input_count'], 119)

    def test_bucket_counts_cannot_hide_one_second_bursts(self):
        def burst(rows):
            for row in rows[1:-1]:
                row['t_ns'] = rows[0]['t_ns'] + ((row['id'] - 1) // 120) * NS
        self.f.rewrite(self.f.manifest['input']['source'], burst)
        report = self.f.analyze()['input']
        self.assertEqual(report['minimum_one_second_input_count'], 120)
        self.assertEqual(report['maximum_source_gap_ms'], 1000)
        self.assertTrue(any('two 120Hz periods' in failure for failure in report['failures']), report)

    def test_short_run_and_missing_mixed_class_fail(self):
        self.f.manifest['latency'] = None
        def kinds(rows):
            for row in rows[1:-1]:
                if row['kind'] == 'scroll':
                    row['kind'] = 'drag'
        self.f.rewrite(self.f.manifest['input']['source'], kinds)
        self.f.rewrite(self.f.manifest['input']['receipts'], kinds)
        result = self.f.analyze()['input']
        self.assertEqual(result['status'], 'FAIL')
        self.assertTrue(any('mixture' in failure for failure in result['failures']))
        self.assertTrue(any('shorter than 120' in failure for failure in result['failures']))

    def test_queue_counter_reset_and_false_depth_deny(self):
        artifact = self.f.manifest['input']['queues'][0]['trace']
        original = (self.f.root / artifact['path']).read_bytes()
        for changes in ({'enqueued': 0}, {'depth': 9}):
            artifact.update(self.f.write(artifact['path'], original))
            self.f.rewrite(artifact, lambda rows: rows[20].update(changes))
            self.assertEqual(self.f.analyze()['input']['status'], 'INCOMPLETE')

    def test_queue_revocation_overflow_and_nondrain_fail(self):
        artifact = self.f.manifest['input']['queues'][0]['trace']
        original = (self.f.root / artifact['path']).read_bytes()
        for counter in ('deadline_revocations', 'overflows', 'depth'):
            artifact.update(self.f.write(artifact['path'], original))
            def change(rows):
                rows[-2][counter] += 1
                if counter == 'depth':
                    rows[-2]['retired'] -= 1
            self.f.rewrite(artifact, change)
            result = self.f.analyze()['input']
            self.assertEqual(result['status'], 'FAIL')
            reason = {'deadline_revocations': 'deadline revocation', 'overflows': 'queue overflow',
                      'depth': 'did not drain'}[counter]
            self.assertTrue(any(reason in failure for failure in result['failures']), result)

    def test_time_weighted_queue_growth_detected_even_when_drained(self):
        rows = [{'event': 'queue', 'index': 1, 't_ns': 0, 'depth': 0, 'enqueued': 0,
                 'retired': 0, 'deadline_revocations': 0, 'overflows': 0}]
        for second in range(120):
            delay = 1_000_000 if second < 60 else 10_000_000
            rows.extend(({'event': 'queue', 'index': len(rows) + 1, 't_ns': second * NS,
                          'depth': 1, 'enqueued': second + 1, 'retired': second,
                          'deadline_revocations': 0, 'overflows': 0},
                         {'event': 'queue', 'index': len(rows) + 2, 't_ns': second * NS + delay,
                          'depth': 0, 'enqueued': second + 1, 'retired': second + 1,
                          'deadline_revocations': 0, 'overflows': 0}))
        # Same event rate, peak depth and final zero: longer residence in the
        # latter half must still be seen as growth by the real validator.
        metrics, failures = H11.queue_metrics(rows, {'t_ns': 0}, {'t_ns': 120 * NS}, 32)
        self.assertEqual(metrics['final_depth'], 0)
        self.assertTrue(any('grows' in value for value in failures), failures)

    def test_queue_capacity_is_checked(self):
        self.f.manifest['input']['queues'][0]['capacity'] = 1
        first = {'t_ns': 0}
        rows = [{'event': 'queue', 'index': 1, 't_ns': 0, 'depth': 0, 'enqueued': 0,
                 'retired': 0, 'deadline_revocations': 0, 'overflows': 0},
                {'event': 'queue', 'index': 2, 't_ns': NS, 'depth': 2, 'enqueued': 2,
                 'retired': 0, 'deadline_revocations': 0, 'overflows': 0},
                {'event': 'queue', 'index': 3, 't_ns': 2 * NS, 'depth': 0, 'enqueued': 2,
                 'retired': 2, 'deadline_revocations': 0, 'overflows': 0}]
        _, failures = H11.queue_metrics(rows, first, {'t_ns': 120 * NS}, 1)
        self.assertIn('queue capacity exceeded', failures)

    def test_queue_aggregation_cannot_hide_intermediate_peak(self):
        baseline = {'event': 'queue', 'index': 1, 't_ns': 0, 'depth': 0, 'enqueued': 0,
                    'retired': 0, 'deadline_revocations': 0, 'overflows': 0}
        # A sampled cumulative row could hide a burst beyond capacity followed
        # by a complete drain. It cannot represent every queue mutation.
        aggregate = {**baseline, 'index': 2, 't_ns': NS, 'enqueued': 1000, 'retired': 1000}
        with self.assertRaisesRegex(H11.EvidenceError, 'aggregates enqueue and retirement'):
            H11.queue_metrics([baseline, aggregate], {'t_ns': 0}, {'t_ns': 120 * NS}, 32)

    def test_declared_idle_queue_can_remain_empty(self):
        rows = [{'event': 'queue', 'index': 1, 't_ns': 0, 'depth': 0, 'enqueued': 0,
                 'retired': 0, 'deadline_revocations': 0, 'overflows': 0}]
        _, failures = H11.queue_metrics(rows, {'t_ns': 0}, {'t_ns': 120 * NS}, 32)
        self.assertEqual(failures, [])

    def test_identity_substitution_is_rejected(self):
        artifact = self.f.manifest['input']['receipts']
        original = (self.f.root / artifact['path']).read_bytes()
        for key, value in (('epoch', 18), ('owner', 'other'), ('run_id', 'other'), ('clock', 'other')):
            artifact.update(self.f.write(artifact['path'], original))
            self.f.rewrite(artifact, lambda rows: rows[0].update({key: value}))
            self.assertEqual(self.f.analyze()['input']['status'], 'INCOMPLETE')

    def test_complete_marker_loss_order_and_time_are_strict(self):
        artifact = self.f.manifest['input']['source']
        original = (self.f.root / artifact['path']).read_bytes()
        alterations = [lambda r: r[-1].update(complete=False), lambda r: r[-1].update(lost_records=1),
                       lambda r: r[-1].update(count=1), lambda r: r[3].update(index=2),
                       lambda r: r[3].update(t_ns=0), lambda r: r[3].update(extra=True)]
        for alteration in alterations:
            artifact.update(self.f.write(artifact['path'], original))
            self.f.rewrite(artifact, alteration)
            self.assertEqual(self.f.analyze()['input']['status'], 'INCOMPLETE')

    def test_latency_never_subtracts_two_machine_clocks(self):
        self.f.manifest['input'] = None
        self.f.rewrite(self.f.manifest['latency']['samples'],
                       lambda rows: rows[1].update(start_clock='mac-monotonic', display_clock='linux-monotonic'))
        result = self.f.analyze()['latency']
        self.assertEqual(result['status'], 'INCOMPLETE')
        self.assertIn('unrelated', result['reason'])

    def test_latency_uncertainty_can_turn_nominal_pass_into_failure(self):
        self.f.manifest['input'] = None
        self.f.rewrite(self.f.manifest['latency']['samples'],
                       lambda rows: [r.update(display_ns=r['start_ns'] + 95_000_000) for r in rows[1:-1]])
        result = self.f.analyze()['latency']
        self.assertEqual(result['status'], 'FAIL')
        self.assertEqual(result['nominal_p95_ms'], 95)
        self.assertEqual(result['upper_p95_ms'], 105)

    def test_p95_and_p99_nearest_rank_boundaries(self):
        self.f.manifest['input'] = None
        artifact = self.f.manifest['latency']['samples']
        def exact(rows):
            # Monotonic latency increase keeps native display order valid.
            for i, row in enumerate(rows[1:-1]):
                delay = 90_000_000 if i < 570 else 140_000_000
                row['display_ns'] = row['start_ns'] + delay
        self.f.rewrite(artifact, exact)
        result = self.f.analyze()['latency']
        self.assertEqual(result['status'], 'PASS', result)
        self.assertEqual((result['upper_p95_ms'], result['upper_p99_ms']), (100, 150))
        self.f.rewrite(artifact, lambda rows: [r.update(display_ns=r['display_ns'] + 1) for r in rows[1:-1]])
        result = self.f.analyze()['latency']
        self.assertEqual(result['status'], 'FAIL')
        self.assertTrue(any('p95' in f for f in result['failures']))
        self.assertTrue(any('p99' in f for f in result['failures']))

    def test_missing_frame_cannot_be_dropped_from_percentile(self):
        self.f.manifest['input'] = None
        self.f.rewrite(self.f.manifest['latency']['samples'], lambda rows: rows[50].update(display_ns=None))
        result = self.f.analyze()['latency']
        self.assertEqual(result['status'], 'FAIL')
        self.assertEqual(result['undisplayed_frames'], 1)

    def test_latency_frame_duplicate_missing_clock_and_reversal_deny(self):
        self.f.manifest['input'] = None
        artifact = self.f.manifest['latency']['samples']
        original = (self.f.root / artifact['path']).read_bytes()
        for change in ({'frame_id': 2}, {'display_ns': 1}, {'start_clock': 'other'}):
            artifact.update(self.f.write(artifact['path'], original))
            self.f.rewrite(artifact, lambda rows: rows[3].update(change))
            self.assertEqual(self.f.analyze()['latency']['status'], 'INCOMPLETE')

    def test_film_quantization_context_and_actual_cadence(self):
        self.f.manifest['input'] = None
        config = self.f.manifest['latency']
        config['uncertainty_ns'] = 1
        self.assertEqual(self.f.analyze()['latency']['status'], 'INCOMPLETE')
        config['uncertainty_ns'] = 10_000_000
        config['network'] = 'wifi'
        self.assertEqual(self.f.analyze()['latency']['status'], 'FAIL')
        config['network'] = 'wired'
        def burst(rows):
            for i, row in enumerate(rows[1:-1]):
                row.update(t_ns=i * 1_000_000, start_ns=i * 1_000_000, display_ns=i * 1_000_000 + 50_000_000)
        self.f.rewrite(config['samples'], burst)
        self.assertEqual(self.f.analyze()['latency']['status'], 'FAIL')

    def test_shared_instrument_clock_requires_matching_boundary(self):
        self.f.manifest['input'] = None
        config = self.f.manifest['latency']
        config['method'] = 'shared-instrument-clock'
        self.assertEqual(self.f.analyze()['latency']['status'], 'INCOMPLETE')
        config['boundary'] = 'capture-to-remote-visible'
        self.assertEqual(self.f.analyze()['latency']['status'], 'PASS')

    def test_filming_source_presentation_is_not_assumed_to_precede_capture(self):
        self.f.manifest['input'] = None
        self.f.manifest['latency']['boundary'] = 'source-visible-to-remote-visible'
        self.assertEqual(self.f.analyze()['latency']['status'], 'INCOMPLETE')

    def test_file_integrity_path_symlink_and_reuse_are_rejected(self):
        config = self.f.manifest['input']
        original = copy.deepcopy(config['source'])
        for change in ({'sha256': 'f' * 64}, {'path': '../outside'}, {'path': 'collection.txt'}):
            config['source'] = {**original, **change}
            self.assertEqual(self.f.analyze()['input']['status'], 'INCOMPLETE')
        config['source'] = original
        source = self.f.root / original['path']
        source.rename(self.f.root / 'real.jsonl')
        source.symlink_to(self.f.root / 'real.jsonl')
        self.assertEqual(self.f.analyze()['input']['status'], 'INCOMPLETE')

    def test_original_recording_hash_is_required_even_with_valid_samples(self):
        self.f.manifest['input'] = None
        self.f.manifest['latency']['recording']['sha256'] = 'f' * 64
        report = self.f.analyze()['latency']
        self.assertEqual(report['status'], 'INCOMPLETE')
        self.assertIn('SHA-256 mismatch', report['reason'])

    def test_duplicate_fields_nonfinite_boolean_and_extra_fields_deny(self):
        for data in (b'{"a":1,"a":2}', b'{"a":NaN}', b'{"a":Infinity}', b'\xff'):
            with self.assertRaises(H11.EvidenceError):
                H11.decode_json(data)
        with self.assertRaises(H11.EvidenceError):
            H11.integer(True, 'not an integer')
        self.f.manifest['unexpected'] = 1
        with self.assertRaises(H11.EvidenceError):
            self.f.analyze()

    def test_allocation_and_duration_bounds(self):
        self.f.manifest['latency'] = None
        with mock.patch.object(H11, 'MAX_ROWS', 100):
            self.assertEqual(self.f.analyze()['input']['status'], 'INCOMPLETE')
        with mock.patch.object(H11, 'MAX_TRACE_BYTES', 100):
            self.assertEqual(self.f.analyze()['input']['status'], 'INCOMPLETE')
        artifact = self.f.manifest['input']['source']
        self.f.rewrite(artifact, lambda rows: rows[-1].update(t_ns=rows[0]['t_ns'] + 601 * NS))
        self.assertEqual(self.f.analyze()['input']['status'], 'INCOMPLETE')

    def test_truncated_trace_denies_even_with_recomputed_hash(self):
        artifact = self.f.manifest['input']['source']
        data = (self.f.root / artifact['path']).read_bytes()
        artifact.update(self.f.write(artifact['path'], data[:-1]))
        self.assertEqual(self.f.analyze()['input']['status'], 'INCOMPLETE')

    def test_cli_status_codes_never_claim_h11_complete(self):
        self.f.manifest['input'] = None
        command = [sys.executable, str(SCRIPT), str(self.f.save())]
        result = subprocess.run(command, text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertFalse(json.loads(result.stdout)['h11_complete'])
        self.f.manifest['latency']['samples']['sha256'] = '0' * 64
        self.f.save()
        result = subprocess.run(command, text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(json.loads(result.stdout)['latency']['status'], 'INCOMPLETE')


if __name__ == '__main__':
    unittest.main()
