#!/usr/bin/env python3
"""Small self-tests for terrain_diffusion_import.py."""

from __future__ import annotations

import struct
import json
import sys
import tempfile
import unittest
from array import array
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import terrain_diffusion_import as importer


class EncodeElevationTest(unittest.TestCase):
    def test_symmetric_zero_keeps_zero_near_midpoint(self) -> None:
        encoded = importer.encode_elevation([-100, 0, 100], "symmetric-zero", None, None)

        self.assertEqual(encoded.height_scale, 100.0)
        self.assertEqual(encoded.encoded_min_meters, -100.0)
        self.assertEqual(encoded.encoded_max_meters, 100.0)
        self.assertEqual(encoded.samples_u16[0], 0)
        self.assertIn(encoded.samples_u16[1], (32767, 32768))
        self.assertEqual(encoded.samples_u16[2], 65535)

    def test_symmetric_zero_handles_one_sided_mountains(self) -> None:
        encoded = importer.encode_elevation([10, 20, 40], "symmetric-zero", None, None)

        self.assertEqual(encoded.height_scale, 40.0)
        self.assertEqual(encoded.encoded_min_meters, -40.0)
        self.assertEqual(encoded.encoded_max_meters, 40.0)
        self.assertGreater(encoded.samples_u16[0], 32768)

    def test_range_encoding_uses_override_bounds(self) -> None:
        encoded = importer.encode_elevation([10, 20, 30], "range", 0.0, 40.0)

        self.assertEqual(encoded.height_scale, 20.0)
        self.assertEqual(encoded.encoded_min_meters, 0.0)
        self.assertEqual(encoded.encoded_max_meters, 40.0)
        self.assertEqual(encoded.samples_u16[0], round(10.0 / 40.0 * 65535.0))


class WriteR16Test(unittest.TestCase):
    def test_write_r16_little_endian(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "tile.r16"
            importer.write_r16(path, [1, 256, 65535])

            self.assertEqual(path.read_bytes(), struct.pack("<3H", 1, 256, 65535))


class LdhWriterTest(unittest.TestCase):
    def test_predictor_streams_match_cpp_layout(self) -> None:
        samples = array("H", [1, 2, 3, 4])

        low, high = importer.encode_predictor_streams(samples, 2, 2)

        self.assertEqual(list(low), [1, 1, 2, 0])
        self.assertEqual(list(high), [0, 0, 0, 0])

    def test_python_ldh_writer_header(self) -> None:
        samples = array("H", [1, 2, 3, 4])

        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "tile.ldh"
            try:
                importer.write_ldh_python(path, samples, 2, 2, 1, checksum=True)
            except RuntimeError as err:
                self.skipTest(str(err))

            data = path.read_bytes()
            header = struct.unpack("<7I9I", data[: importer.LDH_HEADER_SIZE])

            self.assertEqual(header[0], importer.LDH_MAGIC)
            self.assertEqual(header[1], importer.LDH_VERSION)
            self.assertEqual(header[2], 2)
            self.assertEqual(header[3], 2)
            self.assertEqual(
                header[4],
                importer.LDH_FLAG_SPLIT_BYTES | importer.LDH_FLAG_HAS_CHECKSUM,
            )
            self.assertEqual(len(data), importer.LDH_HEADER_SIZE + header[5] + header[6] + 4)


class UrlTest(unittest.TestCase):
    def test_terrain_url_uses_height_for_i_and_width_for_j(self) -> None:
        args = importer.parse_args(
            [
                "--output",
                "out.ldh",
                "--width",
                "32",
                "--height",
                "16",
                "--origin-i",
                "7",
                "--origin-j",
                "9",
                "--seed",
                "123",
            ]
        )

        url = importer.terrain_url(args, 32, 16)

        self.assertIn("i1=7", url)
        self.assertIn("j1=9", url)
        self.assertIn("i2=23", url)
        self.assertIn("j2=41", url)
        self.assertIn("seed=123", url)


class ArgumentValidationTest(unittest.TestCase):
    def test_rejects_zero_scale_even_for_dry_run(self) -> None:
        with self.assertRaisesRegex(SystemExit, "--scale must be positive"):
            importer.main(
                ["--output", "out.ldh", "--size", "16", "--scale", "0", "--dry-run"]
            )

    def test_rejects_non_finite_physical_scale(self) -> None:
        with self.assertRaisesRegex(SystemExit, "--meters-per-sample must be positive"):
            importer.main(
                [
                    "--output",
                    "out.ldh",
                    "--size",
                    "16",
                    "--meters-per-sample",
                    "nan",
                    "--dry-run",
                ]
            )

    def test_rejects_non_finite_range_bound(self) -> None:
        with self.assertRaisesRegex(SystemExit, "--min-meters must be finite"):
            importer.main(
                [
                    "--output",
                    "out.ldh",
                    "--size",
                    "16",
                    "--encoding",
                    "range",
                    "--min-meters",
                    "nan",
                    "--dry-run",
                ]
            )


class MainTest(unittest.TestCase):
    def test_no_compress_metadata_does_not_recommend_missing_ldh(self) -> None:
        terrain = importer.TerrainResponse(array("h", [0, 1, 2, 3]), 2, 2, 0)

        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "tile.ldh"
            with mock.patch.object(importer, "fetch_terrain_area", return_value=terrain):
                result = importer.main(
                    ["--output", str(output), "--size", "2", "--no-compress"]
                )

            metadata = json.loads(output.with_suffix(".json").read_text(encoding="utf-8"))
            self.assertEqual(result, 0)
            self.assertFalse(output.exists())
            self.assertTrue(output.with_suffix(".r16").exists())
            self.assertIsNone(metadata["voxy"]["heightmap"])
            self.assertIsNone(metadata["voxy"]["config_snippet"])
            self.assertEqual(
                metadata["voxy"]["raw_heightmap"], str(output.with_suffix(".r16"))
            )


if __name__ == "__main__":
    unittest.main()
