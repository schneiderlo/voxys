# Derived village: actual-terrain CPU validation

The installed village now has a walkable Meadow cottage at (−71, −901), an open
Market shelter at (−55, −901), two planters, two brick trees and two paths. Both
shelters use the existing building kit, buried foundation courses and real
stairs. Original stepped gable roof, planter, tree and path meshes use the same
fixed dimensions and proxy definitions as their derived collision. There are
no decorative beds, chests or workbenches that impersonate usable furniture.

On the actual unmodified 8192×8192 terrain, all eight groups admitted:
**44 catalog pieces, 22 props, 66 render instances and 94 collision boxes**.
The raw terrain SHA-256 matched the installed world. Two ordinary controller
walks reached the cottage and market interiors through their actual stairs and
openings. Each route took 87, 62, 26 and 57 fixed steps. No teleport, terrain
flattening, game launch or screenshot was used.

Both interior save-pose checks retained the exact floor and a clear player
capsule when the layout was freshly admitted. A legacy player standing where a
new foundation would go, and a pre-existing building at the same location,
instead deferred the entire conflicting cottage without changing saved state.
Whole-group preservation covers both visual objects and collision. The layout
uses actual collider overlap for player admission; a hollow room's bounding box
cannot reject a player safely standing inside it.

All three NPCs remained at their canonical anchors and accessible with real
range/sight queries; approach walks took 31, 31 and 43 steps. The central five
metre square, recovery spawn and resource access stay clear. The existing
protected-construction radius was not expanded. New or moved player pieces
cannot overlap admitted village solids or claim their reserved IDs. Existing
saved parts take priority during fresh admission. Installed groups, including
deferred groups, remain fixed during a running session, avoiding repeated route
simulation and scenery appearing beneath a moving player.

The final compatibility review added a .75m horizontal walking margin around
actual saved player-part solids during fresh admission. Marker collision was
not expanded. A saved foundation and doorway .12m outside the proposed cottage
wall pass the real restore geometry checks; the new cottage is then deferred
to preserve that old doorway's approach. The single focused regression passed
in 90 ms and kept the old state exact. Its first fixture attempt incorrectly
applied new-placement reach from the distant town spawn; the fixture now uses
the appropriate installed/save geometry validator. Both logs are retained.
No authored anchor or empty-world geometry changed, so the earlier full-terrain
walk was retained rather than repeated. Final source hashes are recorded under
`doorway_clearance_followup` in the results.

The preceding strict focused `//tests:adventure` target passed all 70 cases, including seven
new village tests. The first attempt stopped on missing braces around a test
macro, corrected before execution. The standalone full-terrain probe then
compiled and passed on its first execution. [Results and source hashes](results.json)
retain the exact timings and inventory. Compressed logs/XML are in `checks/`.

Reproduce from the repository root inside the Nix shell:

```sh
bazel test //tests:adventure --test_output=errors --jobs=4
bash build-adventure-g-b/village-checks/run-full-terrain-village.sh
```

The preserved [probe](full-terrain-village.cpp) and [build script](run-full-terrain-village.sh)
expect the verified decoded terrain at `/tmp/voxys-adventure-world.r16` and their
source under `build-adventure-g-b/village-checks/`. This evidence covers actual
terrain geometry and production controller behavior. It does not prove rendered
appearance, GPU performance, native/browser gameplay or owner approval; those
remain integration acceptance work.
