#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Synthetic resource-budget parser tests; no product performance claims."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('resource-budget.py')
spec = importlib.util.spec_from_file_location('resource_budget', SCRIPT)
budget = importlib.util.module_from_spec(spec)
spec.loader.exec_module(budget)


def fixture(duration=3600):
    start = {'event': 'start', 'format': 'deskflow-resource-soak-v2', 'created_utc': '2026-10-14T00:00:00Z',
             'platform': 'linux', 'duration_s': duration, 'interval_s': 30, 'identity_precision': 'linux-kernel-ticks',
             'cpu_resolution_s': .01, 'targets': {'viewer': {'pid': 123, 'birth': 'linux-ticks:12345'}},
             'interpretation': 'SYNTHETIC UNIT TEST; never physical evidence'}
    rows = [start]
    for at in range(0, duration + 1, 30):
        rows.append({'event': 'sample', 'elapsed_s': at, 'targets': {'viewer': {
            'observed_start_s': at + .001, 'observed_end_s': at + .002,
            'rss_bytes': 20 * budget.MIB, 'cpu_seconds': 10 + at * .1,
            'disk_read_bytes': None, 'disk_write_bytes': None}}})
    rows.append({'event': 'summary', 'elapsed_s': duration + .003, 'status': 'complete',
                 'targets': [], 'limits': []})
    return rows


class ResourceBudgetTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.path = Path(self.temporary.name) / 'trace.jsonl'
        self.rows = fixture()

    def tearDown(self):
        self.temporary.cleanup()

    def analyze(self, roles=None):
        self.path.write_text(''.join(json.dumps(r) + '\n' for r in self.rows))
        return budget.analyze(self.path, roles)

    def test_complete_synthetic_component(self):
        result = self.analyze()
        self.assertEqual(result['status'], 'PASS', result)
        self.assertFalse(result['h11_complete'])
        self.assertFalse(result['measurement_authenticity_verified'])
        self.assertEqual(result['budgetRevision'], budget.REVISION)
        self.assertGreater(result['targets']['viewer']['averageCpuPercentUpper'], 10)
        self.assertEqual(len(result['traceSha256']), 64)

    def test_short_trace_is_diagnostic_only(self):
        self.rows = fixture(60)
        result = self.analyze()
        self.assertEqual(result['status'], 'INCOMPLETE')
        self.assertIn('3600', result['reason'])
        self.assertIn('targets', result)

    def test_shrinking_rss_does_not_overstate_release_rate(self):
        self.rows[-2]['targets']['viewer']['rss_bytes'] -= budget.MIB
        result = self.analyze()
        self.assertEqual(result['status'], 'PASS', result)
        rate = result['targets']['viewer']['netRssGrowthBytesPerHourUpper']
        self.assertGreater(rate, -budget.MIB)
        self.assertLess(rate, 0)

    def test_each_predeclared_limit(self):
        for kind in ('rss', 'cpu', 'growth'):
            with self.subTest(kind=kind):
                self.rows = fixture()
                for row in self.rows[1:-1]:
                    sample = row['targets']['viewer']
                    if kind == 'rss':
                        sample['rss_bytes'] = 1024 * budget.MIB
                    if kind == 'cpu':
                        sample['cpu_seconds'] = 10 + row['elapsed_s'] * 2
                if kind == 'growth':
                    self.rows[-2]['targets']['viewer']['rss_bytes'] += 100 * budget.MIB
                result = self.analyze()
                self.assertEqual(result['status'], 'FAIL', result)
                self.assertTrue(result['failures'])

    def test_mac_resolution_cannot_create_a_nominal_pass(self):
        self.rows[0].update(platform='darwin', identity_precision='mac-ps-seconds', cpu_resolution_s=1)
        for row in self.rows[1:-1]:
            row['targets']['viewer']['cpu_seconds'] = 10 + row['elapsed_s'] * 1.9999
        self.assertEqual(self.analyze()['status'], 'FAIL')
        self.rows[0]['cpu_resolution_s'] = .01
        self.assertEqual(self.analyze()['status'], 'INCOMPLETE')

    def test_counter_regression_not_clamped_to_zero(self):
        self.rows[5]['targets']['viewer']['cpu_seconds'] = 0
        result = self.analyze()
        self.assertEqual(result['status'], 'INCOMPLETE')
        self.assertIn('regressed', result['reason'])

    def test_disk_regression(self):
        self.rows[1]['targets']['viewer']['disk_read_bytes'] = 100
        self.rows[2]['targets']['viewer']['disk_read_bytes'] = 50
        self.assertEqual(self.analyze()['status'], 'INCOMPLETE')

    def test_gap_or_reorder_cannot_hide_peak(self):
        for edit in (lambda r: r.__delitem__(slice(2, 5)),
                     lambda r: r.insert(3, copy.deepcopy(r[2])),
                     lambda r: r[-1].update(elapsed_s=1)):
            self.rows = fixture()
            edit(self.rows)
            self.assertEqual(self.analyze()['status'], 'INCOMPLETE')

    def test_unknown_target_needs_explicit_role(self):
        for row in self.rows[:-1]:
            row['targets']['worker'] = row['targets'].pop('viewer')
        self.assertEqual(self.analyze()['status'], 'INCOMPLETE')
        self.assertEqual(self.analyze({'worker': 'viewer'})['status'], 'PASS')
        self.assertEqual(self.analyze({'worker': 'unknown'})['status'], 'INCOMPLETE')

    def test_process_set_changed(self):
        self.rows[3]['targets']['foreign'] = self.rows[3]['targets'].pop('viewer')
        self.assertEqual(self.analyze()['status'], 'INCOMPLETE')

    def test_interrupted_or_legacy_is_not_a_pass(self):
        self.rows[-1]['status'] = 'incomplete'
        self.assertEqual(self.analyze()['status'], 'INCOMPLETE')
        self.rows = fixture()
        self.rows[0]['format'] = 'deskflow-resource-soak-v1'
        self.assertEqual(self.analyze()['status'], 'INCOMPLETE')

    def test_observation_window_must_be_real_and_ordered(self):
        for field, value in [('observed_start_s', -1), ('observed_end_s', 10000), ('cpu_seconds', True),
                             ('rss_bytes', 1.5), ('disk_write_bytes', -1)]:
            self.rows = fixture()
            self.rows[2]['targets']['viewer'][field] = value
            self.assertEqual(self.analyze()['status'], 'INCOMPLETE')

    def test_malformed_truncated_duplicate_and_symlink(self):
        for data in (b'{}', b'{}\n', b'{"a":1,"a":2}\n' * 4, b'NaN\n' * 4):
            self.path.write_bytes(data)
            self.assertEqual(budget.analyze(self.path)['status'], 'INCOMPLETE')
        self.analyze()
        link = self.path.with_name('link.jsonl')
        link.symlink_to(self.path)
        self.assertEqual(budget.analyze(link)['status'], 'INCOMPLETE')

    def test_cli_exit_status_and_no_budget_override(self):
        self.analyze()
        result = subprocess.run([sys.executable, str(SCRIPT), str(self.path)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([sys.executable, str(SCRIPT), str(self.path), '--cpu', '999'], capture_output=True)
        self.assertEqual(result.returncode, 2)

    def test_real_sampler_v2_is_parseable_but_short_run_is_not_acceptance(self):
        child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(10)'])
        try:
            result = subprocess.run([sys.executable, str(SCRIPT.with_name('resource-soak.py')),
                                     '--pid', f'viewer={child.pid}', '--duration', '1.1', '--interval', '1',
                                     '--output', str(self.path)], capture_output=True, text=True, timeout=6)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = budget.analyze(self.path)
            self.assertEqual(report['status'], 'INCOMPLETE', report)
            self.assertIn('3600', report['reason'])
            self.assertIn('viewer', report['targets'])
        finally:
            child.terminate()
            child.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
