# Terrain cache and Cove lighting audit

Read-only source audit, 2026-09-29. No production changes, builds, GPU dispatches, or timing probes were run for this audit.

## Findings

The expensive terrain traversal is already absent on a settled cached frame. The two promising larger changes are separating geometry from sun-dependent shadow refresh, and caching the actual Cove lighting model. Neither is a safe one-line invalidation change.

### Exact dirty triggers and current phases

- `src/render/raycast_path.cpp:1097`: `updateStaticUniforms` compares a canonicalized full `CameraUniforms` byte snapshot. All water fields, fog/color/exposure, view-space light, and metrics z/w are excluded. World-space light is excluded only for ordinary non-LEGO mode with dynamic-shadow flag exactly zero. Therefore a sun-direction change invalidates geometry and shadow together in physical LEGO and day/night routes.
- `src/app/application.cpp:3562`: day/night samples lighting at roughly 0.1-second intervals. `application.cpp:6080` sets `lightDirWS.w=1` during day/night, enabling the dynamic terrain shadow marcher. Lighting changes can therefore rerun primary traversal on an otherwise settled view.
- `src/render/raycast_path.cpp:963,997,1030`: resize, heightmap rebinding, and shadow-map rebinding dirty the cache. Camera/projection, terrain metrics, and mode changes dirty the canonical snapshot. Water resource rebinding changes bind groups without invalidating the terrain-only cache.
- `src/render/raycast_path.cpp:1172`: physical LEGO study and normal cached routes dispatch the same full raycast entry when dirty. A clean cache and deferred water return without dispatching compute. A timestamp query can encode an otherwise empty compute pass; it does not force the primary traversal.
- `src/app/application.cpp:5927` forwards the terrain cache refresh to `BlitPath::setStaticCacheState`. `blit_path.cpp:2657` dirties background color whenever depth/shadow were refreshed.
- `src/render/blit_path.cpp:2768` compares its own canonicalized snapshot. It excludes wave time/local offset, exposure, and water surface-only controls, but retains view, lights, fog, shoreline datum/enabled flag, and relevant underwater properties. Resize, static terrain rebinding, terrain/material/lightmap/layout rebinding, water-composite resource rebinding, and debug controls also dirty background color. Changing the cycling mode or fixed sun invalidates the sky bake. Filtered Cove environment views are currently not part of this background cache key.
- `src/render/blit_path.cpp:2975` refreshes the legacy HDR background first. The mesh callback subsequently exposes the current filtered environment and shadow bindings, then `renderSceneTerrain` writes the live opaque scene. Water reads that scene color/depth, not the legacy terrain seed when opaque composition is active.

### Precision prevents a naive geometry/shadow split

Depth and terrain shadow caches are `R32Float`; material normals plus the LEGO top-distance channel are `RGBA16Float` (`raycast_path.cpp:509,544,563`). Static and live bind groups share the same material output (`raycast_path.cpp:887,908`).

The raycast computes shadow gating before this half-precision store (`shaders/terrain_raycast.wgsl:1320`). LEGO gating uses full-f32 normal dot light, the `-0.7072` range, `legoTopDistance < 0.04`, and hit-position edge distances. The material store later rounds normal and top distance to half (`terrain_raycast.wgsl:1372`). Reusing that rounded material to decide whether to trace a shadow can change a boundary decision. Reconstructing position must also repeat the primary `origin + rayDir * cachedDistance` expression rather than using the differently expressed blit view-position reconstruction.

Minimum general split: separate geometry and shadow dirty keys; a dedicated shadow-refresh compute entry; retain sufficient full-f32 shadow-decision data or exactly reconstruct it from full-f32 cached hit distance and original geometry identity; explicit resource bindings/usage and dispatch accounting; resize/move/rebind ownership updates. Ordinary smooth-heightfield dynamic shadows do not require a cached LEGO normal and may permit a narrower exact prototype, but that does not solve the current physical LEGO path and needs its own full-entry boundary oracle. Simply dropping `lightDirWS` from the key freezes terrain shadows.

There is also a source-level suspected existing invalidation gap, separate from the FPS work: `application.cpp:3699` can rewrite the currently bound baked shadow texture in place and return without calling `setShadowMap`. In ordinary fixed-sun mode the raycast key excludes world-space direction; a settled camera may therefore keep the old per-pixel shadow cache after that rewrite. Physical/day-night routes retain direction and refresh. This has not been reproduced. A future split should track an explicit shadow-content revision and test same-view texture rewrites, not only view replacement.

