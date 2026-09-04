#!/usr/bin/env python3
"""CPU differential model for the WGSL radix-scatter prefix optimization.

Run: uv run python tools/test_radix_scatter_prefix.py
Uses only the standard library. This checks the algorithm, NOT shader
compilation, GPU memory ordering, driver behavior, or GPU performance.
The production GPU tests and hardware A/B gate remain required.
"""
from __future__ import annotations

import random
import unittest
from collections.abc import Callable, Sequence

RADIX = 256
WORKERS = 8
GROUP_SIZES = (64, 128, 256)
Record = tuple[int, int, int, int]
Matrix = list[list[int]]
Prefix = Callable[[Matrix, int], Matrix]


def worker_major(counts: Matrix, group_size: int) -> Matrix:
    """Original: worker-major loop and a shared running sum per digit."""
    output = [row.copy() for row in counts]
    scratch = [0] * RADIX
    for worker in range(WORKERS):
        for lid in range(group_size):
            for digit in range(lid, RADIX, group_size):
                count = output[worker][digit]
                output[worker][digit] = scratch[digit]
                scratch[digit] += count
    return output


def digit_major(counts: Matrix, group_size: int) -> Matrix:
    """Candidate: each digit owner scans worker rows with a private sum.

    Reverse owner order deliberately: separate owners must be independent.
    Phase barriers are assumed here, not simulated or validated.
    """
    output = [row.copy() for row in counts]
    for lid in reversed(range(group_size)):
        for digit in range(lid, RADIX, group_size):
            prefix = 0
            for worker in range(WORKERS):
                count = output[worker][digit]
                output[worker][digit] = prefix
                prefix += count
    return output


def digit_of(record: Record, pass_index: int) -> int:
    return (record[pass_index // 4] >> ((pass_index % 4) * 8)) & 255


def local_counts(digits: Sequence[int], group_size: int) -> Matrix:
    counts = [[0] * RADIX for _ in range(WORKERS)]
    chunk_size = group_size // WORKERS
    for index, digit in enumerate(digits):
        counts[index // chunk_size][digit] += 1
    return counts


def scatter(records: Sequence[Record], group_size: int, pass_index: int,
            prefix_fn: Prefix) -> list[Record]:
    """Model a whole pass, including two overdispatched empty workgroups."""
    blocks = (len(records) + group_size - 1) // group_size + 2
    counts = []
    offsets = []
    totals = [0] * RADIX
    for block in range(blocks):
        chunk = records[block * group_size:(block + 1) * group_size]
        matrix = local_counts([digit_of(r, pass_index) for r in chunk], group_size)
        counts.append(matrix)
        offsets.append(totals.copy())
        for digit in range(RADIX):
            totals[digit] += sum(row[digit] for row in matrix)
    bases = []
    running = 0
    for total in totals:
        bases.append(running)
        running += total

    # Canary tail catches accidental writes beyond the live output range.
    canary = object()
    output: list[Record | object] = [canary] * (len(records) + 13)
    chunk_size = group_size // WORKERS
    for block in range(blocks):
        prefixes = prefix_fn(counts[block], group_size)
        for worker in range(WORKERS):
            first = min(block * group_size + worker * chunk_size, len(records))
            last = min(first + chunk_size, (block + 1) * group_size, len(records))
            for index in range(first, last):
                record = records[index]
                digit = digit_of(record, pass_index)
                destination = bases[digit] + offsets[block][digit] + prefixes[worker][digit]
                if not 0 <= destination < len(records):
                    raise AssertionError(f"out-of-range destination {destination}")
                if output[destination] is not canary:
                    raise AssertionError(f"duplicate destination {destination}")
                output[destination] = record
                prefixes[worker][digit] += 1
    if any(item is canary for item in output[:len(records)]):
        raise AssertionError("unwritten output record")
    if any(item is not canary for item in output[len(records):]):
        raise AssertionError("canary overwritten")
    return [item for item in output[:len(records)] if isinstance(item, tuple)]


def records_for(count: int, seed: int) -> list[Record]:
    rng = random.Random(seed)
    # Repeated full keys, byte-wide keys, and full-width u32 values all occur.
    key_pool = [(0, 0), (0xffffffff, 0xffffffff), (255, 0), (0, 255)]
    key_pool += [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(31)]
    result = []
    for index in range(count):
        low, high = key_pool[rng.randrange(len(key_pool))]
        result.append((low, high, rng.getrandbits(32), index))
    return result


class RadixScatterPrefixTest(unittest.TestCase):
    def test_every_digit_has_exactly_one_owner(self) -> None:
        for group_size in GROUP_SIZES:
            owners = [0] * RADIX
            for lid in range(group_size):
                for digit in range(lid, RADIX, group_size):
                    owners[digit] += 1
            self.assertEqual(owners, [1] * RADIX)

    def test_every_partial_workgroup_and_skew(self) -> None:
        rng = random.Random(0x51a7c0de)
        for group_size in GROUP_SIZES:
            for live_count in range(group_size + 1):
                patterns = (
                    [0] * live_count,
                    [255] * live_count,
                    [i % 2 * 255 for i in range(live_count)],
                    [i % RADIX for i in range(live_count)],
                    [i // (group_size // WORKERS) for i in range(live_count)],
                    [rng.randrange(RADIX) for _ in range(live_count)],
                )
                for pattern_index, digits in enumerate(patterns):
                    with self.subTest(group=group_size, live=live_count, pattern=pattern_index):
                        counts = local_counts(digits, group_size)
                        expected = [[sum(counts[w][d] for w in range(worker))
                                     for d in range(RADIX)] for worker in range(WORKERS)]
                        self.assertEqual(worker_major(counts, group_size), expected)
                        self.assertEqual(digit_major(counts, group_size), expected)

    def test_all_bytes_against_stable_bucket_oracle(self) -> None:
        for group_size in GROUP_SIZES:
            chunk = group_size // WORKERS
            sizes = sorted({0, 1, 7, 8, 9, chunk - 1, chunk, chunk + 1,
                            group_size - 1, group_size, group_size + 1,
                            2 * group_size - 1, 2 * group_size + 1, 8193})
            for count in sizes:
                records = records_for(count, count + group_size)
                for pass_index in range(8):
                    with self.subTest(group=group_size, count=count, byte=pass_index):
                        expected = sorted(records, key=lambda r: digit_of(r, pass_index))
                        self.assertEqual(scatter(records, group_size, pass_index, worker_major), expected)
                        self.assertEqual(scatter(records, group_size, pass_index, digit_major), expected)

    def test_full_32_high_word_and_64_bit_stable_sort(self) -> None:
        for group_size in GROUP_SIZES:
            for count in (1, group_size + 1, 8193):
                records = records_for(count, 0x1234 + count)
                modes = ((range(4), lambda r: r[0]),
                         (range(4, 8), lambda r: r[1]),
                         (range(8), lambda r: (r[1], r[0])))
                for passes, key in modes:
                    with self.subTest(group=group_size, count=count, passes=list(passes)):
                        result = records
                        for pass_index in passes:
                            result = scatter(result, group_size, pass_index, digit_major)
                        # Compare all four words, not just sorted keys.
                        self.assertEqual(result, sorted(records, key=key))


if __name__ == "__main__":
    unittest.main(verbosity=2)
