# First kit and static starter assemblies

2026-09-08. **Scoped implementation checkpoint; ASSET-05 remains open.**
Root implemented and inspected this work. Independent review is unavailable
under D18; neither that review nor ASSET-04 prerequisite acceptance is waived.
No gate passed and no gate commit was made. The active goal remains all of G14.

## What now exists

`data/salvage/functional-kit/r08/` contains nine original Blender assets:
beam, plate, engine, propeller, helm, fixed winch, cradle, generator and crate.
Each has editable `.blend`, three embedded-texture GLBs, canonical metadata,
three strictly cooked VMESH files, an authoring preview and provenance.
The frozen recipes are alongside the assets. The existing pontoon v2-rc01 is
reused unchanged. No image-generation output or external model was used.

Equipment keeps the existing salvage content IDs with candidate version 2.
Cargo uses its own namespace. Visual IDs have explicit reserved counter ranges
in `functional_kit.py`; definition order does not assign future IDs. Nothing
replaces the v1 prototype catalog or grants cargo mission/economic behavior.

The three installed registries are:

| Inspection | Native config | Browser experience |
|---|---|---|
| Narrow starter | `salvage_kit_narrow.cfg` | `salvage-kit-narrow` |
| Broad starter | `salvage_kit_broad.cfg` | `salvage-kit-broad` |
| Cargo and cradle | `salvage_kit_cargo.cfg` | `salvage-kit-cargo` |

Both boats contain eleven authored placements, seventeen exact welded socket
connections, eight bundles and twenty-four resident LOD assets. Pontoons sit
at x=±.5 m or ±1.5 m. Each empty boat is 1,035 kg. The wider arrangement adds
480 kg·m² to yaw and roll inertia, with unchanged mass and world COM. All 24
proper build orientations compile to one connected rigid root. A one-tick
misalignment rejects. These are CPU compilation results, not live handling.

The generator is 420 kg; the offset-eye crate is 700 kg. Their tow and latch
sockets have compatible types, with matching authored latch frames. The
cradle is .64 m high so its opposite insertion regions cannot collide. Cargo
is displayed at the seat but is not dynamically captured or banked.

The outboard shaft is at (x=.5, y=−.48, z=2.4) m in the starter frame; the
propeller origin is z=2.72 m, behind the deck. A deliberately limited,
level-water estimate using only the two sealed pontoons at 1,000 kg/m³ gives
water heights −.327407/−.272221/−.235641 m for 1,035/1,455/1,735 kg. It caught
the first outboard being too high. The corrected center is at least .10 m
under those estimated surfaces. This omits trim, waves, other solid cores,
crew and live force production; it does not establish sailing acceptance.

## Executed checks

| Check | Final result | Evidence |
|---|---|---|
| Blender 5.2.1 LTS background authoring | 9 parts, 27 LODs; closed outward components, no degenerate faces | `author-r08.log`, per-part `provenance.json` |
| Real strict converter and C++ metadata validator | All 9 bundles cooked | `tool-build.log`, `cook-r08.log`, exact per-bundle cook manifests |
| Optimized Bazel | 4 kit cases pass, fresh execution | `native-kit-r08.log`, `bazel-kit-r08.xml` |
| Native CMake | 4 kit cases pass; full native application build passes | `cmake-kit-r08.log`, `cmake-kit-r08.xml` |
| Actual WASM CPU, JS exceptions + Asyncify | All 74 shared asset/kit cases pass; no skips; inputs unchanged | `wasm-cpu-r08/manifest.json`, `wasm-cpu-r08/tests.log` |
| Full browser application package | Build passes | `wasm-app-r08.log`, `browser-package-r08.json` |
| Real native and hardware Chrome/Wayland | Four matching view pairs across all three routes; no application/GPU errors | `native-{broad,narrow,cargo}-r08/report.json`, `browser-*-r08-summary.json` |

The CPU WASM diagnostic uses a fixed 128 MiB heap and 1 MiB stack to hold the
embedded asset fixtures and temporary test copies. The application retains its
512 MiB heap. This is not a memory/performance gate. The original 70-case
asset diagnostic remains its default; `--functional-kit` adds the four cases.

At 1920×1080, FOV 60, both runtime skiffs report 24 uploads, 11 model draws,
zero prototype uploads/physics bodies and **12,977,224 requested GPU bytes**
including the fixed owner reservation. Cargo reports 12 uploads, 4 draws and
4,773,480 bytes. The existing 16 MiB owner ceiling was retained. Kit palette
maps are 128/64/64 pixels; the inspected pontoon keeps 512/256/64 maps. These
are requested allocations, not a driver-resident working-set measurement.

