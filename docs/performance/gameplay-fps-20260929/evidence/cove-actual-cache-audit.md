# Actual Cove terrain-lighting cache feasibility

This is a temporary investigation. No production change or FPS result is claimed.

The current Cove background cache contains `fsBackground`'s legacy terrain lighting. The current `fsSceneTerrainCove` subsequently evaluates `backgroundCoveTerrain` for every positive-depth pixel. A dedicated `fsBackgroundCove` can bake that same helper with object visibility 1 into the existing RGBA16Float cache. Live terrain may copy it only for above-water pixels whose current scene visibility is exactly 1. Shadowed pixels and all underwater pixels retain the original helper and animated caustics.

## Required state and ordering

- Add one background pipeline/layout and one private cache-content-kind bit to BlitPath. Existing validity/dirty flags remain authoritative; preserve every camera, material, texture, terrain-shadow and Sun invalidation trigger.
- Defer Cove background refresh until `SceneShadowConsumer::bindEnvironment` has bound the current fixture environment. The callback executes after that fixture's environment bake is encoded.
- If admission, Leave, or the underwater path needs legacy seeding, refresh `fsBackground` when the content-kind bit changes. Never seed those paths with the actual Cove cache.
- The actual-background pipeline can reuse the existing complete static group0, shadow group1, and environment group2. Group0 declares 12 sampled textures; group1 adds one; group2 adds three, totaling the baseline 16-texture limit. The actual descriptor topology still needs GPU validation.
- A private cache-valid value can use CameraUniforms.waterMotion.w without changing its 544-byte layout. A repository-wide audit finds no production shader or CPU consumer of w beyond initialization, static canonicalization and finite-value validation. x/y/z remain untouched. Write the flag only after matching content is encoded or already valid. Avoid exposing a persistent private flag through public camera state.

## Environment content identity

The current FilteredEnvironmentViews contains three handles, with no content generation. Rebakes update the same textures, and the existing binding callback returns early when those handles compare equal. Pointer identity alone therefore cannot key this cache.

A minimal ABI-preserving alternative is to create three fresh identically described sampled TextureViews for the existing output textures on each successful encodeBake. Create all views and groups before opening the compute pass; publish the three views together only after encoding succeeds. Existing bind groups retain references to their old views and still address the same backing textures. BlitPath's existing identity comparison then observes each rebake, creates one new environment bind group and dirties its background. The hidden EnvironmentLighting::Impl can store mip-level counts for those descriptors without changing the public class layout. An explicit uint64 content revision is clearer but changes the returned views struct and all callers' CPU ABI.

Discarded encoding is already covered in the current application: RenderFrameGuard calls BlitPath::discardEncoding for every unsubmitted frame, invalidating its background and sky; fixture discard also resets the mesh environment-encoded state. A retry therefore encodes the environment and cache again. Retain that contract for new pipeline failure, water failure, UI failure and command-buffer creation failure. Same backing textures mean publishing new sampled views before submission is harmless only under this same ordered-encoding/discard contract.

## Exactness and scope limits

The static camera canonicalization removes exposure and surface-only controls. Above-water Cove lighting does not use animated water time; the original fog helper reads fogColor.rgb. Underwater lighting uses animated caustics, so it remains live and keeps the original legacy background cache.

The new entry compiles the helper with a constant visibility 1 while the old live entry receives a runtime visibility. Compiler arithmetic specialization can alter rounding. The added RGBA16Float load/store is nominally idempotent for finite half values, but driver subnormal handling must be verified. Whole shipping-entry Intel comparisons are required before integration or timing. They must compare final live color/depth, not expect the intentionally different cached background images to match.

An unchanged camera and paused lighting can reuse this cache. Camera motion, changing daylight, material edits, environment rebakes, and original terrain-shadow dirtiness still refresh it. The current dirty-frame order shades legacy material in fsBackground, then shades actual Cove material again in fsSceneTerrainCove. The proposed refresh bakes actual Cove instead and copies unshadowed live pixels, removing most of that duplicate legacy shading even when the camera/light keys change. One actual Cove shade remains on each dirty frame, and shadowed pixels additionally shade live. Measure real gameplay before claiming an FPS benefit.

## Relevant files and existing checks

- src/render/blit_path.cpp: createPipeline, bindSceneEnvironment, renderSceneTerrain, updateStaticUniforms, render and shutdown/move operations.
- src/render/blit_path.hpp: background validity/dirty fields and discardEncoding.
- src/render/environment_lighting.cpp: hidden Impl, init and encodeBake.
- src/render/triangle_path.hpp: CameraUniforms waterMotion lane contract.
- src/app/application.cpp: RenderFrameGuard unsubmitted-frame rollback.
- src/render/salvage_asset_fixture.cpp: environment encoding, beforeColor callback, acknowledged/discarded submission.
- tests/test_blit_path.cpp: Cove environment/terrain composition and abandoned-frame helpers.
- tests/test_mesh_path.cpp: environment bake/rebake and abandoned-encoding tests.

The WGSL-only prototype is at shader-candidates/cove-actual-background-cache/ray_blit.wgsl. It must not be substituted into the current game without the corresponding CPU cache ordering, content identity, mode transition and validity-flag implementation.
