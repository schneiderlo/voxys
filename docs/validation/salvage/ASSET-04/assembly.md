# Crossbeam assembly inspection

ASSET-04 remains open. This checkpoint implements the exact static crossbeam
and stacked-pontoon fixture specified in `design.md` section 7. It is not boat
physics, a finished asset kit, accepted cove art, or an independent review.
Use `GAME_IMPLEMENTATION_TODO.md` for the complete goal and gate policy.

## Reproducible scene

Native: launch `salvage_assembly_fixture.cfg`. Browser:
`?experience=salvage-assembly`. The earlier `salvage-asset` route and
`fixture-pontoon-v2.json` remain the individual model inspection baseline.
The new selection is `data/salvage/fixture-pontoon-assembly.json`.

| Placement ordinal (zero-based) | Exact part definition | Translation in .02 m ticks |
| --- | --- | --- |
| 0 | Pontoon, counter 3, version 2 | `{-75,0,0}` |
| 1 | Pontoon, counter 3, version 2 | `{75,0,0}` |
| 2 | Pontoon, counter 3, version 2 | `{225,0,0}` |
| 3 | Pontoon, counter 3, version 2 | `{225,48,0}` |
| 4 | Built-in beam, counter 1, version 1 | `{0,48,-50}` |
| 5 | Built-in beam, counter 1, version 1 | `{0,48,50}` |

All six use rotation 0. Part namespace is
`766f7879732d73616c766167652d7631`. The pontoon still uses the unchanged admitted
v2-r04 cooked bundle, all three original LODs and the exact original manifest
digest. Its exporter-to-canonical basis is applied once by the prefab bridge.

| Weld | Endpoint A: placement/socket | Endpoint B: placement/socket |
| --- | --- | --- |
| Forward beam left | 0 / 101 | 4 / 101 |
| Forward beam right | 1 / 101 | 4 / 107 |
| Aft beam left | 0 / 103 | 5 / 101 |
| Aft beam right | 1 / 103 | 5 / 107 |
| Stacked pontoons | 2 / 1 | 3 / 2 |

The beam uses its existing `PrototypeBoxVisual`: exactly 4 × .96 × 1 metres,
centered on its authored footprint, with its catalog's linear color, roughness
and metallic values. Its canonical basis is identity. It does not have a
modeled bottom socket well; the browser labels that limitation. The beam
checks metadata fit only. Visible peg/well nesting is checked on the separate
v2-on-v2 stack. No v1 definition is overwritten or relabeled as a cooked v2.

## Admission and rendering contract

The registry loader preserves closed schema 1 and adds closed schema 2.
Schema 2 requires `schema`, `bundles`, `placements`, `camera`, `prototypes`,
and `connections`. Limits are 64 KiB of JSON, 8 cooked bundle selections,
8 distinct prototype definitions, 32 total placements and 64 welds. Existing
depth, path, ID, exact-version and integer-coordinate checks remain active.

Each placement selects exactly one `bundle` or `prototype` index and carries
`translation_ticks` plus `rotation`. Each connection has `a` and `b`, each
containing a zero-based `placement` index and a canonical decimal string
`socket` ID. Unknown fields, ambiguous sources, self-links, missing prototypes,
out-of-range ordinals and malformed IDs reject before bundle loading.

Prototype references resolve against exact built-in starter definitions.
The loader validates a combined `PartCatalog` of the selected prototypes and
admitted cooked parts. It then creates a private CPU `BuildModel` using exactly
the registry's six placements and five welds. Weld strength is the componentwise
minimum of both endpoint limits. Invalid versions, missing sockets, one-tick
misalignment, wrong key rotations, duplicate links, blocked clearances or solid
overlap cannot publish a partially validated assembly. An earlier loaded fixture
remains usable after a replacement fails.

The private CPU proof uses the fixed `voxy-inspect-v01` namespace with separate
build, owner, part and connection IDs. It is never saved, networked, inserted
into a `GameSession`, or allocated from the player's durable ID sequence. The
inspection session still has zero builds, inventory and scenery physics bodies.

