# Pinned LDraw cannon source

This package contains actual LDraw cannon parts:

- `2527c01.dat`: cannon with base shortcut, by Andy Westrate [westrate].
- `2527.dat`: 2 × 4 cannon base, by Paul Easter [pneaster].
- `518.dat`: 2 × 8 non-shooting cannon, by Paul Easter [pneaster].

`cannon.mpd` is a project-authored wrapper with one instance of `2527c01.dat`:

```text
1 4 0 0 0 1 0 0 0 1 0 0 0 1 2527c01.dat
```

This selects a red base and preserves the shortcut's dark-grey barrel and fixed
tilt. The wrapper is **not an official LEGO set reconstruction**. The name
“Non-Shooting” describes the source part; it does not implement game firing,
recoil, aiming, projectiles, collision, or damage.

## Contents

- `cannon.mpd`: new project wrapper, with its exact bytes pinned in the manifest.
- `ldraw/parts/` and `ldraw/p/`: exactly 27 external dependencies, unchanged at their original library-relative paths.
- `ldraw/LDConfig.ldr`: unchanged source colour configuration.
- `ldraw/CAreadme.txt`, `CAlicense.txt`, `CAlicense4.txt`: unchanged library notices and licenses.
- `manifest.json`: per-file hashes, lengths, authors and licenses; original archive URL/hash.
- `ATTRIBUTION.md`: source credits, licensing, wrapper provenance, and modification status.
- `validate.py`: self-contained, manifest-driven integrity and dependency validator.

All 27 part/primitive source files declare CC BY 4.0. Seven file authors are
credited, with additional history credits preserved in the original headers.
See [attribution](ATTRIBUTION.md) before distributing a converted mesh.

## Validation and import

Run from any working directory:

```sh
python3 /path/to/voxys/data/adventure/ldraw-cannon-r01/source/validate.py
```

Validation uses only this directory: it checks every pinned source byte and
resolves the one embedded wrapper plus all 27 external dependencies. No download
or full library installation is required. Configure an LDraw importer to use
`source/ldraw/` as its library root and open `source/cannon.mpd`.

References are case-insensitive and may use backslashes. The dependency closure
preserves the archive's actual file paths and line endings. These source files
are pinned independently of future changes to the online library.
