# Village visual revision — 2026-09-17

Status: implementation and independent review in progress. Owner acceptance and displayed-frame performance gate remain open.

## Scope

Improve the village against `docs/design/free-build/village/owner-reference.png`.
Keep the current minifigure, bike, Shift running, free construction, fixed terrain,
and warm low sun. No quests, NPC systems or adventure progression are added.

## Implemented

- Eleven houses using four silhouettes: cottage, covered porch, taller gable and side-wing workshop.
- Sloped individual roof tiles, deep eaves, gables fitted to roof pitch, window frames/shutters, flower boxes, masonry base courses and connected brick chimneys.
- Taller slate tower/spire and windmill, two striped market stalls, well, eighteen crop beds, low hedges, flowers, barrels and secondary footpaths.
- Rebuilt broadleaf crowns/conifers and fourteen candidate trees in village edge clusters. Actual tree admission still checks terrain, construction, doors, player and bike clearance.
- Full paths follow terrain terraces. Flat patches use one mesh; patches across height changes split into four smaller pieces.
- Near + far sun-shadow atlas: near1024² over48studs, far2048² over384studs in3072×2048 atlas. Free-build opt-in; +20MiB depth allocation versus near-only. Trees now cast throughout their visible range.
- Matching sky/ground indirect fill gives authored materials the terrain’s directional volume without changing sun, exposure or colour. Free-build opt-in; no extra textures or draws.
- Renderer batches compatible consecutive repeated geometry/material instances, preserving individual transforms/paint, transparent ordering and shadow eligibility. The village kit has84 unique subdraws; this is not a claim about total scene draw count.

## Asset identity

`data/adventure/creative-village-r01/creative-village.vmesh`:

- SHA-256 `7c3703c12d6b5bcf1a384c21a98de5376e36ff24033dfeceb7d621bdc8f3242f`
- 15,529,402 bytes;173,651 vertices;251,804 triangles;84 subdraws;15 materials/meshes.
- Strict cooker verifies source hashes, bounded file/geometry counts, unit normals, planar cap normals, node identity and per-mesh bounds. Collision header comes from the same authoring boxes.
- Editable Blender/GLB sources, provenance and manifest accompany the cooked mesh.

Rebuild with `tools/adventure_assets/author_creative_village.py` in Blender, then
`tools/adventure_assets/cook_creative_village.py`. Install the generated collision
header and update `installedCreativeVillageMesh` byte/hash pins together.

## Review corrections

The first rich pass was rejected by independent reviewers: repeated foreground roofs hid the square, market and farm connections.
The second pass exposed slate roofs and larger plots but introduced overly tall roadside hedges and visibly floating chimneys. The next revision lowers/breaks up the hedge and extends each chimney down through the roof. A rear footpath crossing a hedge and a market/crop canopy overlap were also corrected.

## Validation still being completed

- Final real-terrain walking, all cottage entrances, bike lane and construction precedence checks.
- Final browser build and actual composed game views at overview and pedestrian scale.
- Independent visual verdict after the corrections.
- Displayed village-plus-large-player-build frame-time measurement remains a separate open performance gate. GPU unit checks or hidden preview capture are not frame-rate evidence.
