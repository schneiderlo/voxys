# Frontier: separate geometry and sunlight cache revisions

Status: source-only design. No implementation, build, GPU probe or timing was run for this design. Original collision code and accepted CPU patches remain unchanged.

## Concrete recurring cost

Frontier enables LEGO terrain (`src/core/config.hpp:231`) and the cached opaque adventure path. Its blit configuration sets `frontierVisuals=true`; Cove is a separate fixture-only condition (`src/app/application.cpp:5172`).

`Application::updateDayNight` advances the clock continuously, but publishes lighting at most once per 0.1 seconds (`application.cpp:3556`). `updateCameraUniforms` sends the effective Sun/moon direction in `lightDirWS.xyz` and sets `.w=1` for live terrain-shadow tracing when day/night is enabled (`application.cpp:6076`). This is existing visual behavior and must remain exact.

`RaycastPath::updateStaticUniforms` already removes water animation, exposure, fog, colors and indirect light from its terrain key. It retains `lightDirWS` in LEGO mode or live-shadow mode. Any exact change to that direction invalidates the entire static terrain snapshot (`src/render/raycast_path.cpp:1097`). `dispatch` then launches `terrain_raycast.main`, which repeats the primary hierarchical terrain DDA, stud intersection, normal computation and depth/material writes before tracing the new sunlight (`raycast_path.cpp:1230`; `shaders/terrain_raycast.wgsl:964`). Primary geometry does not depend on the light.

The archived, contended Frontier idle diagnostic records 78 full `raycast_pipeline` dispatches over 310 rendered frames, or 25.2%. Camera pose is identical at the two endpoints. This is evidence that the recurring dispatch exists, not a causal attribution of all 78 refreshes or an accepted performance comparison. Idle GPU stage medians were 0 ms terrain raycast and 22.315 ms LightingBlit because most sampled frames were cache hits. They conceal occasional raycast bursts. Moving-camera diagnostic medians were about 14.9–15.5 ms terrain raycast; these include shadow work and do not quantify the proposed saving.

The structural opportunity is a sunlight-only dispatch on an unchanged camera/geometry snapshot. This cannot be optimized away by the driver while the CPU still explicitly launches the full geometry kernel. Sky LUT baking is already cached. The earlier water-deferred-shadow candidate already moves PCF behind rejection and reuses inside-terrain baked shadow; it has exact full-entry images, but no persisted driver lowering or instruction-work evidence that the original PCF survives before discard.

## Proposed dispatch contract

Maintain two exact revisions/dirty states:

1. Geometry: the current canonical terrain key with `lightDirWS` additionally removed, plus explicit terrain/resource revisions and framebuffer dimensions.
2. Sunlight: the complete shadow-relevant light state, shadow-height resource/content revision, geometry revision, and shadow mode.

Keep existing full uniforms available to the geometry kernel: removing the light from the comparison key must not remove it from the uniforms consumed by the initial shadow calculation.

For the cached path:

| Change | Dispatch | Writes |
| --- | --- | --- |
| Camera, projection, framebuffer, terrain, LEGO geometry or first use | Existing primary geometry + original sunlight, with exact auxiliary hit metadata | Original depth/material/shadow, auxiliary metadata |
| Only sunlight or baked-shadow contents | New sunlight-only kernel | Terrain-shadow cache only |
| Neither | No terrain dispatch, except existing timestamp-only empty pass | None |

The direct LEGO path remains the original kernel and bindings. Initially scope the new path to `!useDirectPath`; the existing direct-vs-cache selection remains authoritative. `deferWaterComposite=false` still runs the existing water composite after either terrain refresh. No water fields enter the geometry key: the cached terrain snapshot deliberately zeros every water field today.

## Exact hit metadata

The new sunlight kernel needs more than the current material cache:

