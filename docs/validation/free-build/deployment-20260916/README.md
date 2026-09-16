# Creative release deployment repair — 2026-09-16

Release request: push the current free-building game to main and verify Pages.
The subsequent 90-minute performance investigation starts only after that
release is verified. The prepared benchmark is not a completed performance pass.

## Failure and diagnosis

- `5b52d209` passed the full native hook but clean Pages packaging exposed the
  missing tracked `shaders/adventure_hud_triangles.wgsl` file.
- `f6a70ea4` adds that shader. Pages run `35099334968` then reached full browser
  startup and its Chrome software GPU process crashed (SIGSEGV/139).
- The live page consequently remained at source `765eca212188a3d93ea21fa0ba4be25637e17c62`.
- The failure reproduces locally with Chrome 152 and Chrome 151. No WebGPU
  validation error precedes it. A crash stack points into `libvk_swiftshader.so`.
- Temporary, uncommitted diagnostic harnesses isolate compute pipelines; they
  are causal experiments and **never count as a passing release gate**.
- Isolated `solve_static_contacts` fails with the original authored terrain
  helper and passes with caller-owned output scratch. The regular Radeon
  authored-shape test suite passes all 47 cases after this change.
- The authored box narrow-phase kernel also fails when compiled together with
  the ordinary/LEGO path; its authored-only compilation passes. The production
  repair uses two specialization values for the four box-containing pair classes.

## Static-contact equivalence

`authored_terrain_contacts` and `authored_lego_contacts` now clear and write the
caller's `AuthoredTerrainResult` through a function pointer instead of returning
the array-containing structure by value. The sole caller passes its distinct
local result. Initialization, traversal, clipping, arithmetic, tie breaking,
contact order, work limits, status codes and telemetry are unchanged. Every
old early return exposes the same fields in the same zero-initialized result.
There is no alias with the input shape, terrain texture or body buffers.

Focused check:

```sh
nix-shell --run 'bazel test //tests:voxy_tests --test_arg=--gtest_filter=GpuAuthoredShapes.* --test_output=errors'
```

Result: 47/47 passed, including LEGO cap/riser/gap contacts, distant sectors,
authored body frame, invalid geometry and lifetime checks. Full startup, normal
commit hook and final live-build verification are still required.

## Narrow-phase equivalence

For pair classes sphere/box, capsule/box, box/box and box/cylinder, the same
bucket is dispatched twice with `AUTHORED_PAIR_PASS` false/true. After the
unchanged capacity guard and side-effect-free pair lookup, a pass returns unless
its value equals `pair_has_authored(pairRecord)`. These predicates partition the
bucket: every valid pair runs exactly one original branch, once, and writes its
original manifold ordinal. Collision arithmetic, traversal order within each
pair, cache inputs and atomic telemetry increments are unchanged. All other
classes retain one dispatch. The authored pass completes before finalization
and compaction, so downstream readers see the complete same set of manifolds.

Cost: four extra specialized pipeline objects and four indirect dispatches per
narrow phase. Each extra invocation rejected by the partition does no contact
work. This is a browser startup compatibility repair, not a measured performance
optimization. The subsequent profiling pass must include this deployed baseline.

No compiler version pin, disabled collision path, shader stub or relaxed frame,
GPU-completion, UI, error or image assertion is part of the release repair.
The real packaged game passed the extended diagnostic in 157.423 seconds:
13 completed frames, GPU timestamp available, creative authority and hotbar
visible, no browser/GPU errors. That diagnostic omitted the screenshot.

The final software-driver startup budget is six minutes, with an eight-minute
whole-process deadline; the first observation shares the remaining startup
budget rather than expiring after 30 seconds mid-compilation. Hardware retains
its three-minute startup and four-minute whole-process budgets. Other RPCs
retain their 30-second bound. Final screenshot-enabled startup and the normal
full hook results are recorded below.

The screenshot-enabled software check passed in 150.845 seconds, but visual
inspection caught missing thumbnail images: the smoke server served SVG as
`application/octet-stream`. The SVG files themselves are tracked and valid.
The server now emits image MIME types and the creative gate requires every
visible hotbar image to be complete with positive intrinsic width. A corrected
screenshot-enabled run is required before publication.

## Final local browser acceptance

Passed the actual screenshot-enabled smoke script in **153.833 seconds** on
Chrome 152/SwiftShader. All pipelines are real; no diagnostic overrides or stubs.
The hotbar and all its brick images are visible, GPU work completed, and the
browser/GPU error lists are empty. See `software-startup.json` for the exact
command and bounds, and `software-startup.png` for the checked scene.
These software-driver timings are startup diagnostics, not game FPS evidence.

## Full hook

`nix-shell --run 'bash .githooks/pre-commit'` passed in 1190.187 seconds:
2,457 tests passed, eight opt-in tests skipped, four tests remain disabled by the
existing suite, and the terrain importer passed. The real GPU was Radeon/Vulkan.
No test filter or bypass is used for the commit hook.

Public deployment **passed**: Pages run `35110094212` built and deployed
`7ea4d7628a6d2245a8563060e5519d19c8926a9d`. Public HTML reports this exact
source; the actual public browser reaches the creative hotbar. Verified
2026-09-16 14:56:33 UTC. The actual commit hook also passed 2,457 tests and the
importer (1,099.8 s aggregate suite). The performance investigation starts here.
