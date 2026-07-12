#!/usr/bin/env python3
"""
Compare screenshots between two folders.

For every file that exists in both folders (matched by filename), the script:
  - Verifies dimensions match
  - Computes per-channel mean absolute difference and RMS difference
  - Optionally writes a visual diff image (absolute per-channel delta)

Usage:
    python scripts/compare_screenshots.py <folder_a> <folder_b> [--diff-out <dir>]

Examples:
    python scripts/compare_screenshots.py screenshots run2_screenshots --diff-out diffs
"""

import argparse
import sys
from pathlib import Path
from typing import List, Tuple

from PIL import Image, ImageChops, ImageStat


def list_pngs(folder: Path) -> List[Path]:
    return sorted(p for p in folder.glob("*.png") if p.is_file())


def load_image(path: Path) -> Image.Image:
    try:
        return Image.open(path).convert("RGBA")
    except Exception as exc:  # pragma: no cover - defensive logging
        print(f"Failed to load {path}: {exc}", file=sys.stderr)
        raise


def compare_images(path_a: Path, path_b: Path) -> Tuple[Tuple[float, float, float, float], Tuple[float, float, float, float]]:
    """
    Returns (mean_abs_diff per channel, rms_diff per channel)
    """
    img_a = load_image(path_a)
    img_b = load_image(path_b)

    if img_a.size != img_b.size:
        raise ValueError(f"Size mismatch for {path_a.name}: {img_a.size} vs {img_b.size}")

    diff = ImageChops.difference(img_a, img_b)
    stat = ImageStat.Stat(diff)

    # Mean absolute difference per channel
    mean = tuple(stat.mean)

    # RMS difference per channel
    # ImageStat.rms returns RMS of pixel values themselves; since diff image already
    # holds absolute deltas, this is effectively RMS of the per-channel deltas.
    rms = tuple(stat.rms)

    return mean, rms, diff


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare screenshots between two folders.")
    parser.add_argument("folder_a", type=Path, help="First screenshots folder")
    parser.add_argument("folder_b", type=Path, help="Second screenshots folder")
    parser.add_argument("--diff-out", type=Path, default=None, help="Optional directory to write visual diff PNGs")
    args = parser.parse_args()

    folder_a: Path = args.folder_a
    folder_b: Path = args.folder_b

    if not folder_a.is_dir() or not folder_b.is_dir():
        print("Both inputs must be directories.", file=sys.stderr)
        return 1

    if args.diff_out:
        args.diff_out.mkdir(parents=True, exist_ok=True)

    files_a = {p.name: p for p in list_pngs(folder_a)}
    files_b = {p.name: p for p in list_pngs(folder_b)}
    common = sorted(set(files_a) & set(files_b))

    if not common:
        print("No matching PNG filenames between the two folders.", file=sys.stderr)
        return 1

    print(f"Comparing {len(common)} matching files...")
    print(f"{'file':30} | {'mean abs (RGBA)':32} | {'rms (RGBA)':32}")
    print("-" * 100)

    failures = 0
    for name in common:
        path_a = files_a[name]
        path_b = files_b[name]
        try:
            mean, rms, diff_img = compare_images(path_a, path_b)
            if args.diff_out:
                diff_path = args.diff_out / name
                diff_img.save(diff_path)
            mean_str = ", ".join(f"{v:6.2f}" for v in mean)
            rms_str = ", ".join(f"{v:6.2f}" for v in rms)
            print(f"{name:30} | {mean_str:32} | {rms_str:32}")
        except Exception as exc:
            failures += 1
            print(f"{name:30} | ERROR: {exc}", file=sys.stderr)

    if failures:
        print(f"\nCompleted with {failures} failures.", file=sys.stderr)
        return 2

    print("\nAll comparisons completed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