- Original `t` is already stored losslessly as R32Float terrain depth.
- Original unquantized `legoNormal.xyz` is used by `dot(normal, lightDir)` and the `0`/`-0.7072` thresholds.
- Original `legoTopDistance` is used by the bevel eligibility threshold `<0.04`.
- A miss-state distinction is needed to preserve the original early AABB return, which writes depth but leaves old shadow/material bytes untouched. A normal traversal miss or the `-2` loop-limit sentinel instead writes shadow=1.

The existing RGBA16Float material texture rounds the normal and top distance. Stud side normals are continuously varying (`terrain_raycast.wgsl:1182`), so reading the half-float normal back can flip shadow eligibility near a boundary. Copying that texture into a shadow-only kernel is not an exact implementation.

The practical exact auxiliary is one RGBA32Uint texture: `bitcast<u32>` of raw normal XYZ and raw top distance. Integer texture load/bitcast avoids additional normal precision loss and does not need filtering. For negative-depth pixels, the fourth word can instead encode the early-AABB-return versus ordinary traversal-miss state; positive hits interpret it as the original top-distance bits. Write this metadata on every geometry-refresh pixel, including sky/misses, so no uninitialized value can select a refresh branch. Smooth-terrain mode needs no LEGO normal metadata, but the common layout can retain it for a first exact prototype.

This is 16 bytes per pixel, one additional texture/binding. It is the simplest robust format, not a claim of an information-theoretic minimum. A narrower representation is possible only with extra invariants: normal Y is currently 0 or 1, and only one predicate of top distance is consumed by shadows. Packing flags into spare bits of the two normal-XZ float words could fit RG32Uint, but would require proving finite/range invariants for every accepted camera/terrain scale and preserving signed-zero/invalid-normal behavior. Do not begin with that packing. A 12-byte storage-buffer record saves four bytes but introduces storage-binding size limits at large framebuffers.

The existing geometry layout uses three storage textures. Adding this auxiliary makes four. Keep the original direct kernel/layout at three; the cached geometry entry has the extra binding. The sunlight-only entry needs one uniform, heightfield, optional/fallback baked-shadow-height field, sampled cached R32 depth, sampled RGBA32Uint metadata, and one writable R32 shadow cache: four sampled textures and one storage texture. It must never sample and write the same shadow cache in one usage scope. Check actual device limits during prototype admission; the engine currently requests default limits (`src/gpu/context.cpp:334`). Do not add a new required feature or globally enlarge limits. Auxiliary allocation/binding failure should keep the original path usable.

## Hit-position precision blocker

Original shadow position is `camera.cameraPos.xyz + rayDirFromPixel(gid.xy,dims) * t` (`terrain_raycast.wgsl:1323`). The ray helper normalizes the inverse-view transformed original per-pixel vector (`:590`). The sunlight kernel must use that exact helper, framebuffer dimensions and unchanged camera geometry fields.

Blit's radial view-depth reconstruction uses a different sequence and must not be substituted. R32Float storage of `t` does not itself lose precision, but a separately compiled entry can choose a different multiply/add or normalization lowering. Exact device comparisons are required at cell, stud, DDA and shadow threshold boundaries. If identical helper code fails exactness on Intel, store original `hitPos` as a second auxiliary RGBA32Float/Uint surface rather than loosening tolerance. That adds another 16 bytes per pixel and a fifth geometry storage output if produced with all existing outputs, exceeding the proposed four-output budget. A two-stage write or replacing/reorganizing an existing output would then be needed; this is a material design blocker, not a harmless follow-up.

## Revision/invalidation audit

