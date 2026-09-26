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

    def test_invalid_measurements_fail_on_either_side(self):
        fields = [(operation, quantile)
                  for operation in ('update', 'serialization', 'accepted_edit_update')
                  for quantile in ('p50_us', 'p95_us', 'p99_us')]
        fields += [('peak_rss_kib',), ('updates_per_second',)]
        for directory in (self.baseline, self.candidate):
            path = directory / '768-edit-1.metrics.json'
            original = path.read_text()
            for fields_to_set in fields:
                for invalid in (float('nan'), float('inf'), float('-inf'),
                                0, -1, True, '1', 10 ** 400):
                    with self.subTest(side=directory.name, field=fields_to_set, value=invalid):
                        metrics = json.loads(original)
                        target = metrics
                        for field in fields_to_set[:-1]:
                            target = target[field]
                        target[fields_to_set[-1]] = invalid
                        path.write_text(json.dumps(metrics))
                        result = self.compare()
                        self.assertNotEqual(result.returncode, 0, result.stdout)
                        self.assertIn('finite and positive', result.stdout)
                        # Reject before medians/ratios; the diagnostic remains valid JSON.
                        self.assertTrue(json.loads(result.stdout)['failures'])
            path.write_text(original)

    def test_unordered_quantiles_fail(self):
        path = self.candidate / '768-edit-1.metrics.json'
        original = path.read_text()
        for operation in ('update', 'serialization', 'accepted_edit_update'):
            with self.subTest(operation=operation):
                metrics = json.loads(original)
                metrics[operation]['p95_us'] = 50
                path.write_text(json.dumps(metrics))
                result = self.compare()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('nondecreasing', result.stdout)

    def test_identical_invalid_counts_fail(self):
        paths = [directory / '768-edit-1.metrics.json'
                 for directory in (self.baseline, self.candidate)]
        original = [path.read_text() for path in paths]
        for field in ('accepted_placements', 'removed_parts', 'valid_observations',
                      'invalid_observations', 'accepted_edit_update'):
            for invalid in (float('nan'), float('inf'), float('-inf'), -1, 1.5, True, '1'):
                with self.subTest(field=field, value=invalid):
                    for path, text in zip(paths, original):
                        metrics = json.loads(text)
                        if field == 'accepted_edit_update':
                            metrics[field]['count'] = invalid
                        else:
                            metrics[field] = invalid
                        path.write_text(json.dumps(metrics))
                    result = self.compare()
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('accepted edit' if field == 'accepted_edit_update'
                                  else 'nonnegative integer', result.stdout)

    def test_zero_outcome_counts_and_fractional_timings_pass(self):
        for directory in (self.baseline, self.candidate):
            for path in directory.glob('*.metrics.json'):
                metrics = json.loads(path.read_text())
                if metrics['workload'] == 'idle':
                    metrics.update(accepted_placements=0, removed_parts=0)
                metrics.update(valid_observations=0, invalid_observations=600)
                for operation in ('update', 'serialization', 'accepted_edit_update'):
                    if metrics[operation] is not None:
                        metrics[operation].update(p50_us=.1, p95_us=.1, p99_us=.2)
                path.write_text(json.dumps(metrics))
        result = self.compare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