Root compared the native/browser broad overview and outboard view, narrow
overview and cargo view. Scale, module placement, silhouettes, model colors
and the lowered outboard agree. The cradle's visible face overlap is gone.
The images use the actual renderer. Browser UI and animated sky/water timing
differ; images are not claimed pixel-identical. FPS text is incidental capture
telemetry, not a performance result.

`unchanged-parts-repeat.json` verifies eight unchanged parts across r06→r07:
identical GLB bytes at all LODs and identical decoded preview pixels. The
cradle changed there; this record is not a full r08 clean-build certificate.
Final r08 recipes and all files are bound by `summary.json`.

## Preserved corrections

- r01: overly large bevels on thin engine panels generated 44 degenerate faces.
  Bevel widths now respect the smallest component dimension.
- r02: the cradle Boolean left 21 nonmanifold edges. Outward normals and
  bounded weld/degenerate cleanup now run before export validation.
- r03→r04: review corrected socket/eye placement, supported the latch pedestal,
  separated opposite insertion regions and removed generator cage face overlap.
- r04: C++ rejected engine inertia because two symmetric entries differed by
  rounding. The recipe now computes one triangle and mirrors it exactly.
- r05: all cooks passed. Palette maps were subsequently reduced to keep the
  complete skiff within the existing owner budget; no r05 GPU refusal is claimed.
- r06: native captures exposed coplanar cradle rail/floor surfaces. r07 makes
  them meet edge-to-edge instead of covering the same face.
- r07: native/browser admission passed, but the level-water estimate exposed
  an elevated propeller. r08 extends the lower leg and moves the shaft/propeller
  .8 m lower. Final native/WASM/data and image checks were rerun.
- The first native kit test failed to compile because it added two strongly
  typed positions. The test now constructs its analytical vector explicitly.

Earlier candidate folders, source snapshots and failed logs remain available.
The cooker's `published` message means atomic creation of that local output
directory; it is not independent asset approval or a released game package.

## Reproduce

From the repository root, use a new authoring directory:

```sh
/snap/bin/blender --background --factory-startup --python-exit-code 1 \
  --python data/salvage/functional-kit/r08/recipes/author_functional_kit.py -- \
  --output-dir /tmp/new-functional-kit

# Repeat for each of the nine part names after building the two actual tools.
python3 tools/salvage_assets/cook_gameplay_asset.py \
  --sidecar /tmp/new-functional-kit/engine/source/engine.gameplay.json \
  --sources /tmp/new-functional-kit/engine/source \
  --output /tmp/new-functional-kit/engine/cooked \
  --converter build-salvage-native/bin/gltf_vmesh_tool \
  --validator build-salvage-native/bin/gameplay_sidecar_tool

nix-shell
bazel test -c opt //tests:functional_kit --test_output=all
cmake --build build-salvage-native --target functional_kit_tests voxy_native --parallel 4
build-salvage-native/bin/functional_kit_tests
build-salvage-native/bin/voxy_native --config salvage_kit_broad.cfg
```

The generator refuses existing output and records its actual Blender build and
recipe hashes. `build_kit_fixtures.py` selects exact cooked manifests and refuses
to overwrite existing registries. Move only the task-owned old registry to
evidence before deliberately selecting a new candidate.

WASM CPU reproduction:

```sh
python3 scripts/validate_asset_admission_wasm.py \
  --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node \
  --output /tmp/new-kit-wasm-check --exception-mode js --asyncify --functional-kit
```

Native capture commands and each complete view configuration are recorded in
the native reports. Browser reproduction uses the frozen package manifest,
`scripts/smoke_integrated_wasm.mjs`, `VOXY_SMOKE_GPU=gaming`, physical width/height
1920/1080, and the matching `VOXY_SMOKE_ASSET_RECIPE=.../views-<layout>.json` and
fresh `VOXY_SMOKE_ASSET_VIEWS`/`VOXY_SMOKE_REPORT` paths. Run Chrome outside Nix.

## Remaining acceptance and next work

This provides the kit and static assemblies. Complete the close socket/contact
and all-LOD moving inspection, actual kit Reset/Leave/re-entry checks and
independent technical review. In particular, inspect mating wells and the
cargo seat from below; a passing socket equation is not a rendered contact
proof. The open machinery still uses deliberately coarse collision boxes;
render-component volume is not its displacement or measured internal mass.
Refine proxies where that inspection finds misleading contacts.

ASSET-06 must establish final material standards. LOOK-01 must place these
assets in a deliberately composed, walkable cove with correct water ordering,
lighting and shadows. These dry floating inspection layouts are not that cove.
The owner's rejection of the old primitive cove remains unresolved. Physics,
propeller motion, winch cable simulation, latching, jobs and banking retain
their implementation gates. No independent review, human approval, moving
craft, lifecycle/device-loss or final performance acceptance is claimed here.
