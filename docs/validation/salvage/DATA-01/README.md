# DATA-01 validation

Status: complete, 2026-09-07. Implementer: simulation_production. Root integrated
the shared build files and reviewed the source. Independent reviewer
lego_gameplay found no actionable defects and reproduced all six Node cases.
Base revision: `7563f61fd536df7de5d209ff35d3cd2099ebcbb6`, branch
`codex/salvage-implementation`; exact reviewed inputs are recorded in
`reviewed-source-hashes.json`. No gate commit has occurred yet.

Implemented the renderer-independent construction data foundation. This report
covers focused tests, not completion of the game or its later assembly/save gates.

## Evidence

| Check | Result |
|---|---|
| Standalone GCC 15.2.0, C++20, full repository GCC warning list, `-Werror` | Passed |
| Focused native tests, with undefined-behavior and float-cast-overflow sanitizers | **13 passed**, no sanitizer diagnostics |
| Direct Node v22.23.1 conformance tests | **6 passed** |
| Rotation coverage | All 24 frozen IDs, all 576 compositions, proper determinant, inverses, socket transforms |
| Codec capacity | 65,536 records round-trip; excess count rejected |
| Cross-language format | Same independently specified 118-byte placement fixture and quaternion fixtures pass in C++ and JS |

Actual output is retained in [native-ubsan-tests.txt](native-ubsan-tests.txt) and
[node-tests.txt](node-tests.txt).

The native check compiled only `construction_types.cpp` and its test, linking
the existing GoogleTest libraries. It did not invoke Bazel, CMake, a renderer,
or the in-progress shared native build. The compiler was
`/nix/store/788mx070y81zjlg5ipcl0cra3afviw9k-gcc-wrapper-15.2.0/bin/g++`.

Reproduce with the repository GCC warnings from `settings/copts.bzl`, plus:

```sh
g++ -std=c++20 -Werror -O1 -g \
  -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all \
  -Isrc -isystem third_party/googletest/googletest/include \
  src/game/construction/construction_types.cpp \
  tests/test_construction_types.cpp \
  build-lego-native/lib/libgtest_main.a build-lego-native/lib/libgtest.a \
  -pthread -o /tmp/salvage-construction-tests-ubsan
/tmp/salvage-construction-tests-ubsan
node scripts/test_salvage_data.mjs
```

The library paths above refer to existing native GoogleTest outputs; use the
equivalent libraries from your local build. Root integration owns permanent
Bazel/CMake targets and their verification.

## Frozen contracts

- Translation uses signed 32-bit `.02 m` ticks. `INT32_MIN` is invalid; every
  remaining coordinate can be negated and rotated. Arithmetic rejects overflow.
- Rotation IDs enumerate local X over `+X,+Y,+Z,-X,-Y,-Z`, then local Y in that
  order, excluding parallel axes. Local Z is `X cross Y`. Identity is ID 0.
  Composition applies the right operand first. Tests freeze all 24 IDs.
- Raw Blender conversion is `(-x,z,y)`, rotation ID 13. Exported glTF requires
  its recorded remaining conversion. Supplying a second conversion for raw
  Blender/canonical input is an error.
- A world namespace is 16 opaque bytes, excluding all zero. Object counter zero
  is invalid. The single-owner allocator is noncopyable/nonmovable, issues the
  maximum counter once, and then rejects allocation.
- Tick and revision zero are valid initial state. Epoch and request sequence
  zero are invalid sentinels. All four are distinct types with checked advance.
- Quaternion wire order is `x,y,z,w`, IEEE-754 binary32. Normalize at construction,
  canonicalize the sign using `w,x,y,z`, and remove negative zero/subnormals.
  Decode validates rather than repairs. Squared unit-length tolerance is `2e-6`.
- JS JSON counters are canonical decimal strings. Arithmetic accepts only
  `BigInt`. Namespaces use exactly 32 lowercase hex characters.

## Placement foundation format: schema 1

This envelope demonstrates canonical keyed placement exchange. It is **not** a
complete `BuildModel`, command, inventory record, or save file.

All numeric fields are little-endian, with no struct padding:

| Offset | Field |
|---|---|
| 0 | ASCII `SVCP` (4 bytes) |
| 4 | Schema `u32` = 1 |
| 8 | Simulation tick `u64` |
| 16 | Topology revision `u64` |
| 24 | Authority epoch `u64` |
| 32 | Request sequence `u64` |
| 40 | Record count `u32` |
| 44 | First record, followed by more 37-byte records |

Each record contains namespace bytes `[0,16)`, counter `u64` at 16, translations
`i32 x/y/z` at 24/28/32, and rotation `u8` at 36. Records sort by namespace bytes
then **numeric** counter. The 65,536-record limit is a codec allocation bound,
not a gameplay performance promise. Unknown schema, invalid values, duplicate or
unsorted IDs, malformed length and trailing bytes are rejected. Validation
failure leaves C++ output unchanged; JS throws without publishing partial output.

[placement-v1.hex](placement-v1.hex) is the frozen fixture. It has tick
`9007199254740993`, revision 7, epoch 2 and sequence `18446744073709551615`.
Both records use namespace `000102030405060708090a0b0c0d0e0f`:

| Counter | Translation | Rotation |
|---|---|---|
| 256 | (-50, 16, 9) | 1 |
| 18446744073709551615 | (50, -48, 0) | 2 |

## Scope and remaining integration

Namespace generation, durable allocator checkpoint validation, complete build
schemas, content IDs, GPU handles, engine/GLM adapters, and save transactions
remain with their owning later tasks. Never restore an allocator below the
world's durable high-water mark or create concurrent allocators for one world.

The codec allocates bounded vectors; allocation failure follows standard
library behavior. Float normalization does not promise cross-platform replay
determinism or identical bytes for every approximately equivalent authored
rotation. Serialized canonical values preserve their bits across round trips.

The implementation worker did not change shared build files or global GLM
macros. Root subsequently registered lean and combined native tests in both
build systems, and the library in the shared game modules. Actual commands:

```sh
nix-shell --run 'cmake --build build-salvage-native --target construction_types_tests voxy_tests --parallel 4 && ctest --test-dir build-salvage-native -R "^construction_types\\." --output-on-failure'
nix-shell --run 'bazel test -c opt --jobs=4 //tests:config //tests:lego_surface //tests:lego_playground //tests:gltf_vmesh_tool_test //tests:construction_types'
node scripts/test_salvage_data.mjs
```

The integrated CMake build passed and all 13 dedicated CTest cases passed;
see `integrated-cmake.log`. Bazel's construction target passed, as did the
importer; unrelated baseline LEGO test compilation initially failed strict
conversion warnings, retained and fixed under BOOT-02. This does not claim
the entire multi-target command passed on that attempt. Root reproduced six
direct Node cases in `root-node-tests.txt`. No browser GPU execution is needed
to establish these renderer-independent contracts; actual WASM integration
and later engine/import bridges remain their owning tasks.
