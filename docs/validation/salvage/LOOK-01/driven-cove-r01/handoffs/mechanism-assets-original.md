# Cove mechanism presentation assets

Prepared 2026-09-12 in scratch only. These split the accepted corrected propeller
and toy winch into a moving mesh and fixed mesh. They do not implement runtime
motion. Root owns the state/tick/renderer integration and its validation.
No live source/data files, GPU processes, screenshots or previews were used.

## Install exactly these files

- `overlay/tools/salvage_assets/author_cove_mechanisms.py`
- `overlay/data/salvage/mechanisms/r01/propeller/`
- `overlay/data/salvage/mechanisms/r01/winch/`

Copy each to the matching repository path. Every part contains three source
GLBs, an editable Blender source, source gameplay sidecar, three cooked VMESHs,
normalized sidecar, cook manifest and provenance.

Replace the existing propeller and winch **presentation** records with the exact
two records in `presentation-additions.json`. Keep canonical bundle references
and the other five presentation records unchanged. Do not append duplicates.

| Part | Final presentation directory | Cook manifest SHA256 |
|---|---|---|
| Propeller | `mechanisms/r01/propeller/cooked` | `61e53b6fb936e49d74ea7c505b1aadbf0d5955c8ff5944b22f2c59235b4e57b8` |
| Winch | `mechanisms/r01/winch/cooked` | `d86f4f0df012234b720f01a3ca13338a65273aa9819018215119e1a5e2162bc5` |

Update the five CMake WASM preload paths per part (manifest, normalized sidecar,
three LODs) from the old presentation directory to the directory above. Existing
Bazel data globs admit these files. Preserve all global memory and draw caps.
The recipe reuses the installed `author_cove_propeller_art.py` and
`author_cove_machinery_art.py`, with their common shape/material helpers. Exact
input hashes are recorded in provenance; these dependencies must remain installed.
The metric material helper was added to the input hash inventory in a metadata-only
correction; each provenance preserves the original authoring recipe hash. No
source or cooked payload was regenerated or changed for that correction.

## Renderer contract

Every GLB and cooked VMESH LOD has **exactly two mesh-bearing root nodes**.
There are no parents, children, skins, clips or animation channels. Nodes have
identity rotation and unit scale. Each moving mesh's vertices are relative to
its pivot; node translation restores its resting position.

| Role | Exact moving node name | Canonical pivot, metres | Positive canonical axis |
|---|---|---|---|
| Propeller rotor | `voxys_propeller_rotor` | `(0, 0, 0)` | `+Z` |
| Winch drum | `voxys_winch_drum` | `(0, 0.12, 0)` | `+X` |

The other node is `voxys_mechanism_static`. The declared render-to-canonical
basis remains cube rotation **12**. Source pivots are identical for these two
cases; source-local positive axes are **−Z** and **−X**, respectively. Runtime
should derive the source axis from the declared basis, then apply rotation after
the moving node's translation. Preserve normal root, part-grid and physics body
composition. Do not rotate the entire part or change sockets/collision metadata.

The propeller moving node contains the three blades, shaft/hub, sleeve and cap.
Guard, front molding, guard arms and bosses remain fixed. The winch moving node
contains the drum, flanges and cable wraps. Foot, cheeks, bearings, axle caps,
frame and fairlead remain fixed.

The winch drum radius and cable-wrap **centerline radius are both 0.28 m**.
Wrap tube radius is 0.018 m (outer radius 0.298 m); drum depth is 0.42 m.
Use 0.28 m for the approved visual cable-length-to-angle mapping. This is a
presentation ratio, with no physical RPM or spool-capacity claim. Root's driver
owns actual thrust/reel state, attachment identity, pause and accepted ticks.

Canonical gameplay fields are exact, including part IDs, mass, inertia,
collision, occupancy, buoyancy, behavior, sockets, cost and salvage yield.
LOD IDs, basis and 200/60/0 thresholds remain exact. Presentation IDs are unique
`voxys-toy-art-v1` version 1, counters **801–803** (propeller) and **901–903** (winch).

## Resting appearance and clearances

All original geometry is retained. The only intentional visible detail change is
a small **orange surface segment on each existing winch flange rim**. It reuses
the current `coral` material. No mark geometry is added, moved or layered over the
flange; the material boundary only creates a few exported vertex splits. This
makes rotation readable without changing the silhouette or mechanical gaps.

The corrected propeller retains its conservative **13.994 mm** blade-to-guard
radial gap across all angles and LODs. Its guard arms are behind the blade plane
with 25 mm axial separation; their bosses have 5 mm separation. Shaft endpoints,
ring and mounts remain unchanged. The winch retains **10.00002 mm** measured
flange-to-cheek gaps; axial rotation preserves those X bounds.

