# Cove environment: authored kit and playable route

The new coastal workshop environment has passed its scoped native traversal and save compatibility checks. Acceptance combines **six real-control stages in r04 and one read-only restore continuation**. The r04 driver itself remains marked **failed** because its already-ready second process did not pass an unnecessary keyboard-focus wait. The continuation makes no focus or input claim. This is not final LOOK-01 approval, final package approval, or a full-suite result. [Native result](environment/native/final-summary.json)

The composition uses an open cream workshop with teal panels and coral details, four brick stairs, two navigation beacons, a visibly broken inert wreck, and low stone/green shore pieces. The powered gantry keeps its existing functional opening. The installed palette, all source/cooked identities and geometry measurements are recorded in the [manifest](environment/assets/manifest.json), [provenance](environment/assets/provenance.json.gz), [geometry report](environment/assets/geometry-check.json) and [21-file installed hash list](environment/assets/installed-r01.json).

## Geometry and collision contract

All models use the explicit render-to-canonical basis12. Placement remains at Cove scene origin **(−19,−200,−37)**. Collision coordinates are canonical scene-local metres, with integer authoring ticks of0.02m.

- Workshop foundation: X10–16, Z−42 to−36, topY2.56m. Four0.50m treads rise0.32m each, spanningX8–10 and Z−42 to−40; their tops are1.60,1.92,2.24 and2.56m. The west approach stays open.
- East wall: X15.76–16, Z−41.68 to−36.32, Y2.56–5.44. The stepped roof reaches collisionY6.72. Posts, rear wall and service bench have explicit matching solids.
- Scenery contributes39 exact boxes. New solids avoid X[−9,8], Z[−77,−47], preserving the berth, generator, delivery area and boat route. The inert wreck ends atX−10.5; it grants no salvage interaction or inventory identity.
- The gantry retains its existing **ten** collision boxes and functional anchors. Posts reachY7.04 and perimeter beamsY7.44. Raised visual trim extends12mm. Rendering is enabled by the actual installed harbor state.

Each indexed vertex in every LOD must stay within a declared solid plus **2cm** visual allowance; each declared collision face must have actual indexed-vertex support. These checks bound model/contact correspondence. They do not prove every terrain approach or every possible pose. The actual route below validates the new stair/workshop approach. [Independent review](environment/review/independent-review.md)

## Source, loading and ownership

The original, reproducible [Blender recipe](../../../../../tools/salvage_assets/author_cove_harbor_art.py) creates six source pairs without thumbnails or raster textures. The [checker](../../../../../tools/salvage_assets/check_cove_environment.py) validates normals, bounds, openings and contact support. The [cooker](../../../../../tools/salvage_assets/cook_cove_environment.py) uses the existing strict `salvage-rigid-v1` converter. Installed source and cooked files live in [the environment package](../../../../../data/salvage/cove-environment-r01/manifest.json).

The [asset loader](../../../../../src/game/assets/cove_environment.hpp) uses capped, no-follow snapshots:64KiB manifest and1MiB perVMESH. It validates fixed filenames/schema/digests before mesh admission, then validates all six rigid prefabs, named roots, collision contact and resource counts. `prepareCoveEnvironment(input,output,error)` repeats admission from actual mesh data at renderer ownership; caller-supplied cached counts/prefabs are never authority. Refusal preserves prior output.

Source positions/contact remain bounded to100m. Only after those checks succeed does each prefab receive the existing100000m runtime placement allowance, so negative camera sectors and a camera2km away do not cause a fatal draw error. The [sector regression](environment/cpu/cpu-sector-r02.xml) reconstructs the same world geometry in these frames and still refuses an invalid101m source vertex.

The [collision compiler](../../../../../src/game/expedition/cove_environment_collision.hpp) prepares one bounded compound shape from the39 solids. Its unit reference mass is a representation detail: App admits the body as **Static**, separately from the old dock, owned craft and cargo. No fake canonical parts, save identities or new physics authority are introduced.

