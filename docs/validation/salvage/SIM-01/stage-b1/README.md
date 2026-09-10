# SIM-01 stage-b1 — bounded exterior collision geometry

Implemented 2026-09-08 after G00 commit `7f28fab`, on
`codex/salvage-implementation`. `summary.json` records exact uncommitted input
and artifact hashes. **SIM-01 remains open; no full gate or commit is claimed.**
The independent final compiler review remains outstanding.

`AssemblyCollisionPlan` owns the mass plan, canonical part/proxy/root source
mapping, and one exact `BoxUnion` per welded root. It reads only catalog collision
proxies; decorative render studs are excluded. A root's union consists of disjoint
box cells, exterior face patches and a balanced flat child BVH. These are CPU
preparation records. A backend must honor the exterior patches; activating the
raw cells as ordinary unmasked compound boxes would reintroduce internal contact
faces. That backend support and contact generation belong to SIM-02.

## Geometry and limits

Boxes remain exact integer AABBs under proper cube rotations. Source boxes sort
by stable local label backed by durable part and definition-scoped proxy IDs.
Repeated subtraction assigns overlapping volume to the least label without
double-counting. Face clipping removes full or partial internal mating surfaces
while retaining exposed rectangles and enclosed cavity boundaries. Dry mass is
still independently computed from authored mass properties, never union volume.

Each exterior patch carries its source, cell, axis, outward sign and rectangle
coordinates. Source lookup resolves the originating part/proxy/root; labels and
cell indices are local to the immutable build/revision plan. They are not
persistent backend contacts. Coplanar patches can meet along edges; later solver
contact reduction must resolve those shared edge candidates consistently.

The flat preorder BVH splits the longest axis at a median, with canonical cell
indices breaking centroid ties. Escape indices support stackless traversal.
Queries perform closed AABB overlap, including points/touching boundaries, and
return candidate cell indices. They are broad-phase queries, not certified
contacts. The first traversal counts; only a sufficient output buffer receives
the second traversal's copies. Errors preserve the caller's buffer.

Whole-build generated caps: 2,048 input boxes, 4,096 disjoint cells, 24,576 face
patches and 8,388,608 clipping tests. Scratch vectors each cap at 4,096 pieces.
Root coordinates stay within ±256 m per axis. Remaining cell/face/work budgets
are passed across roots, preventing one allowed per-root budget from multiplying
silently by the root count. Excessive work, fragmentation or capacity rejects
the candidate; no incomplete geometry is returned.

Vector growth is explicitly capped per vector. Across roots, unused growth
capacity can be less than twice the live cell/face elements. BVH node storage
requests exactly 2*N−1 elements per root. These are count/request bounds, not a
measured whole-process or allocator-overhead budget. The later scene transaction
must reserve global live, candidate, upload and retiring resources separately.

See [collision design](../collision-design.md) for the full contract. Collision's
least-source rule must not erase overlapping buoyancy provenance; the next
compiler stage must retain solid/sealed contributor identities for later flooding.

## Executed evidence

| Check | Actual result |
|---|---|
| Optimized Bazel | 12 collision/union cases pass; existing 11 mass + 23 build cases cached/pass |
| Native CMake | All 46 shared cases pass; native core library builds with both new sources |
| Strict GCC, warnings as errors, undefined-behavior and float-cast-overflow checks | All 46 cases pass |
| Actual WASM with JS exceptions and Asyncify | All 46 cases pass; no skipped/disabled tests; fixed 64 MiB heap and 1 MiB stack |
| Shipping WASM core library | Both collision sources compile in the current application configuration |
| Actual allocation failures in each runtime | 62 failure cuts preserve canonical input and an existing plan; eventual success |
| Actual read allocation guard in each runtime | 2,000 BVH query calls, including insufficient capacity, make zero allocation attempts |

Seven union tests compare against a separately implemented integer-cell oracle:
overlap/containment/duplicate bounds with lowest-source ownership, partial face
coverage, enclosed cavities, every proper rotation, and 32 deterministic mixed
six-box fixtures with reversed insertion order. The oracle independently expands
unit-cell occupancy and all six neighbor faces; it checks exact union volume,
surface area, absence of duplicate/hidden faces and face provenance. It does not
call the production clipping/extraction/BVH helpers.

