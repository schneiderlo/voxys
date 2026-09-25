# Compile earlier and reuse graphics setup

Follow-up to the [collision and render compilation changes](../README.md).
The default page can now compile GPU programs before the large asset pack has
finished downloading. A local hardware comparison reached play in **148.573 →
110.258 seconds**. A subsequent visit reached play in **99.959 seconds**; this
is faster, but still far from instant on this machine.

These measurements were collected locally before publishing the changes.

## Changes

- `physics_ballistic.wgsl`: primitive and brick terrain contacts share one call
  to the large contact generator. Candidate order, rejection flags, manifold
  reduction and the primitive return value are preserved.
- `physics_queries.wgsl`: capsule queries already handle authored shapes
  before sampling primitive surfaces. The sampler now calls the primitive
  helper directly, excluding five unreachable authored traversals from inlining.
- `web/gpu_startup.js`: reuse identical shader modules, explicit layouts and
  pending/completed async pipelines on the current device. Keys include the
  complete WGSL and descriptors, including constants and render state; only
  diagnostic labels are ignored. The 235 logical startup requests make 181
  actual pipeline requests. These are API request counts, not a count of driver
  compilations: the browser may already cache some repeated requests.
- The release workflow records descriptors during its real application smoke
  run. `publish_graphics_recipe.py` exports matching production compute shaders
  and their dependencies. The local published file contains 135 programs in
  726.1 KiB. Render targets and the adapter-specific narrow-phase shader are
  excluded from this portable file. A second CI smoke run checks reuse with
  the published file present, including the software-adapter fallback.
- A successful startup saves its full recipe locally, keyed by WASM/data hashes,
  experience, GPU profile and configuration. World IDs and telemetry switches
  do not invalidate it. Later visits can also prepare the adapter-specific
  programs early. Work starts with the previously expensive programs and uses
  at most four speculative requests at a time on hardware; normal engine
  requests can start their required programs immediately. Software adapters
  retain exact-request reuse but skip speculative compilation.

Recipes contain WGSL and descriptors, **not executable GPU binaries**. Every
GPU object belongs to the current device. Browser/driver binary caching remains
outside the application's control. Cache failures, stale recipes and failed
speculative requests fall back to ordinary creation. Pending speculative work
settles before the loading screen is dismissed; the temporary API wrappers and
extra GPU references are removed. Frame-loop code and quality settings are
unchanged.

## Focused compiler measurements

Windows Chrome 153, Intel Iris Xe, production layouts captured from the actual
engine, fresh browser profiles, no concurrent benchmark or build. The shared
driver cache and hardware clocks were not reset. These are individual paired
runs, not statistical estimates for every GPU.

| Shader | Programs | Before | Retained change |
| --- | ---: | ---: | ---: |
| Ballistic/static contacts | 8 | 29.447 s | 26.899 s |
| Physics queries | 1 | 13.919 s | 9.894 s |

The immediate same-device repeat took about 1 ms in each test. That does not
establish equivalent speed after a page reload. Archived source bytes, hashes,
logs and reports are in `focused-summary.json` and the matching compressed files.

## Full-page comparison

All four final runs use the same locally built WASM and shader/data pack,
including both shader changes above. This isolates the additional early-start
and reuse behavior from those shader edits. Bazel's combined 144.64 MiB pack
includes optional experiences; this is larger than the production CMake core
pack. Do not compare absolute times with the production-pack measurements in
previous reports.

WASM SHA-256: `60eb17006ffe756579c6902fa2eb79c9618ae9be5055508ed094b7d28bb60521`.
Data SHA-256: `fb7e8ef5d5d6970f77d5aaf652866958e3e89d3a8754944b46db6d4d4a3762d7`.

The local server delayed `.data` responses by 10 seconds to make overlap
observable. This is a controlled delay, not a claim about real download speeds.

| Run | Browser profile | Graphics span | Playable observed |
| --- | --- | ---: | ---: |
| Control: reuse during normal initialization, no early recipe | Fresh | 127.333 s | 148.573 s |
| Published recipe | Fresh | 97.648 s | 110.258 s |
| Saved full recipe | Same profile and origin as previous run | 85.882 s | 99.959 s |
| Control repeat: early compilation disabled | Same retained profile and origin | 153.267 s | 166.845 s |

The first two runs downloaded the pack. Subsequent runs read it from Cache
Storage. The first accelerated run submitted GPU work at **2.841 s**, while
asset loading finished at **14.299 s**: compilation started **11.458 s before
asset loading finished**. It reused 131 distinct early programs; duplicate
engine requests account for the larger early-hit counter. No speculative
compilation failed.

All four runs produced exactly the same set of 181 complete program recipes.
The comparison expands resource references and includes full shader source,
layouts, entry points, constants, vertex formats, targets, blend/depth state,
and other recorded descriptor fields. It ignores labels and measured cost.
`page-summary.json` records this check and every outcome. Hardware/compiler
variance is visible in the two controls, so do not attribute all elapsed-time
variation to one change.

