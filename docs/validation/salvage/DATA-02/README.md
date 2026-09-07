# DATA-02 — immutable starter part catalog

**Current source:** all 16 focused cases pass with the full repository GCC
warnings, `-Werror`, undefined-behavior sanitizer and float-cast-overflow
sanitizer, including four new Winch socket assertions. Independent review found
no defect in the explicit checked-pointer correction.
[Current follow-up review and exact command](winch-review.md),
[current compile output](winch-review-expanded-compile.txt),
[current test output](winch-review-tests.txt).

The optimized Bazel and CMake passes below preceded this later Winch lookup
correction and refer to the prior source hashes retained in
[the previous report](README-before-winch-pointer-fix.md). They are historical
integration evidence; root's G00 checks cover the refreshed source.
[Bazel build/test output](bazel-opt-tests.txt),
[Bazel case output](bazel-opt-test-cases.txt),
[sanitizer output](native-ubsan-tests.txt).

This is validated game data. It does not claim live boat simulation, rendered
part previews, final assets, balanced handling, or completion of later gates.

## Tested source and command

Base revision: `7563f61fd536df7de5d209ff35d3cd2099ebcbb6`. These files were
uncommitted additions during the focused run; shared integration belongs to root.

| File | SHA-256 |
|---|---|
| `src/game/construction/part_catalog.hpp` | `8c107f72da01b065f811db557b38de9a3a014e5717b3348951cfe21a07bc8c13` |
| `src/game/construction/part_catalog.cpp` | `24869c7c346d49421b2fe20123181e4e5738d6e6ddc366339292d3f12044bcf3` |
| `tests/test_part_catalog.cpp` | `b0cd3d3dd28f579d9e47c85705244ecb704d56cde205b5547b438c7d3f64b5bb` |

Host: Linux x86_64. Compiler: GCC 15.2.0 at
`/nix/store/788mx070y81zjlg5ipcl0cra3afviw9k-gcc-wrapper-15.2.0/bin/g++`.