### Cove is deliberately re-shaded live

`shaders/ray_blit.wgsl:2973` caches `backgroundTerrain`, with object visibility/contact both one. `fsSceneTerrain` can reuse it for unshadowed pixels. `fsSceneTerrainCove` (`ray_blit.wgsl:3017`) instead overwrites every positive-depth pixel with `backgroundCoveTerrain`, regardless of visibility. It cannot reuse legacy material lighting: Cove adds authored props, normal-variance material sampling, wet-film energy, and fixture-owned filtered IBL.

Current Cove resources arrive after the background refresh, via `blit_path.cpp:3016` and `bindSceneEnvironment` at 2220. The Cove live pipeline has environment group 2; the background pipeline has only the original layout (`blit_path.cpp:2117,2127`). A real Cove cache must run after this frame's environment bake, track source identity and generation, and preserve new-environment same-frame visibility.

Underwater terrain uses live `waterMotion.x` in `underwaterTerrainCaustic` (`ray_blit.wgsl:2285`) and applies caustics before underwater medium processing (`ray_blit.wgsl:2950`). The static background deliberately canonicalizes time to zero. Copying cached color underwater freezes animation. Splitting static lighting from live caustics across the existing RGBA16F cache introduces an extra rounding point and is not generally byte-identical. An above-water-only cache can avoid this particular problem, but still requires the correct Cove shader, environment ordering/key, shadowed-pixel recomputation, and exact rounding checks.

Skipping legacy terrain shading in the Cove background refresh is also not unconditionally safe. Admission/Leave frames with no shadow-producing scene call `opaqueScene_->seed` from the entire legacy background (`blit_path.cpp:3014,3021`). That fallback remains a real consumer of its terrain pixels.

## Small shader-only candidate

Move the `backgroundTex` load in `fsSceneTerrainCove` into the non-positive-depth branch. Positive-depth pixels overwrite the entire cached color anyway. The texture-load coordinates and comparison remain identical. No cache key, pipeline layout, material math, water input, or fallback behavior changes. This can remove one half-color texture load for a terrain pixel; the driver may already eliminate it, so source work alone does not establish a speedup.

Candidate files are under `shader-candidates/cove-lazy-background-load/`; production remains original. Real-game promotion requires a clean Cove hardware comparison after an exact full-entry oracle.

## Coverage and required additions

- `tests/test_raycast_path.cpp` covers configuration, null/move lifetime, and workgroup dimensions, not GPU cache invalidation or cached geometry/shadow equivalence.
- `tests/test_shader_raycast.cpp:289` checks filtered-normal source structure; the GPU test at 369 only compiles the shader. Source-string tests do not prove a new cache split.
- `tests/test_blit_path.cpp:401` validates baseline bind limits and move ownership.
- `tests/test_blit_path.cpp:417` checks cycling/fixed-sky transitions.
- `tests/test_blit_path.cpp:785` checks moving casters and contacts with fixed camera/depth.
- `tests/test_blit_path.cpp:789` checks Cove submerged energy and shared filtered environment. Its fixture at 1013 changes the environment while terrain remains cached and requires the new environment in that same frame. These numerical tests use tolerances and are not complete bitwise cache equivalence proofs.
- Existing temporary `terrain_image_oracle.py` executes actual `fsBackground`, `fsSceneTerrain`, and `fsSceneTerrainCove` on 11 day/night, sky, LEGO/study, water/underwater/active-caustic, and atlas cases with exact RGBA16F+R32F comparisons. It is suitable for the small lazy-load change. Add scene-only positive/zero/negative-depth boundary fixtures with varying finite cached colors, preserving original coordinates and actual shipping entries. Reuse the Intel descriptor replay and raw-readback proof process.
- Any larger split additionally needs primary traversal/shadow output comparisons at normal/dot/bevel/stud/ray-distance boundaries, consecutive geometry-hit/light-only/camera-change frames, night/sun crossing, resize, terrain/shadow-map replacement, environment-source replacement and re-bake generation, underwater animation, admission/Leave, and resource-validation checks. Counters must prove primary traversal stays unchanged while only shadow or lighting refreshes. Compare raw half/f32 outputs before accepting any FPS result.

## Recommendation

Keep the larger cache split as follow-up work. The current exact output contract, full-f32 shadow decisions, environment callback ordering, and live underwater shading make it structural. Screen the narrow lazy-load candidate only if the compiler retains the redundant load, then retain it only with a measured Cove game-frame benefit.