- Camera position, terrain-space view, inverse view/projection scales, framebuffer dimensions, height/cell scale, terrain dimensions, LEGO/study mode and all current geometry-key fields remain exact. Keep conservative unused fields in the key initially rather than widening the change.
- `setHeightmap`, including same-view calls after an in-place upload, dirties geometry and sunlight. Current setter already invalidates even on repeated handles; preserve that content-update contract.
- `setShadowMap`, including same-view content updates, dirties sunlight and rebuilds bindings; it no longer needs to dirty primary geometry in the cached path. Fallback transitions count as shadow revisions.
- Sun direction and `.w` live-tracing/baked mode dirty sunlight. LEGO mode changes also dirty geometry. Do not resample, quantize or slow the day-cycle clock to get cache hits.
- Water simulation/coast/spectrum replacement retains its existing bind-group updates and direct/composite behavior. Its dynamic surface is never stored as static terrain metadata.
- Allocate replacement auxiliary texture and all replacement bind groups transactionally on resize. A failed optional cache rebuild must not leave old dimensions/metadata with new depth views.
- Moves and shutdown transfer/release the auxiliary, pipelines, groups and dirty states just like existing terrain cache resources.
- `discardEncoding` dirties both geometry and sunlight. Geometry encoded into an abandoned frame must never authorize a later sunlight-only refresh. The current frame guard already calls this hook on unsubmitted frames.
- Preserve `didRefreshStaticCache=true` when either component is refreshed, so Blit invalidates HDR shading after an in-place shadow update. Add distinct geometry/sunlight counters for diagnosis; keep current public semantics and output views.
- Keep GPU terrain depth, material format, water signed-depth/shadow packing, physics heightfield data, collision queries and live-body shadow map behavior unchanged. Screen-space terrain depth consumers receive the same R32 values. The new kernel writes no water output and performs no physics work.

## Cost and acceptance blockers

At 1280×720 the 16-byte auxiliary consumes 14.06 MiB. Every full geometry refresh adds that write traffic. A full-screen sunlight refresh reads 4-byte depth + 16-byte metadata and writes 4-byte shadow, or up to 21.09 MiB before height/shadow loads. At ten lighting steps per second that is about 211 MiB/s of auxiliary/depth/shadow traffic. These are upper-bound format costs, not measured bandwidth. At 3840×2160 the auxiliary alone is 126.56 MiB; framebuffer-scale memory and moving-camera overhead matter.

This favors settled cameras under a running day cycle. A continuously moving/orbiting camera still needs primary geometry every frame and now pays auxiliary write traffic. The prototype must demonstrate that the settled-camera gain is not purchased with a movement/tail regression. Shadow DDA is still expensive; this design removes primary DDA and redundant original depth/material writes only on sunlight-only frames.

## Substantive proof sequence

1. Frozen original-vs-prototype full-kernel fixture: compare original depth R32, shadow R32 and material RGBA16 bytes after a first geometry frame, each sunlight-only frame, and camera/geometry invalidation frames. Pre-fill miss outputs with distinct sentinels to test original non-writes, then include out-of-AABB sky, within-AABB misses and loop-limit sentinel cases.
2. Dense boundary fixtures: stud cap/side/rim and brick edges; `nDotL=0/-0.7072`; topDistance near0.04; low/horizontal/vertical Sun and moon; nonzero camera sectors; signed-zero/near-axis camera rays; smooth versus physical LEGO/study mode; day/night live trace versus baked/fallback-shadow mode. Preserve raw shadow bytes, not only final image tolerance.
3. Actual producer/lifecycle test: prime camera geometry; change only light repeatedly; require primary-DDA dispatch count to stay fixed while sunlight dispatch count increases. Test light then camera, height in-place then light, shadow in-place then light, same-view setters, resize, move, unsubmitted-frame discard/retry and direct↔cached switches. Forced-original refresh provides an image/control oracle; require full HDR, final RGBA and linear-depth bytes at all pixels.
4. Preserve water behavior with real wave evolution, above/underwater/shoreline, water enabled/disabled and both deferred/nondeferred composite routes. Compare terrain-cache depth/material and final water/object occlusion. Include screen-depth primitive consumers and authored/live-body scenes; compare game-state/save results independently.
5. Clean actual-game comparison with normal running day/night, separately stationary and orbit/walk. Record geometry vs sunlight dispatches and GPU timing distributions, not only median FPS. Fixed paused16:00 is a correctness control and intentionally offers little benefit from this cache split. Retain all tail samples and source/artifact hashes.

No promotion until exact per-device shadow geometry proof, engine invalidation tests and measured stationary-versus-moving tradeoff pass.
