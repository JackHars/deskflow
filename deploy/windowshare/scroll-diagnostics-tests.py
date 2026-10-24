#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
"""Regressions for diagnosis that must never turn missing input into a pass."""
import pathlib
import runpy
import unittest

compare = runpy.run_path(str(pathlib.Path(__file__).with_name("scroll-diagnostics.py")))["compare"]


def event(point, phase=2, flags=0):
    fields = [22] + [0] * 16
    fields[9], fields[11], fields[13] = point, point * 10, point * 65536
    fields[14], fields[16] = phase, 1
    return {"raw": [str(value) for value in fields], "flags": str(flags)}


class ScrollDiagnosticTests(unittest.TestCase):
    def test_exact_sequence(self):
        source = [event(1, 1), event(2), event(3), event(0, 4)]
        result = compare(source, source)
        self.assertTrue(result["exactRawAndFlags"])
        self.assertEqual(result["combinedRecords"], [])

    def test_adjacent_sum_keeps_exact_failure(self):
        result = compare([event(1, 1), event(2), event(3), event(0, 4)],
                         [event(1, 1), event(5), event(0, 4)])
        self.assertFalse(result["exactRawAndFlags"])
        self.assertTrue(result["consecutiveSumsExplainEveryReceipt"])
        self.assertEqual(result["combinedRecords"][0]["firstSourceIndex"], 2)
        self.assertEqual(result["combinedRecords"][0]["lastSourceIndex"], 3)

    def test_loss_duplicate_order_and_tail_are_not_sums(self):
        source = [event(1, 1), event(2), event(3), event(0, 4)]
        for received in [source[:1] + source[2:], source[:2] + source[1:],
                         [source[0], source[2], source[1], source[3]], source[:-1]]:
            with self.subTest(received=received):
                result = compare(source, received)
                self.assertFalse(result["exactRawAndFlags"])
                self.assertIsNotNone(result["unexplained"])

    def test_flags_and_phase_boundaries_cannot_be_merged(self):
        for source in [[event(2, 1), event(3)], [event(2), event(3, 4)],
                       [event(2, flags=1), event(3)]]:
            with self.subTest(source=source):
                self.assertIsNotNone(compare(source, [event(5)])["unexplained"])

    def test_all_delta_representations_must_match(self):
        received = event(5)
        received["raw"][13] = str(int(received["raw"][13]) + 1)
        self.assertIsNotNone(compare([event(2), event(3)], [received])["unexplained"])

    def test_equal_total_is_not_enough(self):
        source = [event(2), event(-2), event(3)]
        self.assertIsNotNone(compare(source, [event(3)])["unexplained"])

    def test_ambiguous_zero_sum_is_rejected(self):
        self.assertIsNotNone(compare([event(2), event(3), event(0)], [event(5)])["unexplained"])


if __name__ == "__main__":
    unittest.main()
