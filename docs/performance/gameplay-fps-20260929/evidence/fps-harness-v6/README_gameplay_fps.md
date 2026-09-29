# Real gameplay FPS captures

`benchmark_gameplay_fps.mjs` measures the full Frontier and Free Build scenes on
hardware WebGPU. It counts acquisition calls on the main canvas and
checks that count against the engine's `surface_acquired_frames` counter.
The engine's loop count is reported separately because a loop can submit no
rendered surface. The same GPU texture may serve multiple uncapped renders
between compositor frames. Unique texture turnover is reported separately;
deduplicating it would undercount renderer headroom. Interval percentiles come
from acquisition calls, not RAF
callbacks or repeated asynchronous GPU timing records.

These are uncapped, headless surface-frame rates. They measure renderer
headroom and include normal gameplay; they do not measure a monitor's display
rate or input-to-photon latency. No additional GPU completion fences are added.
GPU intervals are optional, deduplicated by their originating engine frame.

Use Node 22 or later on the same operating system as Chrome. For this WSL host,
copy `scripts/performance/` and packaged sites into
`C:\Users\Public\voxys-fps-20260929`. Run Windows Node and Windows Chrome:

```sh
'/mnt/c/Program Files/nodejs/node.exe' \
  'C:\Users\Public\voxys-fps-20260929\scripts\performance\benchmark_gameplay_fps.mjs' \
  '--chrome=C:\Program Files\Google\Chrome\Application\chrome.exe' \
  '--site=C:\Users\Public\voxys-fps-20260929\baseline' \
  '--output=C:\Users\Public\voxys-fps-20260929\baseline-results' \
  --repeats=3 --experiences=frontier,build --scenarios=idle,orbit,walk
```

Sites must contain coherent `index.html`, JavaScript/WASM/data and page assets.
The isolated Chrome profile is created inside the selected output directory and
removed at shutdown. The runner does not write its profile to the OS temp folder.
Optional `--profile-root=DIR` selects a subdirectory inside that output directory.
If Windows process interop or the local browser connection is restricted, the
capture cannot run; an output-path change does not bypass that restriction.
Both `voxy_wasm.*` and Bazel's `voxy_wasm_cc.*` stems are recognized. Exactly
one complete stem is required; generated glue is never rewritten. A data file
is required for local builds; separately declared dynamic packs are supported.
The runner records source artifact hashes, actual GPU
devices, Chrome/driver identity, flags, physical canvas sizes, raw acquisition
timestamps, health, physics capacities and starting/ending camera/player poses.
Each trial opens a new world through the production save interface and waits
600 authoritative player ticks for initial UI feedback to settle. Each scenario
then runs 600 player ticks. The GPU physics clock has a different bounded
catch-up policy, so it cannot define an equal walking path on slow builds.
Walk holds W at the matched initial player/camera heading for the entire
journey and rejects heading drift above 0.001 radians. Orbit drives one bounded
mouse path that returns to its center and holds the final 50 ticks.
Exact observed input ticks and overshoot are retained.
The readonly `_voxy_get_adventure_player_tick` scalar is polled every 8 ms for
this driver; its read count and CPU time are retained and the same cadence and
reader are required on both sides. Older builds can use full-state JSON for a
diagnostic capture, but clean timing requires the scalar getter.
A player clock that stops for five seconds fails immediately, even if GPU
physics continues. Raw before/after player ticks must match the completed path.
This journey is an experience comparison, not a bit-exact physics replay.

The runner explicitly enables the day/night cycle through its public renderer
control. The default hour is 16:00 with the cycle paused, providing the same sun
direction on both sides. Add `--day-night=1` to measure the normal moving sun
and its shadow work. Preserve that setting between baseline and candidate.
Actual enabled/paused/hour/intensity/direction/color values are read at the
scene endpoints outside the timed span and retained in each row. Disabled or
mispaused cycles, missing settings and a moving hour that fails to advance
reject the scene. Fixed-hour captures require unchanged lighting at both
endpoints and across variants. `--hour=0` selects ordinary moonlit midnight;
it does not imply zero direct light. The optional `--require-zero-sun=1` guard
requires exactly zero actual intensity at both endpoints and rejects moonlit
midnight. It does not change the light or define a normal-night FPS claim.
`--width`/`--height` change the requested physical resolution; both actual
canvas and acquired textures must match. `--uncapped=0` selects the normal
RAF loop, with results still explicitly labeled as headless surface frames.