## Gameplay and limits

All runs used 1280×720, a 20-second post-resize settle and 20-second scenarios.
Instrumentation was removed before timing. No quality, physics frequency or
render-scale reduction was used.

| Idle measurement | Rendered FPS | Median / p95 frame interval |
| --- | ---: | ---: |
| Control | 58.57 | 16.7 / 16.9 ms |
| Published recipe | 58.74 | 16.7 / 16.8 ms |
| Saved recipe | 57.13 | 16.7 / 17.6 ms |
| Control repeat | 57.16 | 16.7 / 17.3 ms |

The first control completed idle, camera orbit, walking and throwing exactly
100 bricks. Both accelerated runs and the control repeat hit Intel's
`DXGI_ERROR_DEVICE_HUNG` during camera orbit. The repeat control has speculative
compilation disabled, and older baseline runs also encountered this failure.
All failures are retained. These measurements support unchanged idle pacing;
they **do not establish a complete moving-camera FPS comparison**, or prove that
the optimization fixes or cannot affect GPU resets. The screenshots show the
same scene and quality settings.

## Validation

- Full native and WASM builds passed. The packaged data was checked to contain
  the exact retained ballistic, query and narrow-phase shader sources.
- 53 loading/startup JavaScript checks and two publisher checks passed. Coverage
  includes descriptor distinctions, mutation, stale configuration/release data,
  failed compilation retries, storage failure, device loss, bounded warmup,
  draining failed work, and aborting an unnecessary download before play.
- Native GPU correctness checks passed on llvmpipe: five query checks, two
  authored-query checks, two terrain-physics checks and two brick checks. The
  expanded capsule-versus-box/cylinder query check also passed with the original
  shader. These are correctness results, not hardware FPS measurements.
- Shared LEGO/authored shader-generation guards and `git diff --check` passed.

The software-browser checks led to a conservative fallback. Four speculative
programs at once exceeded the application's startup deadline on SwiftShader.
The control, with early work disabled, completed initialization at 420.026 s
and retired 43 frames without GPU errors, but headless Chrome returned a black
screenshot and failed the image gate. A trial with one speculative program in
windowed Chrome also exceeded the startup deadline. These browser modes differ;
the trials do not establish a matched software speed comparison.

The final implementation **disables speculative compilation on software
adapters**, retaining normal initialization and duplicate-request reuse, as in
the software control. Unit checks cover both saved and published recipes on
SwiftShader and fallback adapters. Hardware scheduling stays at four; the
hardware measurements above precede this software-only guard. The complete
software-browser smoke was not green locally, and no software FPS improvement
is claimed. Neither the image gate nor the startup deadline was relaxed.

All 135 published compute programs also match the software control's complete
source and descriptors. Its adapter-specific narrow-phase programs remain
excluded from the public file. `software-portable-match.json` records the check.
The failed four-worker run and headless control are archived. A headless retry
was stopped deliberately before completion to switch to the windowed mode used
by the release workflow; its report is labelled `aborted`.

The original measured source hashes are in `tested-sources.json`; the final
software guard and diagnostic changes are in `final-software-sources.json` and
`gpu_startup.final.js.gz`. The discarded one-worker version and its windowed
report are retained separately. GitHub Actions itself has not been run locally.

## Reproduce

Build with `nix-shell --run 'bazel build --config=wasm --jobs=4 //:voxy_wasm'`,
stage the web files and built assets, and write the release manifest. For a
local Bazel combined pack, use the local-only 200 MiB manifest limit; production
keeps its existing 97 MiB core-pack budget.

```sh
# First capture: no published recipe yet; this saves graphics-recipe.json.
node scripts/profile_gameplay.mjs --chrome=PATH --site=SITE --output=CONTROL \
  --startup=1 --data-delay-ms=10000 --settle-seconds=20 --seconds=20

python3 scripts/publish_graphics_recipe.py SITE CONTROL/graphics-recipe.json

# New profile for first-visit overlap. Reuse PROFILE and PORT for a return visit.
node scripts/profile_gameplay.mjs --chrome=PATH --site=SITE --output=EARLY \
  --profile=PROFILE --port=8851 --startup=1 --data-delay-ms=10000 \
  --settle-seconds=20 --seconds=20

# Focused compiler comparison; repeat with an archived baseline shader.
node scripts/performance/compare_pipeline_startup.mjs --chrome=PATH --mode=batch \
  --recipe=CONTROL/graphics-recipe.json --label=physics_queries.wgsl \
  --shader=shaders/physics_queries.wgsl --output=QUERY
```

The repeat control differs only by an immediate return in the staged
`gpu_startup.js` `load()` function. That measurement-only return is not in the
working-tree implementation. `tested-sources.json`, `package.json`, compressed
recipes and complete reports identify the measured artifacts.