Original standalone compilation, with that compiler substituted for `g++`
(the current follow-up's output path is recorded in `winch-review.md`):

```sh
g++ -std=c++20 -Wall -Wextra -Wshadow -Wnon-virtual-dtor \
  -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wpedantic \
  -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion \
  -Wformat=2 -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches \
  -Wlogical-op -Wuseless-cast -Werror -O1 -g \
  -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all \
  -Isrc -isystem third_party/googletest/googletest/include \
  src/game/construction/construction_types.cpp \
  src/game/construction/part_catalog.cpp tests/test_part_catalog.cpp \
  build-lego-native/lib/libgtest_main.a build-lego-native/lib/libgtest.a \
  -pthread -o /tmp/salvage-part-catalog-tests-ubsan
/tmp/salvage-part-catalog-tests-ubsan
```

The GoogleTest paths refer to existing native outputs. Root owns shared build
registrations. This worker ran the focused optimized target after the integration
fix, using normal host access for the project Nix daemon/build cache:

```sh
nix-shell --run 'bazel test -c opt --jobs=4 //tests:part_catalog'
```

The preceding source also passed the integrated CMake target and all 16 focused
CTest cases: [build output](cmake-build-tests.txt),
[test output](cmake-test-cases.txt). Commands:

```sh
nix-shell --run 'cmake --build build-salvage-native --target part_catalog_tests -j4'
nix-shell --run 'ctest --test-dir build-salvage-native -R "^PartCatalog\\." --output-on-failure'
```

No browser, WASM, GPU, or Windows catalog validation was performed by this worker.
Compilation was paused during root's visible GPU baseline capture.

## Optimized-build integration correction

The initial optimized run failed before executing tests: GCC 15.2 with
`-Werror=null-dereference` diagnosed the inlined `std::vector` copy-assignment
inside `PartCatalog::create`. That diagnostic is preserved in
[bazel-opt-failure.txt](bazel-opt-failure.txt). The catalog now reserves storage
and copy-constructs each validated definition with `emplace_back`. It still owns
a deep copy, sorts only its private local result, and publishes only after
construction completes. Validation and allocation-failure publication semantics
are unchanged; no warning was suppressed.

After that correction, optimized compilation exposed a second diagnostic in the
test fixture's `istreambuf_iterator` read. The diagnostic is retained in
[bazel-opt-fixture-failure.txt](bazel-opt-fixture-failure.txt). The fixture now
checks that the file opened and reads only the expected length plus one byte,
rejecting truncation, same-length altered content and trailing bytes. The test
includes explicit same-length and trailing-byte cases.

The first retry inside the workspace sandbox could not access the Nix daemon
socket; [nix-sandbox-attempt.txt](nix-sandbox-attempt.txt) records that environment
failure. The approved normal-host retry compiled and passed the focused Bazel
target. That standalone sanitizer run used the pre-Winch-correction source hashes
preserved in `README-before-winch-pointer-fix.md`.
[Earlier sanitizer output](native-ubsan-before-integration-fix.txt) is retained
separately and is not substituted for the refreshed run.

## API and identity contract

`makeStarterCatalogDraft()` returns editable authoring data.
`PartCatalog::create(draft, issue, resolver, policy)` validates and copies it into
owned storage. Callers can access only const definitions and exact-key lookup.
Changing or destroying the draft cannot alter a published catalog.

`ContentKey` is a DATA-01 `DurableId` plus nonzero definition/asset version.
Lookup distinguishes an unknown ID from a missing version. Multiple explicit
versions of an ID are supported; duplicate `(ID, version)` pairs are rejected.
Definitions and local keyed collections are sorted when published.

Starter IDs use the fixed 16-byte namespace `voxys-salvage-v1`, counters 1–12,
and definition version 1. The namespace remains stable when definitions acquire
new versions. Do not reinterpret existing IDs or silently change authoritative
metadata under the same version; later content/save tasks supply migrations.

Socket IDs are stable within a definition. A runtime socket endpoint names the
part instance and socket ID. Proxy IDs are scoped by definition **and** collection:
collision, solid occupancy, or buoyancy. None of these identities are array
indices or GPU handles.

Errors include the input definition index, static property name and offending
element ID where applicable. Validation failure publishes no catalog. This API
accepts C++ drafts; an eventual file parser must bound input before allocating
those drafts. Vector allocation and injected-resolver exceptions follow normal
C++ behavior.

## Initial twelve definitions

Dimensions are metres. Prices and yields are integer general-material units;
the engine also costs one special-machinery unit. Other special costs and all
special yields are zero.

| ID | Definition | Box dimensions X/Y/Z | Dry mass kg | Cost / paid salvage yield | Function |
|---|---|---|---:|---|---|
| 1 | Beam | 4 / .96 / 1 | 90 | 12 / 8 | Structure and multiple mount stations |
| 2 | Plate | 4 / .32 / 2 | 80 | 10 / 7 | Deck and multiple mount stations |
| 3 | Pontoon | 4 / .96 / 1 | 120 | 24 / 16 | 3.84 m³ sealed displacement; drag coefficients |
| 4 | Engine | 1 / .96 / 1 | 160 | 30 + 1 machinery / 18 | Shaft socket; 18 kW, 500 Nm limits |
| 5 | Propeller | 1 / .96 / .64 | 35 | 16 / 10 | Shaft socket; 2,500 N at its local force frame |
| 6 | Helm | 1 / .96 / 1 | 30 | 10 / 6 | Operator frame and .6 rad steering limit |
| 7 | Winch | 1 / 1.92 / 1 | 140 | 30 / 20 | Line socket; .5–40 m cable, 2 m/s, 12 kN |
| 8 | Tow eye | .6 / .32 / .6 | 8 | 4 / 2 | Compatible cable endpoint |
| 9 | Cargo cradle | 2 / .32 / 2 | 90 | 14 / 9 | Capture socket and motion/alignment limits |
| 10 | Brace | 2 / .32 / 1 | 45 | 8 / 5 | Structural load-transfer parameter |
| 11 | Ballast | 1 / .96 / 1 | 600 | 20 / 14 | Dense mass with COM .24 m below origin |
| 12 | Repair module | 1 / .96 / 1 | 40 | 18 / 12 | 3 m reach, .1 health/s, 12 material/full health |

These are explicit prototype authoring values requiring later gameplay tuning.
Cutting and crane variants are intentionally absent until their mechanics arrive.
No runtime force, control, repair, reward or structural solver is implemented here.

## Physical and connector rules

- Footprints and proxies use DATA-01 `.02 m` ticks and exact proper rotations.
  Solid occupancy is a bounded list of half-open analytical boxes; touching
  faces are allowed. There is no dense tiny-cell grid.
- Collision profile 1 contains boxes, excluding decorative stud contacts.
  Buoyancy uses separate authored solid or sealed boxes. Overlapping buoyancy
  volumes within one part are rejected; touching compartments are allowed.
  Cross-part overlap is checked by later build/compiler work.
- Non-pontoon parts have small explicit solid displacement cores. Their coarse
  collision boxes are not all treated as sealed flotation. Engine and ballast
  inertia use smaller dense mass distributions about their offset COMs.
- Inertia is a full symmetric 3×3 tensor about dry COM. Validation checks finite
  values, positive definiteness, near-singularity, physical principal-moment
  triangle inequalities, and per-axis mass/COM/footprint variance bounds.
  These are necessary physical checks, not a proof of a unique authored density
  distribution inside arbitrary collision geometry.
- Socket-local +Y is the outward normal; +X is the key direction. Engaged
  socket origins coincide, outward normals oppose, and key directions align.
  The relative mating rotation is 180° about local X (`CubeRotation{2}`), not
  identical full frames. Full beam stacking advances .96 m;
  plate stacking advances .32 m. The .18 m insertion region does not add a gap.
- `matchSockets` checks family, profile, complementary roles and connection kind.
  Structural and shaft interfaces weld; winch/eye interfaces use rope; cradle
  interfaces latch. It does not authorize an edit or check world alignment,
  available slots, motion, inventory or damage. Those belong to DATA-03 onward.
- Cradle capture defaults preserve the plan's .10 m / 5° / .5 m/s / 30°/s limits.
  Cargo supplies the complementary latch plug in its later definition.
- Catalogue yield is the upper bound for paid parts. GameSession must apply
  starter-loan provenance and return zero for loan material; this module cannot
  enforce transaction or rescue ownership.

Authoring bounds are distinct from gameplay capacity: 1,024 definitions,
32 boxes per collection, 128 sockets per part, 8 cooked LODs, 100 m maximum
extent per axis, and dry mass .001–1,000,000 kg. Maximum inertia component is
1e12 kg·m², with a minimum maximum-component magnitude of 1e-9 kg·m²;
near-singularity thresholds are applied after scaling. These limits do not
certify solver stability or shipping workload performance.

## Honest visual references

Every starter has exactly one `PrototypeBoxVisual`, explicit footprint bounds
and inline linear color/roughness/metallic values. A small built-in registry
accepts only its fixed asset ID/version and valid box parameters. It creates no
files and does not claim a renderer binding. No motorcycle or other unrelated
legacy mesh is substituted.

`CookedMeshVisual` requires a canonical relative `.vmesh` path and an injected
resolver that checks the exact asset ID, version and path against a known
inventory. An absent resolver rejects cooked references. Traversal, absolute
paths and malformed paths fail before lookup. The built-in prototype ID cannot
be relabeled as cooked geometry to bypass the prototype policy.

`CatalogPolicy{false}` rejects prototype visuals. A cooked reference alone does
not imply final art approval: cooker digest/format validation, asset provenance,
render previews and independent visual review remain ASSET/VIS responsibilities.

The cooked-asset test creates a unique temporary inventory fixture and verifies
that changed content, missing backing files, incorrect paths and incorrect
versions fail. Its payload is a catalog-resolver fixture, **not a generated or
validated VMESH**. Separate LOD tests use an exact in-memory asset inventory.

## Acceptance coverage

The 16 tests cover immutable ownership; stable/exact version lookup; all twelve
starter records; mass/COM/full-inertia failures; every proper box orientation;
sideways/upside-down engaged sockets; missing/duplicate/invalid proxies and
sockets; non-overlapping buoyancy; typed connector compatibility; module limits
and required socket roles; costs/yields/materials/capacities; the exact prototype
registry; actual backing-entry failure; and malformed cooked paths/LOD ordering.

Pending integration: DATA-03 canonical builds and occupancy, ASSET-02 sidecars,
SIM-01 assembly compilation, complete content identity/cooking, real render
bindings, and physics/economy execution. No task checkbox or commit was changed
by this worker.
