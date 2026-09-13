# Full landscape and house geometry — CPU checks

This records the world/query/controller part of the first home milestone.
It does **not** close WORLD-A01 or G-A: the integrated native/browser journey,
render admission, storage and construction budgets belong to the root gate.
No screenshots or GPU runs were used for these checks.

## Installed landscape and starting place

`world_definition.*` selects the **unmodified full 8192×8192 terrain** at one
metre per sample, height scale 600 and water height −200. The recipe is explicit:
`full-main-unmodified-lego-r01`. Adventure must skip the older filename-triggered
WRECKWATER terrain patch. Both compressed-file and decoded-sample digests are
recorded in [results.json](results.json); runtime hashes the one retained sample
span before admission. No second full terrain copy is retained by these queries.

The starting meadow is source cell (4032, 3200), centered world X −63, Z −895.
Its 33×33 m neighborhood has about 1.10 m raw relief. The two suggested home
centers, X −85 and −41 at Z −895, each have about .32 m of brick-height variation
in their 13×13 m neighborhood. These are suggestions, not enforced plots.

The town protects a visible 5 m radius; the northern landmark protects 2 m.
Other sites remain freely buildable subject to ownership, support, materials,
reach and capacity. The first simple walking route is 80 m north. This is an
initial landmark route, **not** the future 400–800 m populated adventure.

The existing `ldh_tool` decoded the installed asset to a temporary raw file.
Its SHA-256 matched the retained original raw terrain. The
[CPU route driver](full-terrain-walk.cpp) advanced the production adventure
controller from the real town spawn to three waypoints, using ordinary movement
vectors and 1/60-second ticks. It traveled 79.92 m and remained Walking. It used
no teleports, replacement terrain or injected accepted state. Exact coordinates,
366/466/500 segment tick counts and exit 0 are transcribed from the tool output
in the results record. This is not presented as a rendered player journey.

## Shared accepted geometry

`AdventureSpatialQueries` owns one complete revision of up to 8,192 static AABB
solids, tagged with world/structure/part identity and indexed in 256 m sectors.
The limit covers 1,024 six-proxy pieces plus headroom; it is an adapter bound,
not proof of the rendered construction budget. Malformed, stale and overflowing
publication preserves the old packet. Callers publish only after accepted
render/physics resources are ready.

Walking uses upright capsule sweeps and exact LEGO terrain support. Camera
spheres reuse the existing terrain sweep and the same structure boxes. Picking
uses exact box faces and a terrain sweep with a ≤10 micrometre conservative
radius, below the .02 m placement lattice. Support queries take a maximum height
so ground beneath a bridge is distinct from its deck and from an upper floor.
There is no room-sized solid collider, Cove fixed-array expansion or boat slot
requirement. The robot controller reuses the Cove animation modes and camera.

`construction_policy.*` rotates the installed kit's individual proxy boxes in
quarter turns. Foundations/piers can embed their bottoms in existing terrain,
with their upper surface above the highest intersected stud. Other pieces
cannot intersect terrain. The preview computes the complete rectangular terrain
footprint. A deterministic face-contact graph must connect every piece in each
structure to that structure's terrain anchor. Removing essential support fails.
Touching faces need at least .019 m overlap on each tangent axis and at most
.021 m separation. Solid overlaps, occupied player space and blocked protected
access refuse before publication.

A usable bed needs roof coverage at its four corners and center, roof clearance
2.2–6 m above its base, walls in at least three cardinal directions within 8 m,
and a supported, clear capsule beside it. The bed must be above water. This
initial geometric shelter rule accepts 4×4 and 4×6 floor layouts and a rotated
bed. It is not a closed, enemy-proof room or a full weather simulation.
Registered beds are rechecked for recovery, allowing town fallback if altered.
Chest transfers, crafting and gathering require nearby accessible geometry;
gathering cannot pass through a wall. Presentation grants no inventory or health.

## Verification

