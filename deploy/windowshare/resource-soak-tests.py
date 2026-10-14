#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""No-window, no-input tests for the read-only resource sampler."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).with_name('resource-soak.py')
SPEC = importlib.util.spec_from_file_location('deskflow_resource_soak', SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class ResourceSoakTests(unittest.TestCase):
    def test_stat_parser_with_spaces_and_closing_parenthesis(self):
        text = '123 (hello ) world) S ' + ' '.join(str(i) for i in range(4, 53))
        result = MODULE.parse_linux_stat(text, 4096, 100)
        self.assertEqual(result.birth, 'linux-ticks:22')
        self.assertEqual(result.rss_bytes, 24 * 4096)
        self.assertEqual(result.cpu_seconds, (14 + 15) / 100)

    def test_bad_linux_stat_refused(self):
        for data in ('', '1 x S', '1 (x) S 1', '99 (bad) ' + ' '.join('0' for _ in range(30))):
            with self.subTest(data=data):
                with self.assertRaises(ValueError):
                    MODULE.parse_linux_stat(data, 4096, 100)

    def test_macos_ps_process_birth_and_cpu(self):
        sample = MODULE.parse_macos_ps('Wed Oct 14 01:02:03 2026    14628    01:23:45')
        self.assertEqual(sample.birth, 'mac-ps-seconds:2026-10-14T01:02:03')
        self.assertEqual(sample.rss_bytes, 14628 * 1024)
        self.assertEqual(sample.cpu_seconds, 5025.)
        self.assertEqual(MODULE.mac_cpu_seconds('2-05:04:03.12'), 2*86400+5*3600+4*60+3.12)
        for malformed in ('?', 'Oct 14 01:02:03 2026 200 01:01:01', 'Wed Oct 14 01:02:03 2026 -100 01:01:01'):
            with self.subTest(malformed=malformed):
                with self.assertRaises(ValueError):
                    MODULE.parse_macos_ps(malformed)

    def test_pid_name_and_duration_rejections(self):
        for value in ('x=0', '../passwd=3', 'a=-5', 'a=', 'a=1;', 'x=999999999999999', 'ä=1'):
            with self.subTest(value=value):
                with self.assertRaises(Exception):
                    MODULE.target(value)
        for value in ('nan', 'inf', '-1', '0', 'hi'):
            with self.assertRaises(Exception):
                MODULE.positive_seconds(value)

    def test_strict_reused_pid_abort(self):
        with tempfile.TemporaryDirectory() as directory:
            dest = Path(directory) / 'metrics.jsonl'
            live = MODULE.Reading('birth-A', 1000, 4.0)
            reused = MODULE.Reading('birth-B', 1000, 4.5)
            with mock.patch.object(MODULE, 'probe', side_effect=[live, reused]), \
                 mock.patch.object(MODULE.time, 'sleep'):
                result = MODULE.measure({'core': 123}, 1.0, 1.0, dest)
            self.assertEqual(result, 3)
            events = [json.loads(line) for line in dest.read_text().splitlines()]
            self.assertEqual([x['event'] for x in events], ['start','failure','summary'])
            self.assertEqual(events[1]['reason'], 'ChangedProcess')
            self.assertEqual(events[-1]['status'], 'incomplete')

    def test_read_only_real_child_and_private_exclusive_evidence(self):
        with subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(5)']) as child:
            with tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'evidence.jsonl'
                cmd = [sys.executable, str(SCRIPT), '--pid', f'child={child.pid}',
                       '--duration', '1.1', '--interval', '1', '--output', str(output)]
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=6)
                self.assertEqual(result.returncode, 0, result.stderr)
                events = [json.loads(line) for line in output.read_text().splitlines()]
                self.assertEqual(events[0]['event'], 'start')
                self.assertEqual(events[0]['format'], 'deskflow-resource-soak-v2')
                self.assertGreater(events[0]['cpu_resolution_s'], 0)
                self.assertEqual(events[-1]['status'], 'complete')
                self.assertGreaterEqual(sum(x['event']=='sample' for x in events), 2)
                for sample in (x for x in events if x['event'] == 'sample'):
                    reading = sample['targets']['child']
                    self.assertLessEqual(reading['observed_start_s'], reading['observed_end_s'])
                self.assertEqual(output.stat().st_mode & 0o777, 0o600)
                self.assertNotIn('command', output.read_text())
                self.assertNotIn('sleep(5)', output.read_text())
                # Exclusive creation also rejects preexisting regular and
                # symlink paths; neither may be overwritten by a diagnostics run.
                again = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
                self.assertEqual(again.returncode, 2)
                victim = Path(directory) / 'protected.txt'
                victim.write_text('unchanged')
                malicious = Path(directory) / 'link.jsonl'
                malicious.symlink_to(victim)
                cmd[-1] = str(malicious)
                self.assertEqual(subprocess.run(cmd, capture_output=True, timeout=5).returncode, 2)
                self.assertEqual(victim.read_text(), 'unchanged')
            child.terminate()

    def test_dead_process_will_not_be_sampled_as_reused(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'evidence.jsonl'
            cmd = [sys.executable,str(SCRIPT),'--pid','dead=2147483647',
                   '--duration','1','--interval','1','--output',str(output)]
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 2)
            self.assertFalse(output.exists())

    def test_percentile_and_monotonic_cpu_stats(self):
        samples = [(1.,MODULE.Reading('A',1000,1.0)),(2.,MODULE.Reading('A',2000,1.5)),
                   (3.,MODULE.Reading('A',1500,2.0))]
        result = MODULE.summarize('viewer', samples)
        self.assertEqual(result['peak_rss_bytes'], 2000)
        self.assertEqual(result['cpu_percent_p95'], 50.)
        self.assertEqual(result['average_cpu_percent'], 50.)

    def test_live_cpu_counter_regression_invalidates_collection(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            readings = [MODULE.Reading('A', 1000, cpu) for cpu in (1, 2, 1)]
            with mock.patch.object(MODULE, 'probe', side_effect=readings), mock.patch.object(MODULE.time, 'sleep'):
                self.assertEqual(MODULE.measure({'viewer': 123}, 1, 1, path), 3)
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            self.assertEqual(rows[-1]['status'], 'incomplete')
            self.assertEqual(rows[-2]['reason'], 'ValueError')
            self.assertIn('regressed', rows[-2]['detail'])


if __name__ == '__main__':
    unittest.main()