Actual cooked moving vertices also establish that the complete rotational sweep
stays within each part's current overall bounds. The recipe/provenance and
candidate report contain these numeric bounds. There is no new cable payout
geometry or alteration to the static fairlead.

## Exact storage and draw accounting

| Part | LOD | Vertices | Triangles | Material draws before → after | GPU bytes | Byte delta |
|---|---:|---:|---:|---|---:|---:|
| Propeller | 1 | 1,512 | 2,984 | 4 → 6 | 144,928 | 0 |
| Propeller | 2 | 680 | 1,320 | 4 → 6 | 65,056 | 0 |
| Propeller | 3 | 336 | 360 | 4 → 5 | 28,768 | 0 |
| Winch | 1 | 4,712 | 9,200 | 5 → 7 | 449,984 | 2,016 |
| Winch | 2 | 1,932 | 3,664 | 5 → 7 | 183,392 | 2,304 |
| Winch | 3 | 700 | 604 | 5 → 7 | 57,968 | 576 |

These two parts change from **925,200 to 930,096 requested GPU bytes**:
**+4,896 bytes**, solely 68 exported vertex splits at the orange material boundary.
Material counts and PBR factors remain unchanged. Fixed instance reservation,
16 MiB owner cap, 48 MiB resident cap and draw caps remain unchanged. Each LOD
now expands to two mesh instances; submesh draw increases are shown above.

The previous full owner projection of 10,740,488 becomes **10,745,384 bytes**.
This is CPU requested-storage accounting; runtime admission still must confirm
it. No new shaders or GPU ABI are required by these asset files.

## Executed checks and evidence

- Both strict `salvage-rigid-v1` cooks passed using existing local converter and
  C++ sidecar validator: `checks/cook-propeller-r01.log`, `checks/cook-winch-r01.log`.
- The actual VMESH node names, root relationships, mesh indices, identity
  rotations/scales and pivot translations match the contract at every LOD.
- Oriented world-space triangle positions at rest match the accepted models.
  Propeller positions and normals are exact. Winch position difference is at
  most 7.45e-9 m from pivot rebasing; maximum re-exported normal component
  difference is 1e-4 on LOD 1 and zero on the other LODs.
- Materials remain the same solid PBR factors, without textures or UV/tangent
  payloads. Propeller triangle material assignments are exact. Winch mark
  recolours 48/36/8 existing triangles across the LODs.
- Normalized gameplay metadata, LOD IDs/basis/thresholds, payload identities,
  finite unit normals, full-sweep bounds and actual GPU bytes passed.
- Final passing verifier log: `checks/verify-r04.log`. `candidate-report.json`
  contains exact hashes and measurements. Per-part provenance hashes all its
  source/cooked files and records the original accepted presentation.

The first numeric comparison used rounded-normal exact keys; a few values lay
on rounding boundaries. Its failure is retained in `checks/verify-r01.log`.
The second used an unnecessarily tight 2e-5 normal component tolerance and
reported the actual 1e-4 maximum; `checks/verify-r02.log` is retained. The final
comparison checks exact triangle correspondence, <1 micrometre positional error
and <1.1e-4 component normal error. No asset was regenerated for these verifier
corrections. `verify-r03.log` also passed before full-sweep accounting was added.

Blender completed every export, authoring check and `.blend` save, then hung in
the sandbox's PulseAudio shutdown. Only that completed process was interrupted;
its exit was **130**, not a clean exit. `checks/blender-r01.log` preserves both
part completion markers and the warning. Automatic thumbnails were disabled
before saving. No screenshots, images, rendering or GPU processes were used.

## Reproduction

From the repository root, use a new output directory under an existing parent:

```sh
PYTHONDONTWRITEBYTECODE=1 /snap/blender/current/blender \
  --background --factory-startup -noaudio --threads 2 --python-exit-code 1 \
  --python tools/salvage_assets/author_cove_mechanisms.py \
  -- --output-dir <new-output-directory>
```

For each part, inside the existing project Nix environment:

```sh
python3 tools/salvage_assets/cook_gameplay_asset.py \
  --sidecar <new-output-directory>/<part>/source/<part>.gameplay.json \
  --sources <new-output-directory>/<part>/source \
  --output <new-output-directory>/<part>/cooked \
  --converter build-native-save-host/bin/gltf_vmesh_tool \
  --validator build-native-save-host/bin/gameplay_sidecar_tool
```

Source and numeric checks do not establish runtime movement, completed-frame
rendering, owner visual approval or a completed game milestone.
