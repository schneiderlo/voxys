# Gameplay sidecars and offline cooking

This implements versioned gameplay metadata beside a render asset. The
[probe fixture](fixtures/probe.gameplay.json) is a complete schema-1 example.
It describes a diagnostic block with simplified gameplay proxies, not approved
kit art or a simulated craft. The original Blender/GLB probe remains unchanged.

## Build and cook

Run from the repository root. Use the configured Nix compiler and existing
GoogleTest archives; this standalone build performs no GPU work:

```sh
nix-shell --run 'bash tools/salvage_assets/build_sidecar_standalone.sh'
/tmp/salvage-sidecar-build/gameplay_sidecar_tests
python3 tools/salvage_assets/test_cook_gameplay_asset.py
```

The integration tests require the actual existing
`build-salvage-native/bin/gltf_vmesh_tool`. Build that existing target through the
normal configured native build; stale binaries must be rebuilt when the selected
profile changes. `SALVAGE_CONVERTER` and
`SALVAGE_VALIDATOR` select different tested executable paths; missing tools fail
instead of skipping. `SALVAGE_CXX` and `SALVAGE_SIDECAR_BUILD_DIR` configure the
standalone build. Normal CMake/Bazel targets are listed below.

The current cooker explicitly invokes
`gltf_vmesh_tool --profile salvage-rigid-v1 input.glb output.vmesh`.
It never falls back to legacy conversion. ASSET-03 has passed its offline
acceptance checks; see `docs/validation/salvage/ASSET-03/wrapper-integration/report.md`.
Runtime rendering remains ASSET-04 work. The earlier completed ASSET-02 report
describes its retained pre-profile source snapshot.

Cook to a **new** directory whose parent already exists:

```sh
python3 tools/salvage_assets/cook_gameplay_asset.py \
  --sidecar tools/salvage_assets/fixtures/probe.gameplay.json \
  --sources data/salvage/authoring_probe \
  --output /tmp/my-salvage-probe-cooked \
  --converter build-salvage-native/bin/gltf_vmesh_tool \
  --validator /tmp/salvage-sidecar-build/gameplay_sidecar_tool
```

The result contains `gameplay.json`, `cook-manifest.json` and one
`lod-<durable-id>.vmesh` per LOD. The normalized sidecar retains every accepted
field and orders ID-bearing arrays numerically. The manifest records source
GLB digests/sizes, input and normalized sidecar digests, actual converter and
validator executable digests, the selected profile/CLI contract, cooker Python
source digest, and exact cooked VMESH digests/sizes. Repeated-cook validation
binds declared source/tool identities; changing the cooker or profile produces
a new manifest even if geometry happens to match an earlier bundle.
Input JSON formatting/order may change the input-sidecar digest while preserving
normalized metadata; it is intentionally still part of source provenance.

## Schema 1 contract

All object fields are required and closed: unknown or missing fields reject.
The C++ parser independently enforces schema, byte/depth/array bounds and uses
`PartCatalog::create` for physical/catalog invariants. The Python wrapper does
not reimplement mass, inertia, socket, strength, cost or module validation.
The shared implementation is now `src/game/assets/gameplay_sidecar.*` in
`voxy::game::assets`. The original tools header forwards that implementation for
existing offline callers. The move changes no schema, CLI or normalized bytes.

| Field | Meaning |
|---|---|
| `schema` | Integer `1`; unknown versions reject. |
| `units` | Metres, kilograms, seconds and radians. |
| `metadata_frame` | `canonical_y_up_minus_z_forward`: +X right, +Y up, −Z forward. |
| `placement_lattice_metres` | Exactly `.02`; 50 ticks/metre. |
| `part` | Exact content key/version, readable name key, rotation mask, footprint, solid/collision proxies, mass/COM/full inertia tensor, buoyancy, sockets, strength, typed module, cost/yield and material factors. Field names and all required nested records are in the fixture/parser. |
| `lods` | Stable local ID, distinct visual content key/version, source binding and minimum screen-height threshold. At least one; final threshold must be zero; thresholds are unique. |
| `tool_anchors` | Stable local ID, unique name key and canonical grid frame within the part footprint. |

Content keys contain a nonzero 16-byte namespace written as 32 lowercase hex
characters, a canonical nonzero decimal-string u64 counter and nonzero u32
version. Socket/proxy/volume/LOD/tool IDs are nonzero decimal-string u64 values,
scoped by the owning part content key and their record collection. They are
never JSON floating-point numbers or glTF node/mesh/array indices. Resource
amounts are decimal strings too, with zero allowed. Changing source-array order
must preserve these explicit authored IDs. A new exported GLB requires an
explicit source rebind, even when the gameplay metadata is unchanged.

Frames contain `translation_ticks` (three checked i32 lattice coordinates) and
`rotation` (DATA-01 proper cube rotation ID, 0–23). Bounds contain half-open
`minimum_ticks`/`maximum_ticks`. A proxy has a stable `id`, frame and positive
`half_extents_ticks`. Mass is kg, COM is metres, and the nine row-major tensor
entries are kg·m² about that COM. Socket families/roles/profiles/capacities and
strengths use the shared DATA-02 validator. Supported module tags are
`structure`, `ballast`, `flotation`, `engine`, `propeller`, `helm`, `winch`,
`tow_eye`, `cargo_cradle`, `brace` and `repair`; each has closed, typed fields.
No unsupported hinge or runtime behavior is invented.

