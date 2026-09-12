# D44 — molded generator and cradle candidate

**Ready for independent source review; not installed or runtime accepted.** Everything authored here is in `overlay/`. The existing generator and cradle remain live until root selects this candidate. No images, previews, app runs, GPU tests, or source/build/config edits were made for this candidate.

## What changes

The generator keeps its cream core and teal protective cage. Teal vent/service surfaces, orange corner/control accents, and four small molded roof studs match the existing Cove machinery. The cradle receives cream runners with teal outer panels, orange end pads and four molded studs. Both retain the existing functional mesh recipe for their working interfaces.

| Part | Near / middle / far vertices | Near / middle / far triangles | Draws per LOD | Maximum envelope growth |
|---|---:|---:|---:|---:|
| Generator | 2924 / 1292 / 716 | 5632 / 2408 / 512 | 5 | 9 mm, top |
| Cradle | 1620 / 734 / 564 | 2932 / 1278 / 400 | 4 | 12 mm, outside X/Z; top 10 mm |

Shared palette: cream `F0DDB2`, teal `197D86`, orange `ED7942`, slate `253D53`, steel `B5C3BE`, rubber `25363A`. Authored sRGB colors become linear PBR factors through the existing pinned helper. There are no image textures, UVs or tangents. Analytic normals and the existing material roughness/metalness settings are reused.

## Interface contract

- Canonical normalized metadata is exactly equal to the original r09 generator/cradle except presentation LOD asset/source bindings. This includes part keys, mass, collision, buoyancy, costs, footprints, sockets and module definitions. LOD IDs `1/2/3`, thresholds `200/60/0`, and basis `12` remain exact.
- Generator lift-eye torus, its mount and cargo latch pin are inherited unchanged. Their actual pre-join geometry hashes are captured before and after detail additions in provenance. The eye is centered at canonical `(0, .52, 0)` metres, with nominal inner radius `.055` metres. Every new detail above `.465` metres stays outside X `[-.30, .30]`; roof studs also stay outside that central corridor.
- Cradle floor, structural mounting well, pedestal and keyed latch housing are inherited unchanged and hash checked before joining. Every new detail stays outside the open interior X `(-.76, .76)` metres. No new geometry covers either keyed opening.
- Every authored component passes the existing closed-manifold/degenerate geometry checks before export. The added relief remains within the old bounds plus 20 mm for every LOD. No cargo-latch gameplay, animation or new cargo type is introduced.
- These are source/geometry assertions, not a claim that screenshots were inspected or new physical behavior was exercised.

## Exact installation payload

`stage-candidates-r01.json` lists 23 payload files with bytes/SHA256. Copy those paths from `overlay/` to their corresponding repository paths only after selection:

- `tools/salvage_assets/author_cove_cargo_art.py`
- `data/salvage/toy-art/r05/generator/{source,cooked}` and `provenance.json`
- `data/salvage/toy-art/r05/cradle/{source,cooked}` and `provenance.json`

Each part has a `.blend`, three GLBs, source sidecar, normalized cooked sidecar, three VMESH files, cook manifest and provenance. No external model/image is used. All recipe-input provenance paths are installed repository-relative paths, including the self recipe and `metric_materials.py`.

Presentation namespace is `766f7879732d746f792d6172742d7631` (`voxys-toy-art-v1`), version 1. Generator counters are `1001–1003`; cradle counters are `1101–1103`. These six keys were checked absent from all currently installed source/cooked sidecars.

`presentation-additions.json` is the exact two-record append. `cove-workshop-candidate-r06.json` is a complete copy of current `cove-workshop-r05.json` with only those two presentations appended (seven → nine). Recommended installed name is `data/salvage/cove-workshop-r06.json`; its paths are relative to `data/salvage`.

| Part | Candidate cook manifest SHA256 |
|---|---|
| Generator | `c9cc02f760378e8f85f63dc4fcc5354f76e10037ffdd0eb2a2fd4376dc735ddd` |
| Cradle | `01b548b7cfd5d15f6e83813a7eec6b5f28fc78259078b3c5a6e5f19baeafaeb0` |

