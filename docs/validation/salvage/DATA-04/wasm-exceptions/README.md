# WebAssembly exceptions and real allocation-failure proof

**Final result: all 21 GameSession tests pass as WebAssembly under Node.**
JavaScript-based exception handling, Asyncify and a fixed 64 MiB WASM heap are
enabled. Final compile/link logs contain no warnings. This validates source-level
WASM behavior, not the graphical browser journey.

Executor: `simulation_production`, 2026-09-07. Compiler: Emscripten **6.0.1-git**,
commit `25e4e8d6550d392ba9e0c2936bce7cf41ee47cc0`. Runtime: **Node v22.23.2**.
Product source was unchanged throughout each run and remained frozen afterward.

## Final evidence

[attempt-03-js-fixed/manifest.json](attempt-03-js-fixed/manifest.json) records
every exact command, expected/actual exit status, runtime/source hashes, artifact
sizes/hashes, temporary SDK-cache location and the successful source-freeze check.
[tests.log](attempt-03-js-fixed/tests.log) records **21/21 passing cases**, including
the actual C++ `bad_alloc` injection and unexpected adapter exception cleanup
cases. No GTest filter, test suppression or fake exception implementation was used.

The runner compiles the repository's actual `gtest-all.cc` and `gtest_main.cc`
sources into WASM. A separate compile probe asserts both
`GTEST_HAS_EXCEPTIONS == 1` and `__has_feature(cxx_exceptions)`. Project source
compiles with strict Clang warnings and `-Werror`; vendored GTest uses its normal
flags. Both compilation and linking use **`-fexceptions`**. Linking additionally
uses **`-sASYNCIFY=1 -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=67108864
-sABORTING_MALLOC=0`**. No native-WASM-EH/Asyncify warning is accepted or suppressed.

Final test module SHA-256:
`ab6e354f631be1aa8f93eae1e5cb8dbba8e226ae2a2d04268bc49c0af171d2ac`.
Its [JavaScript loader](attempt-03-js-fixed/game_session_tests.js) and
[WASM module](attempt-03-js-fixed/game_session_tests.wasm) are retained together.

## Actual allocator behavior

[allocation_probe.cpp](allocation_probe.cpp) calls the real `::operator new`
with a volatile 128 MiB request against fixed 64 MiB WASM memory. It separately
calls the real `std::nothrow` overload. This exceeds the module's allowed heap;
it does not exhaust the Node process or host physical memory.

The same probe object is linked twice; only `ABORTING_MALLOC` changes:

| Link policy | Throwing allocation | Nothrow allocation |
|---|---|---|
| `ABORTING_MALLOC=1` | Node exits 1, `RuntimeError: Aborted(OOM)` | Node exits 1, same abort |
| `ABORTING_MALLOC=0` | Node exits 0, actual `std::bad_alloc` caught | Node exits 0, null returned |

Preserved baseline failures:
[throwing abort](attempt-03-js-fixed/allocation-aborting-1-throwing.log),
[nothrow abort](attempt-03-js-fixed/allocation-aborting-1-nothrow.log).
Successful policy:
[caught allocation failure](attempt-03-js-fixed/allocation-aborting-0-throwing.log),
[nothrow null](attempt-03-js-fixed/allocation-aborting-0-nothrow.log).
The manifest labels those two baseline nonzero exits as expected failures;
they are not counted as passing GameSession cases.

This proves failure delivery for an impossible fixed-heap allocation. It does
not certify every application subsystem under sustained memory exhaustion, nor
does it guarantee that error-reporting code elsewhere avoids new allocations.

## Preserved earlier runs

- [attempt-01](attempt-01/manifest.json): pre-participant-fix **20/20** tests passed
  with **native WASM EH**, `-fwasm-exceptions` at compile/link, and no Asyncify.
  This remains valid isolated evidence for that exact configuration/source.
  It does not justify combining native EH with the application's Asyncify.
- [attempt-02-js-fixed](attempt-02-js-fixed/manifest.json): corrected source,
  **21/21**, JS EH + Asyncify, and both actual allocator-policy outcomes passed.
  Its link logs contain a harmless but unnecessary `MAXIMUM_MEMORY` flag warning
  because memory growth was disabled. The final run removes only that redundant
  test flag and repeats the entire build/run into a new directory.
- [attempt-03-js-fixed](attempt-03-js-fixed/manifest.json): final clean result
  described above. The [executed runner](attempt-03-js-fixed/executed-runner.py)
  is retained with its hash.

The SDK's native-EH feature adds `-fwasm-exceptions` at compile/link, while its
`exceptions` feature adds `-fexceptions` only to C++ compilation. The complete
application therefore also needs the explicit link flag. Root owns that shared
Bazel/CMake correction and application-level acceptance.

## Reproduce

Run from the repository root, choosing a new output directory (the runner
refuses to overwrite evidence):

```sh
python3 docs/validation/salvage/DATA-04/wasm-exceptions/run.py \
  --sdk /home/modkin/.cache/bazel/_bazel_modkin/cbcbf4be6285ea63e5938aa06a96dc1e/external/emsdk++emscripten_deps+emscripten_bin_linux \
  --node /nix/store/3vm4j8im8bzdal3dsgmhv9gb5kvanbp1-nodejs-22.23.2/bin/node \
  --exception-mode js --asyncify --allocation-probes \
  --output /tmp/salvage-wasm-exception-repeat
```

The runner copies the SDK's prebuilt cache to a new temporary directory and
uses its own `EM_CONFIG`/`EM_CACHE`. SDK Python bytecode writes are disabled.
It does not invoke Bazel, alter a shared cache, modify production/shared build
files, or use a GPU. Runtime and SDK paths can be supplied explicitly on another
host; inspect their recorded versions/hashes before comparing results.
