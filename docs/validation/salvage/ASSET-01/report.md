# ASSET-01 — repeatable Blender authoring

Status: complete. Root implemented and verified the workflow; `lego_gameplay`
reviewed the initial implementation and `render_architecture` reviewed the final
camera and generator/build-hash checks without required corrections.
Date: 2026-09-07. Base revision: `7563f61fd536df7de5d209ff35d3cd2099ebcbb6`;
working branch `codex/salvage-implementation`. Exact source hashes are in
[reviewed-source-hashes.json](reviewed-source-hashes.json).

## Delivered

- [Authoring instructions](../../../../tools/salvage_assets/README.md), versioned
  Python generator, explicit export settings and fixture checker.
- [Editable source](../../../../data/salvage/authoring_probe/probe.blend), GLB,
  rendered preview, and manifest with asset/version, dimensions, seed, source
  digest, Blender build, provenance and output hashes.
- Separate output directories; nonempty directories and symlink outputs are
  refused before changing the factory scene. Background/factory-startup guards
  prevent running this recipe in an unrelated interactive scene.

No callable Blender MCP tool was available. The installed Blender was used
through `bpy` in a fresh headless process. No MCP installation or image service
was needed. The workflow leaves room for saved, documented MCP edits later;
no MCP reproducibility is claimed without an actual MCP run.

## Executed validation

Installed `/snap/bin/blender`: **5.2.1 LTS**, build `9e2066aef7ef`.
Snap requires normal-host permissions on this host. CPU rendering uses four
threads, 24 Cycles samples, seed zero, AgX and a 512 × 512 image.

Run from the repository root, using a fresh output directory:

```sh
/snap/bin/blender --background --factory-startup --python-exit-code 1 \
  --python tools/salvage_assets/authoring_probe.py -- \
  --output-dir data/salvage/authoring_probe
/snap/bin/blender --background --factory-startup --python-exit-code 1 \
  --python tools/salvage_assets/authoring_probe.py -- \
  --output-dir /tmp/voxys-salvage-authoring-reviewed-verify
python3 tools/salvage_assets/check_probe.py data/salvage/authoring_probe \
  --compare /tmp/voxys-salvage-authoring-reviewed-verify
```

The first command is the original generation recipe. **Do not rerun it over
the delivered folder.** Use another empty directory when reproducing this
report. Both generation processes exited 0. See [generation.log](generation.log),
[regeneration.log](regeneration.log), and [comparison](regeneration-check.json).

Both runs produced exactly the same 102,836-byte GLB:
`e2e4f6a7b0d95ac7eff48e2cf6c391c3605f288629b70e7d3d0009fc1735b103`.
Decoded RGBA preview pixels also match:
`a4249d3cf5d9612ae5408841cd408eaeb4a7b013a9136940371a1d3a2f2cba28`.
The checker compares generator hash, parameters, Blender version **and build
hash**, mesh/attribute profile, marker positions, output hashes, and image
content. The GLB contains four meshes, four materials and 752 triangles.

PNG metadata contains timing/path information; PNG file bytes need not match
when decoded pixels do. `.blend` whole-file identity is not promised. Each
actual file hash is retained in its manifest.

## Review fixes and negative checks

The first two runs revealed one-ULP bevel UV differences and consequent tangent
differences. Those outputs and logs are retained under `initial-probe/` and
`initial-*.log`. Quantizing this diagnostic fixture's UVs to 1/65536 before
tangent generation removed the instability. This is an explicit fixture
precision choice, not a global texture-production standard.

Independent review found that permitted tall dimensions could leave the initial
camera frame. The camera now fits all actual world-space mesh bounding-box
corners. A separate `.1 × .1 × 20 m` run exited 0 and passed the checker;
[its image](tall-preview.png) was visually inspected and shows the entire narrow
asset within the frame. See [tall check](tall-variant-check.json) and
[tall log](tall-variant.log). The default final image was also visually reviewed:
all three asymmetric axis markers are visible and the model is not clipped.

A repeated invocation targeting an existing output folder exits 2 with an
explicit refusal. The earlier overwrite rejection log is retained in
[overwrite-rejected.log](overwrite-rejected.log); the final repeated invocation also exited 2, recorded in
[final-overwrite-rejected.log](final-overwrite-rejected.log).
The [post-refusal checker](post-overwrite-check.json) passed: all existing
output hashes and the clean-regeneration comparison remain unchanged.

Python syntax checks pass. This is asset-tool verification; it is not a claim
that the repository's complete C++ hook suite ran for this task.

## Coordinates and limits

The fixture uses metres, +Z up and −Y forward in Blender. Raw GLB export converts
`(x,y,z)` to `(x,z,−y)` once; exported asymmetric marker positions verify that
conversion. The manifest records the remaining canonical 180° Y rotation,
`diag(−1,1,−1)`. It is **not applied here**. ASSET-02/04 must implement and check
that adapter before claiming correct in-game orientation.

These are original procedural diagnostic meshes with inline PBR factors and
one UV set, without external images. No third-party references or AI texture
inputs were used. No production part, collision/flotation metadata, runtime
cooker, game preview, final artwork or human art approval is claimed by ASSET-01.
The fixture checker is not a general validator for untrusted GLB data.

The actual installed CLI/exporter was exercised. Background invocation and
export conventions were cross-checked with the official
[Blender CLI manual](https://docs.blender.org/manual/en/3.0/advanced/command_line/arguments.html)
and [glTF manual](https://docs.blender.org/manual/en/4.0/addons/import_export/scene_gltf2.html).
