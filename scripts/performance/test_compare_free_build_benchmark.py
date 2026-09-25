#!/usr/bin/env python3
"""Fault-injection checks for the performance acceptance guard (no GPU needed)."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

COMPARATOR = pathlib.Path(__file__).with_name('compare_free_build_benchmark.py')


class AcceptanceGuard(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.baseline = pathlib.Path(self.temp.name) / 'baseline'
        self.candidate = pathlib.Path(self.temp.name) / 'candidate'
        for directory in (self.baseline, self.candidate):
            directory.mkdir()
            for parts in (0, 64, 256, 768):
                for workload in ('idle', 'edit'):
                    for repeat in (1, 2, 3):
                        stem = directory / f'{parts}-{workload}-{repeat}'
                        distribution = {'p50_us': 100, 'p95_us': 150, 'p99_us': 200}
                        metrics = dict(scope='guard fixture', parts=parts, frames=3600,
                                       workload=workload, accepted_placements=6, removed_parts=6,
                                       valid_observations=285, invalid_observations=315,
                                       archive_sha256='fixture', terrain_sha256='fixture',
                                       update=distribution, serialization=distribution,
                                       accepted_edit_update=dict(distribution, count=10) if workload == 'edit' else None,
                                       peak_rss_kib=100000, updates_per_second=10000)
                        pathlib.Path(str(stem) + '.metrics.json').write_text(json.dumps(metrics))
                        pathlib.Path(str(stem) + '.jsonl').write_bytes(b'{"valid":true}\n')
                        pathlib.Path(str(stem) + '.save').write_bytes(b'exact save bytes')

    def compare(self):
        return subprocess.run([sys.executable, str(COMPARATOR), str(self.baseline),
                               str(self.candidate)], capture_output=True, text=True)

    def test_identical_matrix_passes(self):
        result = self.compare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_incomplete_matrix_fails(self):
        (self.candidate / '768-edit-3.metrics.json').unlink()
        self.assertNotEqual(self.compare().returncode, 0)

    def test_exact_output_mismatch_fails(self):
        for suffix in ('.jsonl', '.save'):
            with self.subTest(suffix=suffix):
                path = self.candidate / ('768-edit-1' + suffix)
                original = path.read_bytes()
                path.write_bytes(original + b'!')
                result = self.compare()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('exact output mismatch', result.stdout)
                path.write_bytes(original)

    def test_each_performance_limit_fails(self):
        paths = [self.candidate / f'768-edit-{repeat}.metrics.json' for repeat in (1, 2, 3)]
        original = [path.read_text() for path in paths]
        for field in ('update', 'serialization', 'accepted_edit_update', 'peak_rss_kib', 'updates_per_second'):
            with self.subTest(field=field):
                for path, text in zip(paths, original):
                    value = json.loads(text)
                    if field in ('update', 'serialization', 'accepted_edit_update'):
                        value[field].update(p50_us=1000, p95_us=1500, p99_us=2000)
                    else:
                        value[field] = 200000 if field == 'peak_rss_kib' else 1000
                    path.write_text(json.dumps(value))
                result = self.compare()
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(json.loads(result.stdout)['failures'])
        for path, text in zip(paths, original):
            path.write_text(text)

    def test_missing_or_changed_accepted_edit_samples_fail(self):
        path = self.candidate / '768-edit-1.metrics.json'
        original = json.loads(path.read_text())
        for samples in (None, dict(original['accepted_edit_update'], count=0),
                        dict(original['accepted_edit_update'], count=9)):
            with self.subTest(samples=samples):
                value = dict(original, accepted_edit_update=samples)
                path.write_text(json.dumps(value))
                result = self.compare()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('accepted edit', result.stdout)


if __name__ == '__main__':
    unittest.main()
