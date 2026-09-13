# G-A building kit: content and integration review

**Status: content checks and source review passed; playable G-A remains open.**
Date: 2026-09-12. Base commit: `acd9d43d224734a633c3af5db5d6d519e6bc3e23`.
The containing implementation commit identifies the eventual published source.
This record does not certify a home journey, frame rate, or human art approval.

## Delivered content

Fourteen original pieces support the first useful home: foundation, floor,
wall, open doorway, flat roof, stairs, beam, three brick sizes, bed, chest,
workbench and pier. Costs and **30 separate collision boxes** come from one
[canonical catalog](../../../../../data/adventure/building-kit-r01/catalog.json).
The generated C++ catalog uses that exact source fingerprint. There is no
room-filling aggregate collider. Door clearance is 1.36 × 2.24 m; stairs have
six .16 m risers. The 1.7 m robot's dimensions are unchanged.

The package includes two editable Blender/GLB sources, two cooked VMESH LODs,
strict-cook results, numerical checks and original-art provenance. It uses
five shared opaque materials: cream, teal, wood, coral and slate. No external
models, image generation or screenshots were used.

| Identity | SHA-256 |
|---|---|
| Canonical catalog | `fc9c763b22b5ef9d81e461608dea0f1b138d8f16a4940e9378442dc18a611150` |
| Near VMESH | `08ce2ff933bdc9d86d68d62e29b15f62d7f22984753f20b51fdb89348fd3236b` |
| Far VMESH | `f95cccd5f23bfbdf483c05a10707cd1fc8880bf9715297baad56bb9fe7f1ea34` |
| Cook manifest | `09702dab193db47786dd9d09466799f00c71138f6b7e306935a739fcebe2492c` |

| Payload | Near | Far |
|---|---:|---:|
| File bytes | 1,116,054 | 525,174 |
| Vertices | 14,908 | 6,984 |
| Indices | 20,100 | 9,924 |
| Meshes / submeshes | 14 / 29 | 14 / 29 |
| Requested geometry/material GPU bytes | 1,154,096 | 542,864 |

See the [manifest](../../../../../data/adventure/building-kit-r01/cooked/manifest.json),
[geometry report](../../../../../data/adventure/building-kit-r01/cooked/geometry-check.json)
and [authoring/reproduction contract](../../../../../tools/adventure_assets/README.md).

## Checks and integration findings

The generated catalog compiled with the repository GCC environment using
C++20, `-Wall -Wextra -Werror`. The canonical-source checker passed. The asset
checker confirmed stable piece IDs, canonical identity node transforms, bounded
vertices/materials/draws, unit normals, visible support for every collision
face, visual containment and triangle clipping against the empty doorway.
Source, author recipe, cook recipe, checker and installed hashes agree.

An independent byte-level winding pass checked **all 6,700 near and 3,308 far
triangles**. Each triangle's geometric normal agreed with its vertex normals;
all fourteen mesh volumes were positive at both LODs. Raw results and reviewed
source hashes are in [source-review.json](source-review.json). Canonical basis
is **0**, not the old Cove exporter basis 12. Runtime positive-Y quarter-turn
matrices map `(x,y,z)` to `(z,y,-x)`, matching collision compilation. The existing
left-handed camera reverses projected outward winding, so runtime `frontFace=CW`
is the correct established mesh-path setting.

The integration review found and corrected:

- The Bazel manifest path pointed outside `cooked/`. It now selects the actual
  installed manifest. CMake now preloads that manifest and copies `adventure.cfg`
  into the native executable directory.
- The generic mesh loader did not bind render bytes to installed geometry.
  Adventure startup now reads at most 1,116,055 bytes, requires exactly 1,116,054
  and checks the compiled near-LOD SHA-256 before VMESH parsing and GPU upload.
- The initial instance allocation counted pieces instead of expanded submesh
  draws. It now reserves 4,608 records, **589,824 bytes**, before gameplay.
- The default mesh destructor could Destroy fallback textures during an
  interrupted shutdown. Adventure now calls `releaseHandles()` first, leaving
  submitted commands' references valid. The HUD already uses release-only
  teardown. The normal mesh buffer-growth helper was inspected separately: it
  already released references without Destroy; no growth lifetime defect is
  claimed here.

The browser companion passed [eight initial UI checks](ui-tests-r01.log), then
[nine initial lifecycle checks](ui-tests-r02.log) after rejecting an empty/inactive runtime
JSON object. Checks cover read-only polling, placement intent bounds, refused
operations, row eligibility and stable focus, save-error/dirty feedback,
inactive-state refusal, distinct New/Continue routes and accessible controls.
Run `node tools/adventure_assets/test_adventure_ui.mjs` to reproduce. This small
DOM model proves intent routing and lifecycle, **not browser layout or play**.

The follow-up UI adds a separately paid Starter room button, explicit DOM focus
ownership, keyboard event isolation, and accessible routes to the existing Cove
and playground. It reuses the Cove save helper's validated Continue link without
reading or duplicating its save metadata. The observation limit is 512 Ki
characters so the full 1,024-part/32-component world remains operable. Chest menus
allow all 64 transfer rows (32 backpack slots plus 32 chest slots). The focused
model now has 16 checks, including full-budget packets and the last chest row.
See [ui-tests-r07.log](ui-tests-r07.log) and [ui-followup-source-hashes.json](ui-followup-source-hashes.json).

An isolated real Chromium DOM regression also passed all five keyboard/focus
stages: Enter sends one menu choice, Tab/Space retain browser behavior, Escape
releases ownership and closes only an open game menu, and canvas/cleanup release
ownership. This explicitly used a UI-boundary test double, disabled GPU rendering,
and did not load the game. See [ui-dom-browser-r01.json](ui-dom-browser-r01.json).

## Current limits

The runtime currently loads the near kit once; the far LOD is authored and
packaged but not selected during play. Near geometry/materials use 1,154,096
GPU bytes; 1,696,960 bytes is the sum **if both LODs are loaded**, not a measured
current-runtime total. Robot, HUD, shadow textures, terrain, staging, driver
memory and full WASM heap are additional costs.

At 1,024 accepted parts and the 32-component limit, the kit needs at most 2,112
part submesh draws. Eighteen resource piles add 36, two markers add four,
the 19-piece starter preview is conservatively bounded by 76 (four per piece),
and the installed robot adds 34: **2,262 color draws** before other future
content, below the 4,608 limit. Shadow draws are additional. This arithmetic is
capacity evidence, not a frame-time measurement. With sun shadows enabled,
the current MeshPath submits every structure; region/instance batching,
far-detail selection and displayed performance remain unverified.

Floors and stair treads have no raised decorative studs. Brick studs extend
.18 m above the .96 m body and are cosmetic insertion geometry; collision and
stacking use the body. All other trim stays within .02 m of an actual solid.
The preview uses colored opaque material; tint alpha alone does not make it
translucent. General sloped roofs, working closed doors, house collapse and
enemy-proof homes are not supplied by this kit.

This original content review required no GPU run or full suite. Subsequent
ordinary-control browser journey attempts are recorded separately and must not
be confused with the numerical asset or isolated DOM checks here. Root owns the
final build and decision to complete any game gate.
