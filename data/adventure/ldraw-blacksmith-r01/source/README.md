# Pinned Blacksmith source

This directory contains the unchanged LDraw source for **LEGO set 21325-1,
Medieval Blacksmith**, by Vincent Messenet [Cheenzo], and only its exact recursive
external dependency closure. It is the real set model assembled from identified
LDraw parts, rather than a procedural building designed to resemble LEGO.

- `21325-medieval-blacksmith.mpd`: original 195,445-byte MPD, including its 117 embedded files.
- `ldraw/parts/` and `ldraw/p/`: 742 required external part/subpart/primitive files, with original library paths and headers.
- `ldraw/LDConfig.ldr`: original LDraw colour configuration (header update 2026-05-29).
- `ldraw/CAreadme.txt`, `CAlicense.txt`, `CAlicense4.txt`: unchanged library notices and full licenses.
- `manifest.json`: source URLs, archive hash, each file's byte count/hash, authors and license headers.
- `ATTRIBUTION.md`: distributable credits, license mapping and the source author's known omissions/substitutions.
- `validate.py`: independent byte-integrity and recursive-dependency check using only this directory.

MPD SHA-256:

```text
0a7f53680569172309ebfe7053dfaccb04ea941643f1e3cbf811cf44ffd5fcee
```

Run from any working directory:

```sh
python3 /path/to/voxys/data/adventure/ldraw-blacksmith-r01/source/validate.py
```

A successful check resolves all 859 used source definitions: 117 embedded and
742 external. No download or full LDraw installation is required. Configure an
LDraw importer to use this directory's `ldraw/` as its parts library. References
are case-insensitive and may use Windows backslashes; retained file paths follow
the original archive. The source contains its own unofficial part definitions.

This package pins the downloaded bytes rather than the evolving online library.
When updating, regenerate the closure and manifest together, preserve all source
notices, re-run validation, and review the model's known substitutions. See
[attribution](ATTRIBUTION.md) before distributing converted geometry.