[CovePlayer](../../../../../src/game/expedition/cove_player.hpp) transactionally combines up to48 environment boxes with up to11 existing harbor/base boxes. Replacing either set preserves the other. All movement, support and camera queries use the combined set. [Restore preparation](../../../../../src/game/expedition/cove_restore.hpp) applies the environment before validating saved occupancy and harbor state.

[Application integration](../../../../../src/app/application.cpp) retains partial shape uploads on bounded backpressure. The new static lifetime records its scheduled admission/removal ticks, ignores only pre-admission observations, requires exact live body/shape identity afterward, and waits for completed post-removal death before retirement. [Admission correction and limits](environment/review/environment-admission-review-r01.md)

Build registration is explicit in [game targets](../../../../../src/game/BUILD), [CMake sources](../../../../../src/CMakeLists.txt), [asset runfiles](../../../../../data/BUILD) and [focused/aggregate tests](../../../../../tests/BUILD). The focused CPU targets are `//tests:cove_environment` and `//tests:cove_environment_collision`; movement cases belong to `//tests:cove_player`. Native uses the same core source/data registration as WASM; this report records only the linked native build used for its actual route.

The [fixture owner](../../../../../src/render/salvage_asset_fixture.hpp) holds a newly admitted immutable CPU copy and all six GPU uploads through its existing generation, submission/discard and retirement protocol. LOD uses actual scene bounds. Scenery shares lighting, depth and shadows; it remains outside the part catalogue and hides during workshop inspection.

## Resource limits

| Payload | Near GPU bytes | Middle | Far | Selected draws |
|---|---:|---:|---:|---:|
| Scenery, five roots |427,520|181,760|121,024|20 /20 /19|
| Gantry, one root |298,624|126,592|86,368|4 /4 /4|

All six resident LODs total **1,241,888 GPU payload bytes**, under the2MiB environment limit. The selected pair uses6 mesh instances and24 draws, or23 at far detail, under its60-instance/90-draw bounds. No texture/image allocation is added. Static-shape storage remains in the existing authored-physics owner.

The recorded unpowered native route submits20 scenery draws,39 confirmed environment proxies, a33,328-byte external effects reservation and **11,411,160 total fixture-owner bytes**. Its20 scenery draws exclude the inactive gantry. Shared fixture limits remain256 instances,512 draws,16MiB per owner and48MiB retained aggregate. These are scoped recorded values, not a claim that future content fits automatically. [Runtime packets](environment/native/r04/summary.json.gz)

## Checks and retained failures

| Check | Evidence and result |
|---|---|
| Original asset/compound/player checks |[Summary](environment/cpu/cpu-final-summary.json):10 CPU cases passed across the recorded runs. [Asset XML](environment/cpu/cove_environment-final.xml), [compound XML](environment/cpu/cove_environment_collision-final.xml), [player/restore XML](environment/cpu/cove_player-final.xml).|
| Sector placement correction |[Summary](environment/cpu/cpu-sector-r02-summary.json):6 asset CPU cases passed, zero skips.|
| Stair correction |[Summary](environment/cpu/stairs-cpu-final-summary.json):2 final stair cases plus4 affected existing movement cases passed on the unchanged production correction. [Final XML](environment/cpu/stairs-cpu-r04.test.xml), [compatibility run](environment/cpu/stairs-cpu-r03.log.gz).|
| Native build used for route |[Successful build log](environment/build/stairs-native-build-r01.log.gz); [actual route binary/config/shader hashes](environment/build/route-inputs-r01.json). Subsequent startup-camera and PCF work is outside this binary/source claim.|

The stair fix preserves exact capsule contact and the3.6m/s horizontal speed. At a rounded top edge, support uses an actually contacted walkable box face; a step attempt still caps contact height to the original feet plus36cm. Tests cover all four32cm steps, standing/restore/descent, per-tick horizontal travel≤0.060001m, a40cm refused riser, continued pressure beneath a low ceiling without penetration or full ascent, and a54-degree refused face. [Controller review](environment/review/stairs-controller-review.md)

