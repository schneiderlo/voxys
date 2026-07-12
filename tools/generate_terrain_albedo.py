#!/usr/bin/env python3
"""Bake one unlit terrain albedo from elevation and tileable materials."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


Image.MAX_IMAGE_PIXELS = None

MATERIAL_FILES = {
    "grass": "Grass001_1K-JPG_Color.jpg",
    "scrub": "Ground037_1K-JPG_Color.jpg",
    "sand": "Ground054_1K-JPG_Color.jpg",
    "rock": "Rock050_1K-JPG_Color.jpg",
    "snow": "Snow001_1K-JPG_Color.jpg",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Blend tileable CC0 materials into a top-down terrain albedo."
    )
    parser.add_argument("--heightmap", type=Path, required=True, help="Raw uint16 .r16 heightmap.")
    parser.add_argument("--metadata", type=Path, required=True, help="Terrain importer JSON metadata.")
    parser.add_argument("--materials", type=Path, required=True, help="Directory containing material color maps.")
    parser.add_argument("--output", type=Path, required=True, help="Output JPEG path.")
    parser.add_argument("--size", type=int, help="Square output size. Defaults to source resolution.")
    parser.add_argument(
        "--sea-level-meters",
        type=float,
        default=0.0,
        help="Elevation treated as the shoreline when blending materials.",
    )
    parser.add_argument("--chunk-rows", type=int, default=128, help="Rows processed per batch.")
    parser.add_argument("--quality", type=int, default=92, help="JPEG quality from 1 to 100.")
    return parser.parse_args()


def smoothstep(edge0: float, edge1: float, values: np.ndarray) -> np.ndarray:
    t = np.clip((values - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def load_material(path: Path, size: int) -> np.ndarray:
    with Image.open(path) as source:
        image = source.convert("RGB").resize((size, size), Image.Resampling.LANCZOS)
        srgb = np.asarray(image, dtype=np.float32) / 255.0
    return np.power(srgb, 2.2, dtype=np.float32)


def load_materials(directory: Path) -> dict[str, tuple[np.ndarray, np.ndarray]]:
    materials: dict[str, tuple[np.ndarray, np.ndarray]] = {}
    for name, filename in MATERIAL_FILES.items():
        path = directory / filename
        if not path.is_file():
            raise FileNotFoundError(f"Missing {name} material: {path}")
        materials[name] = (load_material(path, 256), load_material(path, 367))
    return materials


def sample_material(
    tiles: tuple[np.ndarray, np.ndarray],
    x: np.ndarray,
    y: np.ndarray,
    offset: int,
) -> np.ndarray:
    direct, rotated = tiles
    direct_size = direct.shape[0]
    rotated_size = rotated.shape[0]

    direct_u = (x + offset) % direct_size
    direct_v = (y + offset * 3) % direct_size
    color_a = direct[direct_v, direct_u]

    # A second, sheared sampling lattice breaks obvious square repetition.
    rotated_u = (x + y // 2 + offset * 5) % rotated_size
    rotated_v = (y - x // 3 + offset * 7) % rotated_size
    color_b = rotated[rotated_v, rotated_u]
    return color_a * 0.68 + color_b * 0.32


def material_weights(elevation: np.ndarray, slope: np.ndarray) -> dict[str, np.ndarray]:
    land = smoothstep(-20.0, 120.0, elevation)
    steep = smoothstep(0.30, 0.95, slope)
    high = smoothstep(1100.0, 2550.0, elevation)
    snowline = smoothstep(2150.0, 2950.0, elevation)
    shore = 1.0 - smoothstep(40.0, 420.0, elevation)
    lowland = 1.0 - smoothstep(900.0, 2050.0, elevation)

    weights = {
        # Neutral seabed and beach. The runtime water shader supplies the blue.
        "sand": (1.0 - land) + land * shore * (1.0 - steep),
        "grass": land * (1.0 - shore) * lowland * (1.0 - steep),
        "scrub": land * (1.0 - snowline) * (1.0 - steep * 0.70) * (0.28 + high * 0.72),
        "rock": land * np.maximum(steep, high * 0.58) * (1.0 - snowline * 0.72),
        "snow": land * snowline * (1.0 - steep * 0.30),
    }
    total = sum(weights.values()) + 1.0e-6
    return {name: weight / total for name, weight in weights.items()}


def decode_elevation(raw: np.ndarray, height_scale: float) -> np.ndarray:
    encoded = np.asarray(raw, dtype=np.float32)
    return (encoded * (2.0 / 65535.0) - 1.0) * height_scale


def bake(args: argparse.Namespace) -> None:
    metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
    width = int(metadata["width"])
    height = int(metadata["height"])
    output_width = args.size or width
    output_height = args.size or height
    height_scale = float(metadata["voxy"]["recommended_height_scale"])
    meters_per_sample = float(metadata["meters_per_sample"])

    expected_bytes = width * height * np.dtype("<u2").itemsize
    if args.heightmap.stat().st_size != expected_bytes:
        raise ValueError(
            f"Expected {expected_bytes} bytes for {width}x{height}, "
            f"got {args.heightmap.stat().st_size}."
        )
    if args.chunk_rows < 2:
        raise ValueError("--chunk-rows must be at least 2.")
    if output_width <= 0 or width % output_width or height % output_height:
        raise ValueError("--size must be positive and divide both source dimensions.")
    if not 1 <= args.quality <= 100:
        raise ValueError("--quality must be between 1 and 100.")

    stride_x = width // output_width
    stride_y = height // output_height
    materials = load_materials(args.materials)
    heightmap = np.memmap(args.heightmap, dtype="<u2", mode="r", shape=(height, width))
    output = Image.new("RGB", (output_width, output_height))
    x = np.arange(output_width, dtype=np.int32)[None, :]

    for row0 in range(0, output_height, args.chunk_rows):
        row1 = min(row0 + args.chunk_rows, output_height)
        source0 = max(row0 - 1, 0)
        source1 = min(row1 + 1, output_height)
        elevation_with_border = decode_elevation(
            heightmap[source0 * stride_y:source1 * stride_y:stride_y, ::stride_x],
            height_scale,
        )
        core0 = row0 - source0
        core1 = core0 + (row1 - row0)
        elevation = elevation_with_border[core0:core1]

        gradient_y, gradient_x = np.gradient(
            elevation_with_border,
            meters_per_sample * stride_y,
            meters_per_sample * stride_x,
        )
        slope = np.hypot(gradient_x[core0:core1], gradient_y[core0:core1])
        weights = material_weights(elevation - args.sea_level_meters, slope)
        y = np.arange(row0, row1, dtype=np.int32)[:, None]

        linear = np.zeros((row1 - row0, output_width, 3), dtype=np.float32)
        for offset, name in enumerate(("sand", "grass", "scrub", "rock", "snow"), start=1):
            sampled = sample_material(materials[name], x, y, offset * 31)
            linear += sampled * weights[name][..., None]

        # Broad, directionless tint variation prevents a mechanically uniform bake.
        macro = 0.96 + 0.035 * np.sin(x * 0.0071 + y * 0.0043) * np.sin(
            x * 0.0027 - y * 0.0061
        )
        linear *= macro[..., None]
        srgb = np.power(np.clip(linear, 0.0, 1.0), 1.0 / 2.2)
        pixels = np.asarray(srgb * 255.0 + 0.5, dtype=np.uint8)
        output.paste(Image.fromarray(pixels, mode="RGB"), (0, row0))
        print(f"Baked rows {row0:5d}-{row1:5d} / {output_height}", flush=True)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    output.save(
        args.output,
        "JPEG",
        quality=args.quality,
        subsampling=0,
        optimize=True,
    )
    print(f"Wrote {args.output} ({args.output.stat().st_size / (1024 * 1024):.1f} MiB)")


def main() -> int:
    bake(parse_args())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
