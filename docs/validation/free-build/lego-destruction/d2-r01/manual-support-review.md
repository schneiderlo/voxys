# Independent manual support-removal review

## Accepted bounded behavior

The real imported house now supports two distinct operations. Releasing its connections preserves physical bearing masonry, so remaining at rest is correct. Explicitly removing one named support brick can make the surviving brick above it fall under gravity. These are manual controls, not projectile-triggered destruction.

Root's actual-world regression passes both variants (`manual-support-world.log/.xml`):

- Connection release: certified settled after 52 ticks; zero vacated original cell centers is permitted because bearing supports remain.
- Support removal: certified settled after 72 ticks; 14 original collision-cell centers become vacant, including 5 inside the surviving upper brick's original body volume.
- Both variants exercise rebuilding, restored queries, save recovery, leaving the cannon, and ordinary building afterward.

The independent owner GPU test also verifies support removal makes a surviving part drop by approximately one brick height, preserves another instance sharing the same mesh node, and restores the original source instances on reset. No extra impulse, forced sleeping, teleport or collision-only hole is used.

## Source identity and ownership

The immutable original contains 39 source parts. Manual removal targets only source `415964676630277730`, a 3005 brick at source translation `(6.600431, 12.399989, 2.461079)`. Its surviving upper neighbor is source `409735568841264200`, at `(6.600431, 13.599989, 2.461079)`.

Removal produces 38 surviving source instances and removes the named part's incident bonds. Eligibility is captured from the original graph before removal: 28 surviving eligible parts become dynamic; the other 10 remain fixed. Remaining eligible internal connections are cut, including the upper brick's connection to the plate above. The complete original remains available for rebuilding.

Both target instances share mesh node 2. Rendering iterates stable source bindings, so omitting one source does not hide the shared geometry globally. The source remainder was already exported without all 39 selected instances. It cannot leave a second intact copy of the removed brick behind.

Graph preparation is immutable and validates identities/revisions. A rejected preparation retains the accepted graph. An admission failure before execution retains the original render bindings. Render bindings switch at the encoded mutation boundary; collision/query publication waits for certified settled poses. Save/load remain blocked while the wall is changed, and rebuilding restores all 39 source parts.

## Review correction and routing

The first native volume assertion incorrectly sampled above the upper brick's top. Its source translation denotes the top; the body occupies top minus 1.2 through top. The corrected assertion uses the interior band `top - 1.15 < y < top - 0.05`, with the matching x/z footprint. Removed support cells cannot satisfy that condition. Vacated volume there is evidence that surviving material moved, rather than merely that the manually removed support disappeared.

Action 37 is included in the public action bound, cannon action allowlist and guarded switch dispatch. Its implementation requires cannon use, no menu, visible house, valid physics and an intact wall. Repeated removal is refused. Native HUD now exposes the removal control only before a wall change and disables both release controls until admission is ready; changed walls offer rebuilding instead.

## Limits that remain open

This is not a player-sized walkable opening, and does not close the full D2 walkable-opening gate. Cannon impact still does not select or break these bonds. Moving debris queries remain constrained by the existing cannon/player lock and certified static promotion. Wall changes are session-only. Eight contact patches and the selected solver budget have bounded tests; whole-world performance acceptance is separate. This review does not claim browser visual acceptance or production deployment.