Failures remain available, rather than overwritten:

- Initial authoring exceeded the visual envelope by4mm on one bench pull. Only that pull moved inward. [Failure](environment/assets/geometry-r01-failure.txt), [corrected geometry](environment/assets/geometry-r02.json), [Blender log](environment/assets/blender-r02.log.gz), [six strict cooks](environment/assets/cook-r01.log.gz).
- Initial CPU camera assertions omitted a required terrain-query callback; a later sector test initially had a duplicate trace-macro compile error. These were test-only corrections. [CPU history](environment/cpu/cpu-r01.log.gz), [sector compile history](environment/cpu/cpu-sector-r01.log.gz).
- [Native r01](environment/native/r01/summary.json.gz) exposed the source/runtime placement-limit defect. The separate [startup exit](environment/review/initial-admission-exit.log.gz) exposed the missing new-body admission boundary; source evidence explains the distinction.
- [Native r02](environment/native/r02/summary.json.gz) reproduced a real stair stall atX8.195/Y1.605. The [CPU reproduction](environment/cpu/stairs-cpu-r01.log.gz) matched it. Later CPU ceiling-oracle corrections are preserved in the stair summary; production geometry or clearance was not relaxed.
- [Native r03](environment/native/r03/summary.json.gz) climbed the stairs, then exposed the driver’s incorrect34-draw expectation at an intentionally hidden near-wall avatar. Only that wall capture now permits0 draws when actual camera distance<0.65m. All other records require34, with explicit visible-return validation. [Correction](environment/review/stairs-native-r03-driver-review.json).

## Native acceptance and reproduction

[Native r04](environment/native/r04/summary.json.gz) completed six real-control stages: oldACTv6 resume, approach, stair entry, actual wall contact, camera clearance/visible return, and durable save inside the workshop. Its overall driver status is **failed**: the second process was ready with the correct restored workshop position, but an unnecessary focus wait timed out. Both processes exited through requested SIGTERM, with no force kill or error lines. The original ACT archive remained untouched.

The [read-only continuation](environment/native/restore-r01/summary.json.gz) copied that verified durable checkpoint and passed the remaining restore proof in **1.801seconds**, with **zero input actions and no focus requirement or focus claim**. It preserves exact owned part/connection/settings bytes, material48/machinery0, all11 starter parts, durable build/root identities, walking feet **(14.8265,2.565,−39.5202)**, orbit distance5.5, reduced-motion and load-view preferences. Each record requires a later completed GPU submission and physics tick tied to the same owner. The process closed cleanly and its source archive stayed unchanged. The actual saved payload is [workshop.svce](environment/native/r04/workshop.svce).

Run [the driver](../../../../../scripts/validate_native_cove_environment.py) from the repository root, using new output/storage directories and a native GPU/display environment:

```sh
python3 scripts/validate_native_cove_environment.py \
  --binary PATH_TO_VOXY_NATIVE --source-slot RETAINED_ACT_V6_WORLD_DIRECTORY \
  --storage-root NEW_STORAGE --output NEW_OUTPUT --width 1280 --height 720
```

The source slot must contain the retained real dock checkpoint’s `current`/`mirror` files. The bounded route uses actual keys; its final restart is read-only and does not require keyboard focus. Default execution captures no images.

To finish only a retained successful workshop checkpoint after an unrelated harness interruption:

```sh
python3 scripts/validate_native_cove_environment.py \
  --binary PATH_TO_VOXY_NATIVE --source-slot VERIFIED_WORKSHOP_WORLD_DIRECTORY \
  --restore-only-from PRIOR_ROUTE_SUMMARY_JSON \
  --storage-root NEW_STORAGE --output NEW_OUTPUT --width 1280 --height 720 --seconds 60
```

That mode validates the source against the prior recorded durable checkpoint and performs only the final read-only proof. It cannot create a substitute world or place the player through a state setter. The [evidence index](environment/index.json) binds every archived source/copy digest. Final visible composition, owner approval, browser acceptance and required joint-suite publication remain separate work.
