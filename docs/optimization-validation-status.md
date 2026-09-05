# Optimization integration: validation and runtime repairs

The owner requested resolving all branch conflicts, pushing to main, and
continuing optimization. The first integrated release is
`8cb4baf26bc564bb706ae72b1dd793283b736035`; its Pages deployment and main shader
CI both passed. The follow-up incorporates the native and browser fixes below.
This record supersedes the pending-runtime snapshot at the first promotion.

## Branch integration

All existing branch histories are preserved; ref updates are fast-forwards,
not force pushes. PR #4 was merged with PR #2, already on main. The motocross
prototype is included as an optional experience rather than replacing the
existing terrain demo. Use `?experience=ridgebreak` or `--config ridgebreak.cfg`.
The 8K terrain, 2048-pixel macro texture startup-memory fix, material layers,
normal maps, anisotropic filtering, water simulation cadence and physics
settings are retained. See `optimization-integration.md` for merge decisions.

Temporary write-enabled integration/preparation workflows and patch transport
files are removed from the delivered tree. Permanent shader, bridge and native
renderer regression workflows use read-only repository permissions.

## Additional optimization round

The current water shader bypasses scene-refraction depth reads, UV distortion
and fallback material evaluation for totally internally reflected underwater
rays. Above-water scattering remains only in the above-water branch. Seabed
caustic samples beyond their existing zero-contribution fade and backlit terrain
specular calculations are also avoided. No stale camera cache, lower resolution
or reduced quality tier is used to manufacture higher FPS.

## Runtime bugs found and fixed

### Native gradient-bake lifetime

`e6906823b2a3908ef451b9c5d1f18b431142d691` releases the ended compute-pass handle
before finishing/submitting its command encoder. The previous live handle caused
wgpu-native to reject submission because the command buffer was still referenced.
This changes host-object lifetime; it adds no GPU readback or wait.

After the fix, all **18 BlitPathTest cases passed**, including initialization,
move ownership and rendering. Run `33934929387`, job `101220908502`; raw artifact
`9959997083` (`runtime-repair-native`). The production native targets compiled
and linked successfully.

### Browser single-ended timestamp conversion

`f4235fce9a9aab6aa3dcc19ed4558db9b79eb90f` fixes the pinned emdawnwebgpu bridge
forwarding the C `UINT32_MAX` missing-query sentinel as a real JavaScript index.
Actual canvas testing reported index 4294967295 outside the eight-entry query
set, invalidating the background pass and its command buffer.

`web/loader.js` now normalizes that exact sentinel into an omitted optional
property for render and compute passes on renderer-owned devices only. It does
not modify global GPU prototypes, mutate caller descriptors, disable profiling,
or suppress unrelated validation errors. Eight Node regression groups pass;
loader URLs are build-versioned so the compatibility fix is not stale-cached.

## Passed validation

- Production combined WASM compilation/linking: run `33933267595`.
- New shading-round WASM compilation and preload packaging: run `33934079919`
  (the initial runtime smoke in that run failed; build success is separate).
- Initial main Pages build **and deployment**: run `33934619753`.
- Initial main combined shader regression: run `33934619748`, all steps passed.
- Six gradient CPU/lifetime groups plus ten water/material CPU groups pass.
- GPU differential tests on Chrome/SwiftShader compare 1,093,668 float words for
  combined changes versus the original scene, maximum absolute error
  1.1920928955078125e-7. The isolated additional round compares 1,126,436 words
  with zero differences, including critical angles and optional-course materials.
  Both validate all eight timestamp paths. Raw artifact `9959417418`, run
  `33933604201`. These are software-adapter shader checks, not Intel benchmarks.
- **Real browser startup and GPU retirement now pass for both scenes**, with
  profiling enabled, no uncaptured GPU errors and no device loss. Run
  `33935724464`, browser job `101223213162`, artifact `9960117576`
  (`timestamp-bridge-startup-reports`). Each scene reached 16 frames and returned
  a GPU timing sample, exceeding the eight-frame submission limit.

Browser smoke scope: each scene runs in its own headless Chrome process with
Mesa Vulkan canvas support and a SwiftShader WebGPU adapter, at 320x240 with a
1024-body reserve. It uses the previously compiled integration binary
`b0cf3a4179fafb2a235b8dbd26b06846ebb9ef45`, current web assets and explicitly
injected current ray-blit/water WGSL. It validates the runtime bridge and current
shader integration; it is not an exact final-binary full-resolution image or
hardware-FPS test. The final Pages pipeline rebuilds the production binary.

## Full native suite: four unresolved failures

The completed post-repair CTest run registered 1,525 cases:
**1,515 passed, 4 failed, 2 skipped, and 4 disabled**. It is not fully green.

Remaining failures:

- `AuthoredCoveTest.ProfileHasReadableCoastalHierarchy`
- `AuthoredCoveTest.MaterialZonesAreNormalizedAndDistinct`
- `BlitShaderTest.ShorelineBlendAndWetResponseAreContinuous`
- `BlitShaderTest.CoastalFoamUsesDepthCrestAndSpatialDecay`

The cove implementation and affected test files are unchanged from the starting
main. The shader string expectations in the last two tests are also absent from
that starting main shader; the raw artifact contains that source and an empty
diff for the unchanged cove/test files. These failures were not disabled or
weakened to produce a green report. A complete baseline runtime comparison was
not performed, so this records source evidence rather than claiming a fully
certified baseline test run.

## Performance limits

Warm software shader timings are mixed and include regressions, with the full
paired results retained in `optimization-integration.md` and the raw report.
The new work-avoidance branches are not a demonstrated whole-engine speedup.
There is **no measured Intel/Windows FPS improvement or demonstrated recovery
from the reported 150-to-10 FPS regression**. Full-resolution moving-scene image
and p50/p95/p99 frame-time acceptance on that adapter remains outstanding.
