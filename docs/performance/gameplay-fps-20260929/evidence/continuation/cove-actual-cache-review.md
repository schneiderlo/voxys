# Independent actual-Cove cache review

Source review only. No production edits, compiler, GPU test or timing run.
Reviewed the candidate CPU/shader patch and prepared lifecycle fixture.

## Concrete invalidation gap

`setMaterialTexture` updates binding state without invalidating background color.
Both the actual-Cove background and live Cove terrain read that material view for
normal/type-dependent lighting. With an unshadowed cache hit, a replacement view
can leave old lighting cached even though original live Cove would recompute.
Set backgroundDirty in the candidate setter. Compare replacement-view output to
an explicit forced-refresh control, without another incidental dirty trigger.

The dynamic depth/shadow setters need a separate distinction: static and live
cached terrain bindings use staticDepthView/staticShadowView, so merely replacing
the *different* dynamic view does not replace this dependency. However these views
may alias. An in-place update of the shared shadow view followed by the dynamic
shadow setter can expose fresh pixels to original live Cove while the candidate
copies old color. Conservatively dirty depth/shadow setters, or explicitly require
the static-content dirty signal. Test the shared-view mutation case directly.

## Resource/key audit

| Cached input | Current invalidation/ownership | Finding |
| --- | --- | --- |
| Static depth and terrain-shadow views | setStaticTerrainTextures invalidates; in-place raycaster refresh reports didRefreshStaticCache | Proper game path covered; add shared-view mutation control |
| Material normal/type view | setMaterialTexture only rebuilds bindings in reviewed patch | Missing color invalidation; reported to root/render owner |
| Terrain color/alpha | setTerrainTexture invalidates | Covered; in-place mutation needs dirty signal |
| Terrain material albedo and normal/roughness arrays | setTerrainMaterialTextures invalidates | Covered; same-view rebind after write should be tested |
| Lightmap | setLightmapTexture invalidates | Covered; same-view content mutation should be tested |
| Height field and LEGO layout | setWaterCompositeResources/setLegoLayoutTexture invalidate; raycaster handles terrain edits | Covered by declared producer refresh contract |
| Periodic gradient LUT, samplers, procedural foam | Owned initialization resources; no changing backdrop content setter | No new mutable identity issue found |
| Sky LUT and camera/light/fog values | setCameraUniforms/updateStaticUniforms exact comparison; sky rebuild dirties background | Covered in source; add changing sun/cycle and camera/resize controls |
| Filtered specular/diffuse/BRDF content | Fresh sampled views after successful encodeBake; environment callback invalidates | Allocation/publication order appears safe; same-view external mutation remains outside current owner contract |
| Dynamic object shadows | Current shadow consumer feeds live visibility; exact-1 pixels copy the unshadowed cache | Intentionally live; no cached dynamic-shadow key needed |

RaycastPath's cached route writes its material output with the static depth/shadow
refresh, then Application forwards didRefreshStaticCache to BlitPath. Its settled
deferred-water route skips ray dispatch. This avoids a hidden per-frame material
write under the current producer contract. Direct raycasting does not admit the
opaque cached composition path.

Above-water canonicalized water time/offset, presentation exposure, water surface
colors, IOR, foam and spectrum do not feed the cached actual-Cove terrain helper.
Sea level and water enabled remain keys for shoreline material and wet film.
The below-water flag remains a key; caustic time is still evaluated by live Cove
because the private cache-valid flag is disabled below water.

## Lifetime, layout and queue review

- New background layout is consistent with the baseline16 sampled-texture
  limit:12 full static inputs +1 scene-shadow texture +3 filtered textures.
  It does not sample the background texture while that texture is attached.
  The actual layout must still be validated with a baseline-limit device.
- All three fresh sampled environment views are allocated before publishing.
  Old sampled handles remain alive during allocation, then old bind groups retain
  view/texture references. Releasing old owner references after swapping appears
  safe, including abandoned encoding followed by retry.
- New pipeline/layout handles are transferred and cleared in both move paths and
  released during shutdown. backgroundIsCove is transferred. Its nonreset value
  after shutdown cannot admit a stale cache because backgroundValid is cleared.
- The private four-byte camera lane is aligned and does not change CPU/public
  camera state. Queue writes precede command-buffer submission; the current
  single-Blit-render-per-frame contract is required, as with existing full uniform
  uploads. Earlier shader consumers have no other waterMotion.w use.
- Failed/unsubmitted frames must run both BlitPath discardEncoding and fixture
  environment discard. The application guard does so. The explicit lifecycle
  abandon/retry check exercises this contract.
- Valid timestamp endpoint flow looks coherent. Small robustness correction:
  timestampStarted should equal the actual attached-write predicate, rather than
  query!=nullptr when beginQuery is undefined. Add real timestamp tests to ensure
  the new background ordering does not write duplicate/missing endpoints.

## Highest value missing tests

1. Fix material invalidation, then swap material/normal views on a cache hit.
   Require full opaque HDR+depth equality against a forced-refresh frame and a
   meaningful changed-image control. Restore the original view and repeat.
2. Mutate the shared static/dynamic terrain-shadow texture in place; call its
   setter without changing uniforms or terrain-refresh flag. Compare direct
   output with forced refresh. Repeat for same-view material content updates.
3. Replace/modify lightmap and material arrays one at a time through their public
   setters, requiring exact cached-vs-refreshed images and nontrivial controls.
4. Prime the actual cache, change camera/projection and physical extent, then
   compare against a fresh renderer. Return to original state. Include sky and
   terrain boundary pixels, not only the central flat ground texel.
5. Change sea level, enabled water and below-water state around their thresholds;
   vary sun direction/intensity and cycle phase. Compare full pre-presentation
   HDR/depth against forced refresh. The current test covers only ambient,
   exposure and underwater time, at one sampled positive-depth pixel.
6. With timestamps enabled, test dirty refresh, cache hit, admission/Leave and
   underwater particles, requiring valid begin/end writes exactly once.
7. Retain old filtered views/bind groups while rebaking, then sample old and new
   views in ordered commands and require matching backing content. This directly
   proves ownership rather than only comparing pointer identities.

The existing lifecycle test is useful for rebakes, in-place source environment
changes, discard retry, admission/Leave, move and underwater caustics. Its one-pixel
HDR checks do not establish full frame equivalence, viewport-edge neighbor
selection or target-driver half-rounding equivalence. The33-case full-entry
software oracle is complementary; Intel specialization/precision and same-scene
game timing remain unproved. No FPS improvement follows from this review.
