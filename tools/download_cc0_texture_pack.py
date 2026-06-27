#!/usr/bin/env python3
"""Download a small CC0 terrain material pack from ambientCG.

The files are written under data/generated so they stay out of the repository by
default. ambientCG materials are CC0/Public Domain; keep the generated manifest
with any redistributed copy of the downloaded files.
"""

from __future__ import annotations

import csv
import json
import shutil
import sys
import urllib.request
import zipfile
from dataclasses import dataclass
from io import StringIO
from pathlib import Path


DOWNLOADS_CSV = "https://ambientcg.com/api/v2/downloads_csv?id={asset_id}"


@dataclass(frozen=True)
class Material:
    role: str
    asset_id: str


MATERIALS = [
    Material("grass", "Grass001"),
    Material("forest_floor", "Ground037"),
    Material("dry_ground", "Ground054"),
    Material("rock", "Rock050"),
    Material("snow", "Snow001"),
]


def fetch_text(url: str) -> str:
    request = urllib.request.Request(url, headers={"User-Agent": "voxys-asset-tool/0.1"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read().decode("utf-8")


def download(url: str, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(url, headers={"User-Agent": "voxys-asset-tool/0.1"})
    with urllib.request.urlopen(request, timeout=300) as response:
        with path.open("wb") as out:
            shutil.copyfileobj(response, out)


def download_link_for(asset_id: str) -> str:
    rows = list(csv.DictReader(StringIO(fetch_text(DOWNLOADS_CSV.format(asset_id=asset_id)))))
    for row in rows:
        if row.get("assetId") == asset_id and row.get("downloadAttribute") == "1K-JPG":
            return row["downloadLink"]
    raise RuntimeError(f"No 1K-JPG ambientCG download found for {asset_id}")


def extract_color_maps(zip_path: Path, out_dir: Path, role: str) -> list[str]:
    written: list[str] = []
    with zipfile.ZipFile(zip_path) as archive:
        for name in archive.namelist():
            lower = name.lower()
            if lower.endswith((".jpg", ".jpeg", ".png")) and ("color" in lower or "albedo" in lower):
                suffix = Path(name).suffix.lower()
                out_path = out_dir / f"{role}_color{suffix}"
                with archive.open(name) as src, out_path.open("wb") as dst:
                    shutil.copyfileobj(src, dst)
                written.append(out_path.name)
    return written


def main(argv: list[str]) -> int:
    root = Path(argv[1]) if len(argv) > 1 else Path("data/generated/cc0_texture_pack")
    zips_dir = root / "zips"
    maps_dir = root / "maps"
    root.mkdir(parents=True, exist_ok=True)
    maps_dir.mkdir(parents=True, exist_ok=True)

    manifest = {
        "source": "ambientCG",
        "license": "CC0 / Public Domain",
        "license_url": "https://ambientcg.com/license/",
        "download_attribute": "1K-JPG",
        "materials": [],
    }

    for material in MATERIALS:
        print(f"Resolving {material.asset_id} for {material.role}...")
        link = download_link_for(material.asset_id)
        zip_path = zips_dir / f"{material.asset_id}_1K-JPG.zip"
        if not zip_path.exists():
            print(f"Downloading {link}")
            download(link, zip_path)
        else:
            print(f"Using cached {zip_path}")

        maps = extract_color_maps(zip_path, maps_dir, material.role)
        manifest["materials"].append(
            {
                "role": material.role,
                "asset_id": material.asset_id,
                "download_url": link,
                "zip": str(zip_path.relative_to(root)),
                "color_maps": maps,
            }
        )

    manifest_path = root / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
