# Molded helm and winch r02

Original Blender 5.2.1 LTS presentation replacements for the canonical
`material-calibration/r04/kit/{helm,winch}`. The current selector is
`data/salvage/cove-workshop-r04.json`. Gameplay metadata, mounting wells,
collision, mass, costs and LOD thresholds/basis remain unchanged. Visual keys
use `voxys-toy-art-v1`, version 1, helm counters 401–403 and winch 501–503.

Cream/teal molded surfaces, orange accents, readable helm instruments and winch
cable relief use solid PBR factors without textures, UVs or tangents. Wheel and
drum remain static. Each part includes an editable `.blend`, three GLBs,
source/cooked sidecars, runtime VMESH files, manifest and provenance. No external
models or generated images were used. Helm/winch bounds stay inside the original
envelope; winch flange/cheek clearance is 10 mm.

From repository root, using a new output directory:

```sh
blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/salvage_assets/author_cove_machinery_art.py -- --output-dir <new-directory>
```

Then use the strict cooker/converter/validator command in
`docs/validation/salvage/LOOK-01/machinery-r01/README.md`. Do not overwrite these
visual IDs with different bytes. Provenance records exact helper/source hashes
and the metadata-only correction from scratch to installed recipe paths.
Blender completed exports/saves before a recorded PulseAudio shutdown hang;
strict cooks and final runtime admission pass. Appearance approval, animation
and the full LOOK-01 gate remain open. The linked evidence has final catalog,
budget, native journey and browser status.
