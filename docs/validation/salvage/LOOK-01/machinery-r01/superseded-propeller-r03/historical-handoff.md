# Engine and propeller presentation candidates

Prepared 2026-09-12 in scratch only. No live source/data files were changed.
These complete the same molded cream/teal/orange machinery style as the separate
helm/winch candidate. Runtime admission and owner visual approval remain open.
No images, screenshots, previews or GPU runs were produced.

## Files and integration

- Copy `overlay/tools/salvage_assets/author_cove_power_art.py` to the corresponding
  repository path when this candidate is accepted for integration.
- Copy `overlay/data/salvage/toy-art/r03/engine/` and `propeller/` to their
  corresponding repository paths. Each contains three GLB sources, an editable
  `.blend`, exact source sidecar, three cooked VMESH payloads, normalized sidecar,
  cook manifest and provenance.
- Append the two exact records in `presentation-additions.json` to the chosen
  workshop catalog's `presentations` array. Do not replace canonical `bundles`.
- `overlay/data/salvage/cove-power-candidate-r01.json` is an optional complete
  catalog based on the current three-presentation `cove-workshop-r03.json` plus
  these two additions. It does not include the separate helm/winch additions.
  Relative asset directories resolve from the installed `data/salvage` directory.
- Root can merge these two records with the helm/winch candidate to obtain seven
  presentation replacements. Preserve the three individual-brick catalog entries.

## Compatibility and budgets

The actual installed engine and propeller are `functional-kit/r09`, part keys
`voxys-salvage-v1:4/v2` and `:5/v2`. The installed `material-calibration/r04`
machinery contains the helm and winch, not these two parts.

Every normalized non-LOD field matches the installed original exactly: canonical
part key, mass, inertia, collision, occupancy, buoyancy, module behavior, sockets,
strength, cost and salvage yield. LOD IDs, basis rotation 12 and thresholds
200/60/0 are unchanged. New visual keys use namespace `voxys-toy-art-v1`, version
1, counters 601–603 for engine and 701–703 for propeller.

The engine retains the exact cowling, bracket mounting well, lower leg and shaft
endpoints. Molded panels, small top studs and orange relief stay within the
installed visual envelope plus 2 cm. The propeller retains the shaft endpoints,
guard and three-blade sweep, adding a cream guard molding and orange hub cap.

| Part | LOD | Vertices | Triangles | Draws | Requested GPU bytes |
|---|---:|---:|---:|---:|---:|
| Engine | 1 | 2,332 | 4,480 | 6 | 222,048 |
| Engine | 2 | 1,034 | 1,918 | 6 | 97,848 |
| Engine | 3 | 644 | 412 | 6 | 51,696 |
| Propeller | 1 | 1,512 | 2,984 | 4 | 144,928 |
| Propeller | 2 | 680 | 1,320 | 4 | 65,056 |
| Propeller | 3 | 336 | 360 | 4 | 28,768 |

The two replacements request **610,344 bytes**, replacing **1,619,552 bytes**:
a reduction of **1,009,208 bytes**. The previous observed 14,522,896-byte owner
reservation projects to 13,513,688 with these two changes, or 10,740,488 with the
separate helm/winch replacement as well. These are CPU storage projections;
runtime admission must confirm the result. The 16 MiB owner and 48 MiB resident
limits remain unchanged. Both model and guide instance paths retain the fixed
128 KiB reservation; the paint ABI adds 16,384 bytes inside that existing bound.

All materials are solid PBR factors with no textures, UV seams or tangents.
The shared palette is cream `F0DDB2`, teal `197D86`, orange `ED7942`, slate
`253D53`, steel `B5C3BE`, rubber `25363A`. Plastic roughness is 0.32, rubber 0.72;
only steel has metallic factor 0.8. Factors use the existing sRGB-to-linear
conversion. Analytic manufactured-surface normals are retained through export.

## Executed checks

- Both strict `salvage-rigid-v1` CPU cooks and C++ sidecar validation passed.
  Logs: `checks/cook-engine-r01.log`, `checks/cook-propeller-r01.log`.
- `checks/verify_candidate.py` checks original manifest identity, normalized
  metadata equality, LOD bindings, all cooked hashes, finite/unit normals,
  actual cooked bounds, texture/mip accounting and requested GPU bytes.
  Passing final output: `checks/verify-candidate-r03.log`.
- `candidate-report.json` contains exact per-part results and hashes of every
  overlay file. Per-part provenance also hashes its source and cooked files.
- No image or video files exist in this candidate.

Blender 5.2.1 LTS completed all exports, `.blend` saves and authoring checks.
`file_preview_type='NONE'` suppressed automatic thumbnails. The sandbox then
hung in PulseAudio shutdown despite `-noaudio`; the already-finished process was
interrupted with exit 130. `checks/blender-r01.log` preserves both completion
markers and the shutdown warning. This is recorded as an exit limitation, not
a successful process exit. Both subsequent cooks and binary checks passed.

## Reproduction

Run from the repository root. Use a new output directory; do not overwrite the
immutable `r03` candidate. The author recipe imports existing checked-in
`author_functional_kit`, `author_cove_toy_art` and their shared helpers; their
exact hashes are in each part's provenance. The self-recipe input path was normalized
to its installed location after authoring; the original authoring hash and the
metadata-only correction are preserved explicitly. Source/cooked asset bytes did
not change during that correction.

```sh
PYTHONDONTWRITEBYTECODE=1 /snap/blender/current/blender \
  --background --factory-startup -noaudio --threads 2 --python-exit-code 1 \
  --python tools/salvage_assets/author_cove_power_art.py \
  -- --output-dir <new-output-directory>
```

For each part, use the existing CPU tools inside `nix-shell`:

```sh
python3 tools/salvage_assets/cook_gameplay_asset.py \
  --sidecar <new-output-directory>/<part>/source/<part>.gameplay.json \
  --sources <new-output-directory>/<part>/source \
  --output <new-output-directory>/<part>/cooked \
  --converter build-native-save-host/bin/gltf_vmesh_tool \
  --validator build-native-save-host/bin/gameplay_sidecar_tool
```

Propeller rotation and engine animation remain future work. Geometry checks and
strict cooking do not establish visual approval, live gameplay acceptance or a
completed art gate.
