# Molded engine r03

This shipped directory contains the **engine only**. Its canonical original is
`functional-kit/r09/engine`; `data/salvage/cove-workshop-r04.json` selects the
replacement by exact canonical and presentation manifest hashes. Visual keys
use `voxys-toy-art-v1`, version 1, counters 601–603. All non-LOD gameplay metadata,
shaft endpoint and structural well are preserved. Molded top studs and orange
relief expand the original render envelope by at most 17 mm.

Original Blender 5.2.1 LTS geometry uses solid cream/teal/orange PBR factors, no
textures or UV/tangent charts. Source `.blend`, three GLBs, metadata, cooked
VMESH files and hashed provenance are included. Engine animation is not present.

From repository root, with a fresh output directory:

```sh
blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/salvage_assets/author_cove_power_art.py -- --output-dir <new-directory>
```

Take **only the engine** from this historical pair recipe. Its original r03
propeller intersects its guard and is superseded; final propeller lives in
`toy-art/r04/propeller` and uses `author_cove_propeller_art.py`. Rejected model
hashes and the correction are preserved in
`docs/validation/salvage/LOOK-01/machinery-r01/`.

That evidence also gives the strict cooking command, executed checks and
remaining acceptance. Blender finished exports/saves but required interruption
during PulseAudio shutdown; this limitation is recorded. Provenance preserves
the original recipe hash and its later installed-path correction. Never change
published visual IDs in place. Final appearance approval remains open.
