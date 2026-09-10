# Independent review of the DATA-04 Application bridge

Reviewer: `simulation_production`, 2026-09-07. Read-only review of root's
Application changes. **No blocking correctness defect found in the current
empty-cove profile.** This is source/SDK inspection, not a graphical test pass.

Reviewed hashes:

- `src/app/application.cpp`:
  `f4ed875bee7879f000de493c716038c5c119691ce43857d77500974e6d3e1586`
- `src/app/application.hpp`:
  `108063030695770c27dd958c206b06b3c46624fe5c43a4eb77e69db30591a22a`

## Verified boundaries

1. **Dependency lifetime:** `SalvageLocalSessionState` declares catalog,
   preparation adapter, then session (`application.cpp:138`). Reverse destruction
   destroys the session before either referenced dependency. Factory failure
   retains this ordering through local ownership. Preview initialization failure
   follows the existing enclosing Application cleanup path.
2. **No live fake machinery:** `CovePreparationAdapter` rejects begin/poll and
   cannot activate (`application.cpp:120`). The bootstrap has empty inventory,
   builds, cargo and jobs, with workshop disabled (`application.cpp:5030`). No
   caller advances the fake session clock in Application.
3. **Reset and Leave ordering:** UI callbacks validate and enqueue typed scenic
   intent only (`application.cpp:5077`). Pending Leave supersedes Reset, and
   admission prevents Reset after Leave has begun. Frame-boundary dispatch closes
   admission before requesting Leave and before polling completed retirement
   metadata (`application.cpp:5143`). Therefore Leave can stop a reset respawn
   even on the frame that old generations finally retire.
4. **Lifetime acknowledgment remains intact:** completed metadata still flows
   through `observeRetirement`; accepted destroys alone do not report completion.
   The retained metadata allocation and generation checks remain unchanged.
   Reset preserves the same session. Leave/failure closes admission; scene and
   session destruction occur before the physics world teardown. Repeated close
   is safe under the core's cancellation contract.
5. **Read-only status:** the JSON method calls only `snapshot()` and
   `admissionOpen()` (`application.cpp:5123`). It exposes a copied empty state,
   string-encoded 64-bit counters/resource amounts, and no participant/token or
   allocator authority. The UI's polling cannot commit or advance a tick.

## Namespace entropy inspection

The new namespace consumes four uniformly distributed 32-bit words from
`std::random_device`; no constant seed or fixture namespace is installed.
Participant/token counters 1/2 are scoped by that fresh namespace. All-zero
namespace failure is caught by session validation; Reset does not regenerate it.

For the installed native GCC 15.2 toolchain, preprocessing `bits/c++config.h`
reports `_GLIBCXX_USE_DEV_RANDOM=1` and `_GLIBCXX_USE_RANDOM_TR1=1`. This is not a
statistical entropy certification or a promise about every future standard
library implementation.

Both locally installed Emscripten 6.0.2 and Bazel's Emscripten SDK select
`_LIBCPP_USING_GETENTROPY` in libc++ `include/__config:337–338`.
`libcxx/src/random.cpp` calls `getentropy`, musl `getentropy.c` forwards to
`__wasi_random_get`, and `src/lib/libwasi.js:601–615` uses browser
`crypto.getRandomValues` (with a temporary buffer for shared WASM memory).
Supported Node execution uses its crypto provider. There is no browser
`Math.random` fallback on this path.

Do **not** add a portable `random_device.entropy() > 0` admission check: this
libc++ implementation reports zero for its getentropy branch despite using the
browser crypto provider. Verify fresh-session initialization in the actual
native/browser launch checks. An unavailable browser entropy provider is an
initialization failure; the C++ catch alone does not certify graceful recovery
from arbitrary JavaScript host exceptions.

SDK inspection used the local Nix Emscripten 6.0.2 source and
`/home/modkin/.cache/bazel/_bazel_modkin/cbcbf4be6285ea63e5938aa06a96dc1e/external/emsdk++emscripten_deps+emscripten_bin_linux/emscripten`.
No SDK, Application, shared build or game source was modified by this review.

## Remaining acceptance evidence

Root still needs the compiled native/WASM bridge and actual scenic lifecycle
checks: Reset, Leave-over-Reset, R, re-entry, unchanged empty economy, no fake
tick advancement and unchanged retirement failure handling. Existing screenshot
or source-only evidence cannot substitute for those runs.