On activation, root must add r05 cooked files and the r06 catalog to Bazel data/CMake WASM preloads, switch `salvage_cove.cfg`, and update only the affected exact selector/presentation-count/owner-byte fixture assertions. Current r05 hardcoded fixture cases are in `tests/test_fixture_registry.cpp` and `tests/test_salvage_asset_fixture.cpp`. The focused affected filter is `FixtureRegistry.MoldedMachineryPreservesEveryCanonicalPartAndPlacedTransform:FixtureGPU.CoveMoldedMachineryFitsCurrentOwnerBudget`; select r06, include generator/cradle as revised parts, and expect nine revisions. Preserve the existing 64-brick/three-palette capacity check. No shader, save, input, collision or simulation change is required.

## Requested GPU storage

Actual cooked headers/material slots were parsed with the existing `RigidPrefab` accounting: `vertices*72 + indices*4 + materials*64 + each material texture mip chain`.

| Package | Old bytes | Candidate bytes | Delta |
|---|---:|---:|---:|
| Generator, all 3 LODs | 1,051,660 | 458,688 | −592,972 |
| Cradle, all 3 LODs | 694,396 | 266,184 | −428,212 |
| Both | 1,746,056 | 724,872 | −1,021,184 |

The current combined Cove owner is 10,753,144 requested bytes. Replacing these two unique part packages projects **9,731,960 bytes**, below the unchanged **16,777,216-byte** cap. This includes the existing mechanisms/dock contribution unchanged; final owner admission must verify the projection. No draw or upload limits are raised. The existing `FixtureGPU.CoveMoldedMachineryFitsCurrentOwnerBudget` case omits dock markings: its exact expected owner becomes **9,724,200 bytes** (old 10,745,384 minus 1,021,184), while the actual app with dock markings should report **9,731,960 bytes**. Unique uploads remain 36.

## Actual checks and limitations

`checks/author-r01.log` records all six completed exports, both `.blend` saves, and both final completion records. The installed Blender 5.2.1 sandbox then hung at PulseAudio shutdown with `pa_write()` permission failure. Its completed session was interrupted, exit **130**. This is not presented as a clean Blender process exit. File preview generation was disabled; no images were emitted. No repeated authoring run was used.

Both `checks/cook-generator-r01.log` and `checks/cook-cradle-r01.log` completed with exit **0**, publishing three immutable LODs through the existing `salvage-rigid-v1` converter and gameplay validator. `candidate-report.json` records canonical metadata equality, strict LOD decrease, unique IDs, exact provenance hashes, payload hashes, zero texture bytes, geometry bounds and storage calculations.

Runtime acceptance remains for root: independent review, affected identity/owner cases, one actual visible original-Cove load/control proof with nine presentations and exact admission bytes, then the required gate before publication. Previous full mission/mechanism journeys validate the unchanged game code; this candidate does not claim another complete matrix.

## Reproduce installed recipe offline

Choose a fresh output directory with an existing parent. The recipe refuses replacement.

```sh
PYTHONDONTWRITEBYTECODE=1 /snap/blender/current/blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/salvage_assets/author_cove_cargo_art.py -- --output-dir build-cove-cargo-repeat-r01
```

For each part (`generator`, then `cradle`), cook with the existing converter/validator; the output cooked directory must not exist:

```sh
nix-shell --run 'python3 tools/salvage_assets/cook_gameplay_asset.py --sidecar build-cove-cargo-repeat-r01/generator/source/generator.gameplay.json --sources build-cove-cargo-repeat-r01/generator/source --output build-cove-cargo-repeat-r01/generator/cooked --converter build-native-save-host/bin/gltf_vmesh_tool --validator build-native-save-host/bin/gameplay_sidecar_tool'
```

Use the same command with `cradle` in the three part-specific paths. Authoring/cooking never needs a renderer or screenshots.
