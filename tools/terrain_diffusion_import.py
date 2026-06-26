#!/usr/bin/env python3
"""Import Terrain Diffusion API tiles into Voxys LDH heightmaps.

The Terrain Diffusion API returns signed int16 elevations in meters. Voxys
stores heightmaps as unsigned 16-bit samples, then maps them in shaders as:

    0      -> -height_scale
    32768  -> 0
    65535  -> +height_scale

This tool preserves sea level by default with symmetric-zero encoding, writes a
temporary .r16 file, and invokes the existing ldh_tool compressor.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zlib
from array import array
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


DEFAULT_API_URL = "http://127.0.0.1:8000"
DEFAULT_MODEL = "xandergos/terrain-diffusion-30m"

LDH_MAGIC = 0x4C444831
LDH_VERSION = 1
LDH_HEADER_SIZE = 64
LDH_FLAG_SPLIT_BYTES = 0x01
LDH_FLAG_HAS_CHECKSUM = 0x04


@dataclass(frozen=True)
class TerrainResponse:
    elevation_meters: array
    width: int
    height: int
    climate_channels: int


@dataclass(frozen=True)
class EncodedTerrain:
    samples_u16: array
    min_meters: int
    max_meters: int
    encoded_min_meters: float
    encoded_max_meters: float
    height_scale: float
    encoding: str


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert a Terrain Diffusion API tile into Voxys .ldh format."
    )
    parser.add_argument(
        "--api-url",
        default=DEFAULT_API_URL,
        help=f"Terrain Diffusion API base URL. Default: {DEFAULT_API_URL}",
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="Output .ldh path.",
    )
    parser.add_argument(
        "--raw-output",
        type=Path,
        help="Optional .r16 output path. A temp file is used when omitted.",
    )
    parser.add_argument(
        "--metadata-output",
        type=Path,
        help="Optional metadata .json path. Defaults to output path with .json suffix.",
    )
    parser.add_argument("--width", type=int, help="Tile width in samples.")
    parser.add_argument("--height", type=int, help="Tile height in samples.")
    parser.add_argument(
        "--size",
        type=int,
        help="Square tile size in samples. Used when width/height are omitted.",
    )
    parser.add_argument(
        "--origin-i",
        type=int,
        default=0,
        help="Top row coordinate passed to Terrain Diffusion.",
    )
    parser.add_argument(
        "--origin-j",
        type=int,
        default=0,
        help="Left column coordinate passed to Terrain Diffusion.",
    )
    parser.add_argument(
        "--scale",
        type=int,
        default=1,
        help="Terrain Diffusion API scale. 1 is native model resolution.",
    )
    parser.add_argument(
        "--seed",
        type=int,
        help="Optional Terrain Diffusion seed. The API changes seed when provided.",
    )
    parser.add_argument(
        "--model",
        default=DEFAULT_MODEL,
        help=f"Model name to store in metadata. Default: {DEFAULT_MODEL}",
    )
    parser.add_argument(
        "--meters-per-sample",
        type=float,
        default=30.0,
        help="Source spacing stored in metadata. Use 90 for the 90m model.",
    )
    parser.add_argument(
        "--voxy-cell-scale",
        type=float,
        help="Recommended Voxys cell_scale. Defaults to meters-per-sample / scale.",
    )
    parser.add_argument(
        "--encoding",
        choices=("symmetric-zero", "range"),
        default="symmetric-zero",
        help="Height encoding. symmetric-zero keeps 0 meters at uint16 midpoint.",
    )
    parser.add_argument(
        "--min-meters",
        type=float,
        help="Encoding minimum for range mode. Defaults to tile minimum.",
    )
    parser.add_argument(
        "--max-meters",
        type=float,
        help="Encoding maximum for range mode. Defaults to tile maximum.",
    )
    parser.add_argument(
        "--ldh-tool",
        type=Path,
        help="Path to ldh_tool. If omitted, common build locations are checked.",
    )
    parser.add_argument(
        "--keep-raw",
        action="store_true",
        help="Keep the intermediate .r16 file next to the output when raw-output is omitted.",
    )
    parser.add_argument(
        "--no-compress",
        action="store_true",
        help="Only write .r16 and metadata. Do not create .ldh.",
    )
    parser.add_argument(
        "--prefer-python-ldh",
        action="store_true",
        help="Use the Python/libzstd LDH writer even if ldh_tool is available.",
    )
    parser.add_argument(
        "--zstd-level",
        type=int,
        default=19,
        help="zstd level passed to ldh_tool. Default: 19.",
    )
    parser.add_argument(
        "--checksum",
        action="store_true",
        help="Ask ldh_tool to add an LDH checksum.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=600.0,
        help="HTTP timeout in seconds. Diffusion tiles can be slow on first use.",
    )
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=1024,
        help="Request size per Terrain Diffusion call. Lower uses less memory.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the Terrain Diffusion request and exit.",
    )
    return parser.parse_args(argv)


def resolve_size(args: argparse.Namespace) -> tuple[int, int]:
    width = args.width if args.width is not None else args.size
    height = args.height if args.height is not None else args.size
    if width is None or height is None:
        raise SystemExit("Error: provide --size or both --width and --height.")
    if width <= 0 or height <= 0:
        raise SystemExit("Error: width and height must be positive.")
    return width, height


def terrain_url_for(
    api_url: str,
    origin_i: int,
    origin_j: int,
    width: int,
    height: int,
    scale: int,
    seed: int | None,
) -> str:
    query = {
        "i1": origin_i,
        "j1": origin_j,
        "i2": origin_i + height,
        "j2": origin_j + width,
        "scale": scale,
    }
    if seed is not None:
        query["seed"] = seed
    return f"{api_url.rstrip('/')}/terrain?{urllib.parse.urlencode(query)}"


def terrain_url(args: argparse.Namespace, width: int, height: int) -> str:
    return terrain_url_for(
        args.api_url,
        args.origin_i,
        args.origin_j,
        width,
        height,
        args.scale,
        args.seed,
    )


def fetch_terrain(url: str, timeout: float) -> TerrainResponse:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            payload = response.read()
            width = int(response.headers["X-Width"])
            height = int(response.headers["X-Height"])
    except urllib.error.HTTPError as err:
        details = err.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"Terrain Diffusion HTTP {err.code}: {details}") from err
    except urllib.error.URLError as err:
        raise RuntimeError(f"Could not reach Terrain Diffusion API: {err}") from err

    elevation_bytes = width * height * 2
    if len(payload) < elevation_bytes:
        raise RuntimeError(
            f"Terrain response is too small: got {len(payload)} bytes, "
            f"need at least {elevation_bytes}."
        )

    elevation = array("h")
    elevation.frombytes(payload[:elevation_bytes])
    if sys.byteorder != "little":
        elevation.byteswap()

    climate_bytes = len(payload) - elevation_bytes
    climate_channels = climate_bytes // (width * height * 4) if width * height else 0
    return TerrainResponse(elevation, width, height, climate_channels)


def fetch_terrain_area(args: argparse.Namespace, width: int, height: int) -> TerrainResponse:
    if args.chunk_size <= 0:
        raise ValueError("--chunk-size must be positive.")

    if args.chunk_size >= max(width, height):
        return fetch_terrain(terrain_url(args, width, height), args.timeout)

    elevation = array("h", [0]) * (width * height)
    climate_channels = 0

    for row0 in range(0, height, args.chunk_size):
        chunk_h = min(args.chunk_size, height - row0)
        for col0 in range(0, width, args.chunk_size):
            chunk_w = min(args.chunk_size, width - col0)
            url = terrain_url_for(
                args.api_url,
                args.origin_i + row0,
                args.origin_j + col0,
                chunk_w,
                chunk_h,
                args.scale,
                args.seed,
            )
            print(f"  Fetching chunk row={row0} col={col0} size={chunk_w}x{chunk_h}")
            chunk = fetch_terrain(url, args.timeout)
            if chunk.width != chunk_w or chunk.height != chunk_h:
                raise RuntimeError(
                    f"Chunk returned {chunk.width}x{chunk.height}, "
                    f"expected {chunk_w}x{chunk_h}."
                )
            climate_channels = max(climate_channels, chunk.climate_channels)

            for row in range(chunk_h):
                src_start = row * chunk_w
                dst_start = (row0 + row) * width + col0
                elevation[dst_start:dst_start + chunk_w] = chunk.elevation_meters[
                    src_start:src_start + chunk_w
                ]

    return TerrainResponse(elevation, width, height, climate_channels)


def encode_elevation(
    elevation_meters: Iterable[int],
    encoding: str,
    min_override: float | None,
    max_override: float | None,
) -> EncodedTerrain:
    values = elevation_meters if isinstance(elevation_meters, array) else array("h", elevation_meters)
    if not values:
        raise ValueError("No elevation samples to encode.")

    min_meters = min(values)
    max_meters = max(values)

    if encoding == "symmetric-zero":
        max_abs = max(abs(float(min_meters)), abs(float(max_meters)), 1.0)
        inv_range = 1.0 / (2.0 * max_abs)
        samples = array("H")
        for value in values:
            encoded = round((float(value) + max_abs) * inv_range * 65535.0)
            samples.append(int(max(0.0, min(65535.0, encoded))))
        return EncodedTerrain(
            samples_u16=samples,
            min_meters=min_meters,
            max_meters=max_meters,
            encoded_min_meters=-max_abs,
            encoded_max_meters=max_abs,
            height_scale=max_abs,
            encoding=encoding,
        )

    lo = float(min_meters) if min_override is None else float(min_override)
    hi = float(max_meters) if max_override is None else float(max_override)
    if hi <= lo:
        raise ValueError("range encoding requires max-meters > min-meters.")
    inv_range = 1.0 / (hi - lo)
    samples = array("H")
    for value in values:
        encoded = round((float(value) - lo) * inv_range * 65535.0)
        samples.append(int(max(0.0, min(65535.0, encoded))))
    return EncodedTerrain(
        samples_u16=samples,
        min_meters=min_meters,
        max_meters=max_meters,
        encoded_min_meters=lo,
        encoded_max_meters=hi,
        height_scale=(hi - lo) * 0.5,
        encoding=encoding,
    )


def write_r16(path: Path, samples: Iterable[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    values = samples if isinstance(samples, array) else array("H", samples)
    if values.typecode != "H":
        values = array("H", values)
    if sys.byteorder != "little":
        values = array("H", values)
        values.byteswap()
    with path.open("wb") as file:
        values.tofile(file)


class ZstdLibrary:
    def __init__(self) -> None:
        lib_name = ctypes.util.find_library("zstd")
        if lib_name is None:
            raise RuntimeError("libzstd was not found.")

        self.lib = ctypes.CDLL(lib_name)
        self.lib.ZSTD_compressBound.argtypes = [ctypes.c_size_t]
        self.lib.ZSTD_compressBound.restype = ctypes.c_size_t
        self.lib.ZSTD_compress.argtypes = [
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.c_int,
        ]
        self.lib.ZSTD_compress.restype = ctypes.c_size_t
        self.lib.ZSTD_isError.argtypes = [ctypes.c_size_t]
        self.lib.ZSTD_isError.restype = ctypes.c_uint
        self.lib.ZSTD_getErrorName.argtypes = [ctypes.c_size_t]
        self.lib.ZSTD_getErrorName.restype = ctypes.c_char_p

    def compress(self, data: bytes | bytearray, level: int) -> bytes:
        bound = self.lib.ZSTD_compressBound(len(data))
        dst = ctypes.create_string_buffer(bound)
        src = ctypes.create_string_buffer(bytes(data), len(data))
        size = self.lib.ZSTD_compress(dst, bound, src, len(data), level)
        if self.lib.ZSTD_isError(size):
            name = self.lib.ZSTD_getErrorName(size).decode("utf-8", errors="replace")
            raise RuntimeError(f"zstd compression failed: {name}")
        return dst.raw[:size]


def predict_sample(samples: array, x: int, y: int, width: int) -> int:
    if x == 0 and y == 0:
        return 0
    if y == 0:
        return samples[x - 1]
    if x == 0:
        return samples[(y - 1) * width]

    a = samples[y * width + x - 1]
    b = samples[(y - 1) * width + x - 1]
    c = samples[(y - 1) * width + x]
    return (a + c - b) & 0xFFFF


def encode_predictor_streams(samples: array, width: int, height: int) -> tuple[bytearray, bytearray]:
    if len(samples) != width * height:
        raise ValueError("sample count does not match dimensions.")

    low = bytearray(width * height)
    high = bytearray(width * height)

    for y in range(height):
        row = y * width
        for x in range(width):
            idx = row + x
            delta = (int(samples[idx]) - predict_sample(samples, x, y, width)) & 0xFFFF
            low[idx] = delta & 0xFF
            high[idx] = delta >> 8

    return low, high


def write_ldh_python(
    path: Path,
    samples: array,
    width: int,
    height: int,
    zstd_level: int,
    checksum: bool,
) -> None:
    if len(samples) != width * height:
        raise ValueError("sample count does not match dimensions.")

    zstd = ZstdLibrary()
    low, high = encode_predictor_streams(samples, width, height)
    compressed_low = zstd.compress(low, zstd_level)
    compressed_high = zstd.compress(high, zstd_level)

    flags = LDH_FLAG_SPLIT_BYTES
    if checksum:
        flags |= LDH_FLAG_HAS_CHECKSUM

    header = struct.pack(
        "<7I9I",
        LDH_MAGIC,
        LDH_VERSION,
        width,
        height,
        flags,
        len(compressed_low),
        len(compressed_high),
        *([0] * 9),
    )
    if len(header) != LDH_HEADER_SIZE:
        raise AssertionError("LDH header packing bug.")

    output = bytearray(header)
    output.extend(compressed_low)
    output.extend(compressed_high)
    if checksum:
        output.extend(struct.pack("<I", zlib.crc32(output) & 0xFFFFFFFF))

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(output)


def write_metadata(
    path: Path,
    args: argparse.Namespace,
    terrain: TerrainResponse,
    encoded: EncodedTerrain,
    ldh_path: Path,
    raw_path: Path | None,
) -> None:
    cell_scale = args.voxy_cell_scale
    if cell_scale is None:
        cell_scale = args.meters_per_sample / float(args.scale)

    metadata = {
        "source": "terrain-diffusion",
        "model": args.model,
        "api_url": args.api_url,
        "seed": args.seed,
        "origin_i": args.origin_i,
        "origin_j": args.origin_j,
        "scale": args.scale,
        "width": terrain.width,
        "height": terrain.height,
        "meters_per_sample": args.meters_per_sample,
        "climate_channels_in_response": terrain.climate_channels,
        "elevation_meters": {
            "min": encoded.min_meters,
            "max": encoded.max_meters,
            "encoded_min": encoded.encoded_min_meters,
            "encoded_max": encoded.encoded_max_meters,
        },
        "voxy": {
            "heightmap": str(ldh_path),
            "raw_heightmap": str(raw_path) if raw_path is not None else None,
            "encoding": encoded.encoding,
            "recommended_height_scale": encoded.height_scale,
            "recommended_cell_scale": cell_scale,
            "config_snippet": {
                "terrain.heightmap": str(ldh_path),
                "terrain.height_scale": encoded.height_scale,
                "terrain.cell_scale": cell_scale,
            },
        },
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


def candidate_ldh_tools(explicit: Path | None) -> list[Path]:
    candidates: list[Path] = []
    if explicit is not None:
        candidates.append(explicit)
    env_path = os.environ.get("VOXY_LDH_TOOL")
    if env_path:
        candidates.append(Path(env_path))

    root = repo_root()
    candidates.extend(
        [
            root / "bazel-bin/tools/ldh_tool/ldh_tool",
            root / "build/bin/ldh_tool",
            root / "build/tools/ldh_tool/ldh_tool",
        ]
    )

    runfiles = os.environ.get("RUNFILES_DIR")
    if runfiles:
        base = Path(runfiles)
        candidates.extend(
            [
                base / "__main__/tools/ldh_tool/ldh_tool",
                base / "voxys/tools/ldh_tool/ldh_tool",
            ]
        )

    on_path = shutil.which("ldh_tool")
    if on_path:
        candidates.append(Path(on_path))
    return candidates


def find_ldh_tool(explicit: Path | None) -> Path | None:
    for path in candidate_ldh_tools(explicit):
        if path.exists() and os.access(path, os.X_OK):
            return path
    return None


def run_ldh_tool(
    tool_path: Path,
    raw_path: Path,
    output_path: Path,
    width: int,
    height: int,
    zstd_level: int,
    checksum: bool,
) -> None:
    command = [
        str(tool_path),
        "compress",
        str(raw_path),
        str(output_path),
        "--width",
        str(width),
        "--height",
        str(height),
        "--level",
        str(zstd_level),
    ]
    if checksum:
        command.append("--checksum")
    subprocess.run(command, check=True, cwd=repo_root())


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    width, height = resolve_size(args)
    url = terrain_url(args, width, height)

    if args.dry_run:
        print(url)
        return 0

    if args.output.suffix.lower() != ".ldh" and not args.no_compress:
        raise SystemExit("Error: --output should end in .ldh unless --no-compress is used.")

    print(f"Requesting Terrain Diffusion tile: {width}x{height}")
    print(f"  {url}")
    terrain = fetch_terrain_area(args, width, height)

    encoded = encode_elevation(
        terrain.elevation_meters,
        args.encoding,
        args.min_meters,
        args.max_meters,
    )

    output_path = args.output
    metadata_path = args.metadata_output or output_path.with_suffix(".json")

    temp_dir: tempfile.TemporaryDirectory[str] | None = None
    if args.raw_output is not None:
        raw_path = args.raw_output
        metadata_raw_path: Path | None = raw_path
    elif args.keep_raw or args.no_compress:
        raw_path = output_path.with_suffix(".r16")
        metadata_raw_path = raw_path
    else:
        temp_dir = tempfile.TemporaryDirectory(prefix="voxy_td_import_")
        raw_path = Path(temp_dir.name) / f"{output_path.stem}.r16"
        metadata_raw_path = None

    try:
        write_r16(raw_path, encoded.samples_u16)
        print(f"Wrote raw heightmap: {raw_path}")

        if not args.no_compress:
            output_path.parent.mkdir(parents=True, exist_ok=True)
            ldh_tool = None if args.prefer_python_ldh else find_ldh_tool(args.ldh_tool)
            if ldh_tool is not None:
                run_ldh_tool(
                    ldh_tool,
                    raw_path,
                    output_path,
                    terrain.width,
                    terrain.height,
                    args.zstd_level,
                    args.checksum,
                )
            else:
                print("ldh_tool not found; using Python/libzstd LDH writer.")
                try:
                    write_ldh_python(
                        output_path,
                        encoded.samples_u16,
                        terrain.width,
                        terrain.height,
                        args.zstd_level,
                        args.checksum,
                    )
                    print(f"Wrote LDH heightmap: {output_path}")
                except RuntimeError as err:
                    raise RuntimeError(
                        f"{err} Build ldh_tool with "
                        "`bazel build //tools/ldh_tool:ldh_tool`, or install libzstd."
                    ) from err

        write_metadata(metadata_path, args, terrain, encoded, output_path, metadata_raw_path)
        print(f"Wrote metadata: {metadata_path}")
        print("Suggested voxy.cfg values:")
        print(f"  terrain.heightmap = \"{output_path}\"")
        print(f"  terrain.height_scale = {encoded.height_scale:.3f}")
        cell_scale = args.voxy_cell_scale or (args.meters_per_sample / float(args.scale))
        print(f"  terrain.cell_scale = {cell_scale:.3f}")
    finally:
        if temp_dir is not None:
            temp_dir.cleanup()

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