`LoadedAssetFixture` retains the validated model and connected socket IDs.
The application renders the same placements; prototype LOD entries are `null`
because prototype boxes have no authored detail levels. Fixed detail controls
continue to select only real cooked LOD IDs.

The renderer validates each prototype definition before creating a canonical
box mesh. Both beam placements share one upload. Model and helper instances
use the existing bounded `SalvageAssetFixture` owner, error scopes, frame
tickets, real GPU completion, view rebinds and device-loss cleanup. No extra
primitive submission, simulated body or unmanaged GPU owner is introduced.

The first captures exposed a real inspection failure: ordinary depth testing
hid all connected socket axes inside the opaque assembled parts. Draw counters
alone did not prove visible guides. The corrected X-ray pass explicitly uses
`MeshPathConfig.depthOverlay`, attachment comparison `Always`, depth writes
disabled, and ray-depth rejection disabled. It draws after the model pass.
Model and guide passes have separate instance/uniform buffers so a later
queued upload cannot overwrite an earlier model draw. Both `MeshPath` objects
remain in the same owner, error scopes and actual queue fences. Rebinds affect
only the model's borrowed scene textures; unlit guides use owned fallbacks.
Reset, discard, Leave, successful retirement and exceptional release cover both
paths. The browser labels this as X-ray; clean view is the default.

Socket mode in the assembly view shows only the ten endpoints participating in
the five validated welds: 150 guide boxes. A socket filter must identify a
complete, valid, non-repeating subset; it cannot silently skip unknown IDs.
The individual inspection route still shows all 24 pontoon sockets, or 360
guide boxes. This keeps the 512-draw and 256-model-node ceilings intact while
making the assembly's actual mating sockets the focus. X-ray reveals metadata
behind surfaces; it does not invent a bottom well in the prototype beam.

The assembly has three cooked LOD uploads and one 1936-byte prototype upload,
plus the already-accounted shared 1936-byte guide cube. Conservative requested
GPU reservation is 6264556 bytes. Two 512-entry instance buffers, two uniforms,
four fallback texels and the guide cube request 100544 fixed bytes, within a
128 KiB conservative fixed reservation. Clean view encodes six model draws; socket
mode includes 150 guide boxes. Requested storage excludes driver/staging
overhead and is not a process-memory or performance claim.

## Verification record

Native optimized Bazel and native CMake each pass:

- Seven registry cases, including actual bundle admission, the complete
  crossbeam/stack graph, rejection cases and 24 properly rotated whole layouts
  with negative/sector-adjacent translation.
- Three existing actual-pontoon pipeline cases, preserving all mesh vertices,
  exact metadata, all 24 rotations, rebasing and arbitrary stable LOD IDs.
- Eighteen inspection/owner cases, including six geometry cases and eleven
  actual native WebGPU cases. The new cases cover exact prototype footprint
  and material, explicit socket subsets, shared prototype upload accounting,
  duplicate-prototype rejection and invalid prototype LODs before frame tickets.

Evidence is preserved in `integration/assembly-native-attempt03.log` and
`integration/assembly-cmake-*`. Earlier compile attempts remain: explicit
socket-ID constructors and an explicit float-to-double promotion were fixed;
no warning or test was disabled to make these changes compile.

The corrected owner and the six existing normal MeshPath material/draw cases
pass optimized Bazel again (`assembly-xray-native-attempt01.log`). The CMake
owner/geometry and normal material results are recorded separately in
`assembly-xray-cmake-tests.*` and `assembly-xray-cmake-material.*`.

The current shipping browser journey passed in
`assembly-browser-journey-attempt02/summary.json`: six parts/five welds, three
cooked uploads/one prototype upload, correct per-placement LOD nulls, real
detail/guide buttons, resize, flying, Reset, guide-active GPU-drained Leave,
original-world return, re-entry and another Reset. The live session remained
empty. At drained Leave all upload counts and GPU reservations were zero.

