# Cooked part runtime admission — CPU component evidence

Root, 2026-09-08. **Component checks pass. ASSET-04 and LOOK-01 remain open.**
This report does not approve the pontoon's in-engine appearance or complete the
native/browser fixture, socket-fit, LOD or GPU-lifetime requirements.

## Implemented contract

The sidecar parser now lives in `src/game/assets/gameplay_sidecar.*`; the old
tool header forwards to the shared namespace. The CLI, normalized bytes and
physical schema remain unchanged. Shared `src/core/sha256.hpp` replaces the
private WRECKWATER implementation without changing its wire digest or domains.

`cooked_part_bundle.*` admits immutable CPU snapshots. The installed registry
supplies the expected manifest digest independently of the package. Every read
has a preallocation cap, and every returned snapshot is checked for length and
digest before decoding. Closed schemas, canonical IDs, exact normalized metadata,
source/LOD bindings and complete rigid-node geometry must all pass. The loader
does not reopen files after hashing. A failure cannot replace an active bundle.

`cooked_part_directory.*` implements the provider for Linux/POSIX and Emscripten
MEMFS. It rejects parent traversal, links at every component, unsafe leaves,
nonregular files, empty/oversized files, and changing file lengths. Native keeps
an admitted directory descriptor across renames. MEMFS path-emulates `openat`, so
its adapter additionally checks root device/inode and fails closed after root
replacement. MEMFS does not implement FIFO creation; the actual native FIFO test
proves a reader does not block awaiting a writer. No Windows provider is claimed.

Stable LOD IDs are not array indices. Both `bundle.lods()` and `sidecar.lods`
sort by ID. Runtime users must compare each record's screen threshold explicitly
and use its stable ID; neither array ordinal defines quality order. Decoded payload and
requested GPU counts are separate from metadata, allocator and driver overhead.
Source GLB and tool hashes bind provenance; runtime does not import those sources.

The accepted ASSET-03 diagnostic bundle is copied byte-for-byte to the test-only
`data/salvage/runtime_probe_v1/`. Its manifest is
`f595f6b490fbbcc6f947eb1ac31f7328cc4dedf9d84896268a8aed45dea80898`.
Its real four-mesh, 752-triangle content passes both providers. This does not
replace the original probe or publish the pontoon.

## Actual results

| Check | Evidence | Result |
|---|---|---|
| Optimized Bazel SHA, WRECKWATER goldens, prefab, bundle, sidecar, actual offline cooker | `bazel-reviewed.log` | 5 + 5 + 13 + 16 + 9 + 23 cases pass |
| Native directory provider, including FIFO and no-follow | `directory-bazel-reviewed.log` | 8 pass |
| CMake SHA, prefab, initial bundle, sidecar | `cmake-reviewed.log` | 42 pass; zero compiler warnings |
| CMake bundle expanded with actual cooked probe | `bundle-cmake-actual-probe.log` | 16 pass |
| CMake directory plus saved DATA-05 session/journal extraction | `cmake-directory-data05.log` | 8 directory + 28 session + 12 journal pass; not DATA-05 acceptance |
| Real WebAssembly: SHA, WRECKWATER, prefab, bundle, directory, sidecar | `wasm-directory-attempt01/manifest.json`, `tests.log` | 56 pass; zero skipped; source freeze verified |
| Installed registry, native Bazel/CMake | `../prefab/fixture-registry-tests-first.log`, `../prefab/fixture-registry-cmake-tests.log` | 4 pass in each system; actual r04 package and projected LOD selection |
| Expanded real WebAssembly including installed registry and r04 package | `wasm-registry-attempt02/manifest.json`, `tests.log` | 60 pass; zero skipped; source freeze verified; Node 22.23.1 |

The WASM test uses Emscripten 6.0.1-git and Node 22.23.2, real GoogleTest, JS
exceptions plus Asyncify, fixed 64 MiB heap, the application's existing 1 MiB
stack, stack guards and non-aborting allocation. It executes native C++ compiled
to WASM; it is not a JavaScript reimplementation or a browser GPU test.
`run-wasm.py` and the copied `executed-runner.py` record exact commands, tool
identities, all dependency hashes and emitted JS/WASM artifacts. `summary.json`
links final counts and component hashes; its selected 28 sources were rehashed
against the completed WASM run with no changes.

The expanded registry run retains the same heap, stack, exception and Asyncify
settings, and additionally embeds the three r04 meshes, sidecar, manifest and
installed fixture registry. Attempt 01 supplied a nonexistent `/usr/bin/node`
and never reached compilation; attempt 02 uses the actual installed executable.
The first CMake registry invocation built successfully but used the wrong test
output path. `fixture-registry-cmake-tests.log` records the corrected execution
from `build-salvage-native/bin/fixture_registry_tests`.

## Failures retained and resolved

- Initial bundle builds caught a lambda terminator and a copied JSON range
  variable under strict warnings. Those attempts remain in the directory.
- GCC's optimized inlining exposed an inherited offset-based SHA loop warning.
  The implementation now consumes bounded spans; standard vectors, every split
  of a 129-byte message and boundary-size vectors pass without suppression.
- Strict WASM compilation caught implicit float-to-double quaternion promotion;
  explicit conversions now match the double-precision prefab contract.
- The initial test runner used Emscripten's default 64 KiB stack. A separate
  guarded diagnostic in `wasm-attempt03/` confirmed stack overflow in an existing
  WRECKWATER golden fixture. The same objects pass with the application's existing
  1 MiB stack; no application stack increase was introduced by this test fix.
- Bazel exposes runfiles as symlinks. The first directory test correctly rejected
  them. The corrected test materializes exactly the three declared files into a
  regular temporary package; production no-follow behavior was preserved.

Independent renderer-agent source review of the bundle/SHA boundary is retained
at `../prefab/bundle-review.md`. It predates the directory adapter and sixteenth
bundle case; do not treat it as independent review of later additions.

## Reproduce focused checks

```bash
nix-shell --run 'bazel test -c opt --jobs=4 //tests:cooked_part_bundle //tests:cooked_part_directory //tests:rigid_prefab //tests:sha256 //tests:wreckwater_content_manifest //tools:gameplay_sidecar_test //tools:cook_gameplay_asset_test --test_output=errors'
python3 docs/validation/salvage/ASSET-04/runtime-admission/run-wasm.py --help
```

Use a new evidence output directory for each run. The runner's manifest contains
the exact SDK/Node arguments used here. These focused checks do not replace a
gate's shared native/WASM build, actual game capture or required commit hook.
