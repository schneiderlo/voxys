# Salvage asset authoring

This is the ASSET-01 authoring workflow. The checked-in `authoring_probe` is
an asymmetric diagnostic block, not an approved game part. Later tasks add
gameplay metadata, importer enforcement, engine preview and the functional kit.

## Reproduce the probe

Run from the repository root with Blender 5.2.1 LTS. Use a new or empty output
directory; the generator refuses to overwrite one containing files.

```sh
/snap/bin/blender --background --factory-startup --python-exit-code 1 \
  --python tools/salvage_assets/authoring_probe.py -- \
  --output-dir /tmp/my-salvage-probe
```

On another host, substitute the actual Blender executable after checking its
version. `--python-exit-code 1` makes script exceptions fail the process. The
script also refuses interactive runs and missing factory-startup invocation.
It cannot clear an open artist scene. The standard CLI behavior is described
in the [Blender command-line manual](https://docs.blender.org/manual/en/3.0/advanced/command_line/arguments.html);
the current installed 5.2.1 CLI was exercised for this workflow.

Optional parameters are `--width 2 --length 4 --height 0.64`, in metres.
Each dimension must be finite and between 0.1 and 20. One generator owns one
output directory. Keep different agents and variations in separate directories.

Each successful run produces:

| File | Purpose |
|---|---|
| `probe.blend` | Editable source, geometry, materials, preview camera and lights |
| `probe.glb` | Selected triangle meshes with one UV set and inline PBR factors |
| `preview.png` | 512² neutral preview, Cycles CPU, four threads, 24 samples, seed zero |
| `manifest.json` | Parameters, Blender build, generator hash, export settings and output hashes |

The saved Blender file uses a relative preview path. Reopen the source before
hand editing, save under a new asset/version directory, and retain the original
generator outputs. For a reproducible hand-edited variant, preserve the edited
`.blend` as its declared source and an explicit export recipe; update provenance
to reflect that source. Re-running the original generator will not reproduce
unrecorded manual or MCP edits.

## Frames and appearance

Authoring coordinates are metre scale, +Z up, −Y forward. The small coral marker
identifies forward, teal marks +X and the elevated slate marker identifies +Z.
Object scale/rotation is applied. The body is 2 m wide × 4 m long × .64 m high
by default; markers deliberately make the full asset bounds asymmetric.

The glTF exporter applies its Y-up conversion once. The actual exported marker
translations verify `(x,y,z) → (x,z,−y)`. This export convention is described in
the [Blender glTF manual](https://docs.blender.org/manual/en/4.0/addons/import_export/scene_gltf2.html).
Voxys's canonical construction frame therefore still needs the proper
180° Y rotation `diag(−1,1,−1)`, as recorded in the manifest. That remaining
bridge is **not applied by this fixture** and must not be confused with the
full raw-Blender conversion. ASSET-02/04 must implement and verify the engine
adapter with DATA-01 before calling an asset's orientation correct in game.

Materials use supported base color/roughness/metallic factors. There are no
external textures, animations, cameras or lights in the GLB. Preview lights and
AgX output are authoring presentation only; they are not baked into material
color. The probe's UVs are quantized to 1/65536 before tangent generation to
remove observed one-ULP bevel interpolation differences. This fixture precision
is not a final texture-production standard.

## Reproducibility and review

Two clean runs with identical parameters and the recorded Blender build must
produce the same GLB bytes and decoded preview pixels. PNG metadata records
render time and source path, while `.blend` retains application/file data;
those whole-file hashes may differ and are recorded separately. Cross-version
Blender output equality is not promised. A version change requires rerunning
the comparison and the later cook/import gates.

Inspect the actual preview and validate the export. Creation of a `.blend`
alone is not completion. Evidence and known limits live in
`docs/validation/salvage/ASSET-01/`.

Run the fixture checker with Python and Pillow after generating into a fresh
directory. It verifies output hashes, export shape, marker axes, nonempty image,
and identical model bytes and decoded pixels from the same generator/build:

```sh
python3 tools/salvage_assets/check_probe.py data/salvage/authoring_probe \
  --compare /tmp/my-salvage-probe
```

This checker is specific to the trusted authoring probe. It is not the future
general asset importer or a validator for arbitrary uploaded GLB files.

## Blender MCP and image generation

No callable Blender MCP tool was present in this session's tool inventory on
2026-09-07. Blender 5.2.1 LTS was available directly; the Snap launcher required
normal-host access. No MCP was installed or required for this successful recipe.

If a functioning MCP is available later, use it for scene inspection or
iteration and retain the same sources, recipe and export checks. Save edits
explicitly. MCP control does not by itself certify mesh quality or reproducible
output. Never operate an unrelated open scene.

No AI raster input is used in this probe. Future generated color/decals/masks
must retain prompts, references and actual provider/model when exposed; inspect
seams and lighting, and bake physically coherent normal/roughness separately.
An image-generation result is not automatically a finished PBR material.