The shared recipe is `assembly-views.json`, at 1920×1080 physical pixels and
FOV 60. Native logical windows are 1536×864 on this scaled host; browser uses
1920×1080 at DPR 1. Clean-view captures cover overview, crossbeam top/underside,
and the separate stacked join. Socket captures cover overview and underside.
Current native outputs: `assembly-native-clean-attempt02/` and
`assembly-native-sockets-attempt02/`. Matching current browser outputs:
`assembly-browser-clean-attempt02/` and `assembly-browser-sockets-attempt02/`.
All twelve captures completed at the declared physical resolution without GPU
validation errors. Root inspected the clean overviews and underside X-ray
captures: the six model placements agree and the paired connection axes and
clearances are visible through the assembly. The first socket captures are
preserved as evidence of the invisible-guide defect, not as a passing view.
Root review does not satisfy the independent reviewer requirement.

`assembly-browser-loss-attempt02/summary.json` records a fresh real device
destruction with all 150 connected guide boxes active and real queue completion
observed first. The application receives the actual destroyed-device signal,
completes exceptional shutdown and retains no live inspection session. The
original individual inspector also passes its complete controls, resize,
Reset, guide-active drained Leave and re-entry journey in
`assembly-raw-regression-attempt01/summary.json`, preserving four models,
three cooked uploads, no prototype upload and 360 all-socket guide boxes.

`integration/assembly-summary.json` binds the current source, binaries, browser
package, test XML, reports and images by SHA-256. The 34 native cases per build
system comprise 7 registry, 3 pontoon, 18 inspection/owner and 6 normal MeshPath
cases; repeated builds are not additional unique cases. This is focused
component verification, not a newly completed full repository gate suite.

The shared WASM CPU admission runner is now reusable at
`scripts/validate_asset_admission_wasm.py`. It includes the assembly registry
and expects 66 cases, with real JS exceptions, Asyncify, a fixed 64 MiB heap and
1 MiB stack. The run and all compiler/source identities are recorded in
`runtime-admission/wasm-assembly-attempt01/`. Its final manifest proves all 66
cases passed with zero skips and unchanged source inputs.

## Commands and remaining work

From the repository root in the graphics-capable development environment:

```sh
bazel test -c opt //tests:fixture_registry //tests:pontoon_pipeline //tests:salvage_asset_fixture //:voxy_native --test_output=errors
bazel run -c opt //:voxy_native -- --config salvage_assembly_fixture.cfg
python3 scripts/capture_salvage_asset_views.py --binary /absolute/path/to/voxy_native --output /new/native/output --config salvage_assembly_fixture.cfg --recipe docs/validation/salvage/ASSET-04/assembly-views.json --guides off
```

Build and package the shipping WASM application with the normal CMake workflow.
Current test package: `/tmp/voxys-asset-browser-07`. Run Chrome outside the Nix
GPU-library environment, in a fresh disposable process:

```sh
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 VOXY_SMOKE_ASSET_FIXTURE=/new/journey/output node scripts/smoke_integrated_wasm.mjs /tmp/voxys-asset-browser-07 salvage-assembly
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 VOXY_SMOKE_ASSET_VIEWS=/new/view/output VOXY_SMOKE_ASSET_RECIPE=docs/validation/salvage/ASSET-04/assembly-views.json VOXY_SMOKE_ASSET_GUIDES=2 VOXY_SMOKE_ASSET_VIEW_NAMES=overview,crossbeams-under node scripts/smoke_integrated_wasm.mjs /tmp/voxys-asset-browser-07 salvage-assembly
```

Use `VOXY_SMOKE_REPORT` and `VOXY_SMOKE_SCREENSHOT` for startup evidence paths.
Use `VOXY_SMOKE_ASSET_LOSS` in another fresh run for real device loss while all
connected guides are active. Use fresh output directories and preserve exact
package hashes. No image or FPS overlay establishes final quality acceptance.

Remaining ASSET-04 work includes the asymmetric authored hierarchy/winding
stand, actual rendered rotated assembly matrix, slow projected-size LOD
transitions, independent technical review and final candidate publication.
The subsequent [matched native/browser motion record](native-motion.md) now
captures all four pontoons traversing every LOD and returning. Independent
moving-image review remains pending. Separate evidence preserves this
checkpoint's historical source hashes.
G02 also requires the broader asset kit, material standards and accepted
LOOK-01 cove. No ASSET-04/G02 checkbox or gate commit is justified yet.