The cavity fixture has 98 ticks³ of material and 204 ticks² of boundary, including
its inner walls. The partial-join fixture has 72 ticks³ / 112 ticks² and preserves
four exposed rectangles around the smaller joined box. These deliberately small
integer fixtures are synthetic numerical references, not game-scale buoyancy.

BVH tests compare 1,200 box queries against a linear cell oracle, verify touching
point queries, exact node/escape bounds, insufficient-capacity refusal, invalid
bounds and untouched output tails. Input, cell, face, scratch and clipping-work
budgets have separate rejection cases.

Five integrated cases use actual catalog/build validation: an engaged two-beam
stack removes the internal mating plane; a 256-beam welded stack retains exactly
256 cells, 1,026 exterior patches and 511 BVH nodes; disabled welds remain separate
roots and share global budgets; overlapping authored proxies do not duplicate
volume/mass; and authored proxy/part rotations compose once. Reordered parts,
connections and source boxes produce equivalent geometry/mappings.

The allocation fixture has three actual build parts across two roots with
overlapping authored proxy boxes. Its first root has six cells, 38 exterior
patches and 11 BVH nodes; full compilation accounts for 238 clipping tests.
Replacement global new/new[]/aligned-new fails each allocation in turn, including
validation, scratch/output vectors and BVH preparation. An existing plan and
canonical serialized input remain intact after every attempt.

All test/oracle/probe code was authored by the implementer. Independent arithmetic
and method coverage do not constitute a separate reviewer or live GPU proof.

## Preserved failures

1. `bazel-attempt01.log`: strict optimization detected a nullable source lookup
   dereferenced without an assertion in a test. Added an explicit non-null
   assertion before checking the proxy; attempt02 passes. No warning disabled.
2. `wasm-attempt01`: all 46 tests executed and passed, but the new runner expected
   47 due to a manual count error. Source contains 12 collision, 11 mass and
   23 build tests. Corrected only the expected total; `wasm-attempt02` passes.
   No test was removed, filtered, disabled or skipped. Both manifests preserve
   identical test/core source hashes and their separately hashed runners.

CMake and strict native first attempts passed. Final source/hash checks are in
`summary.json`. The complete GPU/CPU hook suite has not been claimed or run as a
gate acceptance for this intermediate component.

## Reproduce

From the repository root inside `nix-shell`:

```bash
bazel test -c opt //tests:assembly_collision //tests:assembly_compiler //tests:build_model --test_output=errors
cmake --build build-salvage-native --target assembly_collision_tests assembly_compiler_tests build_model_tests voxy_core -j 8
build-salvage-native/bin/assembly_collision_tests
build-salvage-native/bin/assembly_compiler_tests
build-salvage-native/bin/build_model_tests
bash docs/validation/salvage/SIM-01/stage-b1/build-standalone.sh
/tmp/salvage-collision-tests
bash docs/validation/salvage/SIM-01/stage-b1/build-allocation-faults.sh
/tmp/salvage-collision-faults
```

Use SDK/Node paths from `wasm-attempt02/manifest.json` and fresh directories:

```bash
python3 scripts/validate_assembly_compiler_wasm.py --sdk <recorded-sdk> --node <recorded-node> --output <fresh-shared-directory> --exception-mode js --asyncify
python3 docs/validation/salvage/SIM-01/stage-b1/run-wasm-faults.py --shared-validation <fresh-shared-directory> --output <fresh-fault-directory>
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_core -j 8
```

Manifests record executed commands, exact tool/source hashes and retained
binaries. Never overwrite historical attempts or cite an earlier runner as
current proof after editing it.

Next: overlap-aware buoyancy regions, module and joint frames, complete immutable
assembly output/cache identity, then full compiler validation and review. The
cove still uses placeholder scenery; no new playable physics or visual milestone
is claimed here.