The latest [strict Bazel test log](bazel-adventure-input-r01-tests.log.gz) and
[XML report](bazel-adventure-input-r01-tests.xml.gz) record **39 passed, 0 failed**
in .219 seconds, including 20 world/query/player/policy/input cases. The
[build log](bazel-adventure-input-r01-build.log.gz) records the actual focused run.
Source hashes for that result are in the results record.

The preceding [traversal test log](bazel-room-traversal-r01-tests.log.gz),
[XML report](bazel-room-traversal-r01-tests.xml.gz) and
[build log](bazel-room-traversal-r01-build.log.gz) retain the 38-case result before
the controller adapter was added.

The earlier [CMake test log](cmake-r06-tests.log.gz) and
[machine-readable report](cmake-r06-tests.json.gz) record **34 passed, 0 failed**
(17 world/query/player/policy cases plus session cases). The
[build log](cmake-r06-build.log.gz) identifies the actual optimized project build.
The earlier [standalone log](focused-tests-r05.log.gz) and
[report](focused-tests-r05.json.gz) recorded 16 passing focused cases before the
optional starter layout and optimized-build correction.
The dedicated CPU executable compiled with the project's Nix GCC, C++20,
`-Wall -Wextra -Wpedantic -Werror`, using the existing GoogleTest libraries.
It covered accepted revision retention, capacity, bridge layers, doorway
clearance, wall picking/camera agreement, cross-sector solids, walking/jumping,
six physical stair treads, restore rejection, grounded support, two house
layouts, bed obstruction, furniture sight/reach, gathering and footprint height.
A pure optional 19-piece
starter layout also passes complete support/shelter checks in all four yaw
orientations. It supplies placements only; the caller must charge, validate and
publish them as one structure. It never creates a free house.

The added `PlayerClimbsCatalogStairsAndTraversesTwoRoofedRoomLayouts` case took
.007 seconds. It validates two different raised rooms, 4×4 m and 4×6 m, made
from the installed catalog's foundations, walls, doorway, roof, bed and actual
six-tread stair piece. The larger room rotates its bed. In each accepted room,
the production `AdventurePlayer` climbs the stairs, enters the opening, follows
an interior aisle, exits and descends back to terrain. The test checks elevated
floor support, clear player volume and final ground contact. It uses fixed CPU
movement ticks; it is not an ordinary-controls native/browser journey.

This distinguishes the earlier tests honestly: the two original bed-layout
cases checked static support/shelter, and the original controller stair case
walked over hand-authored boxes. The new case exercises movement through actual
catalog geometry. The later suite also checks that an idle connected controller
does not take the mouse's building aim and includes the session agent's
1,024-part save/geometry-capacity workload.

`adventure_input.hpp` gives Adventure its displayed controller bindings:
Confirm/A jumps, Tool/X interacts, and the unused Cove Hook binding is cleared
to avoid a conflicting World action. During building, only the movement sample
suppresses Confirm; the Workshop router still receives it for placement, and
keyboard Space still jumps. The added actual-router regression checks these
actions and confirms Back does not jump. This is CPU integration evidence,
not a physical-controller native/browser journey.

During development, an initial blocked-bed fixture left two legitimate recovery
positions open; production correctly allowed recovery. The fixture now blocks
all four candidate positions. Strict compilation also caught misleading
one-line test/helper indentation; formatting was corrected. The final result
above supersedes those failed attempts; no test predicate was weakened.

The initial optimized CMake run then exposed two genuine roof-coverage failures
that the standalone O1 build did not reproduce. Converting local and world
offsets separately allowed sub-ulp cracks on shared roof edges under FMA.
World boxes now rotate and add int64 lattice coordinates before one metres
conversion. The original two house tests pass with the actual O3/FMA build;
no roof coverage tolerance or shelter requirement was relaxed.

Runtime review added full geometry/protected-space restore preflight, candidate
player validation before replacing live state, exact foundation footprint
placement, and .02 m contact-face snapping while preserving tangent stud snap.
Holding Shift uses .02 m horizontal placement for precise corners. Geometry
packet revisions use logical revision + 1 so a new logical revision-zero world
still has a valid nonzero packet identity; overflow refuses.
The root owns final integrated verification of those adapter calls.