Each LOD's `source` contains a basename `.glb` file, lowercase SHA-256, byte
length, `frame: exported_gltf` and `to_canonical_rotation`. The file must be a
self-contained GLB: buffer/image URIs may only use recognized base64 `data:`
forms for application/octet-stream, application/gltf-buffer, image/png or
image/jpeg. Unknown data-URI prefixes and malformed/empty base64 reject before
the converter can treat them as external filenames. Relative paths, absolute
paths and network URIs reject, even with a matching
GLB digest. The wrapper never downloads dependencies. GLB container version,
length, chunk order/limits and bounded JSON are checked before conversion.

## Coordinate boundary

VMESH mesh-local vertices and glTF node transforms remain in their exported
frame. The cooker **does not bake canonical coordinates into VMESH**. The
recorded proper rotation is per LOD; the probe uses rotation 12,
`diag(-1,1,-1)`. That is only the remaining transform after Blender's existing
Y-up export, not a second raw-Blender conversion.

The tested `renderPointToCanonical` helper applies DATA-01's explicit basis to
an exported-frame point. ASSET-04's CPU `rigid_prefab` module evaluates the
retained node hierarchy, then applies the recorded basis once at the prefab root:

```text
camera-relative part translation * part rotation * LOD basis * node transform
```

Canonical sockets, collision, buoyancy and anchors receive only the part
placement/rotation; never the LOD basis again. Gameplay grid coordinates convert
to metres once. MeshPath takes the resulting complete draw matrices from its
caller. Application fixture integration and actual in-engine socket/material
proof remain pending; neither a sidecar nor CPU math proves those results.

## Bounds and publication

The sidecar is at most 1 MiB, JSON depth at most 32, and strings/individual
records are bounded. C++ independently caps 32 solids, 32 collision proxies,
32 buoyancy regions, 128 sockets, 8 LODs and 128 tool anchors. Each source GLB is
at most 64 MiB and each cooked VMESH at most 256 MiB; GLB JSON is at most 1 MiB.
Python separately caps parsed JSON values at 100,000 and dependency arrays at
4,096 records. These are offline authoring limits, not runtime budgets.

The wrapper snapshots the sidecar and each digest-checked source into a private
sibling staging directory, then invokes the real converter and C++ validator.
Input opens are nonblocking on Linux, so named pipes are rejected before waiting
for a writer. The output resolver also rejects empty meshes, nonfinite vertex
data, invalid indices and unusable triangle ranges after VMESH parsing; a
format-valid empty asset is not a drawable visual. Linux child limits bound CPU time to 60 seconds, address space to 2 GiB and
individual file writes to 256 MiB; wall timeout is 90 seconds per tool. Trusted
local converter/validator binaries are required. This is not a general sandbox
for malicious executable files or a claim that arbitrary glTF is safe.

Only a fully successful stage is renamed to the final name. Linux uses
`renameat2(RENAME_NOREPLACE)` so even an empty destination created by another
publisher is never overwritten. Unsupported filesystems/hosts fail safely.
[Linux rename specification](https://man7.org/linux/man-pages/man2/rename.2.html).
Windows has a standard no-overwrite rename path but has not been tested here.
Exception paths remove the private stage; process/host crashes can leave a
hidden staging directory, and power-loss durability/fsync is not claimed.
Existing published output remains untouched. Content version directories are
append-only through this tool; there is no automatic overwrite/update command.

## Normal build targets

From the repository root in the Nix development environment:

```sh
bazel build -c opt //tools:gltf_vmesh_tool //tools:gameplay_sidecar_tool
bazel test -c opt //tools:gameplay_sidecar_test //tools:cook_gameplay_asset_test
python3 tools/salvage_assets/cook_gameplay_asset.py \
  --sidecar tools/salvage_assets/fixtures/probe.gameplay.json \
  --sources data/salvage/authoring_probe \
  --converter bazel-bin/tools/gltf_vmesh_tool \
  --validator bazel-bin/tools/gameplay_sidecar_tool \
  --output /tmp/salvage-probe-bundle
```

Choose a new output directory if that name already exists; its parent must exist.
The Bazel Python test declares the actual tool binaries, GLB and sidecar as
runfiles. It never substitutes a mock converter for its successful cook checks.
The nine parser cases also belong to the mandatory `//tests:voxy_tests` suite;
the complete Python cooker integration is a separate required asset-task check.

For an already configured native CMake build with tools and tests enabled:

```sh
cmake --build build-salvage-native \
  --target gameplay_sidecar_tool gameplay_sidecar_tests gltf_vmesh_tool --parallel 4
ctest --test-dir build-salvage-native \
  --tests-regex '^(gameplay_sidecar[.]|salvage_asset_cook$)' --output-on-failure
```

When `VOXY_BUILD_TOOLS=OFF`, parser tests remain available. The test that invokes
both offline tools is registered only on Linux with those tools enabled. Its
FIFO and shell failure fixtures, and atomic publication, are verified on Linux;
Windows authoring execution remains unverified. These are native authoring
tools. No browser-side asset cooking is implied.

## Limits of this milestone

Full supported-glTF enforcement and the strict-profile regression suite belong
to ASSET-03. Consult that task's current acceptance evidence before treating a
profile name as a compatibility guarantee; ASSET-02 alone does not prove it.
The digest does not imply visual quality. Runtime
bundle verification must check normalized metadata and exact cooked VMESH
identity, not merely the source digest; the loader and prefab adapter are later
ASSET-04 integration. Asset registry/version immutability across separate bundles,
Windows/WASM tool execution, rendered materials, mesh-based collision validation,
shipping parts and gameplay are not implemented here.