Add `--candidate-site=DIR` to use baseline/candidate/candidate/baseline order
for every repeat in one Chrome process. Add `--candidate-shaders=manifest.json`
to compare shader variants against the same site without rebuilding WASM.
The manifest maps actual shader module labels to a candidate file and the
frozen original, with paths relative to the manifest:

```json
{"ray_blit.wgsl":{"source":"candidate-ray_blit.wgsl","expectedSource":"original-ray_blit.wgsl"}}
```

The runner records SHA-256 for both sources. An optional `expectedSha256` field
also checks the original file against a previously recorded hash. Browser
substitution requires exact original-source equality, preventing a label from
replacing a different version. Every override must actually be used; applied
counts and source hashes are retained.
Use the actual module labels from the engine rather than assuming filenames.

Clean timing and work diagnostics are separate runs. `--counts=1` enables
dispatch, workgroup, render-pass, pipeline-specific draw and buffer-upload
counters. The report calls out `cove_hud_quads` and `adventure_hud_triangles`
writes and bytes per acquired frame. `--profiling=1` enables the existing engine
GPU timestamp sampler when supported. Software adapters, missing frame
counters, hidden pages, inactive physics, engine errors, changed dimensions,
capacity overflows, inactive exploration, open game menus, missing/nonpositive
health and unapplied shaders fail the capture. Both endpoints must preserve
health, mode and menu parity between baseline and candidate.

For the candidate's fresh-world Frontier idle scene, add `--counts=1
--check-hud-work=1`. This requires zero recurring uploads to both HUD labels
after warmup, a stationary player and unchanged health/menu. Initial uploads
and creation of both HUD buffers must have been observed, so missing
instrumentation cannot pass as zero work. An empty triangle backend can create
its buffer without ever uploading triangles.
This guard is scoped to the first idle scenario in a new camp; standing near
enemies or finishing a save later can legitimately update the interface.

`--cpu-profile=1` captures a separate V8 `.cpuprofile` for every scenario at
1 ms sampling intervals, plus artifact/browser/source metadata. These runs are
diagnostics and are rejected by the clean timing comparison. WASM names stay
unverified unless `--baseline-symbol-map=FILE` and
`--baseline-symbol-map-wasm-sha256=SHA` identify a map for the captured WASM
hash. Candidate maps use the corresponding `--candidate-...` options. Historical
maps are never reused silently.

For sequential build phases, compare two complete clean same-host reports:

```sh
node scripts/performance/compare_gameplay_fps.mjs \
  --baseline=/tmp/baseline/report.json --candidate=/tmp/candidate/report.json \
  --output=/tmp/gameplay-comparison.json

node --test scripts/performance/test_gameplay_fps.mjs
```

Comparison requires matching browser/flags/GPU/driver/configuration, balanced
repetitions, valid scenes and comparable player/camera endpoints. It rejects a
median acquisition-throughput drop or p95/p99 interval increase above 7.5%.
Aggregate run medians average the two central values for even repeat counts;
within-run interval percentiles retain nearest ranks. This is a regression
guard; a few noisy runs do not establish statistical significance. Preserve
raw captures and confirm promising results through alternating A/B runs with
no simultaneous builds or other performance probes.

On Windows, the runner reads the built-in power-line API immediately before
each fresh trial, outside measured scenes. It records Online/Offline status
with the trial and rejects aggregate comparisons if that status changes or
is missing. Battery percentages provide context and do not gate acceptance.
A stable power-line status does not prove stable clocks or temperatures;
preserve any early/late throughput drift and use balanced order on a warmed
host. No system settings are changed.

Each capture freezes its exact six runtime source modules under `harness/`
and records their hashes. Journey version 6 retains the straight W path and
reviewed median/game-state/power guards, and adds actual lighting controls and
endpoint guards from version 5 plus an output-owned profile. Versions 3 and 4 retain their original bundles; their requested
day/night settings were not verified against renderer lighting. Version 3 also
retains its failed turning-walk comparisons.
