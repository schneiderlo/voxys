# World scenery and material correction

Owner feedback: the initial props looked buggy/glossy against the main terrain,
and a few starting-area clusters did not address the emptiness of the world.
This revision supersedes the local-only placement policy in creative-scenery-r01.

## Confirmed fixes

- Plastic roughness rises from .32 to .66; rocks from .43 to .76. The sun,
  time of day, terrain material, character and bike lighting are unchanged.
- Flat panel/stud-cap normals are restored AFTER final mesh join/triangulation.
  The independent reviewer measured up to ~25-degree incorrect normals in the
  old exported kit. Both final GLBs now pass their independent measurement:
  4,308 near and 472 far horizontal triangles, zero bent-cap failures. The
  cooker enforces |Ny| > .9999 on horizontal triangles to prevent recurrence.
- Pine trunk now connects the upper foliage tiers (previously floating).
- Crate ribs stop below the body top, removing differently colored coplanar
  faces that could flicker. Small rock tops now support the complete stud.

## World coverage and stability

- Natural scenery is generated from stable, hashed 24-stud world cells across
  the full landscape. Coarser empty patches, terrain relief, altitude and water
  rejection preserve clearings and prevent planting underwater or on cliffs.
- Authored starting-area props remain. A 72-stud exclusion reserves their whole
  layout including crown widths and local ground-search offsets. This prevents
  overlapping trees when entering the area from a different streaming direction.
- Every 32 studs of player travel, rebuild the bounded nearby list (320-stud
  admission radius, at most 640 props). No whole-map object allocation or
  per-frame landscape scan. Trees draw to 280 studs; rocks to 140, flowers to 80.
- Lightweight distant meshes start at 80 studs: 4,684 triangles for the whole
  far kit versus 36,948 near. Distant trees keep their silhouette without tiny
  studs. Near shadows remain enabled inside 110 studs. This is hard-distance
  LOD/culling, not an occlusion system or an infinite visible forest.
- IDs depend on world cells, never load order. Unloaded props can return;
  construction-suppressed props stay absent for the running session. Saved
  construction and door swings are always reserved. New props also defer to
  the player's body and parked/ridden bike envelope.
- Render/collision packets publish together, with geometry-revision and cached
  aim/preview invalidation. Streaming recompiles accepted owned geometry without
  re-running its full terrain/support validation. Movement and save identity are
  unchanged. Collider shapes do not change with visual LOD.
- The expanded draw budget is 8,192. Scenery alone is bounded at 640 * 3 material
  draws; existing construction retains its budget. Collision admission yields
  props when the 8,192-solid limit is reached, preserving owned builds.

## Evidence

27 world tests, 15 door tests and both runtime integration tests pass; browser
build succeeds. Two new travel tests cover stable IDs/positions after leaving
and revisiting a region, persistent suppression, parked-bike clearance, bounded
admission and the actual 8192² terrain in four separated regions:

| World X/Z | Nearby admitted props |
| --- | ---: |
| 1200 / -1120 (start) | 186 |
| 1500 / -1100 | 93 |
| 900 / -1000 | 215 |
| -63 / -895 | 89 |

These are admitted counts inside the nearby streaming area, not simultaneous
on-screen counts. Occlusion/viewpoint, distance and terrain affect visibility.
The independent review also checked streaming revisions, collision ordering,
identity handling and the new-prop clearance policy. Its initial shading and
load-order findings were corrected before the final build.

Sources remain editable Blender/GLB files in data/adventure/creative-props-r01
and data/adventure/creative-props-far-r01, with strict manifests/provenance.
Future rebuilds use author_creative_props.py with --lod 0 or --lod 1, followed
by cook_creative_props.py. Runtime embeds each cooked file's size/hash, which
must be refreshed when either package changes.

Remaining acceptance: owner play feedback, frame-time measurements alongside
large builds, smoother distance transitions, and world-level composition polish.
This revision does not complete F1/F2 or constitute deployment/gate approval.
