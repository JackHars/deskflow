#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Small synthetic parser fixtures only; never measurement/acceptance evidence."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('input_normalizer', HERE / 'input-evidence-normalize.py')
normalizer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(normalizer)
spec = importlib.util.spec_from_file_location('h11_analyzer', HERE / 'h11-evidence.py')
h11 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h11)


class Fixture:
    def __init__(self, directory, version=1):
        self.directory = Path(directory)
        self.values = {}
        binding = ['1'] * 11
        binding[4] = '9'
        inventory = normalizer.LEGACY_QUEUES if version == 1 else normalizer.QUEUES
        capacities = normalizer.LEGACY_CAPACITIES if version == 1 else normalizer.CAPACITIES
        for role, pid, start in [('core', 101, 1_000_000_000), ('host', 202, 3_000_000_000), ('native', 303, 7_000_000_000)]:
            names = list(inventory) if role == 'core' else list(inventory[-4:]) if role == 'host' else []
            native_owner = ['303', '44'] + ['0']*9
            rows = [{'type': 'header', 'schema': f'deskflow-input-evidence-v{version}', 'runId': 'synthetic-parser-fixture',
                     'role': role, 'pid': str(pid), 'clockId': 'fixture-' + role, 'queues': names,
                     'nativeFieldPreservation': 'NOT_RUN', 'observerProvenance': 'NOT_RUN'},
                    {'type': 'arm', 'accepted': True, 'timeNs': str(start),
                     'owner': native_owner if role == 'native' else binding.copy(),
                     'identity': '00'*96 if role == 'native' else 'ab'*96}]
            for name in names:
                for at, enqueued, retired in [(start, 0, 0), (start+1, 1, 0), (start+2, 1, 1), (start+100_000_000, 1, 1)]:
                    rows.append({'type': 'queue', 'timeNs': str(at), 'queue': name,
                                 'enqueued': str(enqueued), 'retired': str(retired),
                                 'depth': str(enqueued-retired), 'capacity': None if capacities[name] is None
                                 else str(capacities[name])})
            for index, event_type in enumerate((10, 11)):
                if role == 'host':
                    continue
                raw = ['0']*17
                raw[0], raw[2] = str(event_type), '42'
                row = {'type': 'source' if role == 'core' else 'native', 'timeNs': str(start+(index+1)*10_000_000),
                       'cgTimestampNs': str(2**53+index+11), 'flags': '0', 'raw': raw}
                if role == 'core':
                    row.update(epoch='9', sequence=str(500+index), effect='key_down' if index == 0 else 'key_up')
                else:
                    row.update(window='44', sourcePid='202', tag=str(normalizer.TAG))
                rows.append(row)
            rows.append({'type': 'end', 'timeNs': str(start+100_000_000), 'sourceEndNs': str(start+80_000_000),
                         'state': 3, 'loss': 0, 'rows': str(len(rows)-2), 'drained': True})
            self.values[role] = rows

    def save(self, role):
        path = self.directory / (role + '.jsonl')
        path.write_bytes(b''.join(normalizer.encoded(row)+b'\n' for row in self.values[role]))
        path.chmod(0o600)
        return path

    def journals(self):
        return tuple(normalizer.Journal(self.save(role), role) for role in ('core', 'host', 'native'))

    def normalize(self):
        return normalizer.normalize(*self.journals())

    def row(self, role, kind, index=0):
        return [row for row in self.values[role] if row['type'] == kind][index]

    def recount(self, role):
        self.values[role][-1]['rows'] = str(sum(row['type'] in ('source', 'native', 'hover', 'queue') for row in self.values[role]))


class NormalizeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.fixture = Fixture(self.temporary.name)

    def rejected(self):
        with self.assertRaises(normalizer.EvidenceError):
            self.fixture.normalize()

    def test_unique_exact_raw_matching_and_original_identity_retained(self):
        result = self.fixture.normalize()
        self.assertEqual([row['id'] for row in result['receipts']], [1, 2])
        self.assertEqual(result['identities'][0]['capture_sequence'], '500')
        self.assertEqual(result['identities'][0]['cg_timestamp_ns'], str(2**53+11))
        self.assertEqual(len(result['queues']), len(normalizer.LEGACY_QUEUES)+4)

    def preparation(self):
        row = {'type': 'readiness', 'timeNs': '1', 'readyCandidate': False, 'loss': 0,
               'preparation': {'snapshotAvailable': True, 'ownerLive': True, 'captureIdle': True,
                               'blockedQueues': [{'queue': 'capture', 'uninitialized': False,
                                                  'mutationBusy': False, 'activityInFlight': False,
                                                  'nonempty': True}]}}
        self.fixture.values['core'].insert(1, row)
        return row

    def test_collector_preparation_metadata_never_grants_acceptance(self):
        self.preparation()
        self.assertEqual(len(self.fixture.normalize()['receipts']), 2)
        self.fixture.row('core', 'arm')['accepted'] = False
        self.rejected()
        self.fixture.row('core', 'arm')['accepted'] = True
        self.fixture.row('native', 'native')['cgTimestampNs'] = '999'
        self.rejected()

    def test_terminal_failure_reports_original_cause_without_accepting_drained_journal(self):
        end = self.fixture.row('core', 'end')
        end.update(loss=9, failure={'site': 'unspecified', 'queue': None})
        with self.assertRaisesRegex(normalizer.EvidenceError,
                                    r'core recorder evidence loss: deadline \(9\); site=unspecified'):
            self.fixture.normalize()
        end.update(loss=4, failure={'site': 'private-buffer-read', 'queue': 'private-buffered-input'})
        with self.assertRaisesRegex(normalizer.EvidenceError,
                                    r'queue-accounting \(4\); site=private-buffer-read; queue=private-buffered-input'):
            self.fixture.normalize()

    def test_preparation_failure_reports_original_site_and_queue(self):
        row = self.preparation()
        row.update(loss=1, failure={'site': 'queue-initialize', 'queue': 'capture'})
        with self.assertRaisesRegex(normalizer.EvidenceError,
                                    r'core recorder evidence loss: contention \(1\); site=queue-initialize; queue=capture'):
            self.fixture.normalize()

    def test_all_legacy_losses_remain_failures_without_optional_diagnostics(self):
        for loss in range(1, len(normalizer.LOSS_NAMES)):
            with self.subTest(loss=loss):
                self.fixture.row('core', 'end')['loss'] = loss
                with self.assertRaisesRegex(normalizer.EvidenceError, 'core recorder evidence loss:'):
                    self.fixture.normalize()

    def test_failure_metadata_is_closed_and_cannot_override_loss(self):
        for mutation in ('zero', 'boolean', 'unknown-loss', 'negative', 'unknown-site', 'site-type',
                         'queue-type', 'foreign-queue', 'extra', 'missing', 'not-object'):
            with self.subTest(mutation=mutation):
                self.fixture = Fixture(self.temporary.name)
                end = self.fixture.row('host', 'end')
                end.update(loss=9, failure={'site': 'unspecified', 'queue': None})
                if mutation == 'zero': end['loss'] = 0
                elif mutation == 'boolean': end['loss'] = True
                elif mutation == 'unknown-loss': end['loss'] = len(normalizer.LOSS_NAMES)
                elif mutation == 'negative': end['loss'] = -1
                elif mutation == 'unknown-site': end['failure']['site'] = 'invented'
                elif mutation == 'site-type': end['failure']['site'] = []
                elif mutation == 'queue-type': end['failure']['queue'] = []
                elif mutation == 'foreign-queue': end['failure']['queue'] = 'capture'
                elif mutation == 'extra': end['failure']['accepted'] = True
                elif mutation == 'missing': del end['failure']['site']
                elif mutation == 'not-object': end['failure'] = None
                self.rejected()

    def test_preparation_metadata_remains_strict_and_bounded(self):
        for mutation in ('extra', 'flag', 'unknown-queue', 'duplicate-queue', 'no-blocker',
                         'unavailable', 'ready-with-detail', 'loss'):
            with self.subTest(mutation=mutation):
                self.fixture = Fixture(self.temporary.name)
                row = self.preparation()
                detail = row['preparation']
                queue = detail['blockedQueues'][0]
                if mutation == 'extra': detail['trusted'] = True
                elif mutation == 'flag': detail['ownerLive'] = 1
                elif mutation == 'unknown-queue': queue['queue'] = 'invented'
                elif mutation == 'duplicate-queue': detail['blockedQueues'].append(copy.deepcopy(queue))
                elif mutation == 'no-blocker': queue['nonempty'] = False
                elif mutation == 'unavailable': detail['snapshotAvailable'] = False
                elif mutation == 'ready-with-detail':
                    row.update(readyCandidate=True, owner=['1'] * 11, identity='ab' * 96)
                elif mutation == 'loss': row['loss'] = 1
                self.rejected()

    def test_output_is_unverified_and_never_claims_h11_or_latency(self):
        journals = self.fixture.journals()
        output = Path(self.temporary.name) / 'normalized'
        result = normalizer.write_normalized(output, journals, normalizer.normalize(*journals))
        self.assertEqual(result['status'], 'NORMALIZED_UNVERIFIED')
        self.assertFalse(result['h11_complete'])
        self.assertIsNone(result['latency'])
        self.assertEqual(result['native_field_preservation'], 'NOT_RUN')
        self.assertEqual(result['pending_queue_coverage']['status'], 'INCOMPLETE')
        self.assertFalse(result['pending_queue_coverage']['full_component_eligible'])
        self.assertNotEqual(result['input']['source_clock'], result['input']['receipt_clock'])
        source = [json.loads(line) for line in (output / 'source.jsonl').read_text().splitlines()]
        self.assertEqual(source[-1]['t_ns'], int(self.fixture.row('core', 'end')['sourceEndNs']))
        self.assertTrue(all(path.stat().st_mode & 0o077 == 0 for path in output.iterdir()))
        with self.assertRaises(normalizer.EvidenceError):
            normalizer.write_normalized(output, journals, normalizer.normalize(*journals))

    def test_complete_declared_inventory_round_trips_into_h11_analyzer(self):
        journals = self.fixture.journals()
        output = Path(self.temporary.name) / 'normalized'
        result = normalizer.write_normalized(output, journals, normalizer.normalize(*journals))
        # Consume the actual hashed artifacts/configuration emitted by the
        # normalizer. This intentionally short fixture must never pass H11.
        report = h11.analyze_input(h11.Bundle(output / 'normalized-input.json'), result['input'], result['run_id'])
        self.assertEqual(len(report['queues']), 19)
        self.assertEqual(report['status'], 'FAIL')
        self.assertTrue(any('shorter than 120 seconds' in failure for failure in report['failures']))
        self.assertEqual(report['capacity_certification'], 'INCOMPLETE')
        self.assertEqual(set(report['unbounded_metrics']), h11.UNBOUNDED_METRICS)
        for name in h11.UNBOUNDED_METRICS:
            self.assertIsNone(report['queues'][name]['capacity'])
            self.assertEqual(report['queues'][name]['peak_depth'], 1)
        self.assertFalse(result['pending_queue_coverage']['full_component_eligible'])

    def test_unbounded_event_metric_preserves_depth_beyond_bounded_schema(self):
        for row in self.fixture.values['core']:
            if row['type'] == 'queue' and row['queue'] == 'event-outstanding':
                row['enqueued'] = str(int(row['enqueued']) * 65537)
                row['retired'] = str(int(row['retired']) * 65537)
                row['depth'] = str(int(row['depth']) * 65537)
        result = self.fixture.normalize()
        queue = next(item for item in result['queues'] if item[1] == 'event-outstanding')
        self.assertIsNone(queue[2])
        self.assertEqual(max(int(row['depth']) for row in queue[3]), 65537)

    def test_v2_actual_bound_and_all_event_inventory_round_trip(self):
        self.fixture = Fixture(self.temporary.name, version=2)
        journals = self.fixture.journals()
        output = Path(self.temporary.name) / 'bounded'
        result = normalizer.write_normalized(output, journals, normalizer.normalize(*journals))
        report = h11.analyze_input(h11.Bundle(output / 'normalized-input.json'), result['input'], result['run_id'])
        self.assertEqual(len(report['queues']), 21)
        self.assertEqual(report['capacity_certification'], 'DECLARED')
        self.assertEqual(report['unbounded_metrics'], [])
        for name in h11.EVENT_METRICS:
            self.assertEqual(report['queues'][name]['capacity'], 64 if name.endswith('dispatch') else 4096)
        self.assertFalse(result['h11_complete'])
        self.assertEqual(result['pending_queue_coverage']['status'], 'INCOMPLETE')
        self.assertFalse(result['pending_queue_coverage']['full_component_eligible'])
        self.assertEqual(report['status'], 'FAIL')  # deliberately short synthetic input

    def test_v2_rejects_missing_all_events_null_and_overflow(self):
        for change in ('missing', 'null', 'overflow', 'mixed-version'):
            self.fixture = Fixture(self.temporary.name, version=2)
            rows = [r for r in self.fixture.values['core'] if r['type'] == 'queue' and r['queue'] == 'event-all-outstanding']
            if change == 'missing':
                self.fixture.row('core', 'header')['queues'].remove('event-all-outstanding')
            elif change == 'null':
                for row in rows:
                    row['capacity'] = None
            elif change == 'overflow':
                rows[1].update(enqueued='4097', depth='4097')
            else:
                self.fixture.row('host', 'header')['schema'] = 'deskflow-input-evidence-v1'
            with self.subTest(change=change):
                self.rejected()

    def test_null_is_only_valid_for_declared_unbounded_event_metrics(self):
        original = copy.deepcopy(self.fixture.values)
        for name, capacities in [('capture', (None, 'null', 0)),
                                 ('event-outstanding', ('256', '0', 'null', 0, False)),
                                 ('event-dispatch', ('65536', 'None', 0.0))]:
            for capacity in capacities:
                with self.subTest(name=name, capacity=capacity):
                    self.fixture.values = copy.deepcopy(original)
                    for row in self.fixture.values['core']:
                        if row['type'] == 'queue' and row['queue'] == name:
                            row['capacity'] = capacity
                    self.rejected()

    def test_unbounded_metrics_still_reject_accounting_loss_and_missing_inventory(self):
        original = copy.deepcopy(self.fixture.values)
        for mutation in ('aggregate', 'underflow', 'missing', 'overflow'):
            with self.subTest(mutation=mutation):
                self.fixture.values = copy.deepcopy(original)
                rows = [row for row in self.fixture.values['core'] if row['type'] == 'queue' and
                        row['queue'] == 'event-outstanding']
                if mutation == 'aggregate':
                    rows[1].update(enqueued='5', retired='5', depth='0')
                elif mutation == 'underflow':
                    rows[1].update(enqueued='0', retired='1', depth='0')
                elif mutation == 'missing':
                    self.fixture.row('core', 'header')['queues'].remove('event-outstanding')
                else:
                    rows[1].update(enqueued=str(2**64), depth=str(2**64))
                self.rejected()

    def test_native_timestamp_rewrite_is_not_repaired(self):
        self.fixture.row('native', 'native')['cgTimestampNs'] = '123'
        self.rejected()

    def test_native_raw_rewrite_is_not_repaired(self):
        self.fixture.row('native', 'native')['raw'][2] = '41'
        self.rejected()

    def test_native_order_is_not_used_as_correlation(self):
        rows = self.fixture.values['native']
        rows[-3], rows[-2] = rows[-2], rows[-3]
        rows[-3]['timeNs'], rows[-2]['timeNs'] = rows[-2]['timeNs'], rows[-3]['timeNs']
        self.rejected()

    def test_duplicate_native_receipt(self):
        row = copy.deepcopy(self.fixture.row('native', 'native', 1))
        self.fixture.values['native'].insert(-1, row)
        self.fixture.recount('native')
        self.rejected()

    def test_missing_native_receipt(self):
        self.fixture.values['native'].remove(self.fixture.row('native', 'native', 1))
        self.fixture.recount('native')
        self.rejected()

    def test_ambiguous_source_tuple(self):
        first = self.fixture.row('core', 'source')
        second = self.fixture.row('core', 'source', 1)
        for key in ('cgTimestampNs', 'flags', 'raw', 'effect'):
            second[key] = copy.deepcopy(first[key])
        self.rejected()

    def test_source_gap(self):
        self.fixture.row('core', 'source', 1)['sequence'] = '502'
        self.rejected()

    def test_source_epoch_substitution(self):
        self.fixture.row('core', 'source')['epoch'] = '10'
        self.rejected()

    def test_owner_focus_revision_substitution(self):
        self.fixture.row('host', 'arm')['owner'][8] = '2'
        self.rejected()

    def test_peer_identity_substitution(self):
        self.fixture.row('host', 'arm')['identity'] = 'cd'*96
        self.rejected()

    def test_run_substitution(self):
        self.fixture.row('host', 'header')['runId'] = 'different-run'
        self.rejected()

    def test_clock_alias(self):
        self.fixture.row('native', 'header')['clockId'] = self.fixture.row('core', 'header')['clockId']
        self.rejected()

    def test_native_destination_substitution(self):
        self.fixture.row('native', 'native')['window'] = '45'
        self.rejected()

    def test_native_cannot_claim_private_lease_identity(self):
        self.fixture.row('native', 'arm')['identity'] = 'ab'*96
        self.rejected()

    def test_observer_process_substitution(self):
        self.fixture.row('core', 'header')['pid'] = '202'
        self.rejected()

    def test_unrelated_native_origin(self):
        for field, value in [('sourcePid', '999'), ('tag', '0')]:
            with self.subTest(field=field):
                fixture = copy.deepcopy(self.fixture.values)
                self.fixture.row('native', 'native')[field] = value
                self.rejected()
                self.fixture.values = fixture

    def test_queue_inventory_omission(self):
        self.fixture.row('core', 'header')['queues'].remove('translation')
        self.rejected()

    def test_queue_peak_cannot_be_aggregated_away(self):
        row = self.fixture.row('core', 'queue', 1)
        row.update(enqueued='50', retired='50', depth='0')
        self.rejected()

    def test_queue_nonempty_terminal(self):
        row = self.fixture.row('core', 'queue', 3)
        row.update(enqueued='2', retired='1', depth='1')
        self.rejected()

    def test_queue_capacity_change(self):
        self.fixture.row('core', 'queue', 1)['capacity'] = '999'
        self.rejected()

    def test_consistent_capacity_substitution(self):
        for row in self.fixture.values['core']:
            if row['type'] == 'queue' and row['queue'] == 'capture':
                row['capacity'] = '512'
        self.rejected()

    def test_queue_missing_mutation_underflow(self):
        row = self.fixture.row('core', 'queue', 1)
        row.update(enqueued='0', retired='1', depth='0')
        self.rejected()

    def test_sticky_loss(self):
        self.fixture.row('core', 'end')['loss'] = 1
        self.rejected()

    def test_failed_arm(self):
        self.fixture.row('core', 'arm')['accepted'] = False
        self.rejected()

    def test_source_after_explicit_stop(self):
        self.fixture.row('core', 'source', 1)['timeNs'] = str(int(self.fixture.row('core', 'end')['sourceEndNs'])+1)
        self.rejected()

    def test_unbounded_tail(self):
        self.fixture.row('native', 'end')['timeNs'] = str(int(self.fixture.row('native', 'end')['sourceEndNs'])+1_000_000_000)
        self.rejected()

    def test_unsupported_ambiguous_modifier(self):
        self.fixture.row('core', 'source')['effect'] = 'unsupported'
        self.rejected()

    def test_explicit_hover_replacement(self):
        for index in range(2):
            source = self.fixture.row('core', 'source', index)
            native = self.fixture.row('native', 'native', index)
            source['effect'] = 'hover'
            for row in (source, native):
                row['raw'] = ['5'] + ['0']*16
                row['raw'][4] = str(index+1)
        self.fixture.values['native'].remove(self.fixture.row('native', 'native'))
        self.fixture.recount('native')
        # A pre-stop capture may be coalesced during the bounded terminal drain.
        self.fixture.values['core'].insert(-1, {'type': 'hover', 'timeNs': '1085000000', 'epoch': '9', 'sequence': '500', 'retainedSequence': '501'})
        self.fixture.recount('core')
        result = self.fixture.normalize()
        self.assertEqual([row['id'] for row in result['receipts']], [2])
        self.assertEqual(result['compressions'][0]['replacement'], 2)

    def test_false_hover_compression_cannot_hide_key(self):
        self.fixture.values['core'].insert(-1, {'type': 'hover', 'timeNs': '1030000000', 'epoch': '9', 'sequence': '500', 'retainedSequence': '501'})
        self.fixture.recount('core')
        self.rejected()

    def test_noncanonical_or_lossy_numbers(self):
        for value in (2**53+11, '01', '+1', '-0', '1.0', str(2**64)):
            with self.subTest(value=value):
                self.fixture.row('core', 'source')['cgTimestampNs'] = value
                self.rejected()

    def test_truncation_duplicate_json_and_world_readable(self):
        path = self.fixture.save('core')
        original = path.read_bytes()
        for data in (original[:-1], original.replace(b'"type":"header"', b'"type":"header","type":"header"', 1)):
            path.write_bytes(data)
            with self.assertRaises(normalizer.EvidenceError):
                normalizer.Journal(path, 'core')
        path.write_bytes(original)
        path.chmod(0o644)
        with self.assertRaises(normalizer.EvidenceError):
            normalizer.Journal(path, 'core')

    def test_symlink_rejected(self):
        path = self.fixture.save('core')
        link = Path(self.temporary.name) / 'link'
        link.symlink_to(path)
        with self.assertRaises(OSError):
            normalizer.Journal(link, 'core')


if __name__ == '__main__':
    unittest.main()
